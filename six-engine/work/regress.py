import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Regression oracle for the six-man engine: the same single-player Royal Rumble (4 in the ring), from power-on,
# on a ROM; every frame the four wrestler structs are recorded. Two ROMs that only move data must give the same
# match.  usage: regress.py ROM OUT.pkl [frames]
import sys, os, pickle, hashlib
sys.path.insert(0, nmp("/home/devbox/work/harness"))
rom, out = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1200
os.environ["NM_ROM"] = rom
from rig import Rig
from n64 import fbshot
R = Rig(cpu=b"dynamic_recompiler", workdir=nmp("/home/devbox/work/sys_rig_") + os.path.basename(out)); M = R.m
R.config(6, 8)
def scr(): return M.s16(0x800457A4)
def mode(): return M.s32(0x800457A0)
def inj(bit, wait=30):
    M.w32(R.sym("dbg_inject"), bit); M.w32(R.sym("dbg_inject_frames"), 4); M.run(wait)
M.run(1500); print("booted", R.state(), flush=True); M.w32(R.sym("credits"), 1); M.run(5); R.key("1", frames=20)
n = 0
while not (scr() == 22 and M.s32(R.sym("scr_stable")) > 30):
    M.run(5); n += 1
    if n > 2000: print("stuck list"); sys.exit(1)
M.w8(0x80158360 + 22 * 0x138 + 5, 7); M.run(10); M.press("A", hold=4, after=30)
n = 0
while scr() not in (26, 27):
    M.run(5); n += 1
    if n > 2000: print("stuck select", mode(), scr()); sys.exit(1)
print("select", flush=True)
M.run(80); M.press("A", hold=6, after=30)
for f in range(12000):
    M.run(1)
    if mode() == -2 and M.r16(0x801532C0) != 0xFFFF and 2 <= M.r16(0x801532C0) <= 3: break
print("bell at", f, mode(), flush=True)
pickle.dump(M.save(), open(out + ".bell.st", "wb"))
TB, WS = 0x80159990, 0x358
rec = []
for f in range(frames):
    M.run(1)
    o = TB & 0x7FFFFF
    rec.append(bytes(M.ram[o:o + 4 * WS]))     # (the emulator's word-swapped bytes: compared as they are)
    if f % 300 == 0: fbshot(M, out + "_%d.png" % f)
pickle.dump(rec, open(out, "wb"))
print("done", len(rec), hashlib.md5(b"".join(rec)).hexdigest())
