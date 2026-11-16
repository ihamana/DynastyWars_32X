#!/bin/bash
# Dynasty Wars 32xM - P1 build (32X cartridge)
set -e
export PATH=/opt/tc/sh/bin:/opt/tc/m68k/bin:$PATH
T=/dw/x32/p1; B=$T/build; G=/dw/x32/gen; mkdir -p $B/split
SH2_ROM_OFS=0x8000
OPT=${OPT:--O2}
SHC="-m2 -mb $GFLAG -fomit-frame-pointer -ffreestanding -nostdlib -w -I$G -I$T/sh2"
sh-elf-as --big --isa=sh2 -o $B/crt0.o $T/sh2/crt0.s
sh-elf-as --big --isa=sh2 -o $B/sys.o $T/sh2/sys.s
python3 $T/tools/mkt16.py $T/sh2/t16.s; sh-elf-as --big --isa=sh2 -o $B/t16.o $T/sh2/t16.s   # V2.8 sprite rows
sh-elf-gcc $SHC $OPT $SHDEFS -c -o $B/main.o $T/sh2/main.c
sh-elf-gcc $SHC $OPT $RDEF -c -o $B/render.o $T/sh2/render.c
sh-elf-gcc $SHC -Os $SHDEFS -c -o $B/ui.o $T/sh2/ui.c
sh-elf-gcc $SHC -O2 -c -o $B/nat.o $G/nat.c                                  # V2.6 native arcade routines (cart)
COLDOPT=${COLDOPT:--Os}
python3 $T/tools/patch_gen.py
ls $G/split/*.c | xargs -P2 -I{} sh -c "f={}; b=\$(basename \$f .c); o=$B/split/\$b.o; O=\"$COLDOPT -DCOLD\"; case \$b in hot|run|warm) O=\"${HOTOPT:-$OPT}\";; esac; [ -f \$o -a \$o -nt \$f ] || sh-elf-gcc $SHC \$O -c -o \$o \$f"
for o in $B/split/*.o; do [ -f $G/split/$(basename $o .o).c ] || rm -f $o; done     # (V3.6) objects of files no longer generated
python3 $T/tools/pickrom.py $B $T
sh-elf-gcc -m2 -mb -nostdlib -T $B/sh2_gen.ld -o $B/sh2.elf $B/crt0.o $B/sys.o $B/main.o $B/render.o $B/ui.o $B/nat.o $B/t16.o $B/split/*.o -lgcc -Wl,-Map=$B/sh2.map
sh-elf-objcopy -O binary -j .vectors -j .text -j .rodata -j .data $B/sh2.elf $B/sh2.bin
sh-elf-objcopy -O binary -j .rom $B/sh2.elf $B/rom.bin
sh-elf-objcopy -O binary -j .rom3 $B/sh2.elf $B/rom3.bin
sh-elf-objcopy -O binary -j .rom2 $B/sh2.elf $B/rom2.bin
sh-elf-objcopy -O binary -j .rom4 $B/sh2.elf $B/rom4.bin
sh-elf-objcopy -O binary -j .hot $B/sh2.elf $B/hot.bin
SH2_SIZE=$(stat -c %s $B/sh2.bin)
sym() { sh-elf-nm $B/sh2.elf | awk -v s=$1 '$3==s{print "0x"$1}'; }
cat > $B/sh2syms.inc <<EOF
        .equ SH2_ROM_OFS, $SH2_ROM_OFS
        .equ SH2_SIZE, $(( (SH2_SIZE+3)/4*4 ))
        .equ SH2_MSTART, $(sym _master_start)
        .equ SH2_SSTART, $(sym _slave_start)
        .equ SH2_MVBR, $(sym _master_vbr)
        .equ SH2_SVBR, $(sym _slave_vbr)
EOF
echo "SH2 lo $SH2_SIZE bytes (bss end $(sym __bss_end)), hot $(stat -c %s $B/hot.bin), cold $(stat -c %s $B/rom.bin)"
rm -f $T/cd/out/sub.bin; $T/cd/build.sh > /dev/null; cp $T/cd/out/sub.bin $B/sub.bin
m68k-elf-as -m68000 --register-prefix-optional -I $T/md -I $B -o $B/boot.o $T/md/boot.s
m68k-elf-gcc -m68000 ${MDOPT:--Os} -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -w -c -o $B/bg.o $T/md/bg.c
m68k-elf-gcc $MDDEFS -m68000 -O2 -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -w -c -o $B/snd.o $T/md/snd.c
m68k-elf-ld -T $T/md/md.ld -o $B/md.elf $B/boot.o $B/bg.o $B/snd.o
m68k-elf-objcopy -O binary $B/md.elf $B/md.bin
python3 $T/tools/mkrom.py $B $T/data ${OUT:-$B/dw32xm.32x}
