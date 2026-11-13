#!/usr/bin/env python3
"""Static recompiler: dynwarj 68000 program -> C (one function, gotos + dispatch switch).
Input: discovered code (d32 work/code.json: pc -> [len, Musashi text]).
Output: gen/prog.c.  Runtime interface (rt.h): registers C.d[8], C.a[8], flags X N Z V C (ints),
memory RB/RW/RL/WB/WW/WL, sr_set(), exc(vector, ret_pc), IDLE yield at 0x64E.
Usage: x68.py CODE_JSON PRG_BIN OUT_C"""
import sys, json, re

code = {int(k): v for k, v in json.load(open(sys.argv[1]))['code'].items()}
J = json.load(open(sys.argv[1]))
PRG = open(sys.argv[2], 'rb').read()
OUT = sys.argv[3]
IDLE_PC = 0x64E

def u16(a): return (PRG[a] << 8) | PRG[a + 1]
def u32(a): return (u16(a) << 16) | u16(a + 2)

def num(s):
    s = s.strip()
    neg = s.startswith('-')
    if neg: s = s[1:]
    v = int(s[1:], 16) if s.startswith('$') else int(s, 0)
    return -v if neg else v

MASK = {'b': '0xFF', 'w': '0xFFFF', 'l': '0xFFFFFFFF'}
SBIT = {'b': 7, 'w': 15, 'l': 31}
CT = {'b': 'u8', 'w': 'u16', 'l': 'u32'}
ST = {'b': 's8', 'w': 's16', 'l': 's32'}
BYTES = {'b': 1, 'w': 2, 'l': 4}

def split_ops(t):
    t = t.split(';')[0].strip()
    parts, depth, cur = [], 0, ''
    for ch in t:
        if ch == '(' : depth += 1
        if ch == ')' : depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): parts.append(cur.strip())
    return parts

class EA:
    """operand: kind in dreg areg imm mem sr ccr usp; for mem: addr C expression (+ pre/post updates)"""
    def __init__(s, kind, n=None, addr=None, pre='', post='', val=None):
        s.kind, s.n, s.addr, s.pre, s.post, s.val = kind, n, addr, pre, post, val

def reg(r):
    return ('C.d[%d]' if r[0] == 'D' else 'C.a[%d]') % int(r[1])

def idx(x):
    m = re.match(r'([DA])([0-7])\.([wl])', x)
    r = reg(m.group(1) + m.group(2))
    return '(u32)(s32)(s16)%s' % r if m.group(3) == 'w' else r

def parse_ea(o, sz, pc, ilen):
    o = o.strip()
    if re.fullmatch(r'D[0-7]', o): return EA('dreg', int(o[1]))
    if re.fullmatch(r'A[0-7]', o): return EA('areg', int(o[1]))
    if o in ('SR', 'CCR', 'USP'): return EA(o.lower())
    if o.startswith('#'): return EA('imm', val=num(o[1:]) & 0xFFFFFFFF)
    inc = 2 if (sz == 'b') else BYTES.get(sz, 4)
    m = re.fullmatch(r'\(A([0-7])\)', o)
    if m: return EA('mem', addr='C.a[%s]' % m.group(1))
    m = re.fullmatch(r'\(A([0-7])\)\+', o)
    if m:
        n = int(m.group(1)); d = inc if (n == 7 and sz == 'b') else BYTES.get(sz, 4)
        return EA('mem', addr='ea%d' % n, pre='u32 ea%d = C.a[%d]; ' % (n, n), post='C.a[%d] += %d; ' % (n, d))
    m = re.fullmatch(r'-\(A([0-7])\)', o)
    if m:
        n = int(m.group(1)); d = inc if (n == 7 and sz == 'b') else BYTES.get(sz, 4)
        return EA('mem', addr='C.a[%d]' % n, pre='C.a[%d] -= %d; ' % (n, d))
    m = re.fullmatch(r'\((-?\$[0-9a-f]+),A([0-7])\)', o)
    if m and m.group(2) == '5' and A5CONST: return EA('mem', addr='A5C(0x%Xu)' % ((0xFFFF8000 + num(m.group(1))) & 0xFFFFFFFF))
    if m: return EA('mem', addr='(C.a[%s] + %d)' % (m.group(2), num(m.group(1))))
    m = re.fullmatch(r'\((?:(-?\$[0-9a-f]+),)?A([0-7]),([DA][0-7]\.[wl])\)', o)
    if m:
        d = num(m.group(1)) if m.group(1) else 0
        return EA('mem', addr='(C.a[%s] + %s + %d)' % (m.group(2), idx(m.group(3)), d))
    m = re.fullmatch(r'\((?:(-?\$[0-9a-f]+),)?PC\)', o)
    if m:
        return EA('mem', addr='0x%X' % ((pc + 2 + (num(m.group(1)) if m.group(1) else 0)) & 0xFFFFFF), val='pcrel')
    m = re.fullmatch(r'\((?:(-?\$[0-9a-f]+),)?PC,([DA][0-7]\.[wl])\)', o)
    if m:
        base = (pc + 2 + (num(m.group(1)) if m.group(1) else 0)) & 0xFFFFFFFF
        return EA('mem', addr='(0x%X + %s)' % (base, idx(m.group(2))), val='pcidx')
    m = re.fullmatch(r'(-?\$[0-9a-f]+)\.w', o)
    if m: return EA('mem', addr='0x%X' % (num(m.group(1)) & 0xFFFF if num(m.group(1)) < 0x8000 else (num(m.group(1)) | 0xFFFF0000) & 0xFFFFFFFF))
    m = re.fullmatch(r'(\$[0-9a-f]+)(\.l)?', o)
    if m: return EA('mem', addr='0x%X' % num(m.group(1)))
    raise ValueError('ea %r at %x' % (o, pc))

def rd(e, sz):
    """C expression for the operand value (zero-extended to sz)"""
    if e.kind == 'dreg': return '(%s)(C.d[%d])' % (CT[sz], e.n) if sz != 'l' else 'C.d[%d]' % e.n
    if e.kind == 'areg': return '(%s)(C.a[%d])' % (CT[sz], e.n) if sz != 'l' else 'C.a[%d]' % e.n
    if e.kind == 'imm': return '0x%X' % (e.val & int(MASK[sz], 16))
    if e.kind == 'mem': return {'b': 'RB', 'w': 'RW', 'l': 'RL'}[sz] + '(%s)' % e.addr
    raise ValueError(e.kind)

def wr(e, sz, v):
    if e.kind == 'dreg':
        if sz == 'l': return 'C.d[%d] = %s;' % (e.n, v)
        return 'C.d[%d] = (C.d[%d] & ~%s) | ((%s) & %s);' % (e.n, e.n, MASK[sz], v, MASK[sz])
    if e.kind == 'areg':
        if sz == 'w': return 'C.a[%d] = (u32)(s32)(s16)(%s);' % (e.n, v)
        return 'C.a[%d] = %s;' % (e.n, v)
    if e.kind == 'mem': return {'b': 'WB', 'w': 'WW', 'l': 'WL'}[sz] + '(%s, %s);' % (e.addr, v)
    raise ValueError(e.kind)

COND = {'t': '1', 'f': '0', 'hi': '(!C.c && !C.z)', 'ls': '(C.c || C.z)', 'cc': '(!C.c)', 'cs': '(C.c)',
        'ne': '(!C.z)', 'eq': '(C.z)', 'vc': '(!C.v)', 'vs': '(C.v)', 'pl': '(!C.n)', 'mi': '(C.n)',
        'ge': '(C.n == C.v)', 'lt': '(C.n != C.v)', 'gt': '(!C.z && C.n == C.v)', 'le': '(C.z || C.n != C.v)'}

