#!/usr/bin/env python3
"""The compiler's checks after a division that can never run, proved from the function's own instructions.

After each `div` the N64 compiler writes a check that stops the program on a division by zero, and another on the most negative
number divided by -1 (the only quotient that doesn't fit):

    div   $zero, $p, $q
    bne   $q, $zero, A        # (bnez)
    nop
    break 7                   # q was 0
  A:
    addiu $at, $zero, -0x1
    bne   $q, $at, B
    lui   $at, 0x8000
    bne   $p, $at, B
    nop
    break 6                   # q was -1 and p 0x80000000
  B:

(divu has only the first.) m2c leaves the checks out (its DivPattern), and the random-input tester counts the original's
instructions as reached only when a test runs them (fuzz_main.cpp): a function is proved only once every one has run. But very
often the divisor can't be 0 or -1 there at all -- the function returned early when it was `blez` (func_800EB9C0: `blez $s1` at
the top, then `x % s1` in a loop) -- and the check is code that never runs, so the function could never be proved (43 functions,
7313 instructions, of the 62 that dividing let in, 2026-11-07).

This proves it, where it can, with a small forward analysis of what range of values each register can hold on the way into each
instruction: a constant loaded, a byte or halfword loaded unsigned, a mask, a comparison's 0 or 1, a register copied, and what a
branch on a register against zero says on each way out of it. Everything else, and every register a call may change, can be
anything. If the divisor can't be 0 at the division, `break 7` never runs; if it can't be -1, the second `bne`, its delay slot and
`break 6` never run. lift.py run_fuzz counts exactly those as reached from the start. Anything not matching the pattern exactly
is left alone, as is any check the analysis can't rule out: those still have to run in a test.

What it takes on trust: the calling rules (a function it calls gives back s0-s7, fp and sp as they were -- what lift.py relies on
everywhere), and mips.cfg's flow graph (a jump through a table goes to its listed cases). Checked against the tester itself on the
62 functions dividing let in: no instruction this calls dead was ever reached by a test (scratch/lift_port/try_dead.py).
"""
import re
from mips import uses_defs, cfg, BRANCH

LOW, HIGH = -0x80000000, 0x7FFFFFFF
FULL = (LOW, HIGH)
# What a call may change (the calling rules): its result, parameter and scratch registers.
CALL_CLOBBERS = {'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7', 't8', 't9', 'ra'}
WIDEN_AFTER = 8         # (times a register's range at one instruction may grow before it is taken as anything: loops end)
OPS = re.compile(r'\$(\w+)')


def number(text):
    return int(text, 0)


