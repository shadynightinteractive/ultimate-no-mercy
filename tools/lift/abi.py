#!/usr/bin/env python3
"""abi.py: converting functions that don't keep to the MIPS calling rules, by making their extra registers explicit
(2026-11-07, scratch/lift_abi).

lift.py refuses a function m2c's plain C signature can't express:
  - "a caller reads what it leaves in v0" (or a2, a3, f12, ...): something after a call to it reads a register it wrote, that
    isn't its result -- a result m2c took as void, a parameter register left set for a later call, a value passed on;
  - "calls something that reads scratch registers" / "... a register it changes": a function it calls reads a register that
    isn't one of its parameters (v1, t0, s0 ...), so the caller's value there is an input to the call;
  - "reads a register other than its parameters": it reads v1, t0, ... itself, as it was handed them.
The C++ is still called on the N64's registers (recomp_context), so those extra registers can be made exact. One rule covers
all three:

  KEEP THE CONTEXT IN STEP. The converted function keeps its values in C variables, and the context's registers only change
  when it calls something (lift_call puts the parameters there, the callee runs on them). So a register the original's own
  instructions write is, in the context, out of step from that instruction on. For each register in the MIRROR set -- the ones
  a caller reads afterwards, and the ones a function it calls reads -- every instruction of its own that writes the register is
  followed, in the assembly given to m2c, by a store of it to a name: `sw $a2, lift_out_a2`. m2c writes that as
  `lift_out_a2 = <the value>;`, and in the C++ lift_out_a2 is the register in the context (LiftAbiOut). Then the context holds
  what the original's register holds at every point: after its own write (the store), after a call (the same translated callee
  ran on the same registers: it changed them the same way, or left them alone the same way), and on the way in (untouched).
  So a callee reading it, and a caller reading it after the return, see the original's value.

  Its own extra inputs (v1, t0, ... read before being written) are loaded at the start, `lw $v1, lift_in_v1`, and in the C++
  lift_in_v1 is the register's value as the function came in. A callee-saved register in the mirror set (s0, a callee reads
  it) is put back as it came in on the way out (LiftAbiKeep): the original saves it in its frame and restores it there, which
  the tester checks (kept_registers_same in fuzz_main.cpp).

A store can't go straight after an instruction in a branch's delay slot: the instruction after it runs after the branch. So
one that sets a mirror register is moved before its branch (a jump, a call, a return, or a branch that isn't a "likely" one,
which runs its delay slot either way): `jal f; addiu $a2, $zero, 3` becomes `addiu $a2, $zero, 3; sw $a2, lift_out_a2; jal f;
nop`. That's the same program only if the branch doesn't read what it writes, or write what it reads (jal writes ra). Where it
does -- `beq $v0, $zero, .L1; addiu $v0, $zero, 1`, the commonest -- or the branch is a "likely" one (its delay slot runs only
when it's taken), a conditional branch keeps its delay slot, and the stores go on each way out of it instead: straight after the
slot for the way it falls through (not for a likely branch, whose slot doesn't run that way), and for the way it's taken, a
few instructions at the end of the function that store and jump on to the branch's target, the branch now going to them.
Anything else that would need moving -- a jump or call whose delay slot depends on it, something branching straight into a
delay slot -- is refused.

The random-input tester checks it: it compares every mirror register at the end, original against converted; it records the
extra registers each callee reads, so the calls must match there too; and for these functions only its stand-ins for the
callees change only the registers the real callee can change -- always the ones it surely writes on every way through it
(must_write: a call's result in v0), each of the others at random changed or left as it was -- where for every other
function they scramble every scratch register. A stand-in that scrambled a2 on every call would hide a converted
function that forgot to leave a2 as the original does before the call.

A float register written as half of a double is kept in step as the whole pair, with one sdc1 (v2, 2026-11-08). Instructions
m2c only gets right as a whole -- a jump through a table, from its bounds check to the jr, and an mfhi, whose register m2c
gives a folded division's value -- get no store; the register is out of step after them, which is allowed only where it's
written again before anything could look at it (rewritten_before_seen).

Not handled (still refused, with the reason given): a mirror register that's hi or lo, or the stack or return address; an
extra input that is hi or lo, or a float one read as half of a double. And everything lift.py refuses for other reasons:
this changes only the checks named above.
"""
import os, re

from mips import uses_defs, uses_defs_in, BRANCH, JUMPS

# LIFT_ABI=0 turns this off: lift.py then refuses these functions as before.
ENABLED = os.environ.get('LIFT_ABI', '1') != '0'

# The refusals before m2c runs that this handles (lift.py candidates' first checks), and the one after.
CALL_REASONS = {'calls something that reads scratch registers', 'calls something that reads a register it changes'}
FIRST_REASONS = CALL_REASONS | {'reads a register other than its parameters'}
LOST_PREFIX = 'a caller reads what it leaves in '

PARAMETERS = {'a0', 'a1', 'a2', 'a3', 'f12', 'f14'}
GPR = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
INT_SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp'}
FLOAT_SAVED = {'f%d' % i for i in range(20, 32)}
# Registers this can't make explicit: the stack and return address (the frame and the return are the converted function's own),
# the kernel's, and hi/lo (no C value stands for them).
NEVER = {'zero', 'sp', 'ra', 'k0', 'k1', 'hi', 'lo', 'gp'}
# What lift.py's registers_set and not_a_whole_function treat as putting back a saved register at the end.
RESTORE = re.compile(r'\$(\w+),\s*-?(?:0[xX])?[0-9A-Fa-f]+\(\$sp\)$')
# The scratch registers fuzz_stand_in can change (fuzz_main.cpp): any of these a callee may write.
STAND_IN_GPR = ['at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7', 't8', 't9']
STAND_IN_FPR = ['f%d' % i for i in range(20)]


def is_float(r):
    return re.fullmatch(r'f\d+', r) is not None


def bit(r):
    """A register's bit in the tester's masks: GPR n is bit n, float register n is bit 32 + n."""
    return 1 << (32 + int(r[1:])) if is_float(r) else 1 << GPR.index(r)


def mask(regs):
    return sum(bit(r) for r in regs)


def context_field(r):
    """The register in the C++'s recomp_context, as an lvalue of its 32 bits: an integer register whole (gpr), a float register
    as its half of the even register of its pair (the N64 runs the game in its 32-bit float mode: f13 is f12's top half)."""
    if is_float(r):
        n = int(r[1:])
        return 'ctx->f%d.%s' % (n & ~1, 'u32h' if n & 1 else 'u32l')
    return 'ctx->r%d' % GPR.index(r)


def is_restore(mn, ops):
    """Putting back a callee-saved register from the stack (as lift.py's registers_set sees it)."""
    m = RESTORE.match(ops)
    return mn in ('lw', 'ld', 'lwc1', 'ldc1') and m is not None and (m.group(1) in INT_SAVED | FLOAT_SAVED | {'gp'})


