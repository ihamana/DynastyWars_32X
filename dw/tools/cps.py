"""CPS1 (dynwarj) data access: gfx decode, bank mapping, palettes from cpsref snapshots,
stage map extraction, sprite animation frames."""
import numpy as np, struct, os
ROM=os.environ.get('DWROM','/tmp/dw/rom')
PRG=open(os.path.join(ROM,'dynwarj_prg.bin'),'rb').read()
_G=np.fromfile(os.path.join(ROM,'gfx.bin'),dtype=np.uint8)
# groups of 4 bytes -> 8 pens  (pen bit k from byte k, MSB = leftmost)
_b=_G.reshape(-1,4)
_bits=np.unpackbits(_b[:,:,None],axis=2)          # (N,4,8) MSB first
GROUPS=(_bits[:,0,:]|(_bits[:,1,:]<<1)|(_bits[:,2,:]<<2)|(_bits[:,3,:]<<3)).astype(np.uint8)  # (N,8)
del _bits,_b
def w16(a): return (PRG[a]<<8)|PRG[a+1]
def s16(a): v=w16(a); return v-0x10000 if v&0x8000 else v
def l32(a): return struct.unpack('>I',PRG[a:a+4])[0]
def bank(layer,code):
    sh={0:1,1:0,2:1,3:3}[layer]; c=code<<sh
    for t,s,e,b in ((0,0,0x3fff,0),(0,0x4000,0x5fff,1),(1,0x6000,0x7fff,1),(3,0,0x3fff,2),(2,0x4000,0x7fff,3)):
        if t==layer and s<=c<=e: return (b*0x4000+(c&0x3fff))>>sh
    return -1
_cache={}
def tile(layer,code):
    """pens array for a tile (16x16 for sprites/scroll2, 32x32 scroll3, 8x8 scroll1). None if unmapped."""
    k=(layer,code)
    if k in _cache: return _cache[k]
    c=bank(layer,code)
    if c<0: _cache[k]=None; return None
    if layer in (0,2):
        g=GROUPS[c*32:c*32+32].reshape(16,2,8).reshape(16,16)
    elif layer==3:
        g=GROUPS[c*128:c*128+128].reshape(32,4,8).reshape(32,32)
    else:
        g=GROUPS[c*16:c*16+16].reshape(8,2,8)[:,0,:]   # caller selects half via code parity in map
    _cache[k]=g; return g
def tile8_s1(code,half):
    c=bank(1,code); g=GROUPS[c*16:c*16+16].reshape(8,2,8)
    return g[:,half,:]
# ---------- snapshots ----------
def load_snap(fn):
    d=open(fn,'rb').read()
    g=d[:0x30000]; A=struct.unpack('<32H',d[0x30000:0x30040]); B=struct.unpack('<32H',d[0x30040:0x30080])
    obj=np.frombuffer(d[0x30080:0x30880],dtype='<u2')
    pal=np.frombuffer(d[0x30880:0x30880+0xc00*4],dtype='<u4')
    rgb=np.stack([(pal>>16)&255,(pal>>8)&255,pal&255],axis=1).astype(np.uint8)
    return dict(g=g,A=A,B=B,obj=obj,rgb=rgb)
# ---------- stage maps ----------
S2TAB=0x6fa8; S3TAB=0x7148
def scene_map(scene,layer,ncols=32):
    """returns (codes,attrs) arrays [rows][cols] in tile units for the whole scene (3 blocks tall)."""
    tab=S2TAB if layer==2 else S3TAB
    bm=l32(tab+scene*8); bd=l32(tab+scene*8+4)
    per=16 if layer==2 else 8; bsz=per*per*4
    H=3*per; W=ncols*per
    codes=np.zeros((H,W),np.uint16); attrs=np.zeros((H,W),np.uint16); blk=np.zeros((3,ncols),np.int32)
    for bx in range(ncols):
        for k in range(3):                     # k=0 bottom row (A2), 1 above, 2 top
            a=bm+2*bx-0x40*k
            w=w16(a); blk[2-k,bx]=w
            base=bd+w*bsz
            for cx in range(per):
                for cy in range(per):
                    o=base+cy*per*4+cx*4
                    codes[(2-k)*per+cy,bx*per+cx]=w16(o); attrs[(2-k)*per+cy,bx*per+cx]=w16(o+2)
    if layer==3: codes&=0x3fff
    return codes,attrs,blk
def render_map(codes,attrs,layer,rgb,x0=0,x1=None):
    ts=16 if layer==2 else 32; palbase=0x400 if layer==2 else 0x600
    H,W=codes.shape; x1=W if x1 is None else x1
    img=np.zeros((H*ts,(x1-x0)*ts,3),np.uint8); img[:]=(255,0,255)
    for r in range(H):
        for c in range(x0,x1):
            t=tile(layer,int(codes[r,c]))
            if t is None: continue
            a=int(attrs[r,c])
            if a&0x20: t=t[:,::-1]
            if a&0x40: t=t[::-1]
            p=palbase+(a&0x1f)*16+t.astype(np.int32)
            px=rgb[p]; px[t==15]=(255,0,255)
            img[r*ts:(r+1)*ts,(c-x0)*ts:(c-x0+1)*ts]=px
    return img
