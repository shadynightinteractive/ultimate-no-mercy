#!/usr/bin/env python3
"""Step 4: puts back together the game functions the translator split in pieces (scratch/lift_merge prototype, 2026-11-07).

Why they're split: the translator (N64Recomp) takes each function's start and size from syms/dump.toml, which came from splat's
disassembly, plus syms/extra_funcs.txt, functions added by hand wherever code jumped to an address the list didn't start a function
at (the translator can't follow a jump past its function's end otherwise). So one N64 function can be several translated ones: the
first part ends in `j L_X`, which the translator writes as `func_X(rdram, ctx); return;`, and func_X carries on in the same stack
frame. An added function inside an existing one is a second copy of the same instructions (func_8002C574 holds func_8002C57C's).

A piece only makes sense with the rest of its function, so the converter takes them together: the ENTRY (the piece the game calls)
with every piece it can jump to, as one function, made from the instructions themselves (which are the same wherever two copies
overlap). The pieces the game never calls -- reached only by these jumps -- keep their translated originals, unchanged, and nothing
replaces them: once the entry is converted, the converted entry runs its own copy of their code.

A group is put together when (the rest stay as they are, with the reason in plan()):
  - its entry is a function's start: not the middle of one (a translated function that returns through a stack frame it never
    made, which no call can reach), and not a copy of another function's middle that nothing calls;
  - every piece is in the same section (overlays share addresses, so an address alone doesn't say which code it is), none is the
    mod's own assembly (rw_*), and copies at the same address hold the same instruction;
  - together, the pieces start at the entry and cover memory without a gap anything runs into, and every instruction is on
    some way from the entry (none that can never run, which the random tester could never reach).
A piece may also be called from elsewhere, or jumped into by other functions: the group runs its own copy of the piece's
instructions, and the piece's translated original stays for the others (2026-11-08). A jump out with the frame released, to a
function the game calls on its own, is a tail call rather than a piece (build_group), kept as a call.
Whether the result is a whole function by the calling rules is lift.py's to check, as for any other.

    python merge.py          prints the plan
"""
import collections, glob, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mips

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
OVERLAYS = os.path.join(PORT, 'RecompiledFuncs', 'recomp_overlays.inl')
MOD = os.path.join(ROOT, 'mod')
CALLING = ('jal', 'jalr')


def sections():
    """Each translated function's section number, from the translator's own table of them. (The translator's static_N_XXXXXXXX
    functions, the targets of jumps it found inside section N, aren't in the table: their section is the N in the name.)"""
    found = {}
    current = None
    for line in open(OVERLAYS, encoding='utf8', errors='replace'):
        m = re.match(r'static FuncEntry section_(\d+)_\w+_funcs\[\]', line)
        if m:
            current = int(m.group(1))
            continue
        m = re.search(r'\.func = (\w+),', line)
        if m and current is not None:
            found[m.group(1)] = current
    return found


def section_of(name, table):
    m = re.match(r'static_(\d+)_[0-9A-F]{8}$', name)
    return int(m.group(1)) if m else table.get(name)


def jumps(f):
    """The pieces f jumps to: {address the translator noted it against: target}, for its calls that aren't calls (no jal before)."""
    return {a: c for a, c in f['calls'].items()
            if f['ins'].get(a - 4, ('',))[0] not in CALLING and f['ins'].get(a, ('',))[0] not in CALLING}


