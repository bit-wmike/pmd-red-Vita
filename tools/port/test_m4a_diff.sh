#!/bin/sh
# Builds and runs the differential test (see test_m4a_diff.c): original
# assembly under qemu-arm vs. the generated C on ARM (qemu-arm) and x86-32.
set -e
GEN=${GEN:-build/port-gen}
T=$GEN/test
mkdir -p "$T"
sh tools/port/gen_m4a.sh
# Reference m4a.c without the LCD waits (no VCOUNT on the host).
sed '/REG_ADDR_VCOUNT/,+1d' src/m4a.c > "$T/m4a_test.c"
ARM="arm-linux-gnueabihf-gcc -static -march=armv7-a -mfpu=neon -std=gnu11 -fno-strict-aliasing -fwrapv -w -O2"
X86="gcc -m32 -static -no-pie -std=gnu11 -fno-strict-aliasing -fwrapv -w -O2"
DEFSYM="-Wl,--defsym=gHostIo=0x04000000 -Wl,--defsym=gHostIwram=0x03000000 -Wl,--defsym=gHostEwram=0x02000000 -Wl,--defsym=gNumMusicPlayers=2 -Wl,--defsym=gMaxLines=0"
HARNESS=""

# 1. reference: the original assembly, run natively under qemu-arm
$ARM -iquote include -I include -x assembler-with-cpp -c src/m4a_1.s -o "$T/ref_m4a_1.o"
$ARM -iquote include -I include -x assembler-with-cpp -c src/arm_funcs.s -o "$T/ref_arm_funcs.o"
$ARM -iquote include -c "$T/m4a_test.c" -o "$T/ref_m4a.o"
$ARM -iquote include -c src/m4a_tables.c -o "$T/ref_tables.o"
$ARM -DREF_BUILD $HARNESS -iquote include -c tools/port/test_m4a_diff.c -o "$T/ref_harness.o"
$ARM -Wl,-Ttext-segment=0x08000000 $DEFSYM "$T/ref_m4a_1.o" "$T/ref_arm_funcs.o" "$T/ref_m4a.o" "$T/ref_tables.o" "$T/ref_harness.o" -o "$T/ref_arm"
qemu-arm "$T/ref_arm" > "$T/out_ref_arm.txt"

# 2. generated C, ARM
$ARM -DPORT -iquote include -I include -I . -c "$GEN/m4a_asm.c" -o "$T/gen_asm_arm.o"
$ARM -DPORT -iquote include -c "$T/m4a_test.c" -o "$T/gen_m4a_arm.o"
$ARM -DPORT $HARNESS -iquote include -c tools/port/test_m4a_diff.c -o "$T/gen_harness_arm.o"
$ARM -Wl,-Ttext-segment=0x08000000 $DEFSYM "$T/gen_asm_arm.o" "$T/gen_m4a_arm.o" "$T/ref_tables.o" "$T/gen_harness_arm.o" -o "$T/gen_arm"
qemu-arm "$T/gen_arm" > "$T/out_gen_arm.txt"

# 3. generated C, x86-32 (with the run-time tag checks enabled)
$X86 -DPORT -DASM_RT_CHECKS -iquote include -I include -I . -c "$GEN/m4a_asm.c" -o "$T/gen_asm_x86.o"
$X86 -DPORT -iquote include -c "$T/m4a_test.c" -o "$T/gen_m4a_x86.o"
$X86 -iquote include -c src/m4a_tables.c -o "$T/x86_tables.o"
$X86 -DPORT $HARNESS -iquote include -c tools/port/test_m4a_diff.c -o "$T/gen_harness_x86.o"
$X86 $DEFSYM "$T/gen_asm_x86.o" "$T/gen_m4a_x86.o" "$T/x86_tables.o" "$T/gen_harness_x86.o" -o "$T/gen_x86"
"$T/gen_x86" > "$T/out_gen_x86.txt"

head -3 "$T/out_ref_arm.txt"
cmp "$T/out_ref_arm.txt" "$T/out_gen_arm.txt" && echo "ARM: generated C == original assembly"
cmp "$T/out_ref_arm.txt" "$T/out_gen_x86.txt" && echo "x86: generated C == original assembly"
