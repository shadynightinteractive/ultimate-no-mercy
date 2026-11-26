#!/usr/bin/env python3
"""Which of a function's calls the mod's compiler inlined, call by call (part 1 of the Linux work, 2026-11-14).

prove_gen.py's closure() takes each callee of a candidate as a whole: one the original calls goes to the tester's stand-in at
every call, one it never calls is compiled in. That is wrong for a callee the compiler inlined at some calls and called at
others (arcade_frame: set_goal, set_phase, menu_choose, story_card_skip, custom_row_on, exh_rows_in, qa_bs_free; route:
menu_cam_put; operator_menu_frame: menu_action): the native code must run the callee's body where the original has it inline
and call the stand-in where the original calls. This module says which, from the compiler's own record:

  irg/<file>.ll     the file compiled with the mod build's flags and -g (gen_dwarf_ir.py; the same code as ir/<file>.ll --
                    same_code() checks it instruction by instruction). Every instruction left in a function carries its
                    source line and column and the chain of calls it was inlined through, so
                      a `call @H` left in F at (line, column), through a chain of inlined calls, is a call the original makes;
                      an instruction of H's body in F, inlined at (line, column), is H run inline there.
  irg/<file>.i      the file preprocessed as native.py preprocesses it, but with its line markers: the same tokens as
                    pp/<file>.i, each with its source line. A function's calls in it and in pp/<file>.native.cpp are the same
                    calls in the same order (checked), which carries each call's line over to the native text.

plan(base, F, calls) gives, for a function with such a callee, each function body the native code needs -- once per different
set of decisions (a "variant") -- and for each of its calls the name it must call: the stand-in's (the callee's own name) or
a variant's. A function without such a callee has no plan (None) and prove_gen.py works as before, byte for byte.

Nothing here is a rule of the tester: a wrong decision is a call the original doesn't make (or the reverse), which the tester
reports as a difference in calls.
"""
import collections, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
IRG = os.path.join(HERE, 'irg')
import native


class Unresolved(Exception):
    pass


# ---- the compiler's record ----
_MD = re.compile(r'^!(\d+) = (?:distinct )?!(\w+)\((.*)\)$', re.M)
_IR = {}


def _field(s, k):
    m = re.search(r'\b%s: (?:!(\d+)|"([^"]*)"|(\w+))' % k, s)
    if not m:
        return None
    return int(m.group(1)) if m.group(1) else (m.group(2) if m.group(2) is not None else m.group(3))


def _ir(base):
    if base not in _IR:
        text = open(os.path.join(IRG, base + '.ll'), encoding='utf8', errors='replace').read()
        md = {int(m.group(1)): (m.group(2), m.group(3)) for m in _MD.finditer(text)}
        bodies = {m.group(1): m.group(0) for m in re.finditer(r'^define [^@\n]*@([\w.]+)\(.*?^}', text, re.M | re.S)}
        _IR[base] = (md, bodies, {})
    return _IR[base]


def _scope_function(md, n, cache):
    start = n
    while n not in cache:
        kind, s = md[n]
        if kind == 'DISubprogram':
            cache[n] = _field(s, 'name')
            break
        nxt = _field(s, 'scope')
        if not isinstance(nxt, int):
            cache[n] = None
            break
        n = nxt
    cache[start] = cache[n]
    return cache[start]


def evidence(base, function, cache={}):
    """(calls, inlined, line0) for the compiled function:
    calls    {(callee, frames)}: a call instruction left in it; frames = ((function the call is written in, line, column),
             (the function that one was inlined into, line and column of that call), ..., (function, line, column))
    inlined  {(callee, frames)}: the callee's body run inline at frames (the same shape: frames[0] is the call inlined)
    line0    {(callee, frames without the first's line and column)}: a call with no line (two calls the compiler merged)"""
    key = (base, function)
    if key in cache:
        return cache[key]
    md, bodies, scopes = _ir(base)
    calls, inlined, line0 = set(), set(), set()
    body = bodies.get(function)
    if body is not None:
        chains = {}
        for line in body.splitlines():
            d = re.search(r'!dbg !(\d+)', line)
            if not d or md.get(int(d.group(1)), ('',))[0] != 'DILocation':
                continue
            n = int(d.group(1))
            if n not in chains:
                ch, q = [], n
                while q is not None:
                    s = md[q][1]
                    ch.append((_scope_function(md, _field(s, 'scope'), scopes), int(_field(s, 'line') or 0), int(_field(s, 'column') or 0)))
                    q = _field(s, 'inlinedAt')
                chains[n] = tuple(ch)
            ch = chains[n]
            for i in range(len(ch) - 1):
                inlined.add((ch[i][0], ch[i + 1:]))
            c = re.search(r'\bcall [^@\n]*@([\w.]+)\(', line)
            if c and not c.group(1).startswith('llvm.'):
                if ch[0][1] == 0:
                    line0.add((c.group(1), ((ch[0][0],),) + ch[1:]))
                else:
                    calls.add((c.group(1), ch))
    cache[key] = (calls, inlined, line0)
    return cache[key]