labels = set(); disp = set()
A5CONST = 1
import os
sys.path.insert(0, '/dw/d32/tools')
def discover(seeds):
    import m68k
    img = m68k.Image(PRG); work = list(seeds); added = 0
    while work:
        pc = work.pop()
        while pc not in code and 0x400 <= pc < 0x60000:
            n, t = img.dasm(pc)
            if t.startswith('dc.') or 'ILLEGAL' in t.upper(): sys.stderr.write('discover: bad code at %x\n' % pc); break
            code[pc] = [n, t]; added += 1
            m = t.split()[0]; base = m.split('.')[0]
            ops = split_ops(t[len(m):])
            if base in ('bra', 'bsr', 'jsr', 'jmp') or (base.startswith('b') and base[1:] in COND) or base.startswith('db'):
                tg = ops[-1]
                if re.fullmatch(r'\$[0-9a-f]+(\.[wl])?', tg): work.append(num(tg.split('.')[0]))
            if base in ('bra', 'jmp', 'rts', 'rte'): break
            pc += n
    return added
EXTRA = '/dw/x32/tools/extra_seeds.txt'
if os.path.exists(EXTRA):
    sd = [int(l.split()[0], 16) for l in open(EXTRA) if l.strip()]
    sys.stderr.write('discovered %d new insns from %d seeds\n' % (discover(sd), len(sd)))
    EXTRA_SEEDS = set(sd)
else: EXTRA_SEEDS = set()

# ---- known bogus decodes in code.json (data decoded as code; they block the real paths) ----
BOGUS = [int(l.split()[0], 16) for l in open(os.path.join(os.path.dirname(__file__), 'bogus.txt'))] if os.path.exists(os.path.join(os.path.dirname(__file__), 'bogus.txt')) else []
for a in BOGUS:
    if a in code: del code[a]
# ---- executed instructions from reference runs (tools/exec_pcs.txt: "PC flags", flag 1 = reached
# by a jump): every one must exist as an instruction; jump targets become dispatch entries.
EXEC_PCS = {}
_ep = os.path.join(os.path.dirname(__file__), 'exec_pcs.txt')
if os.path.exists(_ep):
    for l in open(_ep):
        a, fl = l.split(); EXEC_PCS[int(a, 16)] = int(fl)
def force_decode(pcs):
    import m68k
    img = m68k.Image(PRG); added = removed = 0
    for pc in sorted(pcs):
        if pc in code: continue
        n, t = img.dasm(pc)
        # drop existing decodes that overlap this executed instruction (they were data)
        for q in range(pc - 8, pc + n):
            if q in code and q != pc and (q < pc < q + code[q][0] or pc < q < pc + n):
                del code[q]; removed += 1
        code[pc] = [n, t]; added += 1
    return added, removed
_a, _r = force_decode(EXEC_PCS)
sys.stderr.write('executed pcs: %d, added %d, removed overlapping %d\n' % (len(EXEC_PCS), _a, _r))
# ---- word jump tables: move.w (d,PC,Dn),Dn ; jmp (d2,PC,Dn.w) -- rescan all entries ----
def interior():
    r = set()
    for a, (n, t) in code.items():
        for k in range(2, n, 2): r.add(a + k)
    return r
def trial(pc, inner):
    import m68k
    img = m68k.Image(PRG)
    for _ in range(400):
        if pc in code: return True
        if pc in inner or not (0x400 <= pc < 0x60000): return False
        n, t = img.dasm(pc)
        if t.startswith('dc.') or 'ILLEGAL' in t.upper(): return False
        if any(pc + k in code for k in range(2, n, 2)): return False
        b = t.split()[0].split('.')[0]
        if b in ('bra', 'jmp', 'rts', 'rte'): return True
        pc += n
    return False
def discover_safe(seeds):
    """add each seed's reachable code only if it decodes cleanly and overlaps nothing"""
    import m68k
    img = m68k.Image(PRG); added = 0; rej = 0
    for sd in seeds:
        inner = interior(); tmp = {}; tin = set(); ok = True; work = [sd]
        while work and ok:
            pc = work.pop()
            while pc not in code and pc not in tmp:
                if not (0x400 <= pc < 0x60000) or pc in inner or pc in tin: ok = False; break
                n, t = img.dasm(pc)
                if t.startswith('dc.') or 'ILLEGAL' in t.upper(): ok = False; break
                if any(pc + k in code or pc + k in tmp for k in range(2, n, 2)): ok = False; break
                tmp[pc] = [n, t]; tin |= {pc + k for k in range(2, n, 2)}
                m = t.split()[0]; base = m.split('.')[0]; ops = split_ops(t[len(m):])
                if base in ('bra', 'bsr', 'jsr', 'jmp') or (base.startswith('b') and base[1:] in COND) or base.startswith('db'):
                    tg = ops[-1]
                    if re.fullmatch(r'\$[0-9a-f]+(\.[wl])?', tg): work.append(num(tg.split('.')[0]))
                if base in ('bra', 'jmp', 'rts', 'rte'): break
                pc += n
            if len(tmp) > 3000: ok = False
        if ok: code.update(tmp); added += len(tmp)
        else:
            rej += 1
            if os.environ.get('DBGREJ'): sys.stderr.write('REJECT seed %x at %x %s\n' % (sd, pc, img.dasm(pc)[1] if 0x400 <= pc < 0x60000 else ''))
    sys.stderr.write('discover_safe: %d insns, %d seeds rejected\n' % (added, rej))
    return added
def scan_jtabs():
    found = set(); inner = interior()
    for pc in sorted(code):
        n, t = code[pc]
        prv = [q for q in range(pc - 8, pc) if q in code and q + code[q][0] == pc]
        if not prv: continue
        m = re.match(r'j(?:mp|sr)\s+\((-?)\$([0-9a-f]+),PC,D(\d)\.w\)', t)
        if m:
            m2 = re.match(r'move\.w\s+\((-?)\$([0-9a-f]+),PC,D(\d)\.w\), D(\d)', code[prv[0]][1])
            if not m2: continue
            T = prv[0] + 2 + int(m2.group(1) + m2.group(2), 16); B = pc + 2 + int(m.group(1) + m.group(2), 16)
        else:
            m = re.match(r'j(mp|sr)\s+\(A(\d),D(\d)\.w\)', t)
            if not m: continue
            p2 = [q for q in range(prv[0] - 8, prv[0]) if q in code and q + code[q][0] == prv[0]]
            if not p2 or not re.match(r'move\.w\s+\(A%s,D\d\.w\)' % m.group(2), code[prv[0]][1]): continue
            m3 = re.match(r'lea\s+\(\$[0-9a-f]+,PC\), A%s; \(\$([0-9a-f]+)\)' % m.group(2), code[p2[0]][1])
            if not m3: continue
            T = B = int(m3.group(1), 16)
        lim = T + 2 * 128; a = T
        while a < lim:
            if a in code or a in inner: break
            off = u16(a); off = off - 0x10000 if off & 0x8000 else off
            tg = B + off
            if tg & 1 or not trial(tg, inner): break
            found.add(tg)
            if tg > a: lim = min(lim, tg)
            a += 2
    return found
JT_TARGETS = set()
for _ in range(8):
    nt = scan_jtabs() - JT_TARGETS
    if not nt: break
    JT_TARGETS |= nt
    sys.stderr.write('jump tables: +%d targets, discovered %d insns\n' % (len(nt), discover_safe(sorted(nt))))
_ov = interior() & set(code)
if _ov: sys.stderr.write('OVERLAP %d: %s\n' % (len(_ov), ' '.join('%x' % a for a in sorted(_ov)[:20])))
INSNS = sorted(code)

