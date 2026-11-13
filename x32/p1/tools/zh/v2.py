RES_DENG = 0x6E60
# V1.9: the second translation sheet (data/zh/v2.xlsx: DynastyWars_日文文字_v2.xlsx with the 中文翻譯 column).
# Run inside mkzh.py (exec): uses prg, w16, patch, code_of, grp_codes, norm.
wb2 = openpyxl.load_workbook(D + 'zh_v2.xlsx')
def sheet2(name):
    ws = wb2[name]; hdr = [c.value for c in ws[1]]; ci = hdr.index('中文翻譯')
    return {r[0]: norm(str(r[ci]).strip()) for r in ws.iter_rows(min_row=2, values_only=True) if r[0] is not None and r[ci]}
def fix(t): return t.replace('..', '…').replace('．．', '…').replace('—', '－')
T27 = {k: fix(v) for k, v in sheet2('畫面字串(0x27A4表)').items()}
TPRO = {k: fix(v) for k, v in sheet2('開場劇情').items()}
TEND = {k: fix(v) for k, v in sheet2('結局與角色介紹').items()}
def full(t):                                   # half-width digits / letters -> full width (the font's)
    return ''.join(chr(ord(c) + 0xFEE0) if '0' <= c <= '9' or 'A' <= c <= 'Z' else c for c in t)

# ---------------- strings of the 0x2670 routine (table 0x27A4): x y attr page bytes.. (0x2F next, 0 end) ----------------
ST = 0x27A4
def str_segs(i):
    a = ST + w16(ST + 2 * i); a0 = a; segs = []
    while True:
        x, y, at, pg = prg[a:a + 4]; a += 4; bs = []
        while prg[a] not in (0, 0x2F): bs.append(prg[a]); a += 1
        segs.append((x, y, at, pg, bytes(bs))); e = prg[a]; a += 1
        if e == 0: break
    return a0, a, segs
def page_style(pg): return 'outline' if pg in (2, 3, 4) else 'p4' if 8 <= pg <= 11 else 'box'
def seg16(x, y, at, codes):                    # 16x16 mode: byte = bottom-left code, all in one page
    cs = [c + 0x10 for c in codes]; pg = (cs[0] - 0x6000) >> 8
    assert all((c - 0x6000) >> 8 == pg for c in cs) and pg >= 2
    return bytes([x, y, at, pg]) + bytes(c & 0xFF for c in cs)
def put_str(i, newsegs):                       # newsegs: bytes (built) or ('keep', k)
    a0, end, segs = str_segs(i)
    out = b''
    for n, sg in enumerate(newsegs):
        if isinstance(sg, tuple): x, y, at, pg, bs = segs[sg[1]]; sg = bytes([x, y, at, pg]) + bs
        out += sg + (b'\x2f' if n < len(newsegs) - 1 else b'\0')
    assert len(out) <= end - a0, ('string too long', i, len(out), end - a0)
    patch(a0, out)
LAST = []
def line(x, y, at, st, t):
    cs = grp_codes(t, st); LAST.append(cs); return seg16(x, y, at, cs)
def centred(cx, y, at, st, t): return line(cx - len(t), y, at, st, t)
def per_char(i, pos_text):                     # [(segment index for x/y/attr/style, x override or None, char)]
    a0, end, segs = str_segs(i); out = []
    for k, xo, ch in pos_text:
        x, y, at, pg, bs = segs[k]
        out.append(line(x if xo is None else xo, y, at, 'outline', ch))   # one look (the original pages mixed fonts)
    return out
n27 = 0
for i in (1, 44):
    if 1 in T27: put_str(i, [centred(31 if i == 1 else 33, 19, 5, 'box', full(T27[1]))]); n27 += 1
for i in (2, 46):
    if 2 in T27: put_str(i, [centred(32, 19, 5, 'box', full(T27[2]))]); n27 += 1
if 43 in T27:   # 投入 [the digit stays] 枚硬幣即可開始遊戲
    t = full(T27[43]).replace('　', ''); j = t.index('１')
    put_str(43, [line(36 - 2 * j, 19, 0x1F, 'box', t[:j]), ('keep', 7), line(38, 19, 0x1F, 'box', t[j + 1:])]); n27 += 1
if 45 in T27:
    l1, l2 = full(T27[45]).split('／'); l2 = l2.replace('　', ''); j = l2.index('１')
    a0, end, segs = str_segs(45); kd = [k for k, s in enumerate(segs) if (s[0], s[1]) == (34, 21)][0]
    put_str(45, [centred(32, 19, 0x1F, 'box', l1), line(34 - 2 * j, 21, 0x1F, 'box', l2[:j]), ('keep', kd), line(36, 21, 0x1F, 'box', l2[j + 1:])]); n27 += 1
if 26 in T27:   # 五虎將軍稱號表 / 5 ranks (3 characters each, the original positions)
    parts = T27[26].split('／'); title = parts[0]
    a0, end, segs = str_segs(26)
    pt = [(k, 17 + 2 + 4 * n if len(title) == 7 else None, ch) for n, (k, ch) in enumerate(zip(range(8), title))]
    pr = []
    for r, name in enumerate(parts[1:6]):
        for n, ch in enumerate(name): pr.append((8 + 3 * r + n, None, ch))
    put_str(26, per_char(26, pt + pr)); n27 += 1
for i in (27, 28):
    if i in T27:
        p = T27[i].split('／')
        lv = p[0]; seq = [(0, 31, lv[0]), (1, 33, lv[1])] if len(lv) == 2 else [(k, None, c) for k, c in enumerate(lv)]
        seq += [(3 + k, None, c) for k, c in enumerate(p[1])] + [(6 + k, None, c) for k, c in enumerate(p[2])] + [(8 + k, None, c) for k, c in enumerate(p[3])]
        LAST.clear(); segs_i = per_char(i, seq)
        if i == 27: RES_DENG = LAST[0][0]             # sh2/render.c: the results screen is recognised by it
        put_str(i, segs_i + [('keep', 11)]); n27 += 1
