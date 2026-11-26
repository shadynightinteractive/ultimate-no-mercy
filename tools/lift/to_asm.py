#!/usr/bin/env python3
"""Step 4: writes each translated function back out as MIPS assembly, for m2c to turn into C.

Reads exe/WWFNoMercyRecomp/RecompiledFuncs, where every instruction's MIPS is in a comment, in the order N64Recomp wrote
it. A branch's delay slot is written out twice there, so instructions are de-duplicated by address and sorted.

    python tools/lift/to_asm.py <out folder>

Writes <out folder>/<function>.s, and <out folder>/index.json with each function's size, what it calls, and whether it
has a jump table (and whether its table could be found) or a call through a pointer.

A jump table (a switch statement, compiled) is an indirect jump through a list of addresses in the ROM's data. The translator
already worked out each list: its `switch (jr_addend_XXXXXXXX >> 2)` has a case for each entry. Those are written out as the
data m2c expects, `jtbl_<address>` in .rdata, and the instructions that load from the list refer to it by name, so m2c can
turn it back into a switch.
"""
import glob, json, os, re, sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'exe', 'WWFNoMercyRecomp', 'RecompiledFuncs')
FUNC = re.compile(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\)')
INS = re.compile(r'^\s*// 0x([0-9A-F]{8}): (\S+)\s*(.*)$')
CALL = re.compile(r'\b(\w+)\(rdram, ctx\);')
LABEL = re.compile(r'L_([0-9A-F]{8})')
SWITCH = re.compile(r'switch \(jr_addend_([0-9A-F]{8}) >> 2\) \{')
CASE = re.compile(r'case (\d+): goto L_([0-9A-F]{8});')
# (2026-11-11, trial 5: the translator's own word for where a jump's table is, in its switch's default line:
#  `default: switch_error(__func__, 0x80130B10, 0x801511F8);` -- the jr's address, the table's. See find_kept_tables.)
TABLE_AT_LINE = re.compile(r'switch_error\(__func__, 0x([0-9A-F]{8}), 0x([0-9A-F]{8})\)')
TABLE_AT = {}       # {a jr's address: the table addresses the translator gives for a jr there (overlays share addresses)}


def read_functions(merge=True, extra=(), game=True):
    """Every translated function: its instructions, calls and jump tables. With merge (the default), each function the translator
    split in pieces is put back together under its entry's name (merge.py); the pieces are still there too, as they were.
    game=False: only the files in extra (lift.py's mod_own_functions, the mod's other functions on their own, 2026-11-12)."""
    functions = {}
    # (And the mod's replacement functions, translated as the mod loader translates them, as modrep_<the game function's
    #  name>: modrep.py. Read after the game's, under names of their own, so the game's functions are exactly as before. And
    #  extra: more files of translated functions -- analyse.py's: the mod's other functions, for what the copies read.)
    import modrep
    for path in (sorted(glob.glob(os.path.join(SRC, '*.c'))) + modrep.sources() if game else []) + list(extra):
        current = None
        last = None
        # (A translation of the mod's: its calls to the runtime's libultra functions under the names the game's own translation
        #  calls them by, modrep.game_names, 2026-11-12.)
        own = modrep.is_source(path)
        for line in open(path, encoding='utf8', errors='replace'):
            if own:
                line = modrep.game_names(line)
            m = FUNC.match(line)
            if m:
                current = {'name': m.group(1), 'ins': {}, 'calls': {}, 'tables': {}}
                functions[current['name']] = current
                last = None
                table = None
                continue
            if current is None:
                continue
            m = SWITCH.search(line)
            if m:
                table = current['tables'].setdefault(int(m.group(1), 16), {})
                continue
            m = TABLE_AT_LINE.search(line)
            if m:
                TABLE_AT.setdefault(int(m.group(1), 16), set()).add(int(m.group(2), 16))
            if table is not None:
                m = CASE.search(line)
                if m:
                    table[int(m.group(1))] = int(m.group(2), 16)
                    continue
                if 'default:' in line or line.strip() == '}':
                    table = None
            m = INS.match(line)
            if m:
                last = int(m.group(1), 16)
                current['ins'].setdefault(last, (m.group(2), m.group(3).strip()))
                continue
            if last is not None:
                for c in CALL.findall(line):
                    current['calls'].setdefault(last, c)
    if merge:
        import merge as pieces
        functions = pieces.apply(functions)
    return functions