def target(t):
    return num(t.split(';')[0].strip()) & 0xFFFFFF

# ---- pass 1: labels and dispatch entries ----
for pc in INSNS:
    n, t = code[pc]
    m = t.split()[0]
    ops = split_ops(t[len(m):])
    if m in ('bra', 'bsr') or (m.startswith('b') and m[1:] in COND) or m.startswith('db'):
        labels.add(target(ops[-1]))
    if m in ('jsr', 'jmp') and re.fullmatch(r'\$[0-9a-f]+(\.l|\.w)?', ops[0]):
        labels.add(num(ops[0].split('.')[0]))
    if m in ('jsr', 'bsr', 'trap'): disp.add(pc + n)
    if m in ('pea', 'lea') and 'PC' in ops[0] and ';' in t:
        disp.add(num(t.split(';')[1].strip().strip('()')))
for e in J.get('wtab', []): disp.add(e[1])
for tab in J.get('ptabs', []):
    for e in (tab if isinstance(tab, list) else []): pass
for v in range(64):
    x = u32(v * 4)
    if x in code: disp.add(x)
# pointer tables: any 32-bit value in the program that equals a code address followed by... -> conservative: all function-like entries
for pc in INSNS:
    n, t = code[pc]
    if t.startswith('rts') or t.startswith('rte') or t.startswith('jmp') or t.startswith('bra'):
        nxt = pc + n
        if nxt in code: disp.add(nxt)          # possible table / function entry after an unconditional end
for a in range(0, len(PRG) - 3, 2):              # code pointers stored in data
    x = u32(a)
    if x in code and 0x400 <= x < 0x60000 and not (a in code): disp.add(x)
for pc in INSNS:
    for h in re.findall(r'#\$([0-9a-f]+)', code[pc][1]):
        v = int(h, 16)
        if v in code and v >= 0x400: disp.add(v)
disp |= {a for a in EXTRA_SEEDS if a in code}
disp |= {a for a in JT_TARGETS if a in code}
disp |= {a for a, fl in EXEC_PCS.items() if fl & 1 and a in code}
disp.add(IDLE_PC)
if os.environ.get('DISPALL'): disp |= set(INSNS)
if os.environ.get('DISPLIST'):
    for l in open(os.environ['DISPLIST']): disp.add(int(l, 16))
labels |= disp
labels |= {0x622, 0x65A, 0x44E38}


# ---- flag liveness (live-out per instruction) ----
ALLF = frozenset('xnzvc')
CUSE = {'t': '', 'f': '', 'ra': '', 'hi': 'cz', 'ls': 'cz', 'cc': 'c', 'cs': 'c', 'ne': 'z', 'eq': 'z', 'vc': 'v', 'vs': 'v',
        'pl': 'n', 'mi': 'n', 'ge': 'nv', 'lt': 'nv', 'gt': 'nvz', 'le': 'nvz'}
def fx(pc):
    n, t = code[pc]; m0 = t.split()[0]; mn, _, sz = m0.partition('.'); ops = split_ops(t[len(m0):])
    nxt = pc + n; ft = [nxt] if nxt in code else []
    use = def_ = ''; succ = ft; dyn = False
    if mn in ('bra',): succ = [target(ops[0])]
    elif mn == 'bsr': succ = [target(ops[0])] + ft
    elif mn.startswith('b') and mn[1:] in CUSE and mn not in ('btst', 'bset', 'bclr', 'bchg'):
        use = CUSE[mn[1:]]; succ = [target(ops[0])] + ft
    elif mn.startswith('db'): use = CUSE[mn[2:]]; succ = [target(ops[1])] + ft
    elif mn.startswith('s') and mn[1:] in CUSE and len(ops) == 1: use = CUSE[mn[1:]]
    elif mn in ('jsr', 'jmp'):
        e = ops[0]
        tg = num(e.split('.')[0]) if re.fullmatch(r'\$[0-9a-f]+(\.[wl])?', e) else None
        if mn == 'jsr': succ = ([tg] if tg in code else []) + ft; dyn = tg not in code
        else: succ = [tg] if tg in code else []; dyn = tg not in code
    elif mn == 'rts': succ = []; dyn = 'ret'
    elif mn == 'rte': succ = []; dyn = False; def_ = 'xnzvc'
    elif mn == 'trap': succ = ft; dyn = False   # handler saves SR, rte restores it
    elif mn in ('move', 'movea') and (ops and (ops[-1] in ('SR', 'CCR') )): def_ = 'xnzvc'
    elif mn in ('move',) and ops and ops[0] == 'SR': use = 'xnzvc'
    elif mn in ('move', 'moveq', 'tst', 'clr', 'not', 'ext', 'swap', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'divu', 'divs', 'mulu', 'muls'):
        if ops and ops[-1] in ('SR', 'CCR'): use = def_ = 'xnzvc'
        else: def_ = 'nzvc'
    elif mn in ('add', 'addi', 'sub', 'subi', 'neg', 'addq', 'subq'):
        if ops and re.fullmatch(r'A[0-7]', ops[-1]): pass
        else: def_ = 'xnzvc'
    elif mn in ('cmp', 'cmpi', 'cmpa'): def_ = 'nzvc'
    elif mn in ('lsl', 'lsr', 'asl', 'asr', 'rol', 'ror'): def_ = 'xnzvc' if (len(ops) == 1 or ops[0].startswith('#')) and mn[1] != 'o' else 'nzvc'
    elif mn in ('roxl', 'roxr'): use = 'x'; def_ = 'xnzvc'
    elif mn in ('btst', 'bset', 'bclr', 'bchg'): def_ = 'z'
    elif mn in ('abcd', 'sbcd'): use = 'xz'; def_ = 'xnzvc'
    return set(use), set(def_), succ, dyn
FX = {pc: fx(pc) for pc in INSNS}
RETPTS = [pc + code[pc][0] for pc in INSNS if code[pc][1].split()[0] in ('jsr', 'bsr')]
LIN = {pc: set() for pc in INSNS}
LIVE = {pc: set() for pc in INSNS}
changed = True; it = 0
while changed:
    changed = False; it += 1
    dynlive = set(); retlive = set()
    for a in disp:
        if a in LIN: dynlive |= LIN[a]
    for a in RETPTS:
        if a in LIN: retlive |= LIN[a]
    for pc in reversed(INSNS):
        use, df, succ, dyn = FX[pc]
        lo = set(retlive) if dyn == 'ret' else set(dynlive) if dyn else set()
        for q in succ:
            if q in LIN: lo |= LIN[q]
            else: lo |= ALLF
        li = use | (lo - df)
        if lo != LIVE[pc] or li != LIN[pc]: LIVE[pc] = lo; LIN[pc] = li; changed = True
CURPC = 0
import collections
if os.environ.get('DBGY'):
    for a in INSNS:
        for q in FX[a][2]:
            if q not in LIN: sys.stderr.write('NOSUCC %x %s -> %x\n' % (a, code[a][1], q))
    for r in RETPTS:
        if 'x' not in LIN.get(r, ()): continue
        a = r; seen = set(); path = []
        while a not in seen:
            seen.add(a); path.append(a)
            if 'x' in FX[a][0]: sys.stderr.write('REAL %x -> %s\n' % (r, ' '.join('%x' % q for q in path[-6:]))); break
            nx = [q for q in FX[a][2] if 'x' in LIN.get(q, ())]
            if not nx: break
            a = nx[0]
