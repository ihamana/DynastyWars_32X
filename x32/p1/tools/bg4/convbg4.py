"""(32xM) 4 palette lines version of DW_RE convbg.py.  P1 background converter: CPS scene maps (scroll2 -> plane A, scroll3 -> plane B) -> MD tiles + maps.
usage: convbg.py SCENE SNAP OUTPREFIX [budget]"""
import sys, os, struct, numpy as np, collections
sys.path.insert(0, '/dw/dw/tools')
import cps, mdpal
NL = int(os.environ.get('NL', '4'))
scene=int(sys.argv[1]); snap=cps.load_snap(sys.argv[2]); out=sys.argv[3]
rgb=snap['rgb']; B=snap['B']
# V3.22 PALZONE=pkl (tools/bg4/palzone.py): the game switches a layer's palettes part way through the stage (stage 8):
# each cell is coloured with the version that is up while its column is on screen
ZP={2:{},3:{}}; _ZC={}
if os.environ.get('PALZONE'):
    import pickle
    ZP=pickle.load(open(os.environ['PALZONE'],'rb'))
def crgb(L,cx):
    if not ZP[L]: return rgb
    k=tuple(int(col[min(cx,len(col)-1)]) for p,(col,vs) in sorted(ZP[L].items()))
    if (L,k) not in _ZC:
        a=rgb.copy()
        for (p,(col,vs)),v in zip(sorted(ZP[L].items()),k): a[p*16:p*16+16]=vs[v]
        _ZC[(L,k)]=a
    return _ZC[(L,k)]
