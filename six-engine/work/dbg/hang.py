import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# hang.py ROM STATE FRAMES : run FRAMES frames from STATE, then on until a frame takes > 15 s; then dump RDRAM (big
# endian, /tmp/devbox-0/hang_ram.bin) and the running RSP task, and quit
import sys, os, struct, pickle, threading, time
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
rom, st, N = sys.argv[1], sys.argv[2], int(sys.argv[3])
M = n64.N64(rom, nmp("/home/devbox/work/sys_dbg")); M.run(1); M.load(pickle.load(open(st, "rb")))
M.run(N)
t0 = [time.time()]
def wd():
    while True:
        time.sleep(1)
        if time.time() - t0[0] > 15:
            open(nmp("/tmp/devbox-0/hang_ram.bin"), "wb").write(M.dump())
            print("HUNG: gfx task %08x, last %08x, main pc %08x" % (M.r32(0x8005D8C0), M.r32(0x8005D8C8), M.r32(0x80089040 + 0x11C)), flush=True)
            os._exit(3)
threading.Thread(target=wd, daemon=True).start()
for k in range(200):
    t0[0] = time.time(); M.run(1)
    print("frame", N + k, "ok; gfx task %08x" % M.r32(0x8005D8C0), flush=True)
