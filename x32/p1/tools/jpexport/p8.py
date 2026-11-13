import sys, pickle, numpy as np
sys.path.insert(0,'/dw/dw/tools'); import cps
from PIL import Image, ImageDraw
d=open('/tmp/dw/rom/dynwarj_prg.bin','rb').read()
w=lambda i:(d[i]<<8)|d[i+1]
T=0x27a4
S=[]
for i in range(48):
    a=T+w(T+2*i); segs=[]; p=a
    while True:
        x,y,at,pg=d[p],d[p+1],d[p+2],d[p+3]; p+=4; bs=[]
        while d[p] not in (0,0x2f): bs.append(d[p]); p+=1
        segs.append((x,y,at,pg,bs)); e=d[p]; p+=1
        if e==0: break
    S.append((i,a,segs))
pickle.dump(S,open('S8.pkl','wb'))
def tile(c,h): return cps.tile8_s1(c,h)
def seg_img(x0,y0,pg,bs):
    if pg<2:
        im=np.full((8,8*len(bs)),15,np.uint8)
        for k,b in enumerate(bs): im[:,k*8:k*8+8]=tile(0x6000+(pg<<8)+b,k&1)
    else:
        im=np.full((16,16*len(bs)),15,np.uint8)
        for k,b in enumerate(bs):
            c=0x6000+(pg<<8)+b-0x10
            im[:8,k*16:k*16+8]=tile(c,0); im[:8,k*16+8:k*16+16]=tile(c+1,1); im[8:,k*16:k*16+8]=tile(c+0x10,0); im[8:,k*16+8:k*16+16]=tile(c+0x11,1)
    return im
if __name__=='__main__':
    for i,a,segs in S:
        print(i,hex(a),[(x,y,hex(at),pg,len(bs)) for x,y,at,pg,bs in segs])
