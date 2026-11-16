#!/usr/bin/env python3
"""(32xM V3.22) background palettes the game switches part way through a stage (stage 3's darker stretch, stage 6's
5A, stage 8's second and last sections) -> per arcade palette and per 8-pixel world column the version that is up while
that column is on screen.  convbg4.py PALZONE=<this output> colours each cell with it instead of the snapshot's.
Only versions seen in >= 20 dumps count (fades, flashes and palette cycles stay as they were: tools/bg4/palanim.py).
input: 32X SDRAM dumps of a replay through the stage (lrrun SDRAMEVERY=100 SDRAMLEN=0x20000), stage index, io_cpsa offset
usage: palzone.py DUMPDIR STAGE IO_CPSA_OFS OUT.pkl [PAL ...]   (PAL: only these arcade palettes, hex)"""
import sys, glob, struct, pickle, collections, numpy as np
d, st, io_ofs, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 0), sys.argv[4]
only = {int(p, 16) for p in sys.argv[5:]}
def h(b, a): return struct.unpack('<H', b[a:a + 2])[0]
def cps_rgb(w):
    b = 0x0f + ((w >> 12) << 1); return [((w >> s) & 15) * 0x11 * b // 0x2d for s in (8, 4, 0)]
seen = []                                 # (scroll2 screen-left world x, scroll3 ..., palette bytes 0x40-0x7F)
for f in sorted(glob.glob(d + '/sdr_*.bin')):
    b = open(f, 'rb').read()
    if b[0x81D7 ^ 1] != st: continue      # (work RAM 0xFF81D7: the stage)
    io = [h(b, io_ofs + 2 * i) for i in range(16)]
    base = 0x10000 + ((io[5] * 256) & 0xffff)
    seen.append((io[8] + 96, io[10] + 96, b[base + 0x40 * 32: base + 0x80 * 32]))
W = 4096
res = {2: {}, 3: {}}
for p in range(0x40, 0x80):
    if only and p not in only: continue
    L = 2 if p < 0x60 else 3
    vs = [(s[0] if L == 2 else s[1], s[2][(p - 0x40) * 32:(p - 0x3F) * 32]) for s in seen]
    n = collections.Counter(v for x, v in vs)
    vers = [v for v, c in n.most_common() if c >= 20]
    if len(vers) < 2: continue
    cnt = np.zeros((W, len(vers)), np.int32)
    for x, v in vs:
        if v in vers: c0 = max(0, x >> 3); cnt[c0:c0 + 40, vers.index(v)] += 1
    col = np.zeros(W, np.int32); has = cnt.sum(1) > 0; col[has] = cnt[has].argmax(1)
    idx = np.where(has)[0]
    for c in np.where(~has)[0]: col[c] = col[idx[np.abs(idx - c).argmin()]]
    rgbs = [np.array([cps_rgb(x) for x in struct.unpack('<16H', v)], np.uint8) for v in vers]
    ch = np.where(np.diff(col) != 0)[0]
    print('palette %02X: versions %s, switches at x %s' % (p, [n[v] for v in vers], [int(c + 1) * 8 for c in ch][:8]))
    res[L][p] = (col, rgbs)
pickle.dump(res, open(out, 'wb'))