def called_from_outside(functions):
    """Functions the game can start running by a call, rather than only by a jump from a piece of the same function: called with jal,
    their address in the ROM or built in code (so maybe called through a pointer), the address of a jal the translator looks up at run
    time, or named in the mod's code."""
    out = set()
    words = set()
    for f in functions.values():
        for a, c in f['calls'].items():
            if not (f['ins'].get(a - 4, ('',))[0] not in CALLING and f['ins'].get(a, ('',))[0] not in CALLING):
                out.add(c)
        hi = {}
        for a in sorted(f['ins']):
            mn, ops = f['ins'][a]
            r = re.findall(r'\$(\w+)', ops)
            if mn == 'lui':
                hi[r[0]] = int(ops.split(',')[-1], 0) << 16
            elif mn == 'addiu' and len(r) == 2 and r[1] in hi:
                words.add((hi[r[1]] + int(ops.split(',')[-1], 0)) & 0xFFFFFFFF)
            elif mn == 'jal' and not (f['calls'].get(a + 4) or f['calls'].get(a)):
                words.add(int(ops, 0))
    rom = open(os.path.join(PORT, 'nomercy.z64'), 'rb').read()
    words |= {int.from_bytes(rom[i:i + 4], 'big') for i in range(0, len(rom) - 3, 4)}
    for n in functions:
        m = re.search(r'_([0-9A-F]{8})', n)
        if m and int(m.group(1), 16) in words:
            out.add(n)
    text = []
    for base, _, files in os.walk(MOD):
        if 'build' in base.split(os.sep):
            continue
        for fn in files:
            if fn.endswith(('.c', '.h', '.cpp', '.s', '.S', '.py', '.toml', '.ld')):
                text.append(open(os.path.join(base, fn), encoding='utf8', errors='replace').read())
    named = set(re.findall(r'\b(?:func|static_\d+)_[0-9A-F]{8}\w*', '\n'.join(text)))
    return out | (named & set(functions))


def stack_after(ins, tables, start):
    """Where the stack pointer is after each instruction, against where it was at `start`, on the way the walk first reaches it:
    {address: offset}. An instruction missing from it is one no way from `start` reaches. (lift.py not_a_whole_function checks
    later that every way to an instruction agrees.)"""
    addrs, succ = mips.cfg({'ins': ins, 'tables': tables})
    seen = {}
    work = [(start, 0)]
    while work:
        pc, offset = work.pop()
        if pc in seen:
            continue
        mn, ops = ins[pc]
        m = mips.SP_STEP.match(ops)
        if mn == 'addiu' and m:
            offset += int(m.group(1), 0)
        seen[pc] = offset
        for n in succ.get(pc, []):
            work.append((n, offset))
    return seen


def union(functions, pieces):
    """The pieces' instructions and jump tables together, by address (the first copy's, where two hold one)."""
    ins, tables = {}, {}
    for p in pieces:
        for a, i in functions[p]['ins'].items():
            ins.setdefault(a, i)
        for a, t in functions[p].get('tables', {}).items():
            tables.setdefault(a, t)
    return ins, tables


TAIL_JUMPS = (mips.BRANCH - {'bgezal', 'bltzal'}) | {'j', 'b'}     # (a jump or branch that doesn't link: not a call)


def jump_instruction(ins, noted, target):
    """The address of the jump or branch whose way out the translator noted at `noted` (it notes a jump against its delay slot),
    if that instruction is one and goes to `target` (an address). None otherwise: the translator also notes a function running
    off its end into the next one, which is no jump instruction (func_800004D0's last delay slot, into func_80000580)."""
    i = ins.get(noted - 4)
    m = re.search(r'L_([0-9A-F]{8})', i[1]) if i and i[0] in TAIL_JUMPS else None
    return noted - 4 if m and int(m.group(1), 16) == target else None


def build_group(functions, entry, callable_):
    """The entry and what its jumps reach, decided jump by jump (2026-11-08, scratch/lift_split):

      - a TAIL CALL: a jump or branch (not a run off the end: jump_instruction) that leaves with the stack where the entry
        found it (its frame, if it made one, already released), to a function the game also calls on its own, whose
        instructions aren't already part of this one. That is `return f(...)`
        compiled as a jump: the target runs as a whole function of its own and returns for this one. It stays a call, to the
        target as it is (its translated original, or its conversion), made after the frame is released, as the original
        makes it (lift.py TAIL_HELPER);
      - otherwise a PIECE: the rest of this function -- it runs in this function's frame, or reads what this function left in
        its registers -- whose instructions become part of the whole. Pieces join until nothing new is reached.

    Returns (pieces, tails, instructions, stack): tails {address the translator noted the jump at: target}, instructions the
    pieces' together, stack what stack_after found from the entry over them."""
    pieces = [entry]
    tails = {}
    while True:
        ins, tables = union(functions, pieces)
        stack = stack_after(ins, tables, min(functions[entry]['ins']))
        grew = False
        for p in list(pieces):
            for noted, t in sorted(jumps(functions[p]).items()):
                if t in pieces or noted in tails or noted not in stack:
                    continue        # (already in, already a tail call, or not reached -- yet)
                start = min(functions[t]['ins']) if functions.get(t, {}).get('ins') else None
                if stack[noted] == 0 and t in callable_ and start not in ins and jump_instruction(ins, noted, start) is not None:
                    tails[noted] = t
                else:
                    pieces.append(t)
                    grew = True
        if not grew:
            return pieces, tails, ins, stack


