import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,sys
secs,fn2sec,insns=pickle.load(open(nmp('/home/devbox/work/six/db.pkl'),'rb'))
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
ea_at={}
for r in refs: ea_at[(r[1],r[3])]=r[0]
def show(fn, sec=None):
    out=[]
    for s,f,a,asm in insns:
        if f==fn and (sec is None or s==sec):
            e=ea_at.get((s,a))
            out.append("%08x  %-40s %s"%(a,asm, ("-> %08x"%e) if e else ""))
    return out
if __name__=="__main__":
    for fn in sys.argv[1:]: print("\n".join(show(fn)))
