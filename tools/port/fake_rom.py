#!/usr/bin/env python3
"""Development aid: builds a fake 32 MiB "ROM" from the repository's data so
that the port's ROM loader (hash scan, relocations) can be tested without a
real cartridge image. The blocks are placed at random 4-byte-aligned offsets;
pointer slots and the parts the decomp takes from the real ROM are filled
with noise. Such an image cannot run the game properly (the unextracted
parts are missing) but lets it boot far enough to exercise the port.

Usage: fake_rom.py <out.gba> [seed]      (run after a port build: it reads
       build/port-gen/obj/*.o and the INCBIN files the sources name)
"""
import glob
import os
import random
import re
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SIZE = 32 * 1024 * 1024


def main():
    out = sys.argv[1]
    rnd = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    blocks = []
    # assembler data sections (relocation slots and baserom parts = noise)
    for obj in sorted(glob.glob(os.path.join(ROOT, "build/port-gen/obj/*.o"))):
        with open(obj, "rb") as f:
            elf = ELFFile(f)
            symtab = elf.get_section_by_name(".symtab")
            for idx, sec in enumerate(elf.iter_sections()):
                if sec["sh_type"] != "SHT_PROGBITS" or not (sec["sh_flags"] & 2) or sec.data_size == 0:
                    continue
                if sec.name.startswith(".debug") or sec.name == ".ARM.attributes":
                    continue
                data = bytearray(sec.data())
                for rs in elf.iter_sections():
                    if isinstance(rs, RelocationSection) and rs["sh_info"] == idx:
                        for r in rs.iter_relocations():
                            struct.pack_into("<I", data, r["r_offset"], 0x08000000 | rnd.getrandbits(24))
                for sym in symtab.iter_symbols():
                    if sym["st_shndx"] == idx and sym.name.startswith("port_rom_"):
                        pass  # length unknown here; the .space stays zero (hash treats it as a hole anyway)
                blocks.append(bytes(data))
    # INCBIN files of the C sources
    text = " ".join(open(p).read() for p in glob.glob(os.path.join(ROOT, "src/**/*.[ch]"), recursive=True))
    for m in re.finditer(r'INCBIN_[A-Z0-9]+\s*\(\s*((?:"[^"]+"\s*,?\s*)+)\)', text):
        files = re.findall(r'"([^"]+)"', m.group(1))
        blocks.append(b"".join(open(os.path.join(ROOT, f), "rb").read() for f in files))
    rom = bytearray(SIZE)
    for i in range(SIZE):
        rom[i] = rnd.getrandbits(8) if i % 97 == 0 else 0  # mostly zero with some noise
    rnd.shuffle(blocks)
    corrupt = int(sys.argv[3]) if len(sys.argv) > 3 else 0  # every Nth block loses a quarter of its bytes
    off = 0xC0
    for n, b in enumerate(blocks):
        if corrupt and n % corrupt == 0 and len(b) >= 2048:
            bb = bytearray(b)
            for i in range(len(bb) * 3 // 4, len(bb)):
                bb[i] = rnd.getrandbits(8)
            b = bytes(bb)
        if off + len(b) > SIZE:
            sys.exit("fake_rom: data does not fit")
        rom[off:off + len(b)] = b
        off += (len(b) + 3) & ~3
        off += 4 * rnd.randrange(0, 4)
    with open(out, "wb") as f:
        f.write(rom)
    print("fake_rom: %d blocks, %.1f MiB used" % (len(blocks), off / 1048576.0))


if __name__ == "__main__":
    main()
