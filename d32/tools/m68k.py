"""Thin ctypes wrapper around Musashi's disassembler + helpers for 68000 binary analysis."""
import ctypes, re, os

_lib = ctypes.CDLL(os.path.join(os.path.dirname(__file__), 'libdasm.so'))
_lib.dl_setmem.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint]
_lib.dl_dasm.argtypes = [ctypes.c_uint, ctypes.c_char_p]
_lib.dl_dasm.restype = ctypes.c_int


class Image:
    def __init__(self, data, base=0):
        self.data = bytes(data)
        self.base = base
        self._buf = ctypes.create_string_buffer(self.data, len(self.data))
        _lib.dl_setmem(self._buf, len(self.data), base)
        self._cache = {}

    def dasm(self, pc):
        if pc in self._cache:
            return self._cache[pc]
        out = ctypes.create_string_buffer(256)
        n = _lib.dl_dasm(pc, out)
        r = (n, out.value.decode('latin1'))
        self._cache[pc] = r
        return r

    def u16(self, a):
        a -= self.base
        return (self.data[a] << 8) | self.data[a + 1]

    def u32(self, a):
        return (self.u16(a) << 16) | self.u16(a + 2)


# flow classification from mnemonic text
_COND = r'(hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le|hs|lo)'


def flow(text):
    """Return (kind, target_or_None). kind in: 'seq','jmp','call','cond','ret','ijmp','icall','stop','dbcc'"""
    m = text.split(None, 1)
    op = m[0].lower()
    args = m[1] if len(m) > 1 else ''
    def tgt():
        mm = re.match(r'^\$([0-9a-fA-F]+)(\.[wl])?$', args.strip())
        return int(mm.group(1), 16) if mm else None
    if op in ('rts', 'rte', 'rtr'):
        return 'ret', None
    if op == 'stop':
        return 'stop', None
    if op.startswith('bra'):
        return 'jmp', tgt()
    if op.startswith('bsr'):
        return 'call', tgt()
    if re.match(r'^b' + _COND, op):
        return 'cond', tgt()
    if re.match(r'^db', op):
        t = args.split(',')[-1].strip()
        mm = re.match(r'^\$([0-9a-fA-F]+)$', t)
        return 'dbcc', int(mm.group(1), 16) if mm else None
    if op == 'jmp':
        mm = re.match(r'^\$([0-9a-fA-F]+)(\.l|\.w)?$', args.strip())
        if mm:
            return 'jmp', int(mm.group(1), 16)
        mm = re.search(r';\s*\(\$([0-9a-fA-F]+)\)$', args.strip())
        if mm and ',PC)' in args and '(A' not in args.split(';')[0].replace('PC','').upper()[:0]+'':
            if re.match(r'^\(\$[0-9a-fA-F]+,PC\)', args.strip()):
                return 'jmp', int(mm.group(1), 16)
        return 'ijmp', None
    if op == 'jsr':
        mm = re.match(r'^\$([0-9a-fA-F]+)(\.l|\.w)?$', args.strip())
        if mm:
            return 'call', int(mm.group(1), 16)
        mm = re.search(r';\s*\(\$([0-9a-fA-F]+)\)$', args.strip())
        if mm and re.match(r'^\(\$[0-9a-fA-F]+,PC\)', args.strip()):
            return 'call', int(mm.group(1), 16)
        return 'icall', None
    if op in ('illegal',) or op.startswith('dc.w'):
        return 'stop', None
    return 'seq', None
