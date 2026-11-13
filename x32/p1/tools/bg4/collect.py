# collect visible scroll2/scroll3 cells (code, attr) for the harbour from host snapshots
import sys, glob, os, pickle, struct
sys.path.insert(0,'/dw/dw/tools'); import cps
out=sys.argv[1]; files=sorted(glob.glob(sys.argv[2]+'/snap_*.bin'))
try: S=pickle.load(open(out,'rb'))
except Exception: S={'s2':{}, 's3':{}, 'frames':0}
for f in files:
    d=open(f,'rb').read(); g=d[:0x30000]; A=struct.unpack('<32H',d[0x30000:0x30040])
    w=d[0x30000+128+0x800+0x3000:][:0x10000]
    K=int(os.environ.get("STAGE","1"))
    if w[0x81D7]!=K or w[0x81D8]!=K: continue
    S['frames']+=1
    b2=(A[2]*256)&0x3c000; sx,sy=A[8],A[9]; r0=(sy+16)//16; c0=(sx+64)//16
    for r in range(r0,r0+12):
        for c in range(c0,c0+25):
            rr=r&63; o=b2+((rr&15)+((c&63)<<4)+((rr&0x30)<<6))*4
            k=((g[o]<<8)|g[o+1], (g[o+2]<<8)|g[o+3])
            S['s2'].setdefault(k,set()).add((c,r-32))
    b3=(A[3]*256)&0x3c000; sx,sy=A[10],A[11]; r0=(sy+16)//32; c0=(sx+64)//32
    for r in range(r0,r0+7):
        for c in range(c0,c0+13):
            o=b3+((r&7)+((c&63)<<3)+((r&0x38)<<6))*4
            k=((g[o]<<8)|g[o+1], (g[o+2]<<8)|g[o+3])
            S['s3'].setdefault(k,set()).add((c,r-48))
    if len(sys.argv)>3: os.remove(f)
pickle.dump(S,open(out,'wb'))
print(S['frames'], len(S['s2']), len(S['s3']))
