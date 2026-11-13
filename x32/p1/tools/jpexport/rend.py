from p8 import *
import os
os.makedirs('s8',exist_ok=True)
def compose(segs):
    can=np.full((34*8,64*8),15,np.uint8); x0=y0=10**9; x1=y1=0
    for x,y,at,pg,bs in segs:
        im=seg_img(x,y,pg,bs)
        py=(y-1)*8 if pg>=2 else y*8; px=x*8
        h,wd=im.shape
        reg=can[py+8:py+8+h, px:px+wd]   # +8 margin row
        m=im!=15; reg[m]=im[m]
        x0=min(x0,px); y0=min(y0,py+8); x1=max(x1,px+wd); y1=max(y1,py+8+h)
    c=can[y0:y1,x0:x1]
    a=np.where(c==15,255,np.clip(220-c*14,0,220)).astype(np.uint8)
    return Image.fromarray(a)
for i,a,segs in S:
    compose(segs).save('s8/%02d.png'%i)
