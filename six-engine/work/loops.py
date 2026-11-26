import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# The loop-count patches from the review (kb/review/loops*.out PATCH lines): checked against the instruction
# database and applied as 16-bit immediate changes.
import glob, pickle, collections
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
BY = collections.defaultdict(list)
for s, f, a, asm in insns: BY[a].append((s, f, asm))
def norm(x): return ' '.join(x.split())
def patches():
    out = {}; problems = []
    for p in sorted(glob.glob(nmp('/home/devbox/work/six/kb/review/loops*.out'))):
        for line in open(p):
            f = [x.strip() for x in line.rstrip('\n').split(' | ')]
            if f[0] != 'PATCH' or len(f) < 5: continue
            fn, addr, old, new = f[1], int(f[2], 16), norm(f[3]), int(f[4], 16)
            cands = [(s, ff, asm) for s, ff, asm in BY[addr] if norm(asm) == old]
            if not cands: problems.append(('no such instruction', fn, hex(addr), old)); continue
            sec = cands[0][0]
            k = (sec, addr)
            if k in out and out[k][0] != new: problems.append(('two values', fn, hex(addr), out[k][0], new)); continue
            out[k] = (new, fn, f[5] if len(f) > 5 else '')
    return out, problems
def apply(rom, skip=()):
    ps, problems = patches()
    n = 0
    for (sec, addr), (new, fn, why) in sorted(ps.items()):
        if fn in skip: continue
        rom.set_imm16(addr, sec, new, 'loop ' + fn); n += 1
    return n, problems
if __name__ == '__main__':
    ps, pr = patches()
    print(len(ps), 'patches;', len(pr), 'problems')
    for x in pr: print(x)

# ---- the same loops bounded by the match's wrestler count instead of a fixed 6 ------------------------------------
# The game never uses $gp (only libultra's context save/restore touches it), so the six-man build keeps the count
# in $gp: set once at boot (4), to 6 for a six-man match, and not restored per thread (a global register). A loop's
# `slti/sltiu rt, rs, 4` becomes `slt/sltu rt, rs, $gp`, `addiu rt, $zero, 4` becomes `addu rt, $zero, $gp`, and a
# count-down start `addiu rt, $zero, 3` becomes `addiu rt, $gp, -1`: a four-man match runs exactly as before.
GP = 28
REG = {'$zero': 0, '$at': 1, '$v0': 2, '$v1': 3, '$a0': 4, '$a1': 5, '$a2': 6, '$a3': 7, '$t0': 8, '$t1': 9, '$t2': 10,
       '$t3': 11, '$t4': 12, '$t5': 13, '$t6': 14, '$t7': 15, '$s0': 16, '$s1': 17, '$s2': 18, '$s3': 19, '$s4': 20,
       '$s5': 21, '$s6': 22, '$s7': 23, '$t8': 24, '$t9': 25, '$k0': 26, '$k1': 27, '$gp': 28, '$sp': 29, '$fp': 30, '$ra': 31}
def gp_word(asm, new):
    """the instruction that bounds by $gp, or None (the patch needs a rewrite instead)"""
    p = asm.split(None, 1); op = p[0]; a = [x.strip() for x in p[1].split(',')]
    if op in ('slti', 'sltiu') and int(a[2], 0) == 4 and new == 6:
        return (REG[a[1]] << 21) | (GP << 16) | (REG[a[0]] << 11) | (0x2A if op == 'slti' else 0x2B)
    if op == 'addiu' and a[1] == '$zero' and int(a[2], 0) == 4 and new == 6:
        return (GP << 21) | (REG[a[0]] << 11) | 0x21                      # addu rt, $gp, $zero
    if op == 'addiu' and a[1] == '$zero' and int(a[2], 0) == 3 and new == 5:
        return (9 << 26) | (GP << 21) | (REG[a[0]] << 16) | 0xFFFF       # addiu rt, $gp, -1
    return None
def apply_gp(rom):
    ps, problems = patches()
    if problems: raise ValueError(problems[:3])
    n = 0; left = []
    for (sec, addr), (new, fn, why) in sorted(ps.items()):
        asm = [x for s, f, x in BY[addr] if s == sec][0]
        w = gp_word(asm, new)
        if w is None: left.append((fn, hex(addr), asm, new)); continue
        rom.w32(addr, sec, w, rom.r32(addr, sec), 'loop by $gp ' + fn)
        n += 1
    return n, left
