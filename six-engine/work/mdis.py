import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,sys,collections
secs,fn2sec,insns=pickle.load(open(nmp('/home/devbox/work/six/db.pkl'),'rb'))
F=collections.defaultdict(list)
for s,f,a,asm in insns: F[f].append((a,asm))
def dis(fn):
    return '\n'.join('%08x  %s'%(a,asm) for a,asm in sorted(F[fn]))
if __name__=='__main__':
    for f in sys.argv[1:]: print('==',f, fn2sec.get(f)); print(dis(f))
