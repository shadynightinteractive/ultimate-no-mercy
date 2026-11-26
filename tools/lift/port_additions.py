#!/usr/bin/env python3
"""The code the port added to the game's translated functions: found, checked, and carried into their conversions.

The translator (N64Recomp) writes each N64 instruction as a comment followed by its C. m2c converts from those instructions
only, so code the port added to the translated C (tools/readd_hand_edits.py in exe/WWFNoMercyRecomp: frame-interpolation and
ray-tracing hooks, a netplay null guard, a widescreen cull change) would be lost in a conversion. Until 2026-11-07 lift.py
refused every function whose translated text had anything it didn't recognise -- which also refused the translator's own helper
calls (do_break, do_lwl...), goto labels renumbered after a patch, and calls the port redirected to its own functions.

This file sorts that out (lift.py calls it from candidates()):

1. What is the translator's own (not an addition): an instruction's comment and code; a call line `name(rdram, ctx);` (to_asm.py
   gives m2c that call, so the conversion makes it too); a translator helper inside the code of the instruction it stands for
   (do_break for break, do_lwl for lwl...: HELPERS); the after_N / skip_N labels it numbers in order (a patch that adds a call
   renumbers all that follow, which is no change in what the code does).

2. Additions this pass can carry, each found by its place in the translated text (KINDS):
   - 'entry':   code before the first instruction (between `int c1cs = 0;` and the first instruction's comment). It runs on the
                registers and memory the function was called with. The converted function's adapter (the extern "C" way in from
                translated code, which reads the parameters out of the registers) runs the same code first, on the same
                registers, before it reads them: the same state, the same order. A `return;` in it returns from the adapter
                before anything else runs, as the original returns before its first instruction.
   - 'after call': a `{ ... }` block right after a call line: it runs when the call returns. The converted function calls a
                wrapper instead (lift_port_<function>_<jal address>): the same callee on the same registers, then the same block.
                The block may only read and write the call's result registers (v0, f0, when the callee always sets them) and the
                parameter registers the callee reads (the converted call puts those there): every other register holds something
                different in the converted function (its values are C variables), so a block reading one is refused.
   - 'guarded call': `if (condition) { name(rdram, ctx); }` in place of the call line. The wrapper makes the call only when the
                condition holds. The condition may only read the parameter registers the callee reads; and the converted code
                must not use the call's result (when the call is skipped, the original's v0 is whatever was there before).
   An addition may call port functions declared `extern` (in the block, or at the top of the translated file), and memcpy; nothing
   that keeps state of its own (`static`) or writes outside the game (printf...): the tester couldn't see that.

3. What is refused, with its reason: an addition anywhere else (a change in the middle of an instruction's code, like the
   widescreen cull's compare in func_800024F8: 'the port changed the code of one of its instructions'); one in a piece other
   than the entry of a function put together from pieces; one this pass can't check (registers it reads, calls it can't record).

The random-input tester compares the converted function against the port's translated version, additions and all (lift.py
originals_with_notes). The port functions an addition calls are recording stand-ins there (stub_definitions): each call notes
which function, and every argument (a matrix passed by pointer: its 16 values), in the same list as the calls to game
functions, and returns a result that depends only on how many calls came before. So a missing addition, one in the wrong place,
or one given a different value shows up as a different list of calls.
"""
import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, '..', '..', 'exe', 'WWFNoMercyRecomp', 'RecompiledFuncs')

# The translator's own helper calls, and the instructions they stand for. One is the translator's only inside the code of such an
# instruction (that instruction, or the one before it: a branch or jump's code comes after its delay slot's comment).
HELPERS = {'do_break': {'break'}, 'do_lwl': {'lwl'}, 'do_lwr': {'lwr'}, 'do_swl': {'swl'}, 'do_swr': {'swr'},
           'get_cop1_cs': {'cfc1'}, 'set_cop1_cs': {'ctc1'}, 'sqrtf': {'sqrt.s'},
           'switch_error': {'jr'}, 'pause_self': {'b', 'beq', 'j'}}
