"""minimal BDF reader: glyph(ch) -> (bitmap rows as lists of 0/1 in the font box, w, h, xoff, yoff), font ascent"""
def load(fn):
    G = {}; asc = 0; cur = None
    with open(fn, encoding='utf-8', errors='replace') as f:
        lines = iter(f.read().split('\n'))
    for l in lines:
        if l.startswith('FONT_ASCENT'): asc = int(l.split()[1])
        elif l.startswith('ENCODING'): enc = int(l.split()[1])
        elif l.startswith('BBX'): w, h, xo, yo = map(int, l.split()[1:5])
        elif l == 'BITMAP':
            rows = []
            for r in lines:
                if r == 'ENDCHAR': break
                v = int(r, 16); n = len(r) * 4
                rows.append([(v >> (n - 1 - i)) & 1 for i in range(w)])
            G[enc] = (rows, w, h, xo, yo)
    return G, asc
