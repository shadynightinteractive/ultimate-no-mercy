import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Candidate instructions in a function that tie it to 4 slots: compares/branches/sets with 3 or 4, and constants
# that are 3 or 4 times a known per-slot stride (loop ends over byte offsets).
import sys, re, pickle, collections
sys.path.insert(0, nmp('/home/devbox/work/six'))
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
import spec
STRIDES = sorted(set(s for _, s, _ in spec.OVLE + spec.MAIN if s >= 2))
F = collections.defaultdict(list)
for s, f, a, asm in insns: F[f].append((a, asm, s))
def ival(x):
    try: return int(x, 0)
    except ValueError: return None
def sites(fn):
    out = []
    for a, asm, sec in sorted(F[fn]):
        p = asm.split(None, 1); op = p[0]; args = [x.strip() for x in p[1].split(',')] if len(p) > 1 else []
        imm = ival(args[-1]) if args else None
        if imm is None: continue
        why = None
        if op in ('slti', 'sltiu') and imm in (3, 4, 5): why = 'compare %d' % imm
        elif op in ('addiu', 'ori') and args[1] == '$zero' and imm in (3, 4): why = 'const %d' % imm
        else:
            for st in STRIDES:
                for k in (3, 4):
                    if imm == k * st and st >= 0x8: why = '%d x stride %x' % (k, st)
        if why: out.append((a, sec, asm, why))
    return out
if __name__ == '__main__':
    for fn in sys.argv[1:]:
        for a, sec, asm, why in sites(fn): print('%s %s %08x  %-36s ; %s' % (fn, sec, a, asm, why))
