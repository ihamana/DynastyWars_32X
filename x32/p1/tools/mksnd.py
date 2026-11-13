#!/usr/bin/env python3
"""Sound data for the Mega-CD sub CPU, on the CD data track (after the 150-sector ISO part).
  OKI ROM (the arcade's voices, up to the last phrase: 0x3B400 bytes) -> PRG-RAM 0x44C00-0x7FFFF
  sound effects 0x20..0x7F (offline renders of the arcade YM2151 effects, from DW_RE: p0/sfx/sXX.wav)
     16 kHz, OKI ADPCM (4 bits, decoded by the sub CPU like the voices) -> PRG-RAM 0x10000-0x44BFF
     (Word RAM is left to the main CPU: stage backgrounds)
usage: mksnd.py base.iso out.iso cmdlist(hex,comma or 'all')"""
import sys, struct, numpy as np, wave
from scipy.signal import resample_poly
SRC = '/tmp/dw/sfx/'   # (only the 0x7F jingle is built now: the effects are FM, tools/fm/fmsfx.py)
# V2.1: the level-up jingle (arcade music command 0x16, CD track 23) as PCM effect 0x7F: played over the CD music,
# which keeps going (was a CD jingle track, then a seek back into the music: a gap of silence)
JINGLE = '/tmp/dw/trk/t23.wav'; JGAIN = 1.0
base = open(sys.argv[1], 'rb').read()
assert len(base) == 150 * 2048
want = None if sys.argv[3] == 'all' else set(int(x, 16) for x in sys.argv[3].split(','))
def load(fn):
    w = wave.open(fn); sr = w.getframerate()
    a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(np.float32).mean(1)
    return a, sr
STEPS = [16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552]
SADJ = [-1,-1,-1,-1,2,4,6,8]
def adpcm(x):
    """OKI/Dialogic ADPCM, choosing each code against the sub CPU's decoder (pcm.c next_sample)"""
    sig = 0; st = 0; out = []
    for v in x:
        s_ = STEPS[st]; best = None
        for n in range(16):
            d = ((2 * (n & 7) + 1) * s_) >> 3
            if n & 8: d = -d
            g = max(-2048, min(2047, sig + d))
            e = abs(g - v)
            if best is None or e < best[0]: best = (e, n, g)
        _, n, g = best; sig = g; st = max(0, min(48, st + SADJ[n & 7])); out.append(n)
    if len(out) & 1: out.append(0)
    return bytes((out[i] << 4) | out[i + 1] for i in range(0, len(out), 2))
def conv(fn, cap=1.0, gain=0.9, lo=False):
    a, sr = load(fn)
    blk = sr // 100
    e = np.array([np.abs(a[j:j + blk]).max() for j in range(0, len(a), blk)])
    loud = np.where(e > 250)[0]
    if len(loud) == 0: return None
    t = min((loud[-1] + 1) / 100 + 0.02, cap)
    a = a[:int(t * sr)]
    nf = min(int(0.05 * sr), len(a)); a[-nf:] *= np.linspace(1, 0, nf)
    b = (resample_poly(a, 40, 147) if lo else resample_poly(a, 160, 441)) * gain / 16.0   # 16 kHz (V3.2: or 12 kHz), 12 bits (pcm.c: x FXVOL/256 -> 8 bits)
    b = np.clip(np.round(b), -2047, 2047).astype(int)
    return adpcm(b)
def src(c, lo=False):
    return conv(JINGLE, cap=5.0, gain=JGAIN) if c == 0x7F else conv(SRC + 's%02X.wav' % c, lo=lo)   # 0x7F: the level-up jingle (V2.1)
sfx = {}; low = set()
for c in range(0x20, 0x80):
    if want is not None and c not in want: continue
    try: d = src(c)
    except FileNotFoundError: continue
    if d: sfx[c] = d
OKIAT, OKILEN = 0x44C00, 0x3B400
# V3.2: also the sub CPU's PRG-RAM between its program/bss and its stack (sub.ld, sub_asm.s: stack 0xF7F0 down, header 0xF800)
def pack():
    regions = [[0x10000, OKIAT, bytearray()], [0x3C00, 0xE800, bytearray()]]
    tab = {}
    for c, d in sorted(sfx.items(), key=lambda kv: -len(kv[1])):
        for r in regions:
            if r[0] + (len(r[2]) + len(d) + 2047) // 2048 * 2048 <= r[1]:   # whole sectors are read (CDC DMA)
                tab[c] = (r[0] + len(r[2]), len(d) | (0x80000000 if c in low else 0)); r[2] += d; break
        else: return None, regions
    return tab, regions
