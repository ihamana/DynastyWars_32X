#!/usr/bin/env python3
"""(32xM V3.5) arcade sound effects on the Mega Drive's YM2612, played by a Z80 program.

The arcade plays its effects on YM2151 channels 6 / 7 from a Z80 driver ticking every 4.005 ms.  The register writes
of each effect (recorded with the arcade driver: dw/ref/sndref) are converted to YM2612 writes:
  * same operator layout / envelope / TL formats (OPM M1 M2 C1 C2 = OPN S1 S3 S2 S4), key on bits << 1
  * pitch (KC / KF) -> a pitch number p = semitone * 64 + KF; the Z80 turns p (+ per-operator offset) into block / F-number
  * YM2151 DT2 (operator coarse detune, +600 / +781 / +950 cents): a common offset + the nearest MUL per operator, on
    YM2612 channel 2 (arcade channel 6) / 1 (7); the effects where that is audibly off (EXACT) also have a version for
    channel 3 in its special mode (one frequency per operator), used when the other slot is not on channel 3
  * the streams have no channel: the Z80 adds it (registers 0x30-0xB7 but 0xA8-0xAF, the key-on value)
Stream bytes: 00 end, 01-1C wait n ticks, 1D o1 o2 o3 o4 (u16 offsets: normal, A8, A9, AA frequency registers),
  1E p (u16): pitch, 1F r v: carrier TL (+ the effect volume attenuation), r v (r >= 0x20): plain YM2612 write.
Output data/fm/fmsfx.bin (cart, md/snd.c FM_ROM):
  +0     96 x { u16 offset, u16 length, u16 offset / length of the channel 3 version (0: none), u8 slot (0xFF none), u8 index }
  +0x480 64 x u64 cut masks (bit j: the effect may cut playing effect index j; arcade priority rules, data/sfx_pri.json)
  +0x680 u16 Z80 image length, Z80 image (code, F-number table), u16 n, slot controls (Z80 0x1F00)
  then the streams
usage: fmsfx.py [ids hex,comma]   (default: the effects in data/sfx_pri.json)"""
import sys, os, json, struct, subprocess, math
P1 = os.path.abspath(os.path.join(os.path.dirname(__file__), '../..'))
REF = '/dw/dw/ref/sndref'
TMP = '/tmp/dw/fmsfx'; os.makedirs(TMP, exist_ok=True)
TICK = 1024 * (256 - 0xF2) / 3579545.0            # arcade driver: YM2151 timer B 0xF2
OPNCLK = 7670453.0
NOTE = {0: 0, 1: 1, 2: 2, 3: 2, 4: 3, 5: 4, 6: 5, 7: 5, 8: 6, 9: 7, 10: 8, 11: 8, 12: 9, 13: 10, 14: 11, 15: 11}
DT2 = [0, 384, 500, 608]                           # YM2151 DT2 in 1/64 semitones (600, 781, 950 cents)
CARRIERS = {0: (3,), 1: (3,), 2: (3,), 3: (3,), 4: (2, 3), 5: (1, 2, 3), 6: (1, 2, 3), 7: (0, 1, 2, 3)}   # OPM register order k
MULS = [0.5] + list(range(1, 16))

# ---------------------------------------------------------------- arcade recording
def record(c):
    fn = '%s/log_%s.txt' % (TMP, c)
    if not os.path.exists(fn):
        subprocess.run([REF, c, '6', '/dev/null', fn], env=dict(os.environ, ROMDIR='/tmp/dw/rom'), capture_output=True)
    L = [(float(t), int(r, 16), int(v, 16)) for t, r, v in (l.split() for l in open(fn))]
    ch = next(v & 7 for t, r, v in L if t >= 0 and r == 8 and v & 0x78)
    st = {}; ev = []
    for t, r, v in L:
        if t < 0: continue
        if r == 8:
            if (v & 7) != ch: continue
            v &= 0x78
        elif r >= 0x20:
            if (r & 7) != ch: continue
            r &= 0xF8
            if st.get(r) == v: continue
            st[r] = v
        else: continue
        ev.append((int(round(t / TICK)), r, v))
    return ch, ev

