import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Transplant oracle: the same match state, run by the stock game and by the six-man ROM's code.
# The stock state (a savestate) is loaded into both emulators; in the six-man one the ROM's changed
# instruction words are written over the code in RAM and the moved data is moved (build_rom.RELOC), then
# both run the same frames with the same (no) input, and the wrestler structs are compared each frame.
#   usage: transplant.py STOCK_STATE SIX_ROM [frames]
import sys, os, pickle, struct
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
import n64
st_path, six_rom = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 600
if os.environ.get("TP_ORACLE", "1") == "1": sys.argv.append("--oracle")
import build_rom
from rompatch import secs
n64.OPTIONS[b"parallel-n64-cpucore"] = b"dynamic_recompiler"
st = pickle.load(open(st_path, "rb"))
TB, WS = 0x80159990, 0x358
TAGF = lambda k: nmp("/tmp/devbox-0/tp_%s%s.pkl") % (k, os.environ.get("TP_TAG", ""))
import build_rom
# what's compared each frame: the wrestlers and every moved array (at its place in each game)
# (the same list whatever this ROM moves: an array it leaves in place is read in place)
import spec
moved = {lo: new for lo, hi, new in build_rom.RELOC}
WATCH = [(b, 4 * s, moved.get(b, b)) for b, s, _ in spec.OVLE + spec.MAIN]
def record(M, path, six=False):
    rec = []
    for f in range(frames):
        M.run(1)
        rec.append([bytes(M.ram[((new if six else lo) & 0x7FFFFF):((new if six else lo) & 0x7FFFFF) + n]) for lo, n, new in WATCH])
    pickle.dump(rec, open(path, "wb"))
    n64.fbshot(M, path + ".png")
if six_rom == "stock":         # (a process of its own: one emulator per process)
    A = n64.N64(nmp("/home/devbox/work/nomercy.z64"), nmp("/home/devbox/work/sys_tA") + os.environ.get("TP_TAG", "")); A.run(1); A.load(st)
    record(A, TAGF("stock")); sys.exit(0)
B = n64.N64(six_rom, nmp("/home/devbox/work/sys_tB") + os.environ.get("TP_TAG", "")); B.run(1); B.load(st)
stock = open(nmp("/home/devbox/work/nomercy.z64"), "rb").read(); six = open(six_rom, "rb").read()
# code: the words the six-man ROM changed, in the sections in RAM now
nw = 0
for name in ("main_1050", "ovl_e"):
    s = secs[name]
    if name == "ovl_e" and B.r32(s["vram"]) != struct.unpack(">I", stock[s["rom"]:s["rom"] + 4])[0]:
        print("ovl_e isn't loaded"); sys.exit(1)
    for o in range(0, s["size"], 4):
        a, b = stock[s["rom"] + o:s["rom"] + o + 4], six[s["rom"] + o:s["rom"] + o + 4]
        if a != b:
            ram = B.r32(s["vram"] + o)
            if ram != struct.unpack(">I", a)[0]: print("RAM %08x isn't the ROM's (%08x)" % (s["vram"] + o, ram))
            B.w32(s["vram"] + o, struct.unpack(">I", b)[0]); nw += 1
print("code words", nw)
# data: moved (old place poisoned: a reference the ROM didn't move reads nonsense and shows), and every word in RAM
# that points into a moved array is moved with it (the game keeps pointers to its wrestlers, its AI records, ...)
import numpy as np
ram = np.frombuffer(bytes(B.ram), dtype='<u4').copy()        # (the emulator's RAM: little-endian words)
moves = [] if os.environ.get("TP_NODATA") else build_rom.RELOC
newram = ram.copy()
nptr = 0
for lo, hi, new in moves:
    sel = (ram >= lo) & (ram < hi)
    nptr += int(sel.sum())
    newram[sel] = ram[sel] - lo + new
for lo, hi, new in moves:
    o, n = (lo & 0x7FFFFF) // 4, (hi - lo) // 4
    blk = newram[o:o + n].copy()
    newram[o:o + n] = 0x7F7F7F7F
    d = (new & 0x7FFFFF) // 4
    newram[d:d + n] = blk
print("moved %d arrays, %d pointers" % (len(moves), nptr))
B.ram[:] = newram.tobytes()
B.load(B.save())
record(B, TAGF("six"), six=True)
import subprocess
print(subprocess.run([sys.executable, nmp("/home/devbox/work/six/cmp.py"), TAGF("stock"), TAGF("six")], capture_output=True, text=True).stdout)
