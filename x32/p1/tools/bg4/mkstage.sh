#!/bin/bash
# stage k (k >= 1) MD background: CPS scene k maps -> 4-line MD tiles/maps (Word RAM, CD) + priority cells (cart, 32X)
# usage: mkstage.sh k snapshot.bin   (host snapshot taken in the stage: palette, layer priority masks)
# stage 2 (k 1): EXTRA3=data/bg/s1_extra3.pkl (dynamic scroll3 cells, tools/bg4/dyn3.py)
set -e
k=$1; snap=$2
P=$(cd "$(dirname "$0")/../.." && pwd); D=$P/data/bg; W=/tmp/dw/bgs; mkdir -p $W
python3 $P/tools/bg4/convbg4.py $k $snap $W/s$k | grep -v '^iter'
python3 $P/tools/bg4/bgpack4.py $W/s$k $D/s$k
python3 $P/tools/bg4/mkprimask.py $k $snap $W/pm$k.npy
python3 $P/tools/mkbgpri.py $D/s${k}_mapAflat.bin $D/s${k}_tiles.bin $D/s${k}_pal.bin $D/s${k}_tline.bin $D/s${k}_bgpri.bin $W/pm$k.npy