def plan(name, f, functions, analysis, relied, kept_floats, reload=()):
    """What the converted function has to make explicit. Returns (plan, None), or (None, why) for a case this refuses.

    plan = {'mirror': registers written to the context after each of its own writes, 'out': those the tester compares at the end
            (all the mirror registers but the callee-saved ones, which go back as they came), 'keep': callee-saved mirror registers,
            put back on the way out, 'inputs': registers it reads as it was handed them, other than its parameters and gp}"""
    wrote_all, own = set(), set()
    # (2026-11-13, agent 78dc) A load from at or above the frame's top is no saved register put back: it is a stack parameter
    #  (func_801278DC's `lw $s7, 0x94($sp)`, its 6th, in a frame of 0x80). Such a register is the function's own value, and a
    #  callee that reads it has to be given it, as any other it writes. Until now it counted as put back, so a callee said to
    #  read s7 (func_800DD6C4, by final082's staged analysis) was neither kept in step nor refused: the tester found it.
    #  With today's analysis no function has such a callee (all 0), so no conversion changes; this only stops a silent gap.
    frame = next((int(m.group(1), 0) for mn, ops in (f['ins'][a] for a in sorted(f['ins']))
                  for m in [re.match(r'\$sp,\s*\$sp,\s*-((?:0[xX])?[0-9A-Fa-f]+)$', ops.strip())] if mn == 'addiu' and m), None)
    def loads_parameter(ops):
        m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops.strip())
        return frame is not None and m is not None and int(m.group(1), 0) >= frame
    for mn, ops in f['ins'].values():
        d = uses_defs(mn, ops)[1]
        wrote_all |= d
        if not is_restore(mn, ops) or loads_parameter(ops):
            own |= d
    kept = {'sp', 'ra', 'gp'} | INT_SAVED | kept_floats
    # What a caller reads afterwards that it writes: the registers lift.py refused it for ("a caller reads what it leaves in").
    out = set(relied.get(name, set())) & (wrote_all - kept)
    # What a function it calls reads, other than the parameters lift_call puts there, that it writes itself.
    for callee in set(f['calls'].values()):
        # (2026-11-11, trial 5: a call through a pointer -- lift.py's lift_pointer_<register>, 'pointer' in its entry -- is
        #  made on the registers as they are, parameters too: nothing says which of them its target reads, so lift_call
        #  passes none and every one this function writes is kept in step, with the register that holds the address.)
        passed = set() if analysis.get(callee, {}).get('pointer') else PARAMETERS
        out |= (set(analysis.get(callee, {}).get('use', [])) - passed) & own
    mirror = set(out) | set(reload)
    bad = mirror & NEVER
    if bad:
        return None, 'ABI: a caller or callee relies on %s, which this pass refuses' % ','.join(sorted(bad))
    for r in mirror & FLOAT_SAVED:
        if r not in kept_floats:
            return None, "ABI: a callee reads a saved float register it doesn't put back"
    inputs = set(analysis.get(name, {}).get('own', [])) - PARAMETERS - {'sp', 'ra', 'zero', 'gp'}
    if inputs & NEVER:
        return None, 'ABI: reads %s as it was handed it, which this pass refuses' % ','.join(sorted(inputs & NEVER))
    # (A float register in the mirror set written as half of a double: kept in step as the whole double, with one sdc1 of the
    #  pair -- v2, 2026-11-08; until then refused, 10 functions. One lwc1 can't stand for an input float register read as half
    #  of a double, though.)
    # (2026-11-11, trial 5: one ldc1 can, where the function was handed the whole pair and only ever reads it as a double:
    #  func_8012FEFC's `mul.d $f0, $f0, $f4`, f4 and f5 as it came in. in_doubles: the even register of each such pair, loaded
    #  at the start with `ldc1 $f4, lift_in_df4`, in the C++ the double those two registers hold in the context. Still
    #  refused: a pair with only one half an input (the other a parameter -- f12 with f13, a double parameter -- or written
    #  first), and a pair either half of which some instruction reads as a single float, where m2c would be reading half
    #  of what it was told is a double.)
    def reads_double(mn):
        return bool(mn.endswith('.d') or mn in ('ldc1', 'sdc1') or re.match(r'(cvt|trunc|round|floor|ceil)\.\w\.[dl]$', mn))
    # (uses_defs_in, here and below: a saved register's store that passes it on reads it -- mips.pass_on_stores, 2026-11-16.)
    read_single = set()
    for a, (mn, ops) in f['ins'].items():
        if not reads_double(mn):
            read_single |= {r for r in uses_defs_in(f, a)[0] if is_float(r)}
    pairs = set()
    in_doubles = set()
    for a, (mn, ops) in f['ins'].items():
        u, d = uses_defs_in(f, a)
        if d & mirror and len([r for r in d if is_float(r)]) > 1 and not is_restore(mn, ops):
            pairs.add(min((r for r in d if is_float(r)), key=lambda r: int(r[1:])))
        for r in (u & inputs if reads_double(mn) else ()):
            if not is_float(r):
                continue
            even, odd = 'f%d' % (int(r[1:]) & ~1), 'f%d' % (int(r[1:]) | 1)
            if not {even, odd} <= inputs or {even, odd} & read_single:
                return None, 'ABI: reads a float register it was handed as part of a double'
            in_doubles.add(even)
    # (Both halves of such a pair are then in step -- the sdc1 writes both -- so both are compared at the end, or for a
    #  callee-saved pair both put back: with only f22 kept, func_80140A84's converted version left f23 changed.)
    for even in pairs:
        mirror |= {even, 'f%d' % (int(even[1:]) + 1)}
    for r in mirror & FLOAT_SAVED:
        if r not in kept_floats:
            return None, "ABI: a callee reads a saved float register it doesn't put back"
    keep = mirror & (INT_SAVED | FLOAT_SAVED)
    plan = {'mirror': sorted(mirror), 'out': sorted(mirror - keep), 'keep': sorted(keep), 'inputs': sorted(inputs),
            'doubles': sorted(pairs), 'reload': sorted(reload)}
    if in_doubles:      # (only then, so every other function's plan -- kept in candidates.json -- is as it was)
        plan['in_doubles'] = sorted(in_doubles)
    return plan, None


def store(r):
    return '    %s $%s, lift_out_%s' % ('swc1' if is_float(r) else 'sw', r, r)


def stores(mn, ops, regs):
    """The stores after an instruction that writes these mirror registers: one each, or for a double (a pair of float registers
    written together) one sdc1 of the pair, `sdc1 $f0, lift_out_df0` (v2, 2026-11-08)."""
    floats = [r for r in uses_defs(mn, ops)[1] if is_float(r)]
    if len(floats) > 1:
        even = min(floats, key=lambda r: int(r[1:]))
        return ['    sdc1 $%s, lift_out_d%s' % (even, even)] + [store(r) for r in regs if not is_float(r)]
    return [store(r) for r in regs]


def load(r):
    return '    %s $%s, lift_in_%s' % ('lwc1' if is_float(r) else 'lw', r, r)


def input_loads(p):
    """The loads at the start: one for each input register, but one ldc1 for a pair handed in as a double (in_doubles,
    2026-11-11)."""
    evens = p.get('in_doubles', [])
    halves = {h for e in evens for h in (e, 'f%d' % (int(e[1:]) + 1))}
    return [load(r) for r in p['inputs'] if r not in halves] + ['    ldc1 $%s, lift_in_d%s' % (e, e) for e in evens]


def in_double_halves(p):
    return {h for e in p.get('in_doubles', []) for h in (e, 'f%d' % (int(e[1:]) + 1))}


def rewritten_before_seen(f, succ, starts, r, unstored, analysis, sure, restored=()):
    """Whether register r, out of step in the context from `starts` on, is written again by an instruction of the function's own
    (which has its store) on every way on, before anything could look at it: a return or another jump out, or a call to
    something that reads it. A call to something that surely writes it and doesn't read it (must_write) puts it back in step;
    one that leaves it alone passes it on. A delay slot runs before its jump, so one that writes it counts. (Split out of
    table_jumps in v2, 2026-11-08, for mfhi too.)"""
    seen, work = set(), sorted(starts)
    while work:
        pc = work.pop()
        if pc in seen:
            continue
        seen.add(pc)
        if pc in restored:
            continue        # (an exact store of the original's value goes before this instruction: in step from here. regs2)
        mn, ops = f['ins'][pc]
        if r in uses_defs(mn, ops)[1] and pc not in unstored:
            continue        # (written again, and stored: in step from here)
        target = re.search(r'L_([0-9A-F]{8})', ops)
        leaves = mn in ('jal', 'jalr', 'jr') or ((mn in ('j', 'b') or mn in BRANCH) and target is not None
                                                 and int(target.group(1), 16) not in f['ins'])
        if leaves:
            # (A call, a return, another table jump, or a jump out of the function. Its delay slot runs first.)
            delay = f['ins'].get(pc + 4)
            if delay and r in uses_defs(*delay)[1] and pc + 4 not in unstored:
                continue
            callee = (f['calls'].get(pc + 4) or f['calls'].get(pc)) if mn == 'jal' else None
            if callee in analysis and r not in analysis[callee]['use']:     # (not a system function: unknown)
                if r in sure(callee):
                    continue            # (the callee writes it, as in the original: in step again)
                work.append(pc + 4)     # (left alone: still out of step after the call, so on from there)
                continue
            return False
        work += succ.get(pc, [])
    return True


