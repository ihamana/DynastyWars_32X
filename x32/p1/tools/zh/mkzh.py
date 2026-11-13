#!/usr/bin/env python3
"""Chinese text (V1.7): from the translation sheet (data/zh/*.xlsx) and the KH-Dot-Dougenzaka-16 font.
New 16x16 glyphs get scroll1 codes 0x8000+ (glyph i: c = 0x8000 + i/8*0x20 + i%8*2; cells c, c+1, c+0x10, c+0x11,
both column halves the same), drawn in the game's two styles: 'box' (dialogue font: pen 0 background) and
'outline' (pen 0 outline, transparent around).  Outputs (data/zh/):
  zh.bin       cart 0x370000: u32 S1X table [16384] (code 0x8000 + i, half; V3.4: was 8192) -> cart address, then the 8x8 halves
  s1patch.bin  u32 pairs (k = (code - 0x6000) * 2 + half, cart address): existing codes redrawn (関 -> 關)
  prg.bin      patches to the arcade program: {u32 addr, u16 len, bytes, pad to 2}...
usage: mkzh.py"""
import sys, os, struct, numpy as np, openpyxl
sys.path.insert(0, os.path.dirname(__file__)); import glyphs as GL
P1 = os.path.abspath(os.path.dirname(__file__) + '/../..'); D = P1 + '/data/zh/'
prg = bytearray(open('/tmp/dw/rom/dynwarj_prg.bin', 'rb').read())
w16 = lambda a: (prg[a] << 8) | prg[a + 1]
GL.init(D + 'KH-Dot-Dougenzaka-16.ttf')
BASE = 0x370000                          # V1.9 (was 0x380000): 1024 glyphs
# ---------------- glyph styles ----------------
def style_box(b):                       # b: 16x16 0/1 -> pens (0 background, 3 stroke, 2 the right end of a run)
    p = np.zeros((16, 16), np.uint8)
    for y in range(16):
        for x in range(16):
            if b[y, x]:
                p[y, x] = 2 if (x > 0 and b[y, x - 1] and (x == 15 or not b[y, x + 1])) else 3
    return p
def style_outline(b):
    p = np.full((16, 16), 15, np.uint8)
    for y in range(16):
        for x in range(16):
            if b[y, x]: continue
            if any(0 <= y + dy < 16 and 0 <= x + dx < 16 and b[y + dy, x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)): p[y, x] = 0
    p[b == 1] = 3
    return p
