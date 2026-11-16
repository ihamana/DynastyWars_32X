#!/usr/bin/env python3
"""Option hooks put straight into the translated arcade code (gen/split, idempotent; run by build.sh):
  0x11F74 / 0x11F90  addq.b #1,($60,A6)  charge step   -> += chg_step (1..3)
  0x1782C            item experience read into D2/D3   -> multiplied (exp_item(), BCD, capped at 9999 total)
Done inside the game's own code so the game's thresholds, sounds and level-up checks all still apply
(v0.x changed the player object after the frame, which could leave it in states the game never makes)."""
import sys
G = '/dw/x32/gen/split/'
P = [
 ('p00.c', '{ /* 0020C0 moveq   #$20, D0 */ TR(0x20C0); C.d[0] = 0x20;',
           '{ /* 0020C0 moveq   #$20, D0 */ TR(0x20C0); C.d[0] = 0x40; /*PATCHED: opening story columns twice as slow (V1.10)*/'),
 ('p04.c', '{ /* 011F74 addq.b  #1, ($60,A6) */ TR(0x11F74); u32 s = 0x1;',
           '{ /* 011F74 addq.b  #1, ($60,A6) */ TR(0x11F74); extern unsigned char chg_step; u32 s = chg_step; /*PATCHED*/'),
 ('p04.c', '{ /* 011F90 addq.b  #1, ($60,A6) */ TR(0x11F90); u32 s = 0x1;',
           '{ /* 011F90 addq.b  #1, ($60,A6) */ TR(0x11F90); extern unsigned char chg_step; u32 s = chg_step; /*PATCHED*/'),
 ('p05.c', '{ /* 01782C move.b  ($68,A6), D3 */ TR(0x1782C); u32 v = RB((C.a[6] + 104)); C.d[3] = (C.d[3] & ~0xFF) | ((v) & 0xFF); }',
           '{ /* 01782C move.b  ($68,A6), D3 */ TR(0x1782C); u32 v = RB((C.a[6] + 104)); C.d[3] = (C.d[3] & ~0xFF) | ((v) & 0xFF); }\n'
           '  { extern unsigned exp_item(unsigned item, unsigned have); /*PATCHED*/\n'
           '    unsigned b = exp_item(((C.d[3] & 0xFF) << 8) | (C.d[2] & 0xFF), (RB(C.a[4] + 104) << 8) | RB(C.a[4] + 105));\n'
           '    C.d[2] = (C.d[2] & ~0xFF) | (b & 0xFF); C.d[3] = (C.d[3] & ~0xFF) | ((b >> 8) & 0xFF); }'),
]
# V3.4: 24x24 Chinese blocks (codes 0x9000-0x9FFF, tools/zh/mkzh.py): the opening story (0x20B8) and the screen messages (0x450E4 / 0x451A8 / 0x4520C)
P += [
  (None, '{ /* 0020D6 move.w  D0, (A0) */ TR(0x20D6); u32 v = (u16)(C.d[0]); WW(C.a[0], v); }',
   '{ /* 0020D6 move.w  D0, (A0) */ TR(0x20D6); u32 v = (u16)(C.d[0]); WW(C.a[0], v); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-) */ u32 t = (u16)C.d[0]; if ((t & 0xF000) == 0x9000) { u32 a = C.a[0]; WW(a + 0x100, t + 2); WW(a + 0x100 + 2, C.d[1]);WW(a + 0x104, t + 0x12); WW(a + 0x104 + 2, C.d[1]);WW(a + 0x108, t + 0x22); WW(a + 0x108 + 2, C.d[1]);WW(a + 8, t + 0x20); WW(a + 8 + 2, C.d[1]);WW(a + 0x88, t + 0x21); WW(a + 0x88 + 2, C.d[1]); } }\n'),
  (None, '{ /* 0020FC lea     ($8,A0), A0 */ TR(0x20FC); C.a[0] = (C.a[0] + 8); }',
   '{ /* 0020FC lea     ($8,A0), A0 */ TR(0x20FC); C.a[0] = (C.a[0] + (((C.d[0] & 0xF000) == 0x9000) ? 16 : 8)); /*PATCHED V3.4: 24x24 story glyphs 32 px apart*/ }'),
  (None, '{ /* 045118 add.w   D2, D0 */ TR(0x45118); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }',
   '{ /* 045118 add.w   D2, D0 */ TR(0x45118); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-, word = top-left + 0x10) */ u32 t = (u16)C.d[0] - 0x10; if (((t + 0x10) & 0xF000) == 0x9000) { u32 a = C.a[1]; WW(a + 0xFC, t + 2); WW(a + 0xFC + 2, C.d[1]);WW(a + 0x100, t + 0x12); WW(a + 0x100 + 2, C.d[1]);WW(a + 0x104, t + 0x22); WW(a + 0x104 + 2, C.d[1]);WW(a + 4, t + 0x20); WW(a + 4 + 2, C.d[1]);WW(a + 0x84, t + 0x21); WW(a + 0x84 + 2, C.d[1]); } }\n'),
  (None, '{ /* 0451DC add.w   D2, D0 */ TR(0x451DC); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }',
   '{ /* 0451DC add.w   D2, D0 */ TR(0x451DC); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-, word = top-left + 0x10) */ u32 t = (u16)C.d[0] - 0x10; if (((t + 0x10) & 0xF000) == 0x9000) { u32 a = C.a[1]; WW(a + 0xFC, t + 2); WW(a + 0xFC + 2, C.d[1]);WW(a + 0x100, t + 0x12); WW(a + 0x100 + 2, C.d[1]);WW(a + 0x104, t + 0x22); WW(a + 0x104 + 2, C.d[1]);WW(a + 4, t + 0x20); WW(a + 4 + 2, C.d[1]);WW(a + 0x84, t + 0x21); WW(a + 0x84 + 2, C.d[1]); } }\n'),
  (None, '{ /* 045240 add.w   D2, D0 */ TR(0x45240); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }',
   '{ /* 045240 add.w   D2, D0 */ TR(0x45240); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-, word = top-left + 0x10) */ u32 t = (u16)C.d[0] - 0x10; if (((t + 0x10) & 0xF000) == 0x9000) { u32 a = C.a[1]; WW(a + 0xFC, t + 2); WW(a + 0xFC + 2, C.d[1]);WW(a + 0x100, t + 0x12); WW(a + 0x100 + 2, C.d[1]);WW(a + 0x104, t + 0x22); WW(a + 0x104 + 2, C.d[1]);WW(a + 4, t + 0x20); WW(a + 4 + 2, C.d[1]);WW(a + 0x84, t + 0x21); WW(a + 0x84 + 2, C.d[1]); } }\n'),
  (None, '{ /* 045140 lea     ($100,A1), A1 */ TR(0x45140); C.a[1] = (C.a[1] + 256); }',
   '{ /* 045140 lea     ($100,A1), A1 */ TR(0x45140); C.a[1] = (C.a[1] + (((C.d[0] & 0xF000) == 0x9000) ? 384 : 256)); /*PATCHED V3.4: 24x24 blocks*/ }'),
  (None, '{ /* 045204 lea     ($100,A1), A1 */ TR(0x45204); C.a[1] = (C.a[1] + 256); }',
   '{ /* 045204 lea     ($100,A1), A1 */ TR(0x45204); C.a[1] = (C.a[1] + (((C.d[0] & 0xF000) == 0x9000) ? 384 : 256)); /*PATCHED V3.4: 24x24 blocks*/ }'),
  (None, '{ /* 045268 lea     ($180,A1), A1 */ TR(0x45268); C.a[1] = (C.a[1] + 384); }',
   '{ /* 045268 lea     ($180,A1), A1 */ TR(0x45268); C.a[1] = (C.a[1] + (((C.d[0] & 0xF000) == 0x9000) ? 384 : 384)); /*PATCHED V3.4: 24x24 blocks*/ }'),
]
# V3.4: the 0x2670 strings (oath, 20 px) and their erase (0x2804)
P += [
  (None, '{ /* 00271A add.w   D2, D0 */ TR(0x271A); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }',
   '{ /* 00271A add.w   D2, D0 */ TR(0x271A); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-, byte = top-left + 0x10) */ u32 t = (u16)C.d[0] - 0x10; if (((t + 0x10) & 0xF000) == 0x9000) { u32 a = C.a[1]; WW(a + 0xFC, t + 2); WW(a + 0xFC + 2, C.d[1]); WW(a + 0xFC + 0x2000, t + 2); WW(a + 0xFC + 0x2002, C.d[1]);WW(a + 0x100, t + 0x12); WW(a + 0x100 + 2, C.d[1]); WW(a + 0x100 + 0x2000, t + 0x12); WW(a + 0x100 + 0x2002, C.d[1]);WW(a + 0x104, t + 0x22); WW(a + 0x104 + 2, C.d[1]); WW(a + 0x104 + 0x2000, t + 0x22); WW(a + 0x104 + 0x2002, C.d[1]);WW(a + 4, t + 0x20); WW(a + 4 + 2, C.d[1]); WW(a + 4 + 0x2000, t + 0x20); WW(a + 4 + 0x2002, C.d[1]);WW(a + 0x84, t + 0x21); WW(a + 0x84 + 2, C.d[1]); WW(a + 0x84 + 0x2000, t + 0x21); WW(a + 0x84 + 0x2002, C.d[1]); } }\n'),
  (None, '{ /* 002776 add.w   D2, D0 */ TR(0x2776); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }',
   '{ /* 002776 add.w   D2, D0 */ TR(0x2776); u32 s = (u16)(C.d[2]); u32 dv = (u16)(C.d[0]); u32 r = dv + s; C.d[0] = (C.d[0] & ~0xFFFF) | ((r) & 0xFFFF); }\n  { /*PATCHED V3.4: 24x24 blocks (codes 0x9000-, byte = top-left + 0x10) */ u32 t = (u16)C.d[0] - 0x10; if (((t + 0x10) & 0xF000) == 0x9000) { u32 a = C.a[1]; WW(a + 0xFC, t + 2); WW(a + 0xFC + 2, C.d[1]);WW(a + 0x100, t + 0x12); WW(a + 0x100 + 2, C.d[1]);WW(a + 0x104, t + 0x22); WW(a + 0x104 + 2, C.d[1]);WW(a + 4, t + 0x20); WW(a + 4 + 2, C.d[1]);WW(a + 0x84, t + 0x21); WW(a + 0x84 + 2, C.d[1]); } }\n'),
  (None, '{ /* 002762 lea     ($100,A1), A1 */ TR(0x2762); C.a[1] = (C.a[1] + 256); }',
   '{ /* 002762 lea     ($100,A1), A1 */ TR(0x2762); C.a[1] = (C.a[1] + (((C.d[0] & 0xF000) == 0x9000) ? 384 : 256)); /*PATCHED V3.4: 24x24 blocks*/ }'),
  (None, '{ /* 00279E lea     ($100,A1), A1 */ TR(0x279E); C.a[1] = (C.a[1] + 256); }',
   '{ /* 00279E lea     ($100,A1), A1 */ TR(0x279E); C.a[1] = (C.a[1] + (((C.d[0] & 0xF000) == 0x9000) ? 384 : 256)); /*PATCHED V3.4: 24x24 blocks*/ }'),
  (None, '{ /* 00285C move.w  #$6020, D0 */ TR(0x285C); u32 v = 0x6020; C.d[0] = (C.d[0] & ~0xFFFF) | ((v) & 0xFFFF); }',
   '{ /* 00285C move.w  #$6020, D0 */ TR(0x285C); u32 v = 0x6020; C.d[0] = (C.d[0] & ~0xFFFF) | ((v) & 0xFFFF); }\n  { /*PATCHED V3.4: erase 24x24 blocks (page >= 0x30) */ if (((C.d[2] & 0xFF) >= 0x30)) { u32 a = C.a[1]; WW(a + 0xFC, 0x6020); WW(a + 0xFC + 2, C.d[1]); WW(a + 0x100, 0x6020); WW(a + 0x100 + 2, C.d[1]); WW(a + 0x104, 0x6020); WW(a + 0x104 + 2, C.d[1]); WW(a + 4, 0x6020); WW(a + 4 + 2, C.d[1]); WW(a + 0x84, 0x6020); WW(a + 0x84 + 2, C.d[1]); } }\n'),
  (None, '{ /* 00287E lea     ($100,A1), A1 */ TR(0x287E); C.a[1] = (C.a[1] + 256); }',
   '{ /* 00287E lea     ($100,A1), A1 */ TR(0x287E); C.a[1] = (C.a[1] + (((C.d[2] & 0xFF) >= 0x30) ? 384 : 256)); /*PATCHED V3.4: 24x24 blocks*/ }'),
]
import glob, os
files = sorted(glob.glob(G + '*.c'))
# V3.11: the cutscene subtitle clear (0x45F60: rows 24-27 of columns 8-71) also clears rows 23 and 28, which the 24x24
# blocks of the translated lines reach (lines at y 24 / 27 = rows 23-25 / 26-28): the last lines' bottoms stayed
P += [
  (None, '{ /* 045F60 lea     $900460.l, A0 */ TR(0x45F60); C.a[0] = 0x900460; }',
   '{ /* 045F60 lea     $900460.l, A0 */ TR(0x45F60); C.a[0] = 0x90045C; /*PATCHED V3.11: from row 23*/ }'),
  (None, '{ /* 045F78 lea     ($70,A0), A0 */ TR(0x45F78); C.a[0] = (C.a[0] + 112); }',
   '{ /* 045F78 lea     ($70,A0), A0 */ TR(0x45F78); WL(C.a[0], C.d[0]); WL(C.a[0] + 4, C.d[0]); C.a[0] = (C.a[0] + 8 + 0x68); /*PATCHED V3.11: rows 27-28 too*/ }'),
]

