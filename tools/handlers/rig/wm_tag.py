# wm_tag.py (wm2kall, 2026-11-13): four WrestleMania 2000 Superstars' models in one match -- its Tag Match, 1P 2P vs 3P 4P,
# from its Superstar Select (cap\<state>.st: the four cursors on its first entry). Each pick is made with that entry's
# record pointer (the roster's table, RAM 0x800411B0) set to the man wanted just before it, and the costume the model
# loader makes forced to c for every slot (0x800146CC: lui $a1, c -- data\wmport\wmcap.py's poke, one costume a match).
# Into the match; when all four slots' models are in, the RAM image is kept: cap\<tag>_ram.bin (+ shots).
#   python wm_tag.py <state> <tag> <costume> k1 k2 k3 k4
import os, sys, struct, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wm

ROSTER, ENTRY = 0x800411B0, 1          # (the list's first entry, Steve Austin, is record 1)
WM_DESC, WM_DESC_SIZE = 0x8009DF90, 0x258
PADS = (0, 0, 2, 3)                    # (who makes each pick: 1P picks for 1P and 2P, then 3P, 4P -- seen on its select)

def picks(M, men, c, tag, shots=True):
    ptr = [M.r32(ROSTER + 4 * k) for k in range(0x4C)]
    M.w32(0x800146CC, 0x3C050000 | c); M.load(M.save())
    for i, (pad, k) in enumerate(zip(PADS, men)):
        M.w32(ROSTER + 4 * ENTRY, ptr[k])
        M.press("A", port=pad, hold=5, after=60)
    M.w32(ROSTER + 4 * ENTRY, ptr[ENTRY])
    if shots: wm.step(M, "shot:%s_decision" % tag)

if __name__ == "__main__":
    t0 = time.time()
    state, tag, c = sys.argv[1], sys.argv[2], int(sys.argv[3])
    men = [int(x) for x in sys.argv[4:8]]
    M = wm.session(state)
    picks(M, men, c, tag)
    for st in sys.argv[8:]: wm.step(M, st)
    print("frames", M.frames, "%.1f s" % (time.time() - t0))