# ---------------------------------------------------------------- conversion
def convert(ev, exact):
    """stream without the channel (the Z80 adds it): exact = channel 3 special mode (one frequency per operator)"""
    slot = exact; oc = 0
    out = bytearray([0xB4, 0xC0])
    st = {}; tk = 0
    def wait(n):
        while n > 0: k = min(n, 0x1C); out.append(k); n -= k
    def approx():                                 # channel 2: one offset for the channel + nearest MUL per operator
        best = None
        for B in DT2:
            err = 0; muls = []
            for k in range(4):
                mul = st.get(0x40 + 8 * k, 0) & 15; d2 = st.get(0xC0 + 8 * k, 0) >> 6
                want = (0.5 if mul == 0 else mul) * 2 ** ((DT2[d2] - B) / 768.0)
                m = min(MULS, key=lambda x: abs(math.log2(x / want)))
                w = 4 if k in CARRIERS[st.get(0x20, 0) & 7] else 1
                err += w * abs(math.log2(m / want)); muls.append(0 if m == 0.5 else m)
            if best is None or err < best[0] - 1e-9: best = (err, B, muls)
        return best[1], best[2]
    last_offs = None; last_mul = [None] * 4
    groups = {}
    for t, r, v in ev: groups.setdefault(t, []).append((r, v))
    for t in sorted(groups):
        wait(t - tk); tk = t
        pitch = False; dt = False
        for r, v in groups[t]:
            st[r] = v
            if r == 8: out += bytes([0x28, ((v >> 3) & 15) << 4 | oc])
            elif r == 0x20: out += bytes([0xB0 + oc, v & 0x3F])
            elif r in (0x28, 0x30): pitch = True
            elif 0x40 <= r < 0x60:
                if slot: out += bytes([0x30 + ((r - 0x40) >> 3) * 4 + oc, v & 0x7F])
                else: dt = True
            elif 0x60 <= r < 0x80:
                k = (r - 0x60) >> 3
                out += bytes([0x1F if k in CARRIERS[st.get(0x20, 0) & 7] else 0x40 + k * 4 + oc] + ([0x40 + k * 4 + oc] if k in CARRIERS[st.get(0x20, 0) & 7] else []) + [v & 0x7F])
            elif 0x80 <= r < 0xA0: out += bytes([0x50 + ((r - 0x80) >> 3) * 4 + oc, v])
            elif 0xA0 <= r < 0xC0: out += bytes([0x60 + ((r - 0xA0) >> 3) * 4 + oc, v & 0x9F])
            elif 0xC0 <= r < 0xE0: out += bytes([0x70 + ((r - 0xC0) >> 3) * 4 + oc, v & 0x1F]); dt = True
            else: out += bytes([0x80 + ((r - 0xE0) >> 3) * 4 + oc, v])
        if dt or last_offs is None:
            if slot:
                d2 = [st.get(0xC0 + 8 * k, 0) >> 6 for k in range(4)]
                offs = [DT2[d2[3]], DT2[d2[1]], DT2[d2[0]], DT2[d2[2]]]   # normal = S4, A8 = S3, A9 = S1, AA = S2
            else:
                B, muls = approx(); offs = [B, 0, 0, 0]
                for k in range(4):
                    v = (st.get(0x40 + 8 * k, 0) & 0x70) | muls[k]
                    if v != last_mul[k]: out += bytes([0x30 + k * 4 + oc, v]); last_mul[k] = v
            if offs != last_offs: out += bytes([0x1D]) + struct.pack('>4H', *offs); last_offs = offs; pitch = True
        if pitch:
            kc = st.get(0x28, 0); kf = st.get(0x30, 0) >> 2
            p = (((kc >> 4) & 7) * 12 + NOTE[kc & 15]) * 64 + kf
            out += bytes([0x1E]) + struct.pack('>H', p)
    out.append(0)
    return bytes(out)