C_WORDS = {'if', 'for', 'while', 'switch', 'sizeof', 'return'}
INSTRUCTION = re.compile(r'^\s*// 0x([0-9A-F]{8}): (\S+)')
CALL_LINE = re.compile(r'^\s*(\w+)\(rdram, ctx\);\s*$')
LOOKUP_LINE = re.compile(r'^\s*LOOKUP_FUNC\(0x[0-9A-Fa-f]+\)\(rdram, ctx\);\s*$')
GUARD = re.compile(r'^\s*if \((.+)\) \{\s*$')
# Port functions that take a pointer to values, and how many 32-bit words to note through it: the matrix hooks hand over two 4x4
# float matrices. Any other pointer parameter (except rdram) is refused: the tester couldn't note what it points to.
POINTER_WORDS = 16
NAMED_POINTER = re.compile(r'^(?:const\s+)?(?:float|uint32_t|int32_t)\s*\*\s*(\w*16)$')
NO_STATE = re.compile(r'\b(static|printf|fprintf|puts|fputs|fwrite|exit|abort|malloc|free|goto)\b')
REGISTER_USE = re.compile(r'\bctx->(r\d+|f\d+)\b(?:\.\w+)?')
ARGUMENT_FIELDS = {'a0': 'r4', 'a1': 'r5', 'a2': 'r6', 'a3': 'r7', 'f12': 'f12', 'f14': 'f14'}


def file_declarations():
    """Every `extern` function declaration at the top level of the translated files (the port puts its hooks' there): name ->
    the declaration, one line, without the `extern`."""
    found = {}
    for path in sorted(os.listdir(SOURCE)):
        if path.endswith('.c'):
            for line in open(os.path.join(SOURCE, path), encoding='utf8', errors='replace'):
                m = re.match(r'^extern\s+(.+?\b(\w+)\s*\(.*\))\s*;', line)
                if m:
                    found[m.group(2)] = m.group(1)
    return found


DECLARATIONS = {}


def declaration_of(name, block_text):
    """A port function's declaration: from the addition's own `extern` line, or the file's."""
    m = re.search(r'\bextern\s+([^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*\))\s*;', block_text)
    if m:
        return ' '.join(m.group(1).split())
    if not DECLARATIONS:
        DECLARATIONS.update(file_declarations())
    return DECLARATIONS.get(name)


def instruction_before(lines, i, back=2):
    """The mnemonics of the last `back` instructions whose comments come before line i, nearest first, with their addresses."""
    found = []
    j = i - 1
    while j >= 0 and len(found) < back:
        m = INSTRUCTION.match(lines[j])
        if m:
            found.append((int(m.group(1), 16), m.group(2)))
        j -= 1
    return found


def block_end(lines, i):
    """The last line of the brace block that starts on line i (counting braces; the port's blocks put none in strings)."""
    depth = 0
    for j in range(i, len(lines)):
        text = re.sub(r'/\*.*?\*/', '', lines[j])
        depth += text.count('{') - text.count('}')
        if depth <= 0:
            return j
    return None


def comment_start(lines, i):
    """Where the block comment that ends on the line before i starts (the port writes one before each addition), or i."""
    j = i - 1
    if j < 0 or '*/' not in lines[j] or INSTRUCTION.match(lines[j]):
        return i
    while j >= 0 and '/*' not in lines[j]:
        j -= 1
    return j if j >= 0 else i