def find_tables(f):
    """For each jump through a table: the table's address, and the two instructions to give its name. Returns {jr address:
    (table address, lui address, lw address)} for the ones found."""
    found = {}
    addrs = sorted(f['ins'])
    for i, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        if mn != 'jr' or '$ra' in ops or a not in f['tables']:
            continue
        reg = re.match(r'\$(\w+)', ops).group(1)
        load = None
        for b in reversed(addrs[max(0, i - 8):i]):
            m = re.match(r'\$%s,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)$' % reg, f['ins'][b][1])
            if f['ins'][b][0] == 'lw' and m:
                load = (b, int(m.group(1), 0), m.group(2))
                break
        if not load:
            continue
        for b in reversed(addrs[max(0, i - 12):addrs.index(load[0])]):
            m = re.match(r'\$%s,\s*((?:0[xX])?[0-9A-Fa-f]+)$' % load[2], f['ins'][b][1])
            if f['ins'][b][0] == 'lui' and m:
                found[a] = (((int(m.group(1), 0) << 16) + load[1]) & 0xFFFFFFFF, b, load[0])
                break
    return found


KEPT_BASES = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp'}


def reaching_writes(f, register):
    """{address: the addresses of the instructions that can have been the last to write the register when this one runs
    (None among them: nothing in the function has yet)}, or None without a way through the function to follow. A call
    is taken to leave the register alone: find_kept_tables asks only about registers calls keep, or functions without
    calls. (2026-11-11, trial 5.)"""
    from mips import cfg, uses_defs
    addrs, succ = cfg(f)
    if not addrs:
        return None
    # (Every instruction nothing in the function leads to is a way in: the entry, which isn't always the lowest address in a
    #  function put together from pieces, and anything only another function jumps to.)
    led_to = {n for ns in succ.values() for n in ns}
    work = [a for a in addrs if a not in led_to] or [addrs[0]]
    reach = {a: {None} for a in work}
    while work:
        pc = work.pop()
        after = {pc} if register in uses_defs(*f['ins'][pc])[1] else reach[pc]
        for n in succ.get(pc, []):
            if n not in f['ins']:
                continue
            if n not in reach or not after <= reach[n]:
                reach[n] = set(reach.get(n, ())) | after
                work.append(n)
    return reach


