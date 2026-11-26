#!/usr/bin/env python3
"""The mod's C, compiled for the PC (scratch/agents/modnative1, 2026-11-12): step 1 of making the mod native.

    python native.py census      preprocess the mod's 25 sources as the mod build does, rewrite every pointer type into
                                 nm_native.h's NPtr<T> (so every access to game memory goes through lift_mem.h), compile each
                                 file as C++ for the PC, and say which of the mod's functions compile and why the others don't
                                 (census.json, census.txt)

The rewrite works on the PREPROCESSED source (clang -E with the mod build's own defines), so arcade.h's RD8 / RD16 / RD32
(`*(volatile u32*)(a)`) and every other macro become NPtr accesses with no change to the mod's source. Nothing under mod\\ is
written: the outputs are in pp\\ here.

What the rewrite does, token by token (rewrite_pointers):
  - `T *` / `T*` where T is a type (a built-in, a typedef of the file, or struct/union/enum X), with any const / volatile, and
    any number of stars: NPtr<...NPtr<T>...>. That covers declarations, parameters, return types, casts and sizeof.
  - a declaration of several names with a pointer among them (`u8 *a, b;`) is split at its commas, so each name keeps its own
    type (in C the star binds to the name, not the type).
  - nothing else: a function pointer `T (*f)(...)` is left as it is (counted, not rewritten).

Added 2026-11-13 (scratch/agents/modnative2; modnative1's plan, steps A and B), each a pass of census() in this order:
  name_anonymous_structs  `static struct { ... } x[N];` gets a type name (NMAnon_x), so x can be a view like any global
  array_parameters        `u16 pr[2]` as a parameter of one of the mod's functions is the pointer C makes it: NPtr<u16> pr
  bind_globals            (modnative1's) now also multi-dimensional arrays: NArr<T[N]>, rows that are array views themselves
  bind_local_statics      a function's own static: a view at its mod.map place (`f.x`), or the tester's PC object if private
  frame_locals            a local whose address is handed on: a view of its place in the N64 frame, at the original's offset
  string_views            a string literal: a view of the same bytes in the mod's read-only image, found by content
  rewrite_sizeof          sizeof of an expression: the N64 object's size, not the view's
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
MOD = MOD_DIR   # (modnative5: this folder's copy of the mod, built by build_mod_copy.ps1)
PP = os.path.join(HERE, 'pp')
CLANG = os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'clang.exe')
CLANGXX = os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang++.exe')
GAME_SRC = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp', 'src', 'game')
# The mod build's sources and defines (build_mod.ps1, the six-man build that ships).
SOURCES = ['src/draw.c', 'src/logic.c', 'src/gameplay.c', 'src/bigshow.c', 'src/media.c', 'src/commentary.c', 'src/roster.c',
           'src/variants.c', 'src/files.c', 'src/parts.c', 'src/skel.c', 'src/hd.c', 'src/hands.c', 'src/movex.c', 'src/announce.c',
           'src/arcctl.c', 'src/story.c', 'src/save.c', 'src/util.c', 'src/tron.c', 'src/arena.c', 'src/arena_ringstage.c',
           'src/arena_entrance.c', 'src/arena_ring.c', 'recomp/hooks_recomp.c']
DEFINES = ['-DNM_SIX', '-DARCADE_STORY', '-DARCADE_MOVEX']
# For the PC compile: C's words C++ lacks.
CXX_FLAGS = ['-std=c++20', '-fsyntax-only', '-ferror-limit=0', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-fsigned-char',
             '-Wno-everything', '-DARCADE_LIFT_FUZZ', '-Drestrict=__restrict', '-D_Bool=bool', '-fno-caret-diagnostics',
             '-fno-diagnostics-fixit-info', '-I', GAME_SRC, '-I', os.path.join(DEV, 'tools', 'lift', 'fuzz'), '-include', 'nm_native.h']
# (2026-11-14, modnative7: nm_native.h from the tester's own folder -- the header every candidate is proved with -- not a copy
#  beside this script, which tools/modnative does not have)

TOKEN = re.compile(r'''(?P<ws>\s+)|(?P<tok>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uUlL]*|'''
                   r'''(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?[fFuUlL]*|->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\.\.\.|\S)''')
BUILTIN = {'void', 'char', 'short', 'int', 'long', 'float', 'double', 'signed', 'unsigned', '_Bool', 'bool'}
QUAL = {'const', 'volatile', 'restrict', '__restrict'}
SPEC = BUILTIN | QUAL | {'struct', 'union', 'enum'}


def preprocess(src):
    out = os.path.join(PP, os.path.splitext(os.path.basename(src))[0] + '.i')
    p = subprocess.run([CLANG, '--target=mips-unknown-none-elf', '-E', '-P', '-nostdinc', '-ffreestanding', '-Isrc', '-I',
                        os.path.join(DEV, 'libs', 'AkiMods', 'common', 'include')] + DEFINES + [src, '-o', out],
                       cwd=MOD, capture_output=True, text=True)
    if p.returncode:
        raise SystemExit('preprocessing %s failed: %s' % (src, p.stderr[:2000]))
    return open(out, encoding='utf8', errors='replace').read()


def tokenize(text):
    """[[whitespace before, token]]; the last entry's token is '' (the text after the last token)."""
    toks, ws = [], ''
    for m in TOKEN.finditer(text):
        if m.group('ws') is not None:
            ws += m.group('ws')
        else:
            toks.append([ws, m.group('tok')])
            ws = ''
    toks.append([ws, ''])
    return toks


def typedef_names(toks):
    """The names the file's typedefs make (the last name before the typedef's ';', or the one in `(*name)`)."""
    names, i, n = set(), 0, len(toks)
    while i < n:
        if toks[i][1] == 'typedef':
            depth, j, last, fn = 0, i + 1, None, None
            while j < n and not (toks[j][1] == ';' and depth == 0):
                t = toks[j][1]
                if t in '({[':
                    depth += 1
                elif t in ')}]':
                    depth -= 1
                elif re.match(r'[A-Za-z_]\w*$', t) and depth == 0:
                    last = t
                if t == '*' and j > 0 and toks[j - 1][1] == '(' and j + 1 < n and re.match(r'[A-Za-z_]\w*$', toks[j + 1][1]):
                    fn = toks[j + 1][1]
                j += 1
            if fn:
                names.add(fn)
            elif last:
                names.add(last)
            i = j
        i += 1
    return names


def is_type_end(toks, k, types):
    """Whether toks[k] ends a type specifier (so a following '*' makes a pointer type), and where that specifier starts."""
    t = toks[k][1]
    if t in BUILTIN or t in types:
        start = k
    elif re.match(r'[A-Za-z_]\w*$', t) and k > 0 and toks[k - 1][1] in ('struct', 'union', 'enum'):
        start = k - 1
    else:
        return None
    while start > 0 and toks[start - 1][1] in SPEC and not (toks[start - 1][1] in ('struct', 'union', 'enum') and start == k):
        start -= 1
        if toks[start][1] in ('struct', 'union', 'enum'):
            break
    return start


def rewrite_pointers(toks, types, stats):
    """Rewrites pointer types in place (see the module's notes). stats counts what it did and what it left."""
    n = len(toks)
    i = 0
    while i < n:
        if toks[i][1] != '*':
            i += 1
            continue
        k = i - 1
        while k >= 0 and toks[k][1] in QUAL:
            k -= 1
        start = is_type_end(toks, k, types) if k >= 0 else None
        if start is None:
            i += 1
            continue
        base = ' '.join(t for _, t in toks[start:k + 1] if t not in QUAL)
        # The stars (with any qualifiers between and after them).
        j, stars = i, 0
        while j < n and toks[j][1] in ({'*'} | QUAL):
            if toks[j][1] == '*':
                stars += 1
            j += 1
        new = base
        for _ in range(stars):
            new = 'NPtr<%s>' % new
        ws = toks[start][0]
        for q in range(start, j):
            toks[q] = ['', '']
        toks[start] = [ws, new]
        stats['pointer_types'] = stats.get('pointer_types', 0) + 1
        # A declaration of several names (u8 *a, b;): split it, so each keeps its own type. Only at statement level: the name
        # follows, and the statement goes on to a ',' at the same depth before its ';'.
        if j < n and re.match(r'[A-Za-z_]\w*$', toks[j][1]) and (start == 0 or toks[start - 1][1] in (';', '{', '}', 'static', 'extern', '') or
                                                              toks[start - 1][1] in ('static', 'extern')):
            depth, m = 0, j + 1
            while m < n:
                t = toks[m][1]
                if t in '({[':
                    depth += 1
                elif t in ')}]':
                    if depth == 0:
                        break
                    depth -= 1
                elif t == ';' and depth == 0:
                    break
                elif t == ',' and depth == 0:
                    # the next declarator: its own stars, its own NPtr
                    p, more = m + 1, 0
                    while p < n and toks[p][1] in ({'*'} | QUAL):
                        if toks[p][1] == '*':
                            more += 1
                        toks[p] = ['', '']
                        p += 1
                    decl = base
                    for _ in range(more):
                        decl = 'NPtr<%s>' % decl
                    toks[m] = ['', '; ' + decl]
                    stats['split_declarations'] = stats.get('split_declarations', 0) + 1
                m += 1
        i = j
    return toks


def map_data():
    """name -> (N64 address, size) for the mod's data symbols (mod.map), and the private ones (modglue's private_data.json)."""
    rows = json.load(open(os.path.join(DEV, 'tools', 'lift', 'private_data.json')))
    return {r['name']: (int(r['address'], 16), r['size']) for r in rows}, {r['name'] for r in rows if r['private']}


def split_items(toks):
    """Top-level items as token index ranges [(kind, name, first, last)]: 'function' or 'other' (see items())."""
    out, depth, first = [], 0, 0
    ts = [t for _, t in toks]
    for k, t in enumerate(ts):
        if t in ('(', '[', '{'):
            depth += 1
        elif t in (')', ']'):
            depth -= 1
        elif t == '}':
            depth -= 1
            if depth == 0 and is_function(ts[first:k + 1]):
                out.append(('function', function_name(ts[first:k + 1]), first, k))
                first = k + 1
        elif t == ';' and depth == 0:
            out.append(('other', None, first, k))
            first = k + 1
    return out


