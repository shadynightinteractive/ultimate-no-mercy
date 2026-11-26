import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# slotscan.py's dump: accesses that step per slot (by the current slot 0x80159080, or as 6 equal steps in one pc's
# addresses) and reach slots 4/5 OUTSIDE the six-man engine's places for per-slot data (the moved arrays' new places,
# the engine's blocks, the blob) -- an array that should have moved with the others.
#   slotscan_an.py DUMP
import sys, pickle
sys.argv, dump = ['x'], sys.argv[1]
sys.path.insert(0, nmp('/home/devbox/work/six'))
import build_rom
NEWR = [(n, n + 6 * s, w) for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW]
OLDR = [(b, b + 4 * s, w) for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW]
def inr(a, R):
    for lo, hi, w in R:
        if lo <= a < hi: return w
    return None
def ok(a):
    return inr(a, NEWR) or a >= 0x80780000 or 0x80170000 <= a < 0x80176000   # (the blob / engine blocks)
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
F = {}
for s, f, a, asm in insns:
    if s in ('ovl_e', 'main_1050'): F[a] = (f, asm)
out = []
for l in open(dump):
    p = l.split()
    pc, n = int(p[0], 16), int(p[5])
    addrs = [int(x, 16) for x in p[6:6 + min(n, 12)]]
    smin = {}
    if '|S' in l:
        i = p.index([x for x in p if x.startswith('|S')][0]); mask = int(p[i][2:], 16); k = i + 1
        for b in range(8):
            if mask >> b & 1: smin[b] = int(p[k].split('-')[0], 16); k += 1
    why = None
    if all(b in smin for b in range(6)):
        d = smin[1] - smin[0]
        if d and all(smin[b] == smin[0] + b * d for b in range(6)) and (not ok(smin[4]) or not ok(smin[5])):
            why = 'slot step 0x%x: slot4 %08x (%s) slot5 %08x' % (d, smin[4], inr(smin[4], OLDR) or '-', smin[5])
    if not why and len(addrs) >= 6:
        s_ = sorted(set(addrs))
        for j in range(len(s_) - 5):
            d = s_[j + 1] - s_[j]
            if d and d <= 0x1000 and all(s_[j + k] == s_[j] + k * d for k in range(6)) and not ok(s_[j + 4]) and not ok(s_[j]):
                why = 'six steps 0x%x from %08x' % (d, s_[j]); break
    if why: out.append((pc, why))
for pc, why in out:
    f, asm = F.get(pc, ('?', '?'))
    print('%08x %-14s %-34s %s' % (pc, f, asm, why))
print(len(out), 'flagged')
