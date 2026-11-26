import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Per-slot data from the slot-correlated trace: instructions whose address moves linearly with the
# slot the game is processing (0x80159080).
import sys, collections, pickle
sys.path.insert(0,nmp('/home/devbox/work/six'))
from ana import by
TB=0x80159990; WS=0x358
def parse_line(line):
    main,_,sl=line.partition('|S')
    p=main.split(); pc=int(p[0],16)
    if not sl: return pc,None
    q=sl.split(); mask=int(q[0],16); rng={}
    k=1
    for s in range(8):
        if mask&(1<<s):
            a,b=q[k].split('-'); rng[s]=(int(a,16),int(b,16)); k+=1
    return pc,rng
res=collections.defaultdict(set)
single=0
for path in sys.argv[1:]:
    for line in open(path):
        pc,rng=parse_line(line)
        if not rng or len(rng)<2: continue
        asm=by.get(pc,('',''))[1]
        if '($sp)' in asm: continue
        ss=sorted(rng)
        mins=[rng[s][0] for s in ss]
        d=[(mins[i]-mins[0], ss[i]-ss[0]) for i in range(1,len(ss))]
        ks=set(dd//ds for dd,ds in d if ds and dd%ds==0)
        if len(ks)!=1: continue
        k=ks.pop()
        if k<=0: continue
        base=mins[0]-ss[0]*k
        if k==WS and TB<=base<TB+WS: continue
        if k==WS and TB-0x20<=base<TB+WS: continue
        res[(base,k)].add(pc)
for (b,k),pcs in sorted(res.items()):
    fs=sorted(set(by.get(p,('?',''))[0] for p in pcs))
    print("%08x stride %-5x n=%-3d %s"%(b,k,len(pcs),fs[:4]))
pickle.dump(dict(res),open(nmp('/home/devbox/work/six/slotcorr.pkl'),'wb'))
