#!/usr/bin/env python3
# The strict read-set checker (LIFT_CHECK_READS=1; 2026-11-16, agent argcheck; the user approved the safeguard 2026-11-16, and
# nothing counts from it until an independent review: scratch/agents/argcheck/REVIEW_ARGCHECK.txt).
#
# A second opinion on analysis.json's `use`: which registers each function reads before setting them, worked out here again with
# code of its own, and the STRICT rule for a saved register stored on the stack:
#
#   `sw $s5, 0x18($sp)` is the register being SAVED for the caller -- not a read of it -- only when the same slot is loaded back
#   into the same register on the way out: a load whose value reaches a return (or a jump out of the function) with the register
#   not set again, no other load ever reads that slot, and nothing reads the register after that load. Any other such store
#   passes the value on (a stack parameter, or a spill read back later): a read. A function with no `jr $ra` keeps the old rule
#   (a saved-register store to $sp is a save) with the reload check: every load of its slot loads the same register back.
#
# Until 0.84 analyse.py (mips.py _uses_defs) took every such store as a save, so 32 functions' reads were short and 21 converted
# callers dropped a register (scratch/agents/reviewpt2). analyse.py is being fixed; this file is there so that a disagreement
# between the two shows, and fails the function's count ("analysis disagrees") until it is explained.
#
# WHAT IS SHARED with analyse.py, and nothing else (said exactly in REVIEW_ARGCHECK.txt):
#   - the function loader: analyse.py's own text up to `HAS_EFFECTS =` is run as it is (reading RecompiledFuncs and the mod's
#     translated sources, the jump tables to_asm.py finds, merge.py putting split functions together). to_asm.py's table finder
#     and merge.py use mips.py's cfg/uses_defs to find where control goes -- control flow only, never a read set;
#   - mips.cfg (each instruction's successors) and mips.functions_at (which functions a run-time lookup of an address reaches);
#   - analyse.py's game_call_table (the mod's plat_game_call switch, parsed from the mod's source and map: which game function
#     a constant first parameter selects);
#   - system_reads.table (what the runtime's C++ functions read, read from their source and checked by hand);
#   - the independence files (independent.json, independent_by_hand.json), only to say a finding is explained.
# NOT shared: which register an instruction reads and writes (decode, below), liveness, dead results, the unaligned-load pairs,
# the stack-pointer walk, the save/restore rule, the call and jump handling, the fixpoint over the call graph.
import collections, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

# ---- Registers ----
ARG_REGS = ('a0', 'a1', 'a2', 'a3', 'f12', 'f13', 'f14', 'f15')     # (the tester's argument compare: bit i is ARG_REGS[i])
ABI_ARGS6 = {'a0', 'a1', 'a2', 'a3', 'f12', 'f14'}                    # (analyse.py's assumption for a callee it can't see)
UNKNOWN_CALLEE = ABI_ARGS6                                              # (see unknown_reads below)
RET = {'v0', 'v1', 'f0', 'f1'}                                         # (what a caller may read after a return: see exit_live)
CLOBBER = {'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7', 't8', 't9', 'ra', 'hi', 'lo',
           'fcc'} | {'f%d' % i for i in range(20)}                     # (what a call may change: the calling rules' scratch registers)
CALLEE_SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp', 'ra'} | {'f%d' % i for i in range(20, 32)}
OLD_RULE_SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp', 'ra', 'f20', 'f22', 'f24', 'f26', 'f28', 'f30'}
NOT_INPUTS = {'sp', 'ra'}
# (Registers a caller may read after a call as the callee's result besides v0/v1/f0/f1: any the callee may change (may_change),
#  scratch or saved -- rw_contacts_s0 hands back s0. Never sp, ra (the jal's own) or zero. Until reviewac R1: the scratch ones.)
NOT_RESULTS = RET | {'sp', 'ra', 'zero'}
RESULT_REGS = CLOBBER - RET - {'ra'}      # (what a call the checker can't see may hand back)                                               # (never counted as read at entry, as analysis.json)

# ---- Decoding: what each instruction reads and writes. Written for this checker from the MIPS manual, not from mips.py. ----
OPS = re.compile(r'\$(\w+)')
MEMOP = re.compile(r'^\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)$')
ALU3 = {'add', 'addu', 'sub', 'subu', 'and', 'or', 'xor', 'nor', 'slt', 'sltu', 'sllv', 'srlv', 'srav'}
ALUI = {'addi', 'addiu', 'andi', 'ori', 'xori', 'slti', 'sltiu', 'sll', 'srl', 'sra'}
LOADS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwc1', 'ldc1', 'ld', 'lwu'}
STORES = {'sb', 'sh', 'sw', 'swc1', 'sdc1', 'sd', 'swl', 'swr'}
BRANCH2 = {'beq', 'bne', 'beql', 'bnel'}
BRANCH1 = {'bgez', 'bgtz', 'blez', 'bltz', 'bgezl', 'bgtzl', 'blezl', 'bltzl'}
BRANCH_FP = {'bc1t', 'bc1f', 'bc1tl', 'bc1fl'}
JUMPS = BRANCH2 | BRANCH1 | BRANCH_FP | {'j', 'b', 'jr', 'jal', 'jalr'}
WIDTH = {'lb': 1, 'lbu': 1, 'sb': 1, 'lh': 2, 'lhu': 2, 'sh': 2, 'lw': 4, 'lwu': 4, 'sw': 4, 'lwc1': 4, 'swc1': 4, 'lwl': 4,
         'lwr': 4, 'swl': 4, 'swr': 4, 'ldc1': 8, 'sdc1': 8, 'ld': 8, 'sd': 8}