def declarators(ts):
    """A variable declaration's declarators: [(name, first token index of the declarator, index after it, dims)] and the index
    where the base type ends (the first declarator's start). None if the item isn't a plain variable declaration."""
    if not ts or ts[0] in ('typedef',) or ts[0] in ('struct', 'union', 'enum') and '{' in ts and ts[-2:-1] == ['}']:
        return None
    depth, parts, start = 0, [], 0
    for k, t in enumerate(ts):
        if t in ('(', '[', '{'):
            if t == '(' and depth == 0 and '=' not in ts[:k]:
                return None         # (a prototype, or a function pointer)
            depth += 1
        elif t in (')', ']', '}'):
            depth -= 1
        elif t in (',', ';') and depth == 0:
            parts.append((start, k))
            start = k + 1
    out = []
    for n, (a, b) in enumerate(parts):
        stop = b
        if '=' in ts[a:b]:
            stop = a + ts[a:b].index('=')
        k = stop - 1
        dims = []
        while k >= a and ts[k] == ']':
            depth, j = 0, k
            while j >= a:
                if ts[j] == ']':
                    depth += 1
                elif ts[j] == '[':
                    depth -= 1
                    if depth == 0:
                        break
                j -= 1
            dims.insert(0, ' '.join(ts[j + 1:k]))
            k = j - 1
        if k < a or not re.match(r'[A-Za-z_]\w*$', ts[k]) or ts[k] in SPEC:
            return None
        out.append((ts[k], k, b, dims))
    return out


# (A view's place: the mod's section as the loader put it plus the offset -- lift_mod_address, as the mod's converted copies
#  reach the mod's data. In the tester that is where it put the mod, and lift.py gives each place named this way fresh random
#  contents in every test.)
MOD_BASE = 0x81000000

STORAGE = {'static', 'extern', 'const', 'volatile', '__attribute__'}


def drop_attributes(toks):
    """Every __attribute__((...)) group blanked: the mod's are aligned (globals), noinline, retain / section (hook entry points)
    and unused -- none changes what the C does on the PC, and none is packed (checked 2026-11-12)."""
    k, n = 0, len(toks)
    while k < n:
        if toks[k][1] == '__attribute__':
            depth = 0
            j = k + 1
            while j < n:
                if toks[j][1] == '(':
                    depth += 1
                elif toks[j][1] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            for q in range(k, j + 1):
                toks[q] = [toks[q][0] if q == k else '', '']
            k = j
        k += 1
    return toks


def drop_tentative_repeats(toks):
    """(2026-11-14, modwhole) C's tentative definitions: a file-scope declaration without an initializer that is token for token one
    before it (`static int quiet_over;` twice in logic.c) is legal C and a redefinition in C++: the later ones go. The count."""
    seen, n = set(), 0
    for kind, name, a, b in split_items(toks):
        if kind != 'other':
            continue
        ts = tuple(t for _, t in toks[a:b + 1] if t)
        if len(ts) < 3 or '=' in ts or '(' in ts or ts[0] in ('typedef', 'extern') or '{' in ts or ts[-1] != ';' or \
                not re.match(r'[A-Za-z_]\w*$', ts[-2]):
            continue        # (a declaration of one name: a type, the name, ';' -- not an empty ';')
        if ts in seen:
            for q in range(a, b + 1):
                toks[q] = ['', '']      # (its blank too: the text of every other function stays as it was)
            n += 1
        seen.add(ts)
    return n


def split_multi_declarations(toks, types):
    """`T a, *b = x, c[3];` -> `T a; T *b = x; T c[3];` wherever a statement declares several names (C binds each star to its
    name, so the pointer rewrite needs each declaration on its own). Not typedefs, not a struct defined in the same statement,
    not inside parentheses (a for's first part)."""
    n = len(toks)
    starts = [0] + [k + 1 for k in range(n) if toks[k][1] in (';', '{', '}')]
    count = 0
    for s in starts:
        k = s
        while k < n and toks[k][1] in ('static', 'extern', 'const', 'volatile', 'register', 'inline'):
            k += 1
        if k >= n:
            continue
        t = toks[k][1]
        if not (t in BUILTIN or t in types or t in ('struct', 'union', 'enum')):
            continue
        # the base type: up to the first declarator (an identifier not a type word, or a '*')
        j = k
        while j < n and (toks[j][1] in SPEC or toks[j][1] in types or
                         (j > 0 and toks[j - 1][1] in ('struct', 'union', 'enum') and re.match(r'[A-Za-z_]\w*$', toks[j][1]))):
            j += 1
        base = [x for _, x in toks[s:j]]
        depth, m, commas, braces = 0, j, [], False
        while m < n:
            x = toks[m][1]
            if x in ('(', '[', '{'):
                if x == '{' and depth == 0:
                    braces = True
                    if toks[m - 1][1] == ')':
                        commas = []     # (a function's definition: nothing to split)
                        break
                depth += 1
            elif x in (')', ']', '}'):
                if depth == 0:
                    break
                depth -= 1
            elif x == ';' and depth == 0:
                break
            elif x == ',' and depth == 0:
                commas.append(m)
            m += 1
        if not commas or braces and '=' not in [y for _, y in toks[j:commas[0]]] or m >= n or toks[m][1] != ';':
            continue
        # (part 1 of the Linux work, 2026-11-14: a parenthesis inside an array's brackets is no prototype's --
        #  `s32 on[(256 - 102)], m = 0;` was left whole, so `on` never became a frame array and roster_random_ord ran its host
        #  array past its end)
        first, sq, proto = [y for _, y in toks[j:commas[0]]], 0, False
        for y in first:
            sq += (y == '[') - (y == ']')
            proto = proto or (y == '(' and sq == 0)
        if proto and '=' not in first:
            continue        # (prototypes: `int f(int), g(void);`)
        for c in commas:
            toks[c] = ['', '; ' + ' '.join(base)]
            count += 1
    return count


def struct_views(toks):
    """After each of the file's `typedef struct [Tag] { ... } Name;` at file scope, its view (nm_native.h NView): the fields by
    name, each at its offsetof, so that a pointer into game memory to it can be followed field by field. Bracketed by
    /*NMVIEW*/ ... /*NMVIEW_END*/ (prove_gen.py moves them out of its namespaces: a template's specialization lives at file
    scope). Structures with a nested structure or union defined inside, or a bit field, get none (counted)."""
    made, skipped = 0, 0
    for kind, name, a, b in split_items(toks):
        ts = [t for _, t in toks[a:b + 1]]
        if kind != 'other' or len(ts) < 5 or ts[0] != 'typedef' or ts[1] != 'struct':
            continue
        if '{' not in ts:
            continue
        open_ = ts.index('{')
        depth, close = 0, None
        for k in range(open_, len(ts)):
            if ts[k] == '{':
                depth += 1
            elif ts[k] == '}':
                depth -= 1
                if depth == 0:
                    close = k
                    break
        if close is None or close + 2 >= len(ts) or not re.match(r'[A-Za-z_]\w*$', ts[close + 1]) or ts[close + 2] != ';':
            continue
        sname = ts[close + 1]
        body = ts[open_ + 1:close]
        if any(t in ('struct', 'union', '{', ':') for t in body):
            skipped += 1
            continue
        fields, cur = [], []
        for t in body:
            if t == ';':
                d = declarators(cur + [';'])
                if d:
                    fields += [f[0] for f in d]
                cur = []
            else:
                cur.append(t)
        if not fields:
            continue
        T = 'NM_SELF::%s' % sname
        view = ['/*NMVIEW*/',
                'template <> struct NView<%s> {' % T, '    u32 nm_at;']
        view += ['    NFieldOf<decltype(%s::%s)>::type %s;' % (T, f, f) for f in fields]
        view += ['    explicit NView(u32 x) : nm_at(x)%s {}' % ''.join(', %s(x + (u32)offsetof(%s, %s))' % (f, T, f) for f in fields),
                 '    NView* operator->() { return this; }', '};',
                 'template <> struct NRef<%s> : NView<%s> {' % (T, T),
                 '    explicit NRef(u32 x) : NView<%s>(x) {}' % T,
                 '    NPtr<%s> operator&() const { return NPtr<%s>(this->nm_at); }' % (T, T),
                 # (2026-11-13, modnative2) A whole structure copied: from another in game memory, its bytes, as C's
                 #  assignment (a memcpy) copies them, padding and all; from a PC value -- a compound literal -- field by
                 #  field (nm_assign: arrays element by element).
                 '    const NRef& operator=(const NRef& o) const { nm_copy_bytes(this->nm_at, o.nm_at, NSize<%s>::value); return *this; }' % T,
                 '    const NRef& operator=(const %s& v) const { %s return *this; }' % (T, ' '.join('nm_assign(this->%s, v.%s);' % (f, f) for f in fields)),
                 '};',
                 '/*NMVIEW_END*/;']
        toks[b] = [toks[b][0], toks[b][1] + '\n' + '\n'.join(view) + '\n']
        made += 1
    return made, skipped


def remove_block_externs(toks, bound):
    """A function's own `extern T name;` for a global that is now a view or a bound private object: the namespace's name is the
    one meant, so the block declaration goes."""
    n = len(toks)
    depth = 0
    k = 0
    removed = 0
    while k < n:
        t = toks[k][1]
        if t == '{':
            depth += 1
        elif t == '}':
            depth -= 1
        elif t == 'extern' and depth > 0:
            j = k
            while j < n and toks[j][1] != ';':
                j += 1
            names = [x for _, x in toks[k:j] if x in bound]
            if names and '(' not in [x for _, x in toks[k:j]]:
                for q in range(k, j + 1):
                    toks[q] = [toks[q][0] if q == k else '', '']
                removed += 1
            elif DATA and '(' not in [x for _, x in toks[k:j]]:
                # (a global of another of the mod's files, declared here inside a function: a view of it, here)
                ts_ = [x for _, x in toks[k:j + 1]]
                decl = declarators(ts_[1:])
                if decl and len(decl) == 1 and decl[0][0] in DATA[0] and decl[0][0] not in DATA[1]:
                    nm, at, e, dims = decl[0]
                    t = ' '.join(x for x in ts_[1:1 + at] if x not in STORAGE)
                    # (2026-11-13: of any number of dimensions, as bind_globals)
                    t += ''.join('[%s]' % d for d in dims[1:])
                    for q in range(k, j + 1):
                        toks[q] = [toks[q][0] if q == k else '', '']
                    toks[k] = [toks[k][0], 'static %s<%s> %s(lift_mod_address(0, 0x%X)%s);' % ('NArr' if dims else 'NVar', t, nm, DATA[0][nm][0] - MOD_BASE,
                                                                                            array_size(DATA[0][nm], dims))]
                    removed += 1
            k = j
        k += 1
    return removed


VIEW_TYPES = {}
# (2026-11-13, modnative5) The variables the mod's compiler shrank to a byte, with their two values (shrunk.py, the user's rule (2))
SHRUNK = json.load(open(os.path.join(HERE, 'shrunk.json')))['shrunk'] if os.path.exists(os.path.join(HERE, 'shrunk.json')) else {}


