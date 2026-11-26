import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Compares two transplant recordings: per frame a list of byte blocks (the watched arrays).
import sys, pickle
sys.path.insert(0, nmp('/home/devbox/work/six')); sys.argv.append('--oracle')
import build_rom
a = pickle.load(open(sys.argv[1], 'rb')); b = pickle.load(open(sys.argv[2], 'rb'))
import spec
W = [(b, 4 * s) for b, s, _ in spec.OVLE + spec.MAIN]
n = min(len(a), len(b)); bad = 0
for f in range(n):
    for k, (x, y) in enumerate(zip(a[f], b[f])):
        if x != y:
            offs = sorted(set((i ^ 3) for i in range(len(x)) if x[i] != y[i]))
            print('frame %d: array %08x: %d bytes differ, first offsets %s' % (f, W[k][0], len(offs), [hex(o) for o in offs[:8]]))
            bad += 1
    if bad > 6: break
print('compared %d frames: %s' % (n, 'IDENTICAL' if not bad else 'DIFFER'))
