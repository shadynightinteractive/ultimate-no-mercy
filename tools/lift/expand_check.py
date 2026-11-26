#!/usr/bin/env python3
"""The fields' unchanged check without a build (fields3, 2026-11-12; fields4 2026-11-13; into tools/lift by final082,
2026-11-13): every converted function's C++ with the field names (lift.py fielded_cpp, from fields.json), its macros
expanded back the way the preprocessor and the constants would -- WRESTLER(x, spirit) to M2C_FIELD(x, P<s16> , 0xAE),
GFX_AT(p, 1, w0) to M2C_FIELD(p, P<s32> , 8), GAME_VAR(match_rules) to *(P<s32> )0x800A9950 -- must be the very text it was
made from. check-names (lift.py check-names: the machine code compiled with names and without, byte for byte) is the proof;
this says where a difference would come from, and needs no compiler.

Why the text is enough: each macro is `M2C_FIELD(address, P<field::K::name_t>, field::K::name)` (or `*(P<..var::name_t> )
field::var::name`), name_t an alias of the type, name a constexpr of the offset (int) or address (u32); an index adds
`(index) * size`, folded at compile time. So the expansion, with the alias resolved and the constant folded, is the expression
the compiler sees -- PROVIDED the macro adds no call. (2026-11-13, fieldfix's finding: 0.81's GAME_TABLE called hi() / lo(),
constexpr functions; this check folded them as numbers and said "0 differ", while clang kept them as real calls until
inlining and two functions compiled to other bytes -- play_wrestler_sound, set_up_match_objects. lift.py made them
consteval, which the language folds at compile time without exception.) So now a macro whose expansion calls anything but
M2C_FIELD (P<...> is a type) is REFUSED -- every function using it counts as not the original -- unless the function it
calls is declared `consteval` in the preamble lift.py writes (read from that text, so a return to constexpr is refused).

lift.py is imported read only: every open() for writing in it, to_asm.py and modrep.py raises while this runs.

    python tools/lift/expand_check.py [--cands FILE] [--fields FILE] [--released FILE | --all] [--show N]
--cands     candidates.json (default: tools/lift's; copy it first if a pipeline run may be rewriting it)
--fields    fields.json (default: tools/lift's)
--released  the lifted.cpp whose wrappers say which functions are in the game (default: the game's); --all: every candidate
Exit 1 when any function's expansion isn't its original text.
EXPAND_SELFTEST=<what> breaks one thing on purpose, to see the check catch it: offset, via, rows, table, or constexpr (the
preamble's consteval read as constexpr: every GAME_TABLE user must then be refused).
"""
import argparse
import builtins
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def no_writes(module):
    """Every open() for writing in that module raises: this check must leave the project as it was."""
    def guarded(file, mode='r', *a, **k):
        if any(c in mode for c in 'wax+'):
            raise RuntimeError('expand_check: %s tried to write %s' % (module.__name__, file))
        return builtins.open(file, mode, *a, **k)
    module.open = guarded


ap = argparse.ArgumentParser()
ap.add_argument('--cands', default=os.path.join(HERE, 'candidates.json'))
ap.add_argument('--fields', default=os.path.join(HERE, 'fields.json'))
ap.add_argument('--released', default=None)
ap.add_argument('--all', action='store_true', help='every candidate, not only those in the released lifted.cpp')
ap.add_argument('--show', type=int, default=5)
a = ap.parse_args()
import lift, to_asm, modrep
for m in (lift, to_asm, modrep):
    no_writes(m)
c = json.load(builtins.open(a.cands))
if a.all:
    fns = sorted(n for n, v in c.items() if 'cpp' in v)
else:
    text = builtins.open(a.released or os.path.join(lift.GAME, 'lifted.cpp'), encoding='utf8').read()
    # (and the release's lifted_changed.inc beside it, which lifted.cpp includes: the functions the mod has a hand in, 2026-11-16)
    beside = os.path.join(os.path.dirname(a.released or os.path.join(lift.GAME, 'lifted.cpp')), 'lifted_changed.inc')
    if os.path.exists(beside):
        text += '\n' + builtins.open(beside, encoding='utf8').read()
    # (the wrappers: also the hooked ones' `__attribute__((noinline))`, as check-names reads them since 2026-11-13)
    fns = sorted(n for n in set(re.findall(r'^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram', text, re.M))
                 if 'cpp' in c.get(n, {}))
names = lift.with_copy_names(lift.read_names(), c)
lift.FIELDS_FILE = os.path.abspath(a.fields)
kinds, sources = lift.read_fields()
lift.field_param_kinds(c, names, kinds, sources)
preamble = lift.fields_preamble(kinds, sources)
SELFTEST = os.environ.get('EXPAND_SELFTEST', '')

