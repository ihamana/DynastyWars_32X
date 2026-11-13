#!/usr/bin/env python3
"""(V3.6) which translated file goes into the small ROM windows .rom2 (0x6D00) / .rom3 (0xB800 with ui.o: the FM effects follow at cart 0x36B800):
the largest that fits, so a new region split (hot list / natives) still links.  writes build/sh2_gen.ld"""
import subprocess, glob, sys, re
B = sys.argv[1]; T = sys.argv[2]
def size(f, pat=('.text', '.rodata', '.rom2ui')):
    o = subprocess.run(['/opt/tc/sh/bin/sh-elf-objdump', '-h', f], capture_output=True, text=True).stdout
    return sum(int(p[2], 16) for p in (l.split() for l in o.split('\n')) if len(p) > 3 and p[1].startswith(pat))
fs = {f.split('/')[-1]: size(f) for f in glob.glob(B + '/split/p*.o')}
ui = size(B + '/ui.o', ('.text', '.rodata')) + sum(size(f, ('.rom2ui',)) for f in glob.glob(B + '/*.o') + glob.glob(B + '/split/*.o'))
def fill(cap, avail):
    best = (0, [])
    import itertools
    fl = sorted(avail, key=lambda f: -fs[f])
    for n in (1, 2, 3):
        for c in itertools.combinations(fl, n):
            t = sum(fs[f] for f in c)
            if t <= cap and t > best[0]: best = (t, list(c))
    return best[1]
r2 = fill(0x6D00 - 0x80, list(fs))
r3 = fill(0xB800 - 0x100 - ui, [f for f in fs if f not in r2])
ld = open(T + '/sh2/sh2.ld').read()
ld = ld.replace('*p01.o(.text .text.* .rodata .rodata.*)', ' '.join('*%s(.text .text.* .rodata .rodata.*)' % f for f in r2))
ld = ld.replace('*p05.o(.text .text.* .rodata .rodata.*)', ' '.join('*%s(.text .text.* .rodata .rodata.*)' % f for f in r3))
import os
if os.environ.get('ROMLEN'): ld = ld.replace('LENGTH = 0xE0000', 'LENGTH = ' + os.environ['ROMLEN'])
open(B + '/sh2_gen.ld', 'w').write(ld)
print('rom2 %s %d, rom3 %s %d + ui %d' % (r2, sum(fs[f] for f in r2), r3, sum(fs[f] for f in r3), ui))