def holders(functions, section=None):
    """{name: another translated function of the same section that holds name's first instruction, starting before it}: the
    function that name is a copy of the middle of. (The first such by name, where there are several.)"""
    section = sections() if section is None else section
    by_section = collections.defaultdict(list)
    for n, f in functions.items():
        if f['ins']:
            by_section[section_of(n, section)].append(n)
    found = {}
    for members in by_section.values():
        members.sort()
        for n in members:
            start = min(functions[n]['ins'])
            for g in members:
                if g != n and start in functions[g]['ins'] and min(functions[g]['ins']) < start and \
                        functions[g]['ins'][start] == functions[n]['ins'][start]:
                    found[n] = g
                    break
    return found


FALLS_THROUGH_NOT = {'j', 'b', 'jr'}      # (the jumps that always leave: the instruction after their delay slot never runs next)


def plan(functions):
    """{entry: {'pieces': [...], 'tails': {...}, 'why': None or why it isn't put together}} for every function that jumps into
    another, and for every middle of a function that does (why says what it is).

    What changed on 2026-11-08 (scratch/lift_split), each with why it's exact:
      - The middle of a function -- one that returns through a stack frame it never made (mips.pops_a_frame_it_never_made) -- is
        never an entry. A call there would come back with the caller's stack moved, so no working call reaches it: the
        translator only has it because a symbol list named that address (another overlay's function starts there, or a hand-
        added function). Reached by jumps, it's a piece of whatever jumps there; reached by nothing, it's left out, with why.
      - A function nothing calls, which no other function holds the start of, is an entry like any other (func_801073EC).
      - A piece the game also calls on its own (func_80137818: its address is in the ROM), or jumps into from elsewhere, is
        still a piece: the whole function runs its own copy of the piece's instructions, which are the same instructions, and
        the piece's own translated original stays for its other callers. Before, those groups were refused.
      - A jump that leaves with the frame released, to a function the game calls on its own, is a tail call (build_group).
      - Pieces may leave a gap where nothing runs into it: the instruction before the gap is an unconditional jump's delay slot.
    Still refused, with the reason: pieces in different sections, a piece with no instructions, two copies that differ, a
    piece before the entry, a gap something runs into, and instructions no way from the entry reaches (the mod's stubs that jump
    to its own assembly, rw_*, leave the game's original instructions after the jump, which never run, so the random tester
    could never reach them)."""
    section = sections()
    outside = called_from_outside(functions)
    fragment = {n for n, f in functions.items() if f['ins'] and mips.pops_a_frame_it_never_made(f)}
    callable_ = {n for n in outside - fragment if functions.get(n, {}).get('ins')}
    # (PROPOSAL, rest100 2026-11-14; OFF unless LIFT_PORT_STUBS=1 -- it changes what counts, so it waits for the user.) The port's
    # stubs: a game function whose first instruction the port made `j rw_X` (its own MIPS for the function, rw_X, translated and
    # converted on its own). As the port runs it the function is that jump and nothing else: rw_X runs as a whole function of its
    # own, from its start, with the stack as the stub found it -- a tail call. So rw_X is taken as callable here, the stub becomes
    # `return rw_X()`, and the game's own instructions after the jump, which no way from the stub's start reaches, are left for
    # unreach.py to prove never run (rule E: a game function is entered only at its first instruction).
    stubs = set()
    if os.environ.get('LIFT_PORT_STUBS') == '1':
        for n, f in functions.items():
            if n.startswith('rw_') or not f['ins']:
                continue
            first = min(f['ins'])
            mn, ops = f['ins'][first]
            target = jumps(f).get(first + 4) or jumps(f).get(first)
            if mn == 'j' and target and target.startswith('rw_') and functions.get(target, {}).get('ins') and                     min(functions[target]['ins']) == int(ops.strip().lstrip('L_'), 16):
                stubs.add(n)
                callable_.add(target)
    jumped = set()
    for g in functions.values():
        jumped |= set(jumps(g).values())
    holder = holders(functions, section).get
    result = {}
    for name, f in functions.items():
        if not f['ins'] or not jumps(f):
            continue      # (no jumps out: nothing to put together)
        if name in fragment:
            if name not in jumped:
                h = holder(name)
                result[name] = {'pieces': [name], 'tails': {}, 'why': 'the middle of a function (%s): it returns through a stack '
                                'frame it never made, so no call reaches it' % ('of ' + h if h else 'its start unknown')}
            continue      # (jumped into: a piece of what jumps there)
        if name not in callable_ and name in jumped:
            continue      # (a piece reached only by jumps: part of another entry's group)
        if name not in callable_ and holder(name):
            result[name] = {'pieces': [name], 'tails': {}, 'why': 'the middle of %s: nothing calls it' % holder(name)}
            continue
        pieces, tails, ins, stack = build_group(functions, name, callable_)
        why = None
        if name in stubs and pieces == [name] and set(tails.values()) <= {t for t in tails.values() if t.startswith('rw_')}:
            why = None      # (a port stub, LIFT_PORT_STUBS: its instructions after the jump are left for unreach.py)
            result[name] = {'pieces': pieces, 'tails': tails, 'why': why}
            continue
        if any(p.startswith('rw_') for p in pieces) or name.startswith('rw_'):
            why = "jumps to the mod's own assembly"
        elif any(p not in functions or not functions[p]['ins'] for p in pieces):
            why = 'a piece has no instructions'
        elif len({section_of(p, section) for p in pieces}) != 1 or section_of(name, section) is None:
            why = 'pieces in different sections'
        else:
            for p in pieces:
                for a, i in functions[p]['ins'].items():
                    if not why and ins[a] != i:
                        why = 'two copies differ at %08X' % a
            addrs = sorted(ins)
            if not why and addrs[0] != min(functions[name]['ins']):
                why = 'a piece starts before the entry'
            for a, b in zip(addrs, addrs[1:]):
                if not why and b - a != 4 and not (ins.get(a - 4, ('',))[0] in FALLS_THROUGH_NOT and ins[a][0] not in mips.JUMPS):
                    why = 'the pieces leave a gap that something runs into'
            never = [a for a in addrs if a not in stack]
            if not why and never:
                why = ("the mod replaced it with a jump to its own assembly: the instructions after the jump never run"
                       if any(t.startswith('rw_') for t in tails.values()) else
                       'has instructions no way from its start reaches (%d, from %08X)' % (len(never), never[0]))
        result[name] = {'pieces': pieces, 'tails': tails, 'why': why}
    return result