def exact_hi(f, a, succ):
    """(regs2, 2026-11-09) For an mfhi at a: the mult that set hi, as (mult address, 'mult' or 'multu', rs, rt), found by going back
    from the mfhi through straight-line code (each instruction the only way into the next, none a jump or in a delay slot) to the
    mult; None if anything else could have set hi first (another mult, a div, an mthi), or it isn't that simple."""
    pred = {}
    for x, ns in succ.items():
        for n in ns:
            pred.setdefault(n, set()).add(x)
    b = a
    for _ in range(24):
        ps = pred.get(b, set())
        if ps != {b - 4}:
            return None
        b -= 4
        mn, ops = f['ins'][b]
        if mn in JUMPS or (b - 4 in f['ins'] and f['ins'][b - 4][0] in JUMPS):
            return None
        if mn in ('mult', 'multu'):
            m = re.match(r'\$(\w+),\s*\$(\w+)$', ops)
            return (b, mn, m.group(1), m.group(2)) if m else None
        if mn in ('div', 'divu', 'mthi', 'dmult', 'dmultu', 'ddiv', 'ddivu', 'madd', 'maddu'):
            return None
    return None


def exact_slot(f, jr, run, r):
    """(slot, 2026-11-11) For the jump through a table at jr, whose run of unstored instructions is `run` and whose register r is
    left holding the table slot's address: what gives that address, {'reg': r, 'table': the table's address, 'index': the register
    the addu adds, 'lui': the lui's address}, or None unless the run is exactly the four instructions
        lui $at, hi; addu $at, $at, $index; lw $r, lo($at); jr $r
    one after the other, with $index neither $at nor written in between (so before the lui it holds what the addu adds), and
    nothing else in the run writing r. (A run that starts at the bounds check, rewrite_asm's bounds, is left refused: its register
    is also out of step on the way to the default case.)"""
    from to_asm import find_tables
    found = find_tables(f).get(jr)
    if found is None:
        return None
    table, lui, lw = found
    if sorted(run) != [lui, lui + 4, lui + 8, lui + 12] or lw != lui + 8 or jr != lui + 12:
        return None
    m_lui = re.match(r'\$at,', f['ins'][lui][1]) if f['ins'][lui][0] == 'lui' else None
    m_add = re.match(r'\$at,\s*\$at,\s*\$(\w+)$', f['ins'][lui + 4][1]) if f['ins'][lui + 4][0] == 'addu' else None
    m_lw = re.match(r'\$(\w+),\s*[^(]+\(\$at\)$', f['ins'][lw][1]) if f['ins'][lw][0] == 'lw' else None
    m_jr = re.match(r'\$(\w+)$', f['ins'][jr][1])
    if not (m_lui and m_add and m_lw and m_jr) or m_lw.group(1) != r or m_jr.group(1) != r or m_add.group(1) in ('at', 'zero'):
        return None
    return {'reg': r, 'table': '%08X' % table, 'index': m_add.group(1), 'lui': '%X' % lui}


def table_jumps(f, mirror, analysis, sure, bounds=False, exact=None):
    """The instructions of a jump through a table that get no store after them, or (None, why).

    A switch compiles to `sltiu $v0, $v1, 10; beq $v0, $zero, .Ldefault; sll $v0, $v1, 2; lui $at, %hi(table); addu $at, $at,
    $v0; lw $v0, %lo(table)($at); jr $v0`. m2c turns that back into a switch only as it stands: a store of $v0 after the lw made
    it read the table as data, which doesn't compile (2026-11-07, 45 functions; 53 more were refused for their memory accesses
    that way), and one after the bounds check, or the branch's delay slot handled as below, left m2c's cases outside its switch
    (17). So the instructions from the bounds check (or the lui, without one) to the jr get no store, and a mirror register they
    write is out of step with the original after them -- it holds a code address, or the check's result. That's allowed only
    where every way on from them writes it again (with its store) before anything could look at it: before a return or any other
    jump out, or a call to something that reads it; a call to something that surely writes it and doesn't read it (must_write)
    puts it back in step too, and one that leaves it alone passes it on. (A delay slot runs before its jump, so one that writes
    it counts.) Otherwise refused."""
    from to_asm import find_tables, find_kept_tables, find_slot_tables
    from mips import cfg
    skip = set()
    runs = []           # (instructions with no store, the ways out of them, the mirror registers they write)
    addrs, succ = cfg(f)
    # (2026-11-11, trial 5: a jump whose table's address is kept in a register, to_asm.py find_kept_tables: its run starts at
    #  the addu that adds that register, where the other form's starts at its lui. `lui` below is the run's first instruction.)
    starts = {jr: lui for jr, (_, lui, _) in find_tables(f).items()}
    starts.update({jr: addu for jr, (_, _, _, addu, _) in find_kept_tables(f).items() if jr not in starts})
    # (2026-11-12, agents "refused" and "refused2": and one whose slot address is kept in the frame, to_asm.py find_slot_tables:
    #  its run starts at the first of the two loads it replaces with the lookup.)
    starts.update({jr: jr - 8 for jr in find_slot_tables(f) if jr not in starts})
    for jr, lui in starts.items():
        first = lui
        # (The bounds check: a branch on a register compared with zero, a few instructions before the lui, whose register an
        #  sltiu/sltu/slti just before it set. Its delay slot is part of the run.)
        for b in (range(lui - 4, lui - 28, -4) if bounds else ()):
            bm, bo = f['ins'].get(b, ('', ''))
            m = re.match(r'\$(\w+),\s*\$zero,', bo)
            if bm in ('beq', 'bne') and m:
                for c in range(b - 4, b - 16, -4):
                    cm, co = f['ins'].get(c, ('', ''))
                    if cm in ('sltiu', 'sltu', 'slti', 'slt') and co.startswith('$' + m.group(1) + ','):
                        first = c
                        break
                break
        span = [a for a in f['ins'] if first <= a <= jr]
        written = set()
        for a in span:
            written |= uses_defs(*f['ins'][a])[1] & mirror
        # (Every way out of the run: the jump's delay slot, and where the bounds check's branch lands.)
        exits = {n for a in span for n in succ.get(a, []) if not first <= n <= jr}
        runs.append((set(span), exits, written, jr))
    # (v2, 2026-11-08) The top half of a multiply, mfhi: m2c folds `mult` by a magic number, mfhi, shifts and a subtract into one
    # division, and gives the register after mfhi the quotient's value, not the top half's (func_800E6424's x / 6 kept a3 one out,
    # 2026-11-07). Only the end of such a run is right in m2c's C, so no store goes after an mfhi either: the register is out of
    # step from there, which is allowed on the same terms as a table jump's. (Until v2 a mirror register written by mfhi was
    # refused, 15 functions.) One in a delay slot is still refused.
    for n, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        regs = uses_defs(mn, ops)[1] & mirror
        if mn != 'mfhi' or not regs:
            continue
        if n > 0 and addrs[n - 1] == a - 4 and f['ins'][a - 4][0] in JUMPS:
            return None, 'ABI: keeps in step the top half of a multiply (part of a division m2c folds)'
        runs.append(({a}, set(succ.get(a, [])), regs, None))
    for run, _, _, _ in runs:
        skip |= run
    # (regs2, 2026-11-09) Where the rule below would refuse, an exact value of the original's register can often still be had, and
    # is then stored where the original has it (exact; rewrite_asm and add_declarations do the rest):
    #   - an mfhi's register: the top half of the product of the mult before it (straight-line code from the mult, nothing else
    #     setting hi between), worked out in the C++ from the mult's operands, which are stored as the mult reads them.
    # Without exact (the first round's old behaviour), nothing changes.
    pred = {}
    for x, ns in succ.items():
        for n in ns:
            pred.setdefault(n, set()).add(x)
    for run, exits, written, jr in runs:
        for r in written:
            if rewritten_before_seen(f, succ, exits, r, skip, analysis, sure):
                continue
            if exact is not None and jr is None and len(run) == 1:
                a = next(iter(run))
                m = exact_hi(f, a, succ)
                if m is not None:
                    exact.setdefault('hi', {})['%X' % a] = {'reg': r, 'mult': '%X' % m[0], 'kind': m[1], 'rs': m[2], 'rt': m[3]}
                    continue
            if exact is not None and jr is not None:
                # (slot, 2026-11-11, trial 5) The register the table jump goes through, where every way on doesn't write it
                # again: its exact value can be had after all. Not the case's address, which is what the N64 would hold and
                # what regs2 tried: the port's translated code never loads the table word -- N64Recomp writes the `lw $v0,
                # %lo(table)($at)` as `ctx->r2 = ADD32(ctx->r1, lo)` -- so what the original leaves there is the table SLOT's
                # address, the table's address plus the scaled index the addu added. Both are known before the lui: see
                # exact_slot. (7 functions were refused here, 3,142 instructions, a third of them func_80136C7C.)
                m = exact_slot(f, jr, run, r)
                if m is not None:
                    exact.setdefault('slot', {})['%X' % jr] = m
                    continue
            why = ("ABI: a register it keeps in step holds a table jump's address where something could look at it" if len(run) > 1
                   else 'ABI: keeps in step the top half of a multiply (part of a division m2c folds)')
            return None, why
    if exact is not None:
        # (an mfhi with an exact value is stored after all: it's no longer one of the instructions without a store)
        skip -= {int(a, 16) for a in exact.get('hi', {})}
    return skip, None


