import sys, pickle, numpy as np
sys.path.insert(0, '/dw/x32/p1/tools')
import cpsr
from PIL import Image
OUTS = pickle.load(open('/dw/x32/p1/data/pic.bin.pkl', 'rb'))
res = pickle.load(open('/tmp/dw/pic/all.pkl', 'rb'))
ST = {2: (0, 13, 27, 40), 3: (0, 27, 53, 80)}
def prev(fi):
    img = np.zeros((224, 320, 3), np.uint8)
    lc = fi['lc']
    for k in range(4):
        L = (lc >> (6 + 2 * k)) & 3
        if L < 2 or L not in fi['layers']: continue
        base, scx, scy, ent = fi['layers'][L]
        P = 40 if L == 2 else 80; pg = 0 if L == 2 else 512
        Kx = ((64 + scx) * 5 + 3) // 6; Ky = ((16 + scy) * 5 + 3) // 6
        for (c, r), (code, attr) in ent.items():
            key = (L, code & (0xffff if L == 2 else 0x3fff), (attr >> 5) & 3, c % 3, r % 3)
            if key not in OUTS: continue
            o = OUTS[key]
            ox = (c // 3) * P + ST[L][c % 3] - Kx; oy = 18 + (r // 3) * P + ST[L][r % 3] - Ky
            cols = fi['pal'][pg + (attr & 31) * 16: pg + (attr & 31) * 16 + 16]
            rgb = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], -1).astype(np.uint8)
            h, w = o.shape
            for y in range(h):
                for x in range(w):
                    X, Y = ox + x, oy + y
                    if 0 <= X < 320 and 0 <= Y < 224 and o[y, x] != 15: img[Y, X] = rgb[o[y, x]]
    return Image.fromarray(img)
if __name__ == '__main__':
    out = sys.argv[1]; sel = sys.argv[2:]
    ims = []
    for s in sel:
        run, fr = s.split(':'); fr = int(fr)
        fi = [f for f in res if f['run'] == run and f['fr'] == fr][0]
        ims.append(prev(fi))
    W = Image.new('RGB', (640, 224 * ((len(ims) + 1) // 2)))
    for i, im in enumerate(ims): W.paste(im, ((i % 2) * 320, (i // 2) * 224))
    W.save(out)
