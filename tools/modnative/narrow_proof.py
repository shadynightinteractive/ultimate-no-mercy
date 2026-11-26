#!/usr/bin/env python3
"""narrow_proof.py (modnative7, 2026-11-14): the proofs the STRICT narrow rules need (approved by the user 2026-11-14):

  narrow ARGUMENTS  a stand-in compares only the low 8 / 16 bits of a callee's narrow argument ONLY where the callee is proved,
                    from its own compiled code, to ignore the bits above them -- callee_ignores_high(callee, register, width)
  narrow RESULTS    a stand-in's narrow result is taken by its low bits only ONLY where the caller is proved, from its own
                    compiled code, to ignore the bits above them after every call to that callee -- caller_ignores_high(...)
Otherwise every bit is compared, as for any value.

The code read is the MIPS each function was compiled to, as the translations list it instruction by instruction (each
instruction's "// 0x<address>: <instruction>" line): Stage 1's translation of the mod (w/mod_aot) and N64Recomp's of the game
(exe/WWFNoMercyRecomp/RecompiledFuncs). From the point the value arrives (the callee's entry for an argument, the instruction
after a call's delay slot for a result), every path through the function is followed while the register still holds that value,
and each instruction that reads it must be one that uses only its low w bits:
    andi  rd, R, m        with m < 2^w                      (the result holds only bits of R below w)
    sll   rd, R, s        with s >= 32 - w                  (bits of R at w and above are shifted out)
    sb    R, off(base)    R the value stored, w >= 8          (8 bits stored)
    sh    R, off(base)    R the value stored, w >= 16         (16 bits stored)
Any other read -- an add, a compare, a branch on it, a store of a word, use as an address, a call while it is live (the callee
may read it), a return while a result is live (the caller's caller gets it), a jump the function's code doesn't contain (a tail
call, a jump table) -- and the proof fails: every bit is compared. An instruction that writes R without reading it ends that
path (the value is gone); a call ends it too for a0-a3 / v0 / v1 (the callee may change them). A branch's delay slot is taken
to run on both ways out of it (so a "likely" branch's is over-approximated: more paths, never fewer). An instruction this file
doesn't know, naming R anywhere, fails the proof. So the answer is "proved" only when no path can carry a high bit of the value
anywhere -- written so it can be reviewed on its own (selftest() at the end has the cases).

    python narrow_proof.py selftest
    python narrow_proof.py arg <function> <register> <width>       e.g.  arg modfn_810D0AD4 a0 16
    python narrow_proof.py result <caller A> <callee symbol> <width>
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
AOT = os.environ.get('NM_AOT_DIR') or os.path.join(HERE, 'w', 'mod_aot')
GAME = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp', 'RecompiledFuncs')
TAG = 'nomercy_arcade'

REG = {'zero': 0, 'at': 1, 'v0': 2, 'v1': 3, 'a0': 4, 'a1': 5, 'a2': 6, 'a3': 7, 't0': 8, 't1': 9, 't2': 10, 't3': 11, 't4': 12,
       't5': 13, 't6': 14, 't7': 15, 's0': 16, 's1': 17, 's2': 18, 's3': 19, 's4': 20, 's5': 21, 's6': 22, 's7': 23, 't8': 24,
       't9': 25, 'k0': 26, 'k1': 27, 'gp': 28, 'sp': 29, 'fp': 30, 's8': 30, 'ra': 31}
CALLER_SAVED = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 1, 31}
STORES = {'sb': 8, 'sh': 16, 'sw': None, 'swl': None, 'swr': None, 'sd': None, 'sc': None}
LOADS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'll', 'ld', 'lwu'}
LOADS_MERGE = {'lwl', 'lwr'}                       # (read their destination too)
FPU_MEM = {'lwc1', 'ldc1', 'swc1', 'sdc1'}          # (an FPR and a GPR base)
ALU = {'addu', 'addiu', 'addi', 'add', 'subu', 'sub', 'and', 'andi', 'or', 'ori', 'xor', 'xori', 'nor', 'slt', 'sltu', 'slti',
       'sltiu', 'lui', 'sll', 'srl', 'sra', 'sllv', 'srlv', 'srav', 'mfhi', 'mflo', 'mfc1', 'cfc1', 'move', 'negu', 'neg',
       'not', 'li', 'la'}
READS_DEST = {'movn', 'movz'}                       # (rd kept when the test fails: a read of rd)
MULDIV = {'mult', 'multu', 'div', 'divu', 'mthi', 'mtlo', 'mtc1', 'ctc1', 'dmult', 'dmultu', 'ddiv', 'ddivu', 'teq', 'tne'}
BRANCH = {'beq', 'bne', 'blez', 'bgtz', 'bltz', 'bgez', 'beqz', 'bnez', 'beql', 'bnel', 'blezl', 'bgtzl', 'bltzl', 'bgezl', 'b',
          'bc1t', 'bc1f', 'bc1tl', 'bc1fl'}
CALL_BRANCH = {'bal', 'bltzal', 'bgezal', 'bltzall', 'bgezall'}


def _regs(text):
    return [REG[r] for r in re.findall(r'\$(\w+)', text) if r in REG]


def _imm(text):
    text = text.strip()
    if re.fullmatch(r'L_[0-9A-Fa-f]{8}', text):
        return int(text[2:], 16)      # (a branch target as the translations write it)
    m = re.fullmatch(r'(-?)0x([0-9A-Fa-f]+)', text) or re.fullmatch(r'(-?)(\d+)', text)
    if not m:
        return None
    v = int(m.group(2), 16 if 'x' in text.lower() else 10)
    return -v if m.group(1) else v


_INDEX = None


def _index():
    """name -> [(address, mnemonic, [operand texts])] in address order, one per address."""
    global _INDEX
    if _INDEX is not None:
        return _INDEX
    _INDEX = {}
    paths = [os.path.join(AOT, f) for f in sorted(os.listdir(AOT)) if f.endswith('.c')] if os.path.isdir(AOT) else []
    paths += [os.path.join(GAME, f) for f in sorted(os.listdir(GAME)) if f.endswith('.c')] if os.path.isdir(GAME) else []
    for p in paths:
        name, seen, ins = None, None, None
        for line in open(p, encoding='utf8', errors='replace'):
            m = re.match(r'RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{', line)
            if m:
                name, seen, ins = m.group(1), set(), []
                _INDEX[name] = ins
                continue
            if name is None:
                continue
            m = re.match(r'\s*// 0x([0-9A-F]{8}): (\w[\w.]*)\s*(.*)$', line)
            if m:
                a = int(m.group(1), 16)
                if a not in seen:
                    seen.add(a)
                    ins.append((a, m.group(2), [o.strip() for o in m.group(3).split(',')] if m.group(3).strip() else []))
    for ins in _INDEX.values():
        ins.sort()
    return _INDEX


def code_of(symbol):
    """The instructions of the function a stand-in symbol stands for, or None if they are not to be had."""
    idx = _index()
    if symbol.startswith('modfn_'):
        return idx.get('nm_aot_%s_%s' % (TAG, symbol[len('modfn_'):]))
    if re.fullmatch(r'nm_aot_\w+', symbol):
        return idx.get(symbol)
    return idx.get(symbol)      # (a game function by its RecompiledFuncs name; anything else: None)


def _ends_value(code, start, reg, width, at_return_fails):
    """True when, from instruction index `start` with `reg` holding the value, no path reads a bit of it at or above `width`
    (see the module's text). False at the first read that may."""
    limit = 1 << width
    where = {a: k for k, (a, _, _) in enumerate(code)}
    todo, done = [(start, False)], set()       # (index, in a delay slot whose branch's ways out are pending)
    pending = {}                                # (delay slot index -> its successors)
    while todo:
        k, _ = todo.pop()
        if k in done:
            continue
        done.add(k)
        if k >= len(code):
            return False                        # (runs off the function's end: not followed)
        a, op, ops = code[k]
        regs = _regs(' '.join(ops))
        nxt = [k + 1]
        killed = False
        # -- what it reads of reg, and whether it then holds the value no longer
        if op == 'nop':
            pass
        elif op in STORES:
            if len(ops) < 2:
                return False
            value, base = _regs(ops[0]), _regs(ops[1])
            if reg in base:
                return False
            if reg in value and (STORES[op] is None or width < STORES[op]):
                return False
        elif op in LOADS or op in LOADS_MERGE:
            dest, base = _regs(ops[0]), _regs(ops[1]) if len(ops) > 1 else []
            if reg in base or (op in LOADS_MERGE and reg in dest):
                return False
            killed = reg in dest
        elif op in FPU_MEM:
            if reg in (_regs(ops[1]) if len(ops) > 1 else regs):
                return False
        elif op == 'andi':
            d, s = _regs(ops[0]), _regs(ops[1])
            m = _imm(ops[2]) if len(ops) > 2 else None
            if reg in s and (m is None or m < 0 or m >= limit):
                return False
            killed = reg in d
        elif op == 'sll':
            d, s = _regs(ops[0]), _regs(ops[1])
            sa = _imm(ops[2]) if len(ops) > 2 else None
            if reg in s and (sa is None or sa < 32 - width):
                return False
            killed = reg in d
        elif op in ALU:
            d, s = _regs(ops[0]) if ops else [], _regs(' '.join(ops[1:]))
            if reg in s:
                return False
            killed = reg in d
        elif op in READS_DEST or op in MULDIV:
            if reg in regs:
                return False
        elif op in BRANCH:
            if reg in regs:
                return False
            target = _imm(ops[-1]) if ops else None
            if target is None or target not in where:
                return False                    # (a branch out of the function while it is live)
            pending[k + 1] = [where[target], k + 2]
        elif op == 'j':
            target = _imm(ops[0]) if ops else None
            if target is None or target not in where:
                return False                    # (a tail call: the next function may read it)
            pending[k + 1] = [where[target]]
        elif op in ('jal', 'jalr') or op in CALL_BRANCH:
            if reg in regs:
                return False
            pending[k + 1] = ['call']           # (after the delay slot: the call, which may read it)
        elif op == 'jr':
            if reg in regs:
                return False
            if regs == [31]:
                pending[k + 1] = ['return']
            else:
                return False                    # (a jump table or a tail call through a register)
        elif any(r == reg for r in regs):
            return False                        # (an instruction this file doesn't know, naming it)
        elif op in ('syscall', 'break', 'eret', 'sync', 'cache'):
            return False
        # -- where it goes from here
        if k in pending:
            nxt = []
            for s in pending[k]:
                if s == 'call':
                    if not killed:
                        return False            # (the callee may read it)
                elif s == 'return':
                    if not killed and at_return_fails:
                        return False            # (a result still live at the return: the caller's caller gets it)
                elif not killed:
                    nxt.append(s)
        elif killed:
            nxt = []
        for s in nxt:
            todo.append((s, False))
    return True


def callee_ignores_high(symbol, register, width):
    """True when the function `symbol` is proved from its own code to ignore the bits of argument `register` (4..7) at or
    above `width` (8 or 16)."""
    code = code_of(symbol)
    if not code:
        return False
    return _ends_value(code, 0, register, width, at_return_fails=False)


def caller_ignores_high(caller, callee_address, width):
    """True when the caller (Stage 1's name, or a stand-in symbol) ignores the bits of v0 at or above `width` after EVERY call it
    makes to callee_address (and makes at least one)."""
    code = code_of(caller)
    if not code:
        return False
    sites = [k for k, (_, op, ops) in enumerate(code) if op == 'jal' and ops and _imm(ops[0]) == callee_address]
    if not sites:
        return False
    return all(_ends_value(code, k + 2, 2, width, at_return_fails=True) for k in sites)


def selftest():
    def run(lines, reg, width, ret_fails=False):
        code = []
        for k, l in enumerate(lines):
            op, _, rest = l.partition(' ')
            code.append((0x1000 + 4 * k, op, [o.strip() for o in rest.split(',')] if rest.strip() else []))
        return _ends_value(code, 0, REG[reg], width, ret_fails)
    cases = [
        (['andi $v0, $a0, 0xFF', 'jr $ra', 'nop'], 'a0', 8, True),
        (['andi $v0, $a0, 0x1FF', 'jr $ra', 'nop'], 'a0', 8, False),
        (['sltiu $v0, $a0, 0x10', 'jr $ra', 'nop'], 'a0', 8, False),
        (['sb $a0, 0x0($a1)', 'jr $ra', 'nop'], 'a0', 8, True),
        (['sh $a0, 0x0($a1)', 'jr $ra', 'nop'], 'a0', 8, False),
        (['sh $a0, 0x0($a1)', 'jr $ra', 'nop'], 'a0', 16, True),
        (['sb $a1, 0x0($a0)', 'jr $ra', 'nop'], 'a0', 8, False),
        (['sll $t0, $a0, 24', 'sra $v0, $t0, 24', 'jr $ra', 'nop'], 'a0', 8, True),
        (['sll $t0, $a0, 16', 'jr $ra', 'nop'], 'a0', 8, False),
        (['jal 0x2000', 'nop', 'andi $v0, $a0, 0xFF'], 'a0', 8, False),     # (a call while live)
        (['addiu $a0, $zero, 0x1', 'jal 0x2000', 'nop'], 'a0', 8, True),     # (overwritten first)
        (['beq $a0, $zero, 0x100C', 'nop', 'andi $v0, $a0, 0xFF', 'jr $ra', 'nop'], 'a0', 8, False),   # (a branch on it)
        (['beq $a1, $zero, 0x100C', 'andi $a0, $a0, 0xFF', 'addu $v0, $a0, $a0', 'jr $ra', 'nop'], 'a0', 8, True),
        (['beq $a1, $zero, 0x100C', 'nop', 'andi $a0, $a0, 0xFF', 'addu $v0, $a0, $a0', 'jr $ra', 'nop'], 'a0', 8, False),
        (['andi $v0, $v0, 0xFFFF', 'jr $ra', 'nop'], 'v0', 16, True),
        (['jr $ra', 'nop'], 'v0', 16, False),                                 # (a result returned as it is)
        (['jr $ra', 'nop'], 'a0', 8, True),                                   # (an argument never read)
        (['j 0x5000', 'nop'], 'a0', 8, False),                                # (a tail call)
        (['mult $a0, $a1', 'jr $ra', 'nop'], 'a0', 8, False),
    ]
    bad = 0
    for lines, reg, w, want in cases:
        ret_fails = reg == 'v0'
        got = run(lines, reg, w, ret_fails)
        if got != want:
            bad += 1
            print('FAIL', lines, reg, w, 'got', got)
    print('selftest: %d of %d right' % (len(cases) - bad, len(cases)))
    return bad == 0


if __name__ == '__main__':
    if sys.argv[1] == 'selftest':
        sys.exit(0 if selftest() else 1)
    if sys.argv[1] == 'arg':
        print(callee_ignores_high(sys.argv[2], REG[sys.argv[3]], int(sys.argv[4])))
    elif sys.argv[1] == 'result':
        print(caller_ignores_high('nm_aot_%s_%s' % (TAG, sys.argv[2]), int(re.search(r'[0-9A-F]{8}', sys.argv[3]).group(0), 16), int(sys.argv[4])))
