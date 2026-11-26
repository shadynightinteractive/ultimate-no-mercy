#!/usr/bin/env python3
"""hdrlayout.py: how each extras hole's bytes follow from its game-data header's TEXT, for the game to fill them without a
compiler (phase3 p3hdrfill, 2026-11-19).

  python tools/firstrun/hdrlayout.py [--build mod/build/mod_six] [--headers mod/src] [--out scratch/firstrun_plan]

mkplan.py finds the mod's HOLES: tables of game data the release ships zeroed. The EXTRAS holes (the ported Superstars, the
arenas, the themes' index ...) are tables a game-data header defines (mod/src/*.h, made by mod/gen and the handlers from the
other games' ROMs); phase 3 makes every such header's text natively from the player's ROMs. This tool says, for every extras
hole, how that text becomes the hole's bytes -- the compiler's work, written down once as a LAYOUT:

  - the symbol's C type laid out the MIPS o32 big-endian way (u8/s8/char 1, u16/s16/short 2, u32/s32/int/long/float and
    pointers 4, long long/double 8 aligned 8; a struct aligned to its widest member, its size a multiple of that; arrays,
    nested structs and arrays), from the type as the build's compiler sees it (the header preprocessed in the .c file that
    includes it, with the mod's include paths and defines -- build_mod.ps1 -- and read with pycparser);
  - each LEAF of the symbol's initializer, in text order, put at its byte offset with its kind: INT (a 1/2/4/8-byte integer,
    big-endian, truncated as C does), F32 / F64 (IEEE single / double), STR (a string literal filling a char array: one
    leaf), PTR (an address the link chose: a string, &sym, an array's name -- relocated, so it is the link's, not data:
    mkplan's extras_link_words fills these words in the base image already, and the fill takes each from there: the layout
    says only where it is). Braces only group (C's brace elision followed); bytes no leaf covers are zero.

The layouts go to <out>/firstrun/hdrfill.bin (little-endian; only OUR numbers -- offsets, sizes, leaf indices -- never a game
byte, and since version 2 (relclean 2026-11-19) not the link's addresses either: the package audit found long runs of them
equal to the never-shipped modextra.bin, which holds the same words; it ships in the release's firstrun\\ folder):

  "UNMHFIL1", u32 version (2), u32 the image's size, u32 holes, then per hole:
    u32 start (image offset), u32 size, i32 the symbol's start relative to the hole (0, or -N for a hole "[N..]" that starts
    N bytes into it), u32 the symbol's size, u32 flags (1 COMPUTED: the first-start builder gives this hole's bytes itself,
    no ops -- arcade_extras_in, the extras' source bits; 2 STRING: a string the symbol's pointers point at, its ops STR leaves
    at offset 0 -- see below), u8 n + the header's name (n bytes), u8 n + the symbol's name,
    u32 ops, then per op (24 bytes): u8 kind (1 INT, 2 F32, 3 F64, 4 STR, 5 PTR), 3 x u8 0, u32 size (INT 1/2/4/8, F32 4, F64 8, STR the char
    array's length, PTR 4), u32 first leaf, u32 count, u32 offset (relative to the symbol's start), u32 stride: leaves
    first .. first + count - 1 at offset + k * stride. (Version 1 followed a PTR op with its count u32 values.)
    Only ops that touch the hole are kept; a fill renders them and keeps the hole's window.

THE TEXT RULES (exe/.../romimport/ri_hdrfill.cpp follows the same ones, and this tool checks them by doing the fill itself):
comments (/* */, //) are skipped; lines starting with # are preprocessor lines (object-like #define NAME body: a name a leaf
may use); a symbol's initializer is found at brace depth 0 as  NAME [dims...] [__attribute__((...))] = ...  (exactly once in
its header); an initializer is a { } group or one scalar leaf up to , or ; -- groups split at commas outside ( ); a leaf is
numbers (decimal / hex / octal, u/l suffixes; floats, f suffix = single), char literals, string literals (adjacent ones
joined), casts to the basic types, defines, and + - * / % << >> & | ^ ~ ! ( ).

VERIFY: for EVERY extras hole the text, through the layout, gives the build's bytes (mod_binary.bin) exactly; every
relocated word in the hole is a PTR leaf and every PTR leaf a relocated word; a PTR's target is checked against the build
(a string's bytes, a symbol's address + offset). Anything it can't map stops it with the hole, header and construct named.
THE STRINGS (relclean 2026-11-19, the user's decision of 2026-11-19): a table's string -- a name of ext_late's or of
xarena_names' -- is a pointer in the table and a literal the link put in .rodata.str*, outside the table. mkplan.find_holes
makes each one that only that table refers to a hole of its own (mkplan.extras_strings: the rules there); here it is laid
out as a STRING hole of the table's symbol: one STR op per leaf of the table that points at it (every one a string literal
with exactly the build's bytes), at offset 0 -- the fill writes the string the header's text has. The report says which
strings stay in the image (one shared with code: the empty string).
Exit 0 when all are the same. mkplan.py calls make() at its end (hdrfill.bin next to plan.bin, hdrfill_report.txt next to
plan_report.txt); this command is the same, standalone (no ROM needed). tools/firstrun/hdrfill_check.py then runs the
game's C++ fill on the current headers against the build's modextra.bin.
"""
import argparse, os, re, struct, subprocess, sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import modelf   # noqa: E402

MAGIC = b'UNMHFIL1'
VERSION = 2          # (relclean 2026-11-19: 2 -- a PTR op carries no value; STRING holes)
K_INT, K_F32, K_F64, K_STR, K_PTR = 1, 2, 3, 4, 5
KIND_NAME = {K_INT: 'INT', K_F32: 'F32', K_F64: 'F64', K_STR: 'STR', K_PTR: 'PTR'}
# (the extras holes that are not in a generated header: the mod's own source files, mkplan's EXTRAS_FLAG / EXTRAS_IN_SOURCE)
SOURCE_OF = {'arcade_extras_in': 'extras.c'}   # (handsrom 2026-11-25: hands.c's wm_pose is wmport_files.h's wmh_pose now)
# (holes whose bytes the first-start builder COMPUTES, not text: flag F_COMPUTED, no ops -- the fill asks its caller.
#  arcade_extras_in: the set of source bits, EXTRAS_* in arcade.h -- which source ROMs the player has; phase3 2026-11-19)
COMPUTED = {'arcade_extras_in': "the extras' source bits (EXTRAS_*, arcade.h): which source ROMs the player has"}
F_COMPUTED = 1
F_STRING = 2         # (relclean 2026-11-19: a string an extras table points at -- mkplan.extras_strings)


class Stop(Exception):
    """a construct this tool can't map: said plainly (hole, header, what), never guessed around"""