if os.environ.get('DBGX'):
    a = int(os.environ['DBGX'], 16)
    for _ in range(40):
        sys.stderr.write('TRX %x %-36s in=%s out=%s succ=%s dyn=%s\n' % (a, code[a][1], ''.join(sorted(LIN[a])), ''.join(sorted(LIVE[a])), [hex(q) for q in FX[a][2]], FX[a][3]))
        nx = [q for q in FX[a][2] if 'x' in LIN.get(q, ())]
        if not nx: break
        a = nx[0]
    for a in INSNS:
        if 'x' in FX[a][0]:
            sys.stderr.write('USEX %x %s | prev %s\n' % (a, code[a][1], [code[q][1] for q in range(a-8,a) if q in code and q+code[q][0]==a]))
    for a in RETPTS:
        if a in LIN and 'x' in LIN[a]: sys.stderr.write('RETX %x %s\n' % (a, code[a][1]))
sys.stderr.write('liveness: %d iters, dyn live %s, ret live %s, flag sets %s\n' % (it, ''.join(sorted(dynlive)), ''.join(sorted(retlive)), collections.Counter(len(v) for v in LIVE.values()).most_common(6)))

# ---- pass 2: emit ----
out = []
W = out.append

def flags_logic(sz, r):
    lv = LIVE.get(CURPC, ALLF); o = ''
    if 'n' in lv: o += 'C.n = ((%s) >> %d) & 1; ' % (r, SBIT[sz])
    if 'z' in lv: o += 'C.z = !((%s) & %s); ' % (r, MASK[sz])
    if 'v' in lv: o += 'C.v = 0; '
    if 'c' in lv: o += 'C.c = 0; '
    return o
def gen_arith(kind, s, d, r, sz, setx):
    lv = LIVE.get(CURPC, ALLF); sb = SBIT[sz]; m = MASK[sz]; o = ''
    if 'n' in lv: o += 'C.n = (%s >> %d) & 1; ' % (r, sb)
    if 'z' in lv: o += 'C.z = !(%s & %s); ' % (r, m)
    if 'v' in lv:
        if kind == 'add': o += 'C.v = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1; ' % (s, r, d, r, sb)
        else: o += 'C.v = (((%s ^ %s) & (%s ^ %s)) >> %d) & 1; ' % (s, d, r, d, sb)
    if 'c' in lv or (setx and 'x' in lv):
        if kind == 'add': cx = '(%s < %s)' % (r, d) if sz == 'l' else '((((%s & %s) + (%s & %s)) >> %d) & 1)' % (s, m, d, m, sb + 1)
        else: cx = '((%s & %s) > (%s & %s))' % (s, m, d, m)
        if 'c' in lv: o += 'C.c = %s; ' % cx
        if setx and 'x' in lv: o += 'C.x = C.c; ' if 'c' in lv else 'C.x = %s; ' % cx
    return o

