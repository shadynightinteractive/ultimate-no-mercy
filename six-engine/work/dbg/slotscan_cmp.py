import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Two slotscan.py dumps of the same ROM, a 4-man and a 6-man match: the pcs whose addresses run in equal steps
# 4 long in the 4-man match and longer in the 6-man one (a loop or index by the wrestler count), where the steps past
# the fourth land outside the engine's places for per-slot data -- an array that should have moved.
#   slotscan_cmp.py DUMP4 DUMP6
import sys, pickle
d4, d6 = sys.argv[1], sys.argv[2]
sys.argv = ['x']; sys.path.insert(0, nmp('/home/devbox/work/six'))
import build_rom
NEWR = [(n, n + 6 * s, w) for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW]
OLDR = [(b, b + 4 * s, w) for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW]
def inr(a, R):
    for lo, hi, w in R:
        if lo <= a < hi: return w
    return None
def ok(a): return inr(a, NEWR) or a >= 0x80780000 or 0x80170000 <= a < 0x80176000
def load(p):
    D = {}
    for l in open(p):
        q = l.split(); pc, n = int(q[0], 16), int(q[5])
        if n > 12: continue                     # (more than the dump keeps: not a per-slot pattern)
        A = sorted(set(int(x, 16) for x in q[6:6 + n]))
        smin = {}
        if '|S' in l:
            i = [k for k, x in enumerate(q) if x.startswith('|S')][0]; mask = int(q[i][2:], 16); k = i + 1
            for b in range(8):
                if mask >> b & 1: smin[b] = int(q[k].split('-')[0], 16); k += 1
        D[pc] = (A, smin)
    return D
def runs(A):
    """equal-step runs (base, step, length >= 3)"""
    out = []
    for i in range(len(A) - 2):
        d = A[i + 1] - A[i]
        if d <= 0 or d > 0x2000: continue
        if i and A[i] - A[i - 1] == d: continue
        k = 1
        while i + k + 1 < len(A) and A[i + k + 1] - A[i + k] == d: k += 1
        if k + 1 >= 3: out.append((A[i], d, k + 1))
    return out
# looked at and fine (2026-10-21): indexes that are not slots, or room that is there
KNOWN = {
    0x8000A69C: 'func_8000A690: the Rumble roster 0x800BD4D0 by entrant (40)', 0x8000A6B0: 'func_8000A690: roster costumes',
    0x8000AA60: 'func_8000AA24: the wrestler table 0x80097FE0 (0x114 each) by roster index', 0x8000AAE4: 'func_8000AA80: same',
    0x800248DC: 'func_80023E9C: each scene object\'s own heap block (+0x14), allocated per object', 0x800248EC: 'same',
    0x8002C2F0: 'func_8002BF70: a sound table by value',
    0x800E23BC: 'func_800E2344: stack list sp+0x10, 6 halfwords to sp+0x1B (sp+0x18..0x1F otherwise unused)',
    0x800E2434: 'same', 0x800E2510: 'same', 0x800E253C: 'same', 0x800E259C: 'same', 0x800E25A8: 'same',
    0x801210AC: 'func_80120D98: callers\' 12-word lists; 5 others fit, props capped by rw_prop_append',
    0x801210BC: 'same', 0x80121190: 'same',
    0x800FE92C: 'func_800FE7C8: a move-option table (8-byte records) up to a count by the move', 0x800FEA68: 'same',
    0x8010EFFC: 'func_8010EFB0: a jump table by state',
}
D4, D6 = load(d4), load(d6)
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
F = {a: (f, asm) for s, f, a, asm in insns if s in ('ovl_e', 'main_1050')}
hits = []
for pc, (A6, S6) in D6.items():
    if pc not in D4: continue
    A4, S4 = D4[pc]
    r4 = {(b, d): n for b, d, n in runs(A4)}
    for b, d, n in runs(A6):
        if n >= 5 and r4.get((b, d), 0) in (3, 4):
            bad = [b + k * d for k in range(r4[(b, d)], n) if not ok(b + k * d)]
            if bad: hits.append((pc, 'steps 0x%x from %08x: %d -> %d; outside: %s (%s)' % (d, b, r4[(b, d)], n, ' '.join('%08x' % x for x in bad), inr(bad[0], OLDR) or 'not a moved array')))
    if all(k in S6 for k in range(6)) and all(k in S4 for k in range(4)):
        d = S6[1] - S6[0]
        if d and all(S6[k] == S6[0] + k * d for k in range(6)) and all(S4[k] == S6[k] for k in range(4)):
            bad = [S6[k] for k in (4, 5) if not ok(S6[k])]
            if bad: hits.append((pc, 'by current slot, step 0x%x from %08x; slots 4/5 at %s (%s)' % (d, S6[0], ' '.join('%08x' % x for x in bad), inr(bad[0], OLDR) or 'not a moved array')))
hits = [h for h in hits if h[0] not in KNOWN]
for pc, why in sorted(hits):
    f, asm = F.get(pc, ('?', '?'))
    print('%08x %-14s %-34s %s' % (pc, f, asm, why))
print(len(hits), 'flagged')
