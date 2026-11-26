"""WrestleMania 2000 moves -> No Mercy's by their animations.

A move m's record in the match's tables (No Mercy: files 0x1F0 / 0x1F1 at 0x8015A700 / 0x80162A90, as
s16 [m + k], k 0..9; WM2000: 0x3AE / 0x3AF) names its animations: animation file = value + 0x3322 in No
Mercy (ovl_e func_80130878; the animations are files 0x2DD9-0x4473), + 0x19DB in WM2000 (files
0x1609-0x2847). Most of WM2000's animation files are in No Mercy byte for byte (No Mercy kept even the
ones of Superstars it dropped, Jeff Jarrett's among them): a WM2000 move whose animations all are,
in the same order, is the No Mercy move with those animations, whatever No Mercy calls it.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import struct, pickle, collections
nm_h, wm_h = pickle.load(open(nmp("/tmp/devbox-0/wm2k_work/hashes.pkl"), "rb"))
by_hash = {}
for k, (h, n, m) in sorted(nm_h.items()):
    if 0x2DD9 <= k <= 0x4473: by_hash.setdefault(h, k)
def recs(path): b = open(path, "rb").read(); return [struct.unpack_from(">h", b, o)[0] for o in range(0, len(b), 2)]
NM1, NM2 = recs(nmp("/tmp/devbox-0/nmfiles/f/01f0.bin")), recs(nmp("/tmp/devbox-0/nmfiles/f/01f1.bin"))
WM1, WM2 = recs(nmp("/tmp/devbox-0/wm2k_files/03ae.bin")), recs(nmp("/tmp/devbox-0/wm2k_files/03af.bin"))
def nm_anim(v):
    """a WM2000 animation value -> No Mercy's (None: No Mercy hasn't that animation)"""
    if v <= 0: return v
    f = v + 0x19DB
    if f not in wm_h: return None
    g = by_hash.get(wm_h[f][0])
    return None if g is None else g - 0x3322
index = collections.defaultdict(list)
for m in range(0, len(NM1) - 9, 10):
    index[tuple(NM1[m:m + 10])].append(m)
def nm_move(m):
    """WM2000 move m (a multiple of 10 plus its sub-move) -> No Mercy's moves with the same animations"""
    base = m - m % 10
    t = tuple(nm_anim(v) for v in WM1[base:base + 10])
    if None in t or not any(x > 0 for x in t): return [], t
    return [x + m % 10 for x in index.get(t, [])], t
if __name__ == "__main__":
    import sys
    sys.path.insert(0, nmp("/home/devbox/work/arcade/gen"))
    import wm2k
    for m in (1790, 350, 1360, 620, 3350, 11150, 11580, 11920, 8650, 9180, 9310, 9430, 9450, 5470, 6510, 6340, 6360, 7090, 7120, 7270, 7720, 13040):
        r, t = nm_move(m)
        print(m, wm2k.WM_NAMES.get(m), "->", [(x, wm2k.NM_NAMES.get(x)) for x in r], t if not r else "")