PRIO=[B[0x2a//2],B[0x28//2],B[0x26//2],B[0x24//2]]
layers={}; WO={}
for L in (2,3):
    c,a,blk=cps.scene_map(scene,L,32)
    cols=np.where(blk.any(axis=0))[0]; per=16 if L==2 else 8; ts=per and (16 if L==2 else 32)
    W=(cols.max()+1)*per; c=c[:,:W]; a=a[:,:W]; WO[L]=W*ts   # map width in pixels before the extra cells
    if L==3 and os.environ.get('EXTRA3'):
        # dynamic scroll3 cells (the arcade rewrites them: stage 2 harbour): extra arcade cells after a 12-column gap,
        # 24 per column; the 32X tells the MD which one to show (tools/bg4/dyn3.py writes the list and the 32X table)
        import pickle
        ex=pickle.load(open(os.environ['EXTRA3'],'rb')); G=12; nc=(len(ex)+c.shape[0]-1)//c.shape[0]
        c=np.pad(c,((0,0),(0,G+nc))); a=np.pad(a,((0,0),(0,G+nc)))
        for i,(code,attr) in enumerate(ex): c[i%c.shape[0],W+G+i//c.shape[0]]=code; a[i%c.shape[0],W+G+i//c.shape[0]]=attr
        print('extra3 cells',len(ex),'from column',W+G); W=c.shape[1]
    if L==2 and os.environ.get('EXTRA2'):
        # dynamic scroll2 cells (stage 2 harbour: the ships): the same way, 48 per column (tools/bg4/dyn2.py)
        import pickle
        ex=pickle.load(open(os.environ['EXTRA2'],'rb')); G=8; nc=(len(ex)+c.shape[0]-1)//c.shape[0]
        c=np.pad(c,((0,0),(0,G+nc))); a=np.pad(a,((0,0),(0,G+nc)))
        for i,(code,attr) in enumerate(ex): c[i%c.shape[0],W+G+i//c.shape[0]]=code; a[i%c.shape[0],W+G+i//c.shape[0]]=attr
        print('extra2 cells',len(ex),'from column',W+G); W=c.shape[1]
    H=c.shape[0]; n8=ts//8
    # MD cell grid
    pens=np.full((H*n8*8,W*n8*8),255,np.uint8); pal=np.zeros((H*n8,W*n8),np.int32); pri=np.zeros((H*n8,W*n8),bool)
    palbase=0x40 if L==2 else 0x60
    for y in range(H):
        for x in range(W):
            t=cps.tile(L,int(c[y,x]))
            if t is None: continue
            at=int(a[y,x])
            if at&0x20: t=t[:,::-1]
            if at&0x40: t=t[::-1]
            tt=np.where(t==15,255,t).astype(np.uint8)
            pens[y*ts:(y+1)*ts,x*ts:(x+1)*ts]=tt
            pal[y*n8:(y+1)*n8,x*n8:(x+1)*n8]=palbase+(at&0x1f)
            if L==2:
                grp=(at>>7)&3
                if grp and PRIO[grp]:
                    m=np.isin(t,[p for p in range(16) if PRIO[grp]>>p&1])
                    pri[y*n8:(y+1)*n8,x*n8:(x+1)*n8]|=m.reshape(n8,8,n8,8).any(axis=(1,3))
    layers[L]=dict(pens=pens,pal=pal,pri=pri)
# ---- palette: 2 lines (0: plane B / far, 1: plane A / near) jointly; each cell picks best line
cellcols=[]   # per cell: Counter of md colors
allw=collections.Counter()
for L in (2,3):
    d=layers[L]; h,w=d['pal'].shape
    for cy in range(h):
        for cx in range(w):
            blk=d['pens'][cy*8:cy*8+8,cx*8:cx*8+8]
            m=blk!=255
            if not m.any(): continue
            cols=crgb(L,cx)[d['pal'][cy,cx]*16+blk[m].astype(int)]
            for col in map(tuple,mdpal.to_md3(cols)): allw[col]+=1
# ---- palette: 2 lines optimized jointly (cells choose a line; lines refit by weighted k-means)
cells=[]   # (layer, cy, cx, colors (N,3) float, weights)
for L in (2,3):
    d=layers[L]; h,w=d['pal'].shape
    for cy in range(h):
        for cx in range(w):
            blk=d['pens'][cy*8:cy*8+8,cx*8:cx*8+8]; m=blk!=255
            if not m.any(): continue
            cols=mdpal.md_rgb(mdpal.to_md3(crgb(L,cx)[d['pal'][cy,cx]*16+blk[m].astype(int)]))
            u,cnt=np.unique(cols,axis=0,return_counts=True)
            cells.append((L,cy,cx,u.astype(np.float32),cnt))
print('cells',len(cells))
# V3.12 REGW=a2,b3: cells left of scroll2 x a2 / scroll3 x b3 (stage 2's harbour) and the rest (the land) weigh the same
# in total, so the long harbour does not take the colours of the forest and the castle
if os.environ.get('REGW'):
    a2,b3=[int(v) for v in os.environ['REGW'].split(',')]
    reg=[0 if (c[2]*8<(a2 if c[0]==2 else b3) or c[2]*8>=WO[c[0]]) else 1 for c in cells]
    tw=[sum(c[4].sum() for c,r in zip(cells,reg) if r==k) for k in (0,1)]
    f=[1.0, tw[0]/max(tw[1],1)]
    cells=[(c[0],c[1],c[2],c[3],c[4]*f[r]) for c,r in zip(cells,reg)]
    print('region weights',tw,f)
# V3.12 OKLAB=1: palettes fitted and pixels matched in OKLab (perceptual) instead of weighted RGB: hues hold
# (stage 2's forest floor came out grey-green)
OKL=os.environ.get('OKLAB')=='1'
OKAB=float(os.environ.get('OKAB','1'))   # chroma (a, b) weight against lightness
def oklab(rgb):
    c=np.asarray(rgb,np.float64)/255.0
    c=np.where(c<=0.04045,c/12.92,((c+0.055)/1.055)**2.4)
    l=0.4122214708*c[...,0]+0.5363325363*c[...,1]+0.0514459929*c[...,2]
    m=0.2119034982*c[...,0]+0.6806995451*c[...,1]+0.1073969566*c[...,2]
    s_=0.0883024619*c[...,0]+0.2817188376*c[...,1]+0.6299787005*c[...,2]
    l,m,s_=np.cbrt(l),np.cbrt(m),np.cbrt(s_)
    return np.stack([0.2104542553*l+0.7936177850*m-0.0040720468*s_, 1.9779984951*l-2.4285922050*m+0.4505937099*s_,
                     0.0259040371*l+0.7827717662*m-0.8086757660*s_],-1)*np.array([1.0,OKAB,OKAB])*100
ALLMD=np.array([[r,g,b] for r in range(8) for g in range(8) for b in range(8)]); ALLLAB=oklab(mdpal.md_rgb(ALLMD))
def lab_kmeans(X,W,k,iters=25,tries=4):
    X=np.asarray(X,np.float64); W=np.asarray(W,np.float64); best=None
    if len(X)<=k: return mdpal.to_md3(X)
    L=oklab(X)
    for t in range(tries):
        rng=np.random.default_rng(t+1)
        C=[L[rng.choice(len(L),p=W/W.sum())]]
        for _ in range(k-1):
            d=np.min(((L[:,None]-np.array(C)[None])**2).sum(-1),1)*W; C.append(L[rng.choice(len(L),p=d/d.sum())] if d.sum()>0 else L[0])
        C=np.array(C)
        for it in range(iters):
            a=((L[:,None]-C[None])**2).sum(-1).argmin(1)
            for j in range(k):
                m=a==j
                if m.any(): C[j]=(L[m]*W[m,None]).sum(0)/W[m].sum()
            C=ALLLAB[((C[:,None]-ALLLAB[None])**2).sum(-1).argmin(1)]      # MD colours
        e=(((L[:,None]-C[None])**2).sum(-1).min(1)*W).sum()
        if best is None or e<best[0]: best=(e,C)
    idx=[((ALLLAB-c)**2).sum(-1).argmin() for c in best[1]]
    return ALLMD[idx]
if OKL:
    def _near(levels,rgb):
        P=oklab(mdpal.md_rgb(levels)); x=oklab(np.asarray(rgb,np.float64))
        return np.argmin(((x[...,None,:]-P)**2).sum(-1),axis=-1)
    mdpal.nearest=_near
def fit(sel):
    acc=collections.Counter()
    for i in sel:
        for c,k in zip(map(tuple,cells[i][3]),cells[i][4]): acc[c]+=int(k)
    for f in FIX: acc.pop(f,None)
    X=np.array(list(acc.keys()),np.float32); W=np.array(list(acc.values()),np.float64)
    k=lab_kmeans(X,W,15-len(FIX)) if OKL else mdpal.kmeans(X,W,15-len(FIX))
    return np.concatenate([np.array(k).reshape(-1,3)]+[np.array(mdpal.to_md3(np.array(f,np.float32))).reshape(1,3) for f in FIX]) if FIX else k
# colours covering large areas (sky) go into both lines: cells of either line show the same shade
tot=collections.Counter()
for c in cells:
    for col,k in zip(map(tuple,c[3]),c[4]): tot[col]+=int(k)
allw=sum(tot.values())
FIX=[c for c,w in tot.most_common(int(os.environ.get('NFIX','4'))) if w>allw*0.04]   # V3.12: NFIX=0 for stage 2 (they were the harbour water)
print('shared colours',FIX)
# V3.12 FIXC=r,g,b;r,g,b (MD 0-7 levels): more colours in every line (stage 2's sky: cells next to the walls took
# another line without its blues -> blocks in the sky)
FIX+=[tuple(float(v) for v in mdpal.md_rgb(np.array([int(t) for t in c.split(',')]))) for c in os.environ.get('FIXC','').split(';') if c]
if os.environ.get('SHOWSKY'):
    t=collections.Counter()
    for c in cells:
        if c[0]==3 and c[2]*8>=2150 and c[2]*8<WO[3] and int(os.environ['SHOWSKY'].split('-')[0])<=c[1]<int(os.environ['SHOWSKY'].split('-')[1]):
            for col,k in zip(map(tuple,c[3]),c[4]): t[tuple(int(v) for v in mdpal.to_md3(np.array(col)))]+=k
    print(t.most_common(12)); sys.exit()
if os.environ.get('SHOWTOP'): print([(c,round(w/allw,3)) for c,w in tot.most_common(16)]); sys.exit()
# initial assignment: plane B (far) cells to lines 0..NL/2-1, plane A to the rest, split by mean brightness
# V3.12 sections (SECT=x3,x3,...: scroll3 x where the MD swaps CRAM lines 2-3): lines 0-1 shared by the whole stage,
# lines 2-3 per section; the cells seen on both sides of a swap (the overlap) only use lines 0-1
SECT=[int(v) for v in os.environ.get('SECT','').split(',') if v]
NSEC=len(SECT)+1
def sec_of(L,cx):
    x=cx*8
    if x>=WO[L]: return 0                              # the harbour's extra (dynamic) cells
    k=2 if L==2 else 1; s=0
    for S in SECT:
        lo=k*(S-16)+96-24; hi=k*(S+16)+416+24
        if x>=hi: s+=1
        elif x>=lo: return -1
    return s
if SECT: NL=2+2*NSEC
def allowed(L,cx):
    if not SECT: return list(range(NL))
    s=sec_of(L,cx)
    return [0,1] if s<0 else [0,1,2+2*s,3+2*s]
cell_allowed=[allowed(c[0],c[2]) for c in cells]
assign=np.zeros(len(cells),int)
for L,base,nl in (((3,0,NL//2),(2,NL//2,NL-NL//2)) if not SECT else ()):
    idx=[i for i,c in enumerate(cells) if c[0]==L]
    br=np.array([(cells[i][3].sum(1)*cells[i][4]).sum()/cells[i][4].sum() for i in idx])
    q=np.quantile(br,np.linspace(0,1,nl+1)[1:-1]) if nl>1 else []
    for i,v in zip(idx,br): assign[i]=base+int(np.searchsorted(q,v))
if SECT:                                          # by brightness over each cell's allowed lines
    br=np.array([(c[3].sum(1)*c[4]).sum()/c[4].sum() for c in cells])
    for i,al in enumerate(cell_allowed): assign[i]=al[min(len(al)-1,int(br[i]/(br.max()+1)*len(al)))]
lines=[fit(np.where(assign==l)[0]) for l in range(NL)]
def cell_err(c,li):
    if OKL:
        P=oklab(mdpal.md_rgb(lines[li])); d=((oklab(c[3])[:,None,:]-P[None])**2).sum(-1).min(1)
        return (d*c[4]).sum()
    P=mdpal.md_rgb(lines[li]).astype(np.float32)
    d=(((c[3][:,None,:]-P[None])*mdpal.CW)**2).sum(-1).min(1)
    return (d*c[4]).sum()
best=None
for it in range(10):
    E=np.array([[cell_err(c,l) if l in cell_allowed[i] else 1e30 for l in range(NL)] for i,c in enumerate(cells)])
    assign=E.argmin(1); tot=E.min(1).sum()
    print('iter',it,'err',tot,'cells per line',[int((assign==l).sum()) for l in range(NL)])
    if best is None or tot<best[0]: best=(tot,lines)
    lines=[fit(np.where(assign==l)[0]) if (assign==l).any() else lines[l] for l in range(NL)]
lines=best[1]
# ---- build MD cells
tiles={}; tilelist=[]
def addtile(px):
    k=px.tobytes()
    v=[(px,0),(px[:,::-1],1),(px[::-1],2),(px[::-1,::-1],3)]
    for arr,fl in v:
        kk=arr.tobytes()
        if kk in tiles: return tiles[kk],fl
    tiles[k]=len(tilelist); tilelist.append(px.copy()); return tiles[k],0
SMOOTH=float(os.environ.get('SMOOTH','0')); SMA=float(os.environ.get('SMA','0'))
LN={L:np.full(layers[L]['pal'].shape,-1,int) for L in (2,3)}
maps={}
for L in (2,3):
    d=layers[L]; h,w=d['pal'].shape
    M=np.zeros((h,w),np.uint32)
    for cy in range(h):
        for cx in range(w):
            blk=d['pens'][cy*8:cy*8+8,cx*8:cx*8+8]; m=blk!=255
            if not m.any(): M[cy,cx]=0; continue
            cols=crgb(L,cx)[d['pal'][cy,cx]*16+np.minimum(blk,15).astype(int)]
            best=None; cand={}
            for li in allowed(L,cx):
                ix=mdpal.nearest(lines[li],cols)
                err=(((oklab(mdpal.md_rgb(lines[li])[ix])-oklab(cols))**2).sum(-1)[m].sum() if OKL else
                     ((mdpal.md_rgb(lines[li])[ix].astype(float)-cols)**2).sum(-1)[m].sum())
                cand[li]=(err,li,ix)
                if best is None or err<best[0]: best=(err,li,ix)
            if SMOOTH:   # V3.18: a neighbour's line when nearly as good (cells flipping between lines: blotches)
                nb=[int(LN[L][cy,cx-1]) if cx else -1, int(LN[L][cy-1,cx]) if cy else -1]
                for ln in nb:
                    if ln in cand and ln!=best[1] and cand[ln][0]<=best[0]*(1+SMOOTH)+SMA*m.sum():
                        best=cand[ln]; break
            _,li,ix=best; LN[L][cy,cx]=li
            px=np.where(m,ix+1,0).astype(np.uint8)
            tid,fl=addtile(px)
            li=li if li<2 else 2+(li-2)%2
            M[cy,cx]=(tid+1)|(li<<16)|(fl<<18)|(int(d['pri'][cy,cx])<<20)   # li: 2 bits (16-17), flips 18-19, pri 20
    maps[L]=M
print('unique MD tiles (flip-dedup)',len(tilelist))
# ---- visible-window stats (320x224 approx, all rows of 3-block world height capped to 28 rows window sliding)
for L in (2,3):
    M=maps[L]; h,w=M.shape; mx=0
    for x0 in range(0,w-40,2):
        for y0 in range(0,h-28,4):
            u=np.unique(M[y0:y0+29,x0:x0+41]&0xffff); mx=max(mx,len(u))
    print('layer',L,'map',M.shape,'max unique tiles in 41x29 window',mx)
np.save(out+'_mapA.npy',maps[2]); np.save(out+'_mapB.npy',maps[3])
if SECT:
    np.save(out+'_sect.npy',np.array(SECT))
    np.save(out+'_secl.npy',np.array([np.vstack([l,np.zeros((15-len(l),3),int)]) for l in lines[4:]]))
    lines=lines[:4]
else:
    for f in ('_sect.npy','_secl.npy'):
        if os.path.exists(out+f): os.remove(out+f)
np.save(out+'_tiles.npy',np.array(tilelist)); np.save(out+'_lines.npy',np.array([np.vstack([l,np.zeros((15-len(l),3),int)]) for l in lines]))