# =====================================================================================================================
# The header TEXT, as the game's fill reads it (ri_hdrfill.cpp: the same rules)
# =====================================================================================================================
TOKEN = re.compile(r'''\s+|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|[A-Za-z_]\w*|\.?\d(?:[eEpP][+-]|[\w.])*|<<|>>|\S''')


def strip_text(text):
    """(the text with comments and preprocessor lines blanked, {define name: body}) -- comments become one space (a string
    or char literal is skipped whole, so a // in a string stays); a line whose first non-blank is # is a preprocessor line:
    an object-like #define is kept as a define, the line itself blanked"""
    out = []; i = 0; n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2); j = n if j < 0 else j + 2
            out.append(' '); i = j; continue
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i); j = n if j < 0 else j
            out.append(' '); i = j; continue
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1; continue
        out.append(c); i += 1
    s = ''.join(out)
    defines = {}
    lines = s.split('\n')
    for k, line in enumerate(lines):
        if line.lstrip().startswith('#'):
            m = re.match(r'\s*#\s*define\s+([A-Za-z_]\w*)(\(?)(.*)$', line)
            if m: defines[m.group(1)] = None if m.group(2) else m.group(3).strip()   # (None: a function-like macro)
            lines[k] = ''
    return '\n'.join(lines), defines


def tokens(s):
    return [t for t in TOKEN.findall(s) if not t.isspace()]


def index_definitions(toks):
    """{name: [token index of its initializer's first token]} -- every  NAME [dims] [__attribute__((..))] =  at depth 0"""
    out = {}
    depth = 0; i = 0; n = len(toks)
    while i < n:
        t = toks[i]
        if t in '{([': depth += 1
        elif t in '})]': depth -= 1
        elif depth == 0 and (t[0].isalpha() or t[0] == '_') and t != '__attribute__':
            j = i + 1
            while j < n and toks[j] == '[':                 # (its dimensions)
                d = 0
                while j < n:
                    d += toks[j] == '['; d -= toks[j] == ']'; j += 1
                    if d == 0: break
            while j < n and toks[j] == '__attribute__':     # (__attribute__((aligned(8))) and the like)
                j += 1; d = 0
                while j < n:
                    d += toks[j] == '('; d -= toks[j] == ')'; j += 1
                    if d == 0: break
            if j < n and toks[j] == '=' and not (j + 1 < n and toks[j + 1] == '='):
                out.setdefault(t, []).append(j + 1)
        i += 1
    return out


class Group:
    def __init__(self): self.items = []


class Leaf:
    def __init__(self, toks, index): self.toks, self.index = toks, index
    def text(self): return ' '.join(self.toks)
    def is_string(self): return bool(self.toks) and all(t[0] == '"' for t in self.toks)


def split_initializer(toks, i):
    """the initializer from token i: a Group tree of Leaf (numbered in text order), and the token index after it"""
    count = [0]

    def leaf(ts):
        lf = Leaf(ts, count[0]); count[0] += 1; return lf

    def scalar(i, stops):
        j = i; d = 0
        while j < len(toks):
            t = toks[j]
            if d == 0 and t in stops: break
            if t in '([': d += 1
            elif t in ')]': d -= 1
            elif t in '{}': raise Stop('a brace inside a leaf: %s' % ' '.join(toks[i:j + 1]))
            j += 1
        return toks[i:j], j

    def group(i):              # toks[i] == '{'
        g = Group(); i += 1
        while True:
            if i >= len(toks): raise Stop('an initializer not closed')
            if toks[i] == '}': return g, i + 1
            if toks[i] == '{': sub, i = group(i); g.items.append(sub)
            else:
                ts, i = scalar(i, (',', '}'))
                if not ts: raise Stop('an empty leaf (two commas)')
                g.items.append(leaf(ts))
            if toks[i] == ',': i += 1
            elif toks[i] != '}': raise Stop('after a leaf: %r' % toks[i])

    if toks[i] == '{': return group(i)
    ts, j = scalar(i, (',', ';'))
    return leaf(ts), j


# ---- a leaf's value -------------------------------------------------------------------------------------------------
# A value is (number, type): type 'i32' 'u32' 'i64' 'u64' (int, unsigned, long long, unsigned long long: MIPS o32's long is
# 32 bits) or 'f32' 'f64'. C's rules: a literal's type by its value, base and suffix; the usual arithmetic conversions; wrap.
INT_T = {'i32': (32, True), 'u32': (32, False), 'i64': (64, True), 'u64': (64, False)}
CAST_T = {('u8',): ('int', 1, False), ('s8',): ('int', 1, True), ('u16',): ('int', 2, False), ('s16',): ('int', 2, True),
          ('u32',): ('int', 4, False), ('s32',): ('int', 4, True), ('u64',): ('int', 8, False), ('s64',): ('int', 8, True),
          ('f32',): ('f32',), ('f64',): ('f64',), ('float',): ('f32',), ('double',): ('f64',),
          ('char',): ('int', 1, True), ('signed', 'char'): ('int', 1, True), ('unsigned', 'char'): ('int', 1, False),
          ('short',): ('int', 2, True), ('unsigned', 'short'): ('int', 2, False), ('int',): ('int', 4, True),
          ('signed',): ('int', 4, True), ('unsigned',): ('int', 4, False), ('unsigned', 'int'): ('int', 4, False),
          ('long',): ('int', 4, True), ('unsigned', 'long'): ('int', 4, False), ('long', 'long'): ('int', 8, True),
          ('unsigned', 'long', 'long'): ('int', 8, False)}


def wrap(v, t):
    bits, signed = INT_T[t]
    v &= (1 << bits) - 1
    if signed and v >> (bits - 1): v -= 1 << bits
    return v


def f32_round(x):
    """a Fraction or float rounded to the nearest IEEE single (ties to even) -- as clang reads a literal with an f suffix"""
    q = Fraction(x)
    c = struct.unpack('>f', struct.pack('>f', float(q)))[0]
    bits = struct.unpack('>I', struct.pack('>f', c))[0]
    cands = [c]
    for b in (bits - 1, bits + 1):
        if 0 <= b < 0x100000000 and (b & 0x7F800000) != 0x7F800000: cands.append(struct.unpack('>f', struct.pack('>I', b))[0])
    best = min(cands, key=lambda f: (abs(Fraction(f) - q), struct.unpack('>I', struct.pack('>f', f))[0] & 1))
    return -0.0 if best == 0 and str(x).lstrip().startswith('-') else best


