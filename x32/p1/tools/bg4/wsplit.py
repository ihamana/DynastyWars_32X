#!/usr/bin/env python3
"""(V3.1) Stage 2 water: give the animated water cells of plane B tiles of their own.  The packed tiles are shared
by every cell with the same 8x8 pattern, so most water tiles were also used by cells in another phase or by
something that is not water, and wanim.py had to leave them out (the open sea stood still).  Here a static water
cell whose tile also has another use (another phase sequence, a non-water cell, plane A) gets a copy of the tile.
Rewrites s1_tiles.bin / s1_tline.bin / s1_mapBflat.bin / s1_mapB.bin in place (run once after bgpack4; then wanim.py).
usage: wsplit.py D W3 NANIM"""
import sys, os, struct, pickle, numpy as np, collections
sys.path.insert(0, '/dw/dw/tools'); import cps
D, W3, NA = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mb = open(D + '/s1_mapBflat.bin', 'rb').read(); w, h, y0 = struct.unpack('>HHH', mb[:6])
a = np.frombuffer(mb[6:6 + w * h * 2], '>u2').reshape(w, h).copy()
T = bytearray(open(D + '/s1_tiles.bin', 'rb').read()); TL = bytearray(open(D + '/s1_tline.bin', 'rb').read())
ma = open(D + '/s1_mapAflat.bin', 'rb').read(); wa, ha, ya = struct.unpack('>HHH', ma[:6])
def tid(e): return (e & 0xFFF) | ((e >> 2) & 0x1000)
def setid(e, i): return (e & 0xA000 & ~0x4000) | (e & 0x3000) | ((i >> 12) << 14) | (i & 0xFFF)
useA = {tid(int(v)) for v in np.frombuffer(ma[6:6 + wa * ha * 2], '>u2')}
ex3 = pickle.load(open(D + '/s1_extra3.pkl', 'rb'))
c3, a3, b3 = cps.scene_map(1, 3, 32)
exidx = {(ex3[i][0] & 0x3FFF, ex3[i][1] & 0x7F): i for i in range(NA) if i % 4 == 0}
anim_codes = {ex3[i][0] & 0x3FFF for i in range(NA)}
def cellcode(x, y):
    ac, ay = x // 4, y // 4
    if ac < W3: return (int(c3[ay, ac]) & 0x3FFF, int(a3[ay, ac]) & 0x7F) if ay < c3.shape[0] and ac < c3.shape[1] else (0, 0)
    i = (ac - W3) * 24 + ay
    return (ex3[i][0] & 0x3FFF, ex3[i][1] & 0x7F) if 0 <= i < len(ex3) else (0, 0)
def sig(x, yy):                       # the 4 phase entries (tile, flips) a static water cell shows, or None
    y = yy + y0; code, attr = cellcode(x, y)
    if x >= W3 * 4:                       # an extra cell: the phase-0 row of a group is what the MD shows (32X dyn3)
        ix = (x // 4 - W3) * 24 + y // 4
        if not (0 <= ix < NA and ix % 4 == 0): return None
        g = ix; p0 = 0
    else:
        k = (code & ~3, attr)
        if k not in exidx: return None
        g = exidx[k]; p0 = code & 3
    i, j = x & 3, y & 3; s = []
    for p in range(4):
        ix = g + ((p0 + p) & 3); col = W3 + ix // 24; row = ix % 24
        e = int(a[col * 4 + i, row * 4 + j - y0]); s.append((tid(e), (e >> 12) & 3))
    return (tuple(s), (int(a[x, yy]) >> 12) & 3)
# uses of each tile: (signature) for the water cells shown, 'other' for the rest
uses = collections.defaultdict(set)
sigs = {}
for x in range(w):
    for yy in range(h):
        t = tid(int(a[x, yy]))
        if not t: continue
        s = sig(x, yy)
        if s is None: uses[t].add('other'); continue
        sigs[(x, yy)] = s; uses[t].add(s)
for t in useA: uses[t].add('other')
newid = {}; n0 = len(TL); added = 0
for (x, yy), s in sigs.items():
    e = int(a[x, yy]); t = tid(e)
    if len(uses[t]) == 1: continue           # its own already
    key = (t, s)
    if key not in newid:
        i = len(TL) + 1
        T += T[(t - 1) * 32:t * 32]; TL.append(TL[t - 1]); newid[key] = i; added += 1
    a[x, yy] = setid(e, newid[key])
assert len(TL) < 8192
print('wsplit: %d water cells, %d tiles copied (%d -> %d)' % (len(sigs), added, n0, len(TL)))
open(D + '/s1_tiles.bin', 'wb').write(T); open(D + '/s1_tline.bin', 'wb').write(TL)
open(D + '/s1_mapBflat.bin', 'wb').write(struct.pack('>HHH', w, h, y0) + a.astype('>u2').tobytes())
# block map (as bgpack4.py: 4x4 cells for scroll3)
C = a.T; bs = 2; n = 1 << bs
w2 = (w + n - 1) & ~(n - 1); C = np.pad(C, ((0, 0), (0, w2 - w)))
nbx, nby = w2 >> bs, h >> bs
bid = {}; blocks = []; bmap = np.zeros((nbx, nby), np.uint16)
for bx in range(nbx):
    for by in range(nby):
        b = C[by * n:(by + 1) * n, bx * n:(bx + 1) * n].astype('>u2').tobytes()
        if b not in bid: bid[b] = len(blocks); blocks.append(b)
        bmap[bx, by] = bid[b]
data = struct.pack('>HHHHHH', w, h, y0, bs, nbx, nby) + bmap.astype('>u2').tobytes() + b''.join(blocks)
open(D + '/s1_mapB.bin', 'wb').write(data); print('mapB blocks', len(blocks), '%d KB' % (len(data) // 1024))
