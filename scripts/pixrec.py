#!/usr/bin/env python3
"""Pixel recipes: MD/32X tile data made from the arcade graphics, kept as references instead of pixels.
Every 8x8 tile (4bpp, 32 bytes) in the converted backgrounds is a colour look-up of one 8x8 block of an arcade
scroll2 (16x16) or scroll3 (32x32) tile: tile = LUT[pens of (layer, code, sub-block, flips)].  A .dwpx file keeps
the file with its tile sections zeroed (maps, tables, palettes: no pixels) plus one recipe per tile, so the
pixels come back only from the arcade graphics ROM.
  'DWPX' u32 size, u32 nsec, nsec x (u32 offset, u32 ntiles), u32 zlen, zlib(file with sections zeroed),
  recipes (12 bytes: u8 layer (0 = empty tile), u16 code, u8 sub | flips << 4, u8 lut[8] (16 nibbles))
  pixrec.py unpack GFX_BIN IN_ROOT OUT_ROOT   (build)
  pixrec.py pack FILE OUT.dwpx OFS:PKL ...     (dev: OFS = byte offset of a section, PKL = tools/tilerec output)"""
import sys, os, struct, zlib, glob, pickle
import numpy as np
def gfx_pens(gfx):
    def pens(a, ts):
        a = a.reshape(-1, 4); bits = np.unpackbits(a, axis=1).reshape(-1, 4, 8)
        return ((bits[:, 3] << 3) | (bits[:, 2] << 2) | (bits[:, 1] << 1) | bits[:, 0]).reshape(-1, ts, ts)
    return pens(gfx[0x6000 * 128:0x8000 * 128], 16), pens(gfx[0x1000 * 512:0x1800 * 512], 32)
def unpack1(fn, P2, P3):
    b = open(fn, 'rb').read(); assert b[:4] == b'DWPX'
    size, ns = struct.unpack('>II', b[4:12]); p = 12; sec = []
    for i in range(ns): sec.append(struct.unpack('>II', b[p:p + 8])); p += 8
    zl = struct.unpack('>I', b[p:p + 4])[0]; p += 4
    out = bytearray(zlib.decompress(b[p:p + zl])); p += zl; assert len(out) == size
    for ofs, n in sec:
        for i in range(n):
            L, code, sf = struct.unpack('>BHB', b[p:p + 4]); lut = b[p + 4:p + 12]; p += 12
            if not L: continue
            lut = np.array([v for x in lut for v in (x >> 4, x & 15)], np.uint8)
            sx, sy, fl = sf & 3, (sf >> 2) & 3, sf >> 4
            t = (P2[code - 0x2000] if L == 2 else P3[code])[sy * 8:sy * 8 + 8, sx * 8:sx * 8 + 8]
            if fl & 1: t = t[:, ::-1]
            if fl & 2: t = t[::-1, :]
            px = lut[t]
            out[ofs + i * 32:ofs + i * 32 + 32] = ((px[:, 0::2] << 4) | px[:, 1::2]).astype(np.uint8).tobytes()
    return bytes(out)
if sys.argv[1] == 'unpack':
    P2, P3 = gfx_pens(np.fromfile(sys.argv[2], np.uint8)); src, dst = sys.argv[3], sys.argv[4]
    for fn in sorted(glob.glob(src + '/**/*.dwpx', recursive=True)):
        o = os.path.join(dst, os.path.relpath(fn, src)[:-5] + '.bin'); os.makedirs(os.path.dirname(o), exist_ok=True)
        open(o, 'wb').write(unpack1(fn, P2, P3))
else:
    d = bytearray(open(sys.argv[2], 'rb').read()); secs = []; recs = b''
    for s in sys.argv[4:]:
        ofs, pk = s.split(':', 1); ofs = int(ofs, 0); R = pickle.load(open(pk, 'rb')); secs.append((ofs, len(R)))
        d[ofs:ofs + 32 * len(R)] = bytes(32 * len(R))
        for r in R:
            if r is None: recs += bytes(12); continue
            (L, code, sub, fl), lut = r
            recs += struct.pack('>BHB', L, code, sub | fl << 4) + bytes((int(lut[2 * i]) << 4) | int(lut[2 * i + 1]) for i in range(8))
    z = zlib.compress(bytes(d), 9)
    open(sys.argv[3], 'wb').write(b'DWPX' + struct.pack('>II', len(d), len(secs)) + b''.join(struct.pack('>II', *s) for s in secs) + struct.pack('>I', len(z)) + z + recs)
