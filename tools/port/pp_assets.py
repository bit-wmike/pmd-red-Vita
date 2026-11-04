#!/usr/bin/env python3
"""Port build step for one preprocessed C file (after cpp, before preproc).

  * INCBIN_U8/U16/U32/S8/S16/S32/RGB("file", ...) initialisers: the array
    keeps its declaration (sized from the files) but is left empty and placed
    in the writable section .data.pmd_asset; a registration function at the
    end of the file tells port/host_assets.c the size and hash of the
    contents, which are copied out of the user's ROM at start. The port's
    binary therefore contains none of the game's graphics, maps or text.
  * `register T x asm("rN")` (agbcc matching hacks) loses the register name.

Usage: pp_assets.py <in.i> <out.i> <registration function name>
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import keysel  # noqa: E402
INCBIN_RE = re.compile(r'INCBIN_(U8|U16|U32|S8|S16|S32|RGB|COMP)\s*\(\s*((?:"[^"]+"\s*,?\s*)+)\)')
ELEM = {"U8": 1, "S8": 1, "U16": 2, "S16": 2, "U32": 4, "S32": 4, "RGB": 4, "COMP": 1}
DECL_RE = re.compile(r'(?P<name>[A-Za-z_]\w*)\s*(?P<dims>(?:\[[^\]]*\]\s*)+)\s*=\s*(?P<brace>\{?)\s*$', re.S)
ASMREG_RE = re.compile(r'\basm\s*\(\s*"[rsl][0-9a-z]*"\s*\)')


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    src, out, func = sys.argv[1], sys.argv[2], sys.argv[3]
    text = open(src).read()
    text = ASMREG_RE.sub("", text)
    regs = []
    pos = 0
    pieces = []
    for m in INCBIN_RE.finditer(text):
        files = re.findall(r'"([^"]+)"', m.group(2))
        data = b"".join(open(os.path.join(ROOT, f), "rb").read() for f in files)
        elem = ELEM[m.group(1)]
        # the declaration: back to the previous ';' or '}' at top level
        start = max(text.rfind(";", 0, m.start()), text.rfind("}", 0, m.start())) + 1
        decl = text[start:m.start()]
        dm = DECL_RE.search(decl)
        if not dm:
            sys.exit("%s: cannot parse the declaration before %s" % (src, m.group(0)[:60]))
        dims = re.findall(r'\[([^\]]*)\]', dm.group("dims"))
        inner = 1
        for d in dims[1:]:
            inner *= int(eval(d))
        if dims[0].strip() == "":
            count = len(data) // (elem * inner)
            new_dims = "[%d]" % count + "".join("[%s]" % d for d in dims[1:])
        else:
            new_dims = "".join("[%s]" % d for d in dims)
        # the end of the initialiser: the closing brace (if any) and ';'
        end = m.end()
        if dm.group("brace"):
            end = text.index("}", end) + 1
        semi = text.index(";", end)
        new_decl = decl[:dm.start("name")] + "%s%s __attribute__((section(\".data.pmd_asset\"))) = {0}" % (
            dm.group("name"), new_dims)
        pieces.append(text[pos:start])
        pieces.append(new_decl)
        pos = semi
        key1, off1, key2, off2, fill = keysel.select(data)
        if fill is not None:
            k1, off1 = fill, -2
        else:
            k1 = int.from_bytes(key1, "little")
        k2 = int.from_bytes(key2, "little") if key2 else 0
        regs.append((dm.group("name"), len(data), fnv64(data), k1, off1, k2, off2))
    pieces.append(text[pos:])
    text = "".join(pieces)
    if "INCBIN_" in text and INCBIN_RE.search(text):
        sys.exit("%s: unconverted INCBIN left" % src)
    with open(out, "w") as f:
        f.write(text)
        f.write("\n\n/* Port: game data of this file comes from the ROM (tools/port/pp_assets.py). */\n")
        f.write("void PortAsset_Add(const char *name, void *dst, unsigned size, unsigned long long hash, "
                "unsigned long long key1, int keyOff1, unsigned long long key2, int keyOff2);\n")
        f.write("void %s(void)\n{\n" % func)
        for name, size, h, k1, off1, k2, off2 in regs:
            f.write("    PortAsset_Add(\"%s\", (void *)%s, %du, 0x%016xull, 0x%016xull, %d, 0x%016xull, %d);\n" % (
                name, name, size, h, k1, off1, k2, off2))
        f.write("}\n")


if __name__ == "__main__":
    main()