# (Instructions that do something besides setting registers: never dropped as dead.)
EFFECTS = STORES | JUMPS | {'break', 'syscall', 'sync', 'cache', 'ctc1', 'mtc0', 'eret'}


def regs(ops):
    """The register names in an operand string, $zero and the FP control register (FpcCsr: global state, as memory) left out of
    nothing yet -- decode drops them."""
    return OPS.findall(ops)


def fpair(r):
    """A double's registers: an even FP register and the odd one above it (the N64's 32-bit FP register mode)."""
    m = re.fullmatch(r'f(\d+)', r)
    return {r, 'f%d' % (int(m.group(1)) + 1)} if m and int(m.group(1)) % 2 == 0 else {r}


def decode(mn, ops):
    """(reads, writes) of one instruction, by itself. A store reads the stored register and the base; whether a saved
    register's store to the stack is a save is decided per function (save_stores), not here."""
    r = regs(ops)
    u, d = set(), set()
    if mn in ('nop', 'sync', 'break', 'syscall', 'cache', 'j', 'b'):
        pass
    elif mn == 'jal':
        d = {'ra'}
    elif mn == 'jr':
        u = {r[0]}
    elif mn == 'jalr':
        u, d = {r[-1]}, {'ra'} if len(r) == 1 else {r[0]}
    elif mn in ALU3:
        d, u = {r[0]}, set(r[1:])
    elif mn in ALUI or mn == 'negu':
        d, u = {r[0]}, set(r[1:])
    elif mn == 'lui':
        d = {r[0]}
    elif mn in LOADS:
        m = MEMOP.match(ops)
        d, u = (fpair(m.group(1)) if mn in ('ldc1',) else {m.group(1)}), {m.group(3)}
    elif mn in ('lwl', 'lwr'):
        m = MEMOP.match(ops)
        d, u = {m.group(1)}, {m.group(1), m.group(3)}      # (merges part of a word into the register: reads it too)
    elif mn in STORES:
        m = MEMOP.match(ops)
        u = (fpair(m.group(1)) if mn == 'sdc1' else {m.group(1)}) | {m.group(3)}
    elif mn in BRANCH2 or mn in BRANCH1:
        u = {x for x in r if not x.startswith('L_')}
    elif mn in BRANCH_FP:
        u = {'fcc'}
    elif mn in ('mult', 'multu', 'div', 'divu'):
        u, d = set(r[-2:]), {'hi', 'lo'}                 # (`div $zero, $v0, $a2` and `mult $s2, $s7` alike: the last two)
    elif mn in ('mfhi', 'mflo'):
        u, d = {'hi' if mn == 'mfhi' else 'lo'}, {r[0]}
    elif mn in ('mthi', 'mtlo'):
        u, d = {r[0]}, {'hi' if mn == 'mthi' else 'lo'}
    elif mn == 'mfc1':
        d, u = {r[0]}, {r[1]}
    elif mn == 'mtc1':
        u, d = {r[0]}, {r[1]}
    elif mn == 'cfc1':
        d = {r[0]}                                       # (reads the FP control register: global state, as memory is)
    elif mn == 'ctc1':
        u = {r[0]}
    elif mn.startswith('c.'):
        fmt = mn.split('.')[-1]
        u = set().union(*[(fpair(x) if fmt == 'd' else {x}) for x in r])
        d = {'fcc'}
    elif mn.startswith(('cvt.', 'trunc.', 'round.', 'floor.', 'ceil.')):
        _, to, frm = mn.split('.')
        d = fpair(r[0]) if to in ('d', 'l') else {r[0]}
        u = fpair(r[1]) if frm in ('d', 'l') else {r[1]}
    elif re.fullmatch(r'(add|sub|mul|div)\.[sd]', mn):
        dbl = mn.endswith('.d')
        d = fpair(r[0]) if dbl else {r[0]}
        u = set().union(*[(fpair(x) if dbl else {x}) for x in r[1:]])
    elif re.fullmatch(r'(mov|neg|abs|sqrt)\.[sd]', mn):
        dbl = mn.endswith('.d')
        d, u = (fpair(r[0]) if dbl else {r[0]}), (fpair(r[1]) if dbl else {r[1]})
    else:
        raise ValueError('readcheck: an instruction it does not know: %s %s' % (mn, ops))
    return u - {'zero'}, d - {'zero'}


def mem_operand(ops):
    """(register, offset, base) of a load or store's operands."""
    m = MEMOP.match(ops)
    return m.group(1), int(m.group(2), 0), m.group(3)


