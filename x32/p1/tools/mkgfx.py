#!/usr/bin/env python3
"""CPS1 sprite + scroll1 graphics -> 32X packed 4bpp (left pixel in the high nibble, pen 15 =
transparent), duplicates and fully transparent tiles removed.
  sprites: 16x16, 8 bytes per row, only rows r0..r1 stored; table spr_tab[12288] = r0 << 28 | r1 << 24 |
  (SH-2 address & 0xFFFFFF), or 0 (blank)
  scroll1: one 8x8 half per entry, 4 bytes per row (32 B); table s1_tab[8192*2] (code*2+half)
Data goes into the cart regions given on the command line (offset:size,...)."""
import sys, os, struct, numpy as np
gfx = np.frombuffer(open(sys.argv[1], 'rb').read(), np.uint8)
out = sys.argv[2]
regions = [tuple(int(x, 0) for x in r.split(':')) for r in sys.argv[3].split(',')]   # sprite data regions
s1ofs = int(sys.argv[4], 0)
def pack(region):
    a = region.reshape(-1, 4)
    bits = np.unpackbits(a, axis=1).reshape(-1, 4, 8)
    pen = (bits[:, 3, :] << 3) | (bits[:, 2, :] << 2) | (bits[:, 1, :] << 1) | bits[:, 0, :]
    return ((pen[:, 0::2] << 4) | pen[:, 1::2]).astype(np.uint8).reshape(-1)
spr = pack(gfx[:0x180000]).reshape(12288, 128)
s1 = pack(gfx[0x180000:0x200000]).reshape(8192, 8, 8)
CART = 0x02000000
# sprites
uniq = {}; order = []
for t in range(12288):
    b = spr[t].tobytes()
    if b == b'\xff' * 128: continue
    if b not in uniq: uniq[b] = len(order); order.append(b)
# only rows r0..r1 (first..last row with a visible pixel) are stored: table entry = r0 << 28 | r1 << 24 | address & 0xFFFFFF
trim = []
for b in order:
    rows = [b[r * 8:r * 8 + 8] for r in range(16)]
    nz = [r for r in range(16) if rows[r] != b'\xff' * 8]
    if os.environ.get('NOTRIM'): nz = [0, 15]
    trim.append((nz[0], nz[-1], b''.join(rows[nz[0]:nz[-1] + 1])))
addr = []; blobs = []; ri = 0; used = 0
for r0, r1, d in trim:
    while used + len(d) > regions[ri][1]: ri += 1; used = 0
    addr.append(CART + regions[ri][0] + used); used += len(d)
end_used = regions[ri][0] + used
tab = [0] * 12288
for t in range(12288):
    b = spr[t].tobytes()
    if b != b'\xff' * 128: k = uniq[b]; tab[t] = (trim[k][0] << 28) | (trim[k][1] << 24) | (addr[k] & 0xFFFFFF)
img = {}
for k, (r0, r1, d) in enumerate(trim):
    img[addr[k] - CART] = d
# scroll1 halves
hu = {}; ho = []
s1tab = [0] * 16384
for c in range(8192):
    for g in (0, 1):
        b = s1[c][:, g * 4:g * 4 + 4].tobytes()
        if b == b'\xff' * 32: continue
        if b not in hu: hu[b] = len(ho); ho.append(b)
        s1tab[c * 2 + g] = CART + s1ofs + hu[b] * 32
s1blob = b''.join(ho)
# write: a list of (cart offset, bytes)
chunks = [(s1ofs, s1blob)]
cur = None
for o in sorted(img):
    if cur and cur[0] + len(cur[1]) == o: cur[1].extend(img[o])
    else:
        if cur: chunks.append((cur[0], bytes(cur[1])))
        cur = [o, bytearray(img[o])]
chunks.append((cur[0], bytes(cur[1])))
with open(out, 'wb') as f:
    f.write(struct.pack('>I', len(chunks)))
    for o, b in chunks: f.write(struct.pack('>II', o, len(b))); f.write(b)
open(out + '.sprtab', 'wb').write(struct.pack('>12288I', *tab))
open(out + '.s1tab', 'wb').write(struct.pack('>16384I', *s1tab))
print('sprite data ends at 0x%X (last region)' % end_used)
print('sprites %d unique (%d KB trimmed), scroll1 halves %d (%d KB)' % (len(order), sum(len(d) for _, _, d in trim) // 1024, len(ho), len(s1blob) // 1024))
