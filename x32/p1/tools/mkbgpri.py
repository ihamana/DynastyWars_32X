#!/usr/bin/env python3
"""Cart copy of the stage background's priority cells for the 32X (the full background is on the CD,
which the SH-2 cannot read).  bgpri.bin:
  u16 w, u16 ncells, u8 rowflag[128], u16 pal[32] (2 lines), u32 tiles offset, u16 colidx[w + 1],
  cells {u16 row, u16 entry} per column by row, tiles (32 bytes each, id 1 first)
The pixels are re-indexed to the colours these few tiles use (one 15-colour line, pal[16..31] = 0),
map entries 0x8000 | line bit 0 << 14 | line bit 1 << 13 | id (the tiles are stored already flipped).
V3.18 NG=3 (env): 3 lines; the 3rd line's u16 pal[16] just before the tiles, the tiles offset's top byte = 3.
usage: mkbgpri.py mapA.bin tiles.bin pal.bin tline.bin out"""
import sys, struct, numpy as np
m = open(sys.argv[1], 'rb').read(); w, h, y0 = struct.unpack('>HHH', m[:6])
a = np.zeros((w, y0 + h), np.int64); a[:, y0:] = np.frombuffer(m[6:6 + w * h * 2], '>u2').reshape(w, h); h = y0 + h
tiles = open(sys.argv[2], 'rb').read(); palw = struct.unpack('>64H', open(sys.argv[3], 'rb').read()[:128])
tline = open(sys.argv[4], 'rb').read()
tid = (a & 0xFFF) | ((a >> 2) & 0x1000)               # id bit 12 is in bit 14
pr = ((a & 0x8000) != 0) & (tid != 0)
rows = [y for y in range(h) if pr[:, y].any()]
assert all(y < 128 for y in rows)
# per-pixel priority (optional 6th argument, mkprimask.py): only the front pens of each cell are drawn
pm = np.load(sys.argv[6]) if len(sys.argv) > 6 else None
def tpx(t):                                        # 8x8 pens of MD tile t
    b = np.frombuffer(tiles[(t - 1) * 32:t * 32], np.uint8)
    return np.stack([b >> 4, b & 15], -1).reshape(8, 8)
uniq = {}; ulist = []; cellid = {}                 # (masked pixels, palette line of the source tile) -> new tile
for x in range(w):
    for y in rows:
        if not pr[x, y]: continue
        e = int(a[x, y]); t = int(tid[x, y]); p = tpx(t)
        if e & 0x1000: p = p[:, ::-1]
        if e & 0x2000: p = p[::-1]
        if pm is not None:
            mk = int(pm[y, x]) if y < pm.shape[0] and x < pm.shape[1] else 0
            p = np.where(((mk >> np.arange(64, dtype=np.uint64).reshape(8, 8)) & 1).astype(bool), p, 0)
            if not p.any(): continue
        k = (p.tobytes(), tline[t - 1])
        if k not in uniq: uniq[k] = len(ulist) + 1; ulist.append((p.copy(), tline[t - 1]))
        cellid[(x, y)] = uniq[k]
def tcols(i):
    p, ln = ulist[i - 1]
    return set(palw[ln * 16 + v] for v in p.flatten() if v)
