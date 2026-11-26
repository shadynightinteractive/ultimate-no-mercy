import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# How far the game really goes into an array: the PCs that statically reference [base, base+stride) (refs.pkl),
# and the highest address each touched in a trace (emulator access trace: pc cnt w lo hi ...).
import sys, pickle, collections
refs = pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'), 'rb'))
def load_trace(paths):
    T = {}
    for p in paths:
        for line in open(p):
            q = line.split(); pc = int(q[0], 16); lo, hi = int(q[3], 16), int(q[4], 16)
            a = T.get(pc); T[pc] = (min(lo, a[0]), max(hi, a[1])) if a else (lo, hi)
    return T
def extent(base, stride, T):
    pcs = set(r[3] for r in refs if base <= r[0] < base + stride and r[6] in ('direct', 'indexed'))
    hi = None; n = 0
    for pc in pcs:
        if pc in T:
            n += 1; h = T[pc][1]
            if hi is None or h > hi: hi = h
    if hi is None: return None, 0, len(pcs)
    return (hi - base) // stride + 1, n, len(pcs)
if __name__ == '__main__':
    T = load_trace(sys.argv[3:] or [nmp('/tmp/devbox-0/tr_t3.txt')])
    print(extent(int(sys.argv[1], 16), int(sys.argv[2], 16), T))
