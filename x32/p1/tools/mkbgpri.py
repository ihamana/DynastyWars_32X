#!/usr/bin/env python3
"""Cart copy of the stage background's priority cells for the 32X (the full background is on the CD,
which the SH-2 cannot read).  bgpri.bin:
  u16 w, u16 ncells, u8 rowflag[128], u16 pal[32] (2 lines), u32 tiles offset, u16 colidx[w + 1],
  cells {u16 row, u16 entry} per column by row, tiles (32 bytes each, id 1 first)
The pixels are re-indexed to the colours these few tiles use (one 15-colour line, pal[16..31] = 0),
map entries pri<<15 | vflip<<13 | hflip<<12 | id (the tiles' own palette lines come from tline.bin).
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
grp = [[], []]; tl = {}
for i in sorted(range(1, len(ulist) + 1), key=lambda i: -len(tcols(i))):
    c = tcols(i)
    for g in (0, 1):
        if len(set(grp[g]) | c) <= 15:
            for x in c:
                if x not in grp[g]: grp[g].append(x)
            tl[i] = g; break
    else:                                          # no room: the line with the most of its colours, nearest for the rest
        g = max((0, 1), key=lambda g: len(c & set(grp[g]))); tl[i] = g
        for x in c:
            if x not in grp[g] and len(grp[g]) < 15: grp[g].append(x)
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
cix = grp[0] + grp[1]
rowidx = bytearray([0xFF] * 128)
for i, y in enumerate(rows): rowidx[y] = i
rows = sorted(set(y for x, y in cellid))
ent = []; colidx = []
for x in range(w):
    colidx.append(len(ent))
    for y in rows:
        if (x, y) in cellid:
            i = cellid[(x, y)]; ent.append((y, 0x8000 | (tl[i] << 14) | i))
colidx.append(len(ent))
assert len(ent) < 65536
rowflag = bytes(1 if y in rows else 0 for y in range(128))
hdr = bytearray(struct.pack('>HH', w, len(ent))) + rowflag + pal + bytes(4)
out = hdr + struct.pack('>%dH' % (w + 1), *colidx)
if len(out) & 3: out += bytes(4 - (len(out) & 3))
for y, e in ent: out += struct.pack('>HH', y, e)
tofs = len(out); struct.pack_into('>I', out, 196, tofs)
for b in rt: out += b
open(sys.argv[5], 'wb').write(out)
print('bgpri: rows %d, cells %d, tiles %d, colours %d, %d bytes' % (len(rows), len(ent), len(ids), len(cix), len(out)))
