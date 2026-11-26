import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle
secs,fn2sec,insns=pickle.load(open(nmp('/home/devbox/work/six/db.pkl'),'rb'))
from collections import defaultdict
FN=defaultdict(list)
for s,f,a,asm in insns: FN[(s,f)].append((a,asm))
for k in FN: FN[k].sort()
def parse(asm):
    p=asm.split(None,1); return p[0],[x.strip() for x in p[1].split(',')] if len(p)>1 else []
def find_lui(sec,func,pc,reg):
    lst=FN[(sec,func)]; i=[a for a,_ in lst].index(pc)
    chain=[]
    r=reg
    for j in range(i-1,-1,-1):
        a,asm=lst[j]; op,args=parse(asm)
        if not args or args[0]!=r: continue
        if op=='lui': chain.append((a,asm)); return a,chain
        if op in('addu','add') and r in args[1:]:
            chain.append((a,asm)); continue   # base + index: keep following r
        return None,chain+[(a,asm)]
    return None,chain