# F-number table: p = semitone * 64 + KF from OPM octave 0 C#; F-number of p mod 768 at block 0 (= x2 for block 1...)
def ftab():
    t = []
    for r in range(768):
        f = 440.0 * 2 ** ((r / 64.0 - 56) / 12.0)          # A4 = octave 4, semitone 8 (OPM, 3.58 MHz clock)
        t.append(min(2047, int(round(f * 144 * (1 << 20) / OPNCLK * 2))))
    return t

# ---------------------------------------------------------------- reference model of the Z80 player (for checks)
def chreg(r, ch):                                  # the Z80: channel registers get the channel added
    return r + ch if 0x30 <= r < 0xB8 and not 0xA8 <= r < 0xB0 else r
def simulate(stream, chan, multi, atten=0):
    """-> [(tick, reg, val)] the YM2612 writes the Z80 program makes"""
    T = ftab(); oc = chan; slot = multi; offs = [0] * 4; out = []; i = 0; tk = 0
    def fq(p):
        k = 0
        while p >= 768: p -= 768; k += 1
        k = min(k, 7); f = T[p]; return (k << 3) | (f >> 8), f & 0xFF
    while True:
        b = stream[i]; i += 1
        if b == 0: break
        if b < 0x1D: tk += b; continue
        if b == 0x1D: offs = list(struct.unpack('>4H', stream[i:i + 8])); i += 8; continue
        if b == 0x1E:
            p = struct.unpack('>H', stream[i:i + 2])[0]; i += 2
            hi, lo = fq(p + offs[0]); out += [(tk, 0xA4 + oc, hi), (tk, 0xA0 + oc, lo)]
            if slot:
                for n, (rh, rl) in enumerate(((0xAC, 0xA8), (0xAD, 0xA9), (0xAE, 0xAA))):
                    hi, lo = fq(p + offs[n + 1]); out += [(tk, rh, hi), (tk, rl, lo)]
            continue
        if b == 0x1F: r, v = stream[i], min(127, stream[i + 1] + atten); i += 2; out.append((tk, chreg(r, chan), v)); continue
        v = stream[i]; i += 1
        out.append((tk, chreg(b, chan), v | chan if b == 0x28 else v))
    return out

# ---------------------------------------------------------------- Z80 assembler (just what the player needs)
class Asm:
    def __init__(s, org=0): s.b = bytearray(); s.org = org; s.lab = {}; s.fix = []
    def pc(s): return s.org + len(s.b)
    def L(s, n): s.lab[n] = s.pc()
    def db(s, *x): s.b += bytes(x)
    def w(s, *x):                                  # bytes / ('rel', label) / ('abs', label)
        for v in x:
            if isinstance(v, tuple):
                if v[0] == 'rel': s.fix.append(('rel', len(s.b), v[1], s.pc() + 1)); s.b.append(0)
                else: s.fix.append(('abs', len(s.b), v[1])); s.b += b'\0\0'
            else: s.b.append(v & 0xFF)
    def done(s):
        for f in s.fix:
            a = s.lab[f[2]]
            if f[0] == 'rel':
                d = a - f[3]; assert -128 <= d < 128, ('jr range', f[2]); s.b[f[1]] = d & 0xFF
            else: s.b[f[1]:f[1] + 2] = struct.pack('<H', a)
        return bytes(s.b)
