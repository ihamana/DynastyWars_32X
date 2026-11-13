import sys,struct,glob,pickle,numpy as np
res=pickle.load(open('/tmp/dw/pic/all.pkl','rb'))
BL=(0x6020,0x6120,0x6000,0x6100)
def ext(run,fr):
    b=open('%s/snap_%05d.bin'%(run,fr),'rb').read(); g=b[:0x30000]
    cpsa=struct.unpack('<32H',b[0x30000:0x30040]); ob=struct.unpack('<1024H',b[0x30080:0x30880])
    base=(cpsa[1]*256)&0x3c000; sx,sy=cpsa[6],cpsa[7]
    xs=[];ys=[]
    for r in range(28):
        row=(r+(16+sy)//8)&63
        for c in range(49):
            col=(c+(64+sx)//8)&63; i=base+((row&0x1f)+((col&63)<<5)+((row&0x20)<<6))*4
            code=(g[i]<<8)|g[i+1]
            if code not in BL and code>=0x6000: xs.append(c*8-((64+sx)&7)); ys.append(r)
    sp=[]
    for i in range(0,1024,4):
        x,y,c,a=ob[i:i+4]
        if (a&0xff00)==0xff00: break
        nx=((a>>8)&15)+1 if a&0xff00 else 1
        X=(x&0x1ff)-64; Y=(y&0x1ff)-16
        if -16<X<400 and -16<Y<224: sp.append((X,X+16*nx,Y))
    return (min(xs),max(xs)+8) if xs else None, (min(s[0] for s in sp),max(s[1] for s in sp)) if sp else None
out={}
for fi in res:
    if fi['sel'] or fi['hud'] or fi['scene']<8 or not fi['layers']: continue
    k=(fi['run'][:3],fi['scene'])
    try: e=ext(fi['run'],fi['fr'])
    except Exception as ex: continue
    o=out.setdefault(k,[None,None,0])
    for j in (0,1):
        if e[j]: o[j]=(min(o[j][0],e[j][0]),max(o[j][1],e[j][1])) if o[j] else e[j]
    o[2]+=1
for k,v in sorted(out.items()): print(k,'s1',v[0],'spr',v[1],'n',v[2])
