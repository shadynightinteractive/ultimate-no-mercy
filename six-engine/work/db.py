import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import re, glob, pickle, os
SRC=nmp("/home/devbox/src/WWFNoMercyRecomp")
def load_sections():
    secs=[]; cur=None; fn2sec={}
    for line in open(SRC+"/syms/dump.toml"):
        line=line.strip()
        if line=="[[section]]": cur={}; secs.append(cur); continue
        m=re.match(r'(name|rom|vram|size) = (.*)',line)
        if m and cur is not None and 'funcs_done' not in cur:
            k,v=m.groups(); cur[k]=v.strip('"') if k=='name' else int(v,0)
        m=re.match(r'\{ name = "([^"]+)", vram = (0x[0-9A-Fa-f]+), size = (0x[0-9A-Fa-f]+)',line)
        if m: fn2sec[m.group(1)]=(cur['name'],int(m.group(2),16),int(m.group(3),16))
    return {s['name']:s for s in secs}, fn2sec
def build():
    secs,fn2sec=load_sections()
    insns=[]  # (sec, func, vaddr, asm)
    rx=re.compile(r"^\s*// 0x([0-9A-F]{8}): (.*)$")
    for f in sorted(glob.glob(SRC+"/RecompiledFuncs/funcs_*.c")):
        cur=None; seen=set(); lastsec=None
        for line in open(f):
            if line.startswith("RECOMP_FUNC void"):
                cur=line.split()[2].split("(")[0]; seen=set()
                if cur in fn2sec: lastsec=fn2sec[cur][0]
                continue
            m=rx.match(line)
            if m and cur:
                a=int(m.group(1),16)
                if a in seen: continue   # delay slot duplicates
                seen.add(a)
                sec=fn2sec[cur][0] if cur in fn2sec else lastsec
                insns.append((sec,cur,a,m.group(2).strip()))
    return secs,fn2sec,insns
if __name__=="__main__":
    secs,fn2sec,insns=build()
    pickle.dump((secs,fn2sec,insns),open("db.pkl","wb"))
    print(len(insns),"insns", len(set(i[1] for i in insns)),"funcs", sum(1 for i in insns if i[0] is None),"unsectioned")