for _, old, new in P:                     # the file the instruction is in depends on the hot list: search them all
    hit = 0
    for f in files:
        s = open(f).read()
        if new in s: hit += 1; continue
        n = s.count(old)
        if n:
            assert n == 1, (f, old[:40]); open(f, 'w').write(s.replace(old, new)); hit += 1
            print('patched', os.path.basename(f), old[6:12])
    assert hit == 1, ('patch target', old[:40], hit)

# V3.8: full-screen attack option (ui_opt[7], gen/nat.c fsa_*): the player's attack hit tests (0xA2CC: 0xA430 / 0xA5D4 /
# 0xA678 / 0xA730) take every on-screen target regardless of lane and distance, and go on with the next target after a hit
# (the game's own hit code runs for each; its two return-address pops are skipped and the registers restored at its rts)
import re
FSA = [(0xA440, 'lane'), (0xA5DC, 'lane'), (0xA68C, 'lane'), (0xA740, 'lane'),
       (0xA456, 'range'), (0xA5F0, 'range'), (0xA6A0, 'range'), (0xA756, 'range'),
       (0xA4D6, 'pop'), (0xA4DA, 'pop'), (0xA5F2, 'pop'), (0xA5F6, 'pop'), (0xA6A2, 'pop'), (0xA6A6, 'pop'), (0xA758, 'pop'), (0xA75C, 'pop'),
       (0xA50C, 'rts'), (0xA61C, 'rts'), (0xA6CC, 'rts'), (0xA7C2, 'rts')]