def find_kept_tables(f):
    """(2026-11-11, trial 5) The jumps through a table whose address the compiler keeps in a register -- a switch inside a
    loop: the address is made once, before the loop,

        lui $s2, 0x8015; addiu $s2, $s2, 0x11F8          (once, anywhere before)
        sll $v0, $v1, 2; addu $v0, $v0, $s2; lw $v0, 0x0($v0); jr $v0

    which find_tables, looking for the lui just before the lw, doesn't find (func_8013092C, func_80132940, func_8013843C,
    func_8013AF04: "has a jump table that couldn't be found"). Returns {jr address: (table address, lui address, addiu
    address, addu address, lw address)} for the ones that are surely that:
      - the three instructions before the jr's delay slot are exactly `addu $x, $x, $base` (either order), `lw $r, 0x0($x)`,
        `jr $r`;
      - whichever way the function got to the addu, the last instruction to write $base was the same `addiu $base, $base,
        lo`, and the last before that the `lui $base, hi` straight before it (reaching_writes), so that is what it holds;
      - $base is a register calls keep (s0-s7, fp), or the function makes no calls;
      - hi and lo give the address the translator itself has for this jump's table (TABLE_AT).
    to_asm then names the table in the lui and the addiu, and m2c sees the load as one from the table, as it does when the
    lui is beside it."""
    from mips import uses_defs
    found = {}
    addrs = sorted(f['ins'])
    calls = any(mn in ('jal', 'jalr') for mn, _ in f['ins'].values())
    for a in addrs:
        mn, ops = f['ins'][a]
        if mn != 'jr' or '$ra' in ops or a not in f['tables'] or a - 8 not in f['ins']:
            continue
        r = re.match(r'\$(\w+)$', ops.strip())
        load = re.match(r'\$(\w+),\s*0x0\(\$(\w+)\)$', f['ins'][a - 4][1]) if f['ins'][a - 4][0] == 'lw' else None
        add = re.match(r'\$(\w+),\s*\$(\w+),\s*\$(\w+)$', f['ins'][a - 8][1]) if f['ins'][a - 8][0] == 'addu' else None
        if not (r and load and add) or load.group(1) != r.group(1) or load.group(2) != add.group(1):
            continue
        x = add.group(1)
        others = [s for s in (add.group(2), add.group(3)) if s != x]
        if len(others) != 1 or x not in (add.group(2), add.group(3)):
            continue
        base = others[0]
        if base in ('zero', 'sp', 'ra', 'gp', 'at') or (calls and base not in KEPT_BASES):
            continue
        # (Which writes of $base can be the one the addu reads: followed along every way through the function.)
        reach = reaching_writes(f, base)
        if reach is None or len(reach.get(a - 8, ())) != 1:
            continue
        addiu = next(iter(reach[a - 8]))
        lui = addiu - 4 if addiu is not None else None
        if lui not in f['ins'] or reach.get(addiu) != {lui}:
            continue
        hi = re.match(r'\$%s,\s*((?:0[xX])?[0-9A-Fa-f]+)$' % base, f['ins'][lui][1]) if f['ins'][lui][0] == 'lui' else None
        lo = re.match(r'\$%s,\s*\$%s,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$' % (base, base), f['ins'][addiu][1]) if f['ins'][addiu][0] == 'addiu' else None
        if not (hi and lo):
            continue
        table = ((int(hi.group(1), 0) << 16) + int(lo.group(1), 0)) & 0xFFFFFFFF
        if table not in TABLE_AT.get(a, ()):
            continue
        found[a] = (table, lui, addiu, a - 8, a - 4)
    return found


# (find_slot_tables: an instruction line m2c is given that is part of the instruction on the line before -- a comment m2c
#  leaves out, which abi.py rewrite_asm reads.)
MORE = '  # (lift: and the line above)'


