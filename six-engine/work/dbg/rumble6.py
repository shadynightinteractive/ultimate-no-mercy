import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# From a six-man match state: run, and print each slot's entrance-relevant fields whenever they change
#   rumble6.py ROM STATE frames [every]
import sys, os, pickle
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
import n64
rom, st, N = sys.argv[1], sys.argv[2], int(sys.argv[3]); EV = int(sys.argv[4]) if len(sys.argv) > 4 else 30
M = n64.N64(rom, nmp("/home/devbox/work/sys_r6")); M.run(1); M.load(pickle.load(open(st, "rb")))
sys.argv = [sys.argv[0]]
import build_rom
NEW = {b: n for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW}
def arr(old, stride, i, off=0): return NEW[old] + i * stride + off
W = lambda i, off: arr(0x80159990, 0x358, i, off)
last = None
PRESS = int(os.environ.get("R6_PRESS", "0"))     # press A every PRESS frames (the replay loop after the match)
for f in range(N):
    if PRESS and f % PRESS == PRESS - 1: M.press("A", hold=4, after=0)
    M.run(1)
    if f % EV: continue
    row = []
    for i in range(6):
        row.append("s%d id %d st %x/%x 82 %x 94 %x F1 %x E9 %x D8 %x ent %d | mreq %x mld %x mst %x" % (
            i, M.s16(W(i, 0xA0)), M.r32(W(i, 0x14)), M.r32(W(i, 0x18)), M.r16(W(i, 0x82)), M.r16(W(i, 0x94)), M.r8(W(i, 0xF1)), M.r8(W(i, 0xE9)),
            M.r16(W(i, 0xD8)), M.r8(W(i, 0xF8)), M.r16(arr(0x80076270, 2, i)), M.r16(arr(0x80076264, 2, i)), M.r8(arr(0x80076278, 1, i))))
    g = "entered %d/%d 8016B650 %04x 80162A7C %08x res %08x" % (M.s32(0x80159214), M.s32(0x80159210), M.r16(0x8016B650), M.r32(0x80162A7C), M.r32(0x800A10EC))
    cur = row + [g]
    if cur != last:
        print("frame", f, "match frame %04x" % M.r16(0x801532C0), g)
        for k, r in enumerate(row):
            if last is None or r != last[k]: print("   ", r)
        sys.stdout.flush()
    last = cur
    if os.environ.get("R6_SAVE") and f and f % 15000 == 0: pickle.dump(M.save(), open(os.environ["R6_SAVE"] % f if "%" in os.environ["R6_SAVE"] else os.environ["R6_SAVE"], "wb")); print("saved at", f, flush=True)
    SHOT = os.environ.get("R6_SHOT", nmp("/tmp/devbox-0/r6_"))
    if f and f % int(os.environ.get("R6_SHOTN", "1500")) == 0: n64.fbshot(M, SHOT + "%05d.png" % f)
    if os.environ.get("R6_END"):          # stop R6_END frames after the match overlay is gone (results/menus)
        sc = M.s16(0x800457A4)
        if f % 300 == 0: print("  scr", sc, "mode", M.s32(0x800457A0), flush=True)
        if sc != 42 and 'endf' not in dir(): endf = f; print("match over at", f, "scr", sc, flush=True); n64.fbshot(M, SHOT + "end.png")
        if 'endf' in dir() and f - endf >= int(os.environ["R6_END"]): n64.fbshot(M, SHOT + "after.png"); print("done", f); break
