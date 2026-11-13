#!/usr/bin/env python3
"""MAME dynwar.zip (parent set with the dynwarj / dynwara folders, as MAME stores it) -> the ROM images the
tools use: dynwarj_prg.bin (68000 program), gfx.bin (CPS-A graphics, 4 MB), z80.bin, oki.bin.
usage: rom.py dynwar.zip OUTDIR"""
import sys, os, zipfile, hashlib
z = zipfile.ZipFile(sys.argv[1]); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
names = {os.path.basename(n) if not n.startswith(('dynwarj/', 'dynwara/')) else n: n for n in z.namelist()}
def r(p):
    if p in names: return z.read(names[p])
    for n in z.namelist():                      # split sets / other folder layouts: match by file name
        if n.endswith('/' + p) or n == p or os.path.basename(n) == os.path.basename(p) and '/' not in p: return z.read(n)
    sys.exit('missing ROM file ' + p)
def inter(e, o): b = bytearray(len(e) * 2); b[0::2] = e; b[1::2] = o; return bytes(b)
prg = inter(r('dynwarj/36.12f'), r('dynwarj/42.12h')) + inter(r('dynwarj/37.13f'), r('dynwarj/43.13h')) + \
      inter(r('dynwara/34.10f'), r('dynwara/40.10h')) + inter(r('dynwara/35.11f'), r('dynwara/41.11h'))
g = bytearray(0x400000)
for base, nm in ((0, ['tkm-5.7a', 'tkm-8.9a', 'tkm-6.3a', 'tkm-7.5a']), (0x200000, ['tkm-1.8a', 'tkm-4.10a', 'tkm-2.4a', 'tkm-3.6a'])):
    for k, n in enumerate(nm):
        d = r(n)
        for i in range(0, len(d), 2): g[base + i * 4 + k * 2:base + i * 4 + k * 2 + 2] = d[i:i + 2]
# the Japanese set replaces one graphics ROM (14.10b) of the dynwara layout
idx = {}
for i in range(8):
    for blk in range(0, 0x400000, 0x100000): idx[bytes(g[blk + i:blk + 0x100000:8])] = (blk, i)
blk, i = idx[r('dynwara/14.10b')]; g[blk + i:blk + 0x100000:8] = r('dynwarj/14.10b')
files = {'dynwarj_prg.bin': prg, 'gfx.bin': bytes(g), 'z80.bin': r('tke_17.12b'), 'oki.bin': r('dynwarj/30.12e') + r('dynwarj/31.13e')}
SHA1 = {'dynwarj_prg.bin': '94786bb50e28c0176cdb7d2bb9d24eaa8ec6eea3', 'gfx.bin': 'fe1b91927832c971070f20c2997c56b009b75a24', 'z80.bin': '2b960545741d3b9a53ffbf3ed83030392aa02698', 'oki.bin': '1fe4a575e31726f1bd3bd1460dfe1f52cd665b1f'}
bad = 0
for n, d in files.items():
    open(os.path.join(out, n), 'wb').write(d)
    h = hashlib.sha1(d).hexdigest()
    if h != SHA1[n]: print('WARNING: %s sha1 %s (expected %s): not the MAME dynwarj set?' % (n, h, SHA1[n])); bad += 1
print('ROM images ok' if not bad else 'ROM images written with %d mismatches' % bad)
sys.exit(1 if bad else 0)
