#!/usr/bin/env python3
"""Static translator for the game's hand-written ARM/Thumb routines.

The m4a sound engine core (src/m4a_1.s: SoundMain, the SoundMainRAM mixer,
MPlayMain, the ply_* track commands, m4aSoundVSync ...) and the two link-cable
helpers in src/arm_funcs.s are hand-written assembly.  For the host/Vita port
they are translated instruction by instruction into C: every ARM register
becomes a 32-bit variable, the CPU flags are computed exactly as on the
ARM7TDMI and every branch becomes a goto.  The result behaves like the original
code by construction, on any 32-bit target.

Input is the object file assembled with arm-none-eabi-as (after the C
preprocessor); the instructions are read back with objdump (canonical syntax,
numeric operands) and the relocations / symbols with pyelftools.

Derived from the translator of the Metroid Zero Mission port (mzm-vita) and
extended for the m4a engine:

  * "cores": one C function per entry point (a function symbol that C calls,
    or a local label that is the target of a `bl`).  A core contains every
    instruction reachable from its entry (fall-through, branches, `bx` to an
    `adr`/literal code address), so tail-calls (`b ply_goto`), jumps into the
    middle of another routine (`b ply_goto_1`), fall-through into the next
    routine (ldrb_r3_r2 -> chk_adr_r2) and the SoundMain -> SoundMainRAM
    hand-over (`bx r3` through the RAM copy's address) all end up as gotos in
    the same C function.
  * internal `bl` calls pass the complete register file (struct AsmRegs), so
    non-standard register conventions (ChnVolSetAsm reads r4/r5,
    ld_r3_tp_adr_i returns r2/r3) are preserved by construction.
  * `bl` to a `bx rN` trampoline (call_r3 / call_r3_) is an indirect call
    through a C function pointer (r0-r3 as arguments, r0 as the result).
  * external `bl` targets and the C entry points get their real prototypes
    from the game headers (--protos / --proto).
  * "tagged" registers (--tag Func:rN): the PCM mixer counts loop iterations
    in the top two bits of the output pointer (`adds r5, r5, #0x40000000` +
    `bcc`).  Host pointers have those bits set, so the tag bits of such a
    register are kept in a separate variable (see TaggedReg below).

Only the instruction forms that occur in these files are supported; any other
form aborts the translation instead of silently producing wrong code.
"""
import argparse
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

REGS = {"r%d" % i: "r%d" % i for i in range(13)}
REGS.update({"sl": "r10", "fp": "r11", "ip": "r12", "sp": "sp", "lr": "lr", "pc": "pc",
             "r13": "sp", "r14": "lr", "r15": "pc"})

CONDS = {
    "eq": "z", "ne": "!z", "cs": "c", "hs": "c", "cc": "!c", "lo": "!c", "mi": "n", "pl": "!n",
    "vs": "v", "vc": "!v", "hi": "(c && !z)", "ls": "(!c || z)", "ge": "(n == v)", "lt": "(n != v)",
    "gt": "(!z && n == v)", "le": "(z || n != v)", "al": "1",
}

# Data symbols whose *address* is the value (linker-script constants).
VALUE_SYMBOLS = {"gNumMusicPlayers", "gMaxLines"}

# A literal pool word that refers to one of these symbols is a code address of
# the function on the right (SoundMain jumps to the IWRAM copy of SoundMainRAM;
# in the port there is no copy and the jump goes straight to the routine).
CODE_ALIASES = {"SoundMainRAM_Buffer": "SoundMainRAM"}

R_ARM_ABS32 = 2
R_ARM_THM_CALL = 10
R_ARM_CALL = 28
R_ARM_V4BX = 40

TYPE_WORDS = {"u8", "u16", "u32", "s8", "s16", "s32", "void", "char", "int", "short", "long",
              "unsigned", "signed", "struct", "const", "bool8", "bool16", "bool32", "vu8", "vu16",
              "vu32", "MPlayFunc", "PlyNoteFunc"}


class Insn:
    def __init__(self, addr, mnem, ops, comment, thumb):
        self.addr, self.mnem, self.ops, self.comment, self.thumb = addr, mnem, ops, comment, thumb
        self.size = 0


def die(msg):
    sys.exit("asm2c: " + msg)


