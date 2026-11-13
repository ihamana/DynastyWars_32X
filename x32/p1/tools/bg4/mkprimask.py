"""Per-pixel priority mask of a scene's scroll2 (the pens that the CPS1 layer priority masks put in front of the
sprites), one u64 per 8x8 cell (bit r*8+c, screen orientation).  usage: mkprimask.py SCENE SNAP out.npy"""
import sys, numpy as np
sys.path.insert(0, '/dw/dw/tools')
import cps
scene = int(sys.argv[1]); snap = cps.load_snap(sys.argv[2]); B = snap['B']
PRIO = [B[0x2a // 2], B[0x28 // 2], B[0x26 // 2], B[0x24 // 2]]
c, a, blk = cps.scene_map(scene, 2, 32)
cols = np.where(blk.any(axis=0))[0]; W = (cols.max() + 1) * 16; c = c[:, :W]; a = a[:, :W]
H = c.shape[0]
m = np.zeros((H * 16, W * 16), bool)
for y in range(H):
    for x in range(W):
        at = int(a[y, x]); grp = (at >> 7) & 3
        if not grp or not PRIO[grp]: continue
        t = cps.tile(2, int(c[y, x]))
        if t is None: continue
        if at & 0x20: t = t[:, ::-1]
        if at & 0x40: t = t[::-1]
        m[y * 16:(y + 1) * 16, x * 16:(x + 1) * 16] = np.isin(t, [p for p in range(15) if PRIO[grp] >> p & 1])
cm = m.reshape(H * 2, 8, W * 2, 8).transpose(0, 2, 1, 3).reshape(H * 2, W * 2, 64)
out = (cm.astype(np.uint64) << np.arange(64, dtype=np.uint64)).sum(-1).astype(np.uint64)
np.save(sys.argv[3], out); print('primask', out.shape, int((out != 0).sum()), 'cells')
