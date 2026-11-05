#!/usr/bin/env python3
"""Port build step: the game's assembler data files (data/*.s, sound data)
become empty host arrays that are filled from the user's ROM at run time.

Every file is run through the decomp's own pipeline (C preprocessor, preproc
for the text encoding) and assembled with arm-none-eabi-as. From the object
file this script takes, per section:

  * the size and the layout of "holes": pointer slots (relocations) and
    ranges that the decomp still takes straight from the ROM
    (`.incbin "baserom.gba", off, len`);
  * a 64-bit FNV-1a hash of the contents with the holes zeroed, plus two
    8-byte probe windows, so that port/host_assets.c can find the block in
    the ROM without any address list;
  * every label as blob + offset (`.set`), so the C code links against the
    same symbols as on the GBA;
  * the relocations: at start, port/host_assets.c writes the host address of
    each target symbol into the pointer slots.

The generated files contain no game data: the arrays are .bss and only hashes
and sizes are written out.

Usage: gen_data.py --out <dir> [--as arm-none-eabi-as] [--jobs n] file.s ...
"""
import argparse
import hashlib
import multiprocessing
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import keysel  # noqa: E402

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BASEROM_RE = re.compile(r'^(\s*)\.incbin\s+"baserom\.gba"\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*$', re.M)


CHUNK = 512  # partial matches (other ROM versions) are judged per chunk


def fnv64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def blob_name(src, secname):
    base = re.sub(r'[^A-Za-z0-9]', '_', os.path.splitext(src)[0])
    return "port_blob_%s_%s" % (base, secname.strip(".").replace(".", "_"))


def preprocess(src, out_dir, assembler):
    """cpp -> preproc -> baserom rewrite -> as. Returns (obj path, baserom list)."""
    base = os.path.join(out_dir, re.sub(r'[^A-Za-z0-9]', '_', os.path.splitext(src)[0]))
    i_path, pp_path, obj = base + ".i.s", base + ".pp.s", base + ".o"
    with open(i_path, "w") as f:
        subprocess.check_call(["gcc", "-E", "-x", "assembler-with-cpp", "-iquote", "include", "-I", "include",
                               "-I", ".", "-DMODERN=1", "-DPORT", "-Wno-trigraphs", src], stdout=f, cwd=ROOT)
    with open(pp_path, "w") as f:
        subprocess.check_call([os.path.join(ROOT, "tools/preproc/preproc"), i_path, "charmap.txt"], stdout=f, cwd=ROOT)
    text = open(pp_path).read()
    baseroms = []

    def repl(m):
        off, length = int(m.group(2), 0), int(m.group(3), 0)
        label = "port_rom_%d" % len(baseroms)
        baseroms.append((label, off, length))
        return "%s%s:\n%s.space %d" % (m.group(1), label, m.group(1), length)

    text = BASEROM_RE.sub(repl, text)
    if '"baserom.gba"' in text:
        sys.exit("%s: unhandled baserom.gba reference" % src)
    with open(pp_path, "w") as f:
        f.write(text)
    subprocess.check_call([assembler, "-mcpu=arm7tdmi", "-I", ".", "--defsym", "MODERN=1", pp_path, "-o", obj], cwd=ROOT)
    return obj, baseroms


def analyse(args):
    try:
        return analyse_inner(args)
    except SystemExit as e:
        raise RuntimeError(str(e))