# The macros, from the preamble lift.py writes (so a change there is checked too).
MACROS = {}
for line in preamble:
    m = re.match(r'#define (\w+)\(([^)]*)\) (.*)$', line)
    if m:
        MACROS[m.group(1)] = ([p.strip() for p in m.group(2).split(',')], m.group(3))

# The functions the preamble declares consteval (field::<ns>::<name>): the only calls a macro may make. (2026-11-13, final082)
CONSTEVAL = set()
ns = None
for line in preamble:
    m = re.match(r'namespace (\w+) \{', line)
    if m and m.group(1) != 'field':
        ns = m.group(1)
    f = re.match(r'\s*(consteval|constexpr) \w+ (\w+)\(', line)
    if f and ns and f.group(1) == 'consteval' and SELFTEST != 'constexpr':
        CONSTEVAL.add('field::%s::%s' % (ns, f.group(2)))


def calls_made(params, body):
    """The calls a macro's body makes of its own (not inside its arguments): every `name(` but M2C_FIELD and the parameters."""
    found = set()
    for m in re.finditer(r'((?:\w+::)*\w+)\s*\(', body):
        name = m.group(1)
        if name in ('M2C_FIELD',) or name in params:
            continue
        found.add(name)
    return found


REFUSED = {}     # macro -> the calls that make it refused
for mac, (params, body) in MACROS.items():
    bad = sorted(n for n in calls_made(params, body) if n not in CONSTEVAL)
    if bad:
        REFUSED[mac] = bad

CONST = {}
ns = None
for line in preamble:
    m = re.match(r'namespace (\w+) \{', line)
    if m and m.group(1) != 'field':
        ns = m.group(1)
    m = re.match(r'\s*constexpr (?:int|u32) (\w+) = (0x[0-9A-F]+); using \w+_t = (.+);$', line)
    # (fields4, 2026-11-13: a named array's start, `constexpr u32 wrestlers = 0x8017FEF0;  // Wrestler records`.)
    arr = re.match(r'constexpr u32 (\w+) = (0x[0-9A-F]+);  // \w+ records$', line)
    if arr:
        CONST['field::var::%s' % arr.group(1)] = int(arr.group(2), 16)
    if m:
        CONST['field::%s::%s' % (ns, m.group(1))] = int(m.group(2), 16)
        CONST['field::%s::%s_t' % (ns, m.group(1))] = m.group(3)


def number(v):
    """An offset as m2c writes one: decimal under 10, else hex; a minus sign in front."""
    s = '-' if v < 0 else ''
    v = abs(v)
    return s + ('%d' % v if v < 10 else '0x%X' % v)


def address_sum(e):
    """A sum of constants and (n) * 0xSIZE terms, as the compiler folds it (u32). (fields4, 2026-11-13)"""
    total = 0
    for term in e.split(' + '):
        m = re.fullmatch(r'\((-?\d+)\) \* (0x[0-9A-F]+)', term.strip())
        total += int(m.group(1)) * int(m.group(2), 16) if m else CONST[term.strip()]
    return total & 0xFFFFFFFF


def evaluate(expr):
    """A folded constant: field::K::name [+ (index) * 0xSIZE], or a field::var address."""
    e = expr.strip()
    # (GAME_TABLE's low half, field::var::lo(field::var::name), as lift.py's consteval lo() computes it; the _IN macros' sums
    #  inside hi() / lo(). fields4, 2026-11-13 -- folded here only because the call is refused above unless it is consteval.)
    t = re.fullmatch(r'field::var::lo\((.+)\)', e)
    if t:
        a = address_sum(t.group(1))
        return a - ((a + 0x8000) & 0xFFFF0000)
    # (the _VIA macros' offsets, field::K::name [+ (index) * 0xSIZE] - field::K::anchor, an int. fields4, 2026-11-13)
    if re.fullmatch(r'\d+', e):
        return int(e)            # (GAME_ARRAY's offset, a plain 0)
    w = re.fullmatch(r'(field::\w+::\w+)(?: \+ \((-?\d+)\) \* (0x[0-9A-F]+))? - (field::\w+::\w+)', e)
    if w:
        return CONST[w.group(1)] + (int(w.group(2)) * int(w.group(3), 16) if w.group(2) else 0) - CONST[w.group(4)]
    m = re.fullmatch(r'(field::\w+::\w+)(?: \+ \((-?\d+)\) \* (0x[0-9A-F]+))?', e)
    if not m:
        raise ValueError('not a constant: ' + e)
    return CONST[m.group(1)] + (int(m.group(2)) * int(m.group(3), 16) if m.group(2) else 0)


