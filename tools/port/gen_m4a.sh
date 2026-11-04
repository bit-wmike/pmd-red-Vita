#!/bin/sh
# Translates the hand-written sound engine (src/m4a_1.s) and the two link
# cable helpers (src/arm_funcs.s) into C: build/port-gen/m4a_asm.c, and the
# NAKED ground-map decoder sub_80A5204 of src/ground_map.c into
# build/port-gen/ground_map_asm.c (C entry point Port_sub_80A5204).
# Run from the repository root.  Needs arm-none-eabi-{as,objdump}, python3
# with pyelftools and a host gcc for the preprocessor.
set -e
GEN=${GEN:-build/port-gen}
AS=${ARM_AS:-arm-none-eabi-as}
OBJDUMP=${ARM_OBJDUMP:-arm-none-eabi-objdump}
CPP="${CPP:-gcc -E} -x assembler-with-cpp -iquote include -I include"
mkdir -p "$GEN"
for f in m4a_1 arm_funcs; do
    $CPP src/$f.s -o "$GEN/$f.i.s"
    $AS -mcpu=arm7tdmi "$GEN/$f.i.s" -o "$GEN/$f.o"
done
python3 tools/port/asm2c.py --objdump "$OBJDUMP" --out "$GEN/m4a_asm.c" \
    --protos include/gba/m4a_internal.h --protos include/m4a.h \
    --proto 'void SoundMainBTM(void *buf)' \
    --proto 'u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust)' \
    --proto 'u32 sub_80001E8(void)' \
    --proto 'void sub_8000228(void)' \
    --tag SoundMain:r5 \
    src/m4a_1.s="$GEN/m4a_1.o" src/arm_funcs.s="$GEN/arm_funcs.o"

# ---- src/ground_map.c: NAKED sub_80A5204 -> Port_sub_80A5204 ----
python3 tools/port/extract_naked.py src/ground_map.c sub_80A5204 Port_sub_80A5204 "$GEN/ground_map_asm.s"
$CPP "$GEN/ground_map_asm.s" -o "$GEN/ground_map_asm.i.s"
$AS -mcpu=arm7tdmi "$GEN/ground_map_asm.i.s" -o "$GEN/ground_map_asm.o"
python3 tools/port/asm2c.py --objdump "$OBJDUMP" --out "$GEN/ground_map_asm.c" \
    --include structs/str_ground_bg.h \
    --proto 'u8 *Port_sub_80A5204(void *a, const u8 *b, BmaHeader *c, s32 d)' \
    src/ground_map.c="$GEN/ground_map_asm.o"
