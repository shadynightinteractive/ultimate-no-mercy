import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,collections
L=pickle.load(open(nmp('/home/devbox/work/six/loops.pkl'),'rb'))
TB=0x80159990; WS=0x358
def norm(w):
    b,k=w[2],w[3]&0xFFFFFFFF
    if k>=0x80000000: return None
    return (b,k)
def is_w(b,k): return k in (WS,2*WS,3*WS) and TB-0x10<=b<TB+WS
arrays={}   # (base,k) -> reason
slot_loops=set()
changed=True
it=0
while changed and it<10:
    changed=False; it+=1
    for i,(sec,fn,a,e,b,ind,w) in enumerate(L):
        ws=[norm(x) for x in w]; ws=[x for x in ws if x]
        if not ws: continue
        known=any(is_w(*x) for x in ws) or any(x in arrays for x in ws)
        if not known: continue
        if not any(q[2] in (4,) for q in b) and not any(is_w(*x) for x in ws):
            pass
        if i not in slot_loops: slot_loops.add(i); changed=True
        for x in ws:
            if not is_w(*x) and x not in arrays:
                arrays[x]=(fn,hex(a)); changed=True
print(len(slot_loops),"slot loops;",len(arrays),"co-iterated arrays")
for (bb,k),why in sorted(arrays.items()): print(hex(bb),hex(k),why)
pickle.dump((slot_loops,arrays),open(nmp('/home/devbox/work/six/closure.pkl'),'wb'))