NG = 2
grp = [[], [], [], []]; tl = {}
def pack2():
    """V3.18: tiles -> 2 lines of 15 colours, every tile's colours in its own line.  Local search over the distinct
    colour sets (the V3.17 greedy fill left stages 5-8 with up to 30 needed colours but ~200-350 cells on substitutes)."""
    import random, os
    sets = {}
    for i in range(1, len(ulist) + 1): sets.setdefault(frozenset(tcols(i)), []).append(i)
    S = sorted(sets, key=lambda c: -len(c)); W = [len(sets[c]) for c in S]
    def cost(asg):
        u = [set() for g in range(NG)]
        for c, g in zip(S, asg): u[g] |= c
        return sum(max(0, len(x) - 15) for x in u), u
    best = None
    rng = random.Random(1)
    for rs in range(int(os.environ.get("RESTARTS", "200"))):
        asg = [rng.randrange(NG) if rs else 0 for _ in S]
        if rs == 0:                                    # greedy start, as before
            u = [set() for g in range(NG)]
            for j, c in enumerate(S):
                fit = [g for g in range(NG) if len(u[g] | c) <= 15]
                g = fit[0] if fit else max(range(NG), key=lambda g: len(c & u[g]))
                asg[j] = g; u[g] |= c
        cur, _ = cost(asg)
        improved = True
        while improved and cur:
            improved = False
            for j in rng.sample(range(len(S)), len(S)):
                o = asg[j]
                for g in range(NG):
                    if g == o: continue
                    asg[j] = g; c2, _ = cost(asg)
                    if c2 < cur: cur = c2; improved = True; o = g; break
                asg[j] = o
        if best is None or cur < best[0]: best = (cur, asg[:])
        if cur == 0: break
    cur, asg = best; _, u = cost(asg)
    for c, g in zip(S, asg):
        for i in sets[c]: tl[i] = g
    for g in range(NG):
        # colours used by more tiles first, at most 15 (overflow: nearest)
        cnt = {}
        for c, gg, w in zip(S, asg, W):
            if gg == g:
                for x in c: cnt[x] = cnt.get(x, 0) + w
        grp[g] = sorted(cnt, key=lambda x: (-cnt[x], x))[:15]
    return cur
import os
NG = int(os.environ.get('NG', '2'))
ov = pack2()
def md_rgb(c): return ((c >> 1) & 7, (c >> 5) & 7, (c >> 9) & 7)
def near(g, c):
    if c in grp[g]: return grp[g].index(c) + 1
    r = md_rgb(c); return 1 + min(range(len(grp[g])), key=lambda i: sum((a - b) ** 2 for a, b in zip(md_rgb(grp[g][i]), r)))
rt = []
for i in range(1, len(ulist) + 1):
    p, ln = ulist[i - 1]; g = tl[i]
    q = np.vectorize(lambda v: near(g, palw[ln * 16 + v]) if v else 0)(p).astype(np.uint8)
    rt.append(bytes((q[:, 0::2] << 4 | q[:, 1::2]).flatten()))
ids = ulist
pal = struct.pack('>32H', 0, *(grp[0] + [0] * (15 - len(grp[0]))), 0, *(grp[1] + [0] * (15 - len(grp[1]))))
cix = sum(grp[:NG], [])
rowidx = bytearray([0xFF] * 128)
for i, y in enumerate(rows): rowidx[y] = i
rows = sorted(set(y for x, y in cellid))
ent = []; colidx = []
for x in range(w):
    colidx.append(len(ent))
    for y in rows:
        if (x, y) in cellid:
            i = cellid[(x, y)]; assert i < 0x1000; ent.append((y, 0x8000 | ((tl[i] & 1) << 14) | ((tl[i] >> 1) << 13) | i))   # V3.18: line bit 1 in bit 13
colidx.append(len(ent))
assert len(ent) < 65536
rowflag = bytes(1 if y in rows else 0 for y in range(128))
hdr = bytearray(struct.pack('>HH', w, len(ent))) + rowflag + pal + bytes(4)
out = hdr + struct.pack('>%dH' % (w + 1), *colidx)
if len(out) & 3: out += bytes(4 - (len(out) & 3))
for y, e in ent: out += struct.pack('>HH', y, e)
if NG > 2: out += struct.pack('>16H', 0, *(grp[2] + [0] * (15 - len(grp[2]))))   # V3.18: a 3rd line just before the tiles
tofs = len(out); struct.pack_into('>I', out, 196, tofs | (NG << 24 if NG > 2 else 0))   # (top byte: lines, when 3)
for b in rt: out += b
open(sys.argv[5], 'wb').write(out)
print('bgpri: rows %d, cells %d, tiles %d, colours %d, overflow %d, %d bytes' % (len(rows), len(ent), len(ids), len(cix), ov, len(out)))
