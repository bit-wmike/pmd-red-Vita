#!/bin/sh
# Differential test of the translated ground-map decoder (see test_ground_map_diff.c).
set -e
GEN=${GEN:-build/port-gen}
T=$GEN/test
mkdir -p "$T"
sh tools/port/gen_m4a.sh
ARM="arm-linux-gnueabihf-gcc -static -march=armv7-a -mfpu=neon -std=gnu11 -fno-strict-aliasing -fwrapv -w -O2 -DPORT -DMODERN=1"
X86="gcc -m32 -static -no-pie -std=gnu11 -fno-strict-aliasing -fwrapv -w -O2 -DPORT -DMODERN=1"
$ARM -iquote include -I include -x assembler-with-cpp -c "$GEN/ground_map_asm.s" -o "$T/gm_ref.o"
$ARM -iquote include -I . -c tools/port/test_ground_map_diff.c -o "$T/gm_harness_arm.o"
$ARM "$T/gm_ref.o" "$T/gm_harness_arm.o" -o "$T/gm_ref_arm"
qemu-arm "$T/gm_ref_arm" > "$T/out_gm_ref.txt"
$ARM -iquote include -I . -c "$GEN/ground_map_asm.c" -o "$T/gm_gen_arm.o"
$ARM "$T/gm_gen_arm.o" "$T/gm_harness_arm.o" -o "$T/gm_gen_arm"
qemu-arm "$T/gm_gen_arm" > "$T/out_gm_gen_arm.txt"
$X86 -DASM_RT_CHECKS -iquote include -I . -c "$GEN/ground_map_asm.c" -o "$T/gm_gen_x86.o"
$X86 -iquote include -I . -c tools/port/test_ground_map_diff.c -o "$T/gm_harness_x86.o"
$X86 "$T/gm_gen_x86.o" "$T/gm_harness_x86.o" -o "$T/gm_gen_x86"
"$T/gm_gen_x86" > "$T/out_gm_gen_x86.txt"
tail -2 "$T/out_gm_ref.txt"
cmp "$T/out_gm_ref.txt" "$T/out_gm_gen_arm.txt" && echo "ARM: generated C == original assembly"
cmp "$T/out_gm_ref.txt" "$T/out_gm_gen_x86.txt" && echo "x86: generated C == original assembly"
