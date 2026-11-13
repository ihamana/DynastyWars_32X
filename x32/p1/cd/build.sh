#!/bin/bash
set -e
cd "$(dirname "$0")"
T=/opt/tc/m68k/bin/m68k-elf-
CF="-m68000 -Os -fomit-frame-pointer -ffreestanding -nostdlib -Wall -Wno-unused-function -std=gnu99"
mkdir -p out
${T}gcc $CF -c sub_asm.s -o out/sub_asm.o
for f in sub pcm cdread; do :; done; ${T}gcc $CF -c pcm_asm.s -o out/pcm_asm.o; for f in sub pcm cdread; do ${T}gcc $CF -c $f.c -o out/$f.o; done
${T}gcc $CF -c data.s -o out/data.o
${T}gcc $CF -T sub.ld -o out/sub.elf out/sub_asm.o out/sub.o out/pcm.o out/cdread.o out/pcm_asm.o out/data.o ${SGDK_LIBGCC:-/opt/sgdk/libgcc.a}
${T}objcopy -O binary out/sub.elf out/sub.bin
