#!/usr/bin/env python3
"""private_values.py (modnative7, 2026-11-14): for rule (1), PRIVATE VALUES (NM_RULE_WRITES=1; approved by the user 2026-11-14 in
its conservative form, OFF until an independent review -- REVIEW_PRIVATE_VALUES.txt; prove_gen.py rule_writes) --
the values the mod's own code can give one of its private scalars, read from the mod's IR (ir/<file>.ll, gen_dwarf_ir.py: the
build's own code, as the compiler optimized it).

A private variable (private_data.py: only the mod's C reaches it, by name, its address never held) gets its values from two
places only: its initial value (the .ll's global definition) and the stores to it. Each store's value is worked out where it
can be, inside the storing function:
    a constant                                     that value
    zext of an i1 (a comparison, a bool)            0 or 1
    select / phi of values worked out              their union
    and with a constant mask m >= 0                 0 .. m
    urem by a constant n > 0                        0 .. n-1
    zext / sext of a narrower value                 its type's range (or the value's, when known)
    a load of the same variable                     nothing new (it keeps a value it already had)
    anything else                                   unknown
One unknown store, or any use of the variable that is not a plain load or store of it (a GEP, a call, its address stored or
compared), and the variable is left alone: the tester gives it any value, as before. Only scalars (one field) of 1, 2 or 4
bytes are given values. Up to 64 values are a list; more become their range (lowest .. highest).

    python private_values.py [<name> ...]     print what each private scalar gets (all of them without names)
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
IR_DIR = os.path.join(HERE, 'ir')
LIFT = os.path.join(DEV, 'tools', 'lift')
MAX_LIST = 64
INT_BITS = {'i1': 1, 'i8': 8, 'i16': 16, 'i32': 32}

_FILES = None
_ROWS = None


def _rows():
    global _ROWS
    if _ROWS is None:
        _ROWS = {r['name']: r for r in json.load(open(os.path.join(LIFT, 'private_data.json')))}
    return _ROWS


def _files():
    """Each .ll (not the .front / .before_dae ones): its globals' definitions and its functions' bodies."""
    global _FILES
    if _FILES is None:
        _FILES = []
        for fn in sorted(os.listdir(IR_DIR)):
            if not fn.endswith('.ll') or fn.count('.') != 1:
                continue
            text = open(os.path.join(IR_DIR, fn), encoding='utf8', errors='replace').read()
            funcs = []
            for m in re.finditer(r'^define [^\n]*\{\n(.*?)^\}', text, re.M | re.S):
                funcs.append(m.group(1))
            outside = [l for l in re.sub(r'^define [^\n]*\{\n.*?^\}', '', text, flags=re.M | re.S).split('\n') if '@' in l]
            _FILES.append((fn, text, funcs, outside))
    return _FILES


_ASM = None


def _asm_text():
    """Every assembly source of the mod build (build/patches/*.S, src/*.S, recomp/*.S) and every C file's inline asm, as one text:
    a private variable named there may be written in ways the IR does not show."""
    global _ASM
    if _ASM is None:
        mod = MOD_DIR
        parts = []
        for sub in ('build/patches', 'src', 'recomp'):
            d = os.path.join(mod, sub)
            if os.path.isdir(d):
                for f in sorted(os.listdir(d)):
                    p = os.path.join(d, f)
                    if f.endswith(('.S', '.s')):
                        parts.append(open(p, encoding='utf8', errors='replace').read())
                    elif f.endswith(('.c', '.h')):
                        parts += _c_asm_statements(open(p, encoding='utf8', errors='replace').read())
        _ASM = '\n'.join(parts)
    return _ASM


def _c_asm_statements(text):
    """(F3 of the rule's review, 2026-11-14) Every inline asm statement of a C file -- asm, __asm, __asm__, with volatile /
    __volatile__ / goto / inline -- from its keyword to the parenthesis that closes it, however many ';' its text holds."""
    out = []
    for m in re.finditer(r'\b(?:asm|__asm|__asm__)\b(?:\s+(?:volatile|__volatile__|goto|inline))*\s*\(', text):
        depth, k = 0, m.end() - 1
        while k < len(text):
            if text[k] == '(':
                depth += 1
            elif text[k] == ')':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        out.append(text[m.start():k + 1])
    return out


_IR_ASM = None


def _ir_asm():
    """(F3) {IR file: (has an asm call with a memory clobber, the asm calls' text)}: an asm call that may write memory makes every
    variable defined in that module full range; one whose text names a variable makes that variable full range."""
    global _IR_ASM
    if _IR_ASM is None:
        _IR_ASM = {}
        for fn, text, funcs, outside in _files():
            calls = re.findall(r'\basm\b(?:\s+\w+)*\s+"[^\n]*', text)
            _IR_ASM[fn] = (any('~{memory}' in c for c in calls), '\n'.join(calls))
    return _IR_ASM


def _signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if bits > 1 and v >> (bits - 1) else v


class Unknown(Exception):
    pass


def _value(body, ty, v, var, seen):
    """The set (a Python set, or ('range', lo, hi)) of values v can have, of type ty, in the function body; Unknown if not."""
    v = v.strip()
    if re.fullmatch(r'-?\d+', v):
        return {int(v)}
    if v in ('true', 'false'):
        return {1 if v == 'true' else 0}
    if v in ('null', 'zeroinitializer'):
        return {0}
    if not v.startswith('%'):
        raise Unknown(v)
    if v in seen:
        return set()        # (a loop through phis: what the other ways in give)
    seen = seen | {v}
    m = re.search(r'^\s*%s = (.*)$' % re.escape(v), body, re.M)
    if not m:
        raise Unknown(v)
    d = m.group(1)
    if re.match(r'load (i\d+), ptr @%s\b' % re.escape(var), d):
        return set()        # (the variable's own value again)
    m2 = re.match(r'zext i1 (\S+) to', d)
    if m2:
        return {0, 1}
    m2 = re.match(r'(zext|sext) (i\d+) (\S+) to (i\d+)', d)
    if m2:
        bits = INT_BITS.get(m2.group(2))
        if bits is None:
            raise Unknown(d)
        try:
            inner = _value(body, m2.group(2), m2.group(3), var, seen)
        except Unknown:
            return ('range', 0, (1 << bits) - 1) if m2.group(1) == 'zext' else ('range', -(1 << (bits - 1)), (1 << (bits - 1)) - 1)
        if m2.group(1) == 'zext':
            # (F2 of the rule's review, 2026-11-14: the IR prints a narrow constant signed -- i8 -1 -- and zext makes it 255: each
            #  inner value taken modulo 2^bits; an inner range that reaches below 0 becomes the whole unsigned width)
            if isinstance(inner, tuple):
                return inner if inner[1] >= 0 and inner[2] < (1 << bits) else ('range', 0, (1 << bits) - 1)
            return {x % (1 << bits) for x in inner}
        return inner
    m2 = re.match(r'select (?:\w+ )*i1 [^,]+, (i\d+) ([^,]+), (i\d+) ([^,]+?)(?:, !.*)?$', d)
    if m2:
        return _union(_value(body, m2.group(1), m2.group(2), var, seen), _value(body, m2.group(3), m2.group(4), var, seen))
    m2 = re.match(r'phi (i\d+) (.*)$', d)
    if m2:
        out = set()
        for x in re.findall(r'\[\s*([^,\]]+),', m2.group(2)):
            out = _union(out, _value(body, m2.group(1), x, var, seen))
        return out
    m2 = re.match(r'and (i\d+) (\S+), (-?\d+)', d)
    if m2 and int(m2.group(3)) >= 0:
        return ('range', 0, int(m2.group(3)))
    m2 = re.match(r'urem (i\d+) (\S+), (\d+)', d)
    if m2 and int(m2.group(3)) > 0:
        return ('range', 0, int(m2.group(3)) - 1)
    raise Unknown(d)


def _union(a, b):
    if isinstance(a, set) and isinstance(b, set):
        return a | b
    lo = min(_bounds(a)[0], _bounds(b)[0])
    hi = max(_bounds(a)[1], _bounds(b)[1])
    return ('range', lo, hi)


def _bounds(s):
    if isinstance(s, tuple):
        return s[1], s[2]
    return (min(s), max(s)) if s else (1 << 62, -(1 << 62))


def values_of(name):
    """{'list': [...]} or {'range': [lo, hi]} -- the values the mod can give the private scalar `name` -- or None (unknown:
    the tester gives it any value)."""
    r = _rows().get(name)
    if r is None or not r.get('private') or not r.get('widths') or len(r['widths']) != 1 or r['widths'][0] not in (1, 2, 4):
        return None
    var = name     # (a function's own static is <function>.<name> in both mod.map and the IR)
    width = r['widths'][0]
    if re.search(r'\b%s\b' % re.escape(name.split('.')[-1]), _asm_text()):
        return None    # (named in the mod's assembly -- its patches of the game's functions: writes the IR doesn't show)
    for fn, text, funcs, outside in _files():
        clobbers, asm = _ir_asm()[fn]
        if re.search(r'^@%s = ' % re.escape(name), text, re.M) and clobbers:
            return None    # (F3: its module has an asm call that may write memory)
        if re.search(r'\b%s\b' % re.escape(name.split('.')[-1]), asm):
            return None    # (F3: an asm call's text names it)
    found = None
    vals = set()
    pat_global = re.compile(r'^@%s = (?:[\w()]+ )*?(?:global|constant) (i\d+) ([^,\n]+)' % re.escape(var), re.M)
    use = re.compile(r'@%s\b' % re.escape(var))
    try:
        for fn, text, funcs, outside in _files():
            g = pat_global.search(text)
            if g:
                if found is not None:
                    return None     # (two definitions of one name: not sure which is which)
                found = g.group(1)
                if INT_BITS.get(found) is None or (INT_BITS[found] + 7) // 8 > width:
                    return None
                vals = _union(vals, _value('', found, g.group(2).strip(), var, set()))
            for line in outside:
                if use.search(line) and not pat_global.match(line):
                    raise Unknown(line.strip())     # (its address in another global's initial value)
            for body in funcs:
                for line in body.split('\n'):
                    if not use.search(line):
                        continue
                    if re.match(r'\s*%%\S+ = load (?:volatile )?(i\d+), ptr @%s, ' % re.escape(var), line + ', '):
                        continue
                    m = re.match(r'\s*store (?:volatile )?(i\d+) ([^,]+), ptr @%s(?:,|$)' % re.escape(var), line)
                    if not m:
                        raise Unknown(line.strip())      # (any other use: its address goes somewhere)
                    vals = _union(vals, _value(body, m.group(1), m.group(2), var, set()))
    except Unknown:
        return None
    if found is None:
        return None
    bits = 8 * width
    if isinstance(vals, tuple):
        lo, hi = vals[1], vals[2]
        if hi - lo + 1 >= (1 << bits):
            return None
        return {'range': [lo, hi]}
    if not vals:
        return None
    vals = sorted({_signed(v, bits) if v < 0 or v >> (bits - 1) else v for v in vals})
    if len(vals) > MAX_LIST:
        return {'range': [vals[0], vals[-1]]}
    if len(vals) == 1 and name.split('.')[-1].startswith('dbg_'):
        # (F7 of the rule's review, 2026-11-14: a dbg_ variable is a test knob the mod declares to be set from outside -- "tests:"
        #  -- though no shipped code does; one bounded to a single value gets the full range, so a test tool that pokes it
        #  can't make a run differ from what was proved)
        return None
    return {'list': vals}


if __name__ == '__main__':
    names = sys.argv[1:] or sorted(n for n, r in _rows().items() if r.get('private') and r.get('widths') and len(r['widths']) == 1)
    known = 0
    for n in names:
        v = values_of(n)
        known += v is not None
        print('%-40s %s' % (n, v))
    print('%d of %d with values' % (known, len(names)))
