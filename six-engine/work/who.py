import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,sys,collections
refs=pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'),'rb'))
def who(lo,hi,n=3,secs=None):
    g=collections.defaultdict(list)
    for r in refs:
        if lo<=r[0]<hi and (secs is None or r[1] in secs): g[r[0]].append(r)
    out=[]
    for ea in sorted(g):
        rs=g[ea]
        fns=collections.Counter((r[1][:5] if r[1]!='main_1050' else 'main',r[2]) for r in rs)
        kinds=collections.Counter(r[6][0]+r[4] for r in rs)
        out.append('%08x n%-3d %s | %s'%(ea,len(rs),' '.join('%s%d'%(k,v) for k,v in kinds.most_common(4)),' '.join('%s:%s'%(s,f[5:]) for (s,f),_ in fns.most_common(n))))
    return out
if __name__=='__main__':
    lo=int(sys.argv[1],16); hi=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 3
    print('\n'.join(who(lo,hi,n)))
