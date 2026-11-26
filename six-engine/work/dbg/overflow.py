import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# overflow.py MAP4 MAP6 [--oracle] : pcs that reach further in the six-man game than in the four-man one, outside
# the moved arrays' new places (slots 4 and 5 there are fine) and the heap/expansion RAM: unmoved per-slot data
import sys
sys.path.insert(0, nmp("/home/devbox/work/six"))
ORACLE = "--oracle" in sys.argv
sys.argv = [a for a in sys.argv if a != "--oracle"] + (["--oracle"] if ORACLE else [])
m4, m6 = sys.argv[1], sys.argv[2]
sys_argv = sys.argv; sys.argv = [sys.argv[0]] + (["--oracle"] if ORACLE else [])
import build_rom, spec
def load(p):
    d = {}
    for l in open(p):
        f = l.split(); pc = int(f[0], 16); n = int(f[5])
        d[pc] = (int(f[1]), int(f[2]), int(f[3], 16), int(f[4], 16), [int(x, 16) for x in f[6:6 + min(n, 12)] if not x.startswith("|")])
    return d
A, B = load(m4), load(m6)
NEW = [(n, n + 6 * 4 * 0 + 6 * s) for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW]
EXTRA = [(a, a + 0x20000) for a in build_rom.EXTRA.values()]
def allowed(a):
    if a >= 0x80176000: return True                 # (heap, expansion RAM, framebuffers, blob)
    if any(lo <= a < hi for lo, hi in NEW + EXTRA): return True
    if 0x80056820 <= a < 0x8008F8F8 and False: return True
    return False
import pickle
secs, fn2sec, insns = pickle.load(open(nmp("/home/devbox/work/six/db.pkl"), "rb"))
def fn(pc):
    for k, v in fn2sec.items():
        if v[1] <= pc < v[1] + v[2] and v[0] in ("ovl_e", "main_1050"): return k
    return "?"
rows = []
for pc, (c, w, lo, hi, ads) in B.items():
    if pc in A:
        c4, w4, lo4, hi4, ads4 = A[pc]
        ext = [a for a in ([lo, hi] + ads) if (a > hi4 or a < lo4) and not allowed(a)]
    else:
        ext = [a for a in ([lo, hi] + ads) if not allowed(a)] if w else []
    if ext: rows.append((pc, fn(pc), w, ["%08x" % a for a in sorted(set(ext))][:6], "4-man %08x..%08x" % (A[pc][2], A[pc][3]) if pc in A else "six only"))
for r in sorted(rows): print("%08x %-14s writes %-6d beyond: %s  (%s)" % r)
print(len(rows), "pcs")
