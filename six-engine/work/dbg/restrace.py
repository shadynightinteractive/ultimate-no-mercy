import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# From a late six-man Rumble state: press A through the end of the match until the results table (scr 28), then
# trace which code reads the moved arrays while it shows -- the results screen's readers.
#   restrace.py ROM STATE OUT_PNG_PREFIX
import sys, os, pickle, ctypes as C
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
M = n64.N64(sys.argv[1], nmp("/home/devbox/work/sys_rt")); M.run(1); M.load(pickle.load(open(sys.argv[2], "rb")))
scr = lambda: M.s16(0x800457A4)
f = 0; seen28 = None
while f < 8000:
    if f % 200 == 199: M.press("A", hold=4, after=0)
    M.run(1); f += 1
    if scr() == 28 and seen28 is None: seen28 = f; print("scr 28 at", f, flush=True)
    if seen28 is not None and f - seen28 > 90: break
n64.fbshot(M, sys.argv[3] + "_res.png")
R = [(0x80500000, 0x80540000), (0x8016B9F0, 0x80176000), (0x8009F000, 0x800AB000)]
flat = (C.c_uint32 * 6)(*[x for r in R for x in r])
M.lib.retro_trace_reset(); M.lib.retro_trace_ranges(flat, 3); M.lib.retro_trace_enable(1)
M.run(30)
M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(nmp("/tmp/devbox-0/restrace.txt").encode())
n64.fbshot(M, sys.argv[3] + "_res2.png")
print("scr", scr(), "done")
os._exit(0)
