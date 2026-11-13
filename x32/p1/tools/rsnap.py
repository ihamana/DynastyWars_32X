import sys; sys.path.insert(0,'/dw/x32/p1/tools')
import cpsr, numpy as np
def render_layer(fi, b, L, img=None):
    pal=cpsr.snap_pal(b)
    if img is None: img=np.zeros((224,384,3),np.uint8)
    if L not in fi['layers']: return img
    base,scx,scy,e=fi['layers'][L]; ts=16 if L==2 else 32
    pg=1024 if L==2 else 1536
    for (c,r),(code,attr) in e.items():
        p=cpsr.tile_pens(L,code)
        if p is None: continue
        if attr&0x20: p=p[:,::-1]
        if attr&0x40: p=p[::-1,:]
        X=c*ts-(64+scx); Y=r*ts-(16+scy)
        cols=pal[pg+(attr&31)*16:pg+(attr&31)*16+16]
        rgb=np.stack([(cols>>16)&255,(cols>>8)&255,cols&255],-1).astype(np.uint8)
        x0,y0=max(X,0),max(Y,0); x1,y1=min(X+ts,384),min(Y+ts,224)
        if x0>=x1 or y0>=y1: continue
        sub=p[y0-Y:y1-Y,x0-X:x1-X]; m=sub!=15
        reg=img[y0:y1,x0:x1]; reg[m]=rgb[sub[m]]
    return img
