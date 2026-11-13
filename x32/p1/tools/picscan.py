#!/usr/bin/env python3
"""scan host snapshots (SNAPMENU) -> per-frame layer info for scroll2/scroll3 in menu scenes"""
import sys, glob, struct, numpy as np, hashlib, pickle
def tm_index(L, row, col):
    return (row & 0x0f) + ((col & 0x3f) << 4) + ((row & 0x30) << 6) if L == 2 else (row & 0x07) + ((col & 0x3f) << 3) + ((row & 0x38) << 6)
def load(fn):
    b = open(fn, 'rb').read()
    g = b[:0x30000]; o = 0x30000
    cpsa = struct.unpack('<32H', b[o:o + 64]); cpsb = struct.unpack('<32H', b[o + 64:o + 128]); o += 128
    o += 0x800
    pal = np.frombuffer(b[o:o + 0x3000], '<u4'); o += 0x3000
    w = b[o:o + 0x10000]
    return g, cpsa, cpsb, pal, w
def frame_info(fn):
    g, cpsa, cpsb, pal, w = load(fn)
    lc = cpsb[0x2c // 2]; vc = cpsa[0x11]
    out = {'stage': w[0x81D7], 'scene': w[0x81D8], 'lc': lc, 'vc': vc, 'layers': {}}
    s1b = (cpsa[1] * 256) & 0x3c000; s1x, s1y = cpsa[6], cpsa[7]
    def s1cell(sc, sr):
        col = (((64 + s1x) >> 3) + sc) & 63; row = (((16 + s1y) >> 3) + sr) & 63
        i = s1b + ((row & 0x1f) + (col << 5) + ((row & 0x20) << 6)) * 4
        return (g[i] << 8) | g[i + 1]
    s1on = lc & 2
    out['sel'] = bool(s1on and s1cell(13, 21) == 0x60fd and s1cell(25, 21) == 0x60fd and s1cell(37, 23) == 0x60d1 and s1cell(13, 25) == 0x60ed)
    out['hud'] = bool(s1on and not out['sel'] and s1cell(8, 26) == 0x61ff and s1cell(9, 26) == 0x61f7)
    out['pal'] = np.array(pal[1024:2048])
    for L in (2, 3):
        on = (lc & (4 if L == 2 else 8)) and (vc & (4 if L == 2 else 8))
        if not on: continue
        ts = 16 if L == 2 else 32; sh = 4 if L == 2 else 5
        base = (cpsa[L] * 256) & 0x3c000
        scx, scy = cpsa[8 + (L - 2) * 2], cpsa[9 + (L - 2) * 2]
        c0 = ((64 + scx) >> sh); r0 = ((16 + scy) >> sh)
        c1 = ((64 + scx + 383) >> sh); r1 = ((16 + scy + 223) >> sh)
        c0 = c0 // 3 * 3; r0 = r0 // 3 * 3
        nc = (c1 // 3 * 3 + 3) - c0; nr = (r1 // 3 * 3 + 3) - r0
        ent = {}
        for r in range(nr):
            for c in range(nc):
                col = (c0 + c); row = (r0 + r)
                if col > 63 or row > 63: continue
                i = base + tm_index(L, row, col) * 4
                code = (g[i] << 8) | g[i + 1]; attr = (g[i + 2] << 8) | g[i + 3]
                if L == 2: ok = 0x4000 <= (code << 1) <= 0x7fff
                else: ok = (code & 0x3fff) <= 0x7ff
                ent[(col, row)] = (code, attr) if ok else (0, 0)
        out['layers'][L] = (base, scx, scy, ent)
    return out
if __name__ == '__main__':
    res = []
    for d in sys.argv[2:]:
        for fn in sorted(glob.glob(d + '/snap_*.bin')):
            fr = int(fn[-9:-4]); fi = frame_info(fn); fi['run'] = d; fi['fr'] = fr; res.append(fi)
    pickle.dump(res, open(sys.argv[1], 'wb'))
