import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# From a six-man match state: trace every data access (pc -> addresses, and per current slot 0x80159080 the range)
# for N frames; then slotscan_an.py looks for per-slot strides that run past 4 entries outside the moved arrays.
#   slotscan.py ROM STATE N OUT [PRE]
import sys, pickle, ctypes as C
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
M = n64.N64(sys.argv[1], nmp("/home/devbox/work/sys_ss")); M.run(1); M.load(pickle.load(open(sys.argv[2], "rb")))
PRE = int(sys.argv[5]) if len(sys.argv) > 5 else 0      # frames to run first (untraced)
for f in range(PRE):
    M.run(1)
    if f % 2000 == 0: print("pre", f, flush=True)
M.lib.retro_trace_reset(); M.lib.retro_trace_range(0x80000000, 0x80800000); M.lib.retro_trace_enable(1)
N = int(sys.argv[3])
for f in range(N):
    M.run(1)
    if f % 500 == 0: print("frame", f, flush=True)
M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(sys.argv[4].encode()); print("done", flush=True)
import os; os._exit(0)