def array_size(entry, dims):
    """(2026-11-13, modnative5: the user's rule (4)) An array view's second argument, its size in bytes as mod.map has it, so
    an index past its end is told to the tester (nm_native.h NArr, nm_outside); '' for a scalar or a size the map doesn't give."""
    if not dims or len(entry) < 2 or not entry[1]:
        return ''
    return ', 0x%X' % entry[1]


CONTENT_PLACED = {}     # (name -> (address, size): the constant tables content_place found, for the census report)
INT_SIZES = {'u8': 1, 's8': 1, 'char': 1, 'unsigned char': 1, 'signed char': 1, 'u16': 2, 's16': 2, 'short': 2, 'unsigned short': 2,
             'u32': 4, 's32': 4, 'int': 4, 'unsigned int': 4, 'unsigned': 4}


def _int_value(tok):
    """An integer or character literal's value, or None."""
    m = re.fullmatch(r'(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*', tok)
    if m:
        return int(m.group(1), 0) if not re.fullmatch(r'0\d+', m.group(1)) else int(m.group(1), 8)
    if tok.startswith("'") and tok.endswith("'"):
        bs = literal_bytes('"' + tok[1:-1] + '"')
        return bs[0] if bs and len(bs) == 1 else None
    return None


def decaying(toks, cache={}):
    """(2026-11-14, modwhole) The names some function of the file uses as a value of their own -- not indexed, not a member, not in
    sizeof: an array handed on as a pointer. Only such a constant table is placed by its contents (a table only indexed stays the
    PC object it was, so the functions that use it so keep their text)."""
    key = id(toks), len(toks)
    if key not in cache:
        ts = [t for _, t in toks]
        out = set()
        for kind, name, a, b in split_items(toks):
            if kind != 'function':
                continue
            for k in range(a + 1, b):
                t = ts[k]
                if re.match(r'[A-Za-z_]\w*$', t) and ts[k + 1] not in ('[', '(', '.', '->') and ts[k - 1] not in ('.', '->', 'sizeof') and not (
                        ts[k - 1] == '(' and ts[k - 2] == 'sizeof'):
                    out.add(t)
        cache.clear()
        cache[key] = out
    return cache[key]


def content_place(dts, base, dims):
    """(2026-11-14, modwhole) (address, size) of a constant table's bytes in the mod's read-only image -- `static const T x[N] =
    {...}` / `= "..."` with T an integer type, char, or a pointer to char (each element its string's place, as linked) -- or None.
    Only a const one-dimension array whose every element this works out."""
    if 'const' not in dts or '=' not in dts or not dims or len(dims) != 1:
        return None
    t = ' '.join(x for x in base if x != 'const')
    ptr = re.fullmatch(r'NPtr < (?:const )?char >', t)
    unit = 4 if ptr else INT_SIZES.get(t)
    if not unit or not IMAGE:
        return None
    init = dts[dts.index('=') + 1:]
    if init and init[-1] == ';':
        init = init[:-1]
    out = bytearray()
    if init and init[0].startswith('"') and unit == 1:
        bs = b''
        for x in init:
            if not x.startswith('"'):
                return None
            b_ = literal_bytes(x)
            if b_ is None:
                return None
            bs += b_
        out += bs + b'\0'
    elif init and init[0] == '{' and init[-1] == '}':
        els, cur, depth = [], [], 0
        for x in init[1:-1]:
            if x == ',' and depth == 0:
                els.append(cur)
                cur = []
                continue
            depth += (x in '([{') - (x in ')]}')
            cur.append(x)
        if cur:
            els.append(cur)
        for el in els:
            if ptr:
                if not el or not all(x.startswith('"') for x in el):
                    return None
                bs = b''.join(literal_bytes(x) or b'' for x in el) + b'\0'
                at = IMAGE.find(bs)
                if at < 0:
                    return None
                out += (MOD_BASE + at).to_bytes(4, 'big')
                continue
            neg = el[:1] == ['-']
            v = _int_value(el[-1]) if len(el) == 1 + neg else None
            if v is None:
                return None
            out += ((-v if neg else v) & ((1 << (8 * unit)) - 1)).to_bytes(unit, 'big')
    else:
        return None
    n = dims[0].strip()
    if n:
        try:
            n = int(n, 0)
        except ValueError:
            return None
        if len(out) > n * unit:
            return None
        out += b'\0' * (n * unit - len(out))
    at = IMAGE.find(bytes(out))
    while at >= 0 and at % unit:
        at = IMAGE.find(bytes(out), at + 1)
    if at < 0:
        return None
    return (MOD_BASE + at, len(out))


def bind_globals(toks, data, private, report):
    """The mod's globals as the proof needs them (modglue's rule, confirmed): PRIVATE ones stay PC objects (the tester's table
    gives them their N64 values and compares them); every other one becomes a view at its N64 address (NVar / NArr, through
    lift_mem.h), its initializer dropped -- its contents are the mod's image or the test's memory, as the original sees them.
    report: name -> what was done ('view', 'private', 'not in the map', 'multi-dimensional', ...)."""
    ts = [t for _, t in toks]
    seen = set()
    for kind, name, a, b in split_items(toks):
        if kind != 'other':
            continue
        decl = declarators(ts[a:b + 1])
        if not decl:
            continue
        base = [t for t in ts[a:a + decl[0][1]] if t not in STORAGE]
        if not base or '(' in ts[a:a + decl[0][1]]:
            continue
        repl = []
        keep_host = []
        for nm, k, e, dims in decl:
            if nm not in data and len(decl) == 1 and os.environ.get('NM_NO_CONTENT_PLACE') != '1' and nm in decaying(toks):
                placed = content_place(ts[a:b + 1], base, dims)
                if placed:
                    # (2026-11-14, modwhole: a constant table the map doesn't name, at the place its bytes are -- content_place)
                    data = dict(data)
                    data[nm] = placed
                    CONTENT_PLACED[nm] = placed
            if nm not in data:
                report[nm] = 'not in the mod map (kept as a PC object)'
                keep_host.append(nm)
                continue
            if nm in private:
                report[nm] = 'private (PC object)'
                keep_host.append(nm)
                continue
            addr = data[nm][0]
            t = ' '.join(base)
            # (2026-11-13, modnative2) A multi-dimensional array: a view of its rows, each row an array type (nm_native.h's
            #  NRef<T[N]> is itself an array view), so a[i][j] is the same element as on the N64.
            t += ''.join('[%s]' % d for d in dims[1:])
            if nm in seen:
                continue        # (an extern declaration and the definition: one view)
            seen.add(nm)
            report[nm] = 'view at %08X' % addr
            VIEW_TYPES[nm] = (' '.join(base), dims)     # (for rewrite_sizeof)
            repl.append('static %s<%s> %s(lift_mod_address(0, 0x%X)%s);' % ('NArr' if dims else 'NVar', t, nm, addr - MOD_BASE, array_size(data[nm], dims)))
        if not keep_host and all(nm in seen for nm, _, _, _ in decl) and not repl:
            for q in range(a, b + 1):
                toks[q] = ['', '']
            continue
        if repl and not keep_host:
            for q in range(a, b + 1):
                toks[q] = ['', '']
            toks[a] = [chr(10), ' '.join(repl)]
        elif repl:
            # (some of the names stay PC objects: keep the declaration for them, the views after it)
            toks[b] = [toks[b][0], '; ' + ' '.join(repl)]
            for nm, k, e, dims in decl:
                if nm not in keep_host:
                    toks[a + k] = [toks[a + k][0], '__nm_unused_' + nm]
    return toks


def sizeof_of_expression(ts, types):
    """How many `sizeof` take an expression, not a type: with views, sizeof of a pointed-at value or an array view is the
    size of the view, not of the N64 object (silently wrong, so each is counted for review)."""
    n = 0
    for k, t in enumerate(ts):
        if t == 'sizeof':
            nxt = ts[k + 1] if k + 1 < len(ts) else ''
            after = ts[k + 2] if k + 2 < len(ts) else ''
            if nxt != '(' or not (after in BUILTIN or after in types or after in ('struct', 'union', 'enum', 'NPtr', 'const')):
                n += 1
    return n


ANON_STRUCT = re.compile(r'^((?:static[ \t]+)?(?:const[ \t]+)?)struct[ \t]*\{', re.M)


def name_anonymous_structs(text):
    """(2026-11-13, modnative2) A global of an unnamed structure type -- `static struct { u32 at; u16 was, now; } undo[160];` --
    gets a name for its type first: `typedef struct { ... } NMAnon_undo; static NMAnon_undo undo[160];`. The same structure, the
    same layout; only now struct_views can write its view and bind_globals can make the global one (C++ has no way to name the
    type of an unnamed structure in a template's argument). Returns (text, how many)."""
    out, pos, count = [], 0, 0
    for m in ANON_STRUCT.finditer(text):
        if m.start() < pos:
            continue
        depth, k = 0, m.end() - 1
        while k < len(text):
            if text[k] == '{':
                depth += 1
            elif text[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        name = re.match(r'\s*([A-Za-z_]\w*)', text[k + 1:])
        if not name:
            continue
        tname = 'NMAnon_' + name.group(1)
        out.append(text[pos:m.start()])
        out.append('typedef struct %s %s; %s%s' % (text[m.end() - 1:k + 1], tname, m.group(1), tname))
        pos = k + 1
        count += 1
    out.append(text[pos:])
    return ''.join(out), count


def mod_image():
    """The mod's read-only part as built (mod_binary.bin from 0x81000000 up to its first code): where its string literals are."""
    build = os.path.join(MOD, 'build', 'mod_six')
    code = None
    for line in open(os.path.join(build, 'mod.map'), encoding='utf8', errors='replace'):
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +[0-9a-f]+ +\d+ (\.text\S*)\s*$', line)
        if m:
            code = int(m.group(1), 16) if code is None else min(code, int(m.group(1), 16))
    return open(os.path.join(build, 'mod_binary.bin'), 'rb').read()[:code - MOD_BASE]


ESCAPES = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, '"': 34, "'": 39, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '?': 63, 'e': 27}


def literal_bytes(tok):
    """The bytes of a C string literal token, its escapes worked out (None if it has one this doesn't know)."""
    body, out, k = tok[1:-1], bytearray(), 0
    while k < len(body):
        c = body[k]
        if c != '\\':
            out += c.encode('utf8')
            k += 1
            continue
        e = body[k + 1]
        if e == 'x':
            m = re.match(r'[0-9a-fA-F]+', body[k + 2:])
            out.append(int(m.group(0), 16) & 0xFF)
            k += 2 + len(m.group(0))
        elif e in '01234567':
            m = re.match(r'[0-7]{1,3}', body[k + 1:])
            out.append(int(m.group(0), 8) & 0xFF)
            k += 1 + len(m.group(0))
        elif e in ESCAPES:
            out.append(ESCAPES[e])
            k += 2
        else:
            return None
    return bytes(out)


