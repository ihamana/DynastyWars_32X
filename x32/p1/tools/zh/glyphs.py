"""16x16 Chinese glyph bitmaps from KH-Dot-Dougenzaka-16; characters the font lacks are built from parts
of others (or a Japanese variant form)."""
import numpy as np
from PIL import Image, ImageDraw, ImageFont
FONT = None
def kh(ch):
    im = Image.new('L', (16, 16), 0); d = ImageDraw.Draw(im); d.fontmode = '1'
    d.text((0, 0), ch, font=FONT, fill=255)
    return (np.array(im) > 0).astype(np.uint8)
def squeeze(a, w):          # drop columns (the least informative first) down to width w
    a = a.copy()
    cols = list(range(a.shape[1]))
    while len(cols) > w:
        best = None
        for i in range(1, len(cols)):
            d = int((a[:, cols[i]] != a[:, cols[i - 1]]).sum())   # a column equal to its left neighbour costs nothing
            if best is None or d < best[0]: best = (d, i)
        cols.pop(best[1])
    return a[:, cols]
def trim(a):
    x = np.where(a.any(0))[0]
    return a[:, x.min():x.max() + 1] if len(x) else a
def lr(left, lw, right, rw=None, gap=0):  # left part: columns [0, lw) of char left; right part: char right squeezed (or its columns [16-rw, 16))
    L = kh(left)[:, :lw]
    R = kh(right)
    if rw is None: R = R[:, 16 - (16 - lw - gap):]
    elif rw < 0: R = R[:, -rw:]
    else: R = squeeze(trim(R), rw)
    out = np.zeros((16, 16), np.uint8); out[:, :lw] = L
    w = min(16 - lw - gap, R.shape[1]); out[:, lw + gap:lw + gap + w] = R[:, :w]
    return out
def rl(left, lw, right, rcol):            # left: char left squeezed to lw; right part: columns [rcol, 16) of char right
    L = squeeze(trim(kh(left)), lw); R = kh(right)[:, rcol:]
    out = np.zeros((16, 16), np.uint8); out[:, :L.shape[1]] = L; out[:, 16 - R.shape[1]:] = R
    return out
VARIANT = {'值': '値', '擊': '撃', '步': '歩', '眾': '衆', '絕': '絶', '黃': '黄'}
def build(ch):
    from fontTools.ttLib import TTFont
    if ch in VARIANT: return kh(VARIANT[ch])
    if ch == '你': return lr('作', 5, '弥', None)
    if ch == '傕': return lr('作', 5, '確', None)
    if ch == '喔': return lr('呼', 5, '握', None)
    if ch == '哼': return lr('呼', 4, '亨', 12)
    if ch == '啊': return lr('呼', 4, '阿', 12)
    if ch == '撐': return lr('握', 5, '掌', 11)
    if ch == '汜': return lr('池', 5, '記', None)
    if ch == '郿': return rl('眉', 10, '郭', 10)
    if ch == '鄧': return rl('登', 10, '郭', 10)
    if ch == '丫':
        a = np.zeros((16, 16), np.uint8)
        for i in range(7): a[1 + i, 1 + i] = a[1 + i, 2 + i] = 1; a[1 + i, 14 - i] = a[1 + i, 13 - i] = 1
        a[8:15, 7:9] = 1
        return a
    return kh(ch)
def init(fn):
    global FONT, CMAP
    FONT = ImageFont.truetype(fn, 16)
    from fontTools.ttLib import TTFont
    CMAP = TTFont(fn).getBestCmap()
def glyph(ch):
    if ch in ' 　': return np.zeros((16, 16), np.uint8)
    if ord(ch) not in CMAP or ch in VARIANT or ch in '丫': return build(ch)
    return kh(ch)
