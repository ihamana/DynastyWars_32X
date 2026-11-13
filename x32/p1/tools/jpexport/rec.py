from p8 import *
def w16(a): return (d[a]<<8)|d[a+1]
def rec_segs(a):
    segs=[]
    while True:
        x,y,at=d[a],d[a+1],w16(a+2); s0=a; a+=4; ws=[]
        while True:
            v=w16(a); a+=2
            if v in (0,0xFFFF): break
            ws.append(v)
        segs.append((s0,x,y,at,ws))
        if v==0: break
    return segs,a
def glyph16(tl):
    im=np.full((16,16),15,np.uint8)
    im[:8,:8]=tile(tl,0); im[:8,8:]=tile(tl+1,1); im[8:,:8]=tile(tl+0x10,0); im[8:,8:]=tile(tl+0x11,1)
    return im
def rec_img(ws, mode16=True):
    if mode16:
        im=np.full((16,16*len(ws)),15,np.uint8)
        for k,v in enumerate(ws): im[:,k*16:k*16+16]=glyph16((v+0x5FF0)&0xFFFF)
    else:
        im=np.full((8,8*len(ws)),15,np.uint8)
        for k,v in enumerate(ws): im[:,k*8:k*8+8]=tile((v+0x6000)&0xFFFF,k&1)
    return im
def toimg(c): return Image.fromarray(np.where(c==15,255,np.clip(220-c*14,0,220)).astype(np.uint8))