def string_views(toks, image, places, stats):
    """(2026-11-13, modnative2) Each string literal in a function -- `plat_config("game_mode", 0)` -- becomes a view of the same
    bytes in the mod's read-only image, NPtr<char>(lift_mod_address(0, 0x...)): on the N64 it is an address there, which the
    function passes on or reads, and the native code must pass and read the same. The place is found by content (the bytes and
    their terminating 0); where the image has them at more than one place (the linker merges a string into the tail of another),
    the one the mod's ORIGINAL code of this file names (places: the addresses its translation's relocations make) is taken, and a
    literal with two such places, or none in the image, is left as it is (a compile error: counted, never guessed).
    Not: an array's initializer (`char buf[] = "..."`: the array's own bytes, a copy), sizeof("...")."""
    ts = [t for _, t in toks]
    depth_fn = [False] * len(ts)
    table = [False] * len(ts)
    for kind, name, a, b in split_items(toks):
        if kind == 'function':
            for q in range(a, b + 1):
                depth_fn[q] = True
        elif '=' in ts[a:b + 1] and os.environ.get('NM_NO_TABLE_STRINGS') != '1':
            # (2026-11-14, modwhole: a file-scope table of N64 pointers -- an NPtr before its '=' -- its initializer as in a function)
            head = ts[a:a + ts[a:b + 1].index('=')]
            if 'NPtr' in head and '(' not in head and 'typedef' not in head:
                for q in range(a, b + 1):
                    table[q] = True
    k = 0
    while k < len(ts):
        t = ts[k]
        if not (t.startswith('"') and (depth_fn[k] or table[k])):
            k += 1
            continue
        # adjacent literals are one ("a" "b")
        j, parts = k, []
        while j < len(ts) and ts[j].startswith('"'):
            parts.append(ts[j])
            j += 1
        prev = ts[k - 1] if k else ''
        if (prev == '=' and k > 1 and ts[k - 2] == ']') or (prev == '(' and k > 1 and ts[k - 2] == 'sizeof') or prev in ('asm', '__asm__', 'extern'):
            k = j
            continue
        data = b''
        for p in parts:
            bs = literal_bytes(p)
            if bs is None:
                data = None
                break
            data += bs
        if data is None:
            stats['strings_unknown_escape'] = stats.get('strings_unknown_escape', 0) + 1
            k = j
            continue
        needle, at, found = data + b'\0', 0, []
        while True:
            at = image.find(needle, at)
            if at < 0:
                break
            found.append(at)
            at += 1
        # (the places the file's original makes that no symbol names first -- the compiler's own strings; then those it
        #  named as strings, `$.str`)
        named = [f for f in found if MOD_BASE + f in places and MOD_BASE + f not in NAMED_PLACES] or             [f for f in found if MOD_BASE + f in places and MOD_BASE + f in STRING_SYMBOLS]
        pick = named if named else found if len(found) == 1 else []
        if not pick and found and os.environ.get('NM_NO_FIRST_PLACE') != '1':
            # (2026-11-14, modwhole: the same bytes at several places, none this file's original names -- the first of them;
            #  NM_NO_FIRST_PLACE=1: left as before)
            pick = [found[0]]
            stats['strings_first_place'] = stats.get('strings_first_place', 0) + 1
        elif len(set(pick)) > 1 and os.environ.get('NM_NO_FIRST_PLACE') != '1':
            # (and the same when the original names several of them -- `""`: a zero byte at many places it names)
            pick = [min(pick)]
            stats['strings_first_place'] = stats.get('strings_first_place', 0) + 1
        if len(set(pick)) != 1:
            stats['strings_not_placed'] = stats.get('strings_not_placed', 0) + 1
            k = j
            continue
        toks[k] = [toks[k][0], 'NPtr<char>(lift_mod_address(0, 0x%X))' % pick[0]]
        for q in range(k + 1, j):
            toks[q] = [toks[q][0], '']
        stats['string_views'] = stats.get('string_views', 0) + 1
        k = j
    return toks


def array_parameters(toks, types, names):
    """(2026-11-13, modnative2) A parameter written as an array -- `void continue_screen(u16 pr[2], int start[2])` -- is a
    pointer in C (the caller's array decays), so it becomes one here too: NPtr<u16> pr. In the definition and prototypes of
    each of the mod's functions with a body of its own on the N64 (names: the inventory's), whose callers hand it an address
    in game memory; a helper the compiler always inlined keeps its C parameters, so a caller's PC array still reaches it.
    Returns how many."""
    ts = [t for _, t in toks]
    count = 0
    for kind, name, a, b in split_items(toks):
        # the parameter list: the first '(' after a name at the item's own depth
        k = next((k for k in range(a + 1, b + 1) if ts[k] == '(' and re.match(r'[A-Za-z_]\w*$', ts[k - 1]) and
                  ts[k - 1] not in ('__attribute__', 'sizeof')), None)
        if k is None or (kind == 'other' and '=' in ts[a:k]) or ts[k - 1] not in names:
            continue
        depth, j, start = 0, k, k + 1
        params = []
        while j <= b:
            if ts[j] in '([':
                depth += 1
            elif ts[j] in ')]':
                depth -= 1
                if depth == 0:
                    params.append((start, j))
                    break
            elif ts[j] == ',' and depth == 1:
                params.append((start, j))
                start = j + 1
            j += 1
        for s, e in params:
            p = ts[s:e]
            if len(p) >= 4 and p[-1] == ']' and '[' in p and re.match(r'[A-Za-z_]\w*$', p[p.index('[') - 1]) and '(' not in p:
                at = s + p.index('[') - 1
                base = ' '.join(x for x in ts[s:at] if x not in QUAL)
                if not (base.split()[-1] in BUILTIN or base.split()[-1] in types or base.startswith(('struct', 'NPtr'))):
                    continue
                inner = ''.join('[%s]' % d for d in re.findall(r'\[([^\]]*)\]', ' '.join(ts[at + 1:e]))[1:])
                toks[s] = [toks[s][0], 'NPtr<%s%s> %s' % (base, inner, ts[at])]
                for q in range(s + 1, e):
                    toks[q] = ['', '']
                count += 1
    return count


def switch_declarations(toks):
    """(2026-11-13, modnative2) A scalar declared with a value straight inside a switch's body -- `case G_STORY: int fresh =
    scr == 0;` -- which C allows a later `case` to jump past and C++ doesn't: `int fresh; fresh = scr == 0;`, the same
    thing (a later case never reads it before setting it, or C would read garbage too). Only the built-in scalars (SIZES), so
    no constructor is skipped. Returns how many."""
    ts = [t for _, t in toks]
    count, stack, depth = 0, [], 0
    pending = False
    for k, t in enumerate(ts):
        if t == 'switch':
            pending = True
        elif t == '{':
            depth += 1
            if pending:
                stack.append(depth)
                pending = False
        elif t == '}':
            if stack and stack[-1] == depth:
                stack.pop()
            depth -= 1
        elif stack and stack[-1] == depth and k > 0 and ts[k - 1] in (';', ':', '{', '}') and t in SIZES:
            e = k
            while e < len(ts) and ts[e] not in (';', '=', '(', '['):
                e += 1
            if e < len(ts) and ts[e] == '=' and e == k + 2 and re.match(r'[A-Za-z_]\w*$', ts[k + 1]):
                toks[e] = [toks[e][0], '; ' + ts[k + 1] + ' =']
                count += 1
    return count


def statement_starts(ts, a, b):
    """Indexes in ts[a..b] where a statement may start (after ';', '{' or '}')."""
    return [k for k in range(a + 1, b + 1) if ts[k - 1] in (';', '{', '}')]