def signed(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def get(state, r):
    return (0, 0) if r == 'zero' else state.get(r, FULL)


def transfer(mn, ops, state):
    """The ranges after one instruction."""
    out = dict(state)
    regs = OPS.findall(ops)
    if mn in ('jal', 'jalr'):
        return out          # (the call happens after its delay slot: ranges() clobbers there)
    _, defs = uses_defs(mn, ops)
    value = None
    m = re.match(r'\$(\w+),\s*\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
    if mn in ('addiu', 'addi') and m:
        lo, hi = get(state, m.group(2))
        k = signed(number(m.group(3)))
        if LOW <= lo + k and hi + k <= HIGH:
            value = (lo + k, hi + k)
    elif mn == 'ori' and m and m.group(2) == 'zero':
        value = (number(m.group(3)) & 0xFFFF,) * 2
    elif mn == 'andi' and m:
        value = (0, number(m.group(3)) & 0xFFFF)
    elif mn in ('slti', 'sltiu', 'slt', 'sltu'):
        value = (0, 1)
    elif mn == 'lui':
        k = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if k:
            value = (signed(number(k.group(1)) << 16),) * 2
    elif mn == 'lbu':
        value = (0, 0xFF)
    elif mn == 'lhu':
        value = (0, 0xFFFF)
    elif mn == 'lb':
        value = (-0x80, 0x7F)
    elif mn == 'lh':
        value = (-0x8000, 0x7FFF)
    elif mn == 'srl' and m and number(m.group(3)) > 0:
        value = (0, (1 << (32 - number(m.group(3)))) - 1)
    elif mn in ('addu', 'or') and len(regs) == 3 and 'zero' in regs[1:]:
        value = get(state, regs[2] if regs[1] == 'zero' else regs[1])      # (a copy)
    for r in defs:
        out.pop(r, None)
        out.pop('nz:' + r, None)
    if value is not None and len(defs) == 1:
        out[next(iter(defs))] = value
    if mn in ('addu', 'or') and len(regs) == 3 and 'zero' in regs[1:] and len(defs) == 1:
        source = regs[2] if regs[1] == 'zero' else regs[1]
        if 'nz:' + source in state:
            out['nz:' + next(iter(defs))] = (1, 1)      # (a copy of a register known not to be 0)
    return {r: v for r, v in out.items() if v != FULL}


def refine(state, mn, ops, taken):
    """What a branch on a register against zero says about it, on the way it went."""
    regs = OPS.findall(ops)
    base = mn[:-1] if mn.endswith('l') and mn not in ('bgezal', 'bltzal') else mn
    if base in ('beq', 'bne'):
        if len(regs) < 2 or 'zero' not in regs[:2] or regs[0] == regs[1]:
            return state
        r = regs[1] if regs[0] == 'zero' else regs[0]
        cond = ('==' if base == 'beq' else '!=') if taken else ('!=' if base == 'beq' else '==')
    elif base in ('blez', 'bgtz', 'bltz', 'bgez'):
        r = regs[0]
        cond = {'blez': '<=', 'bgtz': '>', 'bltz': '<', 'bgez': '>='}[base]
        if not taken:
            cond = {'<=': '>', '>': '<=', '<': '>=', '>=': '<'}[cond]
    else:
        return state
    if r == 'zero':
        return state
    lo, hi = get(state, r)
    if cond == '==':
        lo, hi = max(lo, 0), min(hi, 0)
    elif cond == '!=':
        lo, hi = (1 if lo == 0 else lo), (-1 if hi == 0 else hi)
    elif cond == '<=':
        hi = min(hi, 0)
    elif cond == '>':
        lo = max(lo, 1)
    elif cond == '<':
        hi = min(hi, -1)
    elif cond == '>=':
        lo = max(lo, 0)
    out = dict(state)
    # (Besides its range, a register can be known not to be 0 -- 'nz:<register>' -- where the range can't say so: one that could
    # be anything, after a `bne $r, $zero` taken.)
    if cond == '!=':
        out['nz:' + r] = (1, 1)
    if lo > hi or (cond == '==' and 'nz:' + r in state):
        out['__unreachable__'] = (0, 0)       # (this way out can't be taken: nothing after it from here)
    out[r] = (lo, hi)
    return {k: v for k, v in out.items() if v != FULL}


def join(a, b):
    """Either state: each register's range covers both (a register missing from one is anything)."""
    if a is None:
        return b
    if '__unreachable__' in a:
        return b
    if '__unreachable__' in b:
        return a
    return {r: (min(a[r][0], b[r][0]), max(a[r][1], b[r][1])) for r in set(a) & set(b)}


def ranges(f):
    """The register ranges on the way into each instruction (only facts that hold on every way there)."""
    addrs, succ = cfg(f)
    if not addrs:
        return {}
    # Which instruction is a branch's delay slot, and its branch: the range it says holds on the way out of the delay slot.
    branch_of = {}
    for a in addrs:
        mn, _ = f['ins'][a]
        if mn in BRANCH and not (mn.endswith('l') and mn not in ('bgezal', 'bltzal')):
            branch_of[a + 4] = a
    state = {addrs[0]: {}}
    visits = {}
    work = [addrs[0]]
    while work:
        pc = work.pop()
        if '__unreachable__' in state[pc]:
            continue
        mn, ops = f['ins'][pc]
        after = transfer(mn, ops, state[pc])
        if f['ins'].get(pc - 4, ('',))[0] in ('jal', 'jalr'):
            # (pc is a call's delay slot: the call runs now, and may change any of these.)
            after = {r: v for r, v in after.items() if r not in CALL_CLOBBERS and r[3:] not in CALL_CLOBBERS}
        outs = []
        likely = mn in BRANCH and mn.endswith('l') and mn not in ('bgezal', 'bltzal')
        for n in succ.get(pc, []):
            s = after
            if likely:
                # (A likely branch runs its delay slot only when taken: pc+4 is the taken way, the other the way not taken.)
                s = refine(after, mn, ops, n == pc + 4)
            elif pc in branch_of:
                b = branch_of[pc]
                bmn, bops = f['ins'][b]
                target = re.search(r'L_?([0-9A-F]{8})', bops)
                tgt = int(target.group(1), 16) if target else None
                _, ddefs = uses_defs(mn, ops)
                if tgt != pc + 4 and not (set(OPS.findall(bops)) & ddefs) and n in (tgt, pc + 4):
                    s = refine(after, bmn, bops, n == tgt)
            outs.append((n, s))
        for n, s in outs:
            old = state.get(n)
            if old is not None and '__unreachable__' in old:
                old = None          # (only reached so far by a way that can't be taken: as if not reached)
            new = join(old, s) if old is not None else s
            if old is not None:
                # (A register whose range here keeps growing -- a loop's count -- is taken as anything once it has grown
                # WIDEN_AFTER times, so the analysis ends. Counted per register: one still growing doesn't cost the others.)
                for r in [r for r, v in new.items() if old.get(r) != v]:
                    visits[(n, r)] = visits.get((n, r), 0) + 1
                    if visits[(n, r)] > WIDEN_AFTER:
                        del new[r]
            if old is None or new != old:
                state[n] = new
                work.append(n)
    return state


def dead_checks(f):
    """The addresses of the division-check instructions that can never run (see above)."""
    ins = f['ins']
    found = set()
    known = None
    for d, (mn, ops) in sorted(ins.items()):
        if mn not in ('div', 'divu'):
            continue
        m = re.match(r'\$zero,\s*\$(\w+),\s*\$(\w+)$', ops)
        if not m:
            continue
        p, q = m.group(1), m.group(2)
        at = lambda k: ins.get(d + 4 * k, ('', ''))
        check = re.match(r'\$(\w+),\s*\$zero,\s*L_?([0-9A-F]{8})$', at(1)[1])
        if not (at(1)[0] == 'bne' and check and check.group(1) == q and int(check.group(2), 16) == d + 16
                and at(2)[0] == 'nop' and at(3)[0] == 'break'):
            continue
        if known is None:
            known = ranges(f)
        if d not in known or '__unreachable__' in known[d]:
            continue
        lo, hi = get(known[d], q)
        if not (lo <= 0 <= hi) or 'nz:' + q in known[d]:
            found.add(d + 12)                       # break 7
        if mn == 'div':
            o = [at(k) for k in range(4, 10)]
            shape = (o[0][0] == 'addiu' and re.fullmatch(r'\$at,\s*\$zero,\s*-0x1', o[0][1]) and o[1][0] == 'bne'
                     and re.fullmatch(r'\$%s,\s*\$at,\s*L_?%08X' % (q, d + 40), o[1][1]) and o[2][0] == 'lui'
                     and re.fullmatch(r'\$at,\s*0x8000', o[2][1]) and o[3][0] == 'bne'
                     and re.fullmatch(r'\$%s,\s*\$at,\s*L_?%08X' % (p, d + 40), o[3][1]) and o[4][0] == 'nop' and o[5][0] == 'break')
            if shape and not (lo <= -1 <= hi):
                found |= {d + 28, d + 32, d + 36}   # the second bne, its delay slot, break 6
    return found
