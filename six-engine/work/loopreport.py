import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle,collections,sys
sys.path.insert(0,'.')
import loopscan as ls, ana
L=pickle.load(open('loops.pkl','rb'))
T={}
for p in (nmp('/tmp/devbox-0/tr_rr.txt'),nmp('/tmp/devbox-0/tr_rr2.txt')):
    for pc,v in ana.load(p).items(): T[pc]=T.get(pc,0)+v[0]
rows=[]
for sec,fn,a,e,b,ind,w in L:
    lst=ls.FUN[(sec,fn)]; idx={x:i for i,(x,_) in enumerate(lst)}
    ctl=ls.loop_control(lst,idx[a],idx[e])
    ex=sum(T.get(x,0) for x,_ in lst[idx[a]:idx[e]+1])
    rows.append((sec,fn,a,e,ctl,ex,w))
pickle.dump(rows,open('loopreport.pkl','wb'))
if __name__=='__main__':
    want=int(sys.argv[1]) if len(sys.argv)>1 else 4
    for sec,fn,a,e,ctl,ex,w in rows:
        if any(k[2]==want for k in ctl):
            ws=' '.join('%s:%08x+%x'%(x[0][0],x[2],x[3]&0xffffffff) for x in w)
            print('%-9s %-14s %08x-%08x ex=%-7d %s'%(sec,fn,a,e,ex,ws))
