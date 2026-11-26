# What the N64's instructions read and write, and how control flows through a function: shared by analyse.py and lift.py.
import re

ABI_ARGS = {'a0', 'a1', 'a2', 'a3', 'f12', 'f14'}
CLOBBER = {'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7', 't8', 't9', 'ra', 'hi', 'lo'} | {'f%d' % i for i in range(0, 20)}
SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp', 'ra', 'f20', 'f22', 'f24', 'f26', 'f28', 'f30'}
REG = re.compile(r'\$([a-z0-9]+)')
BRANCH = {'beq', 'bne', 'bgez', 'bgtz', 'blez', 'bltz', 'beql', 'bnel', 'bgezl', 'bgtzl', 'blezl', 'bltzl', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl', 'bgezal', 'bltzal'}
STORES = {'sb', 'sh', 'sw', 'sd', 'swl', 'swr', 'swc1', 'sdc1'}


SAVE_STORES = ('sw', 'sd', 'sdc1', 'swc1')


def saves_for_caller(mn, r):
    """Whether an instruction (its registers r, as REG finds them) has the form of saving a callee-saved register on the stack:
    sw / sd / swc1 / sdc1 of one of SAVED with $sp as the base. Whether it really saves it is pass_on_stores' question."""
    return mn in SAVE_STORES and bool(r) and r[-1] == 'sp' and r[0] in SAVED


def uses_defs(mn, ops, passed_on=False):
    """The registers an instruction reads, and those it writes. $zero is neither: it's always 0.

    passed_on: the instruction is a store of a saved register to the stack that passes the value on (a stack parameter, a spilled
    value) rather than keeping it for the caller: it then reads the register, like any other store. Only a reader that knows the
    function can tell (pass_on_stores); uses_defs_in asks it. Without it such a store reads nothing, as before 2026-11-16.

    (Until 2026-11-05 $zero was dropped before working out which operand is which, so `mtc1 $zero, $f20` (f20 = 0) looked like a read of
    f20. A double-precision instruction also uses the odd register of each pair, which is counted now too.)"""
    uses, defs = _uses_defs(mn, REG.findall(ops), passed_on)
    pair = lambda regs: regs | {'f%d' % (int(x[1:]) + 1) for x in regs if re.fullmatch(r'f\d+', x) and int(x[1:]) % 2 == 0}
    parts = mn.split('.')
    if len(parts) == 3 and parts[0] in ('cvt', 'trunc', 'round', 'floor', 'ceil'):
        # Converting: the first format is the result's, the second the source's (cvt.d.s: a double from a single).
        if parts[1] in ('d', 'l'):
            defs = pair(defs)
        if parts[2] in ('d', 'l'):
            uses = pair(uses)
    elif mn.endswith('.d') or mn in ('ldc1', 'sdc1'):
        uses, defs = pair(uses), pair(defs)
    return uses - {'zero'}, defs - {'zero'}


def _uses_defs(mn, r, passed_on=False):
    if mn in ('jal', 'j', 'b', 'nop', 'sync', 'cache', 'break', 'syscall', 'bal'):
        return set(), set()
    if mn == 'jr':
        return set(r[:1]), set()
    if mn == 'jalr':
        return set(r[-1:]), {'ra'}
    if saves_for_caller(mn, r) and not passed_on:
        return set(), set()      # (saving a register for the caller isn't reading it; passing it on is: pass_on_stores)
    if mn in BRANCH or mn in STORES:
        return set(r), set()
    if mn == 'mtlo':
        return set(r), {'lo'}
    if mn == 'mthi':
        return set(r), {'hi'}
    if mn in ('mult', 'multu', 'div', 'divu', 'dmult', 'dmultu', 'ddiv', 'ddivu'):
        return set(r), {'hi', 'lo'}
    if mn == 'mflo':
        return {'lo'}, set(r)
    if mn == 'mfhi':
        return {'hi'}, set(r)
    if mn.startswith('c.'):
        return set(r), set()
    if mn in ('mtc1', 'dmtc1', 'ctc1'):
        return set(r[:1]), set(r[1:])
    if mn in ('lwl', 'lwr'):
        return set(r), set(r[:1])
    if not r:
        return set(), set()
    return set(r[1:]), set(r[:1])


def cfg(f):
    addrs = sorted(f['ins'])
    idx = set(addrs)
    succ = {}
    after_ds = {}
    for i, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        n = addrs[i + 1] if i + 1 < len(addrs) else None
        m = re.search(r'L_([0-9A-F]{8})', ops)
        tgt = int(m.group(1), 16) if m else None
        if mn in BRANCH:
            # (the instruction after a branch runs before it lands: on both paths, or for the "likely" ones only if taken)
            after = addrs[i + 2] if i + 2 < len(addrs) else None
            if mn.endswith('l') and mn not in ('bgezal', 'bltzal'):
                succ[a] = [x for x in (n, after) if x]
                after_ds[n] = [tgt] if tgt in idx else []
            else:
                succ[a] = [n] if n else []
                after_ds[n] = [x for x in (after, tgt if tgt in idx else None) if x]
        elif mn in ('j', 'b'):
            succ[a] = [n] if n else []
            after_ds[n] = [tgt] if tgt in idx else []
        elif mn == 'jr':
            succ[a] = [n] if n else []
            # A return goes nowhere in this function; a jump through a table goes to any of its entries (to_asm.py reads the tables).
            after_ds[n] = [] if '$ra' in ops else sorted({t for t in f.get('tables', {}).get(a, {}).values() if t in idx})
        else:
            succ[a] = [n] if n else []
    for ds, s in after_ds.items():
        if ds in succ:
            succ[ds] = s
    return addrs, succ


# An unaligned word load is two instructions, lwl and lwr, each merging part of a word into the register (uses_defs counts each as
# reading it). Together, offsets 3 apart from the same base, they write all of it: so the first of an adjacent pair reads nothing
# that survives. Kept out of uses_defs, which sees one instruction: only where the next one is known (uses_defs_at). (2026-11-07:
# analyse.py counted every such pair as reading its register, so func_800D9AB0_ovl_a "read" v0 and v1, and every function calling
# it seemed to pass them in.)
UNALIGNED = re.compile(r'\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)$')
JUMPS = BRANCH | {'j', 'b', 'jr', 'jal', 'jalr'}


def unaligned_pair(first, second):
    """Whether two instructions are the two halves of one unaligned word load (lwl and lwr, either order): the same register and
    base, offsets 3 apart, and the base isn't the register being loaded."""
    if not second or {first[0], second[0]} != {'lwl', 'lwr'}:
        return False
    a, b = UNALIGNED.match(first[1]), UNALIGNED.match(second[1])
    return bool(a and b) and a.group(1) == b.group(1) and a.group(3) == b.group(3) and a.group(1) != a.group(3) and \
        abs(int(a.group(2), 0) - int(b.group(2), 0)) == 3


def uses_defs_at(f, a, succ):
    """uses_defs for the instruction at a in f, knowing what comes next (succ from cfg): the first half of an unaligned word load
    whose other half is the next instruction, and nothing can come between -- not a delay slot, and its only successor is the
    partner -- doesn't read the register it loads. And, as uses_defs_in, a saved register's store that passes it on reads it."""
    mn, ops = f['ins'][a]
    u, d = uses_defs_in(f, a)
    if (mn in ('lwl', 'lwr') and succ.get(a) == [a + 4] and f['ins'].get(a - 4, ('',))[0] not in JUMPS
            and unaligned_pair(f['ins'][a], f['ins'].get(a + 4))):
        u = u - d
    return u, d


# ---- Saving a register for the caller, or passing it on (2026-11-16, liftfix) ----
#
# A function that changes a callee-saved register (s0-s7, fp, gp, ra, f20-f30) first stores it in its frame and loads it back
# before it returns: `sw $s5, 0x24($sp)` at the start, `lw $s5, 0x24($sp)` before the `jr $ra`. That store is no use of the
# value -- it only keeps the caller's value safe -- so uses_defs has always said it reads nothing. But a saved register can also
# be stored to the stack as a VALUE: a stack parameter for a function it calls (the 5th and later go at 0x10($sp) and up), or a
# value kept in the frame. Then the store reads it, like any other store.
#
# Until 2026-11-16 every such store counted as a save. The bug (found by playtest2, confirmed by reviewpt2): func_80117714
# (draw_text_request) takes its flags in a3, moves them to s7, then s5, and hands them to func_800DD094 as `sw $s5, 0x18($sp)`,
# its 7th parameter. Taken as a save, a3 looked unread, analysis.json said func_80117714 reads a0-a2, and the converted
# func_80117A84 (draw_menu_text_queue, 0.81) called it with three parameters: the menus' text flags were whatever a3 last held
# (the Rumble Pak notice and the select screen's "Random" drawn in the wrong size and place). 32 functions' read sets were short
# like that, and 21 converted callers left out a register the original sets before the call. The tester could not see it: its
# stand-ins for the callees are made from the same analysis.
#
# The rule now, worked out once per function (pass_on_stores) and used by every reader through uses_defs_in / uses_defs_at:
#   - a store of a saved register to a stack slot is a SAVE only if the function loads the same register back from the same
#     slot on its way out: a lw / ld / lwc1 / ldc1 of that register at that offset from $sp whose value reaches a return (or a
#     jump out of the function, a tail call) without the register being written again -- its epilogue. Every other one passes
#     the value on, and reads the register.
#     ("On its way out", not anywhere: a register the compiler ran short of is stored to a slot and loaded back from it in the
#     middle of the function -- func_80117438 keeps s2 at 0xDC and s3 at 0x5C so, then puts both back from their own save slots
#     in the epilogue. Those stores are values, and read the register. A rule that took any load back from the slot as a
#     restore -- the first fix -- called them saves.)
#   - a function with no `jr $ra` keeps the old rule (every such store a save). It never returns -- a loop that runs for good,
#     or a piece of a function the translator split, whose epilogue is in another piece -- so there's no epilogue to match, and
#     taking its saves for reads would make it look as if it read every register it keeps.
# Why this is right, what it can still get wrong, and what it changed: scratch\agents\liftfix\REVIEW_liftfix.txt.
SP_SLOT = re.compile(r'^\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$')
RELOADS = ('lw', 'ld', 'lwc1', 'ldc1')
_PASS_ON = {}       # (id(f['ins']), has tables) -> (f['ins'], its answer): pass_on_stores, worked out once per function


def sp_slot(ops):
    """(register, offset) of an instruction `$reg, offset($sp)`, or None."""
    m = SP_SLOT.match(ops.strip())
    return (m.group(1), int(m.group(2), 0)) if m else None


def restored_slots(f):
    """{(register, offset)}: the stack slots f loads a saved register back from on its way out -- a load of the register from
    that slot whose value reaches a return (the delay slot of a `jr $ra`) or a jump out of the function (a tail call) with no
    instruction writing the register in between."""
    ins = f['ins']
    addrs, succ = cfg(f)

    def way_out(pc):
        """pc is the last instruction run before the function is left: the delay slot of a return or of a jump elsewhere."""
        before = ins.get(pc - 4)
        return not succ.get(pc) and before is not None and (before[0] == 'jr' and '$ra' in before[1] or before[0] in ('j', 'b'))

    slots = set()
    for a, (mn, ops) in ins.items():
        slot = sp_slot(ops) if mn in RELOADS else None
        if slot is None or slot[0] not in SAVED or slot in slots:
            continue
        reg = slot[0]
        # (Follow the loaded value on: a write of the register ends that way, reaching the way out is a restore.)
        seen, work, out = set(), [a], False
        while work and not out:
            pc = work.pop()
            if pc in seen:
                continue
            seen.add(pc)
            if pc != a and reg in uses_defs(*ins[pc])[1]:
                continue
            out = way_out(pc)
            work += succ.get(pc, [])
        if out:
            slots.add(slot)
    return slots


def pass_on_stores(f):
    """The addresses of f's stores of a saved register to the stack that pass the value on -- and so read the register -- rather
    than save it for the caller: in a function that returns (has a `jr $ra`), each one whose register isn't loaded back from the
    same slot on the way out (restored_slots). (Kept per function: nothing changes a function's instructions once they're read.)"""
    ins = f['ins']
    key = (id(ins), bool(f.get('tables')))      # (a function's jump tables, once read, can add ways through it: cfg)
    known = _PASS_ON.get(key)
    if known is not None and known[0] is ins:
        return known[1]
    found = set()
    if any(mn == 'jr' and '$ra' in ops for mn, ops in ins.values()):
        reloaded = restored_slots(f)
        for a, (mn, ops) in ins.items():
            if saves_for_caller(mn, REG.findall(ops)):
                slot = sp_slot(ops)
                # (An offset this can't read -- none today -- stays a save, as before.)
                if slot is not None and slot not in reloaded:
                    found.add(a)
    found = frozenset(found)
    _PASS_ON[key] = (ins, found)
    return found


def uses_defs_in(f, a):
    """uses_defs for the instruction at a in f, knowing the function: a store of a saved register that passes it on reads it
    (pass_on_stores). Every reader that has the function and the address uses this (or uses_defs_at), so they all agree."""
    mn, ops = f['ins'][a]
    return uses_defs(mn, ops, passed_on=a in pass_on_stores(f))


# ---- What a function hands back (2026-11-16, liftfix, second part) ----
#
# A function's result normally comes back in v0 / v1 (f0 / f1 for a float): analyse.py has always counted those as read after
# a return, whatever the function. But some functions hand a value back in another register, and their callers read it there
# after the call. The port's six-man engine has three: rw_cam_sel_a3 (0x80784554) ends `jr $ra; andi $a3, $a0, 0xFFFF` -- it
# hands its caller's a0 mask back in a3 -- and rw_cam_sel_a0 / rw_t1_a1_n20 hand theirs back in a0 / t1. With only v0-f1 live
# after the return, that delay slot looked dead, so the a0 it reads was lost: analysis.json said rw_cam_sel_a3 reads nothing,
# and the converted func_800E28C0 (match_camera_keep_all_in_view, 0x800E2984) called it with no a0 -- the six-man camera's
# in-play mask was whatever a0 held (argsweep's call sweep found it).
#
# The rule now: a function also hands back every register that one of its callers' OWN instructions read after the call
# before writing it (past later calls only for the registers those calls can't change; to the caller's return, where what the
# caller hands back counts), where the function may have changed that register (an instruction of its own writes it, or a function it calls may,
# and it isn't a saved register it puts back on the way out: may_change). A register a caller doesn't touch but passes to a
# later call isn't counted: by the calling rules it is that call's parameter, not this one's result. (Counting the later
# callees' read sets too was tried first: every register a caller left unset before a later call became a "result", and the
# read sets ran away -- 83% of functions reading only their parameters fell to 54%.)
# analyse.py works these out with the read sets, round by round (callers_read_after), and writes them into analysis.json as each
# function's 'results'; at the function's return they count as read, as v0-f1 do. Every reader takes them from there through
# returned_in: analyse.py (what is read at a return, what a call leaves), lift.py (what a caller relies on after a call, and
# what goes back through a return).
RETURN_VALUE = frozenset({'v0', 'v1', 'f0', 'f1'})


def returned_in(analysis, name):
    """The registers the function `name` hands back to its caller: v0, v1, f0, f1, and its 'results' in analysis.json (the
    other registers its callers read after calling it, that it may change). A name not in the analysis: v0-f1 only."""
    entry = analysis.get(name) if isinstance(name, str) else None
    return set(RETURN_VALUE) | set((entry or {}).get('results', ()))


def may_change(functions, system=RETURN_VALUE):
    """{name: the registers the function may leave changed when it returns}: what its own instructions write, and what the
    functions it calls may change, less the saved registers it loads back on the way out (restored_slots) and sp / ra / zero.
    Worked out over the calls until nothing changes. A call to something not in `functions` (a system function, one of the
    port's native exports), or through a pointer: `system`, by default only v0-f1 -- what is known of it. (With every register
    the calling rules let such a call change, a caller's read on a way it can't really take -- the mod's modfn_810B76A0 reads
    a0-a2 after calling modfn_810C27E0 only on such a way -- made a0-a2 "results" of the mod's one-line wrappers around the
    port's exports, and from them the read sets of ~45 of the mod's functions and ~80 of the game's grew. So a register handed
    back only through such a call is not found: the analysis says nothing of it, as before.)"""
    own, calls, kept = {}, {}, {}
    for n, f in functions.items():
        restored = restored_slots(f) if f['ins'] else set()
        kept[n] = {r for r, _ in restored}
        regs, callees = set(), set()
        for a, (mn, ops) in f['ins'].items():
            slot = sp_slot(ops) if mn in RELOADS else None
            if slot is not None and slot in restored:
                continue            # (putting a saved register back)
            regs |= uses_defs(mn, ops)[1]
            if mn == 'jal':
                c = f['calls'].get(a + 4) or f['calls'].get(a)
                callees.add(c)
            elif mn == 'jalr':
                callees.add(None)
        own[n], calls[n] = regs, callees
    # (From each function's own changes up: a least fixed point. Started from its own writes before the puts-back were taken off,
    #  a cycle of calls -- func_801409D4, func_80140FB4, func_80141034 call each other -- kept a register one of them had put back.)
    found = {n: own[n] - kept[n] - {'sp', 'ra', 'zero'} for n in functions}
    again = True
    while again:
        again = False
        for n in functions:
            now = set(own[n])
            for c in calls[n]:
                now |= found[c] if c in found else set(system)
            now -= kept[n] | {'sp', 'ra', 'zero'}
            if now != found[n]:
                found[n] = now
                again = True
    return found


def callers_read_after(f, a, succ, back, changes=None):
    """The registers the caller f's own instructions read after its call at a (a jal or jalr) before writing them: on every way
    on, to a return or a jump out of f, where `back` (what f hands back) counts as read. (What the call's own delay slot writes
    is not written AFTER the call: the callee runs after it, and may change it.)

    A later call ends the question only for the registers it may change -- `changes(pc)`, the callee's may_change set at the
    call at pc (None: not known, the calling rules' scratch registers, CLOBBER). A register it can't change -- a saved register,
    or one its callee never touches -- still holds what the first call left, so a read of it further on is that call's result.
    (2026-11-16, the reviewer of part 2: rw_cam_view_s5 hands s5 back, `jr $ra; lw $s5, 0x4D00($at)`, and func_800E38A4 reads
    s5 at 0x800E3D24 only after a later call to func_800ED8F8, which can't change s5. Stopping at every call missed it. The
    later call's own reads of such a register still don't count: by the calling rules they are that call's parameters.)

    Only forward from the call, not back round a loop: what a caller takes from a call it takes on its way on, the first time
    through. A register read only after going back round a loop is one set before the call -- which the call, by the calling
    rules, may have changed -- on a way the caller can't really take. The mod's modfn_810B76A0 reads a0-a2 so after calling
    modfn_810C27E0 (back round two loops, on a way that skips where it sets them); counted, a0-a2 became results of the mod's
    functions and the read sets of ~45 of them and ~80 of the game's grew.)"""
    found = set()
    seen = {}
    work = [(s, frozenset()) for s in succ.get(a + 4, [])]
    while work:
        pc, written = work.pop()
        if pc in seen and seen[pc] <= written:
            continue
        seen[pc] = written if pc not in seen else seen[pc] & written
        mn, ops = f['ins'][pc]
        u, d = uses_defs_at(f, pc, succ)
        if mn in ('lwl', 'lwr'):
            # (An unaligned load merges into the register it loads, so uses_defs counts it as reading it; uses_defs_at clears
            #  that only for an adjacent pair. Compiled code always loads all of the word with the two halves -- the mod's
            #  modfn_810C831C has them 7 apart, `lwl $t1, 0x3FB8($s4)` ... `lwr $t1, 0x3($at)` -- so neither half is a caller
            #  reading what a call left: taken as one, t1 became a "result" of a dozen of the mod's functions.)
            u = u - d
        found |= u - written
        written = written | d
        if mn in ('jal', 'jalr', 'jr', 'j', 'b') or (mn in BRANCH and not succ.get(pc + 4)):
            # (A call, return or jump: its delay slot runs first. After a call, the next callee may change anything.)
            if pc + 4 in f['ins']:
                du, dd = uses_defs_in(f, pc + 4)
                found |= du - written
                written = written | dd
            following = succ.get(pc + 4, [])
            if mn in ('jal', 'jalr'):
                # (A later call: from here on, only what it can't change is still the first call's.)
                known = changes(pc) if changes else None
                written = written | (set(known) if known is not None else set(CLOBBER))
                for s in following:
                    if s > pc + 4:
                        work.append((s, frozenset(written)))
                continue
            if not following:
                found |= set(back) - written        # (a return, or a jump out that returns for f: f's caller reads these)
                continue
            for s in following:
                if s > pc + 4:          # (forward only: see above)
                    work.append((s, frozenset(written)))
            continue
        for s in succ.get(pc, []):
            if s > pc:
                work.append((s, frozenset(written)))
    return found


SP_STEP = re.compile(r'\$sp,\s*\$sp,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$')


def pops_a_frame_it_never_made(f):
    """Whether some return leaves the stack pointer higher than it found it: the tail of a function, which the translator started
    part-way through (func_801104E8_ovl_c: `lw $ra, 0x18($sp) ... jr $ra; addiu $sp, $sp, 0x20`). A `jal` to it would return with
    the caller's stack moved, so no working call goes there. (The stack walk of lift.py not_a_whole_function, for this one question:
    a function that is merely unusual -- changing a register it doesn't save -- can still be called, and still counts.)"""
    if not f['ins']:
        return False
    addrs, succ = cfg(f)
    seen = {}
    work = [(addrs[0], 0)]
    while work:
        pc, offset = work.pop()
        mn, ops = f['ins'][pc]
        m = SP_STEP.match(ops)
        if mn == 'addiu' and m:
            offset += int(m.group(1), 0)
        if pc in seen:
            continue
        seen[pc] = offset
        following = succ.get(pc, [])
        before = f['ins'].get(pc - 4, ('', ''))
        if not following and before[0] == 'jr' and '$ra' in before[1] and offset > 0:
            return True
        for n in following:
            work.append((n, offset))
    return False


def functions_at(functions, whole=True):
    """Each address, and the functions there a call can reach: the main one and its overlay copies (func_8010B468,
    func_8010B468_ovl_d). With `whole`, not a tail the translator started part-way through a function (pops_a_frame_it_never_made)."""
    found = {}
    for n in functions:
        m = re.fullmatch(r'func_([0-9A-F]{8})(?:_\w+)?', n)
        if m and functions[n]['ins'] and not (whole and pops_a_frame_it_never_made(functions[n])):
            found.setdefault(int(m.group(1), 16), []).append(n)
    return found
