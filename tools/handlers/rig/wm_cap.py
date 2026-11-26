# wm_cap.py (wm2kall, 2026-11-13): WrestleMania 2000 Superstars in costumes of our choosing, four at a time, in its own
# Tag Match -- each slot's model as the game builds it, from the match's RAM.
#
# How (found in the rig, 2026-11-13): its Superstar Select keeps the four picks in two places, and the match loads from them --
#   0x8009B0C0 + 4 s   slot s's Superstar, his game ID (u32)
#   0x8009EACC + 12 s  slot s's entry: his game ID (u16), his costume (u8), the slot + 1 (u8), then ff 32 32 32 32 32 00 00
# So from its Decision screen (cap\tagdec.st: Exhibition, Tag Match, 1P 2P vs 3P 4P, the picks made), both are written for
# each slot -- any man, any of his four costumes, the same man more than once -- and the match is started. (Picking on the
# select with a record pointer swapped, data\wmport\wmcap.py's way, gives one man a match: the slots keep the list entry.)
# The match is played on as wmcap.py does (A presses, START twice) until the four are in the ring; the RAM image is kept.
#   python wm_cap.py <tag> <costume> id id id id      (ids as hex game IDs, e.g. 0406)
# -> cap\<tag>_ram.bin, cap\<tag>_vs.png (its own intro: the four, waist up), cap\<tag>_ring.png
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wm

PICK_IDS, PICK_ENTRIES = 0x8009B0C0, 0x8009EACC
START_STATE = "tagdec"

def set_picks(M, picks):
    """picks: [(game id, costume)] for slots 0-3"""
    for s, (gid, c) in enumerate(picks):
        M.w32(PICK_IDS + 4 * s, gid)
        M.w16(PICK_ENTRIES + 12 * s, gid)
        M.w8(PICK_ENTRIES + 12 * s + 2, c)
        M.w8(PICK_ENTRIES + 12 * s + 3, s + 1)

COSTUME_SITE = 0x800146CC              # (the model loader's costume: sll $a1, $s5, 16 -> lui $a1, c -- data\wmport\wmcap.py)

def capture(tag, picks, costume, vs_wait=400):
    """picks: four game IDs; costume: the one costume all four are built in (the entry's costume byte isn't what the
    model loader reads: four Jeff Jarretts given 0-3 there all came out in costume 0, == data\wmport\cap\k29_c0)"""
    M = wm.session(START_STATE)
    set_picks(M, [(g, costume) for g in picks])
    M.w32(COSTUME_SITE, 0x3C050000 | costume); M.load(M.save())     # (the core's cached code thrown away after the poke)
    M.press("A", hold=5, after=45)
    M.run(vs_wait)
    wm.step(M, "shot:%s_vs" % tag)
    for t in range(6):                 # (data\wmport\wmcap.py's presses: through the intro and the entrances, into the ring)
        for i in range(4): M.press("START" if t in (2, 4) and i == 0 else "A", hold=3, after=60)
    wm.step(M, "shot:%s_ring" % tag)
    wm.step(M, "ram:%s" % tag)
    return M

if __name__ == "__main__":
    t0 = time.time()
    tag, costume = sys.argv[1], int(sys.argv[2])
    capture(tag, [int(x, 16) for x in sys.argv[3:7]], costume)
    print("done", tag, "%.1f s" % (time.time() - t0))