def emit(pc, n, t):
    m0 = t.split()[0]
    ops = split_ops(t[len(m0):])
    mn, _, sz = m0.partition('.')
    nxt = pc + n
    L = []
    def E(s): L.append(s)
    if mn in ('move', 'movea') and sz:
        if ops[1] in ('SR', 'CCR', 'USP') or ops[0] in ('SR', 'CCR', 'USP'): mn = 'movesr'
        else:
            s = parse_ea(ops[0], sz, pc, n); d = parse_ea(ops[1], sz, pc, n)
            E(s.pre); E('u32 v = %s;' % rd(s, sz)); E(s.post); E(d.pre)
            if mn == 'move': E(flags_logic(sz, 'v'))
            E(wr(d, sz, 'v')); E(d.post)
            return L
    if mn == 'move' and not sz: mn = 'movesr'
    if mn == 'movesr':
        a, b = ops
        if b == 'SR':
            s = parse_ea(a, 'w', pc, n); E(s.pre); E('sr_set(%s);' % rd(s, 'w')); E(s.post)
        elif b == 'CCR':
            s = parse_ea(a, 'w', pc, n); E(s.pre); E('ccr_set(%s);' % rd(s, 'w')); E(s.post)
        elif b == 'USP': E('C.usp = %s;' % reg(a))
        elif a == 'USP': E('%s = C.usp;' % reg(b))
        elif a == 'SR':
            d = parse_ea(b, 'w', pc, n); E(d.pre); E(wr(d, 'w', 'sr_get()')); E(d.post)
        else: raise ValueError(t)
        return L
    if mn == 'moveq':
        v = num(ops[0][1:]) & 0xFFFFFFFF
        if v & 0x80: v |= 0xFFFFFF00
        E('C.d[%d] = 0x%X;' % (int(ops[1][1]), v)); E(flags_logic('l', 'C.d[%d]' % int(ops[1][1])))
        return L
    if mn in ('lea', 'pea'):
        s = parse_ea(ops[0], 'l', pc, n)
        if mn == 'lea': E('C.a[%d] = %s;' % (int(ops[1][1]), s.addr))
        else: E('{ u32 v = %s; C.a[7] -= 4; WL(C.a[7], v); }' % s.addr)
        return L
    if mn in ('clr', 'tst', 'not', 'neg', 'ext', 'swap', 'nbcd') or (mn.startswith('s') and mn[1:] in COND and len(ops) == 1):
        if mn == 'swap':
            r = 'C.d[%d]' % int(ops[0][1]); E('%s = (%s >> 16) | (%s << 16);' % (r, r, r)); E(flags_logic('l', r)); return L
        if mn == 'ext':
            r = int(ops[0][1])
            if sz == 'w': E('C.d[%d] = (C.d[%d] & 0xFFFF0000) | (u16)(s16)(s8)C.d[%d];' % (r, r, r)); E(flags_logic('w', 'C.d[%d]' % r))
            else: E('C.d[%d] = (u32)(s32)(s16)C.d[%d];' % (r, r)); E(flags_logic('l', 'C.d[%d]' % r))
            return L
        d = parse_ea(ops[0], sz or 'b', pc, n); sz = sz or 'b'
        E(d.pre)
        if d.kind == 'mem': E('u32 ea = %s;' % d.addr); d = EA('mem', addr='ea')
        if mn == 'clr': E(wr(d, sz, '0')); E('C.n = 0; C.z = 1; C.v = 0; C.c = 0;')
        elif mn == 'tst': E('u32 v = %s;' % rd(d, sz)); E(flags_logic(sz, 'v'))
        elif mn == 'not': E('u32 v = ~%s;' % rd(d, sz)); E(wr(d, sz, 'v')); E(flags_logic(sz, 'v'))
        elif mn == 'neg':
            E('u32 s = %s, r = 0 - s, z0 = 0;' % rd(d, sz)); E(wr(d, sz, 'r')); E(gen_arith('sub', 's', 'z0', 'r', sz, 1))
        else:   # scc
            E(wr(d, 'b', '(%s) ? 0xFF : 0' % COND[mn[1:]]))
        E(d.post)
        return L
    if mn in ('add', 'adda', 'addi', 'addq', 'sub', 'suba', 'subi', 'subq', 'cmp', 'cmpa', 'cmpi',
              'and', 'andi', 'or', 'ori', 'eor', 'eori'):
        base = mn.rstrip('aiq') if mn not in ('and', 'andi', 'or', 'ori', 'eor', 'eori') else mn.rstrip('i')
        if mn in ('cmpa', 'adda', 'suba'): base = mn[:-1]
        if mn == 'addq' or mn == 'subq': base = mn[:-1]
        if ops[1] in ('CCR', 'SR'):
            v = num(ops[0][1:]); op = {'and': '&', 'or': '|', 'eor': '^'}[base]
            if ops[1] == 'CCR': E('ccr_set(sr_get() %s 0x%X);' % (op, v))
            else: E('sr_set(sr_get() %s 0x%X);' % (op, v))
            return L
        s = parse_ea(ops[0], sz, pc, n); d = parse_ea(ops[1], sz, pc, n)
        E(s.pre); E('u32 s = %s;' % rd(s, sz)); E(s.post); E(d.pre)
        if d.kind == 'mem': E('u32 ea = %s;' % d.addr); dd = EA('mem', addr='ea')
        else: dd = d
        if d.kind == 'areg':           # adda/suba/cmpa/addq/subq An: 32-bit, no flags (cmpa sets flags)
            sv = '(u32)(s32)(s16)s' if sz == 'w' else 's'
            if base == 'cmp': E('{ u32 sv = %s, dv = C.a[%d], r = dv - sv; %s }' % (sv, d.n, gen_arith('sub', 'sv', 'dv', 'r', 'l', 0)))
            else: E('C.a[%d] %s= %s;' % (d.n, '+' if base == 'add' else '-', sv))
            E(d.post); return L
        E('u32 dv = %s;' % rd(dd, sz))
        if base in ('add', 'sub', 'cmp'):
            E('u32 r = dv %s s;' % ('+' if base == 'add' else '-'))
            if base != 'cmp': E(wr(dd, sz, 'r'))
            E(gen_arith('add' if base == 'add' else 'sub', 's', 'dv', 'r', sz, base != 'cmp'))
        else:
            E('u32 r = dv %s s;' % {'and': '&', 'or': '|', 'eor': '^'}[base]); E(wr(dd, sz, 'r')); E(flags_logic(sz, 'r'))
        E(d.post)
        return L
    if mn in ('abcd', 'sbcd'):
        s = parse_ea(ops[0], 'b', pc, n); d = parse_ea(ops[1], 'b', pc, n)
        E(s.pre); E('u32 s = %s;' % rd(s, 'b')); E(s.post); E(d.pre)
        if d.kind == 'mem': E('u32 ea = %s;' % d.addr); d = EA('mem', addr='ea')
        E('u32 r = %s(%s, s);' % ('bcd_add' if mn == 'abcd' else 'bcd_sub', rd(d, 'b'))); E(wr(d, 'b', 'r')); E(d.post)
        return L
    if mn in ('lsl', 'lsr', 'asl', 'asr', 'rol', 'ror', 'roxl', 'roxr'):
        if len(ops) == 1: cnt, d = '1', parse_ea(ops[0], 'w', pc, n); sz = sz or 'w'
        else:
            d = parse_ea(ops[1], sz, pc, n)
            cnt = '%d' % num(ops[0][1:]) if ops[0].startswith('#') else '(%s & 63)' % reg(ops[0])
        E(d.pre)
        if d.kind == 'mem': E('u32 ea = %s;' % d.addr); d = EA('mem', addr='ea')
        E('u32 r = sh_%s(%s, %s, %d);' % (mn, rd(d, sz), cnt, SBIT[sz])); E(wr(d, sz, 'r')); E(d.post)
        return L
    if mn in ('btst', 'bset', 'bclr', 'bchg'):
        d = parse_ea(ops[1], 'b', pc, n)
        bit = ('%d' % num(ops[0][1:])) if ops[0].startswith('#') else reg(ops[0])
        E(d.pre)
        if d.kind == 'dreg':
            E('u32 b = 1u << (%s & 31); u32 v = C.d[%d]; C.z = !(v & b);' % (bit, d.n))
            if mn != 'btst': E('C.d[%d] = v %s;' % (d.n, {'bset': '| b', 'bclr': '& ~b', 'bchg': '^ b'}[mn]))
        else:
            E('u32 ea = %s; u32 b = 1u << (%s & 7); u32 v = RB(ea); C.z = !(v & b);' % (d.addr, bit))
            if mn != 'btst': E('WB(ea, v %s);' % {'bset': '| b', 'bclr': '& ~b', 'bchg': '^ b'}[mn])
        E(d.post)
        return L
    if mn == 'movem':
        regs, ea = (ops[0], ops[1]) if re.match(r'[DA][0-7]', ops[0]) and '(' in ops[1] or ops[1].startswith('-(') else (ops[1], ops[0])
        tomem = re.match(r'[DA][0-7]', ops[0]) is not None
        rl = []
        for part in regs.split('/'):
            a, _, b = part.partition('-')
            if b:
                k = a[0]
                for i in range(int(a[1]), int(b[1]) + 1): rl.append(k + str(i))
            else: rl.append(a)
        step = BYTES[sz]
        if tomem and ea.startswith('-('):
            an = int(ea[3])
            if sz == 'l' and len(rl) >= 3 and 'A%d' % an not in rl:     # V3.6: one WRAM check for the whole block
                E('{ u32 e0 = C.a[%d] - %d; if (ISWRAM(e0) && (e0 & 0xFFFF) <= 0x%X) { u8 *p = WRP(e0); %s C.a[%d] = e0; } else {' % (
                    an, 4 * len(rl), 0x10000 - 4 * len(rl), ' '.join('MVW(p + %d, %s >> 16); MVW(p + %d, %s);' % (8 * 0 + 4 * i, reg(r), 4 * i + 2, reg(r)) for i, r in enumerate(rl)), an))
                for r in reversed(rl): E('C.a[%d] -= %d; WL(C.a[%d], %s);' % (an, step, an, reg(r)))
                E('} }')
                return L
            for r in reversed(rl): E('C.a[%d] -= %d; %s(C.a[%d], %s);' % (an, step, 'WL' if sz == 'l' else 'WW', an, reg(r)))
            return L
        e = parse_ea(ea, sz, pc, n)
        if ea.endswith(')+'):
            an = int(ea[2])
            if sz == 'l' and len(rl) >= 3 and 'A%d' % an not in rl:
                E('{ u32 e0 = C.a[%d]; if (ISWRAM(e0) && (e0 & 0xFFFF) <= 0x%X) { const u8 *p = WRP(e0); %s C.a[%d] = e0 + %d; } else {' % (
                    an, 0x10000 - 4 * len(rl), ' '.join('%s = (MVR(p + %d) << 16) | MVR(p + %d);' % (reg(r), 4 * i, 4 * i + 2) for i, r in enumerate(rl)), an, 4 * len(rl)))
                for r in rl: E('%s = RL(C.a[%d]); C.a[%d] += %d;' % (reg(r), an, an, step))
                E('} }')
                return L
            for r in rl:
                v = 'RL(C.a[%d])' % an if sz == 'l' else '(u32)(s32)(s16)RW(C.a[%d])' % an
                E('%s = %s; C.a[%d] += %d;' % (reg(r), v, an, step))
            return L
        E('u32 ea = %s;' % e.addr)
        for i, r in enumerate(rl):
            if tomem: E('%s(ea + %d, %s);' % ('WL' if sz == 'l' else 'WW', i * step, reg(r)))
            else: E('%s = %s;' % (reg(r), 'RL(ea + %d)' % (i * step) if sz == 'l' else '(u32)(s32)(s16)RW(ea + %d)' % (i * step)))
        return L
    if mn == 'exg':
        E('{ u32 t = %s; %s = %s; %s = t; }' % (reg(ops[0]), reg(ops[0]), reg(ops[1]), reg(ops[1]))); return L
    if mn == 'link':
        an = int(ops[0][1]); E('C.a[7] -= 4; WL(C.a[7], C.a[%d]); C.a[%d] = C.a[7]; C.a[7] += %d;' % (an, an, num(ops[1][1:]))); return L
    if mn == 'unlk':
        an = int(ops[0][1]); E('C.a[7] = C.a[%d]; C.a[%d] = RL(C.a[7]); C.a[7] += 4;' % (an, an)); return L
    if mn in ('divu', 'divs', 'mulu', 'muls'):
        s = parse_ea(ops[0], 'w', pc, n); dn = int(ops[1][1])
        E(s.pre); E('u32 s = %s;' % rd(s, 'w')); E(s.post)
        E('%s(%d, s);' % (mn, dn)); return L
    if mn == 'nop': return L
    if mn == 'bra': E('goto L_%06X;' % target(ops[0])); return L
    if mn == 'bsr': E('C.a[7] -= 4; WL(C.a[7], 0x%X); NCALL(0x%X,0x%X);' % (nxt, target(ops[0]), nxt)); return L
    if mn.startswith('b') and mn[1:] in COND:
        E('if %s goto L_%06X;' % (COND[mn[1:]], target(ops[0]))); return L
    if mn.startswith('db'):
        r = int(ops[0][1]); c = mn[2:]
        cc = COND['f' if c == 'ra' else c]
        E('if (!%s) { u16 w = (u16)C.d[%d] - 1; C.d[%d] = (C.d[%d] & 0xFFFF0000) | w; if (w != 0xFFFF) goto L_%06X; }' % (cc, r, r, r, target(ops[1])))
        return L
    if mn in ('jsr', 'jmp'):
        e = parse_ea(ops[0], 'l', pc, n)
        if mn == 'jsr': E('C.a[7] -= 4; WL(C.a[7], 0x%X);' % nxt)
        if e.addr.startswith('0x') and int(e.addr, 16) in labels:
            E(('NCALL(0x%X,0x%X);' % (int(e.addr, 16), nxt)) if mn == 'jsr' else 'goto L_%06X;' % int(e.addr, 16))
        elif mn == 'jsr': E('pc = %s; NCALLD(0x%X);' % (e.addr, nxt))
        else: E('pc = %s; goto dispatch;' % e.addr)
        return L
    if mn == 'rts': E('pc = RL(C.a[7]); C.a[7] += 4; goto dispatch;'); return L
    if mn == 'rte': E('{ u32 s = RW(C.a[7]); pc = RL(C.a[7] + 2); C.a[7] += 6; sr_set(s); } goto dispatch;'); return L
    if mn == 'trap': E('pc = exc(%d, 0x%X); goto dispatch;' % (32 + num(ops[0][1:]), nxt)); return L
    raise ValueError('unhandled %r at %x' % (t, pc))