def cases_outside_switch(c):
    """Whether m2c's C has a `case` label that isn't inside its own switch: what it wrote for some table jumps whose bounds
    check's branch got its stores. By the braces: each `{` opened on a `switch (...)` line counts as a switch until its `}`.
    (v2, 2026-11-08)

    Its OWN switch (2026-11-10): where the function has more than one, m2c numbers them in comments -- `switch (x) { /* switch
    2 */` and `case 17: /* switch 2 */` -- and a label counts as inside only when the innermost switch around it has its
    number. Until now any switch around it would do, so the inner switch's cases written after it, in the `else` of its
    bounds check, passed for the outer switch's: func_801068AC and func_8013C164 compiled, with those cases part of a switch
    that never has those values (memory-count: the stores of cases 17 to 49 never made). A label inside another switch's
    body (func_80144E74) is caught the same way. third_party/m2c's if_statements.py no longer writes such labels
    (cases_into_their_own_switches); this is the check that it doesn't."""
    stack = []      # (for each open brace: None, or the switch's number -- 0 when m2c didn't number it)
    for line in c.split('\n'):
        text = line.strip()
        number = re.search(r'/\* switch (\d+) \*/', text)
        number = int(number.group(1)) if number else 0
        if re.match(r'case\b|default:', text):
            around = [s for s in stack if s is not None]
            if not around or (number and around[-1] and around[-1] != number):
                return True
        for ch in text:
            if ch == '{':
                stack.append(number if text.startswith('switch') else None)
            elif ch == '}' and stack:
                stack.pop()
    return False


RESULT_REGISTERS = ('v0', 'f0')     # (what lift_call hands back: a call's result as m2c takes it. v1 and the rest, read after a call that
                                    #  left them, are m2c's 'unset' registers, which the reload pass keeps in step already.)


def declared_results(context):
    """(C1) Each callee's result register as m2c's context declares it for this caller (lift.py callee_context / prototype):
    {name: 'v0' or 'f0'}; a void one, or a double, is left out. lift_call hands back that register from the context."""
    found = {}
    for m in re.finditer(r'^\s*([A-Za-z_][\w\s\*]*?)\s*\b(\w+)\(', context or '', re.M):
        kind = m.group(1).strip()
        if kind in ('void', 'f64', 'extern'):
            continue
        found[m.group(2)] = 'f0' if kind == 'f32' else 'v0'
    return found


def call_store_sites(f, p, analysis, sure, results=None):
    """(C1, 2026-11-11, scratch/different_overnight) The calls before which a result register goes into the context: {jal address:
    set of registers}.

    The converted function takes a call's result from the context (lift_call returns ctx->r2), and keeps its own values in C
    variables. A callee that may leave v0 as it was -- one not every way through which writes it (must_write: func_800DBA88's
    first instruction jumps into the mod's code, so nothing about it is sure) -- hands back the CALLER's v0. In the original that
    is whatever the caller last put in v0; in the converted function the context still holds what the last call or the caller
    left there. func_800D9990 stores v0 to 0x80151EDA after such a call: `lb $v0, 0x1ED4($v0); blez $v0, ...; jal func_800DBA88;
    ...; sh $v0, 0x1EDA($at)`, and the converted one stored a stale ctx->r2 (1,187 of 20,872 at 50,000 tests).

    So before such a call, where the function's own instructions may have written the register since the context was last in
    step (the start, a call to something that surely writes it, or another of these stores), and the call's leftover can be read
    (the register is read before it is written again -- through calls that may leave it too; a return counts as a read), the
    register is stored to the context, `sw $v0, lift_out_v0`: m2c writes `lift_out_v0 = <its value there>;`, and the context then
    holds the original's v0 when the callee leaves it alone. Registers already in the plan's mirror set are in step anyway.
    Only where m2c takes the call's result from that register (results: declared_results of the function's context) -- a callee
    declared void, or with its result in the other register, leaves a later read of it to m2c's 'unset register', which the
    reload pass already keeps in step. A call through a register (jalr) is left as before. (Without results: none.)"""
    from mips import cfg
    addrs, succ = cfg(f)
    if not addrs:
        return {}
    mirror = set(p['mirror'])

    def callee_at(ds):
        """The call whose delay slot is ds: (jal address, callee name or None), or None if ds isn't a call's delay slot."""
        j = f['ins'].get(ds - 4)
        if not j or j[0] not in ('jal', 'jalr') or ds - 4 not in succ or succ[ds - 4] != [ds]:
            return None
        return ds - 4, (f['calls'].get(ds) or f['calls'].get(ds - 4)) if j[0] == 'jal' else None

    def surely_writes(callee, r):
        return callee is not None and r in sure(callee)

    def live_after(start, r):
        seen, work = set(), [start]
        while work:
            pc = work.pop()
            if pc in seen or pc not in f['ins']:
                continue
            seen.add(pc)
            mn, ops = f['ins'][pc]
            u, d = uses_defs_in(f, pc)
            if r in u:
                return True
            call = callee_at(pc)
            if call and surely_writes(call[1], r):
                continue                    # (the callee writes it: the leftover can't be read past here)
            if r in d and not call:
                continue
            if r in d and call:
                continue                    # (the delay slot writes it, then the call may leave that: in step only if stored --
                                            #  not this call's leftover any more)
            following = succ.get(pc, [])
            if not following:
                before = f['ins'].get(pc - 4, ('', ''))
                if before[0] == 'jr' or before[0] in ('j', 'b') or before[0] in BRANCH:
                    return True             # (a return, or a jump out: what's left there is the function's)
            work += following
        return False

    sites = {}
    for r in RESULT_REGISTERS:
        if r in mirror:
            continue
        # (may be out of step at each instruction's start: written by the function's own instruction since it was last in step)
        state = {addrs[0]: False}
        work = [addrs[0]]
        while work:
            pc = work.pop()
            dirty = state[pc]
            mn, ops = f['ins'][pc]
            if r in uses_defs(mn, ops)[1] and not is_restore(mn, ops):
                dirty = True
            call = callee_at(pc)
            if call:
                jal, callee = call
                if surely_writes(callee, r):
                    dirty = False
                elif dirty and callee is not None and (results or {}).get(callee) == r and live_after(pc + 4, r):
                    sites.setdefault(jal, set()).add(r)
                    dirty = False
            for n in succ.get(pc, []):
                new = dirty if n not in state else (state[n] or dirty)
                if n not in state or new != state[n]:
                    state[n] = new
                    work.append(n)
    return sites


