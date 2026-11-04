#!/usr/bin/env python3
"""Extracts the body of a NAKED asm_unified() function from a C source file
and writes it as a standalone Thumb assembly file (for tools/port/asm2c.py).

usage: extract_naked.py <source.c> <function> <asm label> <out.s>
"""
import re
import sys

src, func, label, out = sys.argv[1:5]
text = open(src).read()
m = re.search(r"NAKED\s*\n[^\n]*\b%s\s*\([^)]*\)\s*\{\s*asm_unified\((.*?)\);" % re.escape(func), text, re.S)
if not m:
    sys.exit("extract_naked: %s not found in %s" % (func, src))
body = "".join(s.encode().decode("unicode_escape") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
lines = ['\t#include "asm/macros.inc"', '\t#include "asm/constants/gba_constants.inc"', "", "\t.syntax unified", "", "\t.text", "",
         "\tthumb_func_start %s" % label, "%s:" % label]
lines += [l.rstrip() for l in body.split("\n") if l.strip()]
lines += ["\tthumb_func_end %s" % label, ""]
open(out, "w").write("\n".join(lines))
print("extract_naked: %s -> %s (%d lines)" % (func, out, len(lines)))
