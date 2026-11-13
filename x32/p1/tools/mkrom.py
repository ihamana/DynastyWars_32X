import sys, struct, os
B, D, OUT = sys.argv[1:4]
rom = bytearray(b'\xff' * 0x400000)
used = []
def put(ofs, data, name):
    assert ofs + len(data) <= len(rom), (name, hex(ofs), len(data))
    for a, b, n in used: assert os.environ.get('OVERLAPOK') or ofs + len(data) <= a or ofs >= b, ('overlap', name, n)
    used.append((ofs, ofs + len(data), name)); rom[ofs:ofs + len(data)] = data
md = open(B + '/md.bin', 'rb').read(); assert len(md) <= 0x6000   # V2.1: pic.bin starts at 0x6000 (tools/mkpic.sh)
put(0, md, 'md')
put(0x8000, open(B + '/sh2.bin', 'rb').read(), 'sh2 lo')
put(0x10000, open(B + '/hot.bin', 'rb').read(), 'hot')
put(0x28000, open(D + '/gfx.bin.sprtab', 'rb').read(), 'sprtab')
import os
s1tab = bytearray(open(D + '/gfx.bin.s1tab', 'rb').read())
ZH = D + '/zh/'
if os.path.exists(ZH + 's1patch.bin'):                   # Chinese (tools/zh/mkzh.py): existing glyph codes redrawn
    b = open(ZH + 's1patch.bin', 'rb').read()
    for i in range(0, len(b), 8):
        k, a = struct.unpack('>II', b[i:i + 8]); struct.pack_into('>I', s1tab, k * 4, a)
put(0x34000, bytes(s1tab), 's1tab')
FM = D + '/fm/fmsfx.bin'                                # V3.5: YM2612 sound effects + the Z80 player (tools/fm/fmsfx.py, md/snd.c FM_ROM)
if os.path.exists(FM):
    fm = open(FM, 'rb').read(); assert len(fm) <= 0x370000 - 0x36B800, 'fmsfx.bin too big'
    put(0x36B800, fm, 'FM effects')
if os.path.exists(ZH + 'zh.bin'): put(0x370000, open(ZH + 'zh.bin', 'rb').read(), 'Chinese glyphs')
g = open(D + '/gfx.bin', 'rb').read(); n = struct.unpack('>I', g[:4])[0]; p = 4
for i in range(n):
    o, l = struct.unpack('>II', g[p:p + 8]); p += 8; put(o, g[p:p + l], 'gfx%d' % i); p += l
g = open(D + '/pic.bin', 'rb').read(); n = struct.unpack('>I', g[:4])[0]; p = 4
for i in range(n):
    o, l = struct.unpack('>II', g[p:p + 8]); p += 8; put(o, g[p:p + l], 'pic_%d' % i); p += l
put(0x1D000, open(D + '/pic.bin.tab', 'rb').read(), 'pictab')
bg = D + '/bg/'            # stage 1 background tiles/maps/palette: on the CD (tools/mksnd.py -> Word RAM)

put(0x3DF100, open(bg + 'st1bga_anim.bin', 'rb').read(), 'bg anim')
put(0x3DF040, open(bg + 'st1bga_next.bin', 'rb').read(), 'bg anim next')

prg = bytearray(open('/tmp/dw/rom/dynwarj_prg.bin', 'rb').read()[:0x100000])
if os.path.exists(ZH + 'prg.bin'):                       # Chinese text in the arcade program
    b = open(ZH + 'prg.bin', 'rb').read(); i = 0
    while i < len(b):
        a, n = struct.unpack('>IH', b[i:i + 6]); prg[a:a + n] = b[i + 6:i + 6 + n]; i += 6 + n + (n & 1)
assert prg[0x52000:0x80000] == b'\xff' * 0x2E000
put(0x100000, prg[:0x52000], 'prg')            # 0x052000-0x07FFFF is unused (0xFF): extra graphics go there
put(0x180000, prg[0x80000:], 'prg hi')
put(0x200000, open(B + '/rom.bin', 'rb').read(), 'cold code')
put(0x152000, open(B + '/rom2.bin', 'rb').read(), 'cold code 2')
put(0x360000, open(B + '/rom3.bin', 'rb').read(), 'cold code 3')   # sh2.ld rom3 (.rom2ui)
import os
if os.path.exists(bg + 's1_wanim.bin'): put(0x330800, open(bg + 's1_wanim.bin', 'rb').read(), 'harbour water (MD)')   # md/bg.c WA_ROM
pt = bytearray(192)                       # bg priority cells (32X), per stage: u32 cart address[48] at 0x3C0000, blobs in free space
blobs = {}
for k in range(32):
    fn = bg + ('st1bgpri.bin' if k == 0 else 's%d_bgpri.bin' % k)
    if os.path.exists(fn): blobs[k] = open(fn, 'rb').read()
for k in range(16):                       # dynamic scroll3 cells (tools/bg4/dyn3.py): table entry 16 + k
    fn = bg + 's%d_dyn3.bin' % k
    if os.path.exists(fn): blobs[16 + k] = open(fn, 'rb').read()
    fn = bg + 's%d_dyn2.bin' % k                # dynamic scroll2 cells (tools/bg4/dyn2.py): entry 32 + k
    if os.path.exists(fn): blobs[32 + k] = open(fn, 'rb').read()
def gaps(lo, hi):                         # free intervals of [lo, hi) not yet used
    out = []; cur = lo
    for a, b, n in sorted(used):
        if b <= cur or a >= hi: continue
        if a > cur: out.append([cur, a])
        cur = max(cur, b)
    if cur < hi: out.append([cur, hi])
    return [[(a + 3) & ~3, b] for a, b in out]
free = gaps(0x3C00C0, 0x3DF040) + gaps(0x3A1A00, 0x3BD200) + [[0x1D7A00, 0x1E0000]] + gaps(0x330800, 0x360000)   # last: unused (0xFF) arcade program ROM
for k, d in sorted(blobs.items(), key=lambda kv: -len(kv[1])):
    for r in free:
        if r[1] - r[0] >= len(d):
            if r[0] < 0x200000: assert rom[r[0]:r[0] + len(d)] == b'\xff' * len(d); rom[r[0]:r[0] + len(d)] = d
            else: put(r[0], d, 'bgpri %d' % k)
            struct.pack_into('>I', pt, k * 4, r[0]); r[0] = (r[0] + len(d) + 3) & ~3; break
    else: sys.exit('bg priority cells do not fit: stage %d' % k)
put(0x3C0000, bytes(pt), 'bg priority table (32X)')
open(OUT, 'wb').write(rom)
print('bgpri free left', [(hex(a), (b - a) // 1024) for a, b in free]); print('rom', OUT, 'free after cold code: %d KB' % ((0x2E0000 - max(b for a, b, n in used if n == 'cold code')) // 1024))
