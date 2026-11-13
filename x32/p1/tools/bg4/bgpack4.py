#!/usr/bin/env python3
"""(32xM) pack the 4-line background (convbg4.py output, no tile merging: the 32X version has the whole MD
VRAM for the background).  A tile is stored once per palette line it is used with, so the map entry has
no line bits: pri<<15 | vflip<<13 | hflip<<12 | id (1..4095); OUT_tline.bin = line of each tile (u8, id 1 first).
usage: bgpack4.py PRE OUT"""
import numpy as np, sys, struct
sys.path.insert(0, '/dw/dw/tools')
import mdpal
pre, out = sys.argv[1], sys.argv[2]
A = np.load(pre + '_mapA.npy'); Bm = np.load(pre + '_mapB.npy'); T = np.load(pre + '_tiles.npy'); L = np.load(pre + '_lines.npy')
newid = {}; tiles = []; tline = []
def conv(M):
    h, w = M.shape; O = np.zeros((h, w), np.uint16)
    for y in range(h):
        for x in range(w):
            v = int(M[y, x])
            if v == 0: continue
            tid = v & 0xffff; line = (v >> 16) & 3; fl = (v >> 18) & 3; pri = (v >> 20) & 1
            k = (tid, line)
            if k not in newid: newid[k] = len(tiles) + 1; tiles.append(T[tid - 1]); tline.append(line)
            i = newid[k]; O[y, x] = (pri << 15) | ((i >> 12) << 14) | ((fl >> 1) << 13) | ((fl & 1) << 12) | (i & 0xFFF)   # id bit 12 in bit 14
    return O
MA = conv(A); MB = conv(Bm)
print('tiles', len(tiles), 'mapA', MA.shape, 'mapB', MB.shape)
assert len(tiles) < 8192
tb = bytearray()
for t in tiles: tb += bytes(((t[:, 0::2] << 4) | t[:, 1::2]).astype(np.uint8).ravel())
open(out + '_tiles.bin', 'wb').write(tb)
open(out + '_tline.bin', 'wb').write(bytes(tline))
for nm, M, bs in (('A', MA, 1), ('B', MB, 2)):         # blocks: the arcade tiles (scroll2 16x16 = 2x2 cells, scroll3 32x32 = 4x4)
    n = 1 << bs
    rows = np.where((M != 0).any(1))[0]               # only the block rows with cells (header: w, h, first row)
    y0, y1 = (int(rows.min()) & ~(n - 1), (int(rows.max()) + n) & ~(n - 1)) if len(rows) else (0, n)
    C = M[y0:y1]; h, w = C.shape
    w2 = (w + n - 1) & ~(n - 1); C = np.pad(C, ((0, 0), (0, w2 - w)))
    open(out + '_map%sflat.bin' % nm, 'wb').write(struct.pack('>HHH', w, h, y0) + C[:, :w].T.astype('>u2').tobytes())
    nbx, nby = w2 >> bs, h >> bs
    bid = {}; blocks = []; bmap = np.zeros((nbx, nby), np.uint16)
    for bx in range(nbx):
        for by in range(nby):
            b = C[by * n:(by + 1) * n, bx * n:(bx + 1) * n].astype('>u2').tobytes()
            if b not in bid: bid[b] = len(blocks); blocks.append(b)
            bmap[bx, by] = bid[b]
    data = struct.pack('>HHHHHH', w, h, y0, bs, nbx, nby) + bmap.astype('>u2').tobytes() + b''.join(blocks)
    open(out + '_map%s.bin' % nm, 'wb').write(data)
    print('map', nm, 'rows', y0, y1, 'blocks', len(blocks), '%d KB (flat %d KB)' % (len(data) // 1024, w * h * 2 // 1024))
pw = b''
for l in L:
    l = [c for c in l]
    pw += struct.pack('>16H', 0, *[mdpal.md_word(c) for c in l[:15]])
open(out + '_pal.bin', 'wb').write(pw)
print('palette lines', len(L))