def same_code(base):
    """Whether irg/<file>.ll (with -g) is ir/<file>.ll's code: each function's instructions the same once the debug records
    are taken out. (lines differing, lines compared)"""
    def norm(path):
        out = []
        for l in open(path, encoding='utf8', errors='replace'):
            l = l.rstrip('\r\n')
            if (l.startswith(('!', '; ', 'source_filename', 'attributes #', 'declare ')) or 'call void @llvm.dbg.' in l or
                    l.lstrip().startswith('#dbg_') or not l.strip()):
                continue
            l = re.sub(r',? !dbg !\d+', '', l)
            l = re.sub(r',? !DIAssignID !\d+', '', l)
            l = re.sub(r',? !llvm\.loop !\d+', '', l)       # (a loop's record: -g adds one to two loops, no instruction changes)
            l = re.sub(r'(![\w.]+) !\d+', r'\1', l)        # (other records -- loops, types -- by kind, not number)
            l = re.sub(r' #\d+', '', l)                      # (attribute groups, numbered differently)
            out.append(l)
        return out
    a, b = norm(os.path.join(HERE, 'ir', base + '.ll')), norm(os.path.join(IRG, base + '.ll'))
    return sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b)), len(a)


# ---- the source's calls, with their lines ----
_LINES = {}


def _is_declaration(ts, k, start):
    """Whether ts[k], a function's name before '(', is a declaration inside a body (`extern int f(void);`), not a call."""
    j = k - 1
    while j > start and ts[j] not in (';', '{', '}'):
        j -= 1
    head = ts[j + 1:k]
    if not head:
        return False
    if head[0] == 'extern':
        return True
    # (a type, then the name: `int f(void);`, `NPtr<u8> f(int);` -- never an expression such as `i < f()`)
    return head[0] not in ('return', 'else', 'do', 'case', 'goto', 'sizeof') and re.fullmatch(
        r'(?:NPtr < )*[A-Za-z_]\w*(?: [A-Za-z_]\w*)*(?: >| >>)*(?: \*)*', ' '.join(head)) is not None


def function_sites(ts, first, last, defs):
    """Indexes (in ts) of the calls in the function ts[first..last] to functions the file defines, in order."""
    out = []
    k = first
    while k <= last and ts[k] != '{':
        k += 1
    start = k
    for k in range(start + 1, last):
        if ts[k + 1] == '(' and ts[k] in defs and ts[k - 1] not in ('.', '->') and not _is_declaration(ts, k, start):
            out.append(k)
    return out


def source_lines(base):
    """{function: [(callee, line)]} for the file's functions, from irg/<file>.i (the source's own lines)."""
    if base not in _LINES:
        toks, lines, cur, main = [], [], 1, True
        first_file = None
        for l in open(os.path.join(IRG, base + '.i'), encoding='utf8', errors='replace'):
            m = re.match(r'#\s*(?:line\s+)?(\d+)\s+"([^"]*)"', l)
            if m:
                cur = int(m.group(1))
                if first_file is None:
                    first_file = m.group(2)
                main = m.group(2) == first_file
                continue
            if l.startswith('#'):
                continue
            for t in native.TOKEN.finditer(l):
                if t.group('tok') is not None:
                    toks.append(['', t.group('tok')])
                    lines.append(cur if main else -1)
            cur += 1
        toks.append(['', ''])
        ts = [t for _, t in toks]
        its = native.split_items(toks)
        defs = {n for kind, n, a, b in its if kind == 'function'}
        out = {}
        for kind, n, a, b in its:
            if kind == 'function':
                out[n] = [(ts[k], lines[k]) for k in function_sites(ts, a, b, defs)]
        _LINES[base] = out
    return _LINES[base]


# ---- the plan ----
def mixed_callees(base, function, calls):
    """The callees the original both calls and has inline somewhere in the function."""
    ev_calls, ev_inl, _ = evidence(base, function)
    return {h for h, _ in ev_inl if h in calls} & ({h for h, _ in ev_calls} | set(calls))


