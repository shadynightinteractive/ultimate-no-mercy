import pickle, sys, collections
secs,fn2sec,insns=pickle.load(open('db.pkl','rb'))
by={}
for s,f,a,asm in insns:
    if s in ('main_1050','ovl_e','entry'): by[a]=(f,asm)
W0=0x80159990; W1=W0+4*0x358
def load(path):
    T={}
    for line in open(path):
        main,_,sl=line.partition('|S')
        p=main.split(); pc=int(p[0],16); cnt=int(p[1]); w=int(p[2]); lo=int(p[3],16); hi=int(p[4],16); n=int(p[5]); A=[int(x,16) for x in p[6:]]
        T[pc]=(cnt,w,lo,hi,n,A)
    return T
def ap(A):
    A=sorted(set(A))
    if len(A)<2: return None
    d=[b-a for a,b in zip(A,A[1:])]
    import math
    g=0
    for x in d: g=math.gcd(g,x)
    if g==0: return None
    k=[(a-A[0])//g for a in A]
    return A[0],g,k
if __name__=="__main__":
    T={}
    for p in sys.argv[1:]:
        for pc,v in load(p).items():
            if pc in T:
                o=T[pc]; A=list(dict.fromkeys(o[5]+v[5])); T[pc]=(o[0]+v[0],o[1]+v[1],min(o[2],v[2]),max(o[3],v[3]),max(o[4],v[4],len(A)),A)
            else: T[pc]=v
    groups=collections.defaultdict(list)
    for pc,(cnt,w,lo,hi,n,A) in T.items():
        if n>4 or n<2: continue
        asm=by.get(pc,('',''))[1]
        if '($sp)' in asm: continue
        if W0<=lo<W1+0x358*2: continue
        r=ap(A)
        if not r: continue
        b,g,k=r
        if max(k)>3: continue
        if g not in (1,2,4,8,0xC,0x10,0x14,0x18,0x1C,0x20,0x24,0x28,0x2C,0x30,0x34,0x38,0x3C,0x40) and g<0x40: pass
        groups[(b - min(k)*g, g)].append(pc)
    out=sorted(groups.items(), key=lambda x:-len(x[1]))
    for (b,g),pcs in out:
        fs=sorted(set(by.get(p,('?',''))[0] for p in pcs))
        print(hex(b),hex(g),len(pcs),fs[:4])
