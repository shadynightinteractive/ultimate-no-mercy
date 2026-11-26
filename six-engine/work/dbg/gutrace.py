import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# A six-man match state: each slot's state/substate (+0x14/+0x18), anim id (+0xA6), anim left (+0xA2), flags +0x8A
# (0x40 lying) -- printed whenever state, substate or anim id change; screenshots every GU_SHOT frames
#   gutrace.py ROM STATE frames [slots]
import sys, os, pickle
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
import n64
rom, st, N = sys.argv[1], sys.argv[2], int(sys.argv[3])
SL = [int(x) for x in sys.argv[4].split(',')] if len(sys.argv) > 4 else list(range(6))
M = n64.N64(rom, nmp("/home/devbox/work/sys_r6")); M.run(1); M.load(pickle.load(open(st, "rb")))
sys.argv = [sys.argv[0]]
import build_rom
NEW = {b: n for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW}
B = NEW[0x80159990]
W = lambda i, off: B + i * 0x358 + off
EXTRA = [int(x, 16) for x in os.environ.get("GU_F", "").split(',') if x]
last = {}
for f in range(N):
    M.run(1)
    for i in SL:
        k = (M.r32(W(i, 0x14)), M.r32(W(i, 0x18)), M.r16(W(i, 0xA6)))
        if last.get(i) != k:
            print("f%5d mf %5d s%d st %x/%x anim %x left %d 8a %02x 94 %x tgt %d" % (f, M.r16(0x801532C0), i, k[0], k[1], k[2],
                  M.s16(W(i, 0xA2)), M.r8(W(i, 0x8A)), M.r16(W(i, 0x94)), M.s32(W(i, 0x8))) +
                  ''.join(' %x=%x' % (o, M.r16(W(i, o))) for o in EXTRA), flush=True)
            last[i] = k
    if os.environ.get("GU_SHOT") and f % int(os.environ["GU_SHOT"]) == 0: n64.fbshot(M, nmp("/tmp/devbox-0/gu/%05d.png") % f)
