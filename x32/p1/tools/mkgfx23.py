#!/usr/bin/env python3
"""CPS1 scroll2 (16x16) and scroll3 (32x32) tiles used outside the stage scenes -> 32X packed 4bpp
(left pixel in the high nibble, pen 15 transparent), duplicates removed.
  s2_tab[8192] (index = code & 0x1fff for codes 0x2000-0x3fff), s3_tab[2048] (code 0-0x7ff): SH-2
  address or 0.  usage: mkgfx23.py gfx.bin out tuse1.bin[,tuse2...] regions(ofs:size,...)"""
import sys, struct, numpy as np
gfx = np.frombuffer(open(sys.argv[1], 'rb').read(), np.uint8)
out = sys.argv[2]
u = np.zeros((4, 0x10000), np.uint8)
for f in sys.argv[3].split(','): u |= np.fromfile(f, np.uint8).reshape(4, 0x10000)
regions = [tuple(int(x, 0) for x in r.split(':')) for r in sys.argv[4].split(',')]
def pack(region):
    a = region.reshape(-1, 4)
    bits = np.unpackbits(a, axis=1).reshape(-1, 4, 8)
    pen = (bits[:, 3, :] << 3) | (bits[:, 2, :] << 2) | (bits[:, 1, :] << 1) | bits[:, 0, :]
    return ((pen[:, 0::2] << 4) | pen[:, 1::2]).astype(np.uint8).reshape(-1).tobytes()
CART = 0x02000000
blobs = []   # (bytes) unique
uniq = {}
def add(b):
    if b not in uniq: uniq[b] = len(blobs); blobs.append(b)
    return uniq[b]
s2 = {}; s3 = {}
for c in np.nonzero(u[2])[0]:
    if 0x6000 <= c < 0x8000:
        b = pack(gfx[c * 128:(c + 1) * 128])
        if b != b'\xff' * 128: s2[c - 0x6000] = add(b)
for c in np.nonzero(u[3])[0]:
    if 0x1000 <= c < 0x1800:
        b = pack(gfx[c * 512:(c + 1) * 512])
        if b != b'\xff' * 512: s3[c - 0x1000] = add(b)
# place: big (512) first
order = sorted(range(len(blobs)), key=lambda i: -len(blobs[i]))
addr = {}; used = [0] * len(regions); pieces = []
for i in order:
    b = blobs[i]
    for ri in range(len(regions)):
        if used[ri] + len(b) <= regions[ri][1]: break
    else: sys.exit('gfx23 does not fit')
    o = regions[ri][0] + used[ri]; addr[i] = CART + o; used[ri] += len(b); pieces.append((o, b))
pieces.sort(); chunks = []; cur = None
for o, b in pieces:
    if cur and cur[0] + len(cur[1]) == o: cur[1].extend(b)
    else:
        if cur: chunks.append((cur[0], bytes(cur[1])))
        cur = [o, bytearray(b)]
chunks.append((cur[0], bytes(cur[1])))
print('free per region:', [hex(r[1] - u) for r, u in zip(regions, used)])
t2 = [0] * 8192; t3 = [0] * 2048
for k, i in s2.items(): t2[k] = addr[i]
for k, i in s3.items(): t3[k] = addr[i]
with open(out, 'wb') as f:
    f.write(struct.pack('>I', len(chunks)))
    for o, b in chunks: f.write(struct.pack('>II', o, len(b))); f.write(b)
open(out + '.s2tab', 'wb').write(struct.pack('>8192I', *t2))
open(out + '.s3tab', 'wb').write(struct.pack('>2048I', *t3))
print('scroll2 %d tiles, scroll3 %d tiles, unique %d, %d KB' % (len(s2), len(s3), len(blobs), sum(len(b) for b in blobs) // 1024))
