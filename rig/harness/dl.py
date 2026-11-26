"""F3DEX2 display-list walker over a big-endian RDRAM dump."""
import struct
def r32(d,a): return struct.unpack_from(">I", d, a & 0x7FFFFF)[0]
def find_tasks(d):
    out=[]
    for a in range(0, 0x400000, 4):
        if r32(d,a)==1:
            dp=r32(d,a+0x30); ds=r32(d,a+0x34); uc=r32(d,a+0x10)
            if 0x80000000<=dp<0x80800000 or (0<dp<0x800000):
                if 0<ds<0x100000 and (0x80000000<=uc<0x80800000 or 0<uc<0x800000):
                    out.append((0x80000000+a, dp))
    return out
def walk(d, start, segs=None, depth=0, out=None, maxn=200000):
    segs = segs if segs is not None else {}
    out = out if out is not None else []
    p = start; stack=[]
    def res(a):
        seg=(a>>24)&0xF
        return (segs.get(seg,0)+(a&0xFFFFFF)) if (a>>24)<0x80 and seg else a
    n=0
    while n<maxn:
        n+=1
        w0=r32(d,p); w1=r32(d,p+4); op=w0>>24
        out.append((p,w0,w1))
        if op==0xDB and ((w0>>16)&0xFF)==6: segs[((w0&0xFFFF)//4)]=w1&0xFFFFFF
        if op==0xDE:
            tgt=res(w1)
            if (w0>>16)&0xFF==0: stack.append(p+8)
            p=tgt; continue
        if op==0xDF:
            if not stack: break
            p=stack.pop(); continue
        if op in (0xE4,0xE5): # texrect: 3 words in F3DEX2 (E1, F1 follow)
            out.append((p+8,r32(d,p+8),r32(d,p+12))); out.append((p+16,r32(d,p+16),r32(d,p+20))); p+=24; continue
        p+=8
    return out