def reads_own_v1_after_call(f, sure):
    """(2026-11-11, trial 5) Whether the function reads v1 after a call that may have left it alone, when v1 then holds
    what the function's own instructions put there before the call.

    m2c takes v1 after any call to be the call's (the low half of a 64-bit result), and lift.py read_v1_after_calls writes
    that read as ctx->r3. Right when the callee wrote v1. But a callee that leaves v1 alone hands back the CALLER's v1, and
    the converted caller keeps its own values in C variables: the context's r3 is then whatever was there before.
    func_80117F28: `lw $v1, -0x6E08($v1); addiu $v0, $s1, 1; jal rw_wrap_s1_v0; ...; bne $s1, $v1, ...` -- the helper never
    touches v1, and the converted loop compared with a stale ctx->r3 (the tester: 968 of 64,018 ended differently).

    The plain functions' tester can't see this: its stand-ins change v1 on every call. abi.py's stand-ins change only what
    the callee may change, so there it shows. lift.py's round puts v1 in the reload set of such a function: every write of
    v1 by its own instructions is then stored to the context, and the read after the call is loaded from the context --
    the callee's v1 if it wrote one, the function's own if it didn't, as in the original.

    True if at some read of v1 both may hold: no instruction of the function's own has written v1 since the last call
    (so m2c reads it as that call's), and one has since the context was last in step (the start, or a call to something
    that surely writes v1: sure, abi.must_write's sets)."""
    from mips import cfg, uses_defs_at
    addrs, succ = cfg(f)
    if not addrs:
        return False
    state = {addrs[0]: (False, False)}      # (own write since in step, no own write since the last call)
    work = [addrs[0]]
    while work:
        pc = work.pop()
        dirty, left = state[pc]
        mn, ops = f['ins'][pc]
        # (uses_defs_at, as reload_sites reads: the first half of an unaligned load, lwl with lwr, reads nothing that lasts.)
        u, d = uses_defs_at(f, pc, succ)[0], uses_defs(mn, ops)[1]
        if 'v1' in u and dirty and left:
            return True
        if 'v1' in d and not is_restore(mn, ops):
            dirty, left = True, False
        before = f['ins'].get(pc - 4, ('', ''))
        if before[0] in ('jal', 'jalr'):        # (pc is a call's delay slot: the call runs after it)
            callee = (f['calls'].get(pc) or f['calls'].get(pc - 4)) if before[0] == 'jal' else None
            if callee is not None and 'v1' in sure(callee):
                dirty = False
            left = True
        for n in succ.get(pc, []):
            new = (dirty, left) if n not in state else (state[n][0] or dirty, state[n][1] or left)
            if n not in state or new != state[n]:
                state[n] = new
                work.append(n)
    return False


def reload_load(r):
    if r.startswith('d') and is_float(r[1:]):       # (a pair, read as a double: reload_sites. 2026-11-12, agent "refused2")
        return '    ldc1 $%s, lift_ctx_%s' % (r[1:], r)
    return '    %s $%s, lift_ctx_%s' % ('lwc1' if is_float(r) else 'lw', r, r)


def reload_sites(f, regs, unstored, analysis, mirror=()):
    """Where the context's value of each register in regs has to be loaded for m2c (regs, 2026-11-08): at every instruction of the
    function's own, and every call to something that reads it, that reads the register before the function has written it since
    the last call on every way there. Those are what m2c calls 'unset': the value is what the last call left. Every write of the
    function's own is stored to the context (a mirror register), so the context holds the original's value there. {address: set
    of registers}, or (None, why): one of them written by an instruction that gets no store (a table jump, an mfhi), or read as
    half of a double."""
    from mips import cfg, uses_defs_at
    addrs, succ = cfg(f)
    regs = set(regs)
    for a, (mn, ops) in f['ins'].items():
        u, d = uses_defs(mn, ops)
        if a in unstored and d & regs:
            return None, 'ABI: a register to reload is written by an instruction with no store'
    # (2026-11-12, agent "refused2") Until now a register to reload that any double instruction touched anywhere was refused
    # here. But only a reload of half of a double is wrong: one lwc1 at a place that reads the pair, or an odd register. A
    # double instruction's write of the pair is stored whole (stores: one sdc1), so the context holds the even register's
    # single word as the original's register does; a reload where it is read as a single float (func_8013A63C's `mfc1 $a1,
    # $f0`, where f0 is either the float the function worked out through doubles or, on the way that doesn't, what it came in
    # with) loads exactly that. So a site is refused where it reads a reload register as half of a double (below), and an odd
    # float register is refused wherever: what m2c's single float there would be is half of a pair.
    if [r for r in regs if is_float(r) and int(r[1:]) % 2]:
        return None, 'ABI: a register to reload is half of a double'
    # (2026-11-11, trial 5: a saved register to reload -- one a port helper hands back, lift.py handed_back -- is, on the
    #  way in, the value m2c already knows it by; only after a call is it the context's. Loaded at the start it would turn
    #  the function's own `sw $s5, 0x24($sp)` from saving the register into a store of a value.)
    states = {addrs[0]: frozenset((PARAMETERS | INT_SAVED) & regs)}
    work = [addrs[0]]
    sites = {}
    while work:
        pc = work.pop()
        written = set(states[pc])
        mn, ops = f['ins'][pc]
        before = f['ins'].get(pc - 4, ('', ''))
        reads = uses_defs_at(f, pc, succ)[0] & regs
        if mn == 'jal':
            callee = f['calls'].get(pc + 4) or f['calls'].get(pc)
            delay = f['ins'].get(pc + 4)
            given = uses_defs(*delay)[1] if delay else set()
            reads |= (set(analysis.get(callee, {}).get('use', [])) & PARAMETERS & regs) - given
        need = reads - written
        if need and ((mn.endswith('.d') or mn in ('ldc1', 'sdc1') or re.match(r'(cvt|trunc|round|floor|ceil)\.\w\.[dl]$', mn))
                     and uses_defs_in(f, pc)[0] & need):
            # (Read here as a pair: reloaded as the pair, `ldc1 $f2, lift_ctx_df2`, the context's double -- only where both halves
            #  are in step there, the odd one a mirror register too (abi.py plan: a double write of the even one puts both in),
            #  so the context's pair is the original's. func_800EBD2C's `c.lt.d $f0, $f2` after a call. "refused2")
            for r in sorted(uses_defs_in(f, pc)[0] & need):
                if not is_float(r) or int(r[1:]) % 2 or 'f%d' % (int(r[1:]) + 1) not in mirror:
                    return None, 'ABI: a register to reload is half of a double'
                need = (need - {r}) | {'d' + r}
        if need and mn == 'jal' and {'f%d' % (int(r[1:]) + 1) for r in need if is_float(r)} & set(analysis.get(callee, {}).get('use', [])):
            return None, 'ABI: a register to reload is half of a double'      # (a callee's double parameter: lift.py double_kinds)
        if need:
            sites[pc] = sites.get(pc, set()) | need
        written |= uses_defs(mn, ops)[1] & regs
        if before[0] in ('jal', 'jalr'):
            written = set()     # (the call has run: every scratch register is what it left)
        following = succ.get(pc, [])
        for n in following:
            new = frozenset(written) if n not in states else states[n] & frozenset(written)
            if n not in states or new != states[n]:
                states[n] = new
                work.append(n)
    return sites, None