def find_slot_tables(f):
    """(2026-11-12, agent "refused") The jumps through a table whose slot address the compiler kept in the frame -- a switch
    in a loop, the table's slot worked out once before it and the index kept too (func_8012069C):

        lui $v1, 0x8015; addiu $v1, $v1, 0xC90; sll $v0, $t1, 2; addu $v0, $v0, $v1;   (once, before the loop)
        sw $t1, 0xFC($sp); sw $v0, 0x104($sp)
        ...
        lw $t1, 0xFC($sp); sltiu $v0, $t1, 0x1F; beq $v0, $zero, .Ldefault; nop
        lw $t0, 0x104($sp); lw $v0, 0x0($t0); jr $v0

    m2c sees the slot as a local and can't tell it holds &table[index], so it can't write the switch ("has a jump table that
    couldn't be found"). The translator itself switches on the index as it was at the addu (jr_addend), not on the slot. So for
    m2c the two loads become the table lookup from the index the function reloaded just before, `sll $t0, $t1, 2; lui $at,
    %hi(table); addu $at, $at, $t0; lw $v0, %lo(table)($at)`, the form m2c knows -- only where all of this holds:
      - the jr's register is loaded from 0($t), and $t from a frame slot that one instruction only stores;
      - what that store stores is, by the only way to it, the `addu` of `sll $i, $i', 2` and the table's lui + addiu, with
        nothing between them that jumps or calls, and the address is the one the translator gives for this jump (TABLE_AT);
      - $i' is stored, as the sll read it, to a second frame slot that one instruction only stores, and the register the jump
        uses as the index is loaded from that slot, with nothing between that load and the jump that writes it;
      - $t and $at are written before anything reads them, on every way on from the jump (a call or a return counts as a
        read).
    The function then has more instructions in m2c's text than its own: the ones past each replaced instruction's first are
    marked MORE, which abi.py's rewrite_asm counts as part of that instruction (its line), and table_jumps gives the four no
    store, as it does the lookup's instructions in the form it knows. Returns {jr address: (table address, {address:
    replacement lines})}."""
    from mips import cfg, uses_defs, BRANCH
    found = {}
    addrs, succ = cfg(f)
    ins = f['ins']
    jumps = BRANCH | {'j', 'b', 'jr', 'jal', 'jalr'}
    def straight(a, b):
        return all(x in ins and ins[x][0] not in jumps for x in range(a + 4, b, 4))
    sp_slot = re.compile(r'\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$')
    def stores_to(offset):
        out = []
        for a, (mn, ops) in ins.items():
            m = sp_slot.match(ops)
            if mn in ('sw', 'sh', 'sb', 'swc1', 'sdc1', 'swl', 'swr') and m:
                lo = int(m.group(2), 0)
                if lo <= offset + 3 and offset <= lo + {'sh': 1, 'sb': 0, 'sdc1': 7}.get(mn, 3):
                    out.append(a)
        return out
    def dead_after(jr, regs):
        """Whether every register in regs (scratch registers) is written before this function's own instructions read it, on
        every way on from the jump's targets. A call ends the question -- the callee may change them -- once its delay slot
        has run, as a return does: whether a callee reads them before, or a caller after, lift.py already asks of every
        function it converts ("calls something that reads scratch registers", "a caller reads what it leaves in")."""
        for start in set(f['tables'].get(jr, {}).values()):
            work, seen = [(start, frozenset(regs), False)], set()
            while work:
                pc, live, last = work.pop()
                if not live or (pc, live, last) in seen or pc not in ins:
                    continue
                seen.add((pc, live, last))
                mn, ops = ins[pc]
                u, d = uses_defs(mn, ops)
                if u & live:
                    return False
                if last:
                    continue            # (the delay slot of a call or a return has run)
                live = live - d
                ends = mn in ('jal', 'jalr', 'jr')
                for n in succ.get(pc, []):
                    work.append((n, live, ends))
        return True
    for jr in sorted(ins):
        mn, ops = ins[jr]
        if mn != 'jr' or '$ra' in ops or jr not in f['tables'] or jr - 8 not in ins:
            continue
        r = re.match(r'\$(\w+)$', ops.strip())
        load = re.match(r'\$(\w+),\s*0x0\(\$(\w+)\)$', ins[jr - 4][1]) if ins[jr - 4][0] == 'lw' else None
        slot = sp_slot.match(ins[jr - 8][1]) if ins[jr - 8][0] == 'lw' else None
        if not (r and load and slot) or load.group(1) != r.group(1) or slot.group(1) != load.group(2):
            continue
        t = load.group(2)
        stores = stores_to(int(slot.group(2), 0))
        if len(stores) != 1 or ins[stores[0]][0] != 'sw':
            continue
        s = stores[0]
        x = sp_slot.match(ins[s][1]).group(1)
        reach = reaching_writes(f, x)
        if reach is None or len(reach.get(s, ())) != 1:
            continue
        add = next(iter(reach[s]))
        m = re.match(r'\$(\w+),\s*\$(\w+),\s*\$(\w+)$', ins.get(add, ('', ''))[1]) if ins.get(add, ('',))[0] == 'addu' else None
        if not m or not straight(add, s):
            continue
        found_here = None
        for scaled, base in ((m.group(2), m.group(3)), (m.group(3), m.group(2))):
            rs, rb = reaching_writes(f, scaled), reaching_writes(f, base)
            if not rs or not rb or len(rs.get(add, ())) != 1 or len(rb.get(add, ())) != 1:
                continue
            sll, addiu = next(iter(rs[add])), next(iter(rb[add]))
            if sll is None or addiu is None:
                continue
            sm = re.match(r'\$%s,\s*\$(\w+),\s*2$' % scaled, ins[sll][1]) if ins[sll][0] == 'sll' else None
            lo = re.match(r'\$%s,\s*\$%s,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$' % (base, base), ins[addiu][1]) if ins[addiu][0] == 'addiu' else None
            if not sm or not lo or rb.get(addiu) != {addiu - 4} or ins.get(addiu - 4, ('',))[0] != 'lui':
                continue
            hi = re.match(r'\$%s,\s*((?:0[xX])?[0-9A-Fa-f]+)$' % base, ins[addiu - 4][1])
            if not hi or not straight(min(sll, addiu - 4), add):
                continue
            table = ((int(hi.group(1), 0) << 16) + int(lo.group(1), 0)) & 0xFFFFFFFF
            if table in TABLE_AT.get(jr, ()):
                found_here = (table, sll, sm.group(1))
                break
        if not found_here:
            continue
        table, sll, i = found_here
        # (The index's own slot: stored from $i as the sll read it -- the same last write of $i reaches both -- by its only
        #  store.)
        ri = reaching_writes(f, i)
        if not ri or len(ri.get(sll, ())) != 1:
            continue
        index_store = None
        for a, (mn2, ops2) in sorted(ins.items()):
            sm2 = sp_slot.match(ops2)
            if mn2 == 'sw' and sm2 and sm2.group(1) == i and ri.get(a) == ri.get(sll) and straight(min(a, sll), max(a, sll)) \
                    and len(stores_to(int(sm2.group(2), 0))) == 1:
                index_store = a
                break
        if index_store is None:
            continue
        index_offset = int(sp_slot.match(ins[index_store][1]).group(2), 0)
        # (The register the jump's index is reloaded into from that slot, before its bounds check, and not written since.)
        b = None
        for a in range(jr - 12, jr - 40, -4):
            lm = sp_slot.match(ins.get(a, ('', ''))[1]) if ins.get(a, ('',))[0] == 'lw' else None
            if lm and int(lm.group(2), 0) == index_offset:
                rr = reaching_writes(f, lm.group(1))
                if rr and rr.get(jr - 8) == {a}:
                    b = lm.group(1)
                break
        if b is None or not dead_after(jr, {t, 'at'}):
            continue
        # (2026-11-12, agent "refused2") The index the jump goes by is the one the translator switches on: jr_addend, taken at
        # the addu -- the sll's $i', scaled -- not the one reloaded from the frame before the bounds check. The two differ only
        # where something wrote that frame slot in between, which the game doesn't do but the tester's stand-ins may (a buffer
        # the function hands a callee near it): refused's version switched on the reloaded one and was DIFFERENT in 6 of
        # 238,513 tests (calls@A1). So $i' is also stored, at the sll, to a name of its own, lift_ji_<jr> -- a local in the C++
        # (lift.py jump_index_locals) -- and the lookup reads it there. The bounds check still reads the slot, as the
        # original's does. Only where every way from the function's start to the jump goes through the sll, so that local
        # always holds what jr_addend does.
        led_to = {n for ns in succ.values() for n in ns}
        entries = [a for a in addrs if a not in led_to] or [addrs[0]]
        work, seen = [a for a in entries if a != sll], set()
        while work:
            a = work.pop()
            if a in seen:
                continue
            seen.add(a)
            work += [n for n in succ.get(a, []) if n != sll and n in ins]
        if jr - 8 in seen or ins.get(sll - 4, ('',))[0] in jumps or ins.get(jr - 12, ('',))[0] in jumps:
            continue        # (and neither the sll nor the slot's load in a delay slot, where a line more can't go)
        name = 'jtbl_%08X' % table
        index = 'lift_ji_%08X' % jr
        found[jr] = (table, {sll: [ins[sll][0] + ' ' + ins[sll][1], 'sw $%s, %s' % (i, index) + MORE],
                             jr - 8: ['lw $%s, %s' % (t, index), 'sll $%s, $%s, 2' % (t, t) + MORE,
                                      'lui $at, %%hi(%s)' % name + MORE, 'addu $at, $at, $%s' % t + MORE],
                             jr - 4: ['lw $%s, %%lo(%s)($at)' % (r.group(1), name)]})
    return found


