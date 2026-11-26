import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Oracle 2: the stock game and the six-man ROM, from the same menu state (the Royal Rumble's superstar select,
# rr_presel.st), with the same input; the match loads in each (the six-man ROM's ovl_e from its ROM, its main
# code patched into RAM) and every watched array is compared, each at its place, every frame of the match.
#   oracle2.py stock|SIX_ROM [frames] [state]
import sys, os, pickle, struct
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
if os.environ.get("TP_ORACLE", "1") == "1": sys.argv.append("--oracle")
import n64
which = sys.argv[1]
frames = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] != '--oracle' else 1500
ST = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '--oracle' else nmp("/home/devbox/work/states/rr_vs_stock.st")
TAG = os.environ.get("TP_TAG", "")
import build_rom, spec
from rompatch import secs
n64.OPTIONS[b"parallel-n64-cpucore"] = os.environ.get("O2_CORE", "dynamic_recompiler").encode()
rom = nmp("/home/devbox/work/nomercy.z64") if which == "stock" else which
M = n64.N64(rom, nmp("/home/devbox/work/sys_o2%s%s") % (which == "stock" and "A" or "B", TAG)); M.run(1)
M.load(pickle.load(open(ST, "rb")))
# the state must be the stock game's own (no test-build hooks: j/jal into 0x807xxxxx)
_hk = [a for a in range(0x80000450, 0x80040000, 4) if (M.r32(a) >> 26) in (2, 3) and 0x700000 <= ((M.r32(a) & 0x3FFFFFF) << 2) < 0x800000]
assert not _hk, "state has hooks at %s" % [hex(a) for a in _hk[:4]]
if which != "stock":
    stock = open(nmp("/home/devbox/work/nomercy.z64"), "rb").read(); six = open(rom, "rb").read()
    s = secs["main_1050"]; n = 0
    for o in range(0, 0x56670 - 0x450, 4):       # main code + data as loaded at boot
        a, b = stock[s["rom"] + o:s["rom"] + o + 4], six[s["rom"] + o:s["rom"] + o + 4]
        if a != b: M.w32(s["vram"] + o, struct.unpack(">I", b)[0]); n += 1
    print("main words", n)
    # the moved ovl_e arrays: BSS, zeroed when ovl_e loads (the full build's grown BSS does it; here, before)
    for lo, hi, new in build_rom.RELOC:
        o = new & 0x7FFFFF
        M.ram[o:o + 6 * (hi - lo) // 4] = bytes(6 * (hi - lo) // 4)
M.load(M.save())      # (both games: a state round trip changes the emulator's timing, so both take it)
moved = {lo: new for lo, hi, new in build_rom.RELOC}
WATCH = [(b, 4 * s, moved.get(b, b) if which != "stock" else b) for b, s, _ in spec.OVLE + spec.MAIN]
_romb = open(rom, "rb").read()
_ovle = {o: struct.unpack(">I", _romb[secs["ovl_e"]["rom"] + o:secs["ovl_e"]["rom"] + o + 4])[0] for o in (0x100, 0x1000, 0x8000, 0x20000, 0x40000, 0x60000)}
M.press("A", hold=6, after=30)
f0 = None; rec = []
for f in range(20000):
    M.run(1)
    if all(M.r32(0x800D9960 + o) == _ovle[o] for o in _ovle):     # from the first frame ovl_e is loaded
        if f0 is None: f0 = f
        rec.append([bytes(M.ram[(a & 0x7FFFFF):(a & 0x7FFFFF) + n]) for _, n, a in WATCH])
        if len(rec) >= frames: break
n64.fbshot(M, nmp("/tmp/devbox-0/o2_%s%s.png") % (which == "stock" and "stock" or "six", TAG))
pickle.dump((f0, rec), open(nmp("/tmp/devbox-0/o2_%s%s.pkl") % (which == "stock" and "stock" or "six", TAG), "wb"))
print("match from frame", f0, "recorded", len(rec))
if which != "stock":
    fa, ra = pickle.load(open(nmp("/tmp/devbox-0/o2_stock%s.pkl") % TAG, "rb"))
    print("start frames", fa, f0)
    bad = 0
    import numpy as np
    def unmove(y):        # a pointer into a moved array, in the six-man game, is the same pointer moved back
        if len(y) % 4: return y
        w = np.frombuffer(y, dtype='<u4').copy()
        for lo, hi, nw in build_rom.RELOC:
            sel = (w >= nw) & (w < nw + 6 * (hi - lo) // 4)
            w[sel] = w[sel] - nw + lo
        return w.tobytes()
    for i in range(min(len(ra), len(rec))):
        for k, (x, y) in enumerate(zip(ra[i], rec[i])):
            if x != y: y = unmove(y)
            if x != y:
                offs = sorted(set((j ^ 3) for j in range(len(x)) if x[j] != y[j]))
                print("match frame %d: array %08x (%s): %d bytes differ: %s" % (i, WATCH[k][0], (spec.OVLE + spec.MAIN)[k][2], len(offs), [hex(o) for o in offs[:8]]))
                bad += 1
        if bad > 8: break
    print("compared %d frames: %s" % (min(len(ra), len(rec)), "IDENTICAL" if not bad else "DIFFER"))
