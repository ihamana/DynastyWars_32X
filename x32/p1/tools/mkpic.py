#!/usr/bin/env python3
"""Pre-scaled scroll2/scroll3 pictures for the menu/story scenes (384x224 shown as 320x224: 5/6 across only,
the arcade pixels are narrower than the MD's, so this keeps the picture's shape; v0.x also scaled 5/6 down).

The arcade layer is cut into a world-aligned grid of 3x3-tile periods (48 px for scroll2, 96 px for
scroll3) that become 40/80 output pixels.  Output pixel k of a period covers source [1.2k, 1.2k+1.2);
it belongs to the tile that holds its centre and is the area average of that tile's pixels under it
(transparent if less than half is opaque), put back on the tile's own 16-colour palette.  So every
(tile, flips, x phase, y phase) has its own small pre-scaled picture (13/14 or 26/27 px square),
4bpp, left pixel in the high nibble, pen 15 transparent.  The palette index stays the arcade's
(the 32X uses the live palette: fades keep working).

usage: mkpic.py all.pkl out regions(ofs:size,...)
  out        chunks file (like gfx23.bin) ; out.tab = hash table 4096 x (u32 key, u32 addr)
  key = 0x80000000 | (L==3)<<22 | yph<<20 | xph<<18 | flip<<16 | code"""
import sys, struct, pickle, numpy as np
sys.path.insert(0, sys.path[0])
import cpsr
ST = {2: (0, 13, 27, 40), 3: (0, 27, 53, 80)}          # output start of each phase in a period
if sys.argv[1].endswith('.json'):            # placements + palettes (tools/picmeta.py: no pixels)
    import json
    J = json.load(open(sys.argv[1])); PALS = [np.array(p, '<u4') for p in J['pals']]
    out = sys.argv[2]
    regions = [tuple(int(x, 0) for x in r.split(':')) for r in sys.argv[3].split(',')]
    best = {tuple(k): (0, PALS[p].copy()) for *k, p in J['keys']}
else:
    res = pickle.load(open(sys.argv[1], 'rb'))
    out = sys.argv[2]
    regions = [tuple(int(x, 0) for x in r.split(':')) for r in sys.argv[3].split(',')]
    # placements seen in the scaled scenes, with the brightest palette seen for each
    best = {}
    for fi in res:
        if fi['sel'] or fi['hud'] or fi['scene'] < 8: continue
        for L, (base, scx, scy, ent) in fi['layers'].items():
            pg = 0 if L == 2 else 512
            for (c, r), (code, attr) in ent.items():
                if code == 0 and attr == 0: continue
                if cpsr.tile_pens(L, code) is None: continue
                key = (L, code & (0xffff if L == 2 else 0x3fff), (attr >> 5) & 3, c % 3, 0)
                p = attr & 31
                cols = fi['pal'][pg + p * 16:pg + p * 16 + 16]
                d = best.setdefault(key, {}); t = cols.tobytes()
                d[t] = d.get(t, 0) + 1
    # the palette seen most often for each placement (fades and flashes are short)
    for key, d in best.items():
        t = max(d, key=lambda t: (d[t], sum(np.frombuffer(t, '<u4') & 0xffffff)))
        best[key] = (0, np.frombuffer(t, '<u4').copy())

print('placements', len(best))
import os as _os, json as _json
FIX = {}
_fx = _os.path.join(_os.path.dirname(_os.path.abspath(sys.argv[1])), 'pic_fix.json')
if _os.path.exists(_fx):
    for *k, y, x, v in _json.load(open(_fx)): FIX.setdefault(tuple(k), []).append((y, x, v))
if len(sys.argv) > 4:                       # write the placements + palettes (no pixels) and stop
    import json; P = {}; K = []
    for k, (br, cols) in best.items(): t = tuple(int(v) for v in cols); K.append(list(k) + [P.setdefault(t, len(P))])
    json.dump({'pals': [list(t) for t in P], 'keys': K}, open(sys.argv[4], 'w'), separators=(',', ':')); sys.exit()