def bind_local_statics(toks, data, private, stats):
    """(2026-11-13, modnative2) A function's own static variable -- `static int wait;` inside qa_table_input -- is, on the N64,
    the mod's data at a place of its own, which mod.map names `qa_table_input.wait`. As the file's globals (bind_globals): a
    PRIVATE one becomes a reference to the tester's PC object for it (lift_mod_private_<function>_<name>, the table the user
    confirmed, which gives it its N64 value and compares it), its size checked against the N64 symbol's; every other one a view
    at its N64 place (NVar / NArr), its initializer dropped (its contents are the mod's image or the test's memory, as the
    original sees them). A static the map doesn't name (the compiler folded it away, or it is in a helper the compiler
    inlined under another name) is left as it is, and prove_gen refuses the function."""
    ts = [t for _, t in toks]
    for kind, fname, a, b in split_items(toks):
        if kind != 'function':
            continue
        body = ts.index('{', a) if '{' in ts[a:b + 1] else None
        if body is None:
            continue
        externs = []
        for s in statement_starts(ts, body, b):
            if ts[s] != 'static':
                continue
            e = s
            depth = 0
            while e <= b and not (ts[e] == ';' and depth == 0):
                if ts[e] in '({[':
                    depth += 1
                elif ts[e] in ')}]':
                    depth -= 1
                e += 1
            decl = declarators(ts[s + 1:e + 1])
            if not decl or len(decl) != 1:
                continue
            nm, at, _, dims = decl[0]
            t = ' '.join(x for x in ts[s + 1:s + 1 + at] if x not in STORAGE)
            key = '%s.%s' % (fname, nm)
            if key not in data and dims and len(dims) == 1 and key + '.0' in data and key + '.0' in private:
                # (2026-11-13, modnative5) An array the mod's compiler split into one variable per element (f.x.0, f.x.1, ...: all
                #  private): its elements bound to their PC objects (nm_native.h NSplit), its initializer dropped as any static's.
                n_el = 0
                while key + '.%d' % n_el in data:
                    n_el += 1
                c_els = [re.sub(r'\W', '_', key + '.%d' % i) for i in range(n_el)]
                t = ' '.join(x for x in ts[s + 1:s + 1 + at] if x not in STORAGE)
                new = 'static NSplit<%s, %d> %s = {{%s}};   /* %s: split by the mod compiler into one variable per element */' % (
                    t, n_el, nm, ', '.join('lift_mod_private_%s' % c_ for c_ in c_els), key)
                externs += ['extern "C" u8 lift_mod_private_%s[];' % c_ for c_ in c_els]
                stats['local_statics_split'] = stats.get('local_statics_split', 0) + 1
                for q in range(s, e + 1):
                    toks[q] = [toks[q][0] if q == s else '', '']
                toks[s][1] = new
                continue
            if key not in data:
                if 'const' in ts[s:s + 1 + at] and '=' in ts[s:e]:
                    # (a constant table the compiler folded into the code -- the map has no symbol for it: the values the
                    #  original uses are the C initializer's, so a PC constant is the same. Marked for prove_gen, which
                    #  refuses any other unbound static. If its address were handed on, the PC array could not become an
                    #  N64 pointer: a compile error, never a silent difference.)
                    toks[s] = [toks[s][0], 'static /*NM_PC_CONST*/']
                    stats['local_statics_pc_constant'] = stats.get('local_statics_pc_constant', 0) + 1
                    continue
                stats['local_statics_unplaced'] = stats.get('local_statics_unplaced', 0) + 1
                continue
            addr, size = data[key]
            c = re.sub(r'\W', '_', key)
            d = ''.join('[%s]' % x for x in dims)
            if key in private:
                if dims and not dims[0].strip():
                    continue        # (an array sized by its initializer: its type isn't known without it)
                new = ('static %s (&%s)%s = *reinterpret_cast<%s(*)%s>(lift_mod_private_%s); static_assert(sizeof(%s) == %d); /* %s: the N64 size */' % (
                    t, nm, d, t, d, c, nm, size, key)) if dims else (
                    # (2026-11-13, modnative5: the user's rule (2) -- one the mod's compiler shrank to a byte: shrunk.py, NShrunk)
                    'static NShrunk<%s, %d, %d> %s(lift_mod_private_%s);   /* %s: shrunk to one byte by the mod compiler */' % (
                        t, SHRUNK[key]['initial'], SHRUNK[key]['other'], nm, c, key)) if key in SHRUNK and size == 1 and os.environ.get('NM_NO_SHRUNK') != '1' else (
                    'static %s& %s = *reinterpret_cast<%s*>(lift_mod_private_%s); static_assert(sizeof(%s) == %d); /* %s: the N64 size */' % (
                        t, nm, t, c, nm, size, key))
                externs.append('extern "C" u8 lift_mod_private_%s[];' % c)
                stats['local_statics_private'] = stats.get('local_statics_private', 0) + 1
            else:
                new = 'static %s<%s%s> %s(lift_mod_address(0, 0x%X)%s);' % ('NArr' if dims else 'NVar', t,
                                                                         ''.join('[%s]' % x for x in dims[1:]), nm, addr - MOD_BASE,
                                                                         array_size((addr, size), dims))
                stats['local_statics_viewed'] = stats.get('local_statics_viewed', 0) + 1
                if dims:
                    # (2026-11-14, modwhole: `sizeof x` / `sizeof(x)` of the array in this function is the N64 object's size,
                    #  mod.map's -- an NArr view's own type has no length, so rewrite_sizeof's NSizeOf would refuse it)
                    q = e + 1
                    while q < b:
                        if ts[q] == 'sizeof' and ts[q + 1:q + 4] == ['(', nm, ')']:
                            toks[q] = [toks[q][0], '((u32)%d)' % size]
                            for z in range(q + 1, q + 4):
                                toks[z] = [toks[z][0], '']
                            stats['local_statics_sizeof'] = stats.get('local_statics_sizeof', 0) + 1
                            q += 4
                            continue
                        if ts[q] == 'sizeof' and ts[q + 1] == nm and ts[q + 2] not in ('[', '.', '->', '('):
                            toks[q] = [toks[q][0], '((u32)%d)' % size]
                            toks[q + 1] = [toks[q + 1][0], '']
                            stats['local_statics_sizeof'] = stats.get('local_statics_sizeof', 0) + 1
                        q += 1
            for q in range(s, e + 1):
                toks[q] = [toks[q][0] if q == s else '', '']
            toks[s] = [toks[s][0], new]
        if externs:
            # (the tester's PC objects, declared before the function: the tester's file defines them)
            toks[a] = [toks[a][0], ' '.join(sorted(set(externs))) + '\n' + toks[a][1]]
    return toks


def sizeof_operand(ts, k, types):
    """For the `sizeof` at ts[k]: (first, last) token indexes of an EXPRESSION operand (parenthesized or a unary expression),
    or None when it takes a type."""
    n = len(ts)
    j = k + 1
    if j < n and ts[j] == '(':
        after = ts[j + 1] if j + 1 < n else ''
        if after in BUILTIN or after in types or after in ('struct', 'union', 'enum', 'NPtr', 'const', 'volatile', 'NMAnon'):
            return None
        depth, e = 0, j
        while e < n:
            if ts[e] == '(':
                depth += 1
            elif ts[e] == ')':
                depth -= 1
                if depth == 0:
                    break
            e += 1
        return j, e
    # sizeof x, sizeof *p, sizeof a[0], sizeof s.f, sizeof p->f
    e = j
    while e < n and ts[e] in ('*', '&', '-', '!', '~'):
        e += 1
    if e >= n or not re.match(r'[A-Za-z_]\w*$', ts[e]):
        return None
    e += 1
    while e < n:
        if ts[e] == '[':
            depth = 0
            while e < n:
                if ts[e] == '[':
                    depth += 1
                elif ts[e] == ']':
                    depth -= 1
                    if depth == 0:
                        break
                e += 1
            e += 1
        elif ts[e] in ('.', '->') and e + 1 < n:
            e += 2
        else:
            break
    return j, e - 1


def rewrite_sizeof(toks, types, views, data, stats):
    """(2026-11-13, modnative2) `sizeof` of an expression, which views change (sizeof of an NArr is 4, of an NRef 8): the N64's
    size instead. `sizeof NAME` of one of the mod's globals made a view is its symbol's size in mod.map (the N64 object's own);
    `sizeof NAME[i]...` of one the size of its element type (a C++ type of the same size: a pointer in it is a 4-byte NPtr);
    any other expression NSizeOf<decltype(expr)> (nm_native.h), which gives the size of what a view stands for and refuses
    (a compile error) the one it can't know: an array view's whole size."""
    ts = [t for _, t in toks]
    k = 0
    while k < len(ts):
        if ts[k] != 'sizeof':
            k += 1
            continue
        op = sizeof_operand(ts, k, types)
        if op is None:
            k += 1
            continue
        a, b = op
        inner = ts[a + 1:b] if ts[a] == '(' and ts[b] == ')' else ts[a:b + 1]
        expr = ' '.join(inner)
        if len(inner) == 1 and inner[0] in views and inner[0] in data:
            new = '((u32)%d)' % data[inner[0]][1]   # (u32: the mod's size_t, 32 bits on every platform -- modnative7)
        elif len(inner) >= 4 and inner[0] in views and inner[1] == '[' and views[inner[0]] and all(
                re.match(r'[\w\[\]\s+*-]+$', x) for x in inner):
            t, dims = views[inner[0]]
            idx = expr.count('[')
            new = 'sizeof(%s%s)' % (t, ''.join('[%s]' % d for d in dims[idx:]))
        else:
            new = '((u32)NSizeOf<std::remove_cv_t<std::remove_reference_t<decltype(%s)>>>::value)' % expr
        toks[k] = [toks[k][0], new]
        for q in range(k + 1, b + 1):
            toks[q] = ['', '']
        stats['sizeof_rewritten'] = stats.get('sizeof_rewritten', 0) + 1
        k = b + 1
    return toks


# ---- Locals whose address the function hands on: in its N64 frame, at the original's offsets (2026-11-13, modnative2) ----

NO_DEST = {'sw', 'sh', 'sb', 'sd', 'swc1', 'sdc1', 'swl', 'swr', 'beq', 'bne', 'blez', 'bgtz', 'bltz', 'bgez', 'beql', 'bnel',
           'blezl', 'bgtzl', 'bltzl', 'bgezl', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl', 'j', 'jal', 'jr', 'jalr', 'nop', 'break', 'sync',
           'mult', 'multu', 'div', 'divu', 'dmult', 'dmultu', 'ddiv', 'ddivu', 'mtc1', 'ctc1', 'mthi', 'mtlo', 'teq', 'tne',
           'c.eq.s', 'c.lt.s', 'c.le.s', 'c.eq.d', 'c.lt.d', 'c.le.d', 'c.un.s', 'c.ult.s', 'c.ule.s', 'c.olt.s', 'c.ole.s'}


def original_calls(text):
    """The original's instructions [(op, [registers and operands])] in address order (the translator writes a delay slot's
    comment twice: once), and its calls [(index of the jal, callee symbol)]."""
    ins, calls, seen, last_jal = [], [], set(), None
    for line in (text or '').split('\n'):
        m = re.match(r'\s*// 0x([0-9A-F]{8}): (\S+)\s*(.*)$', line)
        if m:
            if m.group(1) in seen:
                continue
            seen.add(m.group(1))
            ins.append((m.group(2), [x.strip() for x in m.group(3).split(',')] if m.group(3) else []))
            if m.group(2) in ('jal', 'jalr'):
                last_jal = len(ins) - 1
            continue
        m = re.match(r'\s*(\w+)\(rdram, ctx\);', line)
        if m and last_jal is not None:
            calls.append((last_jal, m.group(1)))
            last_jal = None
    return ins, calls


def frame_address(ins, at, reg):
    """The frame offset register `reg` holds at instruction `at` (searching back, straight through: `addiu reg, $sp, N`, or a
    copy of a register that does), or None."""
    k = at
    while k >= 0:
        op, args = ins[k]
        if args and args[0] == reg and op not in NO_DEST:
            if op == 'addiu' and len(args) == 3 and args[1] == '$sp':
                return int(args[2], 0)
            if op in ('or', 'addu', 'daddu') and len(args) == 3 and args[2] == '$zero':
                reg = args[1]
                k -= 1
                continue
            return None
        k -= 1
    return None


def sp_offsets(ins):
    """Every frame offset the original makes an address of (addiu $r, $sp, N), but its outgoing parameters' (under 0x10)."""
    return sorted({int(a[2], 0) for op, a in ins if op == 'addiu' and len(a) == 3 and a[1] == '$sp' and a[0] != '$sp' and int(a[2], 0) >= 0x10})


SIZES = {'char': 1, 'signed char': 1, 'unsigned char': 1, 'u8': 1, 's8': 1, 'bool': 1, '_Bool': 1, 'short': 2, 'unsigned short': 2,
         'u16': 2, 's16': 2, 'int': 4, 'unsigned int': 4, 'unsigned': 4, 'u32': 4, 's32': 4, 'float': 4, 'f32': 4, 'long': 4,
         'unsigned long': 4, 'u64': 8, 's64': 8, 'double': 8, 'f64': 8}


def local_size(decl, type_tokens, types):
    """A local's N64 size in bytes from its declaration (a built-in type or a pointer, times its dimensions), or None."""
    nm, at, end, dims = decl
    t = ' '.join(x for x in type_tokens if x not in STORAGE)
    n = 4 if t.startswith('NPtr') else SIZES.get(t)
    for d in dims:
        if not re.match(r'\d+$', d.strip() or 'x'):
            return None
        n = n * int(d) if n else None
    return n