def int_literal(t):
    m = re.match(r'^(0[xX][0-9A-Fa-f]+|0[0-7]*|[1-9]\d*)([uUlL]*)$', t)
    if not m: raise Stop('a number: %s' % t)
    body, suf = m.group(1), m.group(2).lower()
    v = int(body, 16) if body[:2].lower() == '0x' else int(body, 8) if body[0] == '0' else int(body)
    dec = body[0] != '0'                       # (a decimal literal: int, then long long; hex / octal may be unsigned)
    u = 'u' in suf; ll = 'll' in suf
    if suf not in ('', 'u', 'l', 'ul', 'lu', 'll', 'ull', 'llu'): raise Stop('a number suffix: %s' % t)
    order = []
    if not u and not ll: order += ['i32'] + ([] if dec else ['u32'])
    if u and not ll: order += ['u32']
    if not u: order += ['i64'] + ([] if dec else ['u64'])
    order += ['u64']
    for ty in order:
        bits, signed = INT_T[ty]
        if v < (1 << (bits - 1 if signed else bits)): return (v, ty)
    raise Stop('a number too big: %s' % t)


ESC = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, "'": 39, '"': 34, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '?': 63}


def unescape(body):
    """a string or char literal's inside -> its bytes (C's escapes; the text is ASCII / Latin-1)"""
    out = bytearray(); i = 0
    while i < len(body):
        c = body[i]
        if c != '\\': out.append(ord(c)); i += 1; continue
        e = body[i + 1]
        if e == 'x':
            j = i + 2
            while j < len(body) and body[j] in '0123456789abcdefABCDEF': j += 1
            out.append(int(body[i + 2:j], 16) & 0xFF); i = j
        elif e in '01234567':
            j = i + 1
            while j < len(body) and j < i + 4 and body[j] in '01234567': j += 1
            out.append(int(body[i + 1:j], 8) & 0xFF); i = j
        elif e in ESC: out.append(ESC[e]); i += 2
        else: raise Stop('an escape: \\%s' % e)
    return bytes(out)


def string_bytes(toks):
    return b''.join(unescape(t[1:-1]) for t in toks)


class Eval:
    """a leaf's tokens -> (number, type); identifiers are the header's object-like defines (an unknown one: NotConst)"""
    class NotConst(Exception): pass

    def __init__(self, defines): self.defines = defines; self.depth = 0

    def value(self, toks):
        self.t = list(toks); self.i = 0
        v = self.expr(0)
        if self.i != len(self.t): raise Stop('a leaf: %s' % ' '.join(toks))
        return v

    def peek(self): return self.t[self.i] if self.i < len(self.t) else None

    BIN = {'|': 1, '^': 2, '&': 3, '<<': 5, '>>': 5, '+': 6, '-': 6, '*': 7, '/': 7, '%': 7}

    def expr(self, prec):
        a = self.unary()
        while True:
            op = self.peek()
            p = self.BIN.get(op)
            if p is None or p <= prec: return a
            self.i += 1
            a = self.binary(op, a, self.expr(p))

    def cast_type(self):
        """at '(': a type name and ')' -> its cast, else None (and the position as it was)"""
        j = self.i + 1; words = []
        while j < len(self.t) and self.t[j] in ('const', 'volatile', 'unsigned', 'signed', 'char', 'short', 'int', 'long',
                                                'float', 'double', 'u8', 's8', 'u16', 's16', 'u32', 's32', 'u64', 's64',
                                                'f32', 'f64', 'void'):
            if self.t[j] not in ('const', 'volatile'): words.append(self.t[j])
            j += 1
        ptr = False
        while j < len(self.t) and self.t[j] == '*': ptr = True; j += 1
        if not words or j >= len(self.t) or self.t[j] != ')': return None
        if ptr: ct = ('int', 4, False)                  # (a pointer: an address, 32 bits)
        elif tuple(words) in CAST_T: ct = CAST_T[tuple(words)]
        else: raise Stop('a cast: (%s)' % ' '.join(words))
        self.i = j + 1
        return ct

    def unary(self):
        t = self.peek()
        if t is None: raise Stop('a leaf cut short')
        if t == '(':
            ct = self.cast_type()
            if ct: return convert(self.unary(), ct)
            self.i += 1; v = self.expr(0)
            if self.peek() != ')': raise Stop('a ( not closed')
            self.i += 1; return v
        if t == '&': raise Eval.NotConst('&')          # (an address: &x[i] -- the link's)
        if t in ('-', '+', '~', '!'):
            self.i += 1; v, ty = self.unary()
            if t == '+': return promote((v, ty))
            if t == '!': return (int(not v), 'i32')
            if ty in ('f32', 'f64'):
                if t == '~': raise Stop('~ on a float')
                return (-v, ty)
            v, ty = promote((v, ty))
            return (wrap(-v if t == '-' else ~v, ty), ty)
        self.i += 1
        if t[0] == "'":
            b = unescape(t[1:-1])
            if len(b) != 1: raise Stop('a char literal: %s' % t)
            return (b[0] - 256 if b[0] >= 128 else b[0], 'i32')     # (char is signed on MIPS: '\xFF' is -1)
        if t[0].isdigit() or t[0] == '.':
            if re.match(r'^0[xX]', t) and not re.search(r'[pP]', t) or re.match(r'^\d+[uUlL]*$', t): return int_literal(t)
            m = re.match(r'^(\d*\.?\d*(?:[eE][+-]?\d+)?)([fFlL]?)$', t)
            if not m or not m.group(1): raise Stop('a number: %s' % t)
            if m.group(2) in ('f', 'F'): return (f32_round(Fraction(m.group(1))), 'f32')
            if m.group(2): raise Stop('a long double: %s' % t)
            return (float(m.group(1)), 'f64')
        if t[0].isalpha() or t[0] == '_':
            body = self.defines.get(t, 0)
            if body == 0: raise Eval.NotConst(t)
            if body is None: raise Stop('a function-like macro in a leaf: %s' % t)
            if self.depth > 32: raise Stop('a define that refers to itself: %s' % t)
            sub = Eval(self.defines); sub.depth = self.depth + 1
            return sub.value(tokens(body))
        raise Stop('in a leaf: %r' % t)

    def binary(self, op, a, b):
        if a[1] in ('f32', 'f64') or b[1] in ('f32', 'f64'):
            if op not in '+-*/': raise Stop('%s on a float' % op)
            ty = 'f64' if 'f64' in (a[1], b[1]) else 'f32'
            x, y = float(a[0]), float(b[0])
            r = x + y if op == '+' else x - y if op == '-' else x * y if op == '*' else x / y
            return (f32_round(r) if ty == 'f32' else r, ty)
        if op in ('<<', '>>'):
            (x, ty), (y, _) = promote(a), promote(b)
            if not 0 <= y < INT_T[ty][0]: raise Stop('a shift by %d' % y)
            if op == '<<': return (wrap(x << y, ty), ty)
            return (wrap((x if INT_T[ty][1] else x & ((1 << INT_T[ty][0]) - 1)) >> y, ty), ty)
        ty = usual(promote(a)[1], promote(b)[1])
        x, y = wrap(a[0], ty), wrap(b[0], ty)
        if op in '/%':
            if y == 0: raise Stop('a division by zero')
            q = abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1)       # (C: toward zero)
            r = q if op == '/' else x - q * y
        else: r = {'+': x + y, '-': x - y, '*': x * y, '&': x & y, '|': x | y, '^': x ^ y}[op]
        return (wrap(r, ty), ty)