# A branch-likely and its opposite, plain branch (unlikely_branches).
UNLIKELY = {'beql': 'bne', 'bnel': 'beq', 'blezl': 'bgtz', 'bgtzl': 'blez', 'bltzl': 'bgez', 'bgezl': 'bltz', 'bc1tl': 'bc1f',
            'bc1fl': 'bc1t'}


def unlikely_branches(f, targets):
    """(2026-11-12, modcopies) The branch-likelies m2c can't take, written another way: {the branch's address: its target}.

    m2c turns `beql $v0, $zero, L` with the delay slot D into a plain branch to a label it puts just before L, when the
    instruction there is D too (flow_graph.py simplify_ido_branch_likely: "the IDO pattern"). When that instruction is itself
    the delay slot of a call, the new label is in a delay slot, and m2c stops: "Label ... refers to a delay slot; this is
    currently not supported" -- modrep_func_80019D88 and modrep_func_80017C64 (811 instructions, `jal ...; addiu $s2, $s2, 1`
    before the loop's test that the beql skips to). Each such branch is written as what it does:

        bne $v0, $zero, .Lunlikely_<address>     (the opposite test, a plain branch: not taken, the likely one is)
        nop
        D                                         (the delay slot: runs only when the likely branch is taken)
        b L
        nop
    .Lunlikely_<address>:                         (not taken: on after the delay slot, which never ran)

    the same on every way through. Only where m2c would stop -- the instruction before the target the same as the delay slot,
    and itself a call's delay slot -- so every function m2c already takes is written exactly as before (checked on the whole
    game's: only functions m2c failed on, or the mod patches, are written otherwise)."""
    found = {}
    for a, (mn, ops) in f['ins'].items():
        if mn not in UNLIKELY or a + 4 not in f['ins'] or a + 4 in targets:
            continue
        m = LABEL.search(ops)
        if not m:
            continue
        target = int(m.group(1), 16)
        before, before_before = f['ins'].get(target - 4), f['ins'].get(target - 8)
        if target - 4 == a + 4:
            continue        # (the branch's own delay slot before its target: m2c leaves that alone)
        if before is None or before_before is None or before != f['ins'][a + 4]:
            continue
        if before_before[0] in ('jal', 'jalr'):
            found[a] = target
    return found


