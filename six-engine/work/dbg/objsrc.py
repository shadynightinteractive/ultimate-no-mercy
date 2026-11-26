import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# From a late six-man Rumble state: press A through the match end; log every store while the results table (scr 28)
# is set up, and print the ones that write a column object's type (0x73xx halfword) -- who builds the table.
import sys, pickle, ctypes as C
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
M = n64.N64(sys.argv[1], nmp("/home/devbox/work/sys_os")); M.run(1); M.load(pickle.load(open(sys.argv[2], "rb")))
scr = lambda: M.s16(0x800457A4)
f = 0
while f < 8000 and scr() == 42:
    if f % 200 == 199: M.press("A", hold=4, after=0)
    M.run(1); f += 1
print("left the match at", f, "scr", scr(), flush=True)
buf = (C.c_uint32 * (4 * (4 << 20)))()
M.lib.retro_slog(1, buf, 0)
g = 0
while scr() != 28 and g < 3000:
    M.run(1); g += 1
M.run(5)
n = M.lib.retro_slog(0, buf, 0); n = M.lib.retro_slog(2, buf, 4 << 20)
print("stores", n, "scr", scr(), flush=True)
seen = {}
for i in range(n):
    pc, ad, v, m = buf[4*i:4*i+4]
    for sh in (16, 0):
        if (m >> sh) & 0xFFFF == 0xFFFF and ((v >> sh) & 0xFF00) == 0x7300:
            seen.setdefault(pc, []).append((ad + (2 if sh == 0 else 0), (v >> sh) & 0xFFFF))
for pc, l in seen.items(): print("pc %08x" % pc, ["%08x=%04x" % x for x in l[:8]])
import os; os._exit(0)