def find(name, text, callees):
    """The additions in one translated function's text. Returns (additions, the text without them, None) or (None, None, why).

    An addition is {'kind', 'lines' (its code, as written), 'callee', 'site' (the jal's address), 'condition'}. The text without
    them is what the translator wrote: a guarded call put back to the plain call line."""
    lines = text.splitlines()
    additions = []
    keep = [True] * len(lines)
    # (1) Before the first instruction: the header is the signature, `uint64_t hi = 0, lo = 0, result = 0;` and `int c1cs = 0;`.
    first = 3
    while first < len(lines) and not (INSTRUCTION.match(lines[first]) or re.match(r'^L_[0-9A-F]{8}:\s*$', lines[first])
                                      or lines[first].strip() == ';}'):
        first += 1
    if lines[1].strip() != 'uint64_t hi = 0, lo = 0, result = 0;' or lines[2].strip() != 'int c1cs = 0;':
        return None, None, 'the port changed its translated version'
    if first > 3:
        block = lines[3:first]
        # (Only a note there -- tools/fix_backbranches.py writes `/* ... bogus label removed */` -- is no code.)
        if re.sub(r'/\*.*?\*/', '', '\n'.join(block), flags=re.S).strip():
            additions.append({'kind': 'entry', 'lines': block})
        for k in range(3, first):
            keep[k] = False
    # (2) Around each call line.
    i = first
    while i < len(lines):
        m = CALL_LINE.match(lines[i])
        if not m or m.group(1) not in callees:
            i += 1
            continue
        callee = m.group(1)
        jal = [a for a, mn in instruction_before(lines, i) if mn == 'jal']
        site = jal[0] if jal else None
        if i >= 1 and i + 1 < len(lines) and GUARD.match(lines[i - 1]) and lines[i + 1].strip() == '}':
            start = comment_start(lines, i - 1)
            additions.append({'kind': 'guarded call', 'lines': lines[start:i + 2], 'callee': callee, 'site': site,
                              'condition': GUARD.match(lines[i - 1]).group(1)})
            for k in range(start, i + 2):
                keep[k] = False
            keep[i] = True          # (the call line itself is the translator's)
            i += 2
            continue
        if i + 1 < len(lines) and lines[i + 1].lstrip().startswith('{'):
            end = block_end(lines, i + 1)
            if end is None:
                return None, None, 'the port changed its translated version'
            additions.append({'kind': 'after call', 'lines': lines[i + 1:end + 1], 'callee': callee, 'site': site})
            for k in range(i + 1, end + 1):
                keep[k] = False
            i = end + 1
            continue
        i += 1
    rest = [l for l, k in zip(lines, keep) if k]
    # A line that is only a comment is no code: the port's repair scripts (tools/fix_*.py) leave one where they changed the
    # structure the translator wrote. What they changed is code, and is checked as any other code here; the note is dropped.
    joined = re.sub(r'(?m)^[ \t]*/\*.*?\*/[ \t]*\n', '', '\n'.join(rest) + '\n', flags=re.S)
    rest = joined.split('\n')[:-1]
    # (3) What's left must be the translator's alone: no comment of the port's, no call it didn't write.
    for j, l in enumerate(rest):
        if j < 3 or INSTRUCTION.match(l):      # (the signature and the two header lines, checked above)
            continue
        if '/*' in l or '//' in l:
            return None, None, 'the port changed the code of one of its instructions'
        if CALL_LINE.match(l) or LOOKUP_LINE.match(l):
            continue
        # (A declaration is never the translator's, past the two header lines -- except a jump table's index, which it declares
        # where it's set.)
        if (re.match(r'^\s*(?:extern|static|const|unsigned|signed|int|float|double|char|void|gpr|u?int\d+_t|[us]\d+)\b', l)
                and not re.match(r'^\s*gpr jr_addend_[0-9A-F]{8} = ctx->r\d+;\s*$', l)):
            return None, None, 'the port changed the code of one of its instructions'
        for c in re.findall(r'\b([a-z_]\w*)\s*\(', l):
            if c in C_WORDS:
                continue
            kinds = HELPERS.get(c)
            if not kinds or not ({mn for _, mn in instruction_before(rest, j)} & kinds):
                return None, None, 'the port changed the code of one of its instructions'
    return additions, '\n'.join(rest), None


def same_as_translated(current, stock):
    """Whether the text (additions taken out) is what the translator wrote, against the copy taken before the port's later
    changes: every stretch of changed lines has an instruction comment changed with it (a patched instruction: m2c reads the
    comment, so the conversion follows it), once the translator's numbering of its after_N / skip_N labels is set aside and
    call lines are left out (each is the call to_asm.py gives m2c for its jal: a call the port redirected to its own function,
    `rw_run_overlay(rdram, ctx);` where the translator had LOOKUP_FUNC(0x800D9960), is a call the conversion makes too)."""
    def lines(t):
        out = []
        for l in t.splitlines():
            if CALL_LINE.match(l) or LOOKUP_LINE.match(l):
                continue
            out.append(re.sub(r'\b(after|skip)_\d+\b', r'\1_N', l))
        return out
    a, b = lines(stock), lines(current)
    if a == b:
        return True
    import difflib
    hunks = [h for h in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes() if h[0] != 'equal']
    # (The current text's lines in a changed stretch: a changed instruction comment there means that instruction was patched.)
    changed_lines = {j for _, _, _, j1, j2 in hunks for j in range(j1, j2)}
    for tag, i1, i2, j1, j2 in hunks:
        if any(INSTRUCTION.match(l) for l in b[j1:j2]) or any(INSTRUCTION.match(l) for l in a[i1:i2]):
            continue        # (a patched instruction's comment came with it, or an instruction the patched code no longer has)
        if all(re.match(r'^L_[0-9A-F]{8}:\s*$', l) for l in a[i1:i2] + b[j1:j2]):
            continue        # (only labels: one nothing jumps to any more, after a patch. A goto that changed is a changed line.)
        # A stretch of code alone: fine when the instruction it belongs to (the comment before it) was itself changed. difflib
        # matches a patched `sdc1 $f20, 0xF8($sp)` comment and its `SD(..., 0XF8, ...)` code as two stretches, with the
        # unchanged CHECK_FR line between them (func_80126384, whose frame the port made bigger).
        j = j1 - 1
        while j >= 0 and not INSTRUCTION.match(b[j]):
            j -= 1
        if j < 0 or j not in changed_lines:
            return False
    return True