def rewrite_asm(text, name, f, p, analysis, sure, bounds=False, results=None):
    """The function's assembly (to_asm.py's, as m2c gets it) with the plan's loads at the start and stores after each write of a
    mirror register (see the notes at the top). Returns (text, None), or (None, why). (analysis and sure, must_write's sets, for
    table_jumps.) bounds: a table jump's bounds check gets no store either (table_jumps): lift.py asks for that only where
    m2c's switch came out with cases outside it otherwise (v2, 2026-11-08: with it always, 46 functions whose bounds check's
    branch had its ways out stored were lost, for 19 gained).)"""
    lines = text.split('\n')
    start = lines.index('glabel ' + name) + 1
    from to_asm import MORE
    addrs = sorted(f['ins'])
    # The instruction lines after the function's label, in address order (to_asm.py writes one line per instruction, and
    # labels on lines of their own): their place in `lines`, and the labels in front of each.
    where, labels, pending = {}, {}, []
    i = start
    k = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'\.L[0-9A-F]{8}:$', line):
            pending.append(i)
        elif line.endswith(MORE):
            pass        # (2026-11-12, agent "refused": part of the instruction on the line before -- to_asm.py find_slot_tables)
        elif line.startswith('    ') and line.strip():
            if k >= len(addrs):
                return None, 'ABI: its assembly has more lines than instructions'
            where[addrs[k]] = i
            labels[addrs[k]] = pending
            pending = []
            k += 1
        i += 1
    if k != len(addrs):
        return None, 'ABI: its assembly has fewer lines than instructions'
    mirror = set(p['mirror'])
    exact = {}
    unstored, why = table_jumps(f, mirror, analysis, sure, bounds, exact)
    if unstored is None:
        return None, why
    p['exact'] = exact
    before, after, replace = {}, {}, {}      # line number -> lines to put before it / after it / in its place
    # (regs2, 2026-11-09: the exact values table_jumps found: an mfhi's top half, after the mfhi, from the mult's operands, stored
    #  as the mult reads them. The marker is a store of $zero to a name of its own, only to mark the place in m2c's C;
    #  add_declarations puts the value there. A table jump's register was tried the same way and dropped: the port's translated
    #  code leaves the table slot's address in it, not the case's, and the tester said so.)
    for a, h in exact.get('hi', {}).items():
        before.setdefault(where[int(h['mult'], 16)], []).extend(['    sw $%s, lift_mh_%s_a' % (h['rs'], a), '    sw $%s, lift_mh_%s_b' % (h['rt'], a)])
        after.setdefault(where[int(a, 16)], []).append('    sw $zero, lift_hi_%s_%s' % (h['reg'], a))
    exact_hi_at = {int(a, 16) for a in exact.get('hi', {})}
    # (slot, 2026-11-11: a table jump's register, exact_slot. Before the lui, so the jump's own four instructions stay as m2c
    #  needs them: the scaled index as the addu will read it, stored to a name of its own, and a marker where add_declarations
    #  puts `lift_out_<r> = <the table's address> + <that index>`. Nothing between there and the jr reads or writes the
    #  register in the context -- the four instructions aren't calls -- so the context holds it from the lw on, as the original's
    #  register does.)
    for a, t in exact.get('slot', {}).items():
        before.setdefault(where[int(t['lui'], 16)], []).extend(['    sw $%s, lift_tj_%s_i' % (t['index'], a), '    sw $zero, lift_slot_%s_%s' % (t['reg'], a)])
    if p.get('reload'):
        sites, why = reload_sites(f, p['reload'], unstored, analysis, p['mirror'])
        if sites is None:
            return None, why
        doubles = sorted({r for regs in sites.values() for r in regs if r.startswith('d')})
        if doubles:     # (only then, so every other plan is as it was: 2026-11-12, agent "refused2")
            p['reload_doubles'] = doubles
        for a, regs in sorted(sites.items()):
            n = addrs.index(a)
            prev = f['ins'].get(a - 4)
            at = a
            if n > 0 and addrs[n - 1] == a - 4 and prev[0] in JUMPS:
                # (In a delay slot: before its branch, which doesn't write them.)
                if labels[a] or uses_defs(*prev)[1] & regs:
                    return None, 'ABI: a register to reload is read in a delay slot it cannot go before'
                at = a - 4
            before.setdefault(where[at], []).extend(reload_load(r) for r in sorted(regs))
    stubs = []                               # (the ways out of branches whose delay slot stays: see below)
    for n, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        if is_restore(mn, ops):
            continue        # (putting back a saved register: LiftAbiKeep does that in the C++)
        # (regs: the first half of an unaligned load whose partner is next gets no store, so m2c sees the pair as one -- a store
        #  between them made its first half read the register as it was: func_8010ECC4, func_80114A8C. Only on the reload pass.)
        following = f['ins'].get(a + 4, ('', ''))
        if p.get('reload') and mn in ('lwl', 'lwr') and following[0] in ('lwl', 'lwr') and following[0] != mn and uses_defs(mn, ops)[1] == uses_defs(*following)[1]:
            continue
        if a in unstored or a in exact_hi_at:
            continue        # (a jump through a table's own instructions: see table_jumps. An exact mfhi has its own store, above)
        regs = sorted(uses_defs(mn, ops)[1] & mirror)
        if not regs:
            continue
        prev = f['ins'].get(a - 4)
        if n == 0 or addrs[n - 1] != a - 4 or prev[0] not in JUMPS:
            after.setdefault(where[a], []).extend(stores(mn, ops, regs))
            continue
        if labels[a]:
            return None, 'ABI: something branches into the delay slot of an instruction it moves'
        likely = prev[0] in BRANCH and prev[0].endswith('l') and prev[0] not in ('bgezal', 'bltzal')
        bu, bd = uses_defs_in(f, a - 4)
        u, d = uses_defs_in(f, a)
        if not likely and not (bu & d) and not (u & bd):
            # In a delay slot: moved before its branch, with the stores after it, and a nop left in the slot.
            before.setdefault(where[a - 4], []).extend([lines[where[a]]] + stores(mn, ops, regs))
            replace[where[a]] = ['    nop']
            continue
        # A conditional branch whose delay slot can't be moved: it compares what the slot changes (`beq $v0, $zero, .L1;
        # addiu $v0, $zero, 1`, the commonest), or it's a "likely" branch, whose slot runs only when it's taken. The slot stays,
        # and the stores go on the ways out of it: where it falls through, straight after the slot and before any label of the
        # next instruction (so only that way runs them; a likely branch's slot doesn't run that way, so nothing there); and where
        # it's taken, through a few instructions of its own at the end of the function, which store and then jump on to the
        # branch's target. (2026-11-07: 385 functions were refused here at first.)
        target = re.search(r'(\.L[0-9A-F]{8})$', lines[where[a - 4]])
        if prev[0] not in BRANCH or prev[0] in ('bgezal', 'bltzal') or not target:
            return None, 'ABI: a delay slot it would move depends on its branch'
        stub = '.Labi_%08X' % a
        replace[where[a - 4]] = [lines[where[a - 4]][:target.start()] + stub]
        if not likely:
            after.setdefault(where[a], []).extend(stores(mn, ops, regs))
        stubs += [stub + ':'] + stores(mn, ops, regs) + ['    j ' + target.group(1), '    nop']
    # (C1, 2026-11-11: a result register stored before a call that may leave it -- call_store_sites. After whatever else goes
    #  before the jal: a delay slot moved there, with its own stores. A delay slot that writes the register itself goes before
    #  the jal first, as above; if it can't be moved -- something branches into it, or it was kept for its branch -- the
    #  register is left as before, out of step.)
    p['call_stores'] = []
    for jal, regs in sorted(call_store_sites(f, p, analysis, sure, results).items()):
        ds = jal + 4
        slot = f['ins'].get(ds)
        slot_writes = slot is not None and bool(uses_defs(*slot)[1] & regs)
        moved = replace.get(where[ds]) == ['    nop']
        if slot_writes and not moved:
            if labels.get(ds) or where[ds] in replace or f['ins'][jal][0] != 'jal' or 'ra' in uses_defs_in(f, ds)[0]:
                regs = {r for r in regs if r not in uses_defs(*slot)[1]}
            else:
                before.setdefault(where[jal], []).append(lines[where[ds]])
                replace[where[ds]] = ['    nop']
        for r in sorted(regs):
            before.setdefault(where[jal], []).append(store(r))
            if r not in p['call_stores']:
                p['call_stores'].append(r)
    p['call_stores'].sort()
    out = lines[:start] + input_loads(p)
    for i in range(start, len(lines)):
        out += before.get(i, []) + replace.get(i, [lines[i]]) + after.get(i, [])
    while out and not out[-1].strip():
        out.pop()
    return '\n'.join(out + stubs) + '\n', None


