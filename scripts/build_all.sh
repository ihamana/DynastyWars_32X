#!/bin/bash
# Dynasty Wars 32xM: everything from the MAME ROM set.  usage: build_all.sh dynwar.zip [OUTDIR]
# Layout: this repository at /dw, work files in /tmp/dw, toolchains in /opt/tc (sh-elf, m68k-elf), SGDK's
# m68k libgcc.a at /opt/sgdk/libgcc.a (the Docker image sets all of this up).
set -e -o pipefail                     # (a failing step stops the build: without pipefail "| tail" hid it)
python3 -c "import numpy, scipy, PIL, openpyxl, fontTools" || { echo "missing Python packages: pip install numpy scipy pillow openpyxl fonttools (see Dockerfile)"; exit 1; }
ZIP=$(readlink -f "$1"); OUTD=$(readlink -f "${2:-/dw/out}")
[ -f "$ZIP" ] || { echo "usage: $0 dynwar.zip [outdir]"; exit 1; }
R=/dw; P1=$R/x32/p1; D=$P1/data; W=/tmp/dw; J=${JOBS:-$(nproc)}
mkdir -p $W $OUTD
step() { echo; echo "=== $*"; }

step "1/9 ROM images"
python3 $R/scripts/rom.py "$ZIP" $W/rom

step "2/9 host tools (disassembler, sound renderer)"
gcc -O2 -shared -fPIC -w -I$R/dw/ref -o $R/d32/tools/libdasm.so $R/d32/tools/dasmlib.c $R/dw/ref/m68kdasm.c
g++ -O2 -ffp-contract=off -w -I$R/third_party/ymfm -I$R/dw/ref -o $R/dw/ref/sndref $R/dw/ref/sndref.cpp $R/third_party/ymfm/ymfm_opm.cpp -x c $R/dw/ref/z80.c

step "3/9 68000 program -> C (static recompiler)"
python3 $R/d32/tools/mkcode.py $R/meta/code_addr.json $W/rom/dynwarj_prg.bin $W/code.json
rm -rf $R/x32/gen/split; mkdir -p $R/x32/gen/split
( cd $R/x32 && python3 tools/natgen.py > /dev/null && \
  HASHW=tools/disp_v36.txt DISPW=tools/disp_v36.txt WARMLIST=/dev/null NESTW=tools/hot_v36.txt NESTMIN=50000 SPLIT=1 \
  NATLIST=tools/natlist.txt HOTLIST=tools/hot_v36f.txt HOTMIN=4325 python3 tools/x68.py $W/code.json $W/rom/dynwarj_prg.bin gen/split 2>&1 | tail -1 )

step "4/9 graphics (sprites, scroll1, pre-scaled pictures, MD pictures)"
rm -rf $D; mkdir -p $D/bg $D/zh $D/font $D/fm $D/mdpic
cp $R/meta/sfx_pri.json $R/meta/sfx_count.json $D/
python3 $P1/tools/mkgfx.py $W/rom/gfx.bin $D/gfx.bin 0x7a000:0x86000,0x2e0000:0x50000 0x44000 | tail -1
python3 $P1/tools/mkpic.py $R/meta/pic.json $D/pic.bin 0x158D00:0x27300,0x3DF900:0x20700,0x9800:0x6800,0x6000:0x2000,0x3BD200:0x2E00,0x330000:0x800,0x78A20:0x15E0,0x25000:0x2000,0x27000:0x1000,0x3A1A00:0x1B800,0x36C000:0x4000 | tail -1
python3 $R/scripts/snap.py unpack $R/meta/snap $W/snap
CAP=$W/snap/mdpic/ OUT=$D python3 $P1/tools/mdpic.py | grep -c picture | sed 's/$/ MD pictures/'

step "5/9 stage backgrounds"
python3 $R/scripts/pixrec.py unpack $W/rom/gfx.bin $R/meta/bg $D/bg
cp $R/meta/bg/*.bin $D/bg/
ls $D/bg | wc -l | sed 's/$/ background files/'

step "6/9 Chinese text"
cp $R/meta/zh/*.xlsx $R/fonts/KH-Dot-Dougenzaka-16.ttf $D/zh/
cp $R/fonts/fusion-pixel-*.bdf $D/font/
FONT=${SHS_FONT:-/opt/fonts/SourceHanSerifTC-VF.ttf}; [ -f "$FONT" ] || { echo "missing $FONT (Source Han Serif TC VF 2.003)"; exit 1; }
ln -sf "$FONT" $D/font/SourceHanSerifTC-VF.ttf
python3 $P1/tools/zh/mkzh.py | tail -1

step "7/9 sound: YM2612 effects, CD music (rendered from the arcade sound program)"
rm -rf $W/fmsfx; python3 $P1/tools/fm/fmsfx.py | tail -1
mkdir -p $W/mus $W/trk
run() { [ -f $W/mus/$2$1.wav ] || GYM=1.0 GOKI=2.5 ROMDIR=$W/rom $R/dw/ref/sndref $1 $3 $W/mus/$2$1.wav 2>/dev/null; }
export -f run; export R W
printf "%s\n" 02 03 04 05 06 07 08 09 0A 0B 0C 0D 15 | xargs -P$J -I{} bash -c 'run {} L 330'
printf "%s\n" 01 0E 0F 10 11 12 13 14 16 17 18 19 1A 1B 1C | xargs -P$J -I{} bash -c 'run {} c 150'
MUSSRC=$W/mus/ python3 $R/dw/tools/mkmus.py $W/trk | grep -c track | sed 's/$/ CD tracks/'

step "8/9 CD data track"
python3 -c "open('$W/base.iso','wb').write(bytes(150*2048))"
python3 $P1/tools/mksnd.py $W/base.iso $D/snd.iso 7f | tail -1

step "9/9 cartridge"
OUT=$W/dw32xm.32x $P1/build.sh | tail -2

N=DynastyWars_32xM
cp $W/dw32xm.32x $OUTD/$N.32x; cp $D/snd.iso $OUTD/$N.iso
{ printf 'FILE "%s.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n' $N
  for t in $(seq 2 29); do f=$(printf DW_MSU_%02d.wav $t); cp $W/trk/$(printf t%02d.wav $t) $OUTD/$f
    printf 'FILE "%s" WAVE\n  TRACK %02d AUDIO\n    INDEX 01 00:00:00\n' $f $t; done; } > $OUTD/$N.cue
( cd $OUTD && sha1sum $N.32x $N.iso )
echo; echo "done: $OUTD/$N.32x (32X cartridge) + $N.cue / .iso / DW_MSU_*.wav (Mega-CD disc)"