R = lambda n: ('rel', n); A = lambda n: ('abs', n)
JR, JRZ, JRNZ, JRC, JRNC = 0x18, 0x28, 0x20, 0x38, 0x30
CTRL0, CTRL1, ATTEN = 0x1F00, 0x1F20, 0x1F40     # slot controls: +0 state (68000: 1 start), +1 effect, +2 ptr, +4 wait, +7 last channel
BUF0, BUF1 = 0x0900, 0x1400                      #   +5 channel (68000), +6 multi (68000), +8 offsets[4], +16 buffer (the 68000 copies the stream)
TAB = 0x0300
BASE_ATT = 5                                     # carrier TL + 5 (3.75 dB): effects / CD music as in the arcade (md/snd.c fm_vol)
def player():
    a = Asm()
    a.w(0xF3, 0x31, 0x00, 0x20, 0xED, 0x56, 0xC3, A('init'))          # di; ld sp,0x2000; im 1; jp init
    while a.pc() < 0x38: a.db(0)
    a.w(0xFB, 0xED, 0x4D)                                              # 0x38: ei; reti (never enabled)
    a.L('ymw')                                                         # write e to register d (port 0)
    a.w(0x3A, 0x00, 0x40, 0x17, JRC, R('ymw'))                         # ld a,(4000h); rla; jr c,ymw
    a.w(0x7A, 0x32, 0x00, 0x40, 0x7B, 0x32, 0x01, 0x40, 0xC9)          # ld a,d; ld (4000h),a; ld a,e; ld (4001h),a; ret
    a.L('fcalc')                                                       # hl = bc + de -> h = block<<3 | fnum hi, l = fnum lo
    a.w(0x60, 0x69, 0x19, 0xAF)                                        # ld h,b; ld l,c; add hl,de; xor a
    a.w(0x11, 0x00, 0x03)                                              # ld de,768
    a.L('fc1'); a.w(0xB7, 0xED, 0x52, JRC, R('fc2'), 0x3C, JR, R('fc1'))   # or a; sbc hl,de; jr c,fc2; inc a; jr fc1
    a.L('fc2'); a.w(0x19, 0xFE, 0x08, JRC, R('fc3'), 0x3E, 0x07, 0x21, 0xFF, 0x02)   # add hl,de; cp 8; jr c,fc3; ld a,7; ld hl,767
    a.L('fc3'); a.w(0x29, 0x11, TAB & 0xFF, TAB >> 8, 0x19)            # add hl,hl; ld de,TAB; add hl,de
    a.w(0x5E, 0x23, 0x56, 0x07, 0x07, 0x07, 0xB2, 0x67, 0x6B, 0xC9)    # ld e,(hl); inc hl; ld d,(hl); rlca x3; or d; ld h,a; ld l,e; ret
    a.L('init')
    for r, v in ((0x22, 0), (0x2B, 0), (0x27, 0x40), (0x28, 0), (0x28, 1), (0x28, 2), (0x28, 4), (0x28, 5), (0x28, 6),
                 (0x24, 811 >> 2), (0x25, 811 & 3), (0x27, 0x55)):       # LFO / DAC off, ch3 special mode, keys off, timer A 4.0 ms
        a.w(0x16, r, 0x1E, v, 0xCD, A('ymw'))                          # ld d,r; ld e,v; call ymw
    for k in range(4):                                                 # TL 7F on channels 1-3
        for c in (0, 1, 2): a.w(0x16, 0x40 + k * 4 + c, 0x1E, 0x7F, 0xCD, A('ymw'))
    a.L('main')
    a.w(0x3A, 0x00, 0x40, 0xE6, 0x01, JRZ, R('main'))                  # ld a,(4000h); and 1; jr z,main
    a.w(0x16, 0x27, 0x1E, 0x55, 0xCD, A('ymw'))                        # timer A flag reset / reload
    a.w(0xDD, 0x21, CTRL0 & 0xFF, CTRL0 >> 8, 0xCD, A('tick'))         # ld ix,CTRL0; call tick
    a.w(0xDD, 0x21, CTRL1 & 0xFF, CTRL1 >> 8, 0xCD, A('tick'))
    a.w(0xC3, A('main'))
    a.L('tick')
    a.w(0xDD, 0x7E, 0, 0xB7, 0xC8)                                     # ld a,(ix+0); or a; ret z
    a.w(0xFE, 0x01, JRNZ, R('play'))                                   # cp 1; jr nz,play
    a.w(0xDD, 0x7E, 7, 0xDD, 0xBE, 5, JRZ, R('same'))                 # another channel than last time: key the old one off
    a.w(0x5F, 0x16, 0x28, 0xCD, A('ymw'))                              # ld e,a; ld d,28h; call ymw
    a.L('same'); a.w(0xDD, 0x7E, 5, 0xDD, 0x77, 7)                     # last channel = channel
    a.w(0xDD, 0x6E, 16, 0xDD, 0x66, 17, 0xDD, 0x75, 2, 0xDD, 0x74, 3)  # ptr = buffer
    a.w(0xDD, 0x36, 4, 0, 0xDD, 0x36, 0, 2)                            # wait = 0; state = 2
    a.L('play')
    a.w(0xDD, 0x7E, 4, 0xB7, JRZ, R('run'), 0x3D, 0xDD, 0x77, 4, 0xC9)  # if (wait) { wait--; return }
    a.L('run'); a.w(0xDD, 0x6E, 2, 0xDD, 0x66, 3)                      # hl = ptr
    a.L('next')
    a.w(0x7E, 0x23, 0xB7, JRZ, R('end'))                               # ld a,(hl); inc hl; or a; jr z,end
    a.w(0xFE, 0x1D, JRC, R('wait'), JRZ, R('offs'))
    a.w(0xFE, 0x1E, JRZ, R('pitch'), 0xFE, 0x1F, JRZ, R('ctl'))
    a.w(0x57, 0x5E, 0x23)                                              # ld d,a; ld e,(hl); inc hl
    a.w(0xFE, 0x28, JRNZ, R('p1'), 0x7B, 0xDD, 0xB6, 5, 0x5F, JR, R('pw'))   # key on: value | channel
    a.L('p1'); a.w(0xFE, 0x30, JRC, R('pw'), 0xFE, 0xB8, JRNC, R('pw'), 0xFE, 0xA8, JRC, R('padd'), 0xFE, 0xB0, JRC, R('pw'))
    a.L('padd'); a.w(0xDD, 0x86, 5, 0x57)                              # add a,(ix+5); ld d,a
    a.L('pw'); a.w(0xCD, A('ymw'), 0xC3, A('next'))
    a.L('wait'); a.w(0x3D, 0xDD, 0x77, 4, 0xDD, 0x75, 2, 0xDD, 0x74, 3, 0xC9)   # wait = n - 1 (this tick counts); save ptr
    a.L('end'); a.w(0xDD, 0x36, 0, 0, 0xC9)
    a.L('offs')
    for k in range(8): a.w(0x7E, 0xDD, 0x77, 8 + (k ^ 1), 0x23)        # big endian u16 -> (ix+8..15) little endian
    a.w(0xC3, A('next'))
    a.L('ctl')
    a.w(0x7E, 0xDD, 0x86, 5, 0x57, 0x23)                               # ld a,(hl); add a,(ix+5); ld d,a; inc hl
    a.w(0x3A, ATTEN & 0xFF, ATTEN >> 8, 0x86, 0x23)                    # ld a,(ATTEN); add a,(hl); inc hl
    a.w(0xFE, 0x80, JRC, R('ctl1'), 0x3E, 0x7F)                        # cp 128; jr c,ctl1; ld a,7Fh
    a.L('ctl1'); a.w(0x5F, 0xCD, A('ymw'), 0xC3, A('next'))            # ld e,a; call ymw; jp next
    a.L('pitch')
    a.w(0x46, 0x23, 0x4E, 0x23, 0xDD, 0x75, 2, 0xDD, 0x74, 3)          # ld b,(hl); inc hl; ld c,(hl); inc hl; save ptr
    a.w(0xDD, 0x5E, 8, 0xDD, 0x56, 9, 0xCD, A('fcalc'))                # de = offs[0]; call fcalc
    a.w(0xDD, 0x7E, 5, 0xC6, 0xA4, 0x57, 0x5C, 0xCD, A('ymw'))         # reg A4+ch <- h
    a.w(0xDD, 0x7E, 5, 0xC6, 0xA0, 0x57, 0x5D, 0xCD, A('ymw'))         # reg A0+ch <- l
    a.w(0xDD, 0x7E, 6, 0xB7, JRZ, R('pend'))                           # single frequency: done
    for n, (rh, rl) in enumerate(((0xAC, 0xA8), (0xAD, 0xA9), (0xAE, 0xAA))):
        a.w(0xDD, 0x5E, 10 + 2 * n, 0xDD, 0x56, 11 + 2 * n, 0xCD, A('fcalc'))
        a.w(0x16, rh, 0x5C, 0xCD, A('ymw'), 0x16, rl, 0x5D, 0xCD, A('ymw'))
    a.L('pend'); a.w(0xDD, 0x6E, 2, 0xDD, 0x66, 3, 0xC3, A('next'))    # hl = ptr; jp next
    code = a.done()
    assert len(code) <= TAB, len(code)
    img = bytearray(code + bytes(TAB - len(code)))
    for f in ftab(): img += struct.pack('<H', f)
    assert len(img) <= BUF0
    # slot controls (initial values: channel, multi, buffer)
    ctl = bytearray(0x50); ctl[ATTEN - CTRL0] = BASE_ATT
    for base, ch, buf in ((0, 1, BUF0), (0x20, 0, BUF1)):   # arcade channel 6: YM2612 channel 2, 7: channel 1 (3: special mode, shared)
        ctl[base + 5] = ch; ctl[base + 7] = ch; struct.pack_into('<H', ctl, base + 16, buf)
    return bytes(img), bytes(ctl)