def context_lines(p):
    """Declarations for m2c's context: the names the loads and stores use, as 32-bit integers or floats."""
    out = []
    for r in p['inputs']:
        if r not in in_double_halves(p):
            out.append('extern %s lift_in_%s;' % ('f32' if is_float(r) else 's32', r))
    for e in p.get('in_doubles', []):       # (2026-11-11: a pair handed in as a double)
        out.append('extern f64 lift_in_d%s;' % e)
    for r in p['mirror']:
        out.append('extern %s lift_out_%s;' % ('f32' if is_float(r) else 's32', r))
    for r in p.get('doubles', []):
        out.append('extern f64 lift_out_d%s;' % r)
    for r in p.get('reload', []):
        out.append('extern %s lift_ctx_%s;' % ('f32' if is_float(r) else 's32', r))
    for r in p.get('reload_doubles', []):       # (a pair reloaded as a double: reload_sites. "refused2")
        out.append('extern f64 lift_ctx_%s;' % r)
    for r in p.get('call_stores', []):      # (C1: call_store_sites)
        out.append('extern %s lift_out_%s;' % ('f32' if is_float(r) else 's32', r))
    ex = p.get('exact', {})
    for a, h in ex.get('hi', {}).items():
        out += ['extern s32 lift_mh_%s_a;' % a, 'extern s32 lift_mh_%s_b;' % a, 'extern s32 lift_hi_%s_%s;' % (h['reg'], a)]
    for a, t in ex.get('slot', {}).items():     # (slot, 2026-11-11)
        out += ['extern s32 lift_tj_%s_i;' % a, 'extern s32 lift_slot_%s_%s;' % (t['reg'], a)]
    return out


def cpp_declarations(p):
    """The C++ for those names, at the top of the converted function (after it has ctx): what each stands for in the context."""
    out = []
    for e in p.get('in_doubles', []):
        # (2026-11-11: the double's low word is the even register, its high word the odd one, as LiftAbiOutDouble has them.)
        odd = 'f%d' % (int(e[1:]) + 1)
        out.append('    const f64 lift_in_d%s = __builtin_bit_cast(f64, ((uint64_t)(u32)%s << 32) | (uint64_t)(u32)%s);   // %s and %s as they came in, a double' % (
            e, context_field(odd), context_field(e), e, odd))
    for r in p['inputs']:
        if r in in_double_halves(p):
            continue
        if is_float(r):
            out.append('    const f32 lift_in_%s = __builtin_bit_cast(f32, (u32)%s);   // %s as it came in' % (r, context_field(r), r))
        else:
            out.append('    const s32 lift_in_%s = (s32)%s;   // %s as it came in' % (r, context_field(r), r))
    for r in p['keep']:
        if is_float(r):
            out.append('    LiftAbiKeepFloat lift_keep_%s(%s);   // %s goes back as it came in' % (r, context_field(r), r))
        else:
            out.append('    LiftAbiKeep lift_keep_%s(%s);   // %s goes back as it came in' % (r, context_field(r), r))
    for r in p['mirror']:
        out.append('    %s lift_out_%s{%s};   // the context\'s %s, kept in step with the original\'s' % (
            'LiftAbiOutFloat' if is_float(r) else 'LiftAbiOut', r, context_field(r), r))
    for r in p.get('call_stores', []):      # (C1: call_store_sites. Never one of the mirror registers.)
        out.append('    %s lift_out_%s{%s};   // the context\'s %s, stored before a call that may leave it' % (
            'LiftAbiOutFloat' if is_float(r) else 'LiftAbiOut', r, context_field(r), r))
    for r in p.get('doubles', []):
        out.append('    LiftAbiOutDouble lift_out_d%s{ctx->%s};   // %s and %s in the context, written together as a double' % (
            r, r, r, 'f%d' % (int(r[1:]) + 1)))
    for a in p.get('exact', {}).get('hi', {}):
        out.append('    s32 lift_mh_%s_a = 0, lift_mh_%s_b = 0;   // the operands of the mult before the mfhi at %s, as it read them' % (a, a, a))
    for a in p.get('exact', {}).get('slot', {}):
        out.append('    s32 lift_tj_%s_i = 0;   // the scaled index of the table jump at %s, as its addu read it' % (a, a))
    return out


def add_declarations(cpp, name, p):
    """Puts cpp_declarations at the start of the function's body, straight after its signature (which has ctx by now)."""
    sig = re.search(r'^.*\b' + re.escape(name) + r'\(recomp_context\* ctx.*\) \{$', cpp, re.M)
    if not sig:
        return None
    # (2026-11-11: after the frame and its saves when there is one, so LiftFrame is made first and undone LAST. Its destructor puts the
    #  saved registers back from their slots as the original's epilogue does -- the user's decision on frames a function overwrites
    #  itself -- and a LiftAbiKeep undone after it would put back the entry value over that: lift_call.h LiftFrame.)
    at = sig.end()
    frame = re.compile(r'\n    LiftFrame frame\(ctx, 0x[0-9A-F]+\);((?:\n    frame\.keep(?:_double)?\(0x[0-9A-F]+, ctx->[\w.]+\);)*)')
    fm = frame.match(cpp, at)
    if fm:
        at = fm.end()
    cpp = cpp[:at] + '\n' + '\n'.join(cpp_declarations(p)) + cpp[at:]
    # (regs2, 2026-11-09: the exact values' places. Each marker must be there exactly once, and each mult's operands stored once:
    #  otherwise refused, rather than leave the register out of step.)
    ex = p.get('exact', {})
    for a, h in ex.get('hi', {}).items():
        if len(re.findall(r'^\s*lift_mh_%s_a = ' % a, cpp, re.M)) != 1 or len(re.findall(r'^\s*lift_mh_%s_b = ' % a, cpp, re.M)) != 1:
            return None
        if h['kind'] == 'mult':
            value = '(s32)(((int64_t)(s32)lift_mh_%s_a * (int64_t)(s32)lift_mh_%s_b) >> 32)' % (a, a)
        else:
            value = '(s32)(((uint64_t)(u32)lift_mh_%s_a * (uint64_t)(u32)lift_mh_%s_b) >> 32)' % (a, a)
        cpp, n = re.subn(r'\blift_hi_%s_%s = 0;' % (h['reg'], a), 'lift_out_%s = %s;   // the top half of the product (mfhi)' % (h['reg'], value), cpp)
        if n != 1:
            return None
    # (slot, 2026-11-11: the same for a table jump's register. Each index stored once and each marker there once, or refused.)
    for a, t in ex.get('slot', {}).items():
        if len(re.findall(r'^\s*lift_tj_%s_i = ' % a, cpp, re.M)) != 1:
            return None
        value = '(s32)(0x%su + (u32)lift_tj_%s_i)' % (t['table'], a)
        cpp, n = re.subn(r'\blift_slot_%s_%s = 0;' % (t['reg'], a),
                         'lift_out_%s = %s;   // the table slot\'s address, which the translated jump leaves there' % (t['reg'], value), cpp)
        if n != 1:
            return None
    # (regs: lift_ctx_<r> is the register in the context, read where m2c's C reads it.)
    def read(m):
        if m.group(1).startswith('d') and is_float(m.group(1)[1:]):
            return '((f64)ctx->%s.d)' % m.group(1)[1:]      # (the even register's 64 bits, the pair: reload_sites. "refused2")
        return ('__builtin_bit_cast(f32, (u32)%s)' if is_float(m.group(1)) else '((s32)%s)') % context_field(m.group(1))
    return re.sub(r'\blift_ctx_(\w+)\b', read, cpp)


