#!/usr/bin/env python3
"""(32xM) 4-line version of DW_RE convanim.py: stage 1 army dust (scroll2 tilemap rows 52-55).
usage: convanim4.py BGPREFIX OUTPREFIX snapdir...   (host snapshots: gfx, regs, objs, pal, wram)
out: OUT_tiles.bin / OUT_tline.bin (old + new tiles), OUT_anim.bin: u16 n, n x {u16 code, u16 attr, u16 e[4]} sorted"""
import sys, glob, struct, numpy as np
sys.path.insert(0, '/dw/dw/tools')
import cps, mdpal
bgp, out = sys.argv[1], sys.argv[2]
old = open(bgp + '_tiles.bin', 'rb').read(); oldl = open(bgp + '_tline.bin', 'rb').read()
pw = struct.unpack('>64H', open(bgp + '_pal.bin', 'rb').read())
NL = 4
lines = [[((w >> 1) & 7, (w >> 5) & 7, (w >> 9) & 7) for w in pw[l * 16 + 1:l * 16 + 16]] for l in range(NL)]
codes, attrs, blk = cps.scene_map(0, 2, 32)
WO = 0x30000 + 128 + 0x800 + 0x3000
seen = {}
for d in sys.argv[3:]:
    for fn in sorted(glob.glob(d + '/snap_*.bin')):
        raw = open(fn, 'rb').read()
        if raw[WO + 0x81d7] or raw[WO + 0x81d8]: continue
        s = cps.load_snap(fn); A = s['A']; g = s['g']; base = (A[2] * 256) & 0x3ffff
        for row in range(52, 56):
            for col in range(64):
                idx = (row & 0xf) + ((col & 0x3f) << 4) + ((row & 0x30) << 6); o = base + idx * 4
                c = (g[o] << 8) | g[o + 1]; a = (g[o + 2] << 8) | g[o + 3]
                wr = row - 32
                if any(c == codes[wr, wc] for wc in range(col, 512, 64)): continue
                key = (c, a & 0x7f)
                if key not in seen: seen[key] = s['rgb']
if len(sys.argv) > 3 and False: pass
KEEP = __import__('os').environ.get('KEEP')
if KEEP:                                           # only the cells of an earlier table (the dust itself)
    kb = open(KEEP, 'rb').read(); kn = struct.unpack('>H', kb[:2])[0]
    keep = {struct.unpack('>HH', kb[2 + i * 12:6 + i * 12]) for i in range(kn)}
    miss = keep - set(seen); seen = {k: v for k, v in seen.items() if k in keep}
    if miss and seen and __import__('os').environ.get('FILLMISS'):   # V3.2: cells not in these snapshots: same stage palette
        anyrgb = next(iter(seen.values())); seen.update({k: anyrgb for k in miss}); miss = set()
    print('kept', len(seen), 'missing', len(miss))
print('dynamic cells', len(seen))
tiles = {}; tl = []; tll = []
base_n = len(old) // 32
def addtile(px, line):
    for arr, fl in ((px, 0), (px[:, ::-1], 1), (px[::-1], 2), (px[::-1, ::-1], 3)):
        k = (bytes(((arr[:, 0::2] << 4) | arr[:, 1::2]).astype(np.uint8).ravel()), line)
        if k in tiles: return tiles[k], fl
    b = bytes(((px[:, 0::2] << 4) | px[:, 1::2]).astype(np.uint8).ravel())
    tiles[(b, line)] = base_n + 1 + len(tl); tl.append(b); tll.append(line); return tiles[(b, line)], 0
recs = []
for (c, a), rgb in sorted(seen.items()):
    t = cps.tile(2, c)
    if t is None: continue
    if a & 0x20: t = t[:, ::-1]
    if a & 0x40: t = t[::-1]
    cols = rgb[(0x40 + (a & 0x1f)) * 16 + np.minimum(t, 15).astype(int)]
    m = t != 15
    best = None
    for l in range(NL):
        if not m.any(): best = (0, 0); break
        idx = mdpal.nearest(np.array(lines[l]), cols[m])
        P = mdpal.md_rgb(np.array(lines[l])).astype(np.float32)
        e = ((mdpal.md_rgb(mdpal.to_md3(cols[m])).astype(np.float32) - P[idx]) ** 2).sum()
        if best is None or e < best[0]: best = (e, l)
    l = best[1]
    px = np.zeros(t.shape, np.uint8)
    if m.any(): px[m] = mdpal.nearest(np.array(lines[l]), cols[m]) + 1
    ents = []
    for j in range(2):
        for i in range(2):
            q = px[j * 8:j * 8 + 8, i * 8:i * 8 + 8]
            if not q.any(): ents.append(0); continue
            tid, fl = addtile(q, l)
            ents.append(((fl >> 1) << 13) | ((fl & 1) << 12) | tid)
    recs.append((c, a, ents))
recs.sort()
ob = struct.pack('>H', len(recs)) + b''.join(struct.pack('>6H', c, a, *e) for c, a, e in recs)
open(out + '_anim.bin', 'wb').write(ob)
open(out + '_tiles.bin', 'wb').write(old + b''.join(tl))
open(out + '_tline.bin', 'wb').write(oldl + bytes(tll))
print('records', len(recs), 'new tiles', len(tl), 'total', base_n + len(tl))