def insn_lines(pc):
    global CURPC
    n, t = code[pc]; L = []
    if pc == IDLE_PC:
        L.append('L_%06X: if (!C.ran) { C.pc = 0x%X; IDLE_RET; }\nR_%06X:' % (pc, pc, pc))
    elif pc == 0x622: L.append('L_000622: C.ran = 0;')
    elif pc == 0x65A: L.append('L_00065A: if ((C.a[0] & 0xFFFF) != 0x80F4) C.ran = 1;')
    elif pc == 0x44E38: L.append('L_044E38: C.ran = 1;')
    elif pc in labels: L.append('L_%06X:' % pc)
    if pc in NAT:       # V3.6 native routine (gen/nat.c nat_sw): runs in place of the code here, then continues at one of its exits
        L.append('  { /* %06X native */ TR(0x%X); extern u32 nat_%X(void); u32 n = nat_%X(); if (n) { NATLD %s pc = n; goto dispatch; } }'
                 % (pc, pc, pc, pc, ' '.join('if (n == 0x%X) goto L_%06X;' % (c, c) for c in NAT[pc])))
    CURPC = pc
    try: body = emit(pc, n, t)
    except Exception as ex:
        sys.stderr.write('ERR %x %s: %s\n' % (pc, t, ex)); body = ['pc = 0x%X; goto bad;' % pc]
    L.append('  { /* %06X %s */ TR(0x%X); %s }' % (pc, t.replace('*/', '* /'), pc, ' '.join(x for x in body if x)))
    nxt = pc + n
    if t.split()[0].split('.')[0] not in ends_flow and nxt not in code:
        L.append('  pc = 0x%X; goto dispatch;' % nxt)
    return L
ends_flow = ('bra', 'jmp', 'rts', 'rte', 'trap')
HDR = '/* generated by x68.py - do not edit */\n#include "rt.h"\nu32 nat_sw(u32 pc);\n'
NAT = {}
if not os.environ.get('SPLIT'):
    W(HDR + '#define IDLE_RET return\n')
    W('void cpu_run(void)\n{\n    u32 pc = C.pc;\n    if (C.irq && (C.irq > ((sr_get() >> 8) & 7))) { pc = exc(24 + C.irq, pc); C.irq = 0; }\n    goto dispatch;\n')
    for pc in INSNS: out.extend(insn_lines(pc))
    W('dispatch:\n    DLOG(pc);\n    switch (pc & 0xFFFFFF) {')
    for a in sorted(disp):
        if a in code: W('    case 0x%X: goto %s_%06X;' % (a, 'R' if a == IDLE_PC else 'L', a))
    W('    }\nbad:\n    cpu_bad(pc);\n    C.pc = pc;\n}\n')
    txt = '\n'.join(out)
    txt = re.sub(r'NCALL\(0x([0-9A-F]+),0x[0-9A-F]+\);', lambda m: 'goto L_%06X;' % int(m.group(1), 16), txt)
    txt = re.sub(r'NCALLD\(0x[0-9A-F]+\);', 'goto dispatch;', txt)
    open(OUT, 'w').write(txt)
