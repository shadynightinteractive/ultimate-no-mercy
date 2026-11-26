import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle, sys, collections, math
sys.path.insert(0,nmp('/home/devbox/work/six'))
from ana import load, by
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
statbase=collections.Counter()
for r in refs: statbase[r[0]]+=1
T={}
for p in sys.argv[1:]:
    for pc,v in load(p).items():
        if pc in T:
            o=T[pc]; A=list(dict.fromkeys(o[5]+v[5])); T[pc]=(o[0]+v[0],o[1]+v[1],min(o[2],v[2]),max(o[3],v[3]),max(o[4],v[4],len(A)),A)
        else: T[pc]=v
W0=0x80159990
groups=collections.defaultdict(set)
for pc,(cnt,w,lo,hi,n,A) in T.items():
    asm=by.get(pc,('',''))[1]
    if '($sp)' in asm or n!=4: continue
    A=sorted(set(A))
    d=[b-a for a,b in zip(A,A[1:])]
    if len(set(d))!=1: continue
    k=d[0]
    if k==0x358 and W0<=A[0]<W0+0x358: continue
    groups[(A[0],k)].add(pc)
for (b,k),pcs in sorted(groups.items()):
    fs=sorted(set(by.get(p,('?',''))[0] for p in pcs))
    print("%08x stride %-5x pcs %3d  static@base %3d  %s"%(b,k,len(pcs),statbase[b],fs[:3]))
