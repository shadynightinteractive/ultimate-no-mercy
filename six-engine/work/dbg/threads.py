import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# load a state (six ROM), run, and show each thread's state / queue / saved pc, ra
#   threads.py ROM STATE [frames] [blob layout]
import sys, os, struct, pickle
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
rom, st = sys.argv[1], sys.argv[2]; N = int(sys.argv[3]) if len(sys.argv) > 3 else 5
if os.environ.get("CORE_B"): n64.CORE = nmp("/home/devbox/work/harness/core_b.so")
n64.OPTIONS[b"parallel-n64-cpucore"] = os.environ.get("O2_CORE", "dynamic_recompiler").encode()
M = n64.N64(rom, nmp("/home/devbox/work/sys_dbg")); M.run(1); M.load(pickle.load(open(st, "rb")))
THREADS = {0x80056670: "idle", 0x800571E0: "sched?", 0x80057238: "?57238", 0x8005D560: "t_f98", 0x8005D670: "?5d670", 0x8005D710: "t_10f4",
           0x8005E8F0: "gfx", 0x80089040: "main", 0x8008D330: "audio?", 0x8008D510: "pimgr", 0x8008E748: "vimgr"}
for f in range(N):
    M.run(1)
    print("frame", f, "running %08x" % M.r32(0x800537D0), "match frame %04x" % M.r16(0x801532C0))
    for t, nm in THREADS.items():
        print("  %-7s state %04x queue %08x pc %08x ra %08x sp %08x cause %08x badvaddr %08x" % (nm, M.r16(t + 0x10), M.r32(t + 0x8), M.r32(t + 0x11C), M.r32(t + 0x104), M.r32(t + 0xF4), M.r32(t + 0x120), M.r32(t + 0x124)))
        if nm == "main" and (M.r16(t + 0x10) == 1 or os.environ.get("REGS") == "2") and os.environ.get("REGS"):
            names = "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 gp sp fp ra".split()
            regs = [0] + [M.r32(t + 0x20 + 8 * k + 4) for k in range(25)] + [M.r32(t + 0x20 + 8 * 25 + 4 + 8 * k) for k in range(4)]
            print("   ", " ".join("%s=%08x" % (names[k], regs[k]) for k in range(len(names))))
if os.environ.get("HOT"):        # one more frame with every RAM access traced: the hottest pcs
    M.lib.retro_trace_reset(); M.lib.retro_trace_range(0x80000000, 0x80800000); M.lib.retro_trace_enable(1)
    M.run(int(os.environ.get("HOT_N", "1"))); M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(nmp("/tmp/devbox-0/hot.txt").encode())
    rows = sorted((int(l.split()[1]), l.split()[0], l.split()[3], l.split()[4]) for l in open(nmp("/tmp/devbox-0/hot.txt")))[::-1]
    for c, pc, lo, hi in rows[:40]: print("  pc %s count %d  %s..%s" % (pc, c, lo, hi))
    want = [x.lower() for x in os.environ.get("HOT_PCS", "").split(",") if x]
    for c, pc, lo, hi in rows:
        if pc in want: print("  watched pc %s count %d  %s..%s" % (pc, c, lo, hi))
if os.environ.get("RA_AT"):      # the distinct $ra (and the first regs) when RA_AT's pc accesses the main stack, over RA_N frames
    import ctypes as C
    pc_ = int(os.environ["RA_AT"], 16)
    M.lib.retro_trace_reset(); M.lib.retro_trace_range(0x80089000, 0x8008D200); M.lib.retro_trace_ra(pc_); M.lib.retro_trace_enable(1)
    M.run(int(os.environ.get("RA_N", "1"))); M.lib.retro_trace_enable(0)
    ra = (C.c_uint32 * 16)(); n_ = M.lib.retro_trace_ra_get(ra); print("ra at %08x:" % pc_, [hex(ra[k]) for k in range(n_)])
    rg = (C.c_uint32 * 32)()
    if M.lib.retro_trace_regs_get(rg): print(" regs:", " ".join("%d=%08x" % (q, rg[q]) for q in range(32)))
if os.environ.get("WHO"):        # who accesses WHO=lo,hi over WHO_N frames (writes column)
    lo_, hi_ = [int(x, 16) for x in os.environ["WHO"].split(",")]
    M.lib.retro_trace_reset(); M.lib.retro_trace_range(lo_, hi_); M.lib.retro_trace_ra(int(os.environ.get("WHO_PC", "0"), 16))
    if os.environ.get("WHO_COND"): r_, v_ = os.environ["WHO_COND"].split("="); M.lib.retro_trace_cond(int(r_), int(v_, 16))
    M.lib.retro_trace_enable(1)
    for k_ in range(int(os.environ.get("WHO_N", "10"))):
        M.run(1)
        if M.r16(0x80089040 + 0x10) == 1: print("main stopped after", k_ + 1, "frames"); break
    M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(nmp("/tmp/devbox-0/who.txt").encode())
    for l in open(nmp("/tmp/devbox-0/who.txt")):
        p = l.split(); print("  pc %s count %s writes %s  %s..%s" % tuple(p[:5]))
    import ctypes as C
    rg = (C.c_uint32 * 32)()
    if M.lib.retro_trace_regs_get(rg):
        print(" regs at WHO_PC:", " ".join("%d=%08x" % (q, rg[q]) for q in range(32)))
if os.environ.get("HANGHOT"):    # trace every access from now; if a frame takes > 15 s, dump the hottest pcs and quit
    import threading, time
    M.lib.retro_trace_reset(); M.lib.retro_trace_range(0x80000000, 0x80800000); M.lib.retro_trace_enable(1)
    t0 = [time.time()]
    def wd():
        while True:
            time.sleep(2)
            if time.time() - t0[0] > 15:
                M.lib.retro_trace_dump(nmp("/tmp/devbox-0/hang.txt").encode())
                rows = sorted((int(l.split()[1]), l.split()[0], l.split()[3], l.split()[4]) for l in open(nmp("/tmp/devbox-0/hang.txt")))[::-1]
                print("HUNG; hottest:"); [print("  pc %s count %d  %s..%s" % (pc, c, lo, hi)) for c, pc, lo, hi in rows[:30]]
                os._exit(3)
    threading.Thread(target=wd, daemon=True).start()
    for k_ in range(int(os.environ["HANGHOT"])):
        M.lib.retro_trace_reset(); t0[0] = time.time(); M.run(1)
    print("no hang")
