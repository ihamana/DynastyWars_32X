"""small CPS1 scroll2/scroll3 renderer for host snapshots (numpy)"""
import numpy as np, struct
GFX = None
def gfx():
    global GFX
    if GFX is None: GFX = np.fromfile('/tmp/dw/rom/gfx.bin', np.uint8)
    return GFX
def tile_pens(L, code):
    """-> (ts x ts) pens, or None"""
    g = gfx()
    if L == 2:
        if not (0x4000 <= (code << 1) <= 0x7fff): return None
        c = 0x6000 + (code & 0x1fff); ts = 16; a = g[c * 128:(c + 1) * 128]
    else:
        code &= 0x3fff
        if code > 0x7ff: return None
        c = 0x1000 + code; ts = 32; a = g[c * 512:(c + 1) * 512]
    a = a.reshape(-1, 4)
    bits = np.unpackbits(a, axis=1).reshape(-1, 4, 8)
    pen = (bits[:, 3, :] << 3) | (bits[:, 2, :] << 2) | (bits[:, 1, :] << 1) | bits[:, 0, :]
    return pen.reshape(ts, ts)
def snap_pal(b):
    o = 0x30000 + 128 + 0x800
    return np.frombuffer(b[o:o + 0x3000], '<u4')