def merged(functions, entry, pieces, tails=None):
    """The entry with its pieces, as one function: their instructions, their calls (not the jumps between them), their jump tables.
    tails (build_group): the jumps that are tail calls. They stay among its calls, and are noted in 'tail' too ({address the
    translator noted the jump at: target}) for to_asm.py, which writes them as calls, and lift.py, which lets them through."""
    f = {'name': entry, 'ins': {}, 'calls': {}, 'tables': {}, 'pieces': list(pieces), 'tail': dict(tails or {})}
    for p in pieces:
        g = functions[p]
        for a, i in g['ins'].items():
            f['ins'].setdefault(a, i)
        inside = {a for a, t in jumps(g).items() if t in pieces}
        for a, c in g['calls'].items():
            if a not in inside:
                f['calls'].setdefault(a, c)
        for a, t in g.get('tables', {}).items():
            f['tables'].setdefault(a, t)
    return f


def apply(functions):
    """functions, with each group's entry replaced by the whole of it (pieces and tail calls). The pieces stay too, as they were."""
    out = dict(functions)
    for entry, v in plan(functions).items():
        if v['why'] is None:
            out[entry] = merged(functions, entry, v['pieces'], v['tails'])
    return out


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    import to_asm
    fs = to_asm.read_functions(merge=False)
    p = plan(fs)
    tally = collections.Counter()
    for e, v in sorted(p.items()):
        key = v['why'] or 'PUT TOGETHER'
        key = re.sub(r':.*|at [0-9A-F]+$| \(.*', '', key)
        tally[key] += 1
        print('%-22s %-55s %s%s' % (e, key, ' '.join('%s(%d)' % (x, len(fs[x]['ins']) if x in fs else 0) for x in v['pieces']),
                                    ''.join(' tail:%s' % t for t in sorted(set(v['tails'].values())))))
    print(dict(tally))