for i in (29, 30, 31, 32):                    # weapon names (character introductions): 8x8 kana + dakuten row -> 16x16
    if i in T27:
        names = T27[i].split('／'); a0, end, segs = str_segs(i)
        rows = sorted(set(s[1] for s in segs)); kana = [y for y in rows if y + 1 not in rows or y == rows[-1]]
        kana = [y for y in rows if not (y + 1 in rows)]
        assert len(kana) == len(names) == 4, (i, kana, names)
        put_str(i, [line(15, y, 7, 'plain', nm) for y, nm in zip(kana, names)]); n27 += 1
def span(i):
    a0, end, segs = str_segs(i); x0, y0 = segs[0][0], segs[0][1]; xe = segs[-1][0] + 2 * len(segs[-1][4]); return (x0 + xe) // 2, y0
for i in (33, 34, 35):
    if i in T27:                               # V3.4: the oath, Source Han Serif 20 px in 24x24 blocks (24 px apart)
        cx, y = span(i); t = T27[i]; cs = grp24([('ch', c, 20) for c in t])
        put_str(i, [bytes([cx - (3 * len(t)) // 2, y, 1, ((cs[0] + 0x10 - 0x6000) >> 8)]) + bytes((c + 0x10) & 0xFF for c in cs)]); n27 += 1
if 36 in T27:
    for i in (36, 7):
        cx, y = span(i); put_str(i, [centred(cx, y, 1, 'outline', T27[36])]); n27 += 1

# ---------------- opening story (0x20B6, table 0x2104): u32 address, words (code - 0x6000), negative = next column ----------------
PT = 0x2104; npro = 0
for k in range(3):
    if k + 1 not in TPRO: continue
    a0 = PT + w16(PT + 2 * k); a = a0; first = None; sep = None
    while True:
        ad = (w16(a) << 16) | w16(a + 2); a += 4; first = first or ad
        while True:
            v = w16(a); a += 2
            if v == 0 or v & 0x8000: break
        if v == 0: break
        sep = v
    cols = TPRO[k + 1].split('／')
    out = b''
    for n, col in enumerate(cols):
        codes = [blk(('ch', c, 22)) for c in col]                         # V3.4: 24x24 blocks, Source Han Serif 22 px, 32 px apart
        out += struct.pack('>I', first - 0x80 - 0x280 * n) + b''.join(struct.pack('>H', c - 0x6000) for c in codes)   # columns 40 px apart (were 32)
        out += struct.pack('>H', sep if n < len(cols) - 1 else 0)
    assert len(out) <= a - a0, ('story page too long', k, len(out), a - a0)
    patch(a0, out); npro += 1

# ---------------- ending / sequel notice / character introductions (records, table 0x45270) ----------------
def rec_parse(a0):
    a = a0; segs = []
    while True:
        x, y, at = prg[a], prg[a + 1], w16(a + 2); s0 = a; a += 4; ws = []
        while True:
            v = w16(a); a += 2
            if v in (0, 0xFFFF): break
            ws.append(v)
        segs.append([s0, x, y, at, ws])
        if v == 0: break
    return segs, a
END_RECS = [(0x45308, 1, 5, False), (0x453BC, 6, 7, False), (0x45EF4, 13, 7, True)]   # (record, first sheet row, lines, centre)
nend = 0
for a0, r0, nl, cen in END_RECS:
    if any(r0 + j not in TEND for j in range(nl)): continue
    segs, end = rec_parse(a0); assert len(segs) == nl, (hex(a0), len(segs))
    out = b''
    for si, (s0, x, y, at, ws) in enumerate(segs):
        t = full(TEND[r0 + si])
        st = 'outline' if sum(0x6200 <= (v + 0x5FF0) < 0x6600 for v in ws) * 2 > len(ws) else 'box'
        codes = [code_of(c, st) for c in t]
        if cen: x = x + (len(ws) - len(codes))
        out += bytes([x & 0xFF, y]) + struct.pack('>H', at) + b''.join(struct.pack('>H', c + 0x10 - 0x6000) for c in codes) + (b'\xff\xff' if si < nl - 1 else b'\0\0')
    assert len(out) <= end - a0, ('record too long', hex(a0))
    patch(a0, out); nend += 1
INTRO = [(0x458BC, 20), (0x45A4E, 23), (0x45BBC, 26), (0x45D76, 29)]   # 3 lines each: 8x8 cells, words in (top, bottom) pairs
nint = 0
for a0, r0 in INTRO:
    if any(r0 + j not in TEND for j in range(3)): continue
    segs, end = rec_parse(a0); assert len(segs) == 3
    out = b''
    for si, (s0, x, y, at, ws) in enumerate(segs):
        codes = [code_of(c, 'plain') for c in TEND[r0 + si]]   # (V3.4: back to the old font)
        w = []
        for c in codes: w += [c - 0x6000, c + 0x10 - 0x6000, c + 1 - 0x6000, c + 0x11 - 0x6000]
        assert len(w) <= 70, ('intro line too wide', hex(s0), TEND[r0 + si])   # 35 columns
        out += bytes([x, y]) + struct.pack('>H', at) + b''.join(struct.pack('>H', v) for v in w) + (b'\xff\xff' if si < 2 else b'\0\0')
    assert len(out) <= end - a0
    patch(a0, out); nint += 1
print('v2: strings %d, story pages %d, ending records %d, introductions %d' % (n27, npro, nend, nint))
