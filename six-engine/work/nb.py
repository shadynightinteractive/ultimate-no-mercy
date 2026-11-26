import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,sys,collections
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
def nb(lo,hi,secs=None):
    c=collections.defaultdict(lambda: collections.Counter())
    for r in refs:
        if lo<=r[0]<hi and (secs is None or r[1] in secs): c[r[0]][(r[1],r[6])]+=1
    return c
if __name__=="__main__":
    for spec in sys.argv[1:]:
        a,b=spec.split(':'); a=int(a,16); b=int(b,16)
        print("==",hex(a),hex(b))
        for ea,cc in sorted(nb(a,b).items()):
            print("  ",hex(ea), dict(cc))
