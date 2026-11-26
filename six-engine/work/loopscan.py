import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Loops in the game's MIPS and the arrays each one walks (induction pointers and counter-indexed
# accesses). Arrays walked together with a known per-slot array are per-slot too (co-iteration).
import pickle, re, collections, sys
sys.path.insert(0, nmp('/home/devbox/work/six'))
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
TB = 0x80159990; WS = 0x358

FUN = collections.defaultdict(list)
for s, f, a, asm in insns: FUN[(s, f)].append((a, asm))
for k in FUN: FUN[k].sort()

def parse(asm):
    p = asm.split(None, 1); op = p[0]
    return op, [x.strip() for x in p[1].split(',')] if len(p) > 1 else []
def ival(s): return int(s, 0)
BR = {'beq', 'bne', 'beql', 'bnel', 'bgez', 'bltz', 'blez', 'bgtz', 'bgezl', 'bltzl', 'blezl', 'bgtzl'}
MEM = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwu', 'ld', 'sb', 'sh', 'sw', 'sd', 'lwc1', 'swc1', 'ldc1', 'sdc1', 'lwl', 'lwr', 'swl', 'swr'}

def const_before(lst, i, reg):
    """value of reg at index i if it's a constant set by lui/addiu/ori shortly before (linear)."""
    val = None; hi = None
    for j in range(i - 1, max(-1, i - 60), -1):
        a, asm = lst[j]; op, args = parse(asm)
        if not args or args[0] != reg: continue
        if op == 'addiu' and args[1] == reg:
            k = ival(args[2])
            v = const_before(lst, j, reg)
            return None if v is None else (v + k) & 0xFFFFFFFF
        if op == 'addiu' and args[1] != reg:
            v = 0 if args[1] == '$zero' else const_before(lst, j, args[1])
            return None if v is None else (v + ival(args[2])) & 0xFFFFFFFF
        if op == 'lui': return (ival(args[1]) << 16) & 0xFFFFFFFF
        if op == 'ori' and args[1] == reg:
            v = const_before(lst, j, reg); return None if v is None else v | ival(args[2])
        if op in ('addu', 'or') and args[2] == '$zero':
            return 0 if args[1] == '$zero' else const_before(lst, j, args[1])
        return None
    return None

def loops_of(sec, fn):
    lst = FUN[(sec, fn)]
    idx = {a: i for i, a in enumerate(lst and [a for a, _ in lst])}
    out = []
    for i, (a, asm) in enumerate(lst):
        op, args = parse(asm)
        if op in BR and args and args[-1].startswith('L_'):
            t = int(args[-1][2:], 16)
            if t <= a and t in idx:
                out.append((idx[t], i + 1 if i + 1 < len(lst) else i))   # body incl. delay slot
    return lst, out

def analyze_loop(lst, s, e):
    body = lst[s:e + 1]
    # bound compares: slti/sltiu x, r, K inside body
    bounds = []
    for a, asm in body:
        op, args = parse(asm)
        if op in ('slti', 'sltiu'): bounds.append((a, args[1], ival(args[2])))
    # induction regs: addiu r, r, K
    ind = {}
    for a, asm in body:
        op, args = parse(asm)
        if op == 'addiu' and len(args) == 3 and args[0] == args[1]:
            ind[args[0]] = ind.get(args[0], 0) + ival(args[2])
    walks = []
    for r, k in ind.items():
        if k == 0: continue
        b0 = const_before(lst, s, r)
        if b0 is not None and b0 >= 0x80000000:
            # accesses through r in the body: offsets
            offs = set()
            for a, asm in body:
                op, args = parse(asm)
                if op in MEM:
                    m = re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)', args[1])
                    if m and m.group(2) == r: offs.add(ival(m.group(1)))
                if op in ('addu', 'addiu') and r in args[1:]: offs.add(None)
            walks.append(('ptr', r, b0, k, tuple(sorted(o for o in offs if o is not None)), None in offs))
    # counter-indexed (ind +1, or an offset register stepping by k from 0): follow r<<n / chains to
    # lui-based addresses
    for r, k in ind.items():
        if k == 0: continue
        b0 = const_before(lst, s, r)
        if k != 1 and b0 != 0: continue
        derived = {r: 1 if k == 1 else k}
        hiregs = {}
        for a, asm in body:
            op, args = parse(asm)
            if not args: continue
            d = args[0]
            if op == 'sll' and args[1] in derived: derived[d] = derived[args[1]] << ival(args[2]); continue
            if op in ('subu',) and args[1] in derived and args[2] in derived:
                derived[d] = derived[args[1]] - derived[args[2]]; continue
            if op in ('addu',) and args[1] in derived and args[2] in derived and d not in hiregs:
                derived[d] = derived[args[1]] + derived[args[2]]; continue
            if op == 'lui': hiregs[d] = ival(args[1]) << 16; derived.pop(d, None); continue
            if op == 'addiu' and args[1] in hiregs and d == args[1]:
                hiregs[d] = (hiregs[d] + ival(args[2])) & 0xFFFFFFFF; continue
            if op == 'addu' and ((args[1] in hiregs and args[2] in derived) or (args[2] in hiregs and args[1] in derived)):
                h = hiregs.get(args[1], hiregs.get(args[2])); kk = derived.get(args[2], derived.get(args[1]))
                hiregs.pop(d, None)
                derived.pop(d, None)
                # remember combined
                derived[d + '#'] = (h, kk)
                derived[d] = ('P', h, kk)
                continue
            if op in MEM:
                m = re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)', args[1])
                if m and isinstance(derived.get(m.group(2)), tuple):
                    _, h, kk = derived[m.group(2)]
                    walks.append(('idx', r, (h + ival(m.group(1))) & 0xFFFFFFFF, kk, (), False))
                if op[0] == 'l': derived.pop(d, None); hiregs.pop(d, None)
                continue
            if d in derived and op not in ('sw', 'sh', 'sb'): derived.pop(d, None)
            if d in hiregs and op not in ('sw', 'sh', 'sb'): hiregs.pop(d, None)
    return bounds, ind, walks

