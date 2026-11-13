#!/usr/bin/env python3
"""(V2.1) Pictures of the scaled scenes that the cart has no room for (ROUND maps of rounds 2-8, the generals at the
end of part 1), shown by the MD on plane A from the stage background blob in Word RAM (tools/mksnd.py appends
data/mdpic/blobK.bin to stage K's blob; md/bg.c pic_show).

Each picture: arcade scroll2/scroll3 of a host snapshot (layer order from the CPS-B layer control) rendered at
384x224, shown 5/6 across like the 32X does (output pixel o <- source (6o+3)//5, the other touched pixel when that
one is transparent), cut into 8x8 cells with 4 palette lines of 15 colours (weighted k-means), tiles deduplicated
with flips.  Blob part: u16 n, pad, n x { u16 id, u16 ntiles, u32 tiles, u32 map, u32 pal } (offsets from the
part's start), then the data: map 40 x 28 VRAM words (TILE_BG0 = 16 based), palette 64 CRAM words.
The 32X picks the picture (sh2/render.c mdpic_pick) and sends its brightness: a reference colour of the picture
(the 32X's palette index and its level at full brightness) compared with the live palette.

usage: mdpic.py   (the picture list is below; writes data/mdpic/blob*.bin and data/mdpic.h)"""
import sys, os, struct, collections, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, '/dw/dw/tools')
import picscan, cpsr, mdpal
CAP = os.environ.get('CAP', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'mdpic', 'src') + '/')   # host snapshots (copied from /tmp/dw/cap)
OUT = os.environ.get('OUT', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data'))
TILE_BG0 = 16
# (id, blob stage, snapshot, kind) kind 0: the ROUND map of that stage (the 32X: ROUND layout, stage = blob stage),
# kind 1: matched by scroll3 position + 4 cells (the 32X: scaled scene, scroll3 on)
PICS = [
    (1, 1, 's0/snap_14760.bin', 0),
    (2, 2, 's1b/snap_ROUND3.bin', 0),
    (3, 3, 's2/snap_17684.bin', 0),
    (4, 4, 's3/snap_22840.bin', 0),
    (5, 5, 's4/snap_63148.bin', 0),
    (6, 6, 's5/snap_26688.bin', 0),
    (7, 7, 's6/snap_77264.bin', 0),
    (8, 3, 's2/snap_17660.bin', 1),
    # V3.0: the stage-clear pictures (the defeated general / story picture on scroll3; Zhang Fei on scroll2 stays on the 32X)
    (9, 1, 'clr1/snap_66600.bin', 1, (3,)),
    (10, 2, 'clr2/snap_17500.bin', 1, (3,)),
    (11, 3, 'clr3/snap_22600.bin', 1, (3,)),
    (12, 4, 'clr4/snap_63000.bin', 1, (3,)),
    (13, 5, 'clr5/snap_26500.bin', 1, (3,)),
    (14, 6, 'clr6/snap_77000.bin', 1, (3,)),
]
T5 = [[(v * 0x11 * (0x0f + b * 2) // 0x2d) >> 3 for v in range(16)] for b in range(16)]   # sh2/render.c t5
def lum(raw): return T5[raw >> 12][(raw >> 8) & 15] + T5[raw >> 12][(raw >> 4) & 15] + T5[raw >> 12][raw & 15]

def render(fn, only=(2, 3)):
    """-> rgb (224,384,3), opaque mask, (32X palette index, raw) per pixel, frame info, raw palette"""
    fi = picscan.frame_info(fn); b = open(fn, 'rb').read()
    g = b[:0x30000]; cpsa = struct.unpack('<32H', b[0x30000:0x30040])
    pb = (cpsa[5] * 256) & 0x3ffff
    raw = np.frombuffer(g[pb:pb + 0x1000], '>u2')            # 2048 colours (32X cpspal order)
    pal = cpsr.snap_pal(b)
    img = np.zeros((224, 384, 3), np.uint8); m = np.zeros((224, 384), bool); idx = np.zeros((224, 384), np.int32)
    lc = fi['lc']
    for k in range(4):
        L = (lc >> (6 + 2 * k)) & 3
        if L not in only or L not in fi['layers']: continue
        base, scx, scy, ent = fi['layers'][L]; ts = 16 if L == 2 else 32; pg = 1024 if L == 2 else 1536
        for (c, r), (code, attr) in ent.items():
            p = cpsr.tile_pens(L, code)
            if p is None or (code == 0 and attr == 0): continue
            if attr & 0x20: p = p[:, ::-1]
            if attr & 0x40: p = p[::-1, :]
            X = c * ts - (64 + scx); Y = r * ts - (16 + scy)
            x0, y0 = max(X, 0), max(Y, 0); x1, y1 = min(X + ts, 384), min(Y + ts, 224)
            if x0 >= x1 or y0 >= y1: continue
            sub = p[y0 - Y:y1 - Y, x0 - X:x1 - X]; o = sub != 15
            cols = pal[pg + (attr & 31) * 16: pg + (attr & 31) * 16 + 16]
            rgb = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], -1).astype(np.uint8)
            ri = img[y0:y1, x0:x1]; ri[o] = rgb[sub[o]]
            mi = m[y0:y1, x0:x1]; mi[o] = True
            ii = idx[y0:y1, x0:x1]; ii[o] = ((64 if L == 2 else 96) + (attr & 31)) * 16 + sub[o].astype(np.int32)
    return img, m, idx, fi, raw

def scale(a, m):
    out = np.zeros((a.shape[0], 320) + a.shape[2:], a.dtype); om = np.zeros((a.shape[0], 320), bool)
    for o in range(320):
        p = (6 * o + 3) // 5; lo, hi = (6 * o) // 5, (6 * o + 5) // 5; alt = hi if p == lo else lo
        p, alt = min(p, 383), min(alt, 383)
        use = np.where(m[:, p], p, alt)
        out[:, o] = a[np.arange(a.shape[0]), use]; om[:, o] = m[np.arange(a.shape[0]), use]
    return out, om

def quantize(img, m, NL=4):
    cells = []
    for cy in range(28):
        for cx in range(40):
            mm = m[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]
            if not mm.any(): continue
            cols = mdpal.md_rgb(mdpal.to_md3(img[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8][mm]))
            u, cnt = np.unique(cols, axis=0, return_counts=True)
            cells.append((cy, cx, u.astype(np.float32), cnt))
    def fit(sel):
        acc = collections.Counter()
        for i in sel:
            for c, k in zip(map(tuple, cells[i][2]), cells[i][3]): acc[c] += int(k)
        X = np.array(list(acc.keys()), np.float32); W = np.array(list(acc.values()), np.float64)
        return mdpal.kmeans(X, W, 15)
    br = np.array([(c[2].sum(1) * c[3]).sum() / c[3].sum() for c in cells])
    q = np.quantile(br, np.linspace(0, 1, NL + 1)[1:-1])
    assign = np.searchsorted(q, br)
    lines = [fit(np.where(assign == l)[0]) if (assign == l).any() else np.zeros((1, 3), int) for l in range(NL)]
    def err(c, l):
        P = mdpal.md_rgb(lines[l]).astype(np.float32)
        return ((((c[2][:, None, :] - P[None]) * mdpal.CW) ** 2).sum(-1).min(1) * c[3]).sum()
    best = None
    for it in range(8):
        E = np.array([[err(c, l) for l in range(NL)] for c in cells]); assign = E.argmin(1); tot = E.min(1).sum()
        if best is None or tot < best[0]: best = (tot, [l.copy() for l in lines])
        lines = [fit(np.where(assign == l)[0]) if (assign == l).any() else lines[l] for l in range(NL)]
    return best[1]

def build(img, m, lines):
    tiles = {}; tl = []
    def addtile(px):
        for arr, fl in ((px, 0), (px[:, ::-1], 1), (px[::-1], 2), (px[::-1, ::-1], 3)):
            k = arr.tobytes()
            if k in tiles: return tiles[k], fl
        tiles[px.tobytes()] = len(tl); tl.append(px.copy()); return tiles[px.tobytes()], 0
    M = np.zeros((28, 40), np.uint16)
    for cy in range(28):
        for cx in range(40):
            mm = m[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8]
            if not mm.any(): continue
            cols = img[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8].astype(np.float32)
            best = None
            for li in range(4):
                ix = mdpal.nearest(lines[li], cols)
                e = ((((mdpal.md_rgb(lines[li])[ix].astype(np.float32) - cols) * mdpal.CW) ** 2).sum(-1))[mm].sum()
                if best is None or e < best[0]: best = (e, li, ix)
            _, li, ix = best
            px = np.where(mm, ix + 1, 0).astype(np.uint8)
            t, fl = addtile(px)
            M[cy, cx] = (li << 13) | ((fl >> 1) << 12) | ((fl & 1) << 11) | (TILE_BG0 + t)
    return tl, M

def tilebytes(px):
    return b''.join(bytes(((int(r[i]) << 4) | int(r[i + 1])) for i in range(0, 8, 2)) for r in px)

def ref_colour(idx, m, raw):
    cnt = collections.Counter(idx[m].tolist())
    best = max(cnt, key=lambda i: cnt[i] * (lum(int(raw[i])) ** 2))
    return best, lum(int(raw[best]))

blobs = collections.defaultdict(list); hdr = []
for pe in PICS:
    pid, st, fn, kind = pe[:4]; only = pe[4] if len(pe) > 4 else (2, 3)
    path = CAP + fn
    if not os.path.exists(path): print('missing', fn); continue
    img, m, idx, fi, raw = render(path, only)
    ri, rl = ref_colour(idx, m, raw)
    si, sm = scale(img, m)
    lines = quantize(si, sm)
    tl, M = build(si, sm, lines)
    pal = [0] * 64
    for li in range(4):
        for j, c in enumerate(lines[li][:15]): pal[li * 16 + 1 + j] = mdpal.md_word(c)
    sig = []
    if kind == 1:
        base, scx, scy, ent = fi['layers'][3]
        cand = sorted(((c, r), v) for (c, r), v in ent.items() if v[0])
        step = max(1, len(cand) // 5)
        sig = [(c, r, v[0]) for (c, r), v in cand[step::step][:4]]
        sig = (scx, scy, sig)
    blobs[st].append((pid, tl, M, pal))
    hdr.append((pid, st, kind, ri, rl, sig))
    print('picture %d (stage %d, %s): %d tiles, ref colour %d (level %d)' % (pid, st, fn, len(tl), ri, rl))
    from PIL import Image
    prev = np.zeros((224, 320, 3), np.uint8)
    for cy in range(28):
        for cx in range(40):
            w = int(M[cy, cx])
            if not w: continue
            t = tl[(w & 0x7FF) - TILE_BG0]
            if w & 0x800: t = t[:, ::-1]
            if w & 0x1000: t = t[::-1]
            li = w >> 13; P = np.vstack([[0, 0, 0], mdpal.md_rgb(lines[li])[:15], np.zeros((16, 3), int)])[:16]
            prev[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = P[t]
    os.makedirs(OUT + '/mdpic', exist_ok=True)
    Image.fromarray(prev).save(OUT + '/mdpic/prev%d.png' % pid)
for st, L in blobs.items():
    n = len(L); head = 4 + 16 * n; data = bytearray(); ents = b''
    for pid, tl, M, pal in L:
        t_ofs = head + len(data); data += b''.join(tilebytes(t) for t in tl)
        m_ofs = head + len(data); data += M.astype('>u2').tobytes()
        p_ofs = head + len(data); data += struct.pack('>64H', *pal)
        ents += struct.pack('>HHIII', pid, len(tl), t_ofs, m_ofs, p_ofs)
    open(OUT + '/mdpic/blob%d.bin' % st, 'wb').write(struct.pack('>HH', n, 0) + ents + bytes(data))
    print('stage %d blob part %d bytes' % (st, head + len(data)))
with open(OUT + '/mdpic.h', 'w') as f:
    f.write('/* generated by tools/mdpic.py: pictures shown by the MD (md/bg.c pic_show) */\n')
    f.write('/* id, stage, kind (0 ROUND map, 1 scroll3 match), reference colour (32X palette index), its level, scroll3 x, y, 4 x (col, row, code) */\n')
    f.write('static const struct { u8 id, stage, kind, pad; u16 ref, lvl; u16 sx, sy; u16 sig[4][3]; } mdpic_tab[] __attribute__((section(".rom2uid"))) = {\n')
    for pid, st, kind, ri, rl, sig in hdr:
        sx, sy, cells = sig if sig else (0, 0, [])
        cells = list(cells) + [(0, 0, 0)] * (4 - len(cells))
        f.write('    { %d, %d, %d, 0, %d, %d, %d, %d, { %s } },\n' % (pid, st, kind, ri, rl, sx, sy, ', '.join('{ %d, %d, 0x%04X }' % c for c in cells)))
    f.write('};\n')