glyph_codes = {}                        # (char, style) -> code
order = []
def code_of(ch, st):
    k = (ch, st)
    if k not in glyph_codes:
        i = len(order); assert i < 1024, 'too many glyphs'
        glyph_codes[k] = 0x8000 + (i // 8) * 0x20 + (i % 8) * 2; order.append(k)
    return glyph_codes[k]
code_of('武', 'plain'); code_of('智', 'plain'); code_of('LV', 'bmp')
for _c in '平均型安定上昇': code_of(_c, 'box')
for _c in '等策力落石伏兵火計爆裂量略級': code_of(_c, 'hud8')   # 0x8024.. HUD labels / strategy names (sh2/render.c HUD8)   # 0x8006.. select screen type labels (sh2/render.c SEL_TYPE)   # 0x8000 / 0x8002 / 0x8004: select screen stat labels (sh2/render.c SEL_WU)
def grp_codes(text, st):                # all in one 64-glyph block (one "page" for the 0x2670 strings)
    chars = list(dict.fromkeys(text)); gid = len(order)
    if len(order) % 64 + len(chars) > 64:
        while len(order) % 64: order.append(('pad', len(order))); 
    m = {}
    for ch in chars:
        k = (ch, st, gid); i = len(order); assert i < 1024, 'too many glyphs'
        glyph_codes[k] = 0x8000 + (i // 8) * 0x20 + (i % 8) * 2; order.append(k); m[ch] = glyph_codes[k]
    return [m[ch] for ch in text]
BLANK = {'box': 0x66C6, 'outline': 0x646E}
NORM = {'/': '／', '!': '！', '?': '？', ',': '，', '~': '～', '〜': '～', ' ': '　', '.': '．'}
def norm(t): return ''.join(NORM.get(c, c) for c in t)
# ---------------- sheet ----------------
wb = openpyxl.load_workbook(D + 'zh_v1.xlsx')
rows = [r for r in wb.worksheets[0].iter_rows(min_row=2, values_only=True) if r[1]]
tr = {int(r[1], 16): (r[2] or '', norm((r[4] or '').strip())) for r in rows}
patches = []
def patch(a, b): patches.append((a, bytes(b)))
# ---------------- HUD dialogues: u16 attr, 2 lines x 16 top-left codes ----------------
K = 'KEEP'
MANUAL = {   # program-filled slots (names, weapon, numbers) keep the original cells
    0x4736E: [K, K] + list('前來接應！'),
    0x474FA: [K, K, '大', '人', K, K] + list('前來助陣！'),
    0x4753C: [K, K] + list('大人，勢危之時請召吾'),
    0x475C0: list('喔喔喔喔！') + [None] * 11 + [None] * 3 + list('此乃傳聞中之') + [K] * 5 + list('乎！'),
    0x483FE: [K, K] + list('的武力提升了！'),
    0x48440: list('武器換成') + [K] * 5 + ['。'] + [None] * 6 + [K, K] + list('拿起了新武器！'),
    0x484C4: list('距下一級所需經驗值') + [None] * 7 + [K] * 11 + ['。'],
}
def layout(t):                              # V2.2: at most 15 glyphs a line (the HUD talk shows arcade x 64-306 beside the portraits)
    if len(t) <= 15: return list(t)
    cut = None
    for i in range(min(15, len(t)) - 1, 0, -1):
        if t[i] in '，！。？…、' and len(t) - i - 1 <= 15: cut = i + 1; break
    if cut is None: cut = 15
    a, b = t[:cut], t[cut:]
    assert len(b) <= 15, t
    return list(a) + [None] * (16 - len(a)) + list(b)
nd = 0
for a, (kind, t) in sorted(tr.items()):
    if not kind.startswith('遊戲中對話'): continue
    assert w16(a) == 1, hex(a)
    orig = [w16(a + 2 + 2 * k) for k in range(32)]
    cells = MANUAL.get(a) or layout(t)
    cells = cells + [None] * (32 - len(cells)); assert len(cells) == 32, (hex(a), t)
    out = []
    for k, ch in enumerate(cells):
        if ch is K: out.append(orig[k])
        elif ch is None or ch == '　': out.append(BLANK['box'])
        else: out.append(code_of(ch, 'box'))
    patch(a + 2, b''.join(struct.pack('>H', v) for v in out)); nd += 1
# ---------------- V3.4: 24x24 blocks, codes 0x9000-0x9FFF (16 pages x 25: T = page + band * 0x30 + i * 3; cells T + dx + 0x10 dy) ----------------
# drawn by the patched arcade routines (tools/patch_gen.py): opening story (block per character, Source Han Serif 22 px),
# screen messages (a line as one strip of 18 px characters, cut into 24 px blocks)
BLK = []                                  # ('ch', ch, size) / ('strip', text, part)
_blk = {}
def T24(n): p, i = divmod(n, 25); return 0x9000 + p * 0x100 + (i // 5) * 0x30 + (i % 5) * 3
def blk(job):
    if job not in _blk: assert len(BLK) < 400, 'too many 24x24 blocks'; _blk[job] = T24(len(BLK)); BLK.append(job)
    return _blk[job]
def grp24(jobs):                         # all in one page (the 0x2670 strings: page byte + code bytes), deduplicated within it
    u = list(dict.fromkeys(jobs))
    if len(BLK) % 25 + len(u) > 25:
        while len(BLK) % 25: BLK.append(('pad',))
    m = {j: T24(len(BLK) + i) for i, j in enumerate(u)}; BLK.extend(u); assert len(BLK) <= 400
    return [m[j] for j in jobs]
CAP_PX, CAP_MAXW = 18, 300
def cap_pitch(n): return min(22, CAP_MAXW // max(n, 1))
def strip24(t):                           # -> block codes of the line
    nb = -(-(cap_pitch(len(t)) * len(t) + 2) // 24)
    return [blk(('strip', t, k)) for k in range(nb)]
# ---------------- screen messages (records): pointer table at 0x45270 ----------------
TAB = 0x45270
offs = []
i = 0
while True:
    o = w16(TAB + 2 * i)
    if o == 0 or o > 0x2000: break
    offs.append(o); i += 1
# multi-line messages: the sheet has only some of their lines (the rest used other glyphs): whole messages here
REC_LINES = {
    0x452B0: ['逆賊董卓之野心，因劉備軍奮戰，', '已與郿塢城一併攻破。'],
    0x45738: ['終究非我等之敵，', '休想動我一根手指。'],
    0x45580: ['想取本大爺首級？呵呵！好狂妄，', '欲尋本大爺，就來鐵門峽！'],
    0x45482: ['這位誇口已將中原納入掌中的男子，', '究竟是何許人也？', '亂世難道仍將持續…？'],
    0x454F6: ['此戰不過是三國時代的序章，', '而劉備等人對此', '仍一無所知。'],
}
nr = 0
for o in sorted(set(offs)):
    a0 = TAB + o; a = a0; segs = []
    while True:
        x, y, at = prg[a], prg[a + 1], w16(a + 2); s0 = a; a += 4; ws = []
        while True:
            v = w16(a); a += 2
            if v in (0, 0xFFFF): break
            ws.append(v)
        segs.append([s0, x, y, at, ws])
        if v == 0: break
    end = a
    if not any(s[0] in tr for s in segs): continue
    lines = REC_LINES.get(a0)
    if lines: assert len(lines) == len(segs), hex(a0)
    out = b''
    isout = [sum(0x6200 <= (v + 0x5FF0) < 0x6600 for v in sg[4]) * 2 > len(sg[4]) for sg in segs]
    cx0 = segs[0][1] + max(len(sg[4]) for sg in segs)          # the original block's centre (cells)
    for si, (s0, x, y, at, ws) in enumerate(segs):
        if (s0 in tr or lines) and all(isout):             # V3.4: kill captions / cutscene lines: 18 px strips of 24x24 blocks
            t = lines[si] if lines else tr[s0][1]
            bl = strip24(t)
            x = 32 - (3 * len(bl)) // 2                     # V3.11: centred on the 32X screen (column 32 = x 192 of 384)
            if len(segs) > 1: y = segs[-1][2] - 3 * (len(segs) - 1 - si)   # lines 3 cells apart (24 px)
            ws = [b + 0x10 - 0x6000 for b in bl]
            if len(segs) == 1 and 0x45788 <= a0 <= 0x458AA and y == 27: y = 26   # V3.13: continue screen: one row up (row 28 holds the 1P/2P START boxes, which overwrote the blocks' bottom row)
            if os.environ.get("ZHDBG"): print("REC", hex(a0), len(segs), si, x, y, t)
        elif s0 in tr or lines:
            t = lines[si] if lines else tr[s0][1]
            st = 'outline' if isout[si] else 'box'
            codes = [BLANK[st] if c == '　' else code_of(c, st) for c in t]
            if len(segs) == 1: x = x + (len(ws) - len(codes))          # one line: keep it centred
            ws = [c + 0x10 - 0x6000 for c in codes]
        out += bytes([x & 0xFF, y]) + struct.pack('>H', at) + b''.join(struct.pack('>H', v) for v in ws) + (b'\xff\xff' if si < len(segs) - 1 else b'\0\0')
    assert len(out) <= end - a0, ('record too long', hex(a0), len(out), end - a0)
    patch(a0, out); nr += 1
# ---------------- weapon names in the dialogue (5 bottom-left codes, a leading 0 = half a glyph right) ----------------
WEAP = {0x20: '剛者蛇矛', 0x2a: '火炎蛇矛', 0x34: '龍王蛇矛', 0x3e: '嵐之劍', 0x48: '七星劍', 0x52: '怒王劍',
        0x5c: '青龍薙刀', 0x66: '怒心月刃', 0x70: '大偃月刃', 0x7a: '鬼神槍', 0x84: '方天畫戟', 0x8e: '三頭龍槍'}
WT = 0x476F4; bl = BLANK['box'] + 0x10
for o, t in WEAP.items():
    g = [code_of(c, 'box') + 0x10 for c in t]
    m = len(g)
    if m == 5: ws = g
    elif m == 4: ws = [0] + g
    elif m == 3: ws = [bl] + g + [bl]
    else: ws = [0, bl] + g + [bl] * (3 - m)
    patch(WT + o, b''.join(struct.pack('>H', v) for v in ws))
exec(open(os.path.join(os.path.dirname(__file__), 'v2.py'), encoding='utf-8').read())
# ---------------- glyph data ----------------
halves = {}; data = bytearray(); s1x = [0] * 16384      # V3.4: codes 0x8000-0x9FFF
def half(p8):                            # 8x8 pens -> cart address (deduplicated)
    b = bytes(((p8[:, 0::2] << 4) | p8[:, 1::2]).astype(np.uint8).ravel())
    if b not in halves: halves[b] = 0x02000000 + BASE + 0x10000 + len(data); data.extend(b)   # SH-2 address
    return halves[b]
PLACEHOLDER = GL.glyph('賴')            # the font's glyph for characters it does not really have
LV_BMP = ['X.....X.....X...', 'X.....X.....X...', 'X......X...X....', 'X......X...X....',
          'X.......X.X.....', 'X.......X.X.....', 'XXXXX....X......', '................']
import bdf as _bdf
_F8 = None
HUD_GRAD = [0xC, 0xB, 0xA, 0xA, 0x9, 0x8, 0x6, 0x6]     # the HUD letters' rows (0x614C "L"), shadow pen 4
def hud8(ch):                             # fusion-pixel 8 px, top-left 8x8 cell only
    global _F8
    if _F8 is None: _F8 = _bdf.load(os.path.join(P1, 'data/font/fusion-pixel-8px-monospaced-zh_tw.bdf'))
    G, asc = _F8; rows, w, h, xo, yo = G[ord(ch)]
    p = np.full((16, 16), 15, np.uint8); m = np.zeros((9, 9), bool)
    top = asc - (h + yo) if asc - (h + yo) >= 0 else 0
    for y, r in enumerate(rows):
        for x, b in enumerate(r):
            if b and y + top < 8 and x + xo < 8: m[y + top, x + xo] = True
    for y in range(8):
        for x in range(8):
            if m[y, x]: p[y, x] = HUD_GRAD[y]
            elif y and x and m[y - 1, x - 1]: p[y, x] = 4
            else: p[y, x] = 0             # the panel colour (like the HUD space 0x6120)
    return p
_F12 = None
def f12(ch, pad=2):
    global _F12
    if _F12 is None: _F12 = _bdf.load(os.path.join(P1, 'data/font/fusion-pixel-12px-monospaced-zh_tw.bdf'))
    G, asc = _F12; assert ord(ch) in G, 'no glyph for ' + ch
    rows, w, h, xo, yo = G[ord(ch)]
    b = np.zeros((16, 16), np.uint8); top = pad + max(0, asc - (h + yo)); left = pad + xo
    for y, r in enumerate(rows):
        for x, v in enumerate(r):
            if v and 0 <= y + top < 16 and 0 <= x + left < 16: b[y + top, x + left] = 1
    return b
from PIL import ImageFont, Image as _Img, ImageDraw as _Draw
_SHS = {}
def shs(ch, size=15, box=16, wt=600, thr=110):      # Source Han Serif TC (variable: weight), 1-bit, centred in box x box
    k = (size, wt)
    if k not in _SHS:
        f = ImageFont.truetype(os.path.join(P1, 'data/font/SourceHanSerifTC-VF.ttf'), size)
        try: f.set_variation_by_axes([wt])
        except Exception: pass
        _SHS[k] = f
    f = _SHS[k]; im = _Img.new('L', (box * 2, box * 2), 0); _Draw.Draw(im).text((box // 2, box // 2), ch, font=f, fill=255)
    a = np.array(im); ys, xs = np.nonzero(a > thr)
    b = np.zeros((box, box), np.uint8)
    if len(ys):
        h = ys.max() - ys.min() + 1; w = xs.max() - xs.min() + 1
        oy = ys.min() - (box - h) // 2; ox = xs.min() - (box - w) // 2
        for y, x in zip(ys, xs):
            if 0 <= y - oy < box and 0 <= x - ox < box: b[y - oy, x - ox] = 1
    return b
_SH = {}
def shs_cov(ch, size, wt, box):              # V3.2: Source Han Serif TC (weight wt), hinted at size px, grey coverage 0..1, ink centred in box
    k = (size, wt)
    if k not in _SH:
        f = ImageFont.truetype(os.path.join(P1, 'data/font/SourceHanSerifTC-VF.ttf'), size)
        f.set_variation_by_axes([wt]); _SH[k] = f
    W = box * 3; im = _Img.new('L', (W, W), 0); _Draw.Draw(im).text((box, box), ch, font=_SH[k], fill=255)
    a = np.array(im, np.float32) / 255; ys, xs = np.nonzero(a > 0.05)
    c = np.zeros((box, box), np.float32)
    if not len(ys): return c
    oy = (ys.min() + ys.max() + 1) // 2 - box // 2; ox = (xs.min() + xs.max() + 1) // 2 - box // 2
    for y in range(box):
        if 0 <= oy + y < W: c[y] = a[oy + y, max(ox, 0):ox + box] if ox >= 0 else 0
    return c
def shade(c, pen3, H, W):                   # coverage -> 3 shades (pen3: high, mid, low), opaque outline pen 0 around, 15 transparent
    p = np.full((H, W), 15, np.uint8); hi, mid, lo = pen3
    p[c >= 0.15] = lo; p[c >= 0.35] = mid; p[c >= 0.62] = hi
    ink = c >= 0.15
    for y in range(H):
        for x in range(W):
            if not ink[y, x] and any(0 <= y + dy < H and 0 <= x + dx < W and ink[y + dy, x + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)): p[y, x] = 0
    return p
def shs16(ch):                               # V3.2: the arcade's 16x16 font look: white (3) centre, grey (2, 1) edges, black (0) outline
    return shade(shs_cov(ch, 16, 600, 16), (3, 2, 1), 16, 16)
def pens(ch, st):
    if st == 'hud8': return hud8(ch)
    if st == 'bmp':                       # hand drawn, the 8-bit font's style (pen 1, transparent), top row only
        p = np.full((16, 16), 15, np.uint8)
        for y, r in enumerate(LV_BMP):
            for x, c in enumerate(r):
                if c == 'X': p[y, x] = 1
        return p
    if st in ('story', 'outline', 'intro'): return shs16(ch)   # V3.2: Source Han Serif SemiBold 16 px, shaded (story, messages, rankings, introductions)
    if st == 'outline1': return style_outline(GL.glyph(ch))   # the red top texts (continue / game over): the old outline font
    b = GL.glyph(ch)
    if os.environ.get('ZHFONT') == 'shs' and st == 'plain': b = shs(ch)   # test: Source Han Serif look
    if not (ch in '。．・' or not (b == PLACEHOLDER).all()): b = shs(ch)   # not in KH-Dot: Source Han Serif (V1.10)
    if st == 'plain': p = np.full((16, 16), 15, np.uint8); p[b == 1] = 1; return p   # the 8-bit labels' style
    if st == 'p4': p = np.full((16, 16), 15, np.uint8); p[b == 1] = 4; return p      # thin font (pages 8-11)
    return style_box(b) if st == 'box' else style_outline(b)
for key, c in sorted(glyph_codes.items(), key=lambda kv: kv[1]):
    ch, st = key[0], key[1]
    p = pens(ch, st)
    for dy in (0, 1):
        for dx in (0, 1):
            ad = half(p[dy * 8:dy * 8 + 8, dx * 8:dx * 8 + 8])
            k = (c + dy * 0x10 + dx - 0x8000) * 2
            s1x[k] = s1x[k + 1] = ad
def cap_strip(t):                        # V3.4: a whole line (coverage) at the caption pitch, centred in its blocks
    P = cap_pitch(len(t)); nb = -(-(P * len(t) + 2) // 24); W = nb * 24
    c = np.zeros((24, W), np.float32); x0 = (W - P * len(t)) / 2 + (P - 24) / 2
    for i, ch in enumerate(t):
        g = shs_cov(ch, CAP_PX, 600, 24); xs = int(round(x0 + i * P))
        for xx in range(24):
            if 0 <= xs + xx < W: c[:, xs + xx] = np.maximum(c[:, xs + xx], g[:, xx])
    return shade(c, (3, 2, 1), 24, W)
_strips = {}
for n, job in enumerate(BLK):
    if job[0] == 'pad': continue
    if job[0] == 'ch': p = shade(shs_cov(job[1], job[2], 600, 24), (3, 2, 1), 24, 24)
    else:
        if job[1] not in _strips: _strips[job[1]] = cap_strip(job[1])
        p = _strips[job[1]][:, job[2] * 24:job[2] * 24 + 24]
    T = T24(n)
    for dy in range(3):
        for dx in range(3):
            ad = half(p[dy * 8:dy * 8 + 8, dx * 8:dx * 8 + 8]); k = (T + dx + 0x10 * dy - 0x8000) * 2
            s1x[k] = s1x[k + 1] = ad
s1patch = []
for ch, c in (('關', 0x66AC),):          # names drawn by the program (page 6): 関 -> 關
    p = pens(ch, 'box')
    for dy in (0, 1):
        for dx in (0, 1):
            ad = half(p[dy * 8:dy * 8 + 8, dx * 8:dx * 8 + 8]); k = (c + dy * 0x10 + dx - 0x6000) * 2
            s1patch += [(k, ad), (k + 1, ad)]
def s1put(code, p8):                    # redraw one existing 8x8 cell (both column halves)
    global s1patch
    ad = half(p8); k = (code - 0x6000) * 2; s1patch += [(k, ad), (k + 1, ad)]
def redraw16(tl, p):                     # 16x16 at top-left code tl (tl, tl+1, tl+0x10, tl+0x11)
    for dy in (0, 1):
        for dx in (0, 1): s1put(tl + dy * 0x10 + dx, p[dy * 8:dy * 8 + 8, dx * 8:dx * 8 + 8])
BLANK16 = np.full((16, 16), 15, np.uint8)
# continue screen / game over (top, red, program-written blocks of 16x16 cells): V1.10
for slots, t in (([0x6F28, 0x6F2A, 0x6F2C, 0x6F2E, 0x6F40, 0x6F42, 0x6F44], '給孤一試'),
                 ([0x6F48, 0x6F4A, 0x6F4C, 0x6F4E, 0x6F60, 0x6F62, 0x6F64, 0x6F66, 0x6F68], '出師未捷身先死')):
    for n, tl in enumerate(slots): redraw16(tl, pens(t[n], 'outline1') if n < len(t) else BLANK16)
# opening page 4: 32x24 red characters (4 x 3 cells from top-left code t: t + dx + 0x10 dy), 5 columns right to left
# opening page 4: 32x24 red characters (4 x 3 cells from top-left code t: t + dx + 0x10 dy).  V3.2: two columns of 4 characters
# + punctuation (right: 風雲既變， left: 亂世將臨！), Source Han Serif Bold 24 px shaded like the original (pens 5 4 3, outline 0).
# The program (0x21E4) draws the table at 0x2250 in 3 steps (lists ending 0 at 0x2250, 0x225E, 0x2274: u16 scroll1 offset, u16 code)
RED_SLOTS = [0x6708, 0x670C, 0x6730, 0x6704, 0x6734, 0x6738, 0x6700, 0x67A0, 0x673C, 0x67D0, 0x67D4, 0x67D8, 0x67DC]
RED_COLS = [(35, '風雲既變，'), (25, '亂世將臨！')]       # scroll1 column (4 cells wide), first row 6, 4 rows apart
def red24(ch):
    c = np.zeros((24, 32), np.float32); c[:, 4:28] = shs_cov(ch, 24, 800, 24)
    return shade(c, (5, 4, 3), 24, 32)
ents = []; si = 0
for col, txt in RED_COLS:
    for n, ch in enumerate(txt):
        t = RED_SLOTS[si]; si += 1; p = red24(ch)
        for dy in range(3):
            for dx in range(4): s1put(t + dx + 0x10 * dy, p[dy * 8:dy * 8 + 8, dx * 8:dx * 8 + 8])
        ents.append(struct.pack('>HH', col * 0x80 + (6 + 4 * n) * 4, t))
for t in RED_SLOTS[si:]:
    for dy in range(3):
        for dx in range(4): s1put(t + dx + 0x10 * dy, np.full((8, 8), 15, np.uint8))
Z = b'\0\0'
patch(0x2250, ents[0] + ents[1] + ents[2] + Z)                    # step 1: 風雲既
patch(0x225E, ents[3] + ents[4] + Z)                              # step 2: 變，
patch(0x2274, b''.join(ents[5:10]) + Z)                           # step 3: 亂世將臨！ (up to 0x2289)
# codes for the renderer (sh2/render.c)
def tl(ch, st): return glyph_codes[(ch, st)]
hdr = ['/* generated by tools/zh/mkzh.py */']
for name, (ch, st) in {'ZH_WU': ('武', 'plain'), 'ZH_ZHI': ('智', 'plain'),
        'ZH_PING': ('平', 'box'), 'ZH_JUN': ('均', 'box'), 'ZH_XING': ('型', 'box'), 'ZH_AN': ('安', 'box'), 'ZH_DING': ('定', 'box'),
        'ZH_SHANG': ('上', 'box'), 'ZH_SHENG': ('昇', 'box'),
        'ZH_H_DENG': ('等', 'hud8'), 'ZH_H_JI2': ('級', 'hud8'), 'ZH_H_CE': ('策', 'hud8'), 'ZH_H_LUE': ('略', 'hud8'),
        'ZH_H_LI': ('力', 'hud8'), 'ZH_H_LIANG': ('量', 'hud8'), 'ZH_H_LUO': ('落', 'hud8'), 'ZH_H_SHI': ('石', 'hud8'),
        'ZH_H_FU': ('伏', 'hud8'), 'ZH_H_BING': ('兵', 'hud8'), 'ZH_H_HUO': ('火', 'hud8'), 'ZH_H_JI': ('計', 'hud8'),
        'ZH_H_BAO': ('爆', 'hud8'), 'ZH_H_LIE': ('裂', 'hud8')}.items():
    hdr.append('#define %s 0x%04X' % (name, tl(ch, st)))
hdr.append('#define ZH_RES_DENG 0x%04X    /* stage clear results: 等 of 等級 (top-left code) */' % RES_DENG)
open(D + 'zhcodes.h', 'w').write('\n'.join(hdr) + '\n')
blob = struct.pack('>16384I', *s1x) + bytes(data)
open(D + 'zh.bin', 'wb').write(blob)
open(D + 's1patch.bin', 'wb').write(b''.join(struct.pack('>II', k, a) for k, a in s1patch))
pb = b''
for a, b in patches:
    pb += struct.pack('>IH', a, len(b)) + b + (b'\0' if len(b) & 1 else b'')
open(D + 'prg.bin', 'wb').write(pb)
print('dialogues %d, records %d, weapons %d; glyphs %d (box %d, outline %d), zh.bin %d bytes' % (
    nd, nr, len(WEAP), len(order), sum(1 for k in order if len(k) > 1 and k[1] == 'box'), sum(1 for k in order if len(k) > 1 and k[1] == 'outline'), len(blob)))
assert len(blob) <= 0x3A1A00 - BASE
