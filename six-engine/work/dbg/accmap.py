import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# accmap.py ROM STATE FRAMES OUT : every RAM access by game code for FRAMES frames from STATE -> OUT (the core's
# trace table: pc, count, writes, lowest and highest address, up to 12 addresses)
import sys, os, pickle
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
n64.CORE = nmp("/home/devbox/work/harness/core_b.so")
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
rom, st, N, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
M = n64.N64(rom, nmp("/home/devbox/work/sys_dbg")); M.run(1); M.load(pickle.load(open(st, "rb")))
M.lib.retro_trace_reset(); M.lib.retro_trace_range(0x80000000, 0x80800000); M.lib.retro_trace_enable(1)
M.run(N); M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(out.encode())
print("done", out)
