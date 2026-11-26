import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,collections,sys
sys.path.insert(0,nmp('/home/devbox/work/six'))
from verify import verify
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
slot_loops,arrays=pickle.load(open('closure.pkl','rb'))
sc=pickle.load(open('slotcorr.pkl','rb'))
C=collections.defaultdict(set)
for (b,k) in arrays: C[(b,k)].add('loop')
for (b,k) in sc: C[(b,k)].add('corr')
for line in open(nmp('/tmp/devbox-0/cand4.txt')):
    p=line.split(); b=int(p[0],16); k=int(p[2],16); C[(b,k)].add('ap4')
# cluster to struct bases using addr/indexed refs
addrbases=collections.Counter(r[0] for r in refs if r[6] in ('addr','indexed'))
out=[]
for (b,k),src in C.items():
    if k>0x10000 or k==0: continue
    # candidate struct base: the highest ref base in (b-k, b] that has addr/indexed refs
    cands=[x for x in addrbases if b-k<x<=b]
    base=max(cands) if cands else b
    out.append((base,k,b,src))
seen=set()
for base,k,b,src in sorted(out):
    key=(base,k)
    if key in seen: continue
    seen.add(key)
    fields=sorted(set(bb-base for (bs,kk,bb,ss) in out if bs==base and kk==k))
    print(verify(base,k), '| fields',[hex(f) for f in fields][:8], sorted(src))