def scaled(L, code, fl, xph, yph, cols):
    """sums in a fixed order (no BLAS: the same on every CPU).  The releases up to V3.17 used BLAS matrix products,
    which differ in the last bits on a few half-covered pixels: meta/pic_fix.json puts those back."""
    ts = 16 if L == 2 else 32
    pen = cpsr.tile_pens(L, code)
    if fl & 1: pen = pen[:, ::-1]
    if fl & 2: pen = pen[::-1, :]
    rgb = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], -1).astype(np.float64)
    st = ST[L]; ws = []
    for k in range(st[xph], st[xph + 1]):
        a = 1.2 * k - ts * xph; b = a + 1.2
        w = np.zeros(ts)
        for s_ in range(ts):
            lo, hi = max(a, s_), min(b, s_ + 1)
            if hi > lo: w[s_] = hi - lo
        ws.append(w)
    WX = np.array(ws)
    def mm(A):                                            # A @ WX.T, summed over t = 0, 1, ...
        out = np.zeros((A.shape[0], WX.shape[0]))
        for t in range(ts): out = out + A[:, t, None] * WX[None, :, t]
        return out
    op = (pen != 15).astype(np.float64)
    src = rgb[np.minimum(pen, 15)]
    cov = mm(op); tot = mm(np.ones((ts, ts)))
    acc = np.stack([mm(src[:, :, i] * op) for i in range(3)], -1)
    h, w = cov.shape
    o = np.full((h, w), 15, np.uint8)
    pal15 = rgb[:15]
    for y in range(h):
        for x in range(w):
            if cov[y, x] * 2 < tot[y, x]: continue
            c = acc[y, x] / cov[y, x]
            e = (pal15 - c) ** 2 * (0.30, 0.59, 0.11)
            d = e[:, 0] + e[:, 1] + e[:, 2]
            o[y, x] = int(d.argmin())
    for y, x, v in FIX.get((L, code, fl, xph, yph), ()): o[y, x] = v
    return o
def pack(o):
    h, w = o.shape
    rb = (w + 1) // 2
    q = np.full((h, rb * 2), 15, np.uint8); q[:, :w] = o
    return ((q[:, 0::2] << 4) | q[:, 1::2]).astype(np.uint8).tobytes()
blobs = []; uniq = {}; ent = {}; OUTS = {}
for key, (br, cols) in sorted(best.items()):
    L, code, fl, xph, yph = key
    o = scaled(L, code, fl, xph, yph, cols)
    if (o == 15).all(): continue
    OUTS[key] = o
    u = np.unique(o)
    if len(u) == 1: ent[key] = ('solid', int(u[0])); continue
    b = pack(o)
    if b not in uniq: uniq[b] = len(blobs); blobs.append(b)
    ent[key] = uniq[b]
print('stored', len(ent), 'unique', len(blobs), '%d KB' % (sum(len(b) for b in blobs) // 1024))
# place (first fit, big first)
CART = 0x02000000
order = sorted(range(len(blobs)), key=lambda i: -len(blobs[i]))
used = [0] * len(regions); addr = {}; pieces = []
for i in order:
    b = blobs[i]
    for ri in range(len(regions)):
        if used[ri] + len(b) <= regions[ri][1]: break
    else: sys.exit('pictures do not fit')
    o = regions[ri][0] + used[ri]; addr[i] = CART + o; used[ri] += len(b); pieces.append((o, b))
    used[ri] = (used[ri] + 1) & ~1
pieces.sort(); chunks = []; cur = None
for o, b in pieces:
    if cur and cur[0] + len(cur[1]) == o: cur[1].extend(b)
    else:
        if cur: chunks.append((cur[0], bytes(cur[1])))
        cur = [o, bytearray(b)]
chunks.append((cur[0], bytes(cur[1])))
print('free per region:', [hex(r[1] - u) for r, u in zip(regions, used)])
NT = 4096
tab = [(0, 0)] * NT
for (L, code, fl, xph, yph), i in ent.items():
    k = 0x80000000 | ((L == 3) << 22) | (yph << 20) | (xph << 18) | (fl << 16) | code
    h = ((k * 0x9E3779B1) & 0xffffffff) >> 20
    while tab[h][0]: h = (h + 1) & (NT - 1)
    tab[h] = (k, (0x10 | i[1]) if isinstance(i, tuple) else addr[i])
with open(out, 'wb') as f:
    f.write(struct.pack('>I', len(chunks)))
    for o, b in chunks: f.write(struct.pack('>II', o, len(b))); f.write(b)
open(out + '.tab', 'wb').write(b''.join(struct.pack('>II', k, a) for k, a in tab))

pickle.dump(OUTS, open(out + '.pkl', 'wb'))