# The C++ the declarations use. Written into every file that compiles converted code with them (lift.py: the compile check, the
# tester, lifted.cpp), only where one of these functions is in it.
CPP_HELPERS = r'''
// ---- For functions that don't keep to the N64's calling rules (tools/lift/abi.py) ----
// A register in the context the converted function keeps in step with the original's: each of the original's own writes of
// it is a write of this. Integers are kept sign-extended, as the translated code keeps them.
struct LiftAbiOut {
    gpr& r;
    template <class T> const LiftAbiOut& operator=(T value) const {
        auto v = lift_plain(value);
        static_assert(!std::is_floating_point_v<decltype(v)>, "a float written into an integer register");
        u32 word;
        if constexpr (std::is_class_v<decltype(v)>) word = v.a;
        else word = (u32)v;
        r = (gpr)(int64_t)(s32)word;
        return *this;
    }
};
// The same for a float register: its 32 bits in the context.
struct LiftAbiOutFloat {
    uint32_t& bits;
    const LiftAbiOutFloat& operator=(f32 value) const { bits = __builtin_bit_cast(uint32_t, value); return *this; }
};
// A pair of float registers written together as a double (an even register and the odd one after it): the double's low word
// is the even register, its high word the odd one, as the N64 keeps them in its 32-bit float mode.
struct LiftAbiOutDouble {
    fpr& pair;
    const LiftAbiOutDouble& operator=(f64 value) const {
        const uint64_t bits = __builtin_bit_cast(uint64_t, value);
        pair.u32l = (uint32_t)bits;
        pair.u32h = (uint32_t)(bits >> 32);
        return *this;
    }
};
// A callee-saved register the function changes in the context for a callee: put back as it came in, on the way out.
struct LiftAbiKeep {
    gpr& r;
    gpr was;
    explicit LiftAbiKeep(gpr& reg) : r(reg), was(reg) {}
    ~LiftAbiKeep() { r = was; }
};
struct LiftAbiKeepFloat {
    uint32_t& bits;
    uint32_t was;
    explicit LiftAbiKeepFloat(uint32_t& b) : bits(b), was(b) {}
    ~LiftAbiKeepFloat() { bits = was; }
};
'''


def may_write(functions, analysis):
    """For the tester's stand-ins: the scratch registers each function may change, as a mask (bit) -- what its own instructions
    write, and what anything it calls or jumps into may. A call through a pointer, one the translator looks up at run time, or a
    function not in the translated code (a system function): any of them."""
    every = set(STAND_IN_GPR) | set(STAND_IN_FPR)
    own = {}
    for n, f in functions.items():
        regs = set()
        unknown = False
        for a, (mn, ops) in f['ins'].items():
            regs |= uses_defs(mn, ops)[1]
            if mn == 'jalr' or (mn == 'jal' and not (f['calls'].get(a + 4) or f['calls'].get(a))):
                unknown = True
        own[n] = every if unknown else regs & every
    result = dict(own)
    changed = True
    while changed:
        changed = False
        for n, f in functions.items():
            new = set(result[n])
            for c in set(f['calls'].values()):
                new |= result.get(c, every)
            # (A function the mod replaces may be the mod's copy when it runs (tools/lift/modrep.py): what either writes.)
            new |= result.get('modrep_' + n, set())
            if new != result[n]:
                result[n] = new
                changed = True
    return lambda name: mask(result.get(name, every))


def must_write(functions, as_sets=False):
    """For the tester's stand-ins: the scratch registers each function surely writes before it returns, on every way through it
    -- by an instruction of its own, or a call to something that surely writes it -- as a mask. lift.py's result_registers, for
    every register the stand-ins change, not only v0 and f0. A stand-in always changes these (as the real function would: a
    converted caller takes a call's result from v0 and relies on it being the callee's), and changes the others it may change
    only at random. (2026-11-07: without it the stand-in for func_8002E648 sometimes left v0 alone, which the real function
    never does, and its callers differed for that.) Where it isn't known -- a system function, a call through a pointer or one
    looked up at run time, a way out that isn't a return, a function calling itself round again -- this says less is sure,
    which only makes the stand-in leave registers alone more often: more ways tested, never fewer. A function not in the
    translated code (a system function): v0, v1, f0 and f1, as every stand-in always changed them before."""
    from mips import cfg
    every = set(STAND_IN_GPR) | set(STAND_IN_FPR)
    known = {}

    def sure(name):
        if name in known:
            return known[name]
        f = functions.get(name)
        if f is None:
            return {'v0', 'v1', 'f0', 'f1'}
        known[name] = set()             # (while it's worked out: a call back into it counts as writing nothing)
        if not f['ins']:
            return set()
        addrs, succ = cfg(f)
        state = {addrs[0]: frozenset()}
        work = [addrs[0]]
        exits = []
        while work:
            pc = work.pop()
            mn, ops = f['ins'][pc]
            now = set(state[pc]) | (uses_defs(mn, ops)[1] & every)
            if mn == 'jal':
                callee = f['calls'].get(pc + 4) or f['calls'].get(pc)
                now |= sure(callee) if callee else set()
            following = succ.get(pc, [])
            if not following:
                before = f['ins'].get(pc - 4, ('', ''))
                exits.append(now if before[0] == 'jr' and '$ra' in before[1] else set())
            for n in following:
                new = frozenset(now) if n not in state else state[n] & frozenset(now)
                if n not in state or new != state[n]:
                    state[n] = new
                    work.append(n)
        known[name] = set.intersection(*[set(e) for e in exits]) if exits else set()
        # (A function the mod replaces may be the mod's copy when it runs (tools/lift/modrep.py): only what both surely write.)
        if not name.startswith('modrep_') and 'modrep_' + name in functions:
            known[name] = known[name] & sure('modrep_' + name)
        return known[name]
    return sure if as_sets else (lambda name: mask(sure(name)))


def extra_reads(name, analysis):
    """For the tester's stand-ins: the registers a function reads other than its parameters (and gp, sp, ra), as a mask."""
    return mask(set(analysis.get(name, {}).get('use', [])) - PARAMETERS - {'gp', 'sp', 'ra', 'zero', 'hi', 'lo'})
