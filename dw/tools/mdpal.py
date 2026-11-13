"""Mega Drive color helpers + weighted k-means palette quantization."""
import numpy as np
LV=np.array([0,36,73,109,146,182,219,255])
def to_md3(rgb):            # rgb uint8 (...,3) -> 3-bit levels
    return np.clip(np.rint(np.asarray(rgb,np.float32)/36.43),0,7).astype(np.int32)
def md_word(l3):            # (r,g,b) levels -> CRAM word 0000 bbb0 ggg0 rrr0
    r,g,b=[int(v) for v in l3]; return (b<<9)|(g<<5)|(r<<1)
def md_rgb(l3): return LV[np.asarray(l3)]
def kmeans1(cols,weights,k,iters=30,seed=1):
    """cols: (N,3) float in 0..255, weights (N,). returns (k,3) centers snapped to MD levels (unique)."""
    cols=np.asarray(cols,np.float32); w=np.asarray(weights,np.float64)
    if len(cols)<=k:
        return np.unique(to_md3(cols),axis=0)
    rng=np.random.default_rng(seed)
    # k-means++ init
    c=[cols[np.argmax(w)]]
    for _ in range(1,k):
        d=np.min(((cols[:,None,:]-np.array(c)[None])**2).sum(-1),axis=1)*w
        c.append(cols[rng.choice(len(cols),p=d/d.sum())] if d.sum()>0 else cols[rng.integers(len(cols))])
    c=np.array(c)
    for _ in range(iters):
        a=np.argmin(((cols[:,None,:]-c[None])**2).sum(-1),axis=1)
        for j in range(k):
            m=a==j
            if m.any(): c[j]=(cols[m]*w[m,None]).sum(0)/w[m].sum()
    l=np.unique(to_md3(c),axis=0)
    return l
def nearest(levels,rgb):
    """levels (k,3) MD levels; rgb (...,3) -> index into levels"""
    P=md_rgb(levels).astype(np.float32); x=np.asarray(rgb,np.float32)
    d=((x[...,None,:]-P)**2*np.array([0.30,0.59,0.11]*1)).sum(-1)
    return np.argmin(d,axis=-1)

CW=np.array([0.55,0.95,0.40],np.float32)     # channel weights for distances (perceptual-ish)
def kmeans(cols,weights,k,iters=30,seed=1,tries=6):
    cols=np.asarray(cols,np.float32); w=np.asarray(weights,np.float64)
    best=None
    for t in range(tries):
        c=kmeans1(cols*CW,w,k,iters,seed+t*7)            # works on weighted space; returns levels of weighted space -> redo properly
        # kmeans1 snaps to MD levels assuming unweighted space, so run our own refinement here
        P=md_rgb(c).astype(np.float32)/np.maximum(CW,1e-6)
        P=md_rgb(to_md3(P)).astype(np.float32)
        d=(((cols[:,None,:]-P[None])*CW)**2).sum(-1)
        a=d.argmin(1); err=(d.min(1)*w).sum()
        # one refinement pass in original space
        for j in range(len(P)):
            m=a==j
            if m.any(): P[j]=(cols[m]*w[m,None]).sum(0)/w[m].sum()
        L=np.unique(to_md3(P),axis=0)
        d=(((cols[:,None,:]-md_rgb(L)[None].astype(np.float32))*CW)**2).sum(-1); err=(d.min(1)*w).sum()
        if best is None or err<best[0]: best=(err,L)
    return best[1]
def nearest(levels,rgb):
    P=md_rgb(levels).astype(np.float32); x=np.asarray(rgb,np.float32)
    d=(((x[...,None,:]-P)*CW)**2).sum(-1)
    return np.argmin(d,axis=-1)