else:
    # regions: contiguous runs of instructions, cut at dispatch entries once big enough
    RMIN = int(os.environ.get("RMIN", "300")); NFILE = int(os.environ.get("NFILE", "16"))
    HOT = set()
    if os.environ.get('HOTLIST'):
        hm = int(os.environ.get('HOTMIN', '1'))
        for l in open(os.environ['HOTLIST']):
            a, c = l.split()
            if int(c) >= hm: HOT.add(int(a, 16))
    hf = [pc in HOT for pc in INSNS]
    GAP = int(os.environ.get('HOTGAP', '6')); i = 0
    while i < len(INSNS):               # fill short cold gaps between hot instructions
        if not hf[i]:
            j = i
            while j < len(INSNS) and not hf[j]: j += 1
            if 0 < i and j < len(INSNS) and j - i <= GAP and INSNS[j] - INSNS[i - 1] < 0x80:
                for q in range(i, j): hf[q] = True
            i = j
        else: i += 1
    hotof = dict(zip(INSNS, hf))
    regs = []; rhot = []
    hl = [pc for pc in INSNS if hotof[pc]]
    if hl: regs.append(hl); rhot.append(True)
    cur = []
    for pc in INSNS:
        if hotof[pc]:
            if cur: regs.append(cur); rhot.append(False); cur = []
            continue
        if cur and ((len(cur) >= RMIN and pc in disp) or pc - (cur[-1] + code[cur[-1]][0]) > 0x400):
            regs.append(cur); rhot.append(False); cur = []
        cur.append(pc)
    if cur: regs.append(cur); rhot.append(False)
    for r in regs: labels.add(r[0])
    rof = {}
    for ri, r in enumerate(regs):
        for pc in r: rof[pc] = ri
    # cross-region targets need cases
    gre = re.compile(r'goto L_([0-9A-F]{6});')
    bodies = []; entries = [set() for _ in regs]
    # V3.6: native routines (gen/nat.c nat_sw): every jump to a native entry leaves the region (return to cpu_run,
    # which tries nat_sw first); the places a native continues at become region entries
    if os.environ.get('NATLIST'):
        for l in open(os.environ['NATLIST']):
            f = l.split('#')[0].split()
            if f: NAT[int(f[0], 16)] = [int(x, 16) for x in f[1:]]
    NESTED = os.environ.get('NESTED', '1') == '1'
    NESTMIN = int(os.environ.get('NESTMIN', '100000')); NW = {}       # only the call sites run that often (weights file)
    if os.environ.get('NESTW'):
        for l in open(os.environ['NESTW']):
            f = l.split(); NW[int(f[0], 16)] = int(f[1])
    DW = {}
    if os.environ.get('DISPW'):
        for l in open(os.environ['DISPW']):
            f = l.split(); DW[int(f[0], 16)] = int(f[1])
    for a, cs in NAT.items():
        for c in cs:
            if c in rof: labels.add(c); entries[rof[c]].add(c)
    for r in regs:      # (V3.6) fall-through targets in other regions need their labels before any body is written
        for k, pc in enumerate(r):
            ln, lt = code[pc]; nx = r[k + 1] if k + 1 < len(r) else None
            if lt.split()[0].split('.')[0] not in ends_flow and pc + ln in code and nx != pc + ln: labels.add(pc + ln)
    for ri, r in enumerate(regs):
        L = []
        for k, pc in enumerate(r):
            L.extend(insn_lines(pc))
            ln, lt = code[pc]; nx = r[k + 1] if k + 1 < len(r) else None
            if lt.split()[0].split('.')[0] not in ends_flow and pc + ln in code and nx != pc + ln:
                labels.add(pc + ln); L.append('  goto L_%06X;' % (pc + ln))
        txt = '\n'.join(L)
        # V3.6 nested calls: a jsr/bsr into another region calls that region's function (r_xxx) and, when it comes
        # back with the return address of this call (the rts), goes straight on here: no trip through cpu_run
        # and no dispatch.  Any other pc (task switch, deeper unwinding) is passed up as before.  NCD bounds the C depth.
        def ncall(m):
            t, r = int(m.group(1), 16), int(m.group(2), 16)
            if rof.get(t) == ri: return 'goto L_%06X;' % t
            if NESTED and rof.get(r) == ri and t in rof and NW.get(r - 2, 0) + NW.get(r - 4, 0) + NW.get(r - 6, 0) >= NESTMIN:
                entries[rof[t]].add(t); labels.add(r)
                return 'NEST(r_%03d, 0x%X, 0x%X, L_%06X)' % (rof[t], t, r, r)
            return 'goto L_%06X;' % t
        def ncalld(m):
            r = int(m.group(1), 16)
            if NESTED and rof.get(r) == ri and NW.get(r - 2, 0) + NW.get(r - 4, 0) + NW.get(r - 6, 0) >= NESTMIN: labels.add(r); return 'NESTD(%d, 0x%X, L_%06X)' % (ri, r, r)
            return 'goto dispatch;'
        txt = re.sub(r'NCALL\(0x([0-9A-F]+),0x([0-9A-F]+)\);', ncall, txt)
        txt = re.sub(r'NCALLD\(0x([0-9A-F]+)\);', ncalld, txt)
        def rep(m):
            a = int(m.group(1), 16)
            if rof.get(a) == ri: return m.group(0)
            entries[rof[a]].add(a); return 'return 0x%X;' % a
        txt = gre.sub(rep, txt)
        bodies.append(txt)
    for a in disp:
        if a in rof: entries[rof[a]].add(a)
    # V2.8: the hot region keeps the 68000 registers in locals (SH-2 registers / stack slots, not reloaded after
    # every byte store as the global C would be); C.d / C.a are written back wherever control leaves the function
    # and around the few helpers that use C's registers themselves
    RE_R = re.compile(r'C\.([da])\[(\d)\]')
    RE_F = re.compile(r'C\.([xnzvc])\b')
    HELP = re.compile(r'nat_[0-9A-F]+\(|sr_set\(|exc\(|divu\(|divs\(|mulu\(|muls\(|C\.usp|IDLE_RET|sh_[a-z]+\(|bcd_[a-z]+\(|fl_[a-z]+\(|sr_get\(|ccr_set\(')
    def localize(body):
        LS = os.environ.get('LOCSET', 'd0,a7,a0,a6,d1,a1,fx,fn,fz,fv,fc').split(',')
        used = sorted((set(m.group(1) + m.group(2) for m in RE_R.finditer(body)) | set('f' + m.group(1) for m in RE_F.finditer(body))) & set(LS))
        def cn(r): return 'C.%s' % r[1] if r[0] == 'f' else 'C.%s[%s]' % (r[0], r[1])
        sv = ' '.join('%s = %s;' % (cn(r), r) for r in used)
        ld = ' '.join('%s = %s;' % (r, cn(r)) for r in used)
        out = []
        for line in body.split('\n'):
            if HELP.search(line):
                line = line.replace('goto dispatch;', ld + ' goto dispatch;').replace('NATLD', ld)
                if 'IDLE_RET' in line: line = line.replace('IDLE_RET;', sv + ' IDLE_RET;')
                else:
                    i = line.find('*/ TR(')
                    j = line.find(';', i) + 1
                    line = line[:j] + ' ' + sv + line[j:]
                    k = line.rfind('}')
                    if 'goto dispatch' not in line: line = line[:k] + ld + ' ' + line[k:]
                out.append(line); continue
            line = RE_R.sub(lambda m: m.group(1) + m.group(2) if m.group(1) + m.group(2) in used else m.group(0), line)
            line = RE_F.sub(lambda m: 'f' + m.group(1) if 'f' + m.group(1) in used else m.group(0), line)
            line = re.sub(r'return (0x[0-9A-F]+);', lambda m: '{ ' + sv + ' return ' + m.group(1) + '; }', line)
            out.append(line)
        return '\n'.join(out), sv, '    ' + ' '.join('#%s = %s;' % (r, cn(r)) for r in used) + '\n'
    import os.path
    od = OUT; os.makedirs(od, exist_ok=True)
    cold = [ri for ri in range(len(regs)) if not rhot[ri]]
    hot = [ri for ri in range(len(regs)) if rhot[ri]]
    open(os.path.join(od, 'regions.txt'), 'w').write(''.join('r_%03d %06X %06X %s\n' % (ri, r[0], r[-1], 'hot' if rhot[ri] else 'cold') for ri, r in enumerate(regs)))
    # V3.6: WARMLIST (68000 pcs): the cold regions holding them go into warm.c, compiled like the hot code (-O2, helpers inlined) but in the cart
    warm = set()
    if os.environ.get('WARMLIST'):
        for l in open(os.environ['WARMLIST']):
            f = l.split('#')[0].split()
            if f and int(f[0], 16) in rof and not rhot[rof[int(f[0], 16)]]: warm.add(rof[int(f[0], 16)])
    cold = [ri for ri in cold if ri not in warm]
    per = (len(cold) + NFILE - 1) // NFILE
    groups = [('p%02d.c' % f, cold[f * per:(f + 1) * per]) for f in range(NFILE)]
    if warm: groups.append(('warm.c', sorted(warm)))
    if hot: groups.append(('hot.c', hot))
    sys.stderr.write('hot regions %d, hot insns %d\n' % (len(hot), sum(len(regs[r]) for r in hot)))
    for fname, rl in groups:
        o = [HDR + '#define IDLE_RET return 0xFFFFFFFF\n' + ''.join('u32 r_%03d(u32);' % k for k in range(len(regs))) + '\n']
        for ri in rl:
            body = bodies[ri]; sv = ld = ''
            if rhot[ri] and os.environ.get('LOCREG', '0') == '1':
                body, sv, ld = localize(body)
            o.append('u32 r_%03d(u32 pc)\n{\n    int first = 1;\n%s    goto dispatch;%s' % (ri, ld.replace('#', 'u32 ') if ld else '', '\nhbase: ;' if rhot[ri] else ''))
            o.append(body.replace('NATLD ', ''))
            if rhot[ri] and os.environ.get('HOTHASH', '1') == '1':
                # V3.6: the hot region dispatches by an open-addressing hash table (pc -> label offset, computed goto)
                # instead of the switch's binary search
                ents = sorted(entries[ri]); NS = int(os.environ.get('HOTHASHN', '512'))
                while NS < len(ents) * 3 // 2: NS *= 2
                W = {}                                  # most executed entries first: they get their home slots
                if os.environ.get('HASHW'):
                    for l in open(os.environ['HASHW']):
                        f = l.split(); W[int(f[0], 16)] = int(f[1])
                ents.sort(key=lambda a: -W.get(a, 0))
                best = None
                for sh in [(s1, s2) for s1 in (1, 2, 3) for s2 in range(s1 + 1, 20)]:
                    tab = [0] * NS; probes = 0
                    for a in ents:
                        h = ((a >> sh[0]) ^ (a >> sh[1])) & (NS - 1)
                        while tab[h]: h = (h + 1) & (NS - 1); probes += 1
                        tab[h] = a
                    cost = 0                            # weighted probes
                    for i, t in enumerate(tab):
                        if t:
                            h0 = ((t >> sh[0]) ^ (t >> sh[1])) & (NS - 1); cost += ((i - h0) & (NS - 1)) * (W.get(t, 0) + 1)
                    if best is None or cost < best[0]: best = (cost, sh, tab)
                probes, sh, tab = best
                sys.stderr.write('hot hash: %d entries, %d slots, shift %s, extra probes %d\n' % (len(ents), NS, sh, probes))
                lab = lambda a: '%s_%06X' % ('R' if a == IDLE_PC else 'L', a)
                o.append('dispatch:\n    DLOG(pc);\n    { static const u32 hk[%d] = {%s};' % (NS, ','.join('0x%X' % t for t in tab)))
                o.append('      static const short ho[%d] = {%s};' % (NS, ','.join(('&&%s - &&hbase' % lab(t)) if t else '0' for t in tab)))
                o.append('      u32 p = pc & 0xFFFFFF, h = ((p >> %d) ^ (p >> %d)) & %d;' % (sh[0], sh[1], NS - 1))
                o.append('      for (;;) { u32 k = hk[h]; if (k == p) { first = 0; goto *(&&hbase + ho[h]); } if (!k) break; h = (h + 1) & %d; } }' % (NS - 1))
                o.append('    %s\n    if (!first) return pc;\n    return pc | 0x80000000u;\nbad:\n    %s cpu_bad(pc); C.pc = pc; return 0xFFFFFFFF;\n}\n' % (sv, sv))
                continue
            fast = ''
            if DW:              # V3.6: the region's most dispatched entries are tested before the switch's binary search
                tw = sum(DW.get(a, 0) for a in entries[ri])
                top = sorted((a for a in entries[ri] if DW.get(a, 0) * 100 >= tw * int(os.environ.get('FASTPCT', '12')) and DW.get(a, 0) > 0), key=lambda a: -DW[a])[:3]
                fast = ''.join(' if (p == 0x%X) { first = 0; goto %s_%06X; }' % (a, 'R' if a == IDLE_PC else 'L', a) for a in top)
                if fast: fast = '\n    { u32 p = pc & 0xFFFFFF;%s }' % fast
            o.append('dispatch:\n    DLOG(pc);%s\n    switch (pc & 0xFFFFFF) {' % fast)
            for a in sorted(entries[ri]):
                o.append('    case 0x%X: first = 0; goto %s_%06X;' % (a, 'R' if a == IDLE_PC else 'L', a))
            if rhot[ri]: o.append('    }\n    %s\n    if (!first) return pc;\n    return pc | 0x80000000u;\nbad:\n    %s cpu_bad(pc); C.pc = pc; return 0xFFFFFFFF;\n}\n' % (sv, sv))
            else: o.append('    }\n    if (!first) return pc;\nbad:\n    cpu_bad(pc); C.pc = pc; return 0xFFFFFFFF;\n}\n')
        open(os.path.join(od, fname), 'w').write('\n'.join(o))
    o = [HDR, 'typedef u32 (*rfn)(u32);']
    o += ['u32 r_%03d(u32);' % ri for ri in range(len(regs))]
    cl = [ri for ri in range(len(regs)) if not rhot[ri]]
    o.append('static const u32 rstart[%d] = {%s};' % (len(cl) + 1, ','.join('0x%X' % regs[ri][0] for ri in cl) + ',0x1000000'))
    o.append('static const rfn rtab[%d] = {%s};' % (len(cl), ','.join('r_%03d' % ri for ri in cl)))
    RM = 0x600; rm = []
    for b in range(RM):
        a = b << 8; k = 0
        while k + 1 < len(cl) and regs[cl[k + 1]][0] <= a: k += 1
        if k + 1 < len(cl) and regs[cl[k + 1]][0] < a + 0x100: k |= 0x4000     # a region starts inside the block
        rm.append(k)
    # V3.6: bit 15 = the 256-byte block has an entry of the hot region (only then is r_000 tried first)
    hasHot = bool(rhot and rhot[0])
    if hasHot:
        assert regs.index(regs[0]) == 0
        for a in entries[0]:
            assert (a >> 8) < RM, hex(a)
            rm[a >> 8] |= 0x8000
    o.append('static const unsigned short rmap[%d] = {%s};' % (RM, ','.join(map(str, rm))))
    hotcall = ('if (m & 0x8000) {\n            pc = r_000(pc);\n            if (pc == 0xFFFFFFFF) return;\n            if (!(pc & 0x80000000u)) continue;\n        }') if hasHot else ''
    assert len(regs) < 256
    o.append('unsigned char ncd;')
    o.append('static const unsigned char creg[%d] = {%s};' % (len(cl), ','.join(str(ri) for ri in cl)))
    o.append('''u32 rcall(u32 a, int self)
{
    u32 b = a >> 8; u32 m = rmap[b < %d ? b : %d];
    %s
    int i = m & 0x3FFF;
    if (m & 0x4000) while (rstart[i + 1] <= a) i++;
    if (creg[i] == self) return 0xFFFFFFFEu;
    ncd++; u32 r = rtab[i](a); ncd--;
    return r;
}''' % (RM, RM - 1, ('if (m & 0x8000) { if (self == 0) return 0xFFFFFFFEu; ncd++; u32 r = r_000(a); ncd--; if (!(r & 0x80000000u) || r == 0xFFFFFFFF) return r; }' if hasHot else '')))
    o.append('''void cpu_run(void)
{
    u32 pc = C.pc; ncd = 0;
    if (C.irq && (C.irq > ((sr_get() >> 8) & 7))) { pc = exc(24 + C.irq, pc); C.irq = 0; }
    for (;;) {
        u32 a = pc & 0xFFFFFF; u32 b = a >> 8; u32 m = rmap[b < %d ? b : %d];
        %s
        int i = m & 0x3FFF;
        if (m & 0x4000) while (rstart[i + 1] <= a) i++;
        RCOUNT(i); pc = rtab[i](a);
        if (pc == 0xFFFFFFFF) return;
    }
}''' % (RM, RM - 1, hotcall))
    open(os.path.join(od, 'run.c'), 'w').write('\n'.join(o) + '\n')
    sys.stderr.write('regions %d, files %d, entries %d\n' % (len(regs), NFILE, sum(len(e) for e in entries)))
sys.stderr.write('insns %d labels %d dispatch %d\n' % (len(INSNS), len(labels), len(disp)))