def plan(base, function, calls, native_items, thru_names=()):
    """None if the function has no callee that is both called and inlined. Else {'names': the functions whose bodies the native
    code needs, in the order found, 'variants': {function: [(variant's name, {token offset in its item: name to call})]}}.
    native_items: (ts, [(kind, name, first, last)]) of pp/<file>.native.cpp. Raises Unresolved when the record doesn't decide."""
    if not mixed_callees(base, function, calls) and not any(mixed_callees(base, t, calls) for t in thru_names):
        return None
    ts, its = native_items
    defs = {n: (a, b) for kind, n, a, b in its if kind == 'function'}
    src = source_lines(base)
    sites = {}

    def sites_of(g):
        if g not in sites:
            a, b = defs[g]
            ks = function_sites(ts, a, b, defs)
            want = src.get(g)
            if want is None or [ts[k] for k in ks] != [h for h, _ in want]:
                raise Unresolved("%s: its calls in the native text are not the source's, one for one" % g)
            sites[g] = [(k - a, ts[k], line) for k, (_, line) in zip(ks, want)]
        return sites[g]

    variants = collections.OrderedDict()     # function -> [(name, renames)]
    keys = {}
    used_calls, used_inl = set(), set()

    def build(g, root, frames, stack):
        """The name of g's variant for the instance at `frames` in root (None: an instance with no instruction left)."""
        ev_calls, ev_inl, ev_line0 = evidence(base, root)
        mixed = mixed_callees(base, root, calls)
        by_line = collections.defaultdict(list)
        for off, h, line in sites_of(g):
            by_line[(h, line)].append(off)
        renames = {}
        for (h, line), offs in sorted(by_line.items(), key=lambda x: x[1][0]):
            if h == function:
                continue                 # (a call to the candidate itself: prove_gen's nm_self stub, as before)
            if h in calls and h not in mixed:
                continue                 # (called, never inline: the stand-in at every call, as before)
            cols_call = sorted({f[0][2] for c, f in ev_calls if c == h and frames is not None and f[1:] == frames and f[0][:2] == (g, line)})
            cols_inl = sorted({f[0][2] for c, f in ev_inl if c == h and frames is not None and f[1:] == frames and f[0][:2] == (g, line)})
            kinds = sorted([(c, 'call') for c in cols_call] + [(c, 'inline') for c in cols_inl])
            if len(kinds) == len(offs) and len({c for c, _ in kinds}) == len(kinds):
                decided = kinds
            elif not kinds:
                merged = frames is not None and (h, ((g,),) + frames) in ev_line0
                decided = [(None, 'call' if (merged and h in calls) or h in stack else 'gone')] * len(offs)
            elif len({k for _, k in kinds}) == 1 and len(kinds) == 1:
                decided = kinds * len(offs)     # (several calls at one place -- a macro's -- all the same way)
            elif h not in mixed and len({k for _, k in kinds}) == 1:
                decided = [(None, 'ambiguous')] * len(offs)
            else:
                raise Unresolved('%s line %d: %d calls to %s, the record has %s' % (g, line, len(offs), h, kinds))
            for off, (col, kind) in zip(offs, decided):
                if kind == 'call':
                    if h not in calls:
                        raise Unresolved('%s line %d: a call to %s the original is not known to make' % (g, line, h))
                    if col is not None:
                        used_calls.add((h, ((g, line, col),) + frames))
                    continue             # (the stand-in: the callee's own name)
                if h not in defs:
                    raise Unresolved('%s line %d: %s is inline but not defined in the file' % (g, line, h))
                if kind == 'inline':
                    child = ((g, line, col),) + frames
                    used_inl.add((h, child))
                    child_root = root
                elif kind == 'ambiguous':
                    child, child_root = 'ambiguous', root
                else:
                    child, child_root = None, root
                if h in thru_names:
                    child, child_root = (), h        # (run for real, a piece of its own: its own record)
                if frames == 'ambiguous' and h in mixed:
                    raise Unresolved('%s line %d: %s under an instance the record does not tell apart' % (g, line, h))
                renames[off] = build(h, child_root, None if frames == 'ambiguous' and child is not None and child != () else child, stack + (g,))
        key = (g, tuple(sorted(renames.items())))
        if key not in keys:
            vs = variants.setdefault(g, [])
            name = g if (not vs and g not in calls) or g == function else '%s__nmv%d' % (g, len(vs) + 1)
            vs.append((name, renames))
            keys[key] = name
        return keys[key]

    variants[function] = []
    build(function, function, (), ())
    roots = [function] + list(thru_names)
    for root in roots:
        ev_calls, ev_inl, _ = evidence(base, root)
        mixed = mixed_callees(base, root, calls)
        left = sorted(x for x in ev_calls if x[0] in mixed and x not in used_calls and root == function) + \
            sorted(x for x in ev_inl if x[0] in mixed and x not in used_inl and root == function)
        if left:
            raise Unresolved('the record has %d calls/inlinings of %s no call in the source was matched to: %s' % (
                len(left), sorted({x[0] for x in left}), left[:2]))
    return {'names': list(variants), 'variants': variants}


if __name__ == '__main__':
    # python inline_sites.py same            irg/ against ir/, file by file
    # python inline_sites.py <file> <function>   the record for one function
    if sys.argv[1] == 'same':
        for src in native.SOURCES:
            b = os.path.splitext(os.path.basename(src))[0]
            print('%-18s %d lines differ of %d' % ((b,) + same_code(b)))
    else:
        c, i, z = evidence(sys.argv[1], sys.argv[2])
        print(json.dumps({'calls': sorted(c), 'inlined': sorted(i), 'line0': sorted(z)}, indent=1)[:20000])