def outgoing_area(ins):
    """Where the frame's own part starts: past the four words a callee may keep a0-a3 in, and past every stack parameter
    the original stores for a call (a `sw` to sp + 0x10 or more in the dozen instructions before a jal)."""
    low = 0x10
    for k, (op, a) in enumerate(ins):
        if op not in ('jal', 'jalr'):
            continue
        for op2, a2 in ins[max(0, k - 12):k + 2]:
            if op2 in ('sw', 'swc1') and len(a2) == 2:
                m = re.match(r'(0x[0-9A-Fa-f]+|\d+)\(\$sp\)', a2[1])
                if m and 0x10 <= int(m.group(1), 0) < 0x60:
                    low = max(low, int(m.group(1), 0) + 4)
    return low


def saved_slots(ins):
    """The frame offsets the original's prologue saves registers in (before its first branch or call)."""
    out = []
    for op, a in ins:
        if op in ('jal', 'jalr', 'jr') or op.startswith('b'):
            break
        if op in ('sw', 'sdc1', 'sd') and len(a) == 2:
            m = re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\(\$sp\)', a[1])
            if m:
                out += [int(m.group(1), 0)] + ([int(m.group(1), 0) + 4] if op != 'sw' else [])
    return out


def call_args(ts, k):
    """The argument token lists of the call whose '(' is ts[k], and the index of its ')'."""
    depth, args, cur, j = 0, [], [], k
    while j < len(ts):
        t = ts[j]
        if t in '([{':
            depth += 1
            if depth == 1:
                j += 1
                continue
        elif t in ')]}':
            depth -= 1
            if depth == 0:
                break
        if t == ',' and depth == 1:
            args.append(cur)
            cur = []
        else:
            cur.append(t)
        j += 1
    if cur:
        args.append(cur)
    return args, j


def original_reaches(ins, off, n):
    """Whether the original makes the frame address off (`addiu $r, $sp, off`) or loads or stores inside [off, off + n) of its
    frame (2026-11-13, modnative4: the cross-check of a DWARF place, frame_locals)."""
    for op, a in ins:
        if op == 'addiu' and len(a) == 3 and a[1] == '$sp' and a[0] != '$sp' and int(a[2], 0) == off:
            return True
        if len(a) == 2:
            m = re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\(\$sp\)$', a[1])
            if m and off <= int(m.group(1), 0) < off + n:
                return True
    return False