def check(addition, f, analysis, result_registers):
    """Why an addition can't be carried, or None. f is the (whole) function, for its calls."""
    text = '\n'.join(addition['lines'])
    code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    if NO_STATE.search(code):
        return 'the port added code that keeps state or writes outside the game'
    externs = set(re.findall(r'\b([a-z_]\w*)\s*\(', code)) - C_WORDS - {'memcpy', 'extern', addition.get('callee')}
    for c in externs:
        d = declaration_of(c, text)
        if not d or not stub_parts(d):
            return 'the port added a call this pass can\'t record'
    # (The tester counts each line that calls one as an instruction that has to run, with a note put at the start of the line:
    # so each such line has to start a statement. lift.py originals_with_notes.)
    previous = '{'
    for line in code.split('\n'):
        if externs and re.search(r'\b(?:%s)\s*\(' % '|'.join(map(re.escape, externs)), line) and not line.lstrip().startswith('extern'):
            if previous[-1:] not in (';', '{', '}'):
                return 'the port added a call this pass can\'t record'
        if line.strip():
            previous = line.strip()
    if addition['kind'] == 'entry':
        if re.search(r'\bctx->\w+(?:\.\w+)?\s*=[^=]', code):
            return 'the port added code that sets a register on the way in'
        return None
    callee = addition['callee']
    if addition['site'] is None or list(f['calls'].values()).count(callee) != 1:
        return 'the port added code at a call made more than once'
    use = set(analysis.get(callee, {}).get('use', []))
    arguments = {ARGUMENT_FIELDS[r] for r in use if r in ARGUMENT_FIELDS}
    if addition['kind'] == 'guarded call':
        read = set(REGISTER_USE.findall(addition['condition']))
        if read - arguments:
            return 'the port added a condition on a register the call is not given'
        return None
    results = {'v0': 'r2', 'f0': 'f0'}
    allowed = arguments | {results[r] for r in result_registers(callee) if r in results}
    if set(REGISTER_USE.findall(code)) - allowed:
        return 'the port added code after a call that uses a register the conversion does not have'
    # (And it may only change a result: the converted code reads the result from the registers after the call, and nothing
    # else -- a parameter register the port changed there would be seen by the original's later instructions only.)
    written = set(re.findall(r'\bctx->(r\d+|f\d+)\b(?:\.\w+)?\s*=[^=]', code))
    if written - {results[r] for r in result_registers(callee) if r in results}:
        return 'the port added code after a call that uses a register the conversion does not have'
    return None


def stub_parts(declaration):
    """A port function's declaration taken apart for its recording stand-in: (result type, name, [(type, name, words)]), where
    words is 0 for rdram (not noted: the same memory), 1 for a number, 16 for a matrix by pointer. None if it can't be noted."""
    m = re.match(r'^(.+?)\b(\w+)\s*\((.*)\)$', declaration.strip())
    if not m:
        return None
    result = ' '.join(m.group(1).split())
    if result not in ('void', 'uint32_t', 'int32_t', 'int', 'u32', 's32', 'double', 'float'):
        return None
    params = []
    inner = m.group(3).strip()
    for k, p in enumerate([] if inner in ('', 'void') else inner.split(',')):
        p = ' '.join(p.split())
        if re.match(r'^uint8_t\s*\*\s*\w*$', p):
            params.append((p, 'p%d' % k, 0))
        elif re.match(r'^(?:const\s+)?(?:uint32_t|int32_t|int|u32|s32|uint16_t|int16_t)(?:\s+\w+)?$', p):
            params.append((p, 'p%d' % k, 1))
        elif NAMED_POINTER.match(p):
            params.append((p, 'p%d' % k, POINTER_WORDS))
        else:
            return None
    return result, m.group(2), params


