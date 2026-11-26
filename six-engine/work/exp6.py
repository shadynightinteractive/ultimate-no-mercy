import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# The six-man experiment: the six-man ROM (full layout, loops by $gp, stubs) from power-on, the rewrites' blob put in
# RAM, the menus to a Royal Rumble (8 entrants, Watch), rw_match_count = 6 for the match; screenshots and a hang watch.
#   exp6.py ROM [match_frames] [nslots]
import sys, os, time, struct, threading
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
if os.environ.get("EXP_OLD"): n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"   # (the access trace)
rom = sys.argv[1]; MF = int(sys.argv[2]) if len(sys.argv) > 2 else 3000; NS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
M = n64.N64(rom, nmp("/home/devbox/work/sys_exp6")); M.run(1)
BLOB = rom + ".blob" if os.path.exists(rom + ".blob.bin") else nmp("/home/devbox/work/six/rw/out/full/blob")   # (the ROM's own)
blob = open(BLOB + ".bin", "rb").read()
for k in range(0, len(blob), 4): M.w32(0x80780000 + k, struct.unpack(">I", blob[k:k + 4].ljust(4, b"\0"))[0])
M.load(M.save())
scr = lambda: M.s16(0x800457A4)
def wait(cond, limit=3000):
    for _ in range(limit):
        if cond(): return True
        M.run(1)
    return False
def press(b, after=20): M.press(b, hold=4, after=after)
OUT = os.environ.get("EXP_OUT", nmp("/tmp/devbox-0/exp6")); os.makedirs(OUT, exist_ok=True)
if os.environ.get("EXP_RING"):       # a Rumble's wrestlers in the ring (rw_rumble_ring: 6, or the stock 4)
    _a = {l.split()[2]: int(l.split()[0], 16) for l in open(BLOB + ".syms")}["rw_rumble_ring"]
    _w = M.r32(_a & ~3); _sh = (3 - (_a & 3)) * 8
    M.w32(_a & ~3, (_w & ~(0xFF << _sh)) | (int(os.environ["EXP_RING"]) << _sh)); print("rw_rumble_ring", os.environ["EXP_RING"], flush=True)
M.run(1500); press("START", 420); press("START", 200); press("A", 400)
assert wait(lambda: scr() == 2), "main menu? scr %d" % scr()
press("DOWN", 30); press("A", 200); assert wait(lambda: scr() == 7)
press("A", 60); assert wait(lambda: scr() == 9)
press("UP", 15); press("UP", 35); press("A", 60); assert wait(lambda: scr() == 6)
for _ in range(7): press("DOWN", 12)
M.run(10); press("A", 60); assert wait(lambda: scr() == 5)
press("A", 60); assert wait(lambda: scr() == 21)
press("START", 200); assert wait(lambda: scr() in (26, 27))
M.run(80); press("A", 60); press("RIGHT", 20); press("A", 60); press("RIGHT", 20); press("A", 60); press("DOWN", 20); press("A", 60)
assert wait(lambda: scr() == 25, 2000), "versus? scr %d" % scr()
M.run(600)
n64.fbshot(M, OUT + "/vs.png")
sys.argv = [sys.argv[0]]
import importlib; sys.path.insert(0, nmp("/home/devbox/work/six")); build_rom = importlib.import_module("build_rom")
NEW = {b: n for b, st, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW}
def arr(old, stride, i, off=0): return NEW[old] + i * stride + off
EXTRA_IDS = [int(x, 0) for x in os.environ.get("EXP_IDS", "7,8").split(",")]
MENU = os.environ.get("EXP_MENU")      # the menus fill slots 4, 5 and set the count themselves (rw/r_menu.c)
SYMS = {l.split()[2]: int(l.split()[0], 16) for l in open(BLOB + ".syms")}
print("entrants", M.s16(0x800A958C), "roster", [hex(M.r16(0x800BD4D0 + 2 * i)) for i in range(8)], "rw_match_count", M.r32(SYMS["rw_match_count"]), flush=True)
print("slots", [hex(M.r32(arr(0x800A4038, 4, i))) for i in range(6)], "costumes", [M.r32(arr(0x800A8958, 4, i)) for i in range(6)], "pads", [M.r32(arr(0x800A6028, 4, i)) for i in range(6)], "levels", [M.r16(arr(0x8009ED10, 2, i)) for i in range(6)], flush=True)
if NS > 4 and not MENU:
    for i, wid in zip(range(4, NS), EXTRA_IDS):
        M.w32(arr(0x800A4038, 4, i), wid)          # slot wrestler id
    print("slots 4.. given wrestler ids", EXTRA_IDS[:NS - 4], "; slot 0..3:", [hex(M.r32(arr(0x800A4038, 4, i))) for i in range(4)], flush=True)
