#!/usr/bin/env python3
"""Stage 2 harbour water animation for the MD: the arcade steps the water cells' codes (4 phases); the MD map
always shows phase 0 and a phase change rewrites the patterns of the phase-0 tiles in VRAM.
out: u16 n, u16 id[n] (sorted phase-0 tile ids), pad to 4, then n x 4 phases x 32 bytes (patterns, already
flipped like the phase-0 map entry).  Water groups: extra scroll3 cells (convbg4 EXTRA3) at column W3 + i/24,
row i%24, NANIM entries in groups of 4 rows.
usage: wanim.py mapBflat.bin tiles.bin W3 NANIM out.bin"""
import sys, os, struct, numpy as np
m = open(sys.argv[1], 'rb').read(); w, h, y0 = struct.unpack('>HHH', m[:6])
a = np.frombuffer(m[6:6 + w * h * 2], '>u2').reshape(w, h)
T = open(sys.argv[2], 'rb').read(); W3 = int(sys.argv[3]); NA = int(sys.argv[4])
def pat(e):
    i = (e & 0xFFF) | ((e >> 2) & 0x1000)
    if not i: return np.zeros((8, 8), np.uint8)
    b = np.frombuffer(T[(i - 1) * 32:i * 32], np.uint8)
    p = np.stack([b >> 4, b & 15], -1).reshape(8, 8)
    if e & 0x1000: p = p[:, ::-1]
    if e & 0x2000: p = p[::-1]
    return p
def tid(e): return (e & 0xFFF) | ((e >> 2) & 0x1000)
TLINE = open(os.path.join(os.path.dirname(sys.argv[2]), os.path.basename(sys.argv[2]).replace('_tiles.bin', '_tline.bin')), 'rb').read()
PALW = np.frombuffer(open(os.path.join(os.path.dirname(sys.argv[2]), os.path.basename(sys.argv[2]).replace('_tiles.bin', '_pal.bin')), 'rb').read(), '>u2').reshape(-1, 16)
def rgb3(c): return np.array([(c >> 1) & 7, (c >> 5) & 7, (c >> 9) & 7])
NREMAP = [0]
def recolor(p, e, e0):                  # V3.2: the phase tile is on another palette line than the tile it is shown as:
    l, l0 = TLINE[tid(e) - 1], TLINE[tid(e0) - 1]   # its pens to the nearest colours of that tile's line
    if l == l0 or not tid(e) or not tid(e0): return p
    NREMAP[0] += 1
    src = [rgb3(int(PALW[l][k])) for k in range(16)]; dst = [rgb3(int(PALW[l0][k])) for k in range(1, 16)]
    m = [0] + [1 + int(np.argmin([((src[k] - d) ** 2).sum() for d in dst])) for k in range(1, 16)]
    return np.array(m, np.uint8)[p]
def unflip(p, e):                       # pattern to store under the phase-0 tile so that entry e shows p
    if e & 0x1000: p = p[:, ::-1]
    if e & 0x2000: p = p[::-1]
    return p
# tiles that also show something that is not water (a non-animated arcade cell, or plane A) are left alone:
# rewriting their patterns would put water into those cells
import pickle
sys.path.insert(0, '/dw/dw/tools'); import cps
ex3 = pickle.load(open(os.path.dirname(sys.argv[1]) + '/s1_extra3.pkl', 'rb'))
c3, a3, b3 = cps.scene_map(1, 3, 32)
anim_codes = {ex3[i][0] & 0x3FFF for i in range(NA)}
def tid(e): return (e & 0xFFF) | ((e >> 2) & 0x1000)
bad = set()
for x in range(w):
    ac = x // 4
    for yy in range(h):
        y = yy + y0; ay = y // 4
        if ac < W3: code = int(c3[ay, ac]) & 0x3FFF if ay < c3.shape[0] and ac < c3.shape[1] else 0
        else:
            i = (ac - W3) * 24 + ay
            code = ex3[i][0] & 0x3FFF if 0 <= i < len(ex3) else 0
        if code not in anim_codes: bad.add(tid(int(a[x, yy])))
ma = open(os.path.dirname(sys.argv[1]) + '/s1_mapAflat.bin', 'rb').read(); wa, ha, ya = struct.unpack('>HHH', ma[:6])
bad |= {tid(int(v)) for v in np.frombuffer(ma[6:6 + wa * ha * 2], '>u2')}
ent = {}; conflict = set()
for g in range(NA // 4):
    ix = g * 4; col = W3 + ix // 24; row = ix % 24
    for j in range(4):
        for i in range(4):
            x = col * 4 + i
            e = [int(a[x, (row + p) * 4 + j - y0]) for p in range(4)]
            i0 = (e[0] & 0xFFF) | ((e[0] >> 2) & 0x1000)
            if not i0: continue
            v = [unflip(recolor(pat(e[p]), e[p], e[0]), e[0]) for p in range(4)]
            if i0 in ent:
                if any((x != y).any() for x, y in zip(ent[i0], v)): conflict.add(i0)
                continue
            ent[i0] = v
# V3.1: also the water of the static map (most of the sea: the map has all 4 phases of a group side by side, each cell
# stepping from its own phase): the tile of a cell in phase p0 takes the patterns of its group's phases p0 .. p0 + 3
exidx = {(ex3[i][0] & 0x3FFF, ex3[i][1] & 0x7F): i for i in range(NA) if i % 4 == 0}
if os.environ.get('WSTATIC', '1') == '1':
    for x in range(min(w, W3 * 4)):
        for yy in range(h):
            y = yy + y0; ac, ay = x // 4, y // 4
            if ay >= c3.shape[0] or ac >= c3.shape[1]: continue
            code = int(c3[ay, ac]) & 0x3FFF; key = (code & ~3, int(a3[ay, ac]) & 0x7F)
            if key not in exidx: continue
            e0 = int(a[x, yy]); t = tid(e0)
            if not t: continue
            g = exidx[key]; p0 = code & 3; i, j = x & 3, y & 3; v = []
            for k in range(4):
                ix = g + ((p0 + k) & 3); col = W3 + ix // 24; row = ix % 24
                ek = int(a[col * 4 + i, row * 4 + j - y0]); v.append(unflip(recolor(pat(ek), ek, e0), e0))
            if t in ent:
                if any((p != q).any() for p, q in zip(ent[t], v)): conflict.add(t)
                continue
            ent[t] = v
for i in list(ent):
    if i in bad or i in conflict: del ent[i]
print('wanim: shared tiles left out', len(bad & set(ent) | conflict))
ids = sorted(ent)
out = struct.pack('>H', len(ids)) + b''.join(struct.pack('>H', i) for i in ids)
while len(out) & 3: out += b'\0'
for i in ids:
    for p in ent[i]: out += bytes(((p[:, 0::2] << 4) | p[:, 1::2]).astype(np.uint8).ravel())
print('wanim: phase tiles recoloured', NREMAP[0])
open(sys.argv[5], 'wb').write(out); print('wanim', len(ids), 'tiles', len(out), 'bytes')
