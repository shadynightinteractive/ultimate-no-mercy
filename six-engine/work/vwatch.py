import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# The six-man ROM from power-on (the stock game's first frame, its main code patched in), the menus driven like
# oracle3's boot run: which code stores a value in [lo, hi) -- a pointer into a moved array's old place, say.
#   vwatch.py SIX_ROM lo hi frames
import sys, os, struct, ctypes as C
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
args = sys.argv[1:]; sys.argv.append("--oracle")
import n64
from rompatch import secs
n64.OPTIONS[b"parallel-n64-cpucore"] = os.environ.get("O2_CORE", "cached_interpreter").encode()
lo, hi, frames = int(args[1], 16), int(args[2], 16), int(args[3])
A = n64.N64(nmp("/home/devbox/work/nomercy.z64"), nmp("/home/devbox/work/sys_vwA")); A.run(1); st = A.save()
B = A if False else None
n64.CORE = nmp("/home/devbox/work/harness/core_b.so")
B = n64.N64(args[0], nmp("/home/devbox/work/sys_vwB")); B.run(1); B.load(st)
s = secs["main_1050"]
for o in range(0, 0x56670 - 0x450, 4):
    a_, b_ = A.romdata[s["rom"] + o:s["rom"] + o + 4], B.romdata[s["rom"] + o:s["rom"] + o + 4]
    if a_ != b_: B.w32(s["vram"] + o, struct.unpack(">I", b_)[0])
B.load(B.save())
src = open(nmp("/home/devbox/work/six/oracle3.py")).read()
ns = {}; exec(src[src.index("BOOT_INPUTS = ["):src.index("INPUT = [")], ns)
INPUT = [b for b, n_ in ns["BOOT_INPUTS"] for _ in range(n_)]
B.lib.retro_trace_value(lo, hi)
if len(args) > 5:
    B.lib.retro_trace_reset(); B.lib.retro_trace_range(int(args[4], 16), int(args[5], 16)); B.lib.retro_trace_enable(1)
    if len(args) > 6: B.lib.retro_trace_ra(int(args[6], 16))
for f in range(frames):
    B.run(1, buttons=INPUT[f] if f < len(INPUT) else ())
pcs = (C.c_uint32 * 256)(); ads = (C.c_uint32 * 256)(); vals = (C.c_uint32 * 256)()
n = B.lib.retro_trace_value_get(pcs, ads, vals)
print(n, "stores")
seen = {}
for i in range(min(n, 256)):
    seen.setdefault(pcs[i], []).append((ads[i], vals[i]))
for pc, l in sorted(seen.items()): print("pc %08x x%d  e.g. [%08x] = %08x" % (pc, len(l), l[0][0], l[0][1]))
if len(args) > 5:
    B.lib.retro_trace_enable(0); B.lib.retro_trace_dump(nmp("/tmp/devbox-0/vw_tr.txt").encode()); print(open(nmp("/tmp/devbox-0/vw_tr.txt")).read()[:4000])
    if len(args) > 6:
        rg = (C.c_uint32 * 32)(); have = B.lib.retro_trace_regs_get(rg)
        names = "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()
        print("regs at the access:", have, " ".join("%s=%08x" % (names[q], rg[q]) for q in range(32)))
        np_ = __import__("pickle"); open(nmp("/tmp/devbox-0/vw_regs.txt"), "w").write(repr(list(rg)))
        sp = rg[29]; print("stack:", " ".join("%08x" % B.r32(sp + 4 * q) for q in range(96)))
    print("now:", " ".join("%08x" % B.r32(a) for a in range(int(args[4], 16), int(args[5], 16), 4)))
if len(args) > 5:          # and which code touched [args[4], args[5]) (a second run)
    pass