def stub_definitions(declarations):
    """The tester's recording stand-ins for the port functions additions call (fuzz_port_call in fuzz_main.cpp)."""
    out = []
    for k, (name, d) in enumerate(sorted(declarations.items())):
        result, _, params = stub_parts(d)
        typed = ', '.join('%s %s' % (strip_name(t), n) for t, n, _ in params) or 'void'
        body = ['    uint64_t words[%d];' % (1 + sum(w for _, _, w in params)), '    int count = 0;']
        for t, n, w in params:
            if w == 1:
                body.append('    words[count++] = (uint64_t)(uint32_t)%s;' % n)
            elif w:
                body.append('    for (int i = 0; i < %d; i++) { uint32_t bits; std::memcpy(&bits, (const char*)%s + 4 * i, 4); '
                            'words[count++] = bits; }' % (w, n))
        body.append('    uint32_t got = fuzz_port_call(%d, words, count);' % k)
        if result == 'void':
            body.append('    (void)got;')
        elif result in ('double', 'float'):
            body.append('    return (%s)((int32_t)(got %% 2001u) - 1000) / 16.0;' % result)
        else:
            body.append('    return (%s)got;' % result)
        out += ['extern "C" %s %s(%s) {' % (result, name, typed)] + body + ['}']
    return out


def strip_name(param_type):
    """A parameter's type without its name: `const float* aMatrix16` -> `const float*`."""
    t = param_type.strip()
    m = re.match(r'^(.*?[\s*])(\w+)$', t)
    if m and m.group(1).strip() and m.group(2) not in ('int', 'float', 'double', 'uint32_t', 'int32_t', 'u32', 's32', 'uint16_t', 'int16_t'):
        return m.group(1).strip()
    return t


def result_unused(cpp, callee):
    """Whether the converted code does nothing with the result of its (one) call to callee: the call is a statement of its own, or
    its result goes into a variable named nowhere else (m2c keeps a call in a temp_ret_N, unused, to hold its place in order --
    pin_pending_calls in its translate.py)."""
    call = r'lift_call<[^>]*>\(ctx, ::%s\)\(.*\);\s*$' % re.escape(callee)
    if re.search(r'^\s*' + call, cpp, re.M):
        return True
    m = re.search(r'^\s*(\w+) = ' + call, cpp, re.M)
    return bool(m) and len(re.findall(r'\b%s\b' % re.escape(m.group(1)), cpp)) == 2 and \
        re.search(r'^\s*\w+ %s;\s*$' % re.escape(m.group(1)), cpp, re.M) is not None


def carry(name, cpp, additions, functions_by_piece):
    """Puts the additions into a converted function. Returns (cpp, port record, None) or (None, None, why).

    The port record goes into candidates.json with the function: 'entry' (code the adapter runs first), 'wrappers' (definitions,
    after the memory macros), 'declarations' (extern "C" for the port functions, and the wrappers' forward declarations: before
    the converted code), 'externs' (name -> declaration, for the tester's stand-ins), 'kinds'."""
    record = {'entry': '', 'wrappers': [], 'declarations': [], 'externs': {}, 'kinds': []}
    for piece, adds in additions:
        for a in adds:
            text = '\n'.join(a['lines'])
            code = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
            for c in set(re.findall(r'\b([a-z_]\w*)\s*\(', code)) - C_WORDS - {'memcpy', 'extern'}:
                if c != a.get('callee'):
                    record['externs'][c] = declaration_of(c, text)
            record['kinds'].append(a['kind'])
            if a['kind'] == 'entry':
                record['entry'] += '    // [port addition] what the port runs at the start of its translated %s (before its first instruction):\n' % piece
                record['entry'] += text + '\n'
                continue
            callee = a['callee']
            wrapper = 'lift_port_%s_%08X' % (name, a['site'])
            uses = re.findall(r'lift_call<([^>]*)>\(ctx, ::%s\)' % re.escape(callee), cpp)
            if len(uses) != 1:
                return None, None, 'the port added code at a call the conversion makes %d times' % len(uses)
            if a['kind'] == 'guarded call' and not result_unused(cpp, callee):
                return None, None, "the port guarded a call whose result the conversion uses"
            cpp = cpp.replace('(ctx, ::%s)' % callee, '(ctx, ::%s)' % wrapper)
            if a['kind'] == 'guarded call':
                body = ['    if (%s) {' % a['condition'], '        ::%s(rdram, ctx);' % callee, '    }']
                why = 'only when %s (the port\'s guard)' % a['condition']
            else:
                body = ['    ::%s(rdram, ctx);' % callee] + a['lines']
                why = 'and then the port\'s code after it'
            record['declarations'].append('static void %s(uint8_t* rdram, recomp_context* ctx);' % wrapper)
            record['wrappers'].append('\n'.join(
                ['// [port addition] The call %s makes at 0x%08X: %s, %s, as in the port\'s translated version.' % (name, a['site'], callee, why),
                 'static void %s(uint8_t* rdram, recomp_context* ctx) {' % wrapper] + body + ['}']))
    record['declarations'] = ['extern "C" %s;' % d for _, d in sorted(record['externs'].items())] + record['declarations']
    return cpp, record, None