# ---- The functions (the SHARED loader: analyse.py's own text up to HAS_EFFECTS) ----
def load(tools=HERE):
    tools = os.path.abspath(tools)
    """The functions as analyse.py reads them: {name: {'ins': {addr: (mn, ops)}, 'calls', 'lookup', 'tables', 'pieces'?}}, and
    the shared helpers. Runs from the project root, as analyse.py does."""
    sys.path.insert(0, tools)
    here = os.getcwd()
    os.chdir(os.path.abspath(os.path.join(tools, '..', '..')))
    saved_argv = sys.argv
    sys.argv = ['analyse.py']
    try:
        src = open(os.path.join(tools, 'analyse.py'), encoding='utf8').read()
        g = {'__file__': os.path.join(tools, 'analyse.py'), '__name__': 'readcheck_loader'}
        exec(compile(src[:src.index('HAS_EFFECTS = ')], 'analyse.py (loader)', 'exec'), g)
        # (the mod's plat_game_call table: shared too)
        start, end = src.index('def game_call_table():'), src.index('CONSTANT_A0 = ')
        exec(compile(src[start:end], 'analyse.py (game_call_table)', 'exec'), g)
    finally:
        sys.argv = saved_argv
        os.chdir(here)
    import mips, system_reads, modrep
    return g['funcs'], g.get('GAME_CALL'), g.get('GAME_CALL_CASES', {}), mips, system_reads, modrep


# ---- One function ----
class Function:
    """What the checker knows about one function's own code: its control flow, its stack-pointer offsets, its calls and jumps out,
    and its candidate saves."""

    def __init__(self, name, f, mips):
        self.name, self.f = name, f
        self.ins = f['ins']
        self.addrs, self.succ = mips.cfg(f)                  # (SHARED: control flow)
        self.preds = collections.defaultdict(list)
        for a in self.addrs:
            for s in self.succ[a]:
                self.preds[s].append(a)
        self.dec = {a: decode(*self.ins[a]) for a in self.addrs}
        self.unaligned_first()
        self.sp = self.stack_offsets()
        self.has_return = any(mn == 'jr' and re.fullmatch(r'\$ra', ops.strip()) for mn, ops in self.ins.values())
        # A call's delay slot: the instruction after a jal/jalr (the call happens after it).
        self.call_at = {}      # delay slot address -> (jal address, kind)
        for a in self.addrs:
            mn = self.ins[a][0]
            if mn in ('jal', 'jalr') and a + 4 in self.ins:
                self.call_at[a + 4] = a
        # Jumps into another function: a call the translator noted where no jal/jalr is (a `j` written as a call, then a return).
        self.jumps_out = {a: c for a, c in f['calls'].items()
                          if self.ins.get(a - 4, ('',))[0] not in ('jal', 'jalr') and self.ins.get(a, ('',))[0] not in ('jal', 'jalr')}

    def unaligned_first(self):
        """An unaligned word load is two instructions (lwl and lwr, either order, the same register and base, offsets 3 apart):
        together they write the whole register, so the first -- when the second is its one successor and it is in no delay slot
        -- does not read what the register held."""
        for a in self.addrs:
            mn, ops = self.ins[a]
            nxt = self.ins.get(a + 4)
            if mn not in ('lwl', 'lwr') or not nxt or {mn, nxt[0]} != {'lwl', 'lwr'} or self.succ.get(a) != [a + 4]:
                continue
            if self.ins.get(a - 4, ('',))[0] in JUMPS:
                continue
            r1, o1, b1 = mem_operand(ops)
            r2, o2, b2 = mem_operand(nxt[1])
            if r1 == r2 and b1 == b2 and r1 != b1 and abs(o1 - o2) == 3:
                u, d = self.dec[a]
                self.dec[a] = (u - {r1}, d)

    def stack_offsets(self):
        """The stack pointer's offset from its value at entry, before each instruction runs: a number, or None where it isn't one
        number on every way there (set some other way than `addiu $sp, $sp, k`, or different on two ways in)."""
        UNK = object()
        at = {self.addrs[0]: 0} if self.addrs else {}
        work = list(at)
        while work:
            a = work.pop()
            v = at[a]
            mn, ops = self.ins[a]
            out = v
            if 'sp' in self.dec[a][1]:
                m = re.fullmatch(r'\$sp,\s*\$sp,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)', ops.strip())
                out = (v + int(m.group(1), 0)) if (mn in ('addiu', 'addi') and m and v is not UNK) else UNK
            for s in self.succ[a]:
                if s not in at:
                    at[s] = out
                    work.append(s)
                elif at[s] is not UNK and at[s] != out:
                    at[s] = UNK
                    work.append(s)
        return {a: (None if v is UNK else v) for a, v in at.items()}

    def slot(self, a):
        """(register, entry-relative slot, width) of a load or store through $sp at a, or None (another base, or an unknown
        stack offset there)."""
        mn, ops = self.ins[a]
        if mn not in WIDTH:
            return None
        r, off, base = mem_operand(ops)
        if base != 'sp':
            return None
        d = self.sp.get(a)
        return None if d is None else (r, d + off, WIDTH[mn])

    def sp_address_escapes(self):
        """Each instruction that uses $sp's value other than as a load's or store's base or to move $sp itself -- an address in
        the stack made (`addiu $a0, $sp, 0x18`, `addu $a1, $sp, $zero`) or $sp stored -- with the entry-relative address it
        makes: a number, or None where it isn't known (another register added, $sp stored, an unknown stack offset)."""
        for a in self.addrs:
            mn, ops = self.ins[a]
            u, d = self.dec[a]
            if 'sp' not in u or (mn in WIDTH and mem_operand(ops)[2] == 'sp' and mem_operand(ops)[0] != 'sp') or 'sp' in d:
                continue
            delta = self.sp.get(a)
            r = regs(ops)
            m = re.fullmatch(r'\$\w+,\s*\$sp,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)', ops.strip())
            if delta is not None and mn in ('addiu', 'addi') and m:
                yield a, delta + int(m.group(1), 0)
            elif delta is not None and mn in ('addu', 'or') and len(r) == 3 and sorted(r[1:]) == ['sp', 'zero']:
                yield a, delta
            else:
                yield a, None