def expand(text):
    out, i = [], 0
    pat = re.compile(r'\b(%s)\(' % '|'.join(sorted(MACROS, key=len, reverse=True)))
    while True:
        m = pat.search(text, i)
        if not m:
            out.append(text[i:])
            return ''.join(out)
        out.append(text[i:m.start()])
        if m.group(1) in REFUSED:
            raise ValueError('%s calls %s in its expansion: not a constant the compiler must fold' % (m.group(1), ', '.join(REFUSED[m.group(1)])))
        end = lift.closing_paren(text, m.end() - 1)
        args = [expand(x) for x in lift.split_top(text[m.end():end], ', ')]
        params, body = MACROS[m.group(1)]
        if len(args) != len(params):
            raise ValueError('%s takes %d arguments: %r' % (m.group(1), len(params), args))
        values = {p: v.strip() for p, v in zip(params, args)}
        # (One pass, as the preprocessor: each parameter token replaced, `name##_t` pasted.)
        b = re.sub(r'(?<!\w)(%s)(?:##)?(?!\w)' % '|'.join(map(re.escape, params)), lambda x: values[x.group(1)], body)
        b = b.replace('##', '')
        # b is now M2C_FIELD(address, P<field::K::name_t>, <offset expr>) or *(P<..>)field::var::name: resolve and fold.
        f = re.fullmatch(r'M2C_FIELD\((.*), P<(.+?)>, (.+)\)', b, re.S)
        g = re.fullmatch(r'\*\(P<(.+?)> \)(field::var::\w+)', b)
        if f and lift.closing_paren(b, len('M2C_FIELD')) == len(b) - 1:
            parts = lift.split_top(b[len('M2C_FIELD('):-1], ', ')
            t = parts[1].strip()
            t = re.sub(r'field::\w+::\w+_t', lambda x: CONST[x.group(0)], t)
            p0 = parts[0]
            # (GAME_ARRAY's (field::var::name + bytes): the variable's address, as its u32 literal. fields4)
            p0 = re.sub(r'^\((field::var::\w+) \+ ', lambda x: '(0x%08X + ' % CONST[x.group(1)], p0)
            # (GAME_TABLE's high half, field::var::hi(field::var::name), as lift.py's consteval hi(). fields4)
            while 'field::var::hi(' in p0:
                h = p0.index('field::var::hi(')
                e = lift.closing_paren(p0, h + len('field::var::hi'))
                p0 = p0[:h] + '0x%08X' % ((address_sum(p0[h + len('field::var::hi('):e]) + 0x8000) & 0xFFFF0000) + p0[e + 1:]
            out.append('M2C_FIELD(%s, %s , %s)' % (p0, t, number(evaluate(parts[2]))))
        elif g:
            t = re.sub(r'field::\w+::\w+_t', lambda x: CONST[x.group(0)], g.group(1))
            out.append('*(P<%s> )0x%08X' % (t, CONST[g.group(2)]))
        else:
            raise ValueError('unexpected expansion ' + b)
        i = end + 1


if SELFTEST == 'via':
    CONST['field::MenuRow::flags'] += 2       # (an anchor wrong: the MENU_ROW_VIA from it must fail)
elif SELFTEST == 'rows':
    CONST['field::var::wrestlers'] += 2       # (the array's start wrong: every WRESTLER_IN must fail)
elif SELFTEST == 'table':
    CONST['field::var::match_slot_superstars'] += 0x10000   # (the high half wrong: GAME_TABLE must fail)
elif SELFTEST == 'offset':
    CONST['field::Gfx::w1'] += 8          # (a deliberate error: the check must see it)
    CONST['field::var::match_rules'] += 4
bad = 0
changed = 0
refused = 0
for fn in fns:
    cpp = c[fn]['cpp']
    named = lift.fielded_cpp(fn, cpp, names.get(fn), kinds, sources)
    if named == cpp:
        continue
    changed += 1
    try:
        back = expand(named)
    except Exception as e:
        back = 'ERROR %s' % e
        refused += 'in its expansion' in str(e)
    if back != cpp:
        bad += 1
        if bad <= a.show:
            # The first line that differs.
            for x, y in zip(back.split('\n'), cpp.split('\n')):
                if x != y:
                    print('%s:\n  expanded: %s\n  original: %s' % (fn, x.strip()[:200], y.strip()[:200]))
                    break
            else:
                print(fn, back[:200])
print('%d functions, %d with names, %d whose expansion is not the original text (%d of them for a call in a macro)' % (len(fns), changed, bad, refused))
print('macros checked: %d (%s ..); calls allowed: %s; macros refused: %s' % (
    len(MACROS), ', '.join(sorted(MACROS)[:6]), ', '.join(sorted(CONSTEVAL)) or 'none',
    ', '.join('%s (%s)' % (k, ', '.join(v)) for k, v in sorted(REFUSED.items())) or 'none'))
sys.exit(1 if bad else 0)
