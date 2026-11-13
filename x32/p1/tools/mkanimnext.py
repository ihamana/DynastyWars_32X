#!/usr/bin/env python3
"""Stage 1 army dust: next-step table for the MD (st1bga_next.bin, u8 per anim entry, 0xFF = none).
The arcade steps all its dust cells together every 4 frames; the MD advances its cells with this
table on a 'tick' message, so only the irregular changes have to be sent.
usage: mkanimnext.py st1bga_anim.bin out snapdir..."""
import sys, glob, struct, collections
an = open(sys.argv[1], 'rb').read(); n = struct.unpack('>H', an[:2])[0]
keys = {}
for i in range(n):
    code, attr = struct.unpack('>HH', an[2 + i * 12:6 + i * 12]); keys[(code, attr)] = i
def idx(code, attr): return keys.get((code, attr), 0xFF)
succ = collections.defaultdict(collections.Counter); camx = []
for d in sys.argv[3:]:
    prev = None
    for fn in sorted(glob.glob(d + '/snap_*.bin')):
        b = open(fn, 'rb').read(); g = b[:0x30000]
        cpsa = struct.unpack('<32H', b[0x30000:0x30040]); base = (cpsa[2] * 256) & 0x3ffff
        cur = {}
        for slot in range(64):
            for r in range(4):
                i = base + (0xC00 + slot * 16 + 4 + r) * 4
                cur[(slot, r)] = idx((g[i] << 8) | g[i + 1], g[i + 3] & 0x7f)
        camx.append(cpsa[8])
        if prev:
            ch = sum(cur[k] != prev[k] for k in cur)
            if ch >= 20:                                  # a tick frame
                for k in cur: succ[prev[k]][cur[k]] += 1
        prev = cur
nxt = bytearray([0xFF] * 256)
for i in range(n):
    c = succ.get(i)
    if not c: nxt[i] = i; continue
    t, k = c.most_common(1)[0]
    nxt[i] = t if k >= 2 and k * 10 >= sum(c.values()) * 6 else i
print('entries %d, with a successor %d, cam x %d..%d' % (n, sum(nxt[i] != i for i in range(n)), min(camx), max(camx)))
open(sys.argv[2], 'wb').write(bytes(nxt[:n + (n & 1)]))