def all_loops(secnames=('main_1050', 'ovl_e')):
    res = []
    for (sec, fn), lst in FUN.items():
        if sec not in secnames: continue
        lst, loops = loops_of(sec, fn)
        for s, e in loops:
            b, ind, w = analyze_loop(lst, s, e)
            res.append((sec, fn, lst[s][0], lst[e][0], b, ind, w))
    return res

if __name__ == '__main__':
    L = all_loops()
    pickle.dump(L, open(nmp('/home/devbox/work/six/loops.pkl'), 'wb'))
    print(len(L), 'loops')
    wl = [l for l in L if any(w[3] == WS and TB <= w[2] < TB + WS for w in l[6]) or any(w[3] == WS and TB - 8 <= w[2] < TB + WS for w in l[6])]
    print(len(wl), 'walk the wrestler array')

def loop_control(lst, s, e):
    """The compare that decides the loop's back branch: (kind, reg, value, pc_of_const_or_cmp)."""
    # back branch is at e-1 (e is its delay slot) or at e
    bi = e - 1 if e - 1 >= s else e
    a, asm = lst[bi]; op, args = parse(asm)
    if op not in BR:
        bi = e; a, asm = lst[bi]; op, args = parse(asm)
    def regconst(reg, before):
        # constant reg value defined inside the loop body or before it
        for j in range(before - 1, max(-1, s - 80), -1):
            aa, sasm = lst[j]; o2, a2 = parse(sasm)
            if a2 and a2[0] == reg:
                if o2 == 'addiu' and a2[1] == '$zero': return ival(a2[2]), aa
                if o2 == 'ori' and a2[1] == '$zero': return ival(a2[2]), aa
                if o2 == 'lui':
                    return (ival(a2[1]) << 16) & 0xFFFFFFFF, aa
                if o2 == 'addiu' and a2[1] == reg:
                    v = regconst(reg, j)
                    if v[0] is not None: return (v[0] + ival(a2[2])) & 0xFFFFFFFF, aa
                return None, aa
        return None, None
    res = []
    if op in ('bne', 'beq', 'bnel', 'beql'):
        r1, r2 = args[0], args[1]
        # slt-style: find the def of r1 just before
        for j in range(bi - 1, max(s - 1, bi - 12), -1):
            aa, sasm = lst[j]; o2, a2 = parse(sasm)
            if a2 and a2[0] == r1 and r2 == '$zero':
                if o2 in ('slti', 'sltiu'): res.append(('slti', a2[1], ival(a2[2]), aa)); break
                if o2 in ('slt', 'sltu'):
                    v, pc = regconst(a2[2], j)
                    res.append(('slt', a2[1], v, pc if pc else aa)); break
                break
        if not res and r2 != '$zero':
            v, pc = regconst(r2, bi)
            if v is not None: res.append(('bne', r1, v, pc))
            else:
                v, pc = regconst(r1, bi)
                if v is not None: res.append(('bne', r2, v, pc))
    return res