EXACT = {'2A', '39', '41', '4F', '50', '53', '54', '56'}   # DT2 matters (channel 3 special mode when it is free)
if __name__ == '__main__':
    P = json.load(open(P1 + '/data/sfx_pri.json'))
    ids = sys.argv[1].split(',') if len(sys.argv) > 1 else sorted(P['ch'], key=lambda x: int(x, 16))
    cut = {(a, b) for a, b in P['cut']}
    assert len(ids) <= 64
    TABN = 12; MASKS = 96 * TABN; IMG = MASKS + 64 * 8
    tab = bytearray(96 * TABN); masks = bytearray(64 * 8)
    for i in range(96): tab[i * TABN + 8] = 0xFF
    img, ctl = player()
    blob_hdr = IMG + 2 + len(img) + 2 + len(ctl)
    streams = bytearray(); sizes = []
    def add(b):
        global streams
        o = blob_hdr + len(streams); streams += b + (b'\0' if len(b) & 1 else b''); return o
    for n, c in enumerate(ids):
        ch, ev = record(c); slot = 1 if ch == 7 else 0
        s0 = convert(ev, 0); s1 = convert(ev, 1) if c in EXACT else b''
        lim = (0x1F00 - BUF1) if slot else (BUF1 - BUF0)
        assert len(s0) <= lim and len(s1) <= lim, ('stream too long', c)
        o0 = add(s0); o1 = add(s1) if s1 else 0
        struct.pack_into('>HHHHBB', tab, (int(c, 16) - 0x20) * TABN, o0, len(s0), o1, len(s1), slot, n)
        struct.pack_into('>Q', masks, n * 8, sum(1 << j for j, a in enumerate(ids) if (a, c) in cut))
        sizes.append(len(s0) + len(s1))
    out = bytes(tab) + bytes(masks) + struct.pack('>H', len(img)) + img + struct.pack('>H', len(ctl)) + ctl
    assert len(out) == blob_hdr
    out += bytes(streams)
    os.makedirs(P1 + '/data/fm', exist_ok=True)
    open(P1 + '/data/fm/fmsfx.bin', 'wb').write(out)
    print('effects %d (%d with a channel 3 version), Z80 image %d bytes, streams %d bytes, fmsfx.bin %d bytes' % (len(ids), len(EXACT & set(ids)), len(img), len(streams), len(out)))
