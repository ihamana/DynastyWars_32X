#!/usr/bin/env python3
"""Sparse host snapshots.  A snapshot of the arcade state (CPS VRAM, CPS-A/B registers, object RAM, palette,
work RAM) taken by the host emulator; the tools only read a few parts of it (tile map cells, registers,
palettes), so meta/snap keeps only those bytes (no graphics, no program): 'DWSS' u32 size, u32 n,
n x (u32 offset, u32 length, data).
  snap.py pack TRACK.json SRCROOT OUTROOT     (dev: the byte ranges a tool read, tools/track.py)
  snap.py unpack INROOT OUTROOT                (build: *.dwss -> full-size snapshot files, the rest zero)"""
import sys, os, json, struct, glob
def unpack1(fn):
    b = open(fn, 'rb').read(); assert b[:4] == b'DWSS'
    size, n = struct.unpack('>II', b[4:12]); out = bytearray(size); p = 12
    for i in range(n):
        o, l = struct.unpack('>II', b[p:p + 8]); p += 8; out[o:o + l] = b[p:p + l]; p += l
    return bytes(out)
if sys.argv[1] == 'pack':
    T = json.load(open(sys.argv[2])); src, dst = sys.argv[3], sys.argv[4]
    for fn, rg in T.items():
        rel = os.path.relpath(fn, src); d = open(fn, 'rb').read()
        mg = []
        for a, b in rg:                                  # merge ranges closer than 16 bytes
            if mg and a - mg[-1][1] < 16: mg[-1][1] = b
            else: mg.append([a, b])
        o = os.path.join(dst, rel[:-4] + '.dwss'); os.makedirs(os.path.dirname(o), exist_ok=True)
        open(o, 'wb').write(b'DWSS' + struct.pack('>II', len(d), len(mg)) + b''.join(struct.pack('>II', a, b - a) + d[a:b] for a, b in mg))
else:
    src, dst = sys.argv[2], sys.argv[3]
    for fn in glob.glob(src + '/**/*.dwss', recursive=True):
        o = os.path.join(dst, os.path.relpath(fn, src)[:-5] + '.bin'); os.makedirs(os.path.dirname(o), exist_ok=True)
        open(o, 'wb').write(unpack1(fn))