def to_asm(f, overlay_names=False):
    """The function as GNU assembler text, with its labels placed, its calls named, and its jump tables as data.

    overlay_names (lift.py, for a function it converts with its overlay calls, 2026-11-07): a jal with no named callee -- one
    the translator looks up at run time, LOOKUP_FUNC, to whichever overlay is loaded at its address -- is written as a call to
    lift_overlay_<address> rather than func_<address>. func_<address> can also be the name of one overlay's copy there, which
    a call within that overlay names for good; the other name keeps the two kinds of call apart in m2c's C. Off, the text is
    exactly as before."""
    # Tail calls (merge.py build_group, 2026-11-08): a jump or branch out of the function, with its frame released, to a function
    # that returns for it. m2c can't follow a jump to a label outside the text, so the jump goes to a stub of its own at the end,
    # `.Ltail_<the jump's address>`, which calls the target and returns: `jal <target>__tail; nop; jr $ra; nop`. The jump's own
    # delay slot still runs before it, as on the N64; a branch's other way is unchanged. The __tail in the name tells lift.py
    # convert_calls to release the frame first (TAIL_HELPER), as the original does before it jumps. (A function without tail
    # calls is written exactly as before.)
    import merge
    tail_sites = {}
    for noted, target in sorted(f.get('tail', {}).items()):
        site = merge.jump_instruction(f['ins'], noted, int(re.search(r'L_([0-9A-F]{8})', f['ins'][noted - 4][1]).group(1), 16))
        tail_sites[site] = target
    targets = set()
    for mn, ops in f['ins'].values():
        for t in LABEL.findall(ops):
            targets.add(int(t, 16))
    tables = find_tables(f)
    # (2026-11-11, trial 5: and the ones whose address is kept in a register, named in their lui and addiu -- find_kept_tables.
    #  Given here in find_tables' form: the table, and the two instructions that get its name.)
    for jr, (address, lui, addiu, _, _) in find_kept_tables(f).items():
        tables.setdefault(jr, (address, lui, addiu))
    # (2026-11-12, agent "refused": and the ones whose slot address is kept in the frame -- find_slot_tables -- with their two
    #  loads replaced by the lookup m2c knows. Given here with no instruction to name: the replacement names the table.)
    replaced = {}
    for jr, (address, lines_at) in find_slot_tables(f).items():
        if jr not in tables:
            tables[jr] = (address, None, None)
            replaced.update(lines_at)
    named = {}
    unlikely = {a: t for a, t in unlikely_branches(f, targets).items() if a not in tail_sites and a + 4 not in tail_sites}   # (see unlikely_branches)
    lines = ['.set noat', '.set noreorder', '']
    if tables:
        lines.append('.rdata')
        for jr, (address, lui, lw) in sorted(tables.items()):
            name = 'jtbl_%08X' % address
            named[lui] = name
            named[lw] = name
            lines.append('glabel ' + name)
            entries = f['tables'][jr]
            for k in range(max(entries) + 1):
                targets.add(entries.get(k, entries[0]))
                lines.append('.word .L%08X' % entries.get(k, entries[0]))
            lines.append('')
    lines += ['.text', 'glabel ' + f['name']]
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        if a in named and mn == 'lui':
            ops = re.sub(r',\s*\S+$', ', %%hi(%s)' % named[a], ops)
        elif a in named and mn == 'lw':
            ops = re.sub(r',\s*-?(?:0[xX])?[0-9A-Fa-f]+\(', ', %%lo(%s)(' % named[a], ops)
        elif a in named and mn == 'addiu':      # (find_kept_tables: `addiu $s2, $s2, %lo(jtbl_801511F8)`)
            ops = re.sub(r',\s*-?(?:0[xX])?[0-9A-Fa-f]+$', ', %%lo(%s)' % named[a], ops)
        if a in targets:
            lines.append('.L%08X:' % a)
        if a in tail_sites:
            ops = LABEL.sub('.Ltail_%08X' % a, ops)
        ops = LABEL.sub(lambda m: '.L' + m.group(1), ops)
        if a in replaced:           # (find_slot_tables)
            lines += ['    ' + line for line in replaced[a]]
            continue
        if mn == 'jal' and (a + 4 in f['calls'] or a in f['calls']):
            # (The translator writes a call after its delay slot, so it's noted against that instruction.)
            ops = f['calls'].get(a + 4) or f['calls'][a]
        elif mn == 'jal':
            ops = ('lift_overlay_%08X' if overlay_names else 'func_%08X') % int(ops, 0)
        if a in unlikely:
            lines += ['    %s %s' % (UNLIKELY[mn], LABEL.sub('.Lunlikely_%08X' % a, f['ins'][a][1])), '    nop']
            continue
        lines.append('    %s %s' % (mn, ops))
        if a - 4 in unlikely:
            lines += ['    b .L%08X' % unlikely[a - 4], '    nop', '.Lunlikely_%08X:' % (a - 4)]
    for site, target in sorted(tail_sites.items()):
        lines += ['.Ltail_%08X:' % site, '    jal %s__tail' % target, '    nop', '    jr $ra', '    nop']
    return '\n'.join(lines) + '\n'


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    functions = read_functions()
    index = {}
    for name, f in functions.items():
        open(os.path.join(out, name + '.s'), 'w').write(to_asm(f))
        kinds = [mn for mn, _ in f['ins'].values()]
        index[name] = {
            'size': len(f['ins']),
            'calls': sorted(set(f['calls'].values())),
            'indirect_call': 'jalr' in kinds,
            'jump_table': any(mn == 'jr' and '$ra' not in ops for mn, ops in f['ins'].values()),
            # (Every jump through a table has its table written out.)
            # (And find_slot_tables', 2026-11-12, agent "refused".)
            'tables_found': all(a in find_tables(f) or a in find_kept_tables(f) or a in find_slot_tables(f)
                                for a, (mn, ops) in f['ins'].items() if mn == 'jr' and '$ra' not in ops),
        }
    json.dump(index, open(os.path.join(out, 'index.json'), 'w'), indent=0)
    print(len(index), 'functions,', sum(v['size'] for v in index.values()), 'instructions')


if __name__ == '__main__':
    main()