if not MENU: M.w32(SYMS["rw_match_count"], NS); print("rw_match_count", M.r32(SYMS["rw_match_count"]), flush=True)
press("A", 30)
done = [0]; t0 = [time.time()]
def watch():
    while True:
        time.sleep(5)
        if time.time() - t0[0] > 20: print("HANG at match frame", done[0], flush=True); os._exit(2)
threading.Thread(target=watch, daemon=True).start()
f0 = None
for f in range(int(os.environ.get('EXP_PRE', '6000'))):
    M.run(1); t0[0] = time.time()
    if f0 is None and all(M.r32(0x800D9960 + o) == struct.unpack(">I", M.romdata[0x144be0 + o:0x144be0 + o + 4])[0] for o in (0x100, 0x1000, 0x8000, 0x20000, 0x40000, 0x60000)):
        f0 = f; print("match loaded at", f, "count", M.r8(0x80151ED7), flush=True)
        if os.environ.get("EXP_OLD"):     # who still uses a moved array's OLD place (a reference not relocated)
            import ctypes as C, spec
            R = sorted([(b, b + 4 * st) for b, st, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW] + [(0x8009FDF8, 0x8009FE58)])
            mg = []
            for lo, hi in R:
                if mg and lo <= mg[-1][1]: mg[-1][1] = max(mg[-1][1], hi)
                else: mg.append([lo, hi])
            assert len(mg) <= 128, len(mg)
            flat = (C.c_uint32 * (2 * len(mg)))(*[x for r in mg for x in r])
            M.lib.retro_trace_reset(); M.lib.retro_trace_ranges(flat, len(mg)); M.lib.retro_trace_enable(1)
            print("old-place watch:", len(mg), "ranges", flush=True)
        if os.environ.get("EXP_SAVE"):
            import pickle; pickle.dump(M.save(), open(os.environ["EXP_SAVE"], "wb")); print("saved", os.environ["EXP_SAVE"], flush=True)
    if f0 is None and f % 300 == 0: print("  ovl_e probe:", ["%x:%08x/%08x" % (o, M.r32(0x800D9960 + o), struct.unpack(">I", M.romdata[0x144be0 + o:0x144be0 + o + 4])[0]) for o in (0x100, 0x1000, 0x8000, 0x20000, 0x40000, 0x60000)], flush=True)
    if f0 is None and f % 300 == 0: n64.fbshot(M, OUT + "/pre%05d.png" % f); print("pre", f, "scr", scr(), "mode", M.s32(0x800457A0), flush=True)
    if f0 is not None:
        done[0] = f - f0
        if done[0] % 300 == 0:
            n64.fbshot(M, OUT + "/m%05d.png" % done[0]); print("frame", done[0], "count", M.r8(0x80151ED7), "log", M.nlog, "match frame %04x" % M.r16(0x801532C0), "retrace", M.r32(0x80057404), flush=True)
            print("   ids", [M.s16(arr(0x80159990, 0x358, i, 0xA0)) for i in range(6)], "entrant", [M.r8(arr(0x80159990, 0x358, i, 0xF8)) for i in range(6)], "+94", ["%x" % M.r16(arr(0x80159990, 0x358, i, 0x94)) for i in range(6)], "entered", M.s32(0x80159214), "of", M.s32(0x80159210), "result %08x" % M.r32(0x800A10EC), "scr", scr(), "mode", M.s32(0x800457A0), flush=True)
        if os.environ.get("EXP_OLD") and done[0] % 60 == 0 and not all(M.r32(0x800D9960 + o) == struct.unpack(">I", M.romdata[0x144be0 + o:0x144be0 + o + 4])[0] for o in (0x100, 0x1000, 0x8000)):
            M.lib.retro_trace_enable(0); print("ovl_e left at match frame", done[0], "(old-place watch off)", flush=True)
        if done[0] >= MF: break
for i in range(6):
    w = lambda off: arr(0x80159990, 0x358, i, off)
    print("slot", i, "id(+A0) %d model(+9E) %d +94 %04x +E9 %02x | slot wrestler id %08x costume %08x pad %08x | model loaded id %04x req %04x state %02x" % (
        M.s16(w(0xA0)), M.s16(w(0x9E)), M.r16(w(0x94)), M.r8(w(0xE9)), M.r32(arr(0x800A4038, 4, i)), M.r32(arr(0x800A8958, 4, i)),
        M.r32(arr(0x800A6028, 4, i)), M.r16(arr(0x80076264, 2, i)), M.r16(arr(0x80076270, 2, i)), M.r8(arr(0x80076278, 1, i))))
print("mode", M.s32(0x800457A0), "scr", M.s16(0x800457A4), "match flags %08x" % M.r32(0x80097EC0))
print("done", done[0])
if os.environ.get("EXP_OLD"):
    M.lib.retro_trace_enable(0); M.lib.retro_trace_dump(os.environ["EXP_OLD"].encode()); print("old-place accesses ->", os.environ["EXP_OLD"])
os._exit(0)