for pc, kind in FSA:
    hit = 0
    for f in files:
        s = open(f).read()
        m = re.search(r'  \{ /\* %06X [^\n]*\*/ TR\(0x%X\); ([^\n]*) \}\n' % (pc, pc), s)
        if not m: continue
        hit += 1
        if '/*FSA*/' in m.group(0): continue
        body = m.group(1)
        if kind == 'lane': new = '{ extern int fsa_lane(void); int k = fsa_lane(); if (k) C.z = (k == 1); } /*FSA*/ ' + body
        elif kind == 'range': new = '{ extern int fsa_range(void); int k = fsa_range(); if (k) C.c = (k == 2); } /*FSA*/ ' + body
        elif kind == 'pop': new = '{ extern unsigned char fsa_saved; if (!fsa_saved) { ' + body + ' } } /*FSA*/'
        else: new = '{ extern void fsa_restore(void); fsa_restore(); } /*FSA*/ ' + body
        line = m.group(0).replace(body, new, 1)
        open(f, 'w').write(s.replace(m.group(0), line)); print('patched', os.path.basename(f), '%06X' % pc, kind)
    assert hit == 1, ('fsa target', hex(pc), hit)

# V3.19: invincible option on every BCD subtraction from memory (sbcd -(Ay),-(Ax): troops, the object life counters):
# a hit on a player's troops is not written and the flags say "not zero, no borrow" (the game's own death branch after it
# is then not taken), ui.c inv_bcd().  V2.6 only put the troops back after the frame, too late when one hit took them all.
pat = re.compile(r"TR\((0x[0-9A-F]+)\);( C\.a\[\d\] -= 1;  u32 s = RB\(C\.a\[\d\]\); C\.a\[\d\] -= 1;  u32 ea = C\.a\[\d\];) u32 r = bcd_sub\(RB\(ea\), s\); WB\(ea, r\); \}")
nb = 0
for f in files:
    s = open(f).read()
    s2, n = pat.subn(lambda m: 'TR(%s);%s u32 r = bcd_sub(RB(ea), s); { extern int inv_bcd(unsigned, unsigned); int k_ = inv_bcd(ea, %s); if (k_ == 1) { C.c = C.x = 0; C.z = 0; } else WB(ea, k_ ? 0 : r); } /*INV*/ }' % (m.group(1), m.group(2), m.group(1)), s)
    if n: open(f, 'w').write(s2); nb += n
    nb += s.count('/*INV*/')
print('invincible sbcd sites', nb)
assert nb == 15, nb
