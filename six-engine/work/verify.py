import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,sys,collections
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
def verify(base,k,n=4):
    end=base+n*k
    inside=[r for r in refs if base<=r[0]<end]
    secs=collections.Counter(r[1] for r in inside)
    at=[r for r in refs if r[0]==base and r[6] in ('addr','indexed')]
    nxt=sorted(set(r[0] for r in refs if end<=r[0]<end+0x20))
    prv=sorted(set(r[0] for r in refs if base-0x20<=r[0]<base))
    endref=[r for r in refs if r[0]==end and r[6]=='addr']
    return "%08x x%-5x n%d: refs %4d %s | base-refs %d | prev %s | next %s | end-addr-refs %d"%(base,k,n,len(inside),dict(secs),len(at),[hex(x) for x in prv[-2:]],[hex(x) for x in nxt[:3]],len(endref))
if __name__=="__main__":
    for spec in sys.argv[1:]:
        b,k=spec.split('x'); print(verify(int(b,16),int(k,16)))
