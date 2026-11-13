#!/usr/bin/env python3
"""(32xM V3.2) background palette animation: arcade scroll2/3 palettes the game cycles (stage 6's burning city glow,
stage 5's water) -> per version the MD colours of the CRAM slots those pens were quantized to.
input: stage k, host scan (/tmp/dw/sv/sK/scan.pkl: per palette the set of 32-byte contents seen),
       the stage snapshot (base palette), convbg4 intermediates (/tmp/dw/bgs/sK_*.npy, same run as data/bg/sK_*).
output: data/bg/sK_pal.bin = the 64 colours + 'PV' 0xA55A, u8 ngroups, u8 0, per group: u8 nslot, u8 nver,
        u8 CRAM index[nslot] (padded to even), u16 colour[nver][nslot]   (md/bg.c pv_apply)
        sh2/palanim.h: per stage the groups for the 32X: arcade palette, probe entry, probe value per version
usage: palanim.py k [k ...]   (rewrites sh2/palanim.h with every stage given)"""
import sys, os, pickle, struct, collections, numpy as np
sys.path.insert(0, '/dw/dw/tools')
import cps, mdpal
P = os.path.abspath(os.path.join(os.path.dirname(__file__), '../..'))
def cps_rgb(w):
    b = 0x0f + ((w >> 12) << 1); return [((w >> s) & 15) * 0x11 * b // 0x2d for s in (8, 4, 0)]
hdr = []
for k in map(int, sys.argv[1:]):
    S = pickle.load(open('/tmp/dw/sv/s%d/scan.pkl' % k, 'rb'))
    snap = cps.load_snap('/tmp/dw/s2/pal/st%d.bin' % k); rgb = snap['rgb'].astype(np.float32)
    groups = []
    for p, vs in sorted(S['pal'].items()):
        vs = sorted(struct.unpack('>16H', v) for v in vs)
        if len(vs) < 3 or len(vs) > 15: continue
        ch = [i for i in range(16) if len({v[i] for v in vs}) > 1]
        if len(ch) >= 15: continue                          # the whole palette: a fade / flash, not a cycle
        probe = next((i for i in ch if len({v[i] for v in vs}) == len(vs)), None)
        if probe is None: print('stage', k, 'palette %02X: no probe entry' % p); continue
        groups.append(dict(p=p, vs=vs, ch=set(ch), probe=probe))
    if not groups: print('stage', k, 'no cycling palettes'); continue
    gp = {g['p']: gi for gi, g in enumerate(groups)}
    # arcade source of each MD pixel (as convbg4.py builds the layers; no EXTRA cells in these stages)
    cnt = collections.defaultdict(collections.Counter)          # (line, pen) -> Counter((pal, pen))
    for L in (2, 3):
        c, a, blk = cps.scene_map(k, L, 32); cols = np.where(blk.any(axis=0))[0]; per = 16 if L == 2 else 8; ts = 16 if L == 2 else 32
        W = (cols.max() + 1) * per; c = c[:, :W]; a = a[:, :W]; H = c.shape[0]; n8 = ts // 8
        M = np.load('/tmp/dw/bgs/s%d_map%s.npy' % (k, 'A' if L == 2 else 'B')); T = np.load('/tmp/dw/bgs/s%d_tiles.npy' % k)
        palbase = 0x40 if L == 2 else 0x60
        for y in range(H):
            for x in range(W):
                if not (palbase + (int(a[y, x]) & 31)) in gp: continue
                t = cps.tile(L, int(c[y, x]))
                if t is None: continue
                at = int(a[y, x]); pal = palbase + (at & 31)
                if at & 0x20: t = t[:, ::-1]
                if at & 0x40: t = t[::-1]
                for j in range(n8):
                    for i in range(n8):
                        v = int(M[y * n8 + j, x * n8 + i])
                        if not v: continue
                        tid = v & 0xffff; li = (v >> 16) & 3; fl = (v >> 18) & 3
                        px = T[tid - 1]
                        if fl & 1: px = px[:, ::-1]
                        if fl & 2: px = px[::-1]
                        src = t[j * 8:j * 8 + 8, i * 8:i * 8 + 8]
                        for yy in range(8):
                            for xx in range(8):
                                if px[yy, xx] and src[yy, xx] != 15: cnt[(li, int(px[yy, xx]))][(pal, int(src[yy, xx]))] += 1
    # every source of those slots (also from other palettes): the slot is the weighted mean of its sources
    allc = collections.defaultdict(collections.Counter)
    slots = set(cnt)
    for L in (2, 3):
        c, a, blk = cps.scene_map(k, L, 32); cols = np.where(blk.any(axis=0))[0]; per = 16 if L == 2 else 8; ts = 16 if L == 2 else 32
        W = (cols.max() + 1) * per; c = c[:, :W]; a = a[:, :W]; H = c.shape[0]; n8 = ts // 8
        M = np.load('/tmp/dw/bgs/s%d_map%s.npy' % (k, 'A' if L == 2 else 'B')); T = np.load('/tmp/dw/bgs/s%d_tiles.npy' % k)
        lines_used = {s[0] for s in slots}
        palbase = 0x40 if L == 2 else 0x60
        for y in range(H):
            for x in range(W):
                vv = M[y * n8:(y + 1) * n8, x * n8:(x + 1) * n8]
                if not any(((int(v) >> 16) & 3) in lines_used for v in vv.ravel() if v): continue
                t = cps.tile(L, int(c[y, x]))
                if t is None: continue
                at = int(a[y, x]); pal = palbase + (at & 31)
                if at & 0x20: t = t[:, ::-1]
                if at & 0x40: t = t[::-1]
                for j in range(n8):
                    for i in range(n8):
                        v = int(vv[j, i])
                        if not v: continue
                        tid = v & 0xffff; li = (v >> 16) & 3; fl = (v >> 18) & 3
                        px = T[tid - 1]
                        if fl & 1: px = px[:, ::-1]
                        if fl & 2: px = px[::-1]
                        src = t[j * 8:j * 8 + 8, i * 8:i * 8 + 8]
                        m = (px != 0) & (src != 15)
                        for pp, ss in zip(px[m], src[m]):
                            if (li, int(pp)) in slots: allc[(li, int(pp))][(pal, int(ss))] += 1
    lines = np.load('/tmp/dw/bgs/s%d_lines.npy' % k)
    pw = list(struct.unpack('>64H', open(P + '/data/bg/s%d_pal.bin' % k, 'rb').read()[:128]))
    out = []
    for gi, g in enumerate(groups):
        res = []
        for s, cc in sorted(allc.items()):
            tot = sum(cc.values()); mine = sum(n for (pal, pen), n in cc.items() if pal == g['p'] and pen in g['ch'])
            if mine < tot * 0.3: continue
            # the group with the largest share owns the slot
            if any(sum(n for (pal, pen), n in cc.items() if pal == h['p'] and pen in h['ch']) > mine for h in groups if h is not g): continue
            base = sum(rgb[pal * 16 + pen] * n for (pal, pen), n in cc.items()) / tot
            cur = mdpal.md_rgb(lines[s[0]][s[1] - 1]).astype(np.float32)
            cv = []
            for v in g['vs']:
                avg = sum((np.array(cps_rgb(v[pen]), np.float32) if pal == g['p'] else rgb[pal * 16 + pen]) * n for (pal, pen), n in cc.items()) / tot
                cv.append(mdpal.md_word(mdpal.to_md3(np.clip(cur + avg - base, 0, 255))))
            if len(set(cv)) > 1: res.append((s[0] * 16 + s[1], cv))
        print('stage %d palette %02X: %d versions, probe entry %d, %d MD slots %s' % (k, g['p'], len(g['vs']), g['probe'], len(res), [r[0] for r in res]))
        out.append(res)
    keep = [i for i, r in enumerate(out) if r]; groups = [groups[i] for i in keep]; out = [out[i] for i in keep]
    if not groups:
        open(P + '/data/bg/s%d_pal.bin' % k, 'wb').write(struct.pack('>64H', *pw)); print('stage', k, 'nothing to animate'); continue
    blob = struct.pack('>HHBB', 0x5056, 0xA55A, len(groups), 0)
    for g, res in zip(groups, out):
        blob += struct.pack('>BB', len(res), len(g['vs'])) + bytes(r[0] for r in res) + bytes(len(res) & 1)
        for vi in range(len(g['vs'])): blob += b''.join(struct.pack('>H', r[1][vi]) for r in res)
    open(P + '/data/bg/s%d_pal.bin' % k, 'wb').write(struct.pack('>64H', *pw) + blob)
    hdr.append((k, groups))
with open(P + '/sh2/palanim.h', 'w') as f:
    f.write('/* generated by tools/bg4/palanim.py: background palette cycles the 32X detects (render.c pv_scan).\n'
            '   per stage: u8 ngroups; per group: u8 arcade palette, u8 probe entry, u8 nver, u16 probe value per version */\n')
    for k, groups in hdr:
        b = [len(groups)]
        for g in groups: b += [g['p'], g['probe'], len(g['vs'])] + [x for v in g['vs'] for x in (v[g['probe']] >> 8, v[g['probe']] & 255)]
        f.write('static const u8 pv_st%d[] = { %s };\n' % (k, ', '.join('0x%02X' % x for x in b)))
    f.write('static const u8 *const pv_tab[8] = { %s };\n' % ', '.join(('pv_st%d' % k) if k in [h[0] for h in hdr] else '0' for k in range(8)))