def frame_locals(toks, types, inventory, original_text, call_symbols, stats, report):
    """A local the function hands on -- `&x`, or an array passed as a pointer (`variant_family(id, f)`) -- is, on the N64, in
    the function's stack frame, and the address it hands on is there; the native code must hand on the same address, so the
    local becomes a view of its place in the frame (NVar / NArr at nm_frame_base + offset: nm_frame_base is the frame the
    entry made, lift_call.h LiftFrame, the original's size). Its offset is read from the ORIGINAL: at each call that passes
    it, the a-register's `addiu $aN, $sp, offset` (or a copy of one), the k-th call to a callee in the C paired with the k-th
    in the original when both have as many; a local no call shows is placed only if it is the one left and the original makes
    one frame address no other local took. Two offsets for one local, or one left unplaced: the function is left as it is
    (a compile error, counted). An initializer becomes stores (C's rest-of-the-array zeros included); sizeof of it, the C
    type's. Only a function's own locals (not those of a helper the compiler inlined into another: its frame is that one's).
    inventory: name -> its row; original_text(address) -> the translation; call_symbols(row) -> {C name: tester symbol}."""
    ts = [t for _, t in toks]
    for kind, fname, a, b in split_items(toks):
        if kind != 'function' or fname not in inventory:
            continue
        if '{' not in ts[a:b + 1]:
            continue
        body = ts.index('{', a)
        # the function's locals: statement-level declarations of one name
        decls, again = {}, {}
        for s in statement_starts(ts, body, b):
            if ts[s] in ('static', 'extern', 'return', 'typedef', 'goto', 'case', 'default', 'else', 'do'):
                continue
            if not (ts[s] in BUILTIN or ts[s] in types or ts[s] in ('struct', 'union', 'enum', 'const', 'NPtr', 'volatile')):
                continue
            e, depth = s, 0
            while e <= b and not (ts[e] == ';' and depth == 0):
                if ts[e] in '({[':
                    depth += 1
                elif ts[e] in ')}]':
                    depth -= 1
                e += 1
            d = declarators(ts[s:e + 1])
            if d and len(d) == 1 and d[0][0] not in decls:
                decls[d[0][0]] = (s, e, d[0])
            elif d and len(d) == 1:
                again.setdefault(d[0][0], []).append((s, e, d[0]))    # (the same name declared again, in another block)
        if not decls:
            continue
        # the ones handed on
        escaping = set()
        for k in range(body, b + 1):
            t = ts[k]
            if t in decls and not (decls[t][0] <= k <= decls[t][1]):
                prev, nxt = ts[k - 1], ts[k + 1]
                unary = prev == '&' and ts[k - 2] in ('(', ')', ',', '=', 'return', '?', ':', '{', ';')     # (')': a cast's)
                # (an array used as a value: a call's argument, an assignment's or return's value, pointer arithmetic -- not an
                #  index, a member, or a name a nested declaration reuses: `for (int c = 0; ...) x[b][c]`)
                decays = decls[t][2][3] and prev in ('(', ')', ',', '=', 'return', '+', '?', ':') and nxt in (',', ')', ';', '+', ':', '?')                     and not (prev == '(' and ts[k - 2] == 'sizeof')
                if unary or decays:
                    escaping.add(t)
        if not escaping:
            continue
        row = inventory[fname]
        ins, calls = original_calls(original_text(row['address']))
        symbols = call_symbols(row)
        # votes: (C callee, argument) -> [locals in call order]; the original's (symbol, argument) -> [offsets]
        c_side, o_side = {}, {}
        for k in range(body, b):
            if ts[k + 1] == '(' and ts[k] in symbols:
                args, _ = call_args(ts, k + 1)
                n_call = sum(1 for key in c_side if key[0] == ts[k] and key[1] == 0) if False else None
                for i, arg in enumerate(args[:4]):
                    x = arg[-1] if (len(arg) == 2 and arg[0] == '&' or len(arg) == 1) and arg[-1] in escaping else None
                    c_side.setdefault((ts[k], i), []).append(x)
        for at, sym in calls:
            for i in range(4):
                o_side.setdefault((sym, i), []).append(frame_address(ins, at + 1, '$a%d' % i))
        votes = {}
        for (cname, i), xs in c_side.items():
            os_ = o_side.get((symbols[cname], i), [])
            if len(xs) == len(os_):
                pairs = zip(xs, os_)
            elif len(set(os_)) == 1 and len({x for x in xs if x}) == 1:
                pairs = [(x, os_[0]) for x in xs]
            else:
                continue
            for x, o in pairs:
                if x is not None and o is not None:
                    votes.setdefault(x, set()).add(o)
        place = {}
        bad = [x for x, os_ in votes.items() if len(os_) > 1]
        if bad:
            report[fname] = 'two frame offsets for %s' % ', '.join(sorted(bad))
            stats['frame_locals_refused'] = stats.get('frame_locals_refused', 0) + 1
            continue
        for x, os_ in votes.items():
            place[x] = next(iter(os_))
        how = {x: 'calls' for x in place}
        # (2026-11-13, modnative4: the merge of modnative2's and modnative3's) Where the mod's own debug information puts each
        #  local -- modnative3's DWARF, prove_gen.py frame_variables: the file compiled with the mod's flags and -g, whose code
        #  is the same (dwarf_same.txt), llvm-symbolizer's FRAME -- is exact, so it comes first: the calls' votes above must
        #  agree with it (or the function is left as it is), and the placements below (the one free address, the first fit)
        #  are only for a local it gives no single place. A DWARF place the votes don't confirm must be one the original
        #  itself reaches -- an `addiu $r, $sp, off`, or a load or store inside the local -- as frame_arrays checks.
        conflict = []
        for x in sorted(escaping):
            offs = {o for fn_, v_, o in prove_gen.frame_variables(row['object'], fname) if fn_ == fname and v_ == x}
            if len(offs) != 1 or None in offs:
                continue
            off = next(iter(offs))
            if x in place:
                if place[x] != off:
                    conflict.append('%s: calls 0x%X, DWARF 0x%X' % (x, place[x], off))
                else:
                    how[x] = 'dwarf+calls'
                continue
            n_ = local_size(decls[x][2], ts[decls[x][0]:decls[x][0] + decls[x][2][1]], types) or 1
            if not original_reaches(ins, off, n_):
                continue
            place[x] = off
            how[x] = 'dwarf'
        if conflict:
            report[fname] = 'the frame offsets of the calls and of DWARF disagree: ' + '; '.join(conflict)
            stats['frame_locals_refused'] = stats.get('frame_locals_refused', 0) + 1
            continue
        left = [x for x in escaping if x not in place]
        free = [o for o in sp_offsets(ins) if o not in place.values()]
        if len(left) == 1 and len(free) == 1:
            place[left[0]] = free[0]
            how[left[0]] = 'the one free address'
            left = []
        # A local no call to a stand-in shows (one handed only to a helper the compiler inlined, or cast to an integer): the
        # original still has it in its frame, at one of the addresses it makes there that no other local took. Any such free
        # place that holds it whole, clear of the saved registers' slots and the other locals, keeps the native code's own
        # reads and writes of it consistent -- the frame is the function's own, which the tester doesn't compare (fuzz_main.cpp)
        # -- and nothing outside the function can tell which; the first that fits, in declaration order. (Two locals one
        # call shows at the same place are the compiler's sharing of a slot by locals whose lifetimes don't meet: kept.)
        if left:
            size_of = lambda x: local_size(decls[x][2], ts[decls[x][0]:decls[x][0] + decls[x][2][1]], types)
            frame = next((int(a_[2], 0) for op, a_ in ins if op == 'addiu' and a_[:2] == ['$sp', '$sp'] and int(a_[2], 0) < 0), 0)
            taken = [(o, o + 4) for o in saved_slots(ins)] + [(o, o + (size_of(x) or 4)) for x, o in place.items()]
            for x in sorted(left, key=lambda x: decls[x][0]):
                n = size_of(x)
                fit = next((o for o in free if n and 0x10 <= o and o + n <= -frame and
                            not any(o < e_ and s_ < o + n for s_, e_ in taken)), None) if n else None
                if fit is None and n:
                    # (no address the original makes fits it -- the compiler reaches the inlined helper's array at fixed
                    #  frame offsets, `lwc1 $f4, 0x30($sp)`, and makes none: any gap of the frame that holds it does, above
                    #  the parameters the calls pass on the stack, clear of the saved registers' slots and the others)
                    low = outgoing_area(ins)
                    step = 8 if n % 8 == 0 else 4
                    fit = next((o for o in range((low + step - 1) // step * step, -frame - n + 1, step)
                                if not any(o < e_ and s_ < o + n for s_, e_ in taken)), None)
                if fit is None:
                    break
                place[x] = fit
                how[x] = 'first fit'
                taken.append((fit, fit + n))
                if fit in free:
                    free.remove(fit)
            left = [x for x in left if x not in place]
        if left:
            report[fname] = 'no frame offset for %s' % ', '.join(sorted(left))
            stats['frame_locals_refused'] = stats.get('frame_locals_refused', 0) + 1
            continue
        # rewrite: each declaration, and sizeof of it
        ok = True
        edits = []
        for x, off in place.items():
            s, e, (nm, at_, end_, dims) = decls[x]
            base_type = ' '.join(t for t in ts[s:s + at_] if t not in STORAGE)
            stmt = ts[s:e + 1]
            init = stmt[stmt.index('=') + 1:-1] if '=' in stmt[at_:] else None
            if dims:
                view = 'NArr<%s%s> %s(nm_frame_base + 0x%X);' % (base_type, ''.join('[%s]' % d for d in dims[1:]), nm, off)
                stores = []
                if init is not None:
                    if init[:1] != ['{'] or init[-1:] != ['}'] or len(dims) > 1 or not re.match(r'\d+$', dims[0] or 'x'):
                        ok = False
                        break
                    items_, _ = call_args(['('] + init[1:-1] + [')'], 0)
                    if any('{' in it for it in items_) or len(items_) > int(dims[0]):
                        ok = False
                        break
                    stores = ['%s[%d] = %s;' % (nm, i, ' '.join(it)) for i, it in enumerate(items_)]
                    stores += ['%s[%d] = 0;' % (nm, i) for i in range(len(items_), int(dims[0]))]
                size = 'sizeof(%s%s)' % (base_type, ''.join('[%s]' % d for d in dims))
            else:
                if init is not None and init[:1] == ['{']:
                    ok = False
                    break
                view = 'NVar<%s> %s(nm_frame_base + 0x%X);' % (base_type, nm, off)
                stores = ['%s = %s;' % (nm, ' '.join(init))] if init is not None else []
                size = '((u32)NSize<%s>::value)' % base_type
            edits.append((s, e, ' '.join([view] + stores), nm, size))
            # (the same name declared again in another block, of the same type: at the same place -- the compiler gives
            #  locals whose lifetimes don't meet one slot -- or the function is left as it is)
            for s2, e2, d2 in again.get(x, []):
                if d2[3] != dims or ' '.join(t for t in ts[s2:s2 + d2[1]] if t not in STORAGE) != base_type or '=' in ts[s2 + d2[1]:e2]:
                    ok = False
                    break
                edits.append((s2, e2, view, nm, size))
            if not ok:
                break
        if not ok:
            report[fname] = 'a frame local with an initializer this does not rewrite'
            stats['frame_locals_refused'] = stats.get('frame_locals_refused', 0) + 1
            continue
        for s, e, new, nm, size in edits:
            for q in range(s, e + 1):
                toks[q] = [toks[q][0] if q == s else '', '']
            toks[s] = [toks[s][0], new]
            for k in range(e + 1, b):
                if ts[k] == 'sizeof':
                    if ts[k + 1] == nm:
                        toks[k] = [toks[k][0], size]
                        toks[k + 1] = [toks[k + 1][0], '']
                    elif ts[k + 1] == '(' and ts[k + 2] == nm and ts[k + 3] == ')':
                        toks[k] = [toks[k][0], size]
                        for q in (k + 1, k + 2, k + 3):
                            toks[q] = ['', '']
        report[fname] = 'frame locals: ' + ', '.join('%s at 0x%X (%s)' % (x, o, how.get(x, '?')) for x, o in sorted(place.items()))
        for x in place:
            stats['frame_locals_' + how.get(x, '?').replace(' ', '_')] = stats.get('frame_locals_' + how.get(x, '?').replace(' ', '_'), 0) + 1
        stats['frame_locals'] = stats.get('frame_locals', 0) + len(place)
    return toks


def untokenize(toks):
    return ''.join(ws + t for ws, t in toks)


def items(text):
    """The file's top-level items: [(kind, name, first line, last line)] -- 'function' (a definition), 'other'."""
    toks = tokenize(text)
    out, i, n, line = [], 0, len(toks), 1
    start_line, depth, item_toks = 1, 0, []
    for ws, t in toks:
        line += ws.count('\n')
        if not item_toks:
            start_line = line
        item_toks.append(t)
        if t in ('(', '['):
            depth += 1
        elif t in (')', ']'):
            depth -= 1
        elif t == '{':
            depth += 1
        elif t == '}':
            depth -= 1
            if depth == 0 and is_function(item_toks):
                out.append(('function', function_name(item_toks), start_line, line))
                item_toks = []
        elif t == ';' and depth == 0:
            out.append(('other', None, start_line, line))
            item_toks = []
    return out


def is_function(ts):
    """A top-level item that is a function definition: its first '{' at depth 0 comes right after a ')' (or an attribute)."""
    depth = 0
    for k, t in enumerate(ts):
        if t in ('(', '['):
            depth += 1
        elif t in (')', ']'):
            depth -= 1
        elif t == '{' and depth == 0:
            return k > 0 and ts[k - 1] == ')' and '=' not in ts[:k]
        elif t == '=' and depth == 0:
            return False
    return False


def function_name(ts):
    depth = 0
    for k, t in enumerate(ts):
        if t == '(' and depth == 0 and k > 0 and re.match(r'[A-Za-z_]\w*$', ts[k - 1]) and ts[k - 1] not in ('__attribute__', 'RECOMP_PATCH'):
            return ts[k - 1]
        if t in ('(', '['):
            depth += 1
        elif t in (')', ']'):
            depth -= 1
    return None


ERROR = re.compile(r'^(.*?):(\d+):\d+: error: (.*)$')


def category(msg):
    m = msg
    rules = [
        (r"cast from pointer to smaller type|invokes a deleted function|uses deleted function|attempt to use a deleted function|call to deleted constructor of 'NPtr<|conversion function from '.*\[\d*\]' to 'NPtr|no viable conversion from '.*(\*|\[\d*\])' to 'NPtr|cannot initialize .* with an lvalue of type '.*\[|from 'const char \[|'const char\s*\[", 'host pointer -> N64 address (string literal, host array or &local: needs a view)'),
        (r"member reference type 'NPtr<.*' is not a pointer|no member named .* in 'NPtr|member reference base type 'NRef<|no member named .* in 'NRef", 'struct through a pointer into game memory (needs a struct view)'),
        (r'cannot initialize a variable of type .*enum|assigning to .*enum|cannot initialize .* of type .* with an rvalue of type .*int', 'C enum/int conversion'),
        (r'designator|designated initializer|array designators', 'C initializer (designated)'),
        (r'cannot jump from|jump bypasses', 'goto over an initialisation'),
        (r'non-constant-expression cannot be narrowed|narrowing', 'narrowing in braces'),
        (r'incompatible pointer|cannot initialize .* with an rvalue of type .NPtr|no viable conversion from .NPtr|assigning to .NPtr.* from incompatible|no viable overloaded', 'pointer types mixed (C lets them mix with a warning)'),
        (r'invalid operands to binary expression', 'operator on an N64 pointer/value C allows'),
        (r'undeclared identifier|unknown type name|use of undeclared', 'a global of an unnamed struct type (its declaration could not become a view: needs a named struct view)'),
        (r'no matching conversion for C-style cast|cannot cast from type', 'a cast between a host value and an N64 pointer'),
        (r'asm', 'inline asm'),
    ]
    for pat, cat in rules:
        if re.search(pat, m):
            return cat
    return 'other: ' + re.sub(r"'[^']*'", "'…'", m)[:90]


DATA = None


IMAGE, PLACES, STRING_SYMBOLS, NAMED_PLACES = b'', {}, set(), set()
INVENTORY, ROWS, FRAME_REPORT = {}, {}, {}
# ---- The N64's integer division (2026-11-13, scratch/agents/modnative3) ----
# The mod's compiler puts no check after a div or divu, so on the N64 dividing by zero goes on (the quotient -1, the remainder the
# number itself) where C++'s / and % stop the PC: emit_char's `(8u << 10) / (u32)cw` with cw 0 stopped the native build in half
# its tests, the original none. Each integer / % /= %= whose divisor is not a non-zero integer literal becomes nm_div(a, b),
# nm_rem(a, b), nm_div_to(x, y), nm_rem_to(x, y) (nm_native.h: the N64's results; C's for every other divisor). Which ones and
# where their operands are comes from clang's own AST of the rewritten file (-ast-dump=json), so the precedence is clang's, not a
# guess at token level. A division through a view (NRef's /= and %=) is nm_native.h's own operator. Floats keep / as they are.
DIVISION_HELPERS = {'/': 'nm_div', '%': 'nm_rem', '/=': 'nm_div_to', '%=': 'nm_rem_to'}
INTEGER_TYPES = {'int', 'unsigned int', 'long', 'unsigned long', 'short', 'unsigned short', 'char', 'signed char', 'unsigned char', 'bool'}


def _main_file(node):
    """Whether an AST node is the file's own (its range starts in the compiled file, not in a header it includes)."""
    b = node.get('range', {}).get('begin', {})
    return 'offset' in b and 'includedFrom' not in b and 'spellingLoc' not in b and 'expansionLoc' not in b


def _constant_divisor(node):
    """A non-zero integer literal (through casts and parentheses): the division can't stop on the PC, so it stays C's /."""
    while node.get('kind') in ('ImplicitCastExpr', 'ParenExpr', 'CStyleCastExpr') and node.get('inner'):
        node = node['inner'][0]
    if node.get('kind') in ('IntegerLiteral', 'CharacterLiteral'):
        return str(node.get('value')) not in ('0', '-1')
    return node.get('kind') == 'UnaryExprOrTypeTraitExpr'      # (sizeof, never 0)


def _integer(t):
    q = t.get('desugaredQualType', t.get('qualType', ''))
    return q.replace('const ', '').replace('volatile ', '').strip() in INTEGER_TYPES


def n64_divisions(cpp, stats):
    """Rewrites cpp's integer divisions (see above) in place. stats['n64_divisions'] counts them, stats['divisions_kept'] the ones
    by a literal, stats['divisions_64'] any 64-bit one (nm_div refuses those: the N64 calls a library function)."""
    p = subprocess.Popen([CLANGXX] + CXX_FLAGS + ['-Xclang', '-ast-dump=json', cpp], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         cwd=HERE, text=True, encoding='utf8', errors='replace')
    edits = []      # (byte offset, order, bytes replaced, new text)

    def visit(n):
        if not isinstance(n, dict):
            return
        kind, op = n.get('kind'), n.get('opcode')
        if kind in ('BinaryOperator', 'CompoundAssignOperator') and op in DIVISION_HELPERS and _main_file(n) and len(n.get('inner', [])) == 2:
            left, right = n['inner']
            t = n.get('computationResultType', n['type']) if kind == 'CompoundAssignOperator' else n['type']
            q = t.get('desugaredQualType', t.get('qualType', ''))
            if (_integer(t) or 'long long' in q) and _constant_divisor(right):
                stats['divisions_kept'] = stats.get('divisions_kept', 0) + 1
            elif 'long long' in q or q in ('__int128',):
                stats['divisions_64'] = stats.get('divisions_64', 0) + 1     # (none in the mod: its IR has no 64-bit division)
            elif _integer(t) and _main_file(left) and _main_file(right):
                lb, le, rb, re_ = left['range']['begin'], left['range']['end'], right['range']['begin'], right['range']['end']
                start, left_end, right_start, end = lb['offset'], le['offset'] + le['tokLen'], rb['offset'], re_['offset'] + re_['tokLen']
                width = end - start
                edits.append((start, -width, 0, DIVISION_HELPERS[op] + '('))
                edits.append((left_end, 0, ('op', op, right_start), ', '))
                edits.append((end, width, 0, ')'))
                stats['n64_divisions'] = stats.get('n64_divisions', 0) + 1
        for c in n.get('inner', []):
            visit(c)

    # (The dump is one JSON document, hundreds of MB with the headers: each top-level declaration -- four spaces, '{' -- is read
    #  and looked at on its own, so only one is ever in memory.)
    chunk = None
    for line in p.stdout:
        if chunk is None:
            if line.rstrip() == '    {':
                chunk = [line]
            continue
        chunk.append(line)
        if line.rstrip().rstrip(',') == '    }':
            text = ''.join(chunk).rstrip().rstrip(',')
            chunk = None
            if '"includedFrom"' in text[:text.find('"range"')]:
                continue    # (a header's declaration: its place, before its range, is in a file the source includes)
            visit(json.loads(text))
    p.wait()
    if not edits:
        return
    data = open(cpp, 'rb').read()
    out, at = [], 0
    for pos, order, what, new in sorted(edits, key=lambda e: (e[0], e[1])):
        out.append(data[at:pos])
        at = pos
        if what:
            # (the operator itself, between the operands: only spaces before it)
            _, op, right_start = what
            k = data.index(op.encode(), pos, right_start)
            assert not data[pos:k].strip(), (cpp, pos, data[pos:right_start])
            out.append(b', ')
            at = k + len(op)
            # (the spaces after the operator are kept as they were, after the comma's own)
            while at < right_start and data[at:at + 1] in (b' ', b'\t'):
                at += 1
        else:
            out.append(new.encode())
    out.append(data[at:])
    open(cpp, 'wb').write(b''.join(out))


def census():
    global DATA, IMAGE, PLACES, prove_gen
    CONTENT_PLACED.clear()
    import prove_gen        # (the original's text and its calls: frame_locals)
    DATA = map_data()
    STRING_SYMBOLS.update(a_ for n_, (a_, _) in DATA[0].items() if re.match(r'\$?\.(L\.)?str', n_))
    NAMED_PLACES.update(a_ for a_, _ in DATA[0].values())
    INVENTORY.update(json.load(open(os.path.join(HERE, 'inventory.json'))))
    for a_, r_ in INVENTORY.items():
        ROWS.setdefault(r_['object'], {})[r_['name']] = dict(r_, address=a_)
    # (the mod's image and, per file, the unnamed places its original code makes: for string_views)
    IMAGE = mod_image()
    for r in json.load(open(os.path.join(HERE, 'inventory.json'))).values():
        PLACES.setdefault(r['object'], set()).update(int(x[1:], 16) for x in r['data'] if x.startswith('?'))
        # (and the named ones: the compiler names some literals itself, `$.str`)
        PLACES[r['object']].update(DATA[0][x][0] for x in r['data'] if x in DATA[0])
    os.makedirs(PP, exist_ok=True)
    result, totals = {}, {}
    for src in SOURCES:
        # (the mod's RECOMP_IMPORT makes _Pragma diagnostics, which -E leaves as #pragma lines inside declarations: they say
        #  nothing about what the C does, so they go)
        text = re.sub(r'^[ \t]*#[ \t]*pragma\b.*$', '', preprocess(src), flags=re.M)
        stats = {}
        text, stats['anonymous_structs_named'] = name_anonymous_structs(text)
        # (2026-11-14, modnative7) The mod's `long` is 32 bits (MIPS o32), as int is; on Linux / Mac / the Deck a C++ long is 64:
        #  written as the 32-bit type it is, so the native text means the same on every platform (only two imports' declarations
        #  have one: recomp_set_config_u32's value, recomp_get_config_u32's result). `long long` is left as it is.
        text, stats['long_as_32'] = re.subn(r'\bunsigned long\b(?!\s+long)', 'unsigned int', text)
        # (2026-11-13, modnative2) A pointer to an array, `const u16 (*w)[2] = ...`: an NPtr whose element is the array (each
        #  step a whole row, as in C), NPtr<u16[2]>
        text, stats['array_pointers'] = re.subn(r'\b(?:const\s+)?(u8|s8|u16|s16|u32|s32|int|float|char)\s*\(\s*\*\s*(\w+)\s*\)\s*\[(\w+)\]',
                                                r'NPtr<\1[\3]> \2', text)
        toks = tokenize(text)
        types = typedef_names(toks)
        # (each pass on a fresh tokenization of the one before: a pass blanks or merges tokens)
        toks = tokenize(untokenize(drop_attributes(toks)))
        stats['split_statements'] = split_multi_declarations(toks, types)
        toks = tokenize(untokenize(toks))
        stats['tentative_repeats_dropped'] = drop_tentative_repeats(toks)      # (2026-11-14, modwhole)
        toks = tokenize(untokenize(toks))
        rewrite_pointers(toks, types, stats)
        toks = tokenize(untokenize(toks))
        stats['switch_declarations'] = switch_declarations(toks)
        toks = tokenize(untokenize(toks))
        stats['array_parameters'] = array_parameters(toks, types, set(ROWS.get(os.path.splitext(os.path.basename(src))[0], {})))
        toks = tokenize(untokenize(toks))
        bound = {}
        VIEW_TYPES.clear()
        bind_globals(toks, DATA[0], DATA[1], bound)
        toks = tokenize(untokenize(toks))
        stats['block_externs_removed'] = remove_block_externs(toks, {n for n, v in bound.items() if v.startswith(('view', 'private'))})
        bind_local_statics(toks, DATA[0], DATA[1], stats)
        toks = tokenize(untokenize(toks))
        frame_locals(toks, types, ROWS.get(os.path.splitext(os.path.basename(src))[0], {}), prove_gen.original_text,
                     lambda row: {c: s_[0] for c, s_ in prove_gen.callees(row['address'], row, INVENTORY).items()}, stats, FRAME_REPORT)
        toks = tokenize(untokenize(toks))
        string_views(toks, IMAGE, PLACES.get(os.path.splitext(os.path.basename(src))[0], set()), stats)
        toks = tokenize(untokenize(toks))
        rewrite_sizeof(toks, types, {n: VIEW_TYPES.get(n) for n, v in bound.items() if v.startswith('view')}, DATA[0], stats)
        toks = tokenize(untokenize(toks))
        stats['struct_views'], stats['structs_without_view'] = struct_views(toks)
        stats['views'] = sum(1 for v in bound.values() if v.startswith('view'))
        if CONTENT_PLACED:
            stats['content_placed'] = sorted(CONTENT_PLACED)       # (2026-11-14, modwhole: content_place)
            CONTENT_PLACED.clear()
        stats['private'] = sum(1 for v in bound.values() if v.startswith('private'))
        stats['left_on_pc'] = sorted(n for n, v in bound.items() if 'left as' in v)
        out_text = untokenize(toks)
        base = os.path.splitext(os.path.basename(src))[0]
        cpp = os.path.join(PP, base + '.native.cpp')
        open(cpp, 'w', encoding='utf8', newline='\n').write(out_text)
        if os.environ.get('NM_N64_DIVISION', '1') == '1':     # (NM_N64_DIVISION=0: without it, to compare. 2026-11-13)
            n64_divisions(cpp, stats)
            out_text = open(cpp, encoding='utf8').read()
        p = subprocess.run([CLANGXX] + CXX_FLAGS + [cpp], capture_output=True, text=True, cwd=HERE)
        errors = [(int(m.group(2)), m.group(3)) for m in (ERROR.match(l) for l in p.stderr.splitlines()) if m and os.path.basename(m.group(1)) == base + '.native.cpp']
        header_errors = [l for l in p.stderr.splitlines() if ' error: ' in l and base + '.native.cpp' not in l]
        # (part 1 of the Linux work, 2026-11-14: without the folder the project is in -- census.json then names no machine)
        header_errors = [l.replace(DEV + os.sep, '').replace(DEV.replace(os.sep, '/') + '/', '') for l in header_errors]
        if 'fatal error' in p.stderr:
            # (2026-11-14, modnative7: a missing header or file stops the compile before any function is looked at -- it would
            #  otherwise read as "every function compiles", and the division rewriting above would have had no syntax tree)
            raise SystemExit('native.py census: %s: the compile stopped: %s' % (base, [l for l in p.stderr.splitlines() if 'fatal error' in l][:2]))
        its = items(out_text)
        funcs = [(name, a, b) for kind, name, a, b in its if kind == 'function']
        per = {name: [] for name, _, _ in funcs}
        outside = []
        for line, msg in errors:
            owner = next((name for name, a, b in funcs if a <= line <= b), None)
            (per[owner] if owner else outside).append(msg)
        ts_all = tokenize(out_text)
        sizeofs = {}
        for kind, name, a, b in split_items(ts_all):
            if kind == 'function':
                k = sizeof_of_expression([t for _, t in ts_all[a:b + 1]], types)
                if k:
                    sizeofs[name] = k
        result[base] = {'sizeof_expr': sizeofs, 'stats': stats, 'functions': {n: sorted(set(category(m) for m in ms)) for n, ms in per.items()},
                        'raw': {n: ms[:6] for n, ms in per.items() if ms}, 'outside': outside[:20], 'header_errors': header_errors[:5]}
        bad = sum(1 for ms in per.values() if ms)
        print('%-22s %4d functions, %4d compile, %4d with errors; %d errors outside functions; %s' % (
            base, len(per), len(per) - bad, bad, len(outside), stats), flush=True)
    json.dump(result, open(os.path.join(HERE, 'census.json'), 'w'), indent=1)
    json.dump(FRAME_REPORT, open(os.path.join(HERE, 'frame_locals.json'), 'w'), indent=1)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'census':
        census()
    else:
        print(__doc__)
