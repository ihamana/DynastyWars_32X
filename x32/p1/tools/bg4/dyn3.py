#!/usr/bin/env python3
"""32X table of the dynamic scroll3 cells of a stage (the arcade rewrites them; stage 2 harbour):
sorted u32 key (code << 16 | attr & 0x7F) -> u32 source cell in the MD map (column << 5 | row, see convbg4.py EXTRA3).
The first NANIM entries are animated groups (the arcade steps code bits 0-1 of all of them together, every few
frames): 4 consecutive rows each, source bit 15 set.
usage: dyn3.py extra3.pkl W out.bin [NANIM]   (W: first extra column = arcade scroll3 width + 12)"""
import sys, pickle, struct
ex = pickle.load(open(sys.argv[1], 'rb')); W = int(sys.argv[2]); na = int(sys.argv[4]) if len(sys.argv) > 4 else 0
assert na % 4 == 0 and all(ex[i][0] & 3 == i & 3 for i in range(na))
ent = sorted(((code << 16) | (attr & 0x7F), ((W + i // 24) << 5) | (i % 24) | (0x8000 if i < na else 0)) for i, (code, attr) in enumerate(ex))
assert len(set(k for k, _ in ent)) == len(ent)
out = struct.pack('>I', len(ent)) + b''.join(struct.pack('>II', k, s) for k, s in ent)
open(sys.argv[3], 'wb').write(out); print('dyn3', len(ent), 'entries', len(out), 'bytes')