def promote(v):
    return v     # (every integer value here is int or wider already: a char literal is an int, a cast's result is widened)


def usual(a, b):
    """the usual arithmetic conversions of two integer types (int 32, long long 64)"""
    if a == b: return a
    ba, sa = INT_T[a]; bb, sb = INT_T[b]
    if sa == sb: return a if ba >= bb else b
    u, s = (a, b) if not sa else (b, a)
    if INT_T[u][0] >= INT_T[s][0]: return u
    return s                                  # (a signed long long holds every unsigned int)


def convert(v, ct):
    """a value converted to a cast's type (C's conversion); the result widened back to an int/float value type"""
    x, ty = v
    if ct[0] in ('f32', 'f64'):
        if ct[0] == 'f64': return (float(x), 'f64')
        return (f32_round(x if isinstance(x, float) else Fraction(x)), 'f32')     # (an int straight to single: one rounding)
    _, size, signed = ct
    if ty in ('f32', 'f64'): x = int(x)          # (toward zero)
    bits = 8 * size
    x &= (1 << bits) - 1
    if signed and x >> (bits - 1): x -= 1 << bits
    if size == 8: return (x, 'i64' if signed else 'u64')
    return (x, 'i32' if signed or size < 4 else 'u32')


def scalar_bytes(v, kind, size):
    """a value written as a scalar of the layout (big-endian, truncated as C converts)"""
    x, ty = v
    if kind == K_INT:
        if ty in ('f32', 'f64'): x = int(x)
        return (x & ((1 << (8 * size)) - 1)).to_bytes(size, 'big')
    if kind == K_F32: return struct.pack('>f', f32_round(x) if ty not in ('f32',) else x)
    if kind == K_F64: return struct.pack('>d', float(x))
    raise Stop('kind %d' % kind)


# =====================================================================================================================
# The C types, as the build's compiler sees them (the header preprocessed in its .c file; pycparser)
# =====================================================================================================================
class Ty:
    """a laid-out type: kind 'int' 'f32' 'f64' 'ptr' 'array' 'struct'; size, align; int: signed; array: elem, n (None:
    unknown, from the initializer); struct: fields [(name, Ty, offset)]; char: a char array is filled by a string"""
    def __init__(self, kind, size, align, **kw):
        self.kind, self.size, self.align = kind, size, align
        self.__dict__.update(kw)

    def scalar(self): return self.kind in ('int', 'f32', 'f64', 'ptr')


BASIC = {('char',): (1, True), ('signed', 'char'): (1, True), ('unsigned', 'char'): (1, False), ('short',): (2, True),
         ('short', 'int'): (2, True), ('signed', 'short'): (2, True), ('unsigned', 'short'): (2, False),
         ('unsigned', 'short', 'int'): (2, False), ('int',): (4, True), ('signed',): (4, True), ('signed', 'int'): (4, True),
         ('unsigned',): (4, False), ('unsigned', 'int'): (4, False), ('long',): (4, True), ('long', 'int'): (4, True),
         ('unsigned', 'long'): (4, False), ('long', 'long'): (8, True), ('unsigned', 'long', 'long'): (8, False),
         ('signed', 'long', 'long'): (8, True), ('long', 'long', 'int'): (8, True), ('_Bool',): (1, False)}


def find_cc():
    for c in (os.path.join(SRC, 'toolchain', 'zig', 'zig.exe'), os.path.join(SRC, 'toolchain', 'llvm-mingw', 'bin', 'clang.exe')):
        if os.path.exists(c): return [c, 'clang'] if c.endswith('zig.exe') else [c]
    return ['clang']


def chunks(pre):
    """the preprocessed TU's top-level declarations: [(origin file, text)] -- split at ; and at a function body's } at
    depth 0 (line markers say which file each comes from)"""
    out = []; cur = []; depth = 0; origin = None; start_origin = None
    for line in pre.split('\n'):
        if line.startswith('#'):
            m = re.match(r'#\s*\d+\s+"([^"]*)"', line)
            if m: origin = m.group(1).replace('\\\\', '\\')
            continue
        for m in TOKEN.finditer(line):
            t = m.group(0)
            if depth == 0 and not cur and t.isspace(): continue
            if not cur: start_origin = origin
            cur.append(t)
            if t in '{(': depth += 1
            elif t in '})':
                depth -= 1
                if depth == 0 and t == '}':
                    # (a function's body ends a declaration with no ;)
                    k = len(cur) - 2; d = 0
                    while k >= 0:
                        d += cur[k] == '}'; d -= cur[k] == '{'
                        if d < 0: break
                        k -= 1
                    j = k - 1
                    while j >= 0 and cur[j].isspace(): j -= 1
                    if j >= 0 and cur[j] == ')': out.append((start_origin, ''.join(cur))); cur = []
            elif t == ';' and depth == 0: out.append((start_origin, ''.join(cur))); cur = []
        if cur: cur.append('\n')
    return out


def clean(decl):
    """a declaration made plain C for pycparser: no __attribute__((..)), __extension__, inline or asm labels"""
    out = []; toks = TOKEN.findall(decl); i = 0
    while i < len(toks):
        t = toks[i]
        if t in ('__attribute__', '__asm__', 'asm', '__asm'):
            i += 1; d = 0
            while i < len(toks) and (toks[i].isspace() or toks[i] == '('):
                if toks[i] == '(': break
                i += 1
            while i < len(toks):
                d += toks[i] == '('; d -= toks[i] == ')'; i += 1
                if d == 0: break
            out.append(' '); continue
        if t in ('__extension__', '__inline', '__inline__', 'inline', '__restrict', 'restrict', '__volatile__'):
            out.append(' '); i += 1; continue
        out.append(t); i += 1
    return ''.join(out)