def analyse_inner(args):
    src, out_dir, assembler = args
    obj, baseroms = preprocess(src, out_dir, assembler)
    result = {"src": src, "blobs": [], "symbols": [], "locals": [], "relocs": [], "externs": set()}
    with open(obj, "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        all_syms = list(symtab.iter_symbols())
        by_name = {}
        for s in all_syms:
            by_name.setdefault(s.name, s)
        sec_blob = {}
        ram_secs = {}
        for sec_index, sec in enumerate(elf.iter_sections()):
            if sec["sh_type"] == "SHT_NOBITS" and (sec["sh_flags"] & 2) and sec["sh_size"] > 0:
                # RAM variables defined in a data file (the sound library's
                # work areas): a zeroed host array, cleared on reset.
                sec_blob[sec_index] = blob_name(src, sec.name)
                ram_secs[sec_index] = sec["sh_size"]
                continue
            if sec["sh_type"] != "SHT_PROGBITS" or not (sec["sh_flags"] & 2) or sec.data_size == 0:
                continue
            if sec.name.startswith(".debug") or sec.name == ".ARM.attributes":
                continue
            if sec["sh_flags"] & 4:
                sys.exit("%s: section %s contains code" % (src, sec.name))
            sec_blob[sec_index] = blob_name(src, sec.name)
        for sec_index, blob in sec_blob.items():
            sec = elf.get_section(sec_index)
            if sec_index in ram_secs:
                result["blobs"].append({"name": blob, "size": ram_secs[sec_index], "ram": True, "src": src, "sec": sec.name})
                for sym in all_syms:
                    if sym["st_shndx"] != sec_index or not sym.name or sym["st_info"]["type"] in ("STT_SECTION", "STT_FILE"):
                        continue
                    entry = (sym.name, blob, sym["st_value"])
                    if sym["st_info"]["bind"] == "STB_GLOBAL":
                        result["symbols"].append(entry)
                    else:
                        result["locals"].append(entry)
                continue
            data = bytearray(sec.data())
            holes = []
            for rs in elf.iter_sections():
                if not isinstance(rs, RelocationSection) or rs["sh_info"] != sec_index:
                    continue
                for r in rs.iter_relocations():
                    if r["r_info_type"] != 2:  # R_ARM_ABS32
                        sys.exit("%s: unsupported relocation type %d" % (src, r["r_info_type"]))
                    off = r["r_offset"]
                    addend = struct.unpack_from("<I", data, off)[0]
                    sym = symtab.get_symbol(r["r_info_sym"])
                    if sym["st_shndx"] == "SHN_UNDEF":
                        result["relocs"].append((blob, off, sym.name, addend))
                        result["externs"].add(sym.name)
                    elif sym["st_shndx"] == "SHN_ABS":
                        # absolute value (e.g. .set constant): keep it
                        struct.pack_into("<I", data, off, (sym["st_value"] + addend) & 0xFFFFFFFF)
                        continue
                    else:
                        tblob = sec_blob.get(sym["st_shndx"])
                        if tblob is None:
                            sys.exit("%s: relocation into unknown section" % src)
                        result["relocs"].append((blob, off, tblob, addend + sym["st_value"]))
                    struct.pack_into("<I", data, off, 0)
                    holes.append((off, 4))
            rom_addr = None
            for label, rom_off, length in baseroms:
                s = by_name.get(label)
                if s is None or s["st_shndx"] != sec_index:
                    continue
                pos = s["st_value"]
                holes.append((pos, length))
                a = rom_off - pos
                if rom_addr is not None and rom_addr != a:
                    sys.exit("%s: inconsistent baserom offsets in %s" % (src, sec.name))
                rom_addr = a
            holes.sort()
            for off, length in holes:
                data[off:off + length] = b"\0" * length
            # probe windows: 4-aligned, 8 bytes, free of holes, not trivial
            free = bytearray(b"\1") * len(data)
            for off, length in holes:
                free[off:off + length] = b"\0" * length
            key1, off1, key2, off2, fill = keysel.select(data, free)
            # Children: the ranges between global symbols (one game file or
            # table each). When a whole section is not in the ROM as one
            # piece (another ROM version), the children are looked for
            # one by one.
            bounds = {0, len(data)}
            names = {}
            for sym in all_syms:
                if sym["st_shndx"] == sec_index and sym.name and sym["st_info"]["bind"] == "STB_GLOBAL" \
                        and sym["st_info"]["type"] not in ("STT_SECTION", "STT_FILE"):
                    bounds.add(sym["st_value"])
                    names.setdefault(sym["st_value"], sym.name)
            bounds = sorted(bounds)
            ranges = []
            for a, b in zip(bounds, bounds[1:]):
                if ranges and ranges[-1][1] - ranges[-1][0] < 64:
                    ranges[-1] = (ranges[-1][0], b)
                else:
                    ranges.append((a, b))
            children = []
            if len(ranges) >= 2:
                for a, b in ranges:
                    sub = data[a:b]
                    k1, o1, k2, o2, fl = keysel.select(sub, free[a:b])
                    subholes = []
                    for off, length in holes:
                        lo, hi = max(off, a), min(off + length, b)
                        if lo < hi:
                            subholes.append((lo - a, hi - lo))
                    children.append({
                        "off": a, "size": b - a, "hash": fnv64(sub),
                        "chunks": [fnv64(sub[i:i + CHUNK]) for i in range(0, len(sub), CHUNK)] if len(sub) >= 2 * CHUNK else [],
                        "key1": k1, "off1": o1, "key2": k2, "off2": o2, "fill": fl, "holes": subholes,
                        "name": "%s:%s" % (blob, names.get(a, "+%x" % a)),
                    })
            result["blobs"].append({
                "name": blob, "size": len(data), "hash": fnv64(data),
                "chunks": [fnv64(data[i:i + CHUNK]) for i in range(0, len(data), CHUNK)] if len(data) >= 2 * CHUNK else [],
                "key1": key1, "off1": off1, "key2": key2, "off2": off2, "fill": fill,
                "rom_addr": rom_addr, "holes": holes, "src": src, "sec": sec.name, "children": children,
            })
            for sym in all_syms:
                if sym["st_shndx"] != sec_index or not sym.name or sym["st_info"]["type"] in ("STT_SECTION", "STT_FILE"):
                    continue
                if sym.name.startswith("port_rom_"):
                    continue
                entry = (sym.name, blob, sym["st_value"])
                if sym["st_info"]["bind"] == "STB_GLOBAL":
                    result["symbols"].append(entry)
                else:
                    result["locals"].append(entry)
    return result


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--as", dest="assembler", default="arm-none-eabi-as")
    ap.add_argument("--jobs", type=int, default=multiprocessing.cpu_count())
    ap.add_argument("sources", nargs="+")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out)
    obj_dir = os.path.join(out_dir, "obj")
    os.makedirs(obj_dir, exist_ok=True)

    with multiprocessing.Pool(args.jobs) as pool:
        results = pool.map(analyse, [(s, obj_dir, args.assembler) for s in args.sources])

    blobs, symbols, relocs, externs = [], [], [], set()
    local_syms = {}
    for r in results:
        blobs += r["blobs"]
        symbols += r["symbols"]
        relocs += r["relocs"]
        externs |= r["externs"]
        for name, blob, off in r["locals"]:
            local_syms.setdefault(name, (name, blob, off))
    defined = {s[0] for s in symbols} | {b["name"] for b in blobs}
    dup = len(symbols) - len(defined - {b["name"] for b in blobs})
    for e in sorted(externs):
        if e not in defined and e in local_syms:
            symbols.append(local_syms[e])
            defined.add(e)
    c_externs = sorted(e for e in externs if e not in defined)

    with open(os.path.join(out_dir, "data_blobs.s"), "w") as s:
        s.write("/* Generated by tools/port/gen_data.py - do not edit. Game data arrays (filled from the ROM). */\n")
        s.write("\t.bss\n")
        for b in blobs:
            if b.get("ram"):
                continue
            s.write("\t.balign 16\n\t.globl %s\n%s:\n\t.space %d\n" % (b["name"], b["name"], b["size"]))
        s.write("\t.section ewram_data,\"aw\"\n")
        for b in blobs:
            if not b.get("ram"):
                continue
            s.write("\t.balign 16\n\t.globl %s\n%s:\n\t.space %d\n" % (b["name"], b["name"], b["size"]))
        s.write("\t.text\n")
        for name, blob, off in symbols:
            s.write("\t.globl %s\n\t.set %s, %s + %d\n" % (name, name, blob, off))
        s.write('\t.section .note.GNU-stack,"",%progbits\n')

    with open(os.path.join(out_dir, "data_relocs.c"), "w") as c:
        c.write("/* Generated by tools/port/gen_data.py - do not edit. */\n")
        c.write("#include <stdint.h>\n#include <string.h>\n#include \"port/port_assets.h\"\n\n")
        for b in blobs:
            c.write("extern unsigned char %s[];\n" % b["name"])
        blobs = [b for b in blobs if not b.get("ram")]
        declared = {b["name"] for b in blobs}
        for e in sorted({r[2] for r in relocs} | set(c_externs)):
            if e not in declared:
                c.write("extern unsigned char %s[];\n" % e)
        c.write("\nconst struct PortReloc gPortRelocs[] = {\n")
        relocs.sort(key=lambda r: (r[0], r[1]))
        for blob, off, target, addend in relocs:
            c.write("    { %s + %d, %s, %du },\n" % (blob, off, target, addend))
        c.write("};\nconst unsigned gPortRelocCount = sizeof(gPortRelocs) / sizeof(gPortRelocs[0]);\n\n")
        def entry(dst, b, holes_name, chunks_name, rom_addr, children_name, child_count, name):
            k1 = struct.unpack("<Q", b["key1"])[0] if b["key1"] else (b["fill"] if b["fill"] is not None else 0)
            k2 = struct.unpack("<Q", b["key2"])[0] if b["key2"] else 0
            off1 = b["off1"] if b["fill"] is None else -2
            return "    { %s, %du, 0x%016xull, 0x%016xull, %d, 0x%016xull, %d, 0x%x, %s, %d, %s, %d, %s, %d, %s },\n" % (
                dst, b["size"], b["hash"], k1, off1, k2, b["off2"], rom_addr,
                holes_name if b["holes"] else "0", len(b["holes"]),
                chunks_name if b["chunks"] else "0", len(b["chunks"]),
                children_name, child_count, c_str(name))
        for i, b in enumerate(blobs):
            if b["holes"]:
                c.write("static const uint32_t sHoles%d[] = {%s};\n" % (i, ",".join("%d,%d" % h for h in b["holes"])))
            if b["chunks"]:
                c.write("static const uint64_t sChunks%d[] = {%s};\n" % (i, ",".join("0x%xull" % h for h in b["chunks"])))
            for j, ch in enumerate(b["children"]):
                if ch["holes"]:
                    c.write("static const uint32_t sHoles%d_%d[] = {%s};\n" % (i, j, ",".join("%d,%d" % h for h in ch["holes"])))
                if ch["chunks"]:
                    c.write("static const uint64_t sChunks%d_%d[] = {%s};\n" % (i, j, ",".join("0x%xull" % h for h in ch["chunks"])))
            if b["children"]:
                c.write("static const struct PortBlob sChildren%d[] = {\n" % i)
                for j, ch in enumerate(b["children"]):
                    c.write(entry("%s + %d" % (b["name"], ch["off"]), ch, "sHoles%d_%d" % (i, j), "sChunks%d_%d" % (i, j), 0, "0", 0, ch["name"]))
                c.write("};\n")
        c.write("\nconst struct PortBlob gPortBlobs[] = {\n")
        for i, b in enumerate(blobs):
            c.write(entry(b["name"], b, "sHoles%d" % i, "sChunks%d" % i, (b["rom_addr"] if b["rom_addr"] is not None else 0),
                          ("sChildren%d" % i) if b["children"] else "0", len(b["children"]), "%s:%s" % (b["src"], b["sec"])))
        c.write("};\nconst unsigned gPortBlobCount = sizeof(gPortBlobs) / sizeof(gPortBlobs[0]);\n")
        nchildren = sum(len(b["children"]) for b in blobs)
        print("gen_data: %d child blocks" % nchildren)

    blobs = [b for b in blobs if not b.get("ram")]
    known = sum(1 for b in blobs if b["rom_addr"] is not None)
    total = sum(b["size"] for b in blobs)
    print("gen_data: %d blobs (%.1f MiB, %d with known ROM address), %d symbols, %d relocations, %d C targets" %
          (len(blobs), total / 1048576.0, known, len(symbols), len(relocs), len(c_externs)))
    if dup:
        print("gen_data: warning: %d duplicate global symbols" % dup)


if __name__ == "__main__":
    main()
