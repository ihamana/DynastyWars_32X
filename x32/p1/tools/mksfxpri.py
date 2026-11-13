#!/usr/bin/env python3
"""Arcade sound-effect channel / priority rules, measured with the Z80 sound driver (DW_RE ref/sndref):
each effect plays on one YM2151 channel (6 or 7); a new effect on a busy channel starts only if the driver
lets it cut the one playing (b is sent 0.05 s after a).  -> data/sfx_pri.json {ch: {id: ch}, cut: [[a, b], ...]}
usage: mksfxpri.py ids(hex, comma)"""
import sys, os, subprocess, json
REF = '/dw/dw/ref/sndref'
ids = sys.argv[1].split(',')
tmp = '/tmp/dw/sfxpri.txt'
def keyons(cmd, seq=None, secs='0.3'):
    env = dict(os.environ, ROMDIR='/tmp/dw/rom')
    if seq: env['SEQ'] = seq
    subprocess.run([REF, cmd, secs, '/dev/null', tmp], env=env, capture_output=True)
    out = []
    for l in open(tmp):
        t, r, v = l.split(); t = float(t); r = int(r, 16); v = int(v, 16)
        if t >= 0 and r == 8 and (v & 0x78): out.append((t, v & 7))
    return out
ch = {}
for c in ids:
    k = keyons(c, secs='2')
    ch[c] = k[0][1] if k else 7
cut = []
for a in ids:
    for b in ids:
        if ch[a] != ch[b]: continue
        if any(0.045 <= t < 0.1 for t, _ in keyons(a, '0.05:' + b)): cut.append([a, b])
json.dump({'ch': ch, 'cut': cut}, open(sys.path[0] + '/../data/sfx_pri.json', 'w'))
print('channels', ch, 'cut pairs', len(cut))