class GameCase(tuple):
    """The game functions one case of plat_game_call runs (callee_of): read with plat_game_call's argument shift (reads_of)."""


GAME_CALL_SHIFT = {'a0': 'a1', 'a1': 'a2', 'a2': 'a3', 'a3': 'a3'}      # (the case function's register -> the caller's)


# ---- Liveness, with the save rule ----
class Checker:
    def __init__(self, tools=HERE, independence=True, unknown_callee=None):
        tools = os.path.abspath(tools)
        # (What a call it can't resolve is taken to read: analyse.py's six by default. lift.py's argument compare passes all
        #  eight ARG_REGS, f13/f15 too, so a double handed on through such a call is never skipped -- reviewac G3.)
        self.unknown = set(unknown_callee) if unknown_callee is not None else set(UNKNOWN_CALLEE)
        funcs, self.game_call, self.game_call_cases, mips, system_reads, modrep = load(tools)
        self.mips, self.modrep = mips, modrep
        self.funcs = funcs
        self.F = {n: Function(n, f, mips) for n, f in funcs.items()}
        self.at_address = mips.functions_at(funcs)                       # (SHARED: run-time lookups)
        called = sorted({c for f in funcs.values() for c in f['calls'].values() if c not in funcs})
        self.system = system_reads.table(called)                          # (SHARED: the runtime's C++ functions)
        self.twin = {modrep.replaced(n): n for n in funcs if modrep.is_modrep(n) and modrep.replaced(n) in funcs}
        self.merged = {n for n, f in funcs.items() if 'pieces' in f}
        self.indep = {}
        if independence:
            p = os.path.join(tools, 'independent.json')
            if os.path.exists(p):
                self.indep = {n: set(r) for n, r in json.load(open(p)).items()}
            p = os.path.join(tools, 'independent_by_hand.json')
            if os.path.exists(p):
                for n, v in json.load(open(p)).items():
                    if n != 'about':
                        self.indep.setdefault(n, set()).update(v['registers'])
            for n in self.merged:
                self.indep.pop(n, None)
        self.unknown_calls = {}      # (kind of call this checker could not resolve -> the functions with one, for the report)
        self._current, self._deps, self._ret = None, set(), set()
        self.V = {}                  # ((function, None or a RESULT_REGS register) -> entry reads: run)
        self._new = []               # (items first demanded during the current evaluation: run queues them)

    # -- which function a call or jump reaches --
    def constant_a0(self, F, jal):
        """The constant in a0 at a call, when one `addiu/ori $a0, $zero, K` in the same straight run sets it (the delay slot
        first, then back through single-predecessor instructions, stopping at another call)."""
        def value(a):
            mn, ops = F.ins[a]
            m = re.fullmatch(r'\$a0,\s*\$zero,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)', ops.strip())
            return int(m.group(1), 0) if mn in ('addiu', 'ori') and m else None
        if jal + 4 in F.ins and 'a0' in F.dec[jal + 4][1]:
            return value(jal + 4)
        x = jal
        for _ in range(64):
            ps = F.preds.get(x, [])
            if len(ps) != 1:
                return None
            x = ps[0]
            if F.ins[x][0] in ('jal', 'jalr'):
                return None
            if 'a0' in F.dec[x][1]:
                return value(x)
        return None

    def callee_of(self, F, jal):
        """The callee of the call at jal: a name, a tuple of names (a run-time lookup, or one case of plat_game_call), or None."""
        mn, ops = F.ins[jal]
        c = F.f['calls'].get(jal + 4) or F.f['calls'].get(jal)
        if mn == 'jal' and not c:
            try:
                names = self.at_address.get(int(ops.strip(), 0))
            except ValueError:
                names = None
            c = tuple(names) if names else None
        if c is not None and c == self.game_call:
            k = self.constant_a0(F, jal)
            if k in self.game_call_cases:
                c = GameCase(self.game_call_cases[k])
        return c

    def reads_of(self, c, R, demand=frozenset()):
        """What a callee reads at entry, for its caller: R for a function with code; the runtime's table for a system function;
        else the calling rules' six argument registers (as analyse.py assumes: see the review's known limits)."""
        if isinstance(c, GameCase):
            # (One case of the mod's plat_game_call(fn, a0, a1, a2): it hands its a1, a2, a3 on as the case function's a0, a1, a2
            #  -- `or $a0, $a1, $zero` in the delay slot of its call -- and its own a3 stays a3. So the case's a0 is the caller's
            #  a1, and so on; plus a0 itself, the case number the switch reads. reviewac F2.)
            r = set().union(*(self.reads_of(x, R) for x in c))      # (plat_game_call hands back v0 only: no other demand)
            return (r - set(GAME_CALL_SHIFT)) | {GAME_CALL_SHIFT[x] for x in r & set(GAME_CALL_SHIFT)} | {'a0'}
        if isinstance(c, tuple):
            return set().union(*(self.reads_of(x, R, demand) for x in c)) if c else set(self.unknown)
        if c is None:
            self.unknown_calls.setdefault('indirect', set()).add(self._current)
            return set(self.unknown)
        if c in self.F:
            # (its reads with nothing but v0/v1/f0/f1 read after it, and those with each other register this caller reads after)
            out = set()
            for key in [None] + sorted(set(demand) - NOT_RESULTS):
                item = (c, key)
                if item not in self.V:
                    self.V[item] = set()
                    self._new.append(item)
                self._deps.add(item)
                out |= self.V[item]
            return out
        if c in self.system:
            return set(self.system[c])
        self.unknown_calls.setdefault('no code: ' + c, set()).add(self._current)
        return set(self.unknown)

    # -- one function's entry reads, given every callee's --
    def exit_live(self, F, a, R):
        """What is read after an instruction that ends the function's path: a return gives the caller v0, v1, f0, f1; a jump into
        another function runs it with the registers as they are."""
        if a in F.jumps_out:
            return set(self.reads_of(F.jumps_out[a], R, self._ret))      # (it returns to this function's callers)
        prev = F.ins.get(a - 4, ('', ''))
        if prev[0] == 'jr' and prev[1].strip() == '$ra':
            # (A return. v0, v1, f0, f1 are kept by never taking an instruction that writes one of them as dead: see live -- taking
            #  them as live here would have every void function "read" v0 at entry. Another register a caller reads after its
            #  call is live here in the evaluation for that register (self._ret: run) -- rw_cam_sel_a3 returns in a3,
            #  `jr $ra; andi $a3, $a0, 0xFFFF`, so a caller reading a3 has it read a0. Until 2026-11-16 nothing was, as in
            #  analyse.py's result_unused: argsweep's finding, func_800E28C0 dropping that a0.)
            return set(self._ret)
        if prev[0] == 'jr':
            # (a jump through a register with no table: to anywhere)
            self.unknown_calls.setdefault('jr without table', set()).add(self._current)
            return set(self.unknown)
        return set()

    def live_out(self, F, a, live, R):
        """What is live just after the instruction at a (at a call's delay slot: before the callee runs)."""
        if F.succ[a]:
            out = set().union(*(live[s] for s in F.succ[a]))
            if a in F.jumps_out:
                # (A conditional branch into another function: the translator notes it as a call on the delay slot, which also
                #  goes on to the fall-through. Taken, the target runs with the registers as they are. reviewac F1.)
                out |= self.reads_of(F.jumps_out[a], R, self._ret)
        else:
            out = self.exit_live(F, a, R)
        if a in F.call_at:
            # The delay slot of a call: it runs, then the callee, then the instruction after. The callee's reads for the result
            # registers THIS function's own code reads after it (own_live).
            c = self.callee_of(F, F.call_at[a])
            m = self.may_change_of(c)
            own = set().union(*(self._own[s] for s in F.succ[a])) if F.succ[a] else set(self._ret)
            # (What the callee may change is gone after it (reviewac R1: from its own code, may_change_of). Of what it leaves
            #  alone, a saved register flows across, and a scratch register only where this function's own code reads it after
            #  the call (rw_cam_sel_a3 leaves a1 and a2 for func_800E28C0 to read): a later callee's assumed read of f12 -- the
            #  calling rules' guess for a callee the checker can't see -- is no reliance on this one leaving it. A register the
            #  caller reads after the call that the callee may change is that callee's result: demanded of it.)
            out = (out - m - (CLOBBER - own)) | self.reads_of(c, R, (own & m) - NOT_RESULTS)
        return out

    def own_live(self, F):
        """Liveness of F's OWN reads: a call kills the scratch registers and adds nothing, a return (or a jump out) has the
        current demand (self._ret) live, a jump through a register with no table nothing. What a caller's code itself reads
        after a call -- the result registers it relies on (live_out)."""
        live = {a: set() for a in F.addrs}
        changed = True
        while changed:
            changed = False
            for a in reversed(F.addrs):
                if F.succ[a]:
                    out = set().union(*(live[s] for s in F.succ[a]))
                    if a in F.jumps_out:
                        out |= self._ret
                else:
                    prev = F.ins.get(a - 4, ('', ''))
                    out = set(self._ret) if (a in F.jumps_out or (prev[0] == 'jr' and prev[1].strip() == '$ra')) else set()
                if a in F.call_at:
                    out = out - self.may_change_of(self.callee_of(F, F.call_at[a]))
                u, d = F.dec[a]
                mn = F.ins[a][0]
                if mn in ('jal', 'jalr'):
                    new = (out - d) | u
                elif d and mn not in EFFECTS and not (d & out) and not (d & RET):
                    new = out
                else:
                    new = (out - d) | u
                if new != live[a]:
                    live[a] = new
                    changed = True
        return live

    def live(self, F, R, saves):
        """Backward liveness over F: {address: registers live before it}. `saves`: the store addresses taken as saves (they
        read neither the stored register nor $sp)."""
        live = {a: set() for a in F.addrs}
        changed = True
        while changed:
            changed = False
            for a in reversed(F.addrs):
                out = self.live_out(F, a, live, R)
                u, d = F.dec[a]
                mn = F.ins[a][0]
                if a in saves:
                    u = set()
                if mn in ('jal', 'jalr'):
                    new = (out - d) | u          # (jal sets ra; the call itself is at the delay slot)
                elif d and mn not in EFFECTS and not (d & out) and not (d & RET):
                    new = out                    # (its results are never read: what it reads doesn't matter)
                else:
                    new = (out - d) | u
                if new != live[a]:
                    live[a] = new
                    changed = True
        return live

    def save_candidates(self, F):
        """Stores of a callee-saved register through $sp at a known offset: {address: (register, slot, width)}."""
        out = {}
        for a in F.addrs:
            mn = F.ins[a][0]
            if mn in ('sw', 'swc1', 'sdc1', 'sd'):
                s = F.slot(a)
                if s and s[0] in CALLEE_SAVED:
                    out[a] = s
        return out

    def reaches_exit_unset(self, F, start, reg):
        """Whether, after the instruction at start, some way to the end of the function leaves reg as start left it."""
        seen = set()
        work = [start]
        while work:
            a = work.pop()
            if not F.succ[a] or a in F.jumps_out:      # (the end of a way, or a jump out of the function from it)
                return True
            for s in F.succ[a]:
                if s in seen:
                    continue
                seen.add(s)
                if reg in F.dec[s][1] or (s in F.call_at and reg in self.may_change_of(self.callee_of(F, F.call_at[s]))):
                    continue
                work.append(s)
        return False

    def entry_reads(self, F, R):
        """F's registers read at entry, given each callee's (R), and the saves the strict rule allows."""
        if not F.addrs:
            return set(), {}
        self._own = self.own_live(F)
        cands = self.save_candidates(F)
        # Loads through $sp, each by its slot; any at an unknown offset makes every slot unknown.
        loads = {}
        unknown_load = False
        for a in F.addrs:
            mn, ops = F.ins[a]
            if mn in LOADS or mn in ('lwl', 'lwr'):
                if mem_operand(ops)[2] != 'sp':
                    continue
                s = F.slot(a)
                if s is None:
                    unknown_load = True
                else:
                    loads[a] = s
        RELOAD = {'sw': 'lw', 'sd': 'ld', 'swc1': 'lwc1', 'sdc1': 'ldc1'}
        if not F.has_return:
            # (No `jr $ra`: the old rule -- a store of one of mips.SAVED's registers through $sp is a save -- but with the reload
            #  check (d) too, reviewac F3: only where every load overlapping its slot loads the same register back from it, and
            #  the slot and every $sp load's offset are known.)
            saves = set()
            for a in F.addrs:
                mn, ops = F.ins[a]
                if mn not in RELOAD or mem_operand(ops)[2] != 'sp' or mem_operand(ops)[0] not in OLD_RULE_SAVED:
                    continue
                sl = F.slot(a)
                if sl is None or unknown_load:
                    continue
                reg, slot, width = sl
                if all(loads[l] == (reg, slot, width) and F.ins[l][0] == RELOAD[mn]
                       for l, (r2, s2, w2) in loads.items() if s2 < slot + width and slot < s2 + w2):
                    saves.add(a)
            live = self.live(F, R, saves)
            return live[F.addrs[0]] - NOT_INPUTS, {'rule': 'old (no jr $ra) with the reload check', 'saves': sorted(saves)}
        restores = {}         # store address -> [restore load addresses]
        if not unknown_load:
            for st, (reg, slot, width) in cands.items():
                overlapping = [l for l, (r2, s2, w2) in loads.items() if s2 < slot + width and slot < s2 + w2]
                ok = [l for l in overlapping if loads[l] == (reg, slot, width) and
                      F.ins[l][0] == RELOAD[F.ins[st][0]] and
                      self.reaches_exit_unset(F, l, reg)]
                if ok and len(ok) == len(overlapping):
                    restores[st] = ok
        saves = set(restores)
        while True:
            live = self.live(F, R, saves)
            # A restore whose register is read before the end: the saved value is used, so the store is a read after all.
            drop = set()
            for st in saves:
                reg = cands[st][0]
                for l in restores[st]:
                    after = self.live_out(F, l, live, R)
                    if fpair(reg) & after if F.ins[st][0] == 'sdc1' else reg in after:
                        drop.add(st)
            if not drop:
                break
            saves -= drop
        return live[F.addrs[0]] - NOT_INPUTS, {'rule': 'strict', 'saves': sorted(saves),
                                               'not_saves': sorted(set(cands) - saves), 'unknown_load': unknown_load}

    def names_in(self, c):
        """The functions with code a callee value stands for (a name, a lookup tuple, a plat_game_call case)."""
        if isinstance(c, tuple):
            return [x for x in c if x in self.F]
        return [c] if c in self.F else []

    def callers_of(self, names):
        """{function: sorted callees of `names` it calls -- a jal (a run-time lookup's functions, a plat_game_call case's
        included), or a jump into another function}. For rule B (lift.py score)."""
        names = set(names)
        out = collections.defaultdict(set)
        for n, F in self.F.items():
            for jal in F.call_at.values():
                out[n] |= set(self.names_in(self.callee_of(F, jal))) & names
            for c in F.jumps_out.values():
                out[n] |= set(self.names_in(c)) & names
        return {n: sorted(v) for n, v in out.items() if v}

    # -- the whole program --
    def may_change_of(self, c):
        """What a call to c may change (self.M, compute_may_change): a function's own set; for a run-time lookup or a
        plat_game_call case, any of them (and plat_game_call itself); anything the checker can't see, the scratch registers."""
        if isinstance(c, GameCase):
            return set().union(*(self.may_change_of(x) for x in c)) | self.may_change_of(self.game_call)
        if isinstance(c, tuple):
            return set().union(*(self.may_change_of(x) for x in c)) if c else set(CLOBBER)
        if c in self.F:
            return self.M.get(c, set())
        return set(CLOBBER)

    def changed_at_ends(self, F):
        """F's own may-change set, given its callees' (self.M): a forward walk of the registers whose value may differ from
        their value at entry, and the stack slots that surely still hold a register's entry value (stored there before the
        register changed, never overwritten since): loading one back puts the register back. What may differ at a return or
        a jump out of the function (with the jumped-to function's set) is what F may change."""
        if not F.addrs:
            return set()
        TOP = None
        state = {F.addrs[0]: (frozenset(), frozenset())}         # (registers that may differ, slots (offset, width, reg) intact)
        work = [F.addrs[0]]
        out_m = set()
        while work:
            a = work.pop()
            diff, slots = state[a]
            diff, slots = set(diff), set(slots)
            mn, ops = F.ins[a]
            u, d = F.dec[a]
            if mn in STORES and mem_operand(ops)[2] == 'sp':
                sl = F.slot(a)
                if sl is None:
                    slots = set()                               # (a store at an unknown offset: no slot is surely intact)
                else:
                    reg, off, width = sl
                    slots = {x for x in slots if not (x[0] < off + width and off < x[0] + x[1])}
                    regs = fpair(reg) if mn == 'sdc1' else {reg}
                    if not (regs & diff) and mn in ('sw', 'swc1', 'sdc1', 'sd'):
                        slots.add((off, width, reg))
            if mn in LOADS and mem_operand(ops)[2] == 'sp':
                sl = F.slot(a)
                if sl is not None and (sl[1], sl[2], sl[0]) in slots:
                    diff -= d                                    # (put back from its save slot)
                else:
                    diff |= d
            else:
                diff |= d
            if a in F.call_at:
                diff |= self.may_change_of(self.callee_of(F, F.call_at[a])) | {'ra'}
            ends = not F.succ[a]
            if a in F.jumps_out:
                out_m |= diff | self.may_change_of(F.jumps_out[a])
            elif ends:
                prev = F.ins.get(a - 4, ('', ''))
                out_m |= diff | (set(CLOBBER) if prev[0] == 'jr' and prev[1].strip() != '$ra' else set())
            for s_ in F.succ[a]:
                new = (frozenset(diff), frozenset(slots))
                old = state.get(s_)
                if old is not None:
                    new = (old[0] | new[0], old[1] & new[1])
                if new != old:
                    state[s_] = new
                    work.append(s_)
        return out_m - {'sp', 'zero'}

    def compute_may_change(self):
        """self.M: every function's may-change set, to a fixpoint over the call graph (from nothing, only growing). A mod copy's
        is taken with its game function's (either may run)."""
        self.M = {n: set() for n in self.F}
        users = collections.defaultdict(set)
        for n, F in self.F.items():
            for jal in F.call_at.values():
                c = self.callee_of(F, jal)
                for x in self.names_in(c) + ([self.game_call] if isinstance(c, GameCase) and self.game_call in self.F else []):
                    users[x].add(n)
            for c in F.jumps_out.values():
                for x in self.names_in(c):
                    users[x].add(n)
        for n, t in self.twin.items():
            users[t].add(n)
        work = collections.deque(self.F)
        queued = set(self.F)
        while work:
            n = work.popleft()
            queued.discard(n)
            m = self.changed_at_ends(self.F[n])
            if n in self.twin:
                m |= self.M[self.twin[n]]
            if not m <= self.M[n]:
                self.M[n] |= m
                for u in users[n]:
                    if u not in queued:
                        queued.add(u)
                        work.append(u)

    def run(self, explained=True, names=None):
        """Every function's entry reads, to a fixpoint over the call graph: {function: its reads as its callers see them} (with
        nothing but v0/v1/f0/f1 read after it, and with each other register some caller really reads after its call -- see
        RESULT_REGS and reads_of). explained=True subtracts each function's independence-file registers (as analysis.json's
        `use` does); False is the raw reads."""
        if not getattr(self, 'M', None):
            self.compute_may_change()
        self.V = {(n, None): set() for n in self.F}
        info = {}
        users = collections.defaultdict(set)      # (item -> the items whose evaluation used it)
        work = collections.deque(self.V)
        queued = set(self.V)
        rounds = 0
        while work:
            item = work.popleft()
            queued.discard(item)
            n, key = item
            rounds += 1
            self._current, self._deps, self._new = n, set(), []
            self._ret = set() if key is None else {key}
            r, i = self.entry_reads(self.F[n], self.V)
            if explained:
                r = r - self.indep.get(n, set())
            if n in self.twin:
                # (func_X may run as its mod copy: either one's reads, for the same demand)
                t = (self.twin[n], key)
                if t not in self.V:
                    self.V[t] = set()
                    self._new.append(t)
                self._deps.add(t)
                r = r | self.V[t]
            for d in self._deps:
                users[d].add(item)
            for d in self._new:
                if d not in queued:
                    queued.add(d)
                    work.append(d)
            if key is None:
                info[n] = i
            if r != self.V[item]:
                if not (self.V[item] <= r):
                    raise SystemExit('readcheck: %s read set shrank (%s -> %s): not monotone' % (item, sorted(self.V[item]), sorted(r)))
                self.V[item] = r
                for u in users[item]:
                    if u not in queued:
                        queued.add(u)
                        work.append(u)
        self.rounds = rounds
        self._ret = set()
        R = {n: set(self.V[(n, None)]) for n in self.F}
        self.demanded = collections.defaultdict(set)
        for (n, key), v in self.V.items():
            if key is not None:
                R[n] |= v
                self.demanded[n].add(key)
        return R, info

    # -- stack parameters --
    def stack_reads(self):
        """For each function, which incoming stack-parameter words it may read (word k: entry $sp + 0x10 + 4k), or None for
        any: a load through $sp at an offset reaching them; None for an address made at or above entry's $sp or one not known
        (varargs' va_list), an unknown stack offset, or a jump into another function at entry's $sp (its parameters passed on).
        (An address of the function's own frame, below entry's $sp, is taken not to reach above it: no C function indexes a
        local past its end into its caller's frame. A known limit, in the review.)"""
        out = {}
        for n, F in self.F.items():
            words, unknown = set(), False
            # An address made at or above entry's $sp (the home slots, the stack parameters: varargs' va_list), or one not
            # known, may be read through. One below it is the function's own frame: a local array or structure.
            if any(e is None or e >= 0 for _, e in F.sp_address_escapes()):
                unknown = True
            for a in F.addrs:
                mn, ops = F.ins[a]
                if mn in WIDTH and mem_operand(ops)[2] == 'sp' and (mn in LOADS or mn in ('lwl', 'lwr')):
                    s = F.slot(a)
                    if s is None:
                        unknown = True
                        continue
                    _, e, w = s
                    for b in range(e, e + w):
                        if b >= 0x10:
                            words.add((b - 0x10) // 4)
            for a, c in F.jumps_out.items():
                if F.sp.get(a) == 0:
                    unknown = True    # (a jump into another function at entry's stack pointer passes the stack parameters on)
            out[n] = None if unknown else words
        # (func_X may run as its mod copy modrep_func_X: either one's words, as run() merges their registers. reviewac G4.)
        for n, t in self.twin.items():
            out[n] = None if (out.get(n) is None or out.get(t) is None) else out[n] | out[t]
        return out

    def outgoing_words(self, n):
        """How many words from $sp + 0x10, at its calls, F writes as outgoing stack parameters before its own locals begin: the
        lowest slot above that which F itself loads or whose address it takes bounds them (at most 16). None if F's calls are
        made at different or unknown stack offsets."""
        F = self.F[n]
        at_calls = {F.sp.get(a) for a in F.addrs if F.ins[a][0] in ('jal', 'jalr')}
        if len(at_calls) != 1 or None in at_calls:
            return None
        base = at_calls.pop() + 0x10
        low = base + 0x40
        for a in F.addrs:
            mn, ops = F.ins[a]
            d = F.sp.get(a)
            if d is not None and mn in WIDTH and mem_operand(ops)[2] == 'sp' and (mn in LOADS or mn in ('lwl', 'lwr')):
                e = d + mem_operand(ops)[1]
                if e >= base:
                    low = min(low, e)
        for _, e in F.sp_address_escapes():
            if e is not None and e >= base:
                low = min(low, e)
        return max(0, min(16, (low - base) // 4))


def findings(R, analysis, names=None):
    """{function: [registers the checker finds it reads that analysis.json's use doesn't list]}."""
    out = {}
    for n in (names or R):
        if n not in analysis:
            continue
        miss = R[n] - set(analysis[n]['use'])
        if miss:
            out[n] = sorted(miss)
    return out


def main():
    import argparse
    p = argparse.ArgumentParser(description='The strict read-set checker: registers analysis.json omits.')
    p.add_argument('--analysis', default=os.path.join(HERE, 'analysis.json'))
    p.add_argument('--analysis-mod', default=None)
    p.add_argument('--tools', default=HERE, help='the tools folder whose loader and independence files to use')
    p.add_argument('--out', default=None, help='write the findings and the read sets as JSON')
    a = p.parse_args()
    ck = Checker(a.tools)
    R, info = ck.run(explained=True)
    raw, _ = ck.run(explained=False)
    analysis = json.load(open(a.analysis))
    mod_path = a.analysis_mod or os.path.join(os.path.dirname(os.path.abspath(a.analysis)), 'analysis_mod.json')
    if os.path.exists(mod_path):
        analysis.update(json.load(open(mod_path)))
    found = findings(R, analysis)
    over = {n: sorted(set(analysis[n]['use']) - R[n]) for n in R if n in analysis and set(analysis[n]['use']) - R[n]}
    print('readcheck: %d functions, %d rounds; analysis disagrees (reads it omits) for %d functions' % (len(R), ck.rounds, len(found)))
    for n in sorted(found):
        print('  %-32s +%s' % (n, ','.join(found[n])))
    print('(analysis lists more than the checker for %d functions: not findings)' % len(over))
    print('unresolved calls: %s' % {k: len(v) for k, v in sorted(ck.unknown_calls.items(), key=lambda kv: -len(kv[1]))[:12]})
    if a.out:
        stack = ck.stack_reads()
        json.dump({'analysis': os.path.relpath(os.path.abspath(a.analysis), ROOT).replace('\\', '/'),
                   'findings': found, 'over': over,
                   'reads': {n: sorted(r) for n, r in R.items()}, 'raw_reads': {n: sorted(r) for n, r in raw.items()},
                   'stack_reads': {n: (None if s is None else sorted(s)) for n, s in stack.items()},
                   'outgoing_words': {n: ck.outgoing_words(n) for n in ck.F},
                   'rules': {n: {k: v for k, v in i.items() if k != 'saves'} for n, i in info.items() if i.get('not_saves')}},
                  open(a.out, 'w'), indent=0, sort_keys=True)
        print('->', a.out)
    return found


if __name__ == '__main__':
    main()
