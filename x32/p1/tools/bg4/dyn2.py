#!/usr/bin/env python3
"""Stage 2 harbour dynamic cells (the arcade rewrites scroll2 for the ships and some scroll3 cells too).
input: s1_seen.pkl = cells seen in host runs through the stage ({'s2': {(code, attr): positions}, 's3': ...},
/tmp/dw/hv/collect2.py).
outputs:
  s1_extra2.pkl  scroll2 (code, attr) not in the static map (convbg4.py EXTRA2 puts them after the map, 48 per column)
  s1_extra3.pkl  scroll3 extras, new ones appended (the first NANIM animated groups unchanged)
  s1_dyn2.bin    32X table: u32 W, u32 n, n x {u32 key, u32 src} sorted by key, u32 static key [W][48]
                 key = code << 16 | attr & 0x1FF; src = column << 6 | row (MD map cell of the same cell),
                 0xFFFE = nothing (transparent); static key 0 = transparent cell
usage: dyn2.py D (data/bg) [stage, default 1: s{k}_seen.pkl -> s{k}_extra2.pkl, s{k}_dyn2.bin] [first column X0]"""
import sys, pickle, struct, numpy as np
sys.path.insert(0, '/dw/dw/tools'); import cps
D = sys.argv[1]
K = int(sys.argv[2]) if len(sys.argv) > 2 else 1     # V3.1: any stage (stage 4's bridge)
X0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0    # V3.2: static keys only from column X0 (header W | X0 << 16)
X1 = int(sys.argv[4]) if len(sys.argv) > 4 else None # V3.2: and up to X1 (the header W: the 32X looks at columns X0..X1-1)
S = pickle.load(open(D + '/s%d_seen.pkl' % K, 'rb'))
def blank(code):
    t = cps.tile(2, code)
    return t is None or (t == 15).all()
c, a, blk = cps.scene_map(K, 2, 32); cols = np.where(blk.any(axis=0))[0]; W = (cols.max() + 1) * 16
c = c[:, :W]; a = a[:, :W]; H = c.shape[0]
pos = {}
for x in range(W):
    for y in range(H):
        k = (int(c[y, x]), int(a[y, x]) & 0x1FF)
        if not blank(k[0]): pos.setdefault(k, (x, y))
seen = sorted({(k[0], k[1] & 0x1FF) for k in S['s2']})
ex = [k for k in seen if k not in pos and not blank(k[0])]
pickle.dump(ex, open(D + '/s%d_extra2.pkl' % K, 'wb'))
G = 8
for i, k in enumerate(ex): pos[k] = (W + G + i // H, i % H)
tab = {}
for k in seen:
    tab[(k[0] << 16) | k[1]] = 0xFFFE if blank(k[0]) else (pos[k][0] << 6) | pos[k][1]
if X0 == 0:
    for k, (x, y) in pos.items(): tab.setdefault((k[0] << 16) | k[1], (x << 6) | y)
for k in seen:                                      # every transparent code the arcade used
    if blank(k[0]): tab[(k[0] << 16) | k[1]] = 0xFFFE
ent = sorted(tab.items())
st = np.zeros((W, H), np.uint32)
for x in range(W):
    for y in range(H):
        cd = int(c[y, x]); st[x, y] = 0 if blank(cd) else (cd << 16) | (int(a[y, x]) & 0x1FF)
X1 = X1 or W
out = struct.pack('>II', X1 | X0 << 16, len(ent)) + b''.join(struct.pack('>II', k, s) for k, s in ent) + st[X0:X1].astype('>u4').tobytes()
open(D + '/s%d_dyn2.bin' % K, 'wb').write(out)
print('dyn2: W', W, 'extras', len(ex), 'table', len(ent), 'bytes', len(out))
if K != 1: sys.exit(0)
# scroll3: append the new extras
c3, a3, b3 = cps.scene_map(1, 3, 32); cols = np.where(b3.any(axis=0))[0]; W3 = (cols.max() + 1) * 8
st3 = {(int(c3[y, x]), int(a3[y, x]) & 0x7F) for y in range(24) for x in range(W3)}
ex3 = pickle.load(open(D + '/s1_extra3.pkl', 'rb'))
have = {(cd, at & 0x7F) for cd, at in ex3}
new3 = sorted({(k[0] & 0x3FFF, k[1] & 0x7F) for k in S['s3']} - st3 - have)
new3 = [k for k in new3 if k[0]]
if new3:
    ex3 = list(ex3) + new3
    pickle.dump(ex3, open(D + '/s1_extra3.pkl', 'wb'))
print('extra3: +%d -> %d' % (len(new3), len(ex3)))
