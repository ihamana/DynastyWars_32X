#!/usr/bin/env python3
"""(V3.0) Put the current data/mdpic/blobK.bin into an existing snd.iso: each stage background blob (CD header
+0x600 + k*8: lba, sectors) keeps its background part, its picture part (blob header +28) is replaced, and the
stage blobs (the last thing on the data track) are laid out again.  (mksnd.py rebuilds everything, sounds included.)
usage: isopic.py in.iso out.iso"""
import sys, struct, os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'mdpic')
iso = bytearray(open(sys.argv[1], 'rb').read())
H = 150 * 2048
st = {}
for k in range(1, 32):
    lba, ns = struct.unpack('>II', iso[H + 0x600 + k * 8:H + 0x600 + k * 8 + 8])
    if lba: st[k] = (lba, ns)
first = min(l for l, n in st.values())
assert max(l + n for l, n in st.values()) * 2048 == len(iso), 'stage blobs are not the end of the track'
out = iso[:first * 2048]; lba = first
for k in sorted(st):
    l, n = st[k]; b = bytearray(iso[l * 2048:(l + n) * 2048])
    assert b[:4] == b'BGST'
    po = struct.unpack('>I', b[28:32])[0]
    base = b[:po] if po else b.rstrip(b'\0')   # (no picture part: the blob ends with padding)
    if not po: base = b[:struct.unpack('>I', b[24:28])[0] + 4096] if False else base
    mp = os.path.join(D, 'blob%d.bin' % k)
    if os.path.exists(mp):
        if not po:
            sys.exit('stage %d has no picture part yet: use mksnd.py' % k)
        base = bytearray(base); base += open(mp, 'rb').read(); base += bytes((-len(base)) & 0xFF)
    assert len(base) <= 0x40000, ('stage %d does not fit in Word RAM' % k, len(base))
    ns2 = (len(base) + 2047) // 2048
    struct.pack_into('>II', out, H + 0x600 + k * 8, lba, ns2)
    out += base + bytes(ns2 * 2048 - len(base)); lba += ns2
    print('stage %d: %d KB (was %d sectors, now %d)' % (k, len(base) // 1024, n, ns2))
open(sys.argv[2], 'wb').write(out)