import json as _j, os as _o   # how often each effect was played in the 7 stage host runs (data/sfx_count.json)
CNT = _j.load(open(sys.path[0] + '/../data/sfx_count.json')) if _o.path.exists(sys.path[0] + '/../data/sfx_count.json') else {}
while True:     # V3.2: if they do not fit, the longest 16 kHz effects become 12 kHz (pcm.c: length bit 31 = 12 kHz)
    tab, regions = pack()
    if tab: break
    hi = [c for c in sfx if c not in low and c != 0x7F]
    if not hi: sys.exit('sound effects do not fit')
    hi = [c for c in hi if len(sfx[c]) >= 3072] or hi
    c = min(hi, key=lambda c: (CNT.get('%02X' % c, 0), -len(sfx[c]))); low.add(c); sfx[c] = src(c, True)   # the least played first
print('effects at 12 kHz:', ' '.join('%02X' % c for c in sorted(low)))
# stage MD backgrounds (drawn by the 68000 from Word RAM, 2M mode): stage 0 loaded at start-up, the others when the
# main CPU asks (sub.c command 0x30).  Blob: 'BGST', u16 stage, u16 0, u32 offsets of tiles, map A, map B, palette, tile lines.
BG = sys.path[0] + '/../data/bg/'
import os
STAGES = {0: ('st1bga_tiles', 'st1bgp_mapA', 'st1bgp_mapB', 'st1bgp_pal', 'st1bga_tline')}
for k in range(1, 32):
    if os.path.exists(BG + 's%d_tiles.bin' % k): STAGES[k] = tuple('s%d_%s' % (k, n) for n in ('tiles', 'mapA', 'mapB', 'pal', 'tline'))
bgblob = {}
for k, names in STAGES.items():
    wr = bytearray(32); ofs = []
    for nm in names:
        d = open(BG + nm + '.bin', 'rb').read()
        ofs.append(len(wr)); wr += d; wr += bytes((-len(wr)) & 0xFF)
    struct.pack_into('>4sHH5I', wr, 0, b'BGST', k, 0, *ofs)
    mp = BG + '../mdpic/blob%d.bin' % k                 # V2.1: pictures shown by the MD (tools/mdpic.py), header +28
    if os.path.exists(mp):
        struct.pack_into('>I', wr, 28, len(wr)); wr += open(mp, 'rb').read(); wr += bytes((-len(wr)) & 0xFF)
    assert len(wr) <= 0x40000, 'stage %d background does not fit in Word RAM: %d' % (k, len(wr))
    bgblob[k] = bytes(wr)
    print('stage %d background %d KB' % (k, len(wr) // 1024))
wr = bgblob[0]
oki = open('/tmp/dw/rom/oki.bin', 'rb').read()
assert len(oki) == 0x40000 and oki[OKILEN:] == b'\xff' * (0x40000 - OKILEN) or True
blobs = [(oki[:OKILEN], OKIAT)] + [(bytes(r[2]), r[0]) for r in regions if r[2]] + [(bytes(wr), 0x80000)]
data = bytearray(); hdr = bytearray(2048)
struct.pack_into('>II', hdr, 0, 0x44573332, len(blobs))
lba = 151
for i, (b, dest) in enumerate(blobs):
    ns = (len(b) + 2047) // 2048
    if dest == 0x80000: struct.pack_into('>II', hdr, 0x600, lba, ns)    # stage 0 background (start-up blob)
    struct.pack_into('>III', hdr, 8 + i * 12, lba, ns, dest)
    data += b + bytes(ns * 2048 - len(b)); lba += ns
for k in sorted(bgblob)[1:]:                       # +0x600: stage k background: u32 lba, u32 sectors
    b = bgblob[k]; ns = (len(b) + 2047) // 2048
    struct.pack_into('>II', hdr, 0x600 + k * 8, lba, ns)
    data += b + bytes(ns * 2048 - len(b)); lba += ns
for c in range(0x20, 0x80):
    a, n = tab.get(c, (0, 0))
    struct.pack_into('>II', hdr, 0x100 + (c - 0x20) * 8, a, n)
# arcade channel / priority rules (tools/mksfxpri.py): +0x400 u8 n, then n (<= 63) x { u8 id, u8 ch, u16 0, u32 cuts 0-31, u32 cuts 32-63 } (V3.2: 12 bytes)
import json, os
pj = sys.path[0] + '/../data/sfx_pri.json'
if os.path.exists(pj):
    P = json.load(open(pj)); L = sorted(P['ch'], key=lambda x: int(x, 16))[:63]
    cut = {(a, b) for a, b in P['cut']}
    hdr[0x400] = len(L)
    for i, b in enumerate(L):
        m = sum(1 << j for j, a in enumerate(L) if (a, b) in cut)
        struct.pack_into('>BBHII', hdr, 0x404 + i * 12, int(b, 16), P['ch'][b], 0, m & 0xFFFFFFFF, m >> 32)
    assert 0x404 + len(L) * 12 <= 0x600
open(sys.argv[2], 'wb').write(base + bytes(hdr) + bytes(data))
print('background %d KB in Word RAM' % (len(wr) // 1024)); print('effects %d (%d KB), regions %s, sectors %d' % (len(sfx), sum(len(d) for d in sfx.values()) // 1024,
      ['%X+%dKB' % (r[0], len(r[2]) // 1024) for r in regions], lba))