class Types:
    """the typedefs and struct tags of one translation unit, and the declared type of each name in a header"""
    def __init__(self, pre, header):
        from pycparser import c_parser, c_ast
        self.c_ast = c_ast
        self.typedefs, self.tags, self.decls = {}, {}, {}
        self.parser = c_parser.CParser()
        stubs = []
        hb = os.path.basename(header).lower()
        for origin, text in chunks(pre):
            first = text.split(None, 1)[0] if text.strip() else ''
            from_header = origin is not None and os.path.basename(origin).lower() == hb
            if first == 'typedef' or (first in ('struct', 'union', 'enum') and '{' in text and '=' not in text.split('{')[0]):
                body = clean(text).strip()
                ast = self.parse(stubs, body)
                if ast is None: continue
                for node in ast:
                    self.note_tags(node.type if hasattr(node, 'type') else node)
                    if isinstance(node, c_ast.Typedef):
                        self.typedefs[node.name] = node.type; stubs.append('typedef int %s;' % node.name)
            elif from_header and '=' in text:
                # (a definition in the header: its declarator, the initializer cut off at the first = at depth 0)
                d = 0; cut = None
                for k, ch in enumerate(text):
                    if ch in '([{': d += 1
                    elif ch in ')]}': d -= 1
                    elif ch == '=' and d == 0: cut = k; break
                if cut is None: continue
                ast = self.parse(stubs, clean(text[:cut]).strip() + ';')
                if ast is None: continue
                for node in ast:
                    if isinstance(node, c_ast.Decl) and node.name: self.decls.setdefault(node.name, node.type)
                    self.note_tags(getattr(node, 'type', None))

    def parse(self, stubs, body):
        try:
            return self.parser.parse('\n'.join(stubs) + '\n' + body + '\n').ext[len(stubs):]
        except Exception:
            return None     # (a declaration pycparser can't read: a function's, a GNU form -- only an error if it's needed)

    def note_tags(self, t):
        c = self.c_ast
        while isinstance(t, (c.TypeDecl, c.PtrDecl, c.ArrayDecl)): t = t.type
        if isinstance(t, (c.Struct, c.Union)) and t.name and t.decls is not None: self.tags[(type(t).__name__, t.name)] = t

    def layout(self, t, where):
        """a pycparser type -> Ty (MIPS o32)"""
        c = self.c_ast
        if isinstance(t, c.TypeDecl): return self.layout(t.type, where)
        if isinstance(t, c.PtrDecl): return Ty('ptr', 4, 4)
        if isinstance(t, c.ArrayDecl):
            e = self.layout(t.type, where)
            n = None if t.dim is None else self.const(t.dim, where)
            return Ty('array', None if n is None else e.size * n, e.align, elem=e, n=n)
        if isinstance(t, c.IdentifierType):
            names = tuple(x for x in t.names if x not in ('const', 'volatile'))
            if names in BASIC:
                size, signed = BASIC[names]
                if names == ('_Bool',): raise Stop('%s: a _Bool' % where)
                return Ty('int', size, size, signed=signed, char=size == 1 and 'char' in names)
            if names == ('float',): return Ty('f32', 4, 4)
            if names in (('double',), ('long', 'double')): return Ty('f64', 8, 8)
            if len(names) == 1 and names[0] in self.typedefs:
                ty = self.layout(self.typedefs[names[0]], where)
                if ty.kind == 'int' and names[0] in ('u8', 's8'): ty.char = True    # (a u8 array may hold a string)
                return ty
            raise Stop('%s: a type it does not know: %s' % (where, ' '.join(names)))
        if isinstance(t, c.Enum): return Ty('int', 4, 4, signed=True, char=False)
        if isinstance(t, c.Union): raise Stop('%s: a union (union %s)' % (where, t.name))
        if isinstance(t, c.Struct):
            s = t
            if s.decls is None:
                s = self.tags.get(('Struct', t.name))
                if s is None: raise Stop('%s: struct %s not defined' % (where, t.name))
            fields = []; off = 0; align = 1
            for d in s.decls:
                if d.bitsize is not None: raise Stop('%s: a bit-field (%s)' % (where, d.name))
                ft = self.layout(d.type, where)
                if ft.size is None: raise Stop('%s: a flexible array member (%s)' % (where, d.name))
                off = (off + ft.align - 1) // ft.align * ft.align
                fields.append((d.name, ft, off)); off += ft.size; align = max(align, ft.align)
            size = (off + align - 1) // align * align
            return Ty('struct', size, align, fields=fields)
        raise Stop('%s: a type form %s' % (where, type(t).__name__))

    def const(self, e, where):
        """an array dimension (preprocessed: numbers and operators)"""
        c = self.c_ast
        if isinstance(e, c.Constant):
            v = Eval({}).value(tokens(e.value))
            return v[0]
        if isinstance(e, c.BinaryOp):
            a, b = self.const(e.left, where), self.const(e.right, where)
            return {'+': a + b, '-': a - b, '*': a * b, '/': a // b, '<<': a << b, '>>': a >> b, '|': a | b, '&': a & b}[e.op]
        if isinstance(e, c.UnaryOp) and e.op == '-': return -self.const(e.expr, where)
        if isinstance(e, c.Cast): return self.const(e.expr, where)
        raise Stop('%s: an array dimension it can\'t work out: %s' % (where, type(e).__name__))


# =====================================================================================================================
# The layout: the initializer's leaves put on the type (C's rules, brace elision too)
# =====================================================================================================================
class Op:
    __slots__ = ('kind', 'size', 'leaf', 'off', 'value', 'toks')

    def __init__(self, kind, size, leaf, off, value=0):
        self.kind, self.size, self.leaf, self.off, self.value = kind, size, leaf.index, off, value
        self.toks = leaf.toks


def lay(ty, init, where, ev):
    """[Op] for an object of type ty initialized by init (a Group or a Leaf); ty's unknown array size is set"""
    ops = []

    def scalar_op(t, off, lf):
        if t.kind == 'int' or t.kind == 'ptr':
            # (a constant -- a number, or a pointer the game has, (const void*)0x8004BD64u -- is an integer; an address the
            #  link chose -- a string, a symbol -- is the link's word, PTR)
            if lf.is_string():
                if t.kind != 'ptr': raise Stop('%s: a string in an integer: %s' % (where, lf.text()))
                ops.append(Op(K_PTR, 4, lf, off)); return
            try:
                ev.value(lf.toks)
            except Eval.NotConst:
                if t.size != 4: raise Stop('%s: an address in a %d-byte integer: %s' % (where, t.size, lf.text()))
                ops.append(Op(K_PTR, 4, lf, off)); return                         # (an address cast to u32: the link's)
            ops.append(Op(K_INT, t.size, lf, off)); return
        ev.value(lf.toks)
        ops.append(Op(K_F32 if t.kind == 'f32' else K_F64, t.size, lf, off))

    def is_char_array(t): return t.kind == 'array' and t.elem.kind == 'int' and t.elem.size == 1 and getattr(t.elem, 'char', False)

    def subobjects(t):
        if t.kind == 'array':
            k = 0
            while t.n is None or k < t.n:
                yield t.elem, k * t.elem.size; k += 1
        else:
            for name, ft, off in t.fields: yield ft, off

    def fill(t, off, node):
        if t.scalar():
            if isinstance(node, Group):
                if len(node.items) != 1 or isinstance(node.items[0], Group): raise Stop('%s: a scalar in braces with %d items' % (where, len(node.items)))
                node = node.items[0]
            scalar_op(t, off, node); return
        if is_char_array(t):
            n2 = node
            if isinstance(n2, Group) and len(n2.items) == 1 and isinstance(n2.items[0], Leaf) and n2.items[0].is_string(): n2 = n2.items[0]
            if isinstance(n2, Leaf) and n2.is_string():
                s = string_bytes(n2.toks)
                if t.n is None: t.n = len(s) + 1; t.size = t.n
                if len(s) > t.n: raise Stop('%s: a string longer than its array: %s' % (where, n2.text()))
                ops.append(Op(K_STR, t.n, n2, off)); return
        if isinstance(node, Leaf): raise Stop('%s: an aggregate given one leaf: %s' % (where, node.text()))
        cur = [0]
        used = members(t, off, node.items, cur)
        if cur[0] != len(node.items): raise Stop('%s: more initializers than the type holds' % where)
        if t.kind == 'array' and t.n is None: t.n = used; t.size = used * t.elem.size

    def members(t, off, items, cur):
        """t's subobjects from items[cur:], as C fills them (a brace-less run fills an aggregate member member by member)"""
        used = 0
        for st, so in subobjects(t):
            if cur[0] >= len(items): break
            it = items[cur[0]]
            if isinstance(it, Group) or st.scalar() or (is_char_array(st) and it.is_string()):
                cur[0] += 1; fill(st, off + so, it)
            else:
                members(st, off + so, items, cur)        # (brace elision)
            used += 1
        return used

    fill(ty, 0, init)
    return ops


# =====================================================================================================================
# The fill (the same as the game's: the leaves' values written by the layout) and the checks against the build
# =====================================================================================================================
def render(ops, size, ev, where):
    """the symbol's bytes from its ops (PTR words: their stored value)"""
    b = bytearray(size)
    for op in ops:
        if op.kind == K_PTR: b[op.off:op.off + 4] = op.value.to_bytes(4, 'big'); continue
        if op.kind == K_STR:
            s = string_bytes(op.toks)
            b[op.off:op.off + len(s)] = s; continue
        b[op.off:op.off + op.size] = scalar_bytes(ev.value(op.toks), op.kind, op.size)
    return b


def address_of(toks, decls_ty, ev):
    """an address leaf -> (symbol, byte offset) or ('"str"', bytes): &x[i][j], x, x[i] (a row decays), (cast)(x + n), x + n"""
    t = [x for x in toks]
    if t and all(x[0] == '"' for x in t): return ('"', string_bytes(t))
    # (casts and outer parentheses away)
    changed = True
    while changed:
        changed = False
        if len(t) >= 3 and t[0] == '(':
            d = 0
            for k, x in enumerate(t):
                d += x == '('; d -= x == ')'
                if d == 0: break
            inner = t[1:k]
            if k == len(t) - 1: t = inner; changed = True
            elif all(re.match(r'^(\w+|\*|const)$', x) for x in inner) and inner[0] in CAST_WORDS: t = t[k + 1:]; changed = True
    plus = 0
    if '+' in t:
        k = t.index('+'); plus = ev.value(t[k + 1:])[0]; t = t[:k]
    amp = bool(t) and t[0] == '&'
    if amp: t = t[1:]
    if not t or not re.match(r'^[A-Za-z_]\w*$', t[0]): raise Stop('an address it can\'t read: %s' % ' '.join(toks))
    name = t[0]; ty = decls_ty(name); off = 0; k = 1
    while k < len(t) and t[k] in ('[', '.'):
        if t[k] == '.':                                  # (a member: &x[i].next)
            if ty is None or ty.kind != 'struct' or k + 1 >= len(t): raise Stop('a member of a non-struct: %s' % ' '.join(toks))
            f = [x for x in ty.fields if x[0] == t[k + 1]]
            if not f: raise Stop('no member %s: %s' % (t[k + 1], ' '.join(toks)))
            off += f[0][2]; ty = f[0][1]; k += 2; continue
        e = t.index(']', k)
        if ty is None or ty.kind != 'array': raise Stop('an index into a non-array: %s' % ' '.join(toks))
        off += ev.value(t[k + 1:e])[0] * ty.elem.size; ty = ty.elem; k = e + 1
    if k != len(t): raise Stop('an address it can\'t read: %s' % ' '.join(toks))
    if plus:
        el = ty.elem if ty is not None and ty.kind == 'array' else None
        if el is None: raise Stop('pointer arithmetic on a non-array: %s' % ' '.join(toks))
        off += plus * el.size
    return (name, off)


CAST_WORDS = ('u8', 's8', 'u16', 's16', 'u32', 's32', 'u64', 's64', 'unsigned', 'signed', 'int', 'char', 'short', 'long',
              'void', 'const', 'float', 'double')


def runs(ops, window):
    """the ops that touch window (lo, hi) of the symbol, merged into runs: [(kind, size, leaf0, count, off0, stride, [values])]"""
    lo, hi = window
    keep = [o for o in ops if o.off < hi and o.off + o.size > lo]
    out = []
    for o in keep:
        if out:
            r = out[-1]
            kind, size, leaf0, count, off0, stride, vals = r
            if kind == o.kind and size == o.size and o.leaf == leaf0 + count:
                st = o.off - (off0 + (count - 1) * stride) if count > 1 else o.off - off0
                if count == 1 and st > 0 or count > 1 and st == stride:
                    r[3] += 1; r[5] = st
                    if o.kind == K_PTR: vals.append(o.value)
                    continue
        out.append([o.kind, o.size, o.leaf, 1, o.off, o.size, [o.value] if o.kind == K_PTR else []])
    return out


def write_bin(path, image_size, entries):
    B = bytearray(MAGIC) + struct.pack('<III', VERSION, image_size, len(entries))
    for e in entries:
        B += struct.pack('<IIiII', e['start'], e['size'], e['sym_at'], e['sym_size'], e.get('flags', 0))
        for s in (e['header'], e['symbol']):
            b = s.encode('ascii'); B += bytes([len(b)]) + b
        B += struct.pack('<I', len(e['runs']))
        for kind, size, leaf0, count, off0, stride, vals in e['runs']:
            B += struct.pack('<B3xIIIII', kind, size, leaf0, count, off0, stride)
    open(path, 'wb').write(B)
    return len(B)


def includer(src_dirs, header):
    """the mod's .c file that includes header (its types as the build sees them), or header itself if it is a .c file"""
    if header.endswith('.c'): return next(os.path.join(d, header) for d in src_dirs if os.path.exists(os.path.join(d, header)))
    pat = re.compile(r'^\s*#\s*include\s+"%s"' % re.escape(header), re.M)
    for d in src_dirs:
        for f in sorted(os.listdir(d)):
            if f.endswith('.c') and pat.search(open(os.path.join(d, f), encoding='latin-1').read()): return os.path.join(d, f)
    raise Stop('%s: no .c file of the mod includes it' % header)


def preprocess(cc, cfile, headers, src):
    """the .c file preprocessed as build_mod.ps1 compiles it (its include paths and defines), line markers kept"""
    args = cc + ['--target=mips-unknown-none-elf', '-mabi=32', '-E', '-nostdinc', '-ffreestanding', '-DNM_SIX', '-DARCADE_STORY',
                 '-DARCADE_MOVEX', '-I', headers, '-I', os.path.join(src, 'mod', 'src'), '-I', os.path.join(src, 'libs', 'AkiMods', 'common', 'include'),
                 cfile]
    r = subprocess.run(args, capture_output=True, text=True, encoding='latin-1')
    if r.returncode: raise Stop('preprocessing %s failed: %s' % (os.path.basename(cfile), r.stderr[:2000]))
    return r.stdout


def hole_header(label):
    """mkplan's label -> (symbol, header): "<sym> (<h>)", "<sym> [..N] (<h>)", "<sym> [N..] (<h>)", the flag, a source table"""
    sym = label.split(' ')[0]
    if sym in SOURCE_OF: return sym, SOURCE_OF[sym]
    m = re.search(r'\(([\w.]+\.h)\)$', label)
    if not m: raise Stop('a hole with no header: %s' % label)
    return sym, m.group(1)


def make(build, headers, out_dir, cc=None, log=print):
    """the layouts of every extras hole of the build -> out_dir/hdrfill.bin, checked; (ok, report lines)"""
    import mkplan
    cc = cc or find_cc()
    headers = headers or os.path.join(SRC, 'mod', 'src')
    image, holes, relocs = mkplan.find_holes(build, headers)
    elf, secs, syms = modelf.read_elf(os.path.join(build, 'mod.elf'))
    by_name = {}
    for s in syms:
        if s['alloc']: by_name.setdefault(s['name'], []).append(s)
    src_dirs = [os.path.join(SRC, 'mod', 'src'), os.path.join(SRC, 'mod', 'recomp')]
    extras = [(s, e, label) for s, e, cls, label in holes if cls == 'extras' and mkplan.STRING_TAG not in label]
    strings = [(s, e, label) for s, e, cls, label in holes if cls == 'extras' and mkplan.STRING_TAG in label]
    texts, tus = {}, {}       # header -> (tokens, defines, index); header -> Types
    entries, report, constructs = [], [], {}   # (constructs: what the leaves use -> an example)
    total = same = computed = 0
    strings_seen, strings_bytes = set(), 0     # (the strings the extras' pointers point at)
    sym_ops = {}              # (header, symbol) -> its ops, PTR values read from the build (a STRING hole's leaves are there)
    for s, e, label in extras:
        sym, header = hole_header(label)
        where = '%s (%s)' % (label, header)
        if sym in COMPUTED:
            entries.append(dict(start=s, size=e - s, sym_at=0, sym_size=e - s, header=header, symbol=sym, runs=[], flags=F_COMPUTED))
            report.append('%-60s %7d bytes  COMPUTED by the first-start builder: %s' % (where, e - s, COMPUTED[sym]))
            computed += e - s
            continue
        if header not in texts:
            path = os.path.join(headers if header.endswith('.h') else os.path.join(SRC, 'mod', 'src'), header)
            stripped, defines = strip_text(open(path, encoding='latin-1').read())
            toks = tokens(stripped)
            texts[header] = (toks, defines, index_definitions(toks))
            tus[header] = Types(preprocess(cc, includer(src_dirs, header), headers, SRC), header)
        toks, defines, idx = texts[header]
        T = tus[header]
        if len(idx.get(sym, [])) != 1: raise Stop('%s: %d definitions of %s found in the text' % (where, len(idx.get(sym, [])), sym))
        # (the symbol in the build: the one whose range holds this hole)
        cand = [x for x in by_name.get(sym, []) if x['addr'] - modelf.BASE <= s and e <= x['addr'] - modelf.BASE + x['size']]
        if len(cand) != 1: raise Stop('%s: the build\'s symbol not found' % where)
        sa, ssize = cand[0]['addr'] - modelf.BASE, cand[0]['size']
        if sym not in T.decls: raise Stop('%s: its declaration not read (pycparser)' % where)
        ty = T.layout(T.decls[sym], where)
        init, _ = split_initializer(toks, idx[sym][0])
        ev = Eval(defines)
        ops = lay(ty, init, where, ev)
        if ty.size != ssize: raise Stop('%s: laid out %s bytes, the build has %d' % (where, ty.size, ssize))
        # (the link's words: every PTR leaf relocated, every relocated word a PTR leaf)
        ptr_at = {sa + o.off for o in ops if o.kind == K_PTR}
        rel_at = {a - modelf.BASE for a in relocs if sa <= a - modelf.BASE < sa + ssize}
        if ptr_at != rel_at:
            raise Stop('%s: the address leaves (%d) are not the relocated words (%d): e.g. %s' % (
                where, len(ptr_at), len(rel_at), sorted(ptr_at ^ rel_at)[:4]))
        decl_ty = lambda n: (T.layout(T.decls[n], where) if n in T.decls else None)
        lo, hi = s - sa, e - sa
        for o in ops:
            if o.kind != K_PTR: continue
            o.value = struct.unpack_from('>I', image, sa + o.off)[0]
            tgt = address_of(o.toks, decl_ty, ev)
            if tgt[0] == '"':
                a = o.value - modelf.BASE
                if not (0 <= a < len(image)) or image[a:a + len(tgt[1]) + 1] != tgt[1] + b'\0':
                    raise Stop('%s: leaf %d\'s string is not where its word points' % (where, o.leaf))
                constructs.setdefault('a string literal as a pointer', '%s: %s' % (header, ' '.join(o.toks)))
                if lo <= o.off < hi and a not in strings_seen:
                    strings_seen.add(a); strings_bytes += len(tgt[1]) + 1
            else:
                name, off = tgt
                if not any(x['addr'] + off == o.value for x in by_name.get(name, [])):
                    raise Stop('%s: leaf %d (%s) does not point at %s + %d' % (where, o.leaf, ' '.join(o.toks), name, off))
                constructs.setdefault('an address (&x[i].m, an array name, (u32)(x + n))', '%s: %s' % (header, ' '.join(o.toks)))
        sym_ops.setdefault((header, sym), ops)
        # (relclean 2026-11-19) the fill takes each pointer word from the mod's base image (hdrfill.bin carries no value): the
        #  base image has the link's words of the extras holes (mkplan.extras_link_words: every relocated word wholly inside
        #  one), so a pointer of this hole must be a whole word inside it
        for o in ops:
            if o.kind == K_PTR and o.off < hi and o.off + 4 > lo and (o.off < lo or o.off + 4 > hi or (sa + o.off) % 4):
                raise Stop('%s: leaf %d is a pointer word across the hole\'s edge (the base image would not carry it)' % (where, o.leaf))
        # (the window of the symbol this hole is, and the check: the fill gives the build's bytes)
        lo, hi = s - sa, e - sa
        for o in ops:          # (what the fill meets in this hole: the leaves it evaluates)
            if o.kind == K_PTR or o.off >= hi or o.off + o.size <= lo: continue
            def saw(what): constructs.setdefault(what, '%s: %s' % (header, ' '.join(o.toks)))
            for k, t in enumerate(o.toks):
                if t[0] == "'": saw('a char literal')
                elif t[0] == '"': saw('a string literal filling a char array')
                elif t[0].isdigit() or t[0] == '.':
                    if re.search(r'[.eE]', t) and not t.lower().startswith('0x'): saw('a float literal' + (' (f)' if t[-1] in 'fF' else ''))
                    elif t.lower().startswith('0x'): saw('a hex integer' + (' with a suffix' if re.search(r'[uUlL]$', t) else ''))
                    elif t[0] == '0' and len(t) > 1: saw('an octal integer')
                    else: saw('a decimal integer' + (' with a suffix' if re.search(r'[uUlL]$', t) else ''))
                elif t == '*' and k + 1 < len(o.toks) and o.toks[k + 1] in ('*', ')'): pass      # (a cast's pointer)
                elif t in ('-', '+', '~', '!', '<<', '>>', '|', '&', '*', '/', '%', '^'): saw('the operator ' + t)
                elif t[0].isalpha() or t[0] == '_': saw('a define in a leaf' if t in defines else 'a cast')
        made = render(ops, ssize, ev, where)[lo:hi]
        ok = bytes(made) == image[s:e]
        if not ok:
            k = next(i for i in range(e - s) if made[i] != image[s + i])
            near = [o for o in ops if o.off <= lo + k < o.off + o.size]
            raise Stop('%s: DIFFERENT from the build at +%d (%s)' % (where, k, near[0].toks if near else 'no leaf: should be zero'))
        total += e - s; same += e - s
        entries.append(dict(start=s, size=e - s, sym_at=sa - s, sym_size=ssize, header=header, symbol=sym, runs=runs(ops, (lo, hi))))
        report.append('%-60s %7d bytes  %5d leaves  %4d ops  SAME' % (where, e - s, len(ops), len(entries[-1]['runs'])))
    # (relclean 2026-11-19) the STRING holes (mkplan.extras_strings): a string a table's pointers point at, filled from that
    #  table's text -- every leaf of the table pointing at it is a string literal with exactly these bytes, written at its start
    n_str = 0
    for s, e, label in strings:
        table, header = hole_header(label)
        sym = table.split(mkplan.STRING_TAG)[0]
        where = '%s (%s)' % (label, header)
        ops = sym_ops.get((header, sym))
        if ops is None: raise Stop('%s: its table %s was not laid out' % (where, sym))
        leaves = [o for o in ops if o.kind == K_PTR and o.value == modelf.BASE + s]
        if not leaves: raise Stop('%s: no leaf of %s points at it' % (where, sym))
        made = bytearray(e - s)
        for o in leaves:
            if not (o.toks and all(t[0] == '"' for t in o.toks)): raise Stop('%s: leaf %d is not a string literal' % (where, o.leaf))
            b = string_bytes(o.toks)
            if len(b) + 1 != e - s: raise Stop('%s: leaf %d\'s string is not this one' % (where, o.leaf))
            made[:len(b)] = b
        if bytes(made) != image[s:e]: raise Stop('%s: DIFFERENT from the build' % where)
        total += e - s; same += e - s; n_str += 1
        entries.append(dict(start=s, size=e - s, sym_at=0, sym_size=e - s, header=header, symbol=sym, flags=F_STRING,
                            runs=[[K_STR, e - s, o.leaf, 1, 0, e - s, []] for o in leaves]))
        report.append('%-60s %7d bytes  %5d leaves (a string of %s)  SAME' % (where, e - s, len(leaves), sym))
    entries.sort(key=lambda x: x['start'])
    in_holes = {s for s, e, label in strings}
    outside = sorted(a for a in strings_seen if a not in in_holes)
    os.makedirs(out_dir, exist_ok=True)
    n = write_bin(os.path.join(out_dir, 'hdrfill.bin'), len(image), entries)
    head = ['hdrlayout: %d extras holes, %d bytes: %d SAME from the header text (%d of them strings), %d computed by the first-start builder; hdrfill.bin %d bytes' % (
                len(entries), total + computed, same, n_str, computed, n),
            "the strings the extras' pointers point at: %d, %d bytes; %d in holes of their own (filled from the text), %d left in the "
            "image (%s: mkplan.extras_strings says why)" % (len(strings_seen), strings_bytes, len(strings_seen) - len(outside), len(outside),
                                                            ', '.join('0x%X %d bytes' % (a, image.index(b'\0', a) - a + 1) for a in outside) or 'none'),
            'the leaves use (an example of each):'] + ['  %-48s %s' % (k, v[:100]) for k, v in sorted(constructs.items())]
    for h in head: log(h)
    return True, head + [''] + report


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', default=os.path.join(SRC, 'mod', 'build', 'mod_six'))
    ap.add_argument('--headers', default=os.path.join(SRC, 'mod', 'src'), help='the folder of the generated headers the build used')
    ap.add_argument('--out', default=os.path.join(SRC, 'scratch', 'firstrun_plan'))
    ap.add_argument('--cc', help='a clang to preprocess with (default: the toolchain\'s zig clang, else clang)')
    a = ap.parse_args()
    try:
        ok, lines = make(a.build, a.headers, os.path.join(a.out, 'firstrun'), [a.cc] if a.cc else None)
    except Stop as x:
        print('hdrlayout: STOP --', x); sys.exit(1)
    open(os.path.join(a.out, 'hdrfill_report.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