def split_ops(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def is_reg(r):
    return r.strip() in REGS


def reg(r):
    r = r.strip()
    if r not in REGS:
        die("not a register: %r" % r)
    return REGS[r]


def imm(s):
    s = s.strip()
    if not s.startswith("#"):
        die("not an immediate: %r" % s)
    return int(s[1:], 0) & 0xFFFFFFFF


def reglist(s):
    s = s.strip()
    if not (s[0] == "{" and s[-1] == "}"):
        die("bad register list %r" % s)
    out = []
    for part in s[1:-1].split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            a, b = int(reg(a)[1:]), int(reg(b)[1:])
            out += ["r%d" % i for i in range(a, b + 1)]
        else:
            out.append(reg(part))
    order = lambda r: {"sp": 13, "lr": 14, "pc": 15}[r] if not r.startswith("r") else int(r[1:])
    return sorted(out, key=order)


# ---------------------------------------------------------------------------
# C prototypes (for the entry-point wrappers and the external calls)
# ---------------------------------------------------------------------------
class Proto:
    def __init__(self, ret, name, params):
        self.ret, self.name, self.params = ret, name, params  # params: list of (type, name or None)
        self.override = False  # --proto that differs from a header declaration

    @staticmethod
    def parse(text):
        text = text.strip().rstrip(";").strip()
        m = re.match(r"^(.+?)\s*\b([A-Za-z_]\w*)\s*\((.*)\)$", text)
        if not m:
            return None
        ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
        if ret.startswith("typedef") or ret.startswith("extern") and "(" in ret:
            return None
        ret = re.sub(r"^extern\s+", "", ret)
        plist = []
        if params and params != "void":
            for p in params.split(","):
                toks = re.findall(r"\w+|\*", p.strip())
                if not toks:
                    return None
                if len(toks) >= 2 and re.match(r"^\w+$", toks[-1]) and toks[-1] not in TYPE_WORDS \
                        and toks[-2] != "struct":
                    plist.append((" ".join(toks[:-1]).replace(" *", "*").replace("* ", "*"), toks[-1]))
                else:
                    plist.append((" ".join(toks).replace(" *", "*"), None))
        return Proto(ret, name, plist)

    def ctype(self, t):
        return t.replace("*", " *")

    def wrapper(self, core):
        decl, args = [], []
        for i, (t, _) in enumerate(self.params):
            decl.append("%s p%d" % (self.ctype(t), i))
            args.append("(u32)(uintptr_t)p%d" % i if "*" in t else "(u32)p%d" % i)
        if len(args) > 4:
            die("%s: more than 4 parameters not supported" % self.name)
        cname = self.name
        out = []
        if self.override:
            # The header declares this routine differently (e.g. SoundMainBTM
            # as (void) although the jump table calls it with a pointer): the C
            # name differs, the linker symbol stays the same.
            cname = self.name + "__asmwrap"
            out.append("%s %s(%s) __asm__(\"%s\");" % (self.ret, cname, ", ".join(decl) if decl else "void", self.name))
        out += ["%s %s(%s)" % (self.ret, cname, ", ".join(decl) if decl else "void"), "{",
               "    struct AsmRegs R_ = {{0}};", "    u32 stack_[ASM_STACK_WORDS];",
               "    ASM_REGS_INIT(R_, stack_);"]
        for i, a in enumerate(args):
            out.append("    R_.r[%d] = %s;" % (i, a))
        out.append("    %s(&R_);" % core)
        if self.ret == "void":
            pass
        elif "*" in self.ret:
            out.append("    return (%s)(uintptr_t)R_.r[0];" % self.ret)
        else:
            out.append("    return (%s)R_.r[0];" % self.ret)
        out += ["}", ""]
        return out

    def call(self):
        if len(self.params) > 4:
            die("%s: more than 4 parameters not supported" % self.name)
        args = []
        for i, (t, _) in enumerate(self.params):
            if "*" in t:
                args.append("(%s)(uintptr_t)r%d" % (self.ctype(t), i))
            else:
                args.append("(%s)r%d" % (t, i))
        call = "%s(%s)" % (self.name, ", ".join(args))
        if self.ret == "void":
            return call + ";"
        return "r0 = (u32)(uintptr_t)%s;" % call


def load_protos(paths, extra):
    protos = {}
    for path in paths:
        text = open(path).read()
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        for m in re.finditer(r"^[ \t]*((?:[A-Za-z_][\w \t\*]*?)\b[A-Za-z_]\w*[ \t]*\([^;{]*\))[ \t]*;",
                             text, flags=re.M):
            p = Proto.parse(m.group(1))
            if p and p.name not in protos:
                protos[p.name] = p
    for e in extra:
        p = Proto.parse(e)
        if not p:
            die("bad --proto %r" % e)
        old = protos.get(p.name)
        if old is not None and (old.ret != p.ret or [t for t, _ in old.params] != [t for t, _ in p.params]):
            p.override = True
        protos[p.name] = p
    return protos


# ---------------------------------------------------------------------------
# Object file reader
# ---------------------------------------------------------------------------
class Translator:
    def __init__(self, src, obj, objdump):
        self.src, self.obj = src, obj
        with open(obj, "rb") as f:
            elf = ELFFile(f)
            text = elf.get_section_by_name(".text")
            text_idx = [i for i in range(elf.num_sections()) if elf.get_section(i).name == ".text"][0]
            self.text = text.data()
            self.relocs = {}
            for rs in elf.iter_sections():
                if isinstance(rs, RelocationSection) and rs.name == ".rel.text":
                    symtab = elf.get_section(rs["sh_link"])
                    for r in rs.iter_relocations():
                        if r["r_info_type"] == R_ARM_V4BX:
                            continue
                        self.relocs[r["r_offset"]] = (symtab.get_symbol(r["r_info_sym"]).name, r["r_info_type"])
            symtab = elf.get_section_by_name(".symtab")
            self.mapping = []
            self.funcs = {}      # addr -> name (STT_FUNC)
            self.globals = set()
            self.labels = {}     # addr -> name (any .text symbol)
            for s in symtab.iter_symbols():
                if s.name in ("$t", "$a", "$d") or s.name.startswith(("$t.", "$a.", "$d.")):
                    self.mapping.append((s["st_value"], s.name[:2]))
                    continue
                if s["st_shndx"] != text_idx or not s.name:
                    continue
                addr = s["st_value"] & ~1
                if s["st_info"]["type"] == "STT_FUNC":
                    self.funcs[addr] = s.name
                    self.labels[addr] = s.name
                    if s["st_info"]["bind"] == "STB_GLOBAL":
                        self.globals.add(s.name)
                elif s["st_info"]["type"] == "STT_NOTYPE" and addr not in self.labels:
                    self.labels[addr] = s.name
            self.mapping.sort()
        self.insns = []
        dump = subprocess.check_output([objdump, "-d", "--no-show-raw-insn", obj], text=True)
        for line in dump.splitlines():
            m = re.match(r"^\s+([0-9a-f]+):\t(\S+)\s*(.*)$", line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            mnem = m.group(2)
            rest = m.group(3)
            comment = ""
            if "@" in rest:
                rest, comment = rest.split("@", 1)
            elif ";" in rest:
                rest, comment = rest.split(";", 1)
            ops = split_ops(rest.strip()) if rest.strip() else []
            mode = self.mode_at(addr)
            if mnem in (".word", ".short", ".byte") or mode == "$d":
                continue
            if mode not in ("$t", "$a"):
                die("instruction at 0x%x without mapping symbol" % addr)
            self.insns.append(Insn(addr, mnem, ops, comment.strip(), mode == "$t"))
        for i, ins in enumerate(self.insns):
            ins.size = (self.insns[i + 1].addr - ins.addr) if i + 1 < len(self.insns) else (2 if ins.thumb else 4)
        self.by_addr = {ins.addr: ins for ins in self.insns}
        self.data_refs = set()
        self.code_consts = self.scan_code_consts()

    def mode_at(self, addr):
        mode = None
        for a, m in self.mapping:
            if a <= addr:
                mode = m
            else:
                break
        return mode

    def pool_word(self, addr):
        """(C expression for the value, code target or None) of a literal pool word."""
        raw = int.from_bytes(self.text[addr:addr + 4], "little")
        if addr in self.relocs:
            name, rtype = self.relocs[addr]
            if rtype != R_ARM_ABS32:
                die("unexpected relocation type %d at pool 0x%x" % (rtype, addr))
            name = CODE_ALIASES.get(name, name)
            if name in self.func_addrs:
                if raw not in (0, 1):
                    die("code pointer with addend at pool 0x%x" % addr)
                return None, self.func_addrs[name]
            self.data_refs.add(name)
            if name in VALUE_SYMBOLS:
                return "((u32)(uintptr_t)(%s) + 0x%xu)" % (name, raw), None
            return "((u32)(uintptr_t)&(%s) + 0x%xu)" % (name, raw), None
        return const_addr(raw), None

    @property
    def func_addrs(self):
        return {n: a for a, n in self.funcs.items()}

    def scan_code_consts(self):
        """Linear pre-pass: for every `bx rN`, the code address rN holds (adr /
        literal), as {insn addr: target}.  Any other write to the register
        between the adr and the bx invalidates the constant."""
        consts, out = {}, {}
        for ins in self.insns:
            base, cond, S = split_mnem(ins)
            if base == "bx":
                r = reg(ins.ops[0])
                if r in consts:
                    out[ins.addr] = consts[r]
                continue
            tgt = adr_target(ins, base, self)
            if tgt is not None:
                if tgt[1] is None:
                    consts.pop(tgt[0], None)
                else:
                    consts[tgt[0]] = tgt[1]
                continue
            for w in written_regs(ins, base):
                consts.pop(w, None)
        return out

    def succ(self, ins):
        """(fallthrough?, [branch targets]) of an instruction."""
        base, cond, S = split_mnem(ins)
        if base == "b":
            tgt = int(ins.ops[0].split()[0], 16)
            return (cond not in (None, "al")), [tgt]
        if base == "bx":
            if ins.addr in self.code_consts:
                return False, [self.code_consts[ins.addr] & ~1]
            return cond not in (None, "al"), []
        if base in ("pop", "ldmfd", "ldmia", "ldm") and "pc" in reglist(ins.ops[-1]):
            return cond not in (None, "al"), []
        return True, []


def const_addr(v):
    """A constant that the assembly uses as an address: GBA I/O and RAM are
    mapped to the host arrays, everything else is taken literally."""
    if 0x04000000 <= v < 0x04000400:
        return "((u32)(uintptr_t)(gHostIo + 0x%x))" % (v - 0x04000000)
    if 0x03000000 <= v < 0x03008000:
        return "((u32)(uintptr_t)(gHostIwram + 0x%x))" % (v - 0x03000000)
    if 0x02000000 <= v < 0x02040000:
        return "((u32)(uintptr_t)(gHostEwram + 0x%x))" % (v - 0x02000000)
    return "0x%08xu" % v


ARM_MNEM_RE = re.compile(
    r"^(stmfd|ldmfd|stmia|ldmia|stmdb|ldm|stm|push|pop|umull|mla|mul|ldrsb|ldrsh|ldrb|ldrh|strb|strh|ldr|str|"
    r"and|eor|sub|rsb|add|adc|sbc|rsc|tst|teq|cmp|cmn|orr|mov|bic|mvn|lsl|lsr|asr|ror|bx|bl|b|nop)"
    r"(s?)(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?(s?)$")
THUMB_B_RE = re.compile(r"^b(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(\.n|\.w)?$")


ARM_BRANCH_RE = re.compile(r"^(bl|bx|b)(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?$")


def split_mnem(ins):
    """(base mnemonic incl. trailing 's', condition or None, S flag)."""
    mn = ins.mnem
    if not ins.thumb:
        m = ARM_BRANCH_RE.match(mn)
        if m:
            return m.group(1), m.group(2), False
        m = ARM_MNEM_RE.match(mn)
        if not m:
            die("unknown ARM mnemonic %r at 0x%x" % (mn, ins.addr))
        s = m.group(2) or m.group(4) or ""
        base = m.group(1) + s
        return base, m.group(3), bool(s)
    m = THUMB_B_RE.match(mn)
    if m:
        return "b", m.group(1), False
    base = mn.replace(".n", "").replace(".w", "")
    return base, None, base.endswith("s") and base[:-1] in WRITE_DP


def adr_target(ins, base, tr):
    """If ins is an address-forming instruction (`add rd, pc, #imm` or a
    pc-relative `ldr` of a code pointer), returns (rd, target or None)."""
    if base == "add" and len(ins.ops) == 3 and ins.ops[1] == "pc" and ins.ops[2].startswith("#"):
        if ins.thumb:
            return reg(ins.ops[0]), ((ins.addr + 4) & ~3) + imm(ins.ops[2])
        return reg(ins.ops[0]), ins.addr + 8 + imm(ins.ops[2])
    if base == "ldr" and len(ins.ops) == 2 and ins.ops[1].startswith("[pc"):
        pool = pool_addr(ins)
        _, tgt = tr.pool_word(pool)
        return reg(ins.ops[0]), tgt
    return None


def pool_addr(ins):
    m = re.search(r"([0-9a-f]+)\s*<", ins.comment) or re.search(r"\(([0-9a-f]+)", ins.comment)
    if not m:
        die("pc-relative load without target at 0x%x" % ins.addr)
    return int(m.group(1), 16)


WRITE_DP = {"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "orr", "mov", "bic", "mvn",
            "lsl", "lsr", "asr", "ror", "neg", "mul", "mla"}


def written_regs(ins, base):
    """Registers an instruction may write (conservative; used for the adr
    constant tracking and the tagged registers)."""
    b = base[:-1] if base.endswith("s") and base[:-1] in WRITE_DP else base
    out = set()
    if b in WRITE_DP:
        out.add(reg(ins.ops[0]))
        if b in ("add", "sub") and ins.ops[0] == "sp":
            out.add("sp")
    elif b == "umull":
        out |= {reg(ins.ops[0]), reg(ins.ops[1])}
    elif b in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        out.add(reg(ins.ops[0]))
        out |= writeback_reg(ins)
    elif b in ("str", "strb", "strh"):
        out |= writeback_reg(ins)
    elif b in ("pop", "ldmfd"):
        out |= set(reglist(ins.ops[-1])) | {"sp"}
    elif b in ("push", "stmfd", "stmdb"):
        out.add("sp")
    elif b in ("ldm", "ldmia"):
        out |= set(reglist(ins.ops[1]))
        if ins.ops[0].endswith("!"):
            out.add(reg(ins.ops[0].rstrip("!")))
    elif b in ("stm", "stmia"):
        if ins.ops[0].endswith("!"):
            out.add(reg(ins.ops[0].rstrip("!")))
    elif b == "bl":
        out |= {"r0", "r1", "r2", "r3", "r12", "lr"}
    elif b in ("b", "bx", "cmp", "cmn", "tst", "teq", "nop"):
        pass
    else:
        die("written_regs: unknown mnemonic %r at 0x%x" % (ins.mnem, ins.addr))
    return out


def writeback_reg(ins):
    if len(ins.ops) > 2:  # post-indexed
        m = re.match(r"^\[(\w+)\]$", ins.ops[1])
        return {reg(m.group(1))} if m else set()
    if ins.ops[1].endswith("!"):
        m = re.match(r"^\[(\w+)", ins.ops[1])
        return {reg(m.group(1))}
    return set()


# ---------------------------------------------------------------------------
# Code generation for one core
# ---------------------------------------------------------------------------
class CoreGen:
    def __init__(self, tr, name, entry, ctx, tagged):
        self.tr, self.name, self.entry, self.ctx = tr, name, entry, ctx
        self.tagged = tagged  # set of register names whose top two bits live in <reg>_tag
        self.lines = []
        self.targets = set()
        self.cur = None

    def emit(self, s):
        self.lines.append("    " + s)

    # ----- operand helpers -------------------------------------------------
    def operand2(self, ops):
        """ARM operand 2 (or thumb rm/imm). Returns (expr, carry_expr or None)."""
        if len(ops) == 1:
            o = ops[0]
            if o.startswith("#"):
                return "0x%xu" % imm(o), None
            return self.rd(o), None
        r = self.rd(ops[0])
        m = re.match(r"(lsl|lsr|asr|ror)\s+(#\S+|\w+)$", ops[1])
        if not m:
            die("bad shifted operand %r" % ops)
        kind, amt = m.group(1), m.group(2)
        if not amt.startswith("#"):
            rs = self.rd(amt)
            return "SHIFT_%s(%s, %s & 0xff, &c_)" % (kind.upper(), r, rs), "REGSHIFT"
        return self.shift_imm(kind, r, imm(amt))

    def shift_imm(self, kind, r, k):
        if kind == "lsl":
            if k == 0:
                return r, None
            return "(%s << %d)" % (r, k), "((%s >> %d) & 1)" % (r, 32 - k)
        if kind == "lsr":
            if k == 0 or k == 32:
                return "0u", "(%s >> 31)" % r
            return "(%s >> %d)" % (r, k), "((%s >> %d) & 1)" % (r, k - 1)
        if kind == "asr":
            if k == 0 or k == 32:
                return "(u32)((s32)%s >> 31)" % r, "(%s >> 31)" % r
            return "(u32)((s32)%s >> %d)" % (r, k), "(((u32)((s32)%s >> %d)) & 1)" % (r, k - 1)
        if kind == "ror":
            if k == 0:  # RRX
                return "((%s >> 1) | (c << 31))" % r, "(%s & 1)" % r
            return "((%s >> %d) | (%s << %d))" % (r, k, r, 32 - k), "((%s >> %d) & 1)" % (r, k - 1)
        die("shift %s" % kind)

    def rd(self, r):
        """A register read as a plain value."""
        r = reg(r)
        if r in self.tagged:
            self.emit("ASM_TAG_CHECK(%s_tag);" % r)
        return r

    def wr(self, r):
        """A register written by a generic instruction (clears its tag)."""
        r = reg(r)
        if r in self.tagged:
            self.post.append("%s_tag = 0;" % r)
        return r

    def base_reg(self, r):
        """A register used as a memory base (the tag must be clear)."""
        r = reg(r)
        if r in self.tagged:
            self.emit("ASM_TAG_CHECK(%s_tag);" % r)
        return r

    def mem_addr(self, ins, op, rest):
        """Returns (address expr, writeback statement or None)."""
        m = re.match(r"^\[(\w+)\](!?)$", op)
        if m and rest is None:
            return self.base_reg(m.group(1)), None
        if m and rest is not None:  # post-indexed: [rn], #imm / [rn], rm
            rn = self.base_reg(m.group(1))
            off = ("0x%xu" % imm(rest)) if rest.startswith("#") else self.rd(rest)
            return rn, "%s += %s;" % (rn, off)
        m = re.match(r"^\[(\w+),\s*(#-?\w+|-?\w+)(?:,\s*(lsl|lsr|asr)\s+#(\d+))?\](!?)$", op)
        if not m:
            die("addressing mode %r at 0x%x" % (op, ins.addr))
        rn, off, skind, samt, wb = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        if rn == "pc":
            die("unexpected pc-relative %r" % op)
        rn = self.base_reg(rn)
        if off.startswith("#"):
            v = int(off[1:], 0)
            expr = "(%s + %du)" % (rn, v & 0xFFFFFFFF) if v >= 0 else "(%s - %du)" % (rn, -v)
        else:
            neg = off.startswith("-")
            rm = self.rd(off.lstrip("-"))
            if skind:
                rm, _ = self.shift_imm(skind, rm, int(samt))
            expr = "(%s %s %s)" % (rn, "-" if neg else "+", rm)
        if wb:
            return rn, "PRE:%s = %s;" % (rn, expr)
        return expr, None

    def set_nz(self, d):
        self.emit("n = %s >> 31; z = (%s == 0);" % (d, d))

    # ----- instruction translation ----------------------------------------
    def translate(self, ins):
        base, cond, S = split_mnem(ins)
        self.post = []
        if cond and cond != "al" and base not in ("b",):
            self.emit("if (%s) {" % CONDS[cond])
            self.do(ins, base, None, S)
            for p in self.post:
                self.emit(p)
            self.post = []
            self.emit("}")
        else:
            self.do(ins, base, cond, S)
            for p in self.post:
                self.emit(p)
            self.post = []

    def goto(self, tgt):
        tgt &= ~1
        if tgt not in self.tr.by_addr:
            die("%s: branch target 0x%x is not an instruction" % (self.name, tgt))
        self.targets.add(tgt)
        return "goto L_%x;" % tgt

    def do(self, ins, base, cond, S):
        e = self.emit
        T = ins.thumb
        ops = ins.ops
        if base == "nop":
            return
        if base == "b":
            tgt = int(ops[0].split()[0], 16)
            if cond and cond != "al":
                e("if (%s) %s" % (CONDS[cond], self.goto(tgt)))
            else:
                e(self.goto(tgt))
            return
        if base == "bl":
            self.do_bl(ins)
            return
        if base == "bx":
            if ins.addr in self.tr.code_consts:
                e(self.goto(self.tr.code_consts[ins.addr]))
            else:
                self.rd(ops[0])
                e("goto ret_;")
            return
        if base in ("push", "stmfd", "stmdb"):
            if base == "stmdb" and ops[0] != "sp!":
                die("stmdb with base other than sp! at 0x%x" % ins.addr)
            lst = reglist(ops[-1])
            e("sp -= %d;" % (4 * len(lst)))
            for i, r in enumerate(lst):
                e("ST32(sp + %d, %s);" % (4 * i, self.rd(r)))
            return
        if base in ("pop", "ldmfd"):
            lst = reglist(ops[-1])
            ret = False
            for i, r in enumerate(lst):
                if r == "pc":
                    ret = True
                    continue
                e("%s = LD32(sp + %d);" % (self.wr(r), 4 * i))
            e("sp += %d;" % (4 * len(lst)))
            if ret:
                e("goto ret_;")
            return
        if base in ("ldm", "ldmia", "stmia", "stm"):
            rn = ops[0]
            wb = rn.endswith("!")
            rn = self.base_reg(rn.rstrip("!"))
            lst = reglist(ops[1])
            if rn in lst and wb:
                die("ldm/stm base in list")
            load = base.startswith("ld")
            if load and "pc" in lst:
                die("ldm with pc at 0x%x" % ins.addr)
            for i, r in enumerate(lst):
                if load:
                    e("%s = LD32(%s + %d);" % (self.wr(r), rn, 4 * i))
                else:
                    e("ST32(%s + %d, %s);" % (rn, 4 * i, self.rd(r)))
            if wb:
                e("%s += %d;" % (rn, 4 * len(lst)))
                if rn in self.tagged:
                    self.post.append("%s_tag = 0;" % rn)
            return
        if base in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "str", "strb", "strh"):
            if ops[1].startswith("[pc"):
                if base != "ldr":
                    die("pc-relative %s" % base)
                rd = self.wr(ops[0])
                expr, tgt = self.tr.pool_word(pool_addr(ins))
                if tgt is not None:
                    e("/* %s = code address of %s (handled at the bx) */" % (rd, self.tr.labels.get(tgt, "0x%x" % tgt)))
                    e("%s = 0x%xu;" % (rd, tgt | 1))
                else:
                    e("%s = %s;" % (rd, expr))
                return
            addr, wb = self.mem_addr(ins, ops[1], ops[2] if len(ops) > 2 else None)
            if wb and wb.startswith("PRE:"):
                e(wb[4:])
                wb = None
            acc = {"ldr": "%s = LD32(%s);", "ldrb": "%s = LD8(%s);", "ldrh": "%s = LD16(%s);",
                   "ldrsb": "%s = (u32)(s32)(s8)LD8(%s);", "ldrsh": "%s = (u32)(s32)(s16)LD16(%s);"}
            if base in acc:
                rd = self.wr(ops[0])
                if wb and rd in addr:
                    die("load into base with writeback")
                e(acc[base] % (rd, addr))
            else:
                fn = {"str": "ST32", "strb": "ST8", "strh": "ST16"}[base]
                e("%s(%s, %s);" % (fn, addr, self.rd(ops[0])))
            if wb:
                e(wb)
                wbr = wb.split()[0]
                if wbr in self.tagged:
                    self.post.append("%s_tag = 0;" % wbr)
            return
        if base in ("mul", "muls"):
            rm, rs = self.rd(ops[1]), self.rd(ops[2] if len(ops) > 2 else ops[0])
            rd = self.wr(ops[0])
            e("%s = %s * %s;" % (rd, rm, rs))
            if base == "muls":
                self.set_nz(rd)
            return
        if base == "mla":
            rm, rs, rn = self.rd(ops[1]), self.rd(ops[2]), self.rd(ops[3])
            e("%s = %s * %s + %s;" % (self.wr(ops[0]), rm, rs, rn))
            return
        if base == "umull":
            rm, rs = self.rd(ops[2]), self.rd(ops[3])
            lo, hi = self.wr(ops[0]), self.wr(ops[1])
            e("{ u64 t_ = (u64)%s * (u64)%s; %s = (u32)t_; %s = (u32)(t_ >> 32); }" % (rm, rs, lo, hi))
            return
        # ---- data processing ----
        op = base[:-1] if S else base
        if op in self.tagged_forms(ins, ops):
            return
        if T and op in ("add", "sub") and not S:
            # thumb add/sub without flags: sp adjust, hi registers, adr
            if ops[0] == "sp" and len(ops) == 2:
                e("sp %s= %du;" % ("+" if op == "add" else "-", imm(ops[1])))
                return
            if len(ops) == 3 and ops[1] == "pc" and op == "add":
                tgt = adr_target(ins, op, self.tr)[1]
                e("%s = 0x%xu; /* adr */" % (self.wr(ops[0]), tgt))
                return
            if len(ops) == 2:
                src = self.rd(ops[1]) if not ops[1].startswith("#") else "0x%xu" % imm(ops[1])
                e("%s %s= %s;" % (self.wr(ops[0]), "+" if op == "add" else "-", src))
                return
            if len(ops) == 3 and ops[1] == "sp":
                e("%s = sp + %du;" % (self.wr(ops[0]), imm(ops[2])))
                return
            die("thumb %s %r" % (base, ops))
        if not T and op == "add" and len(ops) == 3 and ops[1] == "pc":
            tgt = adr_target(ins, op, self.tr)[1]
            e("%s = 0x%xu; /* adr */" % (self.wr(ops[0]), tgt))
            return
        if op == "mov" and T and not S and len(ops) == 2 and not ops[1].startswith("#"):
            src = self.rd(ops[1])
            e("%s = %s;" % (self.wr(ops[0]), src))
            return
        if op in ("lsl", "lsr", "asr", "ror"):
            if len(ops) == 2 and not ops[1].startswith("#"):
                # thumb register shift: rd = rd <shift> rs
                rs = self.rd(ops[1])
                rd = self.rd(ops[0])
                e("{ u32 a_ = %s & 0xff; u32 c_ = c; %s = SHIFT_%s(%s, a_, &c_); c = c_; }" % (rs, self.wr(rd), op.upper(), rd))
                if S:
                    self.set_nz(rd)
                return
            if len(ops) == 3 and not ops[2].startswith("#"):
                rm, rs = self.rd(ops[1]), self.rd(ops[2])
                rd = self.wr(ops[0])
                e("{ u32 c_ = c; %s = SHIFT_%s(%s, %s & 0xff, &c_); %s }" % (rd, op.upper(), rm, rs, "c = c_;" if S else ""))
                if S:
                    self.set_nz(rd)
                return
            rm = self.rd(ops[1])
            k = imm(ops[2])
            val, carry = self.shift_imm(op, rm, k)
            rd = self.wr(ops[0])
            if S:
                if carry:
                    e("c = %s;" % carry)
                e("%s = %s;" % (rd, val))
                self.set_nz(rd)
            else:
                e("%s = %s;" % (rd, val))
            return
        if op in ("mov", "mvn"):
            val, carry = self.operand2(ops[1:])
            rd = self.wr(ops[0])
            if op == "mvn":
                val = "~(%s)" % val
            self.emit_with_carry(rd, val, carry, S)
            if S:
                self.set_nz(rd)
            return
        if op == "neg":
            rm = self.rd(ops[1])
            rd = self.wr(ops[0])
            e("{ u32 a_ = 0, b_ = %s, r_ = a_ - b_; c = (a_ >= b_); v = ((a_ ^ b_) & (a_ ^ r_)) >> 31; %s = r_; }" % (rm, rd))
            self.set_nz(rd)
            return
        if op in ("cmp", "cmn", "tst", "teq"):
            rn = self.rd(ops[0])
            val, carry = self.operand2(ops[1:])
            if carry == "REGSHIFT":
                die("register-shifted operand in %s at 0x%x" % (op, ins.addr))
            if op == "cmp":
                e("{ u32 a_ = %s, b_ = %s, r_ = a_ - b_; c = (a_ >= b_); v = ((a_ ^ b_) & (a_ ^ r_)) >> 31; n = r_ >> 31; z = (r_ == 0); }" % (rn, val))
            elif op == "cmn":
                e("{ u32 a_ = %s, b_ = %s, r_ = a_ + b_; c = (r_ < a_); v = ((a_ ^ r_) & (b_ ^ r_)) >> 31; n = r_ >> 31; z = (r_ == 0); }" % (rn, val))
            else:
                if carry:
                    e("c = %s;" % carry)
                e("{ u32 r_ = %s %s %s; n = r_ >> 31; z = (r_ == 0); }" % (rn, "&" if op == "tst" else "^", val))
            return
        if op in ("add", "sub", "rsb", "and", "orr", "eor", "bic", "adc", "sbc"):
            if T and len(ops) == 2:
                rn, rest = self.rd(ops[0]), ops[1:]
            else:
                rn, rest = self.rd(ops[1]), ops[2:]
            val, carry = self.operand2(rest)
            if carry == "REGSHIFT":
                die("register-shifted operand in %s at 0x%x" % (op, ins.addr))
            rd = self.wr(ops[0])
            if op in ("add", "sub", "rsb"):
                a, b = (rn, val) if op != "rsb" else (val, rn)
                if op == "add":
                    if S:
                        e("{ u32 a_ = %s, b_ = %s, r_ = a_ + b_; c = (r_ < a_); v = ((a_ ^ r_) & (b_ ^ r_)) >> 31; %s = r_; }" % (a, b, rd))
                    else:
                        e("%s = %s + %s;" % (rd, a, b))
                else:
                    if S:
                        e("{ u32 a_ = %s, b_ = %s, r_ = a_ - b_; c = (a_ >= b_); v = ((a_ ^ b_) & (a_ ^ r_)) >> 31; %s = r_; }" % (a, b, rd))
                    else:
                        e("%s = %s - %s;" % (rd, a, b))
            elif op in ("adc", "sbc"):
                die("%s not supported at 0x%x" % (op, ins.addr))
            else:
                sym = {"and": "&", "orr": "|", "eor": "^"}
                if op == "bic":
                    if S and carry:
                        e("c = %s;" % carry)
                    e("%s = %s & ~(%s);" % (rd, rn, val))
                else:
                    if S and carry:
                        e("c = %s;" % carry)
                    e("%s = %s %s %s;" % (rd, rn, sym[op], val))
            if S:
                self.set_nz(rd)
            return
        die("unsupported instruction %s %s at 0x%x" % (ins.mnem, ops, ins.addr))

    def emit_with_carry(self, rd, val, carry, S):
        if carry == "REGSHIFT":
            self.emit("{ u32 c_ = c; %s = %s; %s }" % (rd, val, "c = c_;" if S else ""))
            return
        if S and carry:
            self.emit("c = %s;" % carry)
        self.emit("%s = %s;" % (rd, val))

    # ----- tagged registers ------------------------------------------------
    def tagged_forms(self, ins, ops):
        """The three instruction forms that touch the tag bits of a tagged
        register.  Returns a set containing the op if one was handled."""
        if not self.tagged or ins.thumb:
            return set()
        base, cond, S = split_mnem(ins)
        op = base[:-1] if S else base
        e = self.emit
        # adds R, R, #K  (K multiple of 0x40000000): the loop counter step
        if op == "add" and S and len(ops) == 3 and ops[2].startswith("#") and reg(ops[0]) == reg(ops[1]) \
                and reg(ops[0]) in self.tagged:
            k = imm(ops[2])
            if k & 0x3FFFFFFF:
                die("tagged add with low bits at 0x%x" % ins.addr)
            r = reg(ops[0])
            e("{ u32 a_ = %s_tag << 30, b_ = 0x%xu, r_ = a_ + b_; c = (r_ < a_); v = ((a_ ^ r_) & (b_ ^ r_)) >> 31; "
              "n = r_ >> 31; z = (r_ == 0) && ((%s & 0x3fffffffu) == 0); %s_tag = r_ >> 30; }" % (r, k, r, r))
            return {op}
        # bic R, R, #0xC0000000: clear the tag
        if op == "bic" and len(ops) == 3 and ops[2].startswith("#") and reg(ops[0]) == reg(ops[1]) \
                and reg(ops[0]) in self.tagged:
            k = imm(ops[2])
            if k != 0xC0000000:
                die("tagged bic with unexpected mask at 0x%x" % ins.addr)
            if S:
                die("tagged bics at 0x%x" % ins.addr)
            e("%s_tag = 0;" % reg(ops[0]))
            return {op}
        # lsr rd, R, #30/#31: read the tag
        if op == "lsr" and len(ops) == 3 and ops[2].startswith("#") and reg(ops[1]) in self.tagged:
            k = imm(ops[2])
            if k not in (30, 31) or S:
                die("tagged lsr form at 0x%x" % ins.addr)
            r = reg(ops[1])
            e("%s = %s_tag >> %d;" % (self.wr(ops[0]), r, k - 30))
            return {op}
        return set()

    # ----- external / internal calls ---------------------------------------
    def do_bl(self, ins):
        e = self.emit
        rel = self.tr.relocs.get(ins.addr)
        if rel is not None:
            name, rtype = rel
            if rtype not in (R_ARM_THM_CALL, R_ARM_CALL):
                die("unexpected relocation on bl at 0x%x" % ins.addr)
            if name in self.tr.func_addrs:
                tgt = self.tr.func_addrs[name]
            else:
                proto = self.ctx.protos.get(name)
                if proto is None:
                    die("call to external function %s without prototype (use --proto)" % name)
                self.ctx.extern_calls.add(name)
                e("lr = 0xdeadu;")
                e(proto.call())
                return
        else:
            tgt = int(ins.ops[0].split()[0], 16) & ~1
        if tgt not in self.tr.by_addr:
            die("bl target 0x%x is not an instruction" % tgt)
        tins = self.tr.by_addr[tgt]
        tbase, tcond, _ = split_mnem(tins)
        if tbase == "bx" and tcond is None and tgt not in self.tr.code_consts:
            # trampoline: `bx rN` = indirect call through a C function pointer
            r = self.rd(tins.ops[0])
            e("lr = 0xdeadu;")
            e("r0 = ASM_CALL4(%s)(r0, r1, r2, r3);" % r)
            return
        core = self.ctx.core_for(self.tr, tgt)
        e("lr = 0xdeadu; ASM_SAVE(); %s(R); ASM_LOAD();" % core)
        for r in self.tagged:
            self.post.append("%s_tag = 0;" % r)

    # ----- driver ----------------------------------------------------------
    def reachable(self):
        seen, work = set(), [self.entry]
        while work:
            a = work.pop()
            if a in seen:
                continue
            if a not in self.tr.by_addr:
                die("%s: control reaches 0x%x which is not an instruction" % (self.name, a))
            seen.add(a)
            ins = self.tr.by_addr[a]
            ft, tgts = self.tr.succ(ins)
            if ft:
                work.append(a + ins.size)
            work += [t & ~1 for t in tgts]
        return sorted(seen)

    def generate(self):
        body = []
        for a in self.reachable():
            ins = self.tr.by_addr[a]
            self.lines = []
            self.translate(ins)
            body.append((a, ins, self.lines))
        addrs = {a for a, _, _ in body}
        for t in self.targets:
            if t not in addrs:
                die("%s: branch target 0x%x outside core" % (self.name, t))
        out = ["static void %s(struct AsmRegs *R)" % self.name, "{",
               "    u32 r0 = R->r[0], r1 = R->r[1], r2 = R->r[2], r3 = R->r[3], r4 = R->r[4], r5 = R->r[5];",
               "    u32 r6 = R->r[6], r7 = R->r[7], r8 = R->r[8], r9 = R->r[9], r10 = R->r[10], r11 = R->r[11];",
               "    u32 r12 = R->r[12], sp = R->r[13], lr = R->r[14];",
               "    u32 n = R->n, z = R->z, c = R->c, v = R->v;"]
        for r in sorted(self.tagged):
            out.append("    u32 %s_tag = 0;" % r)
        out.append("")
        # The instructions are emitted in address order; the entry point need
        # not be the first of them (ld_r3_tp_adr_i's core also contains the
        # earlier chk_adr_r2, which it falls into).
        self.targets.add(self.entry)
        out.append("    goto L_%x;" % self.entry)
        for addr, ins, lines in body:
            if addr in self.targets:
                out.append("L_%x:" % addr)
            lbl = self.tr.labels.get(addr)
            out.append("    /* %04x%s %s %s */" % (addr, (" <%s>" % lbl) if lbl else "", ins.mnem, ", ".join(ins.ops)))
            out += lines
        out += ["    goto ret_;", "ret_:",
                "    R->r[0] = r0; R->r[1] = r1; R->r[2] = r2; R->r[3] = r3; R->r[4] = r4; R->r[5] = r5;",
                "    R->r[6] = r6; R->r[7] = r7; R->r[8] = r8; R->r[9] = r9; R->r[10] = r10; R->r[11] = r11;",
                "    R->r[12] = r12; R->r[13] = sp; R->r[14] = lr;",
                "    R->n = n; R->z = z; R->c = c; R->v = v;",
                "}"]
        return out


class Context:
    def __init__(self, protos, tags):
        self.protos = protos
        self.tags = tags            # core name -> set of tagged registers
        self.extern_calls = set()
        self.cores = {}             # (translator, entry addr) -> core name
        self.pending = []

    def core_for(self, tr, addr):
        key = (id(tr), addr)
        if key not in self.cores:
            name = tr.labels.get(addr)
            if name is None:
                name = "%s_%x" % (tr.src.split("/")[-1].split(".")[0], addr)
            name = "asmcore_" + re.sub(r"\W", "_", name)
            self.cores[key] = name
            self.pending.append((tr, addr, name))
        return self.cores[key]


HEADER = r"""
struct AsmRegs { u32 r[16]; u32 n, z, c, v; };

#ifndef ASM_STACK_WORDS
#define ASM_STACK_WORDS 512
#endif
#define ASM_REGS_INIT(R, stack) do { (R).r[13] = (u32)(uintptr_t)&(stack)[ASM_STACK_WORDS]; (R).r[14] = 0xdeadu; } while (0)
#define ASM_SAVE() do { R->r[0] = r0; R->r[1] = r1; R->r[2] = r2; R->r[3] = r3; R->r[4] = r4; R->r[5] = r5; \
    R->r[6] = r6; R->r[7] = r7; R->r[8] = r8; R->r[9] = r9; R->r[10] = r10; R->r[11] = r11; R->r[12] = r12; \
    R->r[13] = sp; R->r[14] = lr; R->n = n; R->z = z; R->c = c; R->v = v; } while (0)
#define ASM_LOAD() do { r0 = R->r[0]; r1 = R->r[1]; r2 = R->r[2]; r3 = R->r[3]; r4 = R->r[4]; r5 = R->r[5]; \
    r6 = R->r[6]; r7 = R->r[7]; r8 = R->r[8]; r9 = R->r[9]; r10 = R->r[10]; r11 = R->r[11]; r12 = R->r[12]; \
    sp = R->r[13]; lr = R->r[14]; n = R->n; z = R->z; c = R->c; v = R->v; } while (0)
typedef u32 (*AsmCall4)(u32, u32, u32, u32);
#define ASM_CALL4(p) ((AsmCall4)(uintptr_t)(p))
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdump", default="arm-none-eabi-objdump")
    ap.add_argument("--out", required=True)
    ap.add_argument("--protos", action="append", default=[], help="C header to take prototypes from")
    ap.add_argument("--proto", action="append", default=[], help="extra/overriding C prototype")
    ap.add_argument("--tag", action="append", default=[], help="Func:rN - register with loop-counter tag bits")
    ap.add_argument("--entry", action="append", default=[], help="extra global entry point (needs a prototype)")
    ap.add_argument("--include", action="append", default=[], help="extra header to #include in the output (types of the prototypes)")
    ap.add_argument("pairs", nargs="+", help="source.s=object.o")
    args = ap.parse_args()

    protos = load_protos(args.protos, args.proto)
    tags = {}
    for t in args.tag:
        f, r = t.split(":")
        tags.setdefault("asmcore_" + f, set()).add(reg(r))
    ctx = Context(protos, tags)

    trs = [Translator(*p.split("="), objdump=args.objdump) for p in args.pairs]
    wrappers, skipped, nwrap = [], [], 0
    for tr in trs:
        for addr, name in sorted(tr.funcs.items()):
            if name in tr.globals and name in protos:
                core = ctx.core_for(tr, addr)
                wrappers += protos[name].wrapper(core)
                nwrap += 1
            elif name in tr.globals:
                skipped.append(name)
    cores_out = []
    while ctx.pending:
        tr, addr, name = ctx.pending.pop(0)
        g = CoreGen(tr, name, addr, ctx, tags.get(name, set()))
        cores_out += g.generate() + [""]
    for name in tags:
        if name not in ctx.cores.values():
            die("--tag: no core named %s" % name[8:])

    out = ["/* Generated by tools/port/asm2c.py from %s - do not edit. */" % ", ".join(t.src for t in trs),
           '#include "port/asm_rt.h"'] + ['#include "%s"' % h for h in args.include] + [HEADER]
    refs = set()
    for tr in trs:
        refs |= tr.data_refs
    declared = "".join(open(h).read() for h in args.protos)
    out.append("/* Data referenced by the assembly: %s */" % ", ".join(sorted(refs)))
    for name in sorted(refs):
        if not re.search(r"\b%s\b" % re.escape(name), declared):
            out.append("extern unsigned char %s[];  /* not declared in a header: type unknown here */" % name)
    out.append("")
    out.append("/* C functions called from the assembly */")
    for name in sorted(ctx.extern_calls):
        pr = protos[name]
        out.append("%s %s(%s);" % (pr.ret, name, ", ".join(pr.ctype(t) for t, _ in pr.params) or "void"))
    out.append("")
    out += ["static void %s(struct AsmRegs *R);" % n for n in ctx.cores.values()] + [""]
    out += cores_out
    out += wrappers
    with open(args.out, "w") as f:
        f.write("\n".join(out) + "\n")
    print("asm2c: %d cores, %d C entry points, external calls: %s" % (
        len(ctx.cores), nwrap, ", ".join(sorted(ctx.extern_calls))))
    if skipped:
        print("asm2c: global routines without C prototype (no C entry point generated): %s" % ", ".join(skipped))


if __name__ == "__main__":
    main()
