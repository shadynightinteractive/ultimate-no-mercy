"""Tidying of the converted C++ (2026-11-12, the readability track): post-passes on m2c's output, after everything else lift.py's
candidates stage does to it and before the compile check, so what the tester proves is the tidied text.

Each pass is exactly behaviour-preserving by construction (the rules are at each pass), and every function it changes is proved
again by the random tester like any other change of its C++. A function this module can't read -- a line it doesn't know -- is
left exactly as it was. Only m2c's own temporaries are folded away, never a variable names.json names.

    tidy(name, cpp, named=()) -> (cpp, what changed: {pass: count})

The passes (repeated, in this order, until nothing changes, at most four times):
  invert   `if (c) { } else { B }` -> `if (!c) { B }` (in any arm of an else-if chain too), and an empty else dropped: m2c
           writes an empty branch where the N64's branch skips the work. The condition is negated exactly: == and != swapped,
           a whole-number compare turned round (never a float's: NaN), && and || by De Morgan (the same order of evaluation,
           stopping at the same place), a comma list's last value; anything else as !(c).
  dead_out a write of a register kept in step with the original's (abi.py's LiftAbiOut: a write into the context) that is never
           seen: on every way on from it, the same register is written again before anything could read the context -- a
           call, ctx or frame named, a division, a goto, break, continue or switch start (not followed), the function's end.
           Worked backwards over the statements, round each loop until nothing changes. Only a write whose value comes from
           locals alone is dropped, so no memory read and no call goes with it. And never the last line that names a game
           address (0x80xxxxxx): the tester takes its pointer inputs from the text (keep_addresses).
  unused   an if whose branches are all empty and whose conditions only compute from locals; a temporary set from locals and
           never read.
  fold     a temporary m2c assigned once and read once, `temp_v0_7 = temp_a0 * 0x10; ... header + temp_v0_7`, put into its one
           use: `header + (temp_a0 * 0x10)`. Either its value is worked out from local numbers alone (no memory, no call, no
           division) and none of them changes in between; or it is one read of game memory, and nothing between reaches memory
           or calls, and its use's statement reads it every time it runs, before any other access but a store it makes last
           (so every read and write of memory happens in the same order, as often as before). Never past a label or into a
           loop or switch that starts after it. In the temporary's type: a cast where the expression's own type differs, and
           always on a read of memory (the field itself is a place, Ref<T>) unless stored whole into a place of its type.
  loops    `loop_N:` ... `goto loop_N;` (a loop m2c couldn't write as do-while) -> do { } while (c), while (c) { }, or
           `for (;;) { ... continue; ... break; }`, where every goto to it is in the statements after the label at its own
           level, none inside a loop of its own (where continue would mean something else), and no break or continue there
           belongs to anything outside.
  tails    `goto block_N;` -> a copy of what follows `block_N:` to the end of its block (at most TAIL_LINES lines, no label or
           case in it), where running the copy goes on exactly where the original would: it ends in a return, or both the goto
           and the tail are the last statements of blocks that go on at the same place; a break or continue in it belongs to the
           same loop or switch at both places. A label no goto names any more is taken away.
           (2026-11-13, tidy2) What is copied is what runs from the label on, straight: out of the end of each block it is
           in, up to its first jump, or to where the goto's own block goes on when the goto is its last statement; labels
           and cases in it are left out of the copy (marks the copy never needs). See trace_from.
Added 2026-11-13 (tidy2: the long shared tails and the switch-heavy functions that kept their gotos), run after loops:
  jumps    a goto that never runs (right after another jump) taken away; a goto that goes where falling would, taken away
           (cases one after another, each `goto L;`, end up stacked over one goto); a goto to where the innermost loop or
           switch goes on after it -> break; to where the innermost loop tests again -> continue; to the end of a function
           that returns nothing -> return.
  skip     if (c) { X goto L; } A L:  ->  if (c) { X } else { A }: every branch of an if but one ends in the same goto to a
           label later in its block; the code between, with no label or case in it, goes into the one other branch.
  hoist    the same last statement in every branch of an if with an else, written once after it.
  flatten  } else { if (d) { ... } }  ->  } else if (d) { ... }; and if (c) { if (d) { X } }  ->  if (c && d) { X } (no
           elses, the joined condition at most FLAT_LENGTH characters).
  And statements left after a jump with no label before them (they never run) taken away, as long as a line naming each
  game address stays (keep_addresses).
"""
import re

# ---- Tokens -----------------------------------------------------------------------------------------------------------------

TOKEN = re.compile(r'''
 (?P<ws>\s+)
|(?P<str>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
|(?P<num>(?:0[xX][0-9A-Fa-f]+|\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+(?:[eE][-+]?\d+)?)[uUlLfF]*)
|(?P<id>[A-Za-z_]\w*)
|(?P<op><<=|>>=|->|::|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^=|[-+*/%&|^~!<>=?:,;.()\[\]{}])
''', re.X)

TEMPLATES = {'P', 'Ref', 'lift_call', 'lift_pointer', 'lift_ret'}
INT_TYPES = {'s8', 'u8', 's16', 'u16', 's32', 'u32'}
FLOAT_TYPES = {'f32'}
VALUE_TYPES = INT_TYPES | FLOAT_TYPES
ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='}


class Tok:
    __slots__ = ('kind', 'text', 'start', 'end')

    def __init__(self, kind, text, start, end):
        self.kind, self.text, self.start, self.end = kind, text, start, end

    def __repr__(self):
        return self.text


def tokens(text):
    """The tokens of an expression or statement, a template's name and its <...> as one token of kind 'tmpl' (P<s32>,
    lift_call<s32>), or None if the text has something this module doesn't know."""
    out = []
    i = 0
    while i < len(text):
        m = TOKEN.match(text, i)
        if not m:
            return None
        kind = m.lastgroup
        if kind != 'ws':
            out.append(Tok(kind, m.group(0), m.start(), m.end()))
        i = m.end()
    # (templates: the name, then <...> to its matching >, a >> closing two)
    merged = []
    k = 0
    while k < len(out):
        t = out[k]
        if t.kind == 'id' and t.text in TEMPLATES and k + 1 < len(out) and out[k + 1].text == '<':
            depth, j = 0, k + 1
            while j < len(out):
                x = out[j].text
                if x == '<':
                    depth += 1
                elif x == '>':
                    depth -= 1
                elif x == '>>':
                    depth -= 2
                elif x in (';', '{', '}', '(', ')'):
                    return None
                if depth <= 0:
                    break
                j += 1
            if j >= len(out) or depth != 0:
                return None
            merged.append(Tok('tmpl', text[t.start:out[j].end], t.start, out[j].end))
            k = j + 1
            continue
        merged.append(t)
        k += 1
    return merged


# ---- The tree of a function's body --------------------------------------------------------------------------------------------

class Line:
    """A line kept as it is: a statement, a label, a case, a comment or a blank."""
    __slots__ = ('text', 'kind')

    def __init__(self, text, kind):
        self.text, self.kind = text, kind

    def lines(self):
        return [self.text]


class If:
    """if (cond) { ... } [else if (cond) { ... }]* [else { ... }]: arms [(head line, cond, block)], els (the else's block or
    None), indent: the if's own indentation."""
    __slots__ = ('indent', 'arms', 'els')

    def __init__(self, indent, arms, els):
        self.indent, self.arms, self.els = indent, arms, els

    def lines(self):
        out = []
        for k, (cond, block) in enumerate(self.arms):
            out.append(self.indent + ('' if k == 0 else '} else ') + 'if (' + cond + ') {')
            out += block_lines(block)
        if self.els is not None:
            out.append(self.indent + '} else {')
            out += block_lines(self.els)
        out.append(self.indent + '}')
        return out


class Do:
    __slots__ = ('indent', 'body', 'cond')

    def __init__(self, indent, body, cond):
        self.indent, self.body, self.cond = indent, body, cond

    def lines(self):
        return [self.indent + 'do {'] + block_lines(self.body) + [self.indent + '} while (' + self.cond + ');']


class Switch:
    __slots__ = ('indent', 'expr', 'body', 'pad')

    def __init__(self, indent, expr, body, pad=''):
        self.indent, self.expr, self.body, self.pad = indent, expr, body, pad   # (pad: m2c's spaces after the brace, kept)

    def lines(self):
        return [self.indent + 'switch (' + self.expr + ') {' + self.pad] + block_lines(self.body) + [self.indent + '}']


class For:
    """for (;;) { ... }: made by the loops pass only."""
    __slots__ = ('indent', 'body')

    def __init__(self, indent, body):
        self.indent, self.body = indent, body

    def lines(self):
        return [self.indent + 'for (;;) {'] + block_lines(self.body) + [self.indent + '}']



class While:
    """while (cond) { ... }: made by the loops pass only."""
    __slots__ = ('indent', 'cond', 'body')

    def __init__(self, indent, cond, body):
        self.indent, self.cond, self.body = indent, cond, body

    def lines(self):
        return [self.indent + 'while (' + self.cond + ') {'] + block_lines(self.body) + [self.indent + '}']


def block_lines(block):
    out = []
    for node in block:
        out += node.lines()
    return out


class Unreadable(Exception):
    pass


def line_kind(s):
    if not s:
        return 'blank'
    if s.startswith('//') or (s.startswith('/*') and s.endswith('*/')):
        return 'comment'
    if s.startswith('case ') and s.endswith(':') or s == 'default:':
        return 'case'
    if re.fullmatch(r'[A-Za-z_]\w*:', s):
        return 'label'
    code = re.sub(r';\s+//[^"]*$', ';', s)       # (abi.py's lines end in a comment: `...;   // the context's v0, ...`)
    if code.endswith(';') and not s.startswith('}'):
        return 'stmt'
    raise Unreadable(s)


def parse_block(lines, i, closing):
    """Nodes from lines[i] until a line that closes the block (one starting with '}' -- checked by the caller): (block, i)."""
    block = []
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        indent = raw[:len(raw) - len(raw.lstrip())]
        if s.startswith('}'):
            return block, i
        m = re.fullmatch(r'if \((.*)\) \{', s)
        if m:
            arms = []
            cond = m.group(1)
            j = i + 1
            els = None
            while True:
                body, j = parse_block(lines, j, True)
                if j >= len(lines):
                    raise Unreadable('unclosed if')
                arms.append((cond, body))
                c = lines[j]
                if c[:len(c) - len(c.lstrip())] != indent:
                    raise Unreadable('if closed at another indentation')
                cs = c.strip()
                m2 = re.fullmatch(r'\} else if \((.*)\) \{', cs)
                if m2:
                    cond = m2.group(1)
                    j += 1
                    continue
                if cs == '} else {':
                    els, j = parse_block(lines, j + 1, True)
                    if j >= len(lines) or lines[j] != indent + '}':
                        raise Unreadable('unclosed else')
                    j += 1
                    break
                if cs == '}':
                    j += 1
                    break
                raise Unreadable(cs)
            block.append(If(indent, arms, els))
            i = j
            continue
        if s == 'do {':
            body, j = parse_block(lines, i + 1, True)
            if j >= len(lines):
                raise Unreadable('unclosed do')
            m2 = re.fullmatch(r'\} while \((.*)\);', lines[j].strip())
            if not m2 or not lines[j].startswith(indent + '}'):
                raise Unreadable('do without its while')
            block.append(Do(indent, body, m2.group(1)))
            i = j + 1
            continue
        m = re.fullmatch(r'switch \((.*)\) \{', s)
        if m:
            body, j = parse_block(lines, i + 1, True)
            if j >= len(lines) or lines[j] != indent + '}':
                raise Unreadable('unclosed switch')
            block.append(Switch(indent, m.group(1), body, raw[len(raw.rstrip()):]))
            i = j + 1
            continue
        if s.endswith('{'):
            raise Unreadable(s)
        block.append(Line(raw, line_kind(s)))
        i += 1
    return block, i


class Function:
    """A converted function: its head (the signature line), its declarations' lines and the blank after them, its body as a
    tree, and its closing line."""

    def __init__(self, cpp):
        lines = cpp.split('\n')
        if not lines or not lines[0].endswith(') {') or lines[-1] != '}':
            raise Unreadable('not one function')
        self.head = lines[0]
        body, i = parse_block(lines, 1, True)
        if i != len(lines) - 1:
            raise Unreadable('the body ends early')
        self.body = body
        self.types = {}
        # (The parameters' types, from the signature: `s32 arg0`, `P<void> block`, recomp_context* ctx left out.)
        m = re.search(r'\(([^()]*)\) \{$', self.head)
        if not m:
            raise Unreadable('signature')
        for p in m.group(1).split(','):
            p = p.strip()
            pm = re.fullmatch(r'(.+?)\s*\b([A-Za-z_]\w*)', p)
            if p and p != 'void' and pm:
                self.types[pm.group(2)] = pm.group(1).strip()
        # (The locals' types: m2c's declarations, `s32 temp_v0;`, at the top of the body, and abi.py's `const s32 lift_in_a0 = ...`.)
        self.decl_lines = {}
        for k, node in enumerate(body):
            if isinstance(node, Line) and node.kind == 'stmt':
                dm = re.fullmatch(r'\s*((?:const )?[A-Za-z_][\w<> ]*?[\w>]) ([A-Za-z_]\w*)(?: = [^;]*|\{[^;]*\}|\([^;]*\))?;(?:\s*//.*)?', node.text)
                if dm and dm.group(1) not in ('return', 'goto'):
                    name = dm.group(2)
                    self.types.setdefault(name, dm.group(1))
                    if re.fullmatch(r'\s*[A-Za-z_][\w<> ]*?[\w>] [A-Za-z_]\w*;', node.text):
                        self.decl_lines[name] = node

    def text(self):
        return '\n'.join([self.head] + block_lines(self.body) + ['}'])


# ---- Expressions ------------------------------------------------------------------------------------------------------------

class Expr:
    """A parsed expression: kind 'id', 'num', 'unary', 'cast', 'bin', 'paren', 'other' (anything else, which is never folded)."""
    __slots__ = ('kind', 'op', 'args', 'text')

    def __init__(self, kind, op, args, text):
        self.kind, self.op, self.args, self.text = kind, op, args, text


BINARY = {'||': 1, '&&': 2, '|': 3, '^': 4, '&': 5, '==': 6, '!=': 6, '<': 7, '>': 7, '<=': 7, '>=': 7, '<<': 8, '>>': 8,
          '+': 9, '-': 9, '*': 10, '/': 10, '%': 10}


class Parser:
    """A small expression parser for the folding pass: whole-number and float locals, numbers, casts to those types, unary and
    binary operators. Anything else -- a call, a field, a pointer, an assignment -- raises Unreadable."""

    def __init__(self, toks):
        self.t = toks
        self.k = 0

    def peek(self):
        return self.t[self.k].text if self.k < len(self.t) else None

    def take(self):
        tok = self.t[self.k]
        self.k += 1
        return tok

    def expr(self, level=1):
        left = self.unary()
        while True:
            op = self.peek()
            if op not in BINARY or BINARY[op] < level:
                return left
            self.take()
            right = self.expr(BINARY[op] + 1)
            left = Expr('bin', op, [left, right], None)

    def unary(self):
        op = self.peek()
        if op in ('-', '+', '~', '!'):
            self.take()
            return Expr('unary', op, [self.unary()], None)
        if op == '(':
            # a cast to a value type, or a parenthesised expression
            if self.k + 2 < len(self.t) and self.t[self.k + 1].text in VALUE_TYPES and self.t[self.k + 2].text == ')':
                self.k += 3
                ty = self.t[self.k - 2].text
                return Expr('cast', ty, [self.unary()], None)
            self.take()
            e = self.expr()
            if self.peek() != ')':
                raise Unreadable('paren')
            self.take()
            return Expr('paren', None, [e], None)
        if self.k >= len(self.t):
            raise Unreadable('end')
        tok = self.take()
        if tok.kind == 'id':
            if self.peek() in ('(', '[', '.', '->', '++', '--', '<'):
                raise Unreadable('not a plain variable')
            return Expr('id', None, [], tok.text)
        if tok.kind == 'num':
            return Expr('num', None, [], tok.text)
        raise Unreadable(tok.text)


def parse_expr(text):
    toks = tokens(text)
    if toks is None:
        raise Unreadable('tokens')
    p = Parser(toks)
    e = p.expr()
    if p.k != len(toks):
        raise Unreadable('trailing')
    return e


def promote(t):
    if t in ('s8', 'u8', 's16', 'u16', 'bool', 's32'):
        return 's32'
    return t


def number_type(text):
    t = text.lower()
    if re.fullmatch(r'(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?f', t) and ('.' in t or 'e' in t):
        return 'f32'
    if '.' in t or ('e' in t and not t.startswith('0x')):
        return 'f64'
    m = re.fullmatch(r'(0x[0-9a-f]+|\d+)(u?)', t)
    if not m:
        return None
    v = int(m.group(1), 0)
    if m.group(2):
        return 'u32' if v <= 0xFFFFFFFF else None
    if v <= 0x7FFFFFFF:
        return 's32'
    if m.group(1).startswith('0x') and v <= 0xFFFFFFFF:
        return 'u32'
    return None


def expr_type(e, types):
    """The C++ type of a parsed expression, by C++'s rules for these types: s32 (int), u32 (unsigned), f32, f64, bool, or None
    when this can't say. Every variable must be a value-typed local or parameter."""
    if e.kind == 'id':
        t = types.get(e.text)
        t = t[6:] if t and t.startswith('const ') else t
        return t if t in VALUE_TYPES else None
    if e.kind == 'num':
        return number_type(e.text)
    if e.kind == 'paren':
        return expr_type(e.args[0], types)
    if e.kind == 'cast':
        return e.op if expr_type(e.args[0], types) is not None else None
    if e.kind == 'unary':
        t = expr_type(e.args[0], types)
        if t is None:
            return None
        if e.op == '!':
            return 'bool'
        if e.op == '~' and t in ('f32', 'f64'):
            return None
        return promote(t)
    if e.kind == 'bin':
        a, b = expr_type(e.args[0], types), expr_type(e.args[1], types)
        if a is None or b is None:
            return None
        if e.op in ('&&', '||', '==', '!=', '<', '>', '<=', '>='):
            return 'bool'
        if e.op in ('<<', '>>'):
            return None if a in ('f32', 'f64') or b in ('f32', 'f64') else promote(a)
        a, b = promote(a), promote(b)
        if e.op in ('&', '|', '^', '%') and ('f32' in (a, b) or 'f64' in (a, b)):
            return None
        if 'f64' in (a, b):
            return 'f64'
        if 'f32' in (a, b):
            return 'f32'
        if 'u32' in (a, b):
            return 'u32'
        return 's32'
    return None


def expr_ids(e, out):
    if e.kind == 'id':
        out.add(e.text)
    for a in e.args:
        expr_ids(a, out)
    return out


def expr_ops(e, out):
    if e.kind in ('bin', 'unary'):
        out.add(e.op)
    for a in e.args:
        expr_ops(a, out)
    return out


# ---- Conditions: their negation ----------------------------------------------------------------------------------------------

def split_top(text, seps):
    """The text split at the top-level occurrences (outside parentheses and brackets) of any of the operator tokens seps:
    ([parts], [the separators]), or None if it can't be read."""
    toks = tokens(text)
    if toks is None:
        return None
    parts, found, depth, start = [], [], 0, 0
    for t in toks:
        if t.text in ('(', '['):
            depth += 1
        elif t.text in (')', ']'):
            depth -= 1
        elif depth == 0 and t.text in seps:
            parts.append(text[start:t.start].strip())
            found.append(t.text)
            start = t.end
    parts.append(text[start:].strip())
    return parts, found


def outer_parens(text):
    """The text without one pair of parentheses round all of it, or None if it isn't so wrapped."""
    if not (text.startswith('(') and text.endswith(')')):
        return None
    depth = 0
    toks = tokens(text)
    if toks is None:
        return None
    for k, t in enumerate(toks):
        if t.text in ('(', '['):
            depth += 1
        elif t.text in (')', ']'):
            depth -= 1
            if depth == 0 and k != len(toks) - 1:
                return None
    return text[1:-1].strip()


FLIP = {'==': '!=', '!=': '=='}
INT_FLIP = {'<': '>=', '>=': '<', '>': '<=', '<=': '>'}
INT_LIKE = INT_TYPES | {'bool'}


def operand_kind(text, types):
    """'int' when the value of this text is surely a whole number or a game address (P, compared as an unsigned number), so
    that !(a < b) is a >= b; None when it might be a float, or this can't tell."""
    text = text.strip()
    toks = tokens(text)
    if not toks:
        return None
    if text[0] in '-~!' and not text.startswith(('->', '!=')):
        return operand_kind(text[1:], types)
    sp = split_top(text, tuple(BINARY))
    if sp is None:
        return None
    if sp[1]:
        if not all(sp[0]):
            return None
        if any(op in ('&&', '||', '==', '!=', '<', '>', '<=', '>=') for op in sp[1]):
            return 'int'        # (a bool)
        return 'int' if all(operand_kind(p, types) == 'int' for p in sp[0]) else None
    inner = outer_parens(text)
    if inner is not None:
        return operand_kind(inner, types)
    # A cast to a whole number or an address, (s32) x or (P<T> )x, of one operand (split_top found no operator after it):
    # whatever the operand was, the value is a whole number now.
    if re.match(r'\((s8|u8|s16|u16|s32|u32|P<[\w<> ]+?>\s*)\)', text):
        return 'int'
    if re.fullmatch(r'\w+', text):
        if re.fullmatch(r'(?:0[xX][0-9A-Fa-f]+|\d+)[uU]?', text):
            return 'int'
        t = types.get(text, '')
        t = t[6:] if t.startswith('const ') else t
        if t in INT_LIKE or t.startswith('P<') or (t.startswith('Ref<') and (t[4:-1] in INT_LIKE or t[4:].startswith('P<'))):
            return 'int'
        return None
    m = re.fullmatch(r'M2C_FIELD\((.*), P<([\w<> ]+?)> , ([-\w]+)\)', text)
    if m and (m.group(2) in INT_LIKE or m.group(2).startswith('P<')):
        return 'int'
    m = re.match(r'\*\(P<([\w<> ]+?)> \)', text)
    if m and (m.group(1) in INT_LIKE or m.group(1).startswith('P<')) and re.fullmatch(r'\*\(P<[\w<> ]+?> \)(?:0x[0-9A-F]+|\w+)', text):
        return 'int'
    m = re.match(r'lift_call<([\w<> ]+?)>\(', text)
    if m and (m.group(1) in INT_LIKE or m.group(1).startswith('P<')):
        return 'int'
    return None


def wrap(text):
    """Text as an operand of && or ||: as it is if it's one already, else in parentheses."""
    if re.fullmatch(r'!?\w+', text) or (text.startswith('(') and outer_parens(text) is not None):
        return text
    return '(' + text + ')'


def negate(cond, types):
    """`!(cond)`, written plainly where C++ makes it exactly the same: == and != swapped, a whole-number compare turned round,
    && and || by De Morgan (evaluated in the same order, stopping at the same place), a comma list's last value negated. Else
    `!(cond)`."""
    c = cond.strip()
    inner = outer_parens(c)
    if inner is not None:
        return wrap(negate(inner, types))
    sp = split_top(c, (',',))
    if sp is None:
        return '!(' + c + ')'
    if sp[1]:
        return ', '.join(sp[0][:-1] + [wrap(negate(sp[0][-1], types))])
    sp = split_top(c, ('?', ':', '=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='))
    if sp[1]:
        return '!(' + c + ')'
    for op, other in (('||', '&&'), ('&&', '||')):
        sp = split_top(c, (op,))
        if sp[1]:
            return (' %s ' % other).join(wrap(negate(p, types)) for p in sp[0])
    sp = split_top(c, ('|', '^', '&'))
    if sp[1] or c.startswith('&'):
        return '!(' + c + ')'
    sp = split_top(c, ('==', '!=', '<', '>', '<=', '>='))
    if len(sp[1]) == 1:
        op = sp[1][0]
        left, right = sp[0]
        if left and right:
            if op in FLIP:
                return '%s %s %s' % (left, FLIP[op], right)
            if op in INT_FLIP and operand_kind(left, types) == 'int' and operand_kind(right, types) == 'int':
                return '%s %s %s' % (left, INT_FLIP[op], right)
        return '!(' + c + ')'
    if sp[1]:
        return '!(' + c + ')'
    if c.startswith('!'):
        rest = c[1:].strip()
        if outer_parens(rest) is not None:
            return outer_parens(rest)
        if re.fullmatch(r'\w+', rest):
            return rest
    if re.fullmatch(r'\w+', c):
        return '!' + c
    return '!(' + c + ')'


# ---- The passes ------------------------------------------------------------------------------------------------------------

def child_blocks(node):
    if isinstance(node, If):
        return [b for _, b in node.arms] + ([node.els] if node.els is not None else [])
    if isinstance(node, (Do, Switch, For, While)):
        return [node.body]
    return []


def walk_blocks(block, visit):
    """visit(block) on this block and every block inside it, innermost first."""
    for node in block:
        for inner in child_blocks(node):
            walk_blocks(inner, visit)
    visit(block)


def is_empty(block):
    return all(isinstance(n, Line) and n.kind == 'blank' for n in block)


def shift(block, levels):
    """Every line of a block moved right (or left) by levels of four spaces; labels stay at the start of their lines."""
    pad = '    ' * abs(levels)
    for node in block:
        if isinstance(node, Line):
            if node.kind in ('label', 'blank'):
                continue
            if levels > 0:
                node.text = pad + node.text
            elif node.text.startswith(pad):
                node.text = node.text[len(pad):]
            else:
                raise Unreadable('shift')
        else:
            if levels > 0:
                node.indent = pad + node.indent
            elif node.indent.startswith(pad):
                node.indent = node.indent[len(pad):]
            else:
                raise Unreadable('shift')
            for b in child_blocks(node):
                shift(b, levels)


def invert_empty_then(fn, stats):
    """if (c) { } else { B }  ->  if (!c) { B }; and an empty else dropped. The condition is evaluated as before, once, and the
    same statements run."""
    def visit(block):
        for node in block:
            if not isinstance(node, If):
                continue
            if node.els is not None and is_empty(node.els):
                node.els = None
                stats['empty_else'] = stats.get('empty_else', 0) + 1
            # (Any arm of an if / else if chain, the first or a later one: its condition is still tested only when the ones
            #  before it were false.)
            k = 0
            while k < len(node.arms):
                if is_empty(node.arms[k][1]) and (k + 1 < len(node.arms) or node.els is not None):
                    cond = negate(node.arms[k][0], fn.types)
                    if k + 1 == len(node.arms):
                        node.arms = node.arms[:k] + [(cond, node.els)]
                    else:
                        # if (c) { } else if (d) { X } else { Y }  ->  if (!c) { if (d) { X } else { Y } }
                        rest = If(node.indent, node.arms[k + 1:], node.els)
                        shift([rest], 1)
                        node.arms = node.arms[:k] + [(cond, [rest])]
                    node.els = None
                    stats['invert'] = stats.get('invert', 0) + 1
                k += 1
    walk_blocks(fn.body, visit)


def operand_end(tok):
    return tok is not None and (tok.kind in ('id', 'num') or tok.text in (')', ']'))


def plain_value(text, fn, value_ids_only=False):
    """Whether an expression only reads locals and parameters by value and computes: no memory, no call, no division, no
    assignment. (A value-typed local or parameter; a game address held in one, P<T>, unless value_ids_only.)"""
    toks = tokens(text)
    if not toks:
        return False
    k = 0
    while k < len(toks):
        t = toks[k]
        prev = toks[k - 1] if k else None
        nxt = toks[k + 1].text if k + 1 < len(toks) else None
        if t.kind == 'id':
            ty = fn.types.get(t.text)
            if ty is None:
                return False
            ty = ty[6:] if ty.startswith('const ') else ty
            if not (ty in VALUE_TYPES or (not value_ids_only and re.fullmatch(r'P<[\w<> ]+>', ty))):
                return False
            if nxt in ('(', '[', '.', '->', '++', '--') or nxt in ASSIGN_OPS:
                return False
        elif t.kind == 'num':
            pass
        elif t.text == '(':
            # a cast to a value type or an address type, or a parenthesis
            if k + 2 < len(toks) and toks[k + 2].text == ')' and (toks[k + 1].text in VALUE_TYPES or
                                                                  (not value_ids_only and toks[k + 1].kind == 'tmpl' and toks[k + 1].text.startswith('P<'))):
                k += 3
                continue
        elif t.text == ')':
            pass
        elif t.text in ('+', '-', '~', '!', '<<', '>>', '|', '^', '==', '!=', '<', '>', '<=', '>=', '&&', '||'):
            pass
        elif t.text in ('*', '&'):
            if not operand_end(prev):
                return False        # (a dereference, or an address)
        else:
            return False
        k += 1
    return True


# One read of game memory as a temporary's whole value (the folding pass): a field of a local's or parameter's address, or a
# word at a fixed address.
MEM_READ = re.compile(r'M2C_FIELD\((\w+), P<(s8|u8|s16|u16|s32|u32|f32)> , (?:-?0x[0-9A-F]+|-?\d+)\)|\*\(P<(s8|u8|s16|u16|s32|u32|f32)> \)0x[0-9A-F]+')


def mem_free(text, fn, extra=None):
    """Whether a statement or condition never reaches game memory, never calls, never reads the context and never divides:
    every name in it a local or parameter held by value (a number or an address in P<T>), or a register kept in step that it
    writes (lift_out_*: the context, not game memory). Anything this can't read counts as reaching memory."""
    toks = tokens(text)
    if toks is None:
        return False
    for k, t in enumerate(toks):
        prev = toks[k - 1] if k else None
        nxt = toks[k + 1].text if k + 1 < len(toks) else None
        if t.kind == 'id':
            if t.text in VALUE_TYPES or t.text in ('return', 'goto', 'break', 'continue'):
                continue
            if t.text.startswith('lift_out_') and nxt == '=':
                continue
            if prev is not None and prev.text == 'goto':
                continue
            ty = (extra or {}).get(t.text) or fn.types.get(t.text)
            if ty is None:
                return False
            ty = ty[6:] if ty.startswith('const ') else ty
            if not (ty in VALUE_TYPES or re.fullmatch(r'P<[\w<> ]+>', ty)):
                return False
            if nxt in ('(', '[', '.', '->'):
                return False
        elif t.kind == 'tmpl':
            # (only as a cast to an address type, `(P<void> )x`, which reads nothing; `*(P<T> )x` is caught at the *)
            if not (t.text.startswith('P<') and prev is not None and prev.text == '(' and nxt == ')'):
                return False
        elif t.kind == 'str':
            return False
        elif t.text in ('*', '&') and not operand_end(prev):
            return False
        elif t.text in ('[', ']', '.', '->', '::', '/', '%', '/=', '%=', '{', '}'):
            return False
    return True


def mem_free_use(before, after, owner, fn, ty, field_type):
    """Whether the statement a memory read is folded into reaches memory no other way, apart from a store it makes after
    working out its value: `M2C_FIELD(local, P<T> , offset) = <value>;`, whose address reads nothing (C++17 evaluates an
    assignment's right side first, the built-in's order, for an overloaded = too). So the read comes where it did: after
    everything before it, before everything after."""
    held = 'lift_tidy_folded'
    text = before + held + after
    extra = {held: ty}
    m = re.fullmatch(r'(\s*)M2C_FIELD\((\w+), P<\w+> , (?:-?0x[0-9A-F]+|-?\d+)\) = (.*);', text) if isinstance(owner, Line) else None
    if m and plain_value(m.group(2), fn):
        return mem_free(m.group(3), fn, extra)
    m = re.fullmatch(r'(\s*)\*\(P<\w+> \)0x[0-9A-F]+ = (.*);', text) if isinstance(owner, Line) else None
    if m:
        return mem_free(m.group(2), fn, extra)
    return mem_free(text, fn, extra)


# What may stand between two writes of a register kept in step, the first never seen: a statement that neither calls (M2C_FIELD
# only reaches game memory, never the context), reads ctx, leaves the straight run of statements, nor divides.
PURE_CALLS = {'M2C_FIELD'}


def reads_context_or_calls(text):
    toks = tokens(text)
    if toks is None:
        return True
    for k, t in enumerate(toks):
        nxt = toks[k + 1].text if k + 1 < len(toks) else None
        if t.kind == 'id' and t.text in ('ctx', 'frame', 'return', 'goto', 'break', 'continue', 'do_break', 'throw'):
            return True
        if t.kind == 'id' and nxt == '(' and t.text not in PURE_CALLS:
            return True
        if t.kind == 'tmpl' and (nxt == '(' and not t.text.startswith('P<') or t.text.startswith('Ref<')):
            return True
        if t.text in ('/', '%', '/=', '%=', '->', '.', '::', '{', '}'):
            return True
    return False


OUT_WRITE = re.compile(r'(\s*)(lift_out_\w+) = (.*);')


# The game addresses the random tester finds in a converted function's text (lift.py run_fuzz's `addresses`: every 0x80xxxxxx in
# it), which it gives the function as pointers and fills with random data. A line dropped here must not take the last mention of
# one away, or the test would change: fewer such inputs, fewer of the original's instructions reached (func_8010D7BC lost 32 of
# its 305 with `lift_out_v0 = 0x80150000;` dropped, 2026-11-12). So one line holding it stays.
ADDRESS = re.compile(r'0x80[0-9A-Fa-f]{6}\b')


def keep_addresses(fn, dead, nodes):
    """The dead set less what must stay so that every address in the function's text is still there."""
    if not dead:
        return dead
    rest = set()
    place = {}
    for k, (t, owner) in enumerate(body_texts(fn)):
        place.setdefault(id(owner), k)
        if id(owner) not in dead:
            rest |= set(a.lower() for a in ADDRESS.findall(t))
    out = set(dead)
    # (the first such line in the function is the one kept: the same every run)
    for key in sorted(dead, key=lambda k: place.get(k, 0)):
        found = set(a.lower() for a in ADDRESS.findall(nodes[key].text))
        if found - rest:
            out.discard(key)
            rest |= found
    return out


def drop_dead_out_writes(fn, stats):
    """A write of a register kept in step (lift_out_*, a write into the context) that is never seen: on every way on from it
    the same register is written again before anything could read the context. What can read it, counted as reading every
    such register: a call (the callee reads the context; M2C_FIELD only reaches game memory), anything naming ctx or frame, a
    division (it can stop the game), the function's end (its caller and the tester see them all), and -- not followed here --
    a goto, a break, a continue and a switch's start. Worked backwards over the statements, round each loop until nothing
    changes (the registers a point may still have read, `live`). Only a write whose value comes from locals alone is dropped,
    so dropping it drops no memory read and no call."""
    universe = frozenset(n for n, t in fn.types.items() if t.startswith('LiftAbiOut'))
    if not universe:
        return
    seen = {}       # id(write) -> (the write, the registers that may be read after it)

    def barrier(text):
        return reads_context_or_calls(text)

    def block_live(block, out):
        live = out
        for node in reversed(block):
            live = node_live(node, live)
        return live

    def node_live(node, out):
        if isinstance(node, Line):
            if node.kind in ('blank', 'comment', 'label', 'case'):
                return out
            s = node.text.strip()
            m = OUT_WRITE.fullmatch(node.text)
            if m and m.group(2) in universe and plain_value(m.group(3), fn):
                seen[id(node)] = (node, out)
                return out - {m.group(2)}
            if s.startswith(('return', 'goto ')) or s in ('break;', 'continue;') or barrier(node.text):
                return universe
            return out
        if isinstance(node, If):
            alt = out if node.els is None else block_live(node.els, out)
            for cond, b in reversed(node.arms):
                arm = block_live(b, out)
                alt = universe if barrier(cond) else arm | alt
            return alt
        if isinstance(node, Do):
            start = frozenset()
            while True:
                at_test = universe if barrier(node.cond) else start | out
                new = block_live(node.body, at_test)
                if new == start:
                    return start
                start = new
        if isinstance(node, While):
            before = frozenset()
            while True:
                new = universe if barrier(node.cond) else block_live(node.body, before) | out
                if new == before:
                    return before
                before = new
        if isinstance(node, For):
            top = frozenset()
            while True:
                new = block_live(node.body, top)
                if new == top:
                    return top
                top = new
        if isinstance(node, Switch):
            block_live(node.body, universe)
            return universe
        return universe

    block_live(fn.body, universe)
    dead = {key for key, (node, out) in seen.items() if OUT_WRITE.fullmatch(node.text).group(2) not in out}
    dead = keep_addresses(fn, dead, {key: node for key, (node, _) in seen.items()})
    if not dead:
        return

    def visit(block):
        before = len(block)
        block[:] = [n for n in block if id(n) not in dead]
        stats['dead_out'] = stats.get('dead_out', 0) + before - len(block)
    walk_blocks(fn.body, visit)


def drop_unused(fn, stats, named=()):
    """What the other passes leave doing nothing: an if whose every branch is empty, with conditions that only compute from
    locals (no call, no memory, no assignment), and a temporary that is set (from locals alone) and never read -- its
    assignment and its declaration."""
    def visit(block):
        keep = []
        for n in block:
            if isinstance(n, If) and all(is_empty(b) for _, b in n.arms) and (n.els is None or is_empty(n.els)) and \
                    all(plain_value(c, fn) and not ADDRESS.search(c) for c, _ in n.arms):
                stats['empty_if'] = stats.get('empty_if', 0) + 1
                continue
            keep.append(n)
        block[:] = keep
    walk_blocks(fn.body, visit)
    while True:
        texts_all = '\n'.join(t for t, _ in body_texts(fn))
        gone = None
        for name, decl in list(fn.decl_lines.items()):
            if not name.startswith('temp_') or name in named or len(re.findall(r'\b%s\b' % re.escape(name), texts_all)) != 2:
                continue
            found = []

            def find(block):
                for n in block:
                    m = DEF.fullmatch(n.text) if isinstance(n, Line) and n.kind == 'stmt' else None
                    if m and m.group(2) == name and plain_value(m.group(3), fn):
                        found.append((block, n))
            walk_blocks(fn.body, find)
            if len(found) == 1 and not keep_addresses(fn, {id(found[0][1])}, {id(found[0][1]): found[0][1]}):
                pass        # (it holds the last mention of a game address: see keep_addresses)
            elif len(found) == 1:
                block, n = found[0]
                block.remove(n)
                fn.body.remove(decl)
                del fn.decl_lines[name]
                gone = name
                stats['unused'] = stats.get('unused', 0) + 1
                break
        if gone is None:
            return


# ---- Folding a temporary into its one use -----------------------------------------------------------------------------------

DEF = re.compile(r'(\s*)(temp_\w+) = (.*);')


def node_texts(node):
    """The texts of a node in the order they're written: (text, the node it belongs to) -- an if's conditions, a do's
    condition at its end, a switch's value, each line."""
    if isinstance(node, Line):
        yield node.text, node
    elif isinstance(node, If):
        for cond, b in node.arms:
            yield cond, node
            for n in b:
                yield from node_texts(n)
        if node.els is not None:
            for n in node.els:
                yield from node_texts(n)
    elif isinstance(node, Do):
        for n in node.body:
            yield from node_texts(n)
        yield node.cond, node
    elif isinstance(node, Switch):
        yield node.expr, node
        for n in node.body:
            yield from node_texts(n)
    elif isinstance(node, For):
        for n in node.body:
            yield from node_texts(n)
    elif isinstance(node, While):
        yield node.cond, node
        for n in node.body:
            yield from node_texts(n)


def path_to(node, target_text_owner, path):
    """The nodes from node down to the one that owns a text (inclusive), or None."""
    if node is target_text_owner:
        return path + [node]
    for b in child_blocks(node):
        for n in b:
            p = path_to(n, target_text_owner, path + [node])
            if p:
                return p
    return None


def assigned_in(text, names):
    """Whether a text assigns (=, op=, ++, --) or takes the address of any of these names."""
    toks = tokens(text)
    if toks is None:
        return True
    for k, t in enumerate(toks):
        if t.kind != 'id' or t.text not in names:
            continue
        nxt = toks[k + 1].text if k + 1 < len(toks) else None
        prev = toks[k - 1] if k else None
        if nxt in ASSIGN_OPS or nxt in ('++', '--') or (prev is not None and prev.text in ('++', '--')):
            return True
        if prev is not None and prev.text == '&' and not operand_end(toks[k - 2] if k >= 2 else None):
            return True
    return False


def occurrences(fn, name):
    pat = re.compile(r'\b%s\b' % re.escape(name))
    return sum(len(pat.findall(t)) for t, _ in body_texts(fn))


def body_texts(fn):
    for n in fn.body:
        yield from node_texts(n)


def fold_temps(fn, stats, named=()):
    """temp = E; ... use(temp)  ->  use(E), for a temporary m2c assigns once and reads once (see the module's note). One at a
    time, each from the tree as the one before left it."""
    while True:
        texts_all = '\n'.join(t for t, _ in body_texts(fn))
        done = []
        def visit(block):
            if done:
                return
            for k, node in enumerate(block):
                m = DEF.fullmatch(node.text) if isinstance(node, Line) and node.kind == 'stmt' else None
                if m and try_fold(fn, block, k, m, named, texts_all):
                    done.append(m.group(2))
                    return
        walk_blocks(fn.body, visit)
        if not done:
            return
        stats['fold'] = stats.get('fold', 0) + 1


def try_fold(fn, block, k, m, named, texts_all):
    name, value = m.group(2), m.group(3)
    if name in named or name not in fn.decl_lines:
        return False
    ty = fn.types.get(name)
    if ty not in INT_TYPES and ty not in FLOAT_TYPES:
        return False
    pat = re.compile(r'\b%s\b' % re.escape(name))
    if len(pat.findall(texts_all)) != 3:          # (its declaration, this assignment and one read)
        return False
    memory = MEM_READ.fullmatch(value)
    if memory:
        # One read of game memory, `M2C_FIELD(base, P<T> , offset)` or `*(P<T> )address`: folded only where nothing between
        # reaches memory or calls (so it reads what it read before, and in the same order with every other access), and the
        # use's own statement reaches memory no other way than by a store after it (see mem_free_use).
        base = memory.group(1)
        field_type = memory.group(2) or memory.group(3)
        if base is not None and not plain_value(base, fn):
            return False
        if (field_type in FLOAT_TYPES) != (ty in FLOAT_TYPES):
            return False
        e, et = None, None
        reads = {base} if base else set()
    else:
        if not plain_value(value, fn, value_ids_only=True):
            return False
        try:
            e = parse_expr(value)
        except Unreadable:
            return False
        et = expr_type(e, fn.types)
        if et is None or (ty in FLOAT_TYPES) != (et in ('f32', 'f64')):
            return False
        reads = expr_ids(e, set())
    if any(re.search(r'&\s*%s\b' % re.escape(r), texts_all) for r in reads):
        return False
    # Find the use: in a later node of this block. Every text between (and the use's own text before it) is checked.
    for j in range(k + 1, len(block)):
        later = block[j]
        texts = list(node_texts(later))
        hit = None
        for ti, (t, owner) in enumerate(texts):
            if pat.search(t):
                hit = ti
                break
        if hit is None:
            # (nothing here reads it: it may only be passed by if it changes none of the reads and has no label)
            for t, owner in texts:
                if isinstance(owner, Line) and owner.kind in ('label', 'case'):
                    return False
                if assigned_in(t, reads) or (memory and not mem_free(t, fn)):
                    return False
            continue
        for t, owner in texts[:hit]:
            if isinstance(owner, Line) and owner.kind in ('label', 'case'):
                return False
            if assigned_in(t, reads) or (memory and not mem_free(t, fn)):
                return False
        t, owner = texts[hit]
        # (Not inside a loop or a switch that starts after the assignment: it would be read again after its reads changed,
        #  or reached from the switch's start. The owner itself may be a do whose condition reads it: that's a loop too.)
        path = path_to(later, owner, [])
        if path is None or any(isinstance(n, (Do, Switch, For, While)) for n in path):
            return False
        mu = pat.search(t)
        before, after = t[:mu.start()], t[mu.end():]
        if assigned_in(before, reads) or re.match(r'\s*(?:\+\+|--|\(|\[|\.|->|=(?!=)|[-+*/%&|^]=|<<=|>>=)', after) \
                or re.search(r'(?:\+\+|--|&)\s*$', before):
            return False
        # (Whether the use is a whole operand where nothing binds tighter round it -- an assignment's right side, a return's
        #  value, a call's argument, an if's whole condition: then the value needs no parentheses there.)
        lhs = before.rstrip()
        whole = (after == ';' and (lhs.endswith('=') and not lhs.endswith(('==', '!=', '<=', '>=')) or lhs.endswith('return'))) or \
            (re.search(r'(?:\(|, )$', before) is not None and re.match(r'[),]', after) is not None) or \
            (before == '' and after == '' and not isinstance(owner, Line))
        if memory:
            # (And read every time the statement runs, as the assignment was: not inside a branch of it, nor after a && or
            #  || or ?: in it, nor in an else-if's condition. Then the reads and writes of memory happen in the same order,
            #  each as often as before -- even a read of a hardware register, which can change something, stays as it was.)
            if path != [later] or (isinstance(owner, If) and owner.arms[0][0] is not t):
                return False
            before_toks = tokens(before)
            if before_toks is None or any(x.text in ('&&', '||', '?') for x in before_toks):
                return False
            if not mem_free_use(before, after, owner, fn, ty, field_type):
                return False
            # (Read as the temporary held it, in its type: a cast, since the field itself is a place in memory, Ref<T>, which
            #  other operators would take as it is. Not where it's stored whole into a local of its type, or into a field of
            #  its type: there the read is what the temporary's own assignment did.)
            plain_store = after == ';' and field_type == ty and (
                re.fullmatch(r'\s*(\w+) = $', before) and fn.types.get(re.fullmatch(r'\s*(\w+) = $', before).group(1)) == ty or
                re.fullmatch(r'\s*M2C_FIELD\(\w+, P<%s> , -?\w+\) = $' % re.escape(ty), before))
            # (Nor right after a cast to a whole number, `(u32) temp`, where the field is of the temporary's type: the field's
            #  only conversion is to its own type, so the cast converts from that type, as it did from the temporary.)
            recast = field_type == ty and ty in INT_TYPES and re.search(r'\((?:s8|u8|s16|u16|s32|u32)\) $', before)
            shown = value if plain_store or recast else '(%s) %s' % (ty, value)
        else:
            # (The value as the temporary held it: in its type, by a cast where the expression's own type differs.)
            atomic = e.kind in ('id', 'num') and not value.startswith('-')
            shown = value if atomic or (whole and et == ty) else '(' + value + ')'
            if et != ty:
                shown = '(%s) %s' % (ty, shown if shown.startswith('(') else '(' + shown + ')')
        new = before + shown + after
        if isinstance(owner, Line):
            owner.text = new
        elif isinstance(owner, If):
            owner.arms = [(new if c is t else c, b) for c, b in owner.arms]
            if not any(c is new for c, _ in owner.arms):
                arms = []
                done = False
                for c, b in owner.arms:
                    if not done and c == t:
                        arms.append((new, b))
                        done = True
                    else:
                        arms.append((c, b))
                owner.arms = arms
        elif isinstance(owner, (Do, While)):
            owner.cond = new
        elif isinstance(owner, Switch):
            owner.expr = new
        else:
            return False
        del block[k]
        decl = fn.decl_lines.pop(name)
        fn.body.remove(decl)
        return True
    return False


# ---- Loops ----------------------------------------------------------------------------------------------------------------

def gotos_in(node, label):
    return [t for t, _ in node_texts(node) if re.search(r'\bgoto %s;' % re.escape(label), t)]


def loop_safe(node, label, in_loop=False, in_switch=False):
    """Whether, in this node, every `goto label;` can become `continue;` (it isn't inside a do or a for of its own; a switch
    is fine, continue passes through it), every break belongs to a loop or switch inside, and every continue to a loop inside;
    and the label is named nowhere but in those gotos."""
    if isinstance(node, Line):
        s = node.text.strip()
        if s == 'goto %s;' % label:
            return not in_loop
        if s == 'break;':
            return in_loop or in_switch
        if s == 'continue;':
            return in_loop
        return not re.search(r'\b%s\b' % re.escape(label), s)
    heads = [c for c, _ in node.arms] if isinstance(node, If) else [node.cond] if isinstance(node, (Do, While)) else \
        [node.expr] if isinstance(node, Switch) else []
    if any(re.search(r'\b%s\b' % re.escape(label), h) for h in heads):
        return False
    loop = in_loop or isinstance(node, (Do, For, While))
    switch = in_switch or isinstance(node, Switch)
    return all(loop_safe(n, label, loop, switch) for b in child_blocks(node) for n in b)


def only_goto(block, label):
    """Whether a block is just `goto label;` (and blanks)."""
    real = [n for n in block if not (isinstance(n, Line) and n.kind == 'blank')]
    return len(real) == 1 and isinstance(real[0], Line) and real[0].text.strip() == 'goto %s;' % label


def recover_loops(fn, stats):
    """A loop m2c left as `loop_N:` and gotos back to it, where every goto to it is in the statements after it at its own level,
    S1 ... Sk (Sk the last with one):
      Sk is `if (c) { goto loop_N; }` and the only goto:   do { S1 ... Sk-1 } while (c);
      S1 is Sk, `if (c) { B goto loop_N; }`, the only goto:  while (c) { B }
      otherwise:                                           for (;;) { S1 ... Sk break; }, each goto a continue
    (Sk's falling through is the loop's end: break, to what follows. Where Sk is `goto loop_N;` itself, the loop needs no
    break.) None of S1 ... Sk may have a break or a continue that belongs to anything outside them."""
    def visit(block):
        k = 0
        while k < len(block):
            node = block[k]
            if not (isinstance(node, Line) and node.kind == 'label' and node.text.startswith('loop_')):
                k += 1
                continue
            label = node.text.strip()[:-1]
            texts = [t for t, _ in body_texts(fn)]
            total = sum(len(re.findall(r'\bgoto %s;' % re.escape(label), t)) for t in texts)
            mentions = sum(len(re.findall(r'\b%s\b' % re.escape(label), t)) for t in texts)
            last, seen = None, 0
            for j in range(k + 1, len(block)):
                seen += len(gotos_in(block[j], label))
                if seen == total:
                    last = j
                    break
            region = block[k + 1:last + 1] if last is not None else []
            real = [n for n in region if not (isinstance(n, Line) and n.kind == 'blank')]
            if not total or mentions != total + 1 or not real or not all(loop_safe(n, label) for n in region) or \
                    (isinstance(real[0], Line) and real[0].kind in ('label', 'case')):
                k += 1
                continue
            indent = real[0].indent if not isinstance(real[0], Line) else re.match(r'\s*', real[0].text).group(0)
            sk = real[-1]
            if total == 1 and isinstance(sk, If) and len(sk.arms) == 1 and sk.els is None and only_goto(sk.arms[0][1], label):
                if len(real) == 1:
                    k += 1          # (a loop of nothing but its test: left as it is)
                    continue
                body = region[:region.index(sk)]
                shift(body, 1)
                block[k:last + 1] = [Do(indent, body, sk.arms[0][0])]
                stats['loops_do'] = stats.get('loops_do', 0) + 1
                k += 1
                continue
            if total == 1 and len(real) == 1 and isinstance(sk, If) and len(sk.arms) == 1 and sk.els is None:
                arm = sk.arms[0][1]
                tail = [n for n in arm if not (isinstance(n, Line) and n.kind == 'blank')]
                if tail and isinstance(tail[-1], Line) and tail[-1].text.strip() == 'goto %s;' % label:
                    body = arm[:arm.index(tail[-1])]
                    block[k:last + 1] = [While(indent, sk.arms[0][0], body)]
                    stats['loops_while'] = stats.get('loops_while', 0) + 1
                    k += 1
                    continue
            def to_continue(b):
                for n in b:
                    if isinstance(n, Line) and n.text.strip() == 'goto %s;' % label:
                        n.text = n.text.replace('goto %s;' % label, 'continue;')
                    for cb in child_blocks(n):
                        to_continue(cb)
            body = list(region)
            if isinstance(sk, Line) and sk.text.strip() == 'goto %s;' % label:
                body.remove(sk)
            else:
                body.append(Line(indent + 'break;', 'stmt'))
            to_continue(body)
            shift(body, 1)
            block[k:last + 1] = [For(indent, body)]
            stats['loops_for'] = stats.get('loops_for', 0) + 1
            k += 1
    walk_blocks(fn.body, visit)


# ---- Short tails copied in place of a goto -------------------------------------------------------------------------------

TAIL_LINES = 3      # (the most lines a tail may have to be copied: past that the goto reads better than the copy)


def parents(fn):
    """Each block's place: id(block) -> (the node it belongs to, the block that node is in), the body -> (None, None)."""
    out = {id(fn.body): (None, None)}

    def visit(block):
        for node in block:
            for b in child_blocks(node):
                out[id(b)] = (node, block)
                visit(b)
    visit(fn.body)
    return out


def real_nodes(block, start=0):
    return [n for n in block[start:] if not (isinstance(n, Line) and n.kind in ('blank', 'comment'))]


def next_point(block, j, places):
    """Where running goes on from block[j] (labels and cases are only marks: running passes them)."""
    while j < len(block):
        n = block[j]
        if isinstance(n, Line) and n.kind in ('blank', 'comment', 'label', 'case'):
            j += 1
            continue
        return ('at', id(n))
    return after_block(block, places)


def after_block(block, places):
    """Where running goes when it falls off the end of a block."""
    owner, outer = places[id(block)]
    if owner is None:
        return ('return',)
    if isinstance(owner, Do):
        return ('do-test', id(owner))
    if isinstance(owner, (For, While)):
        return ('loop-top', id(owner))
    # (an if's arm, or a switch's body: on after the statement)
    return next_point(outer, next(k for k, n in enumerate(outer) if n is owner) + 1, places)


def enclosing(block, places, kinds):
    """The innermost statement of these kinds that holds the block, or None."""
    while True:
        owner, outer = places[id(block)]
        if owner is None:
            return None
        if isinstance(owner, kinds):
            return id(owner)
        block = outer


def outward_jumps(nodes, in_loop=False, in_switch=False):
    """The breaks and continues in these nodes that belong to something outside them: a set of 'break', 'continue'."""
    found = set()
    for n in nodes:
        if isinstance(n, Line):
            s = n.text.strip()
            if s == 'break;' and not (in_loop or in_switch):
                found.add('break')
            if s == 'continue;' and not in_loop:
                found.add('continue')
            continue
        loop = in_loop or isinstance(n, (Do, For, While))
        switch = in_switch or isinstance(n, Switch)
        for b in child_blocks(n):
            found |= outward_jumps(b, loop, switch)
    return found


JUMP = re.compile(r'(?:return\b|break;|continue;|goto \w+;)')


def without_marks(nodes):
    """The nodes as they run, without their labels and cases: copies, the originals untouched (an if's, a loop's blocks
    copied too). Only for nodes with no switch in them."""
    out = []
    for n in nodes:
        if isinstance(n, Line):
            if n.kind not in ('label', 'case'):
                out.append(Line(n.text, n.kind))
        elif isinstance(n, If):
            out.append(If(n.indent, [(c, without_marks(b)) for c, b in n.arms], None if n.els is None else without_marks(n.els)))
        elif isinstance(n, Do):
            out.append(Do(n.indent, without_marks(n.body), n.cond))
        elif isinstance(n, For):
            out.append(For(n.indent, without_marks(n.body)))
        elif isinstance(n, While):
            out.append(While(n.indent, n.cond, without_marks(n.body)))
        else:
            raise Unreadable('a switch in a tail')
    return out


def trace_from(fn, lblock, li, places, goal, limit):
    """What runs from a label on, straight (2026-11-13, tidy2): the statements after it, past labels and cases, and out of
    the end of each block it is in (an if's branch or a switch's body: on after that statement), up to the first of
      a jump at its own level (return, break, continue, goto): 'jump', that statement the last one taken;
      the place `goal` (where a goto that is the last statement of its block goes when it falls off): 'merge', that
        statement not taken -- from there both run the same;
      the end of a function that returns nothing: 'jump', with a `return;` made for it.
    -> ([(statement, the block it is in)], how it ends), or None: it falls into a loop's test or top, or the end of a
    function that returns a value, or holds a switch, or runs past `limit` lines."""
    out, lines = [], 0
    block, j = lblock, li + 1
    while True:
        if j < len(block):
            n = block[j]
            j += 1
            if isinstance(n, Line) and n.kind in ('blank', 'comment', 'label', 'case'):
                continue
            if goal == ('at', id(n)):
                return out, 'merge'
            if any(isinstance(o, Switch) for _, o in node_texts(n)):
                return None
            out.append((n, block))
            lines += len(without_marks([n])[0].lines())
            if lines > limit:
                return None
            if isinstance(n, Line) and n.kind == 'stmt' and JUMP.match(n.text.strip()):
                return out, 'jump'
            continue
        owner, outer = places[id(block)]
        if owner is None or isinstance(owner, (Do, For, While)):
            point = after_block(block, places)
            if point == goal:
                return out, 'merge'
            if point == ('return',) and fn.head.startswith('void ') and lines < limit:
                indent = '    '
                out.append((Line(indent + 'return;', 'stmt'), fn.body))
                return out, 'jump'
            return None
        block, j = outer, next(k for k, x in enumerate(outer) if x is owner) + 1


def copy_tails(fn, stats):
    """`goto block_N;` -> a copy of what runs from `block_N:` on (trace_from: out of the label's blocks too, 2026-11-13),
    where that is at most TAIL_LINES lines, and running the copy goes on exactly where running the original would:
      it ends in a return -- the same anywhere -- or in a break or a continue that belongs to the same loop or switch at
      both places (each statement's break or continue, at its end or not, checked where it was);
      or the goto is the last statement of its block, and the copy stops where the goto's block, falling off its end, goes
      on: both run on from the same place.
    Labels and cases are left out of the copy (marks: running passes them, and they stay where they were). Not a copy with
    a goto in it, nor one ending in a goto (a goto for a goto: nothing gained). A label left with no goto is taken away."""
    places = parents(fn)
    labels = {}

    def find(block):
        for i, node in enumerate(block):
            if isinstance(node, Line) and node.kind == 'label' and node.text.strip().startswith('block_'):
                labels[node.text.strip()[:-1]] = (block, node)
    walk_blocks(fn.body, find)
    gotos = []

    def find_gotos(block):
        for j, node in enumerate(block):
            if isinstance(node, Line) and re.fullmatch(r'goto block_\w+;', node.text.strip()):
                gotos.append((block, node))
    walk_blocks(fn.body, find_gotos)
    for block, g in gotos:
        label = g.text.strip()[5:-1]
        if label not in labels:
            continue
        lblock, lnode = labels[label]
        li = next((k for k, n in enumerate(lblock) if n is lnode), None)
        k = next((k for k, n in enumerate(block) if n is g), None)
        if li is None or k is None:
            continue
        last_in_block = all(isinstance(n, Line) and n.kind in ('blank', 'comment', 'label') for n in block[k + 1:])
        goal = after_block(block, places) if last_in_block else None
        found = trace_from(fn, lblock, li, places, goal, TAIL_LINES)
        if found is None:
            continue
        run, how = found
        copies = []
        for n, home in run:
            copies += without_marks([n])
        tail_text = [l for n in copies for l in n.lines()]
        if any(re.search(r'\bgoto\b', l) for l in tail_text[:-1]):
            continue        # (a goto inside it, not at its end: left alone)
        if tail_text and tail_text[-1].strip().startswith('goto '):
            continue        # (a goto for a goto: nothing gained)
        # (a break or a continue in each statement belongs to the same loop or switch here as where the statement was)
        ok = True
        for n, home in run:
            jumps = outward_jumps([n])
            if 'continue' in jumps and enclosing(block, places, (Do, For, While)) != enclosing(home, places, (Do, For, While)):
                ok = False
            if 'break' in jumps and enclosing(block, places, (Do, For, While, Switch)) != enclosing(home, places, (Do, For, While, Switch)):
                ok = False
        if not ok:
            continue
        # The copy, each statement moved to the goto's indentation.
        g_indent = re.match(r'\s*', g.text).group(0)
        for c in copies:
            first = c.indent if not isinstance(c, Line) else re.match(r'\s*', c.text).group(0)
            if (len(g_indent) - len(first)) % 4:
                ok = False
                break
            if len(g_indent) != len(first):
                shift([c], (len(g_indent) - len(first)) // 4)
        if not ok:
            continue
        block[k:k + 1] = copies
        places = parents(fn)
        stats['tails'] = stats.get('tails', 0) + 1
    # (labels no goto names now)
    texts = '\n'.join(t for t, _ in body_texts(fn))
    for label, (lblock, lnode) in labels.items():
        if not re.search(r'\bgoto %s;' % re.escape(label), texts):
            for k, n in enumerate(lblock):
                if isinstance(n, Line) and n.kind == 'label' and n.text.strip() == label + ':':
                    del lblock[k]
                    stats['labels_gone'] = stats.get('labels_gone', 0) + 1
                    break
    drop_dead_after_jump(fn, stats)


# ---- Gotos that say what falling, break, continue or return would (2026-11-13, tidy2) ----------------------------------------

def label_places(fn):
    """Each label's place: name -> (its block, the label's line)."""
    out = {}

    def find(block):
        for node in block:
            if isinstance(node, Line) and node.kind == 'label':
                out[node.text.strip()[:-1]] = (block, node)
    walk_blocks(fn.body, find)
    return out


def index_of(block, node):
    return next(k for k, n in enumerate(block) if n is node)


def drop_unnamed_labels(fn, stats):
    """A block_N label no goto names any more, taken away (it is only a mark)."""
    for label, (lblock, lnode) in label_places(fn).items():
        if label.startswith('block_') and not any(re.search(r'\bgoto %s;' % re.escape(label), t) for t, _ in body_texts(fn)):
            del lblock[index_of(lblock, lnode)]
            stats['labels_gone'] = stats.get('labels_gone', 0) + 1


def block_nodes(fn):
    """Every node of the function, at any depth."""
    out = []

    def visit(block):
        out.extend(block)
    walk_blocks(fn.body, visit)
    return out


def after_node(key, places):
    """Where running goes on after the statement with this id (a loop or a switch: where a break inside it goes)."""
    for owner, outer in places.values():
        if outer is not None:
            for k, n in enumerate(outer):
                if id(n) == key:
                    return next_point(outer, k + 1, places)
    raise Unreadable('a statement with no place')


def loop_again(key, places):
    """Where a continue in this loop goes: a do's test, a for's or a while's top (as after_block names them)."""
    for owner, _ in places.values():
        if owner is not None and id(owner) == key:
            return ('do-test', key) if isinstance(owner, Do) else ('loop-top', key)
    raise Unreadable('a loop with no body')


def plain_jumps(fn, stats):
    """A `goto L;` right after a return, break, continue or goto in its block (no label or case between: it never runs),
    taken away; and a `goto L;` where running on from L is the same as one of these, made that one:
      nothing   falling on from the goto (to what follows it, past labels and cases, out of the ends of its blocks) comes
                to the same statement as L does -- or to another `goto L;` (cases one after another, each `goto L;`, end
                up one goto under all their cases);
      break;    L is where the innermost loop or switch holding the goto goes on after it;
      continue; L is where the innermost loop holding the goto tests again or starts its next turn (the end of a do's
                body; a for's or a while's top);
      return;   L is the end of a function that returns nothing.
    Each is what the goto did: the same statements run next. Labels no goto names any more go."""
    void = fn.head.startswith('void ')
    changed = True
    while changed:
        changed = False
        places = parents(fn)
        labels = label_places(fn)
        by_id = {id(n): n for n in block_nodes(fn)}
        gotos = []

        def find_gotos(block):
            for node in block:
                if isinstance(node, Line) and node.kind == 'stmt' and re.fullmatch(r'goto \w+;', node.text.strip()):
                    gotos.append((block, node))
        walk_blocks(fn.body, find_gotos)
        for block, g in gotos:
            label = g.text.strip()[5:-1]
            if label not in labels:
                continue
            lblock, lnode = labels[label]
            target = next_point(lblock, index_of(lblock, lnode) + 1, places)
            k = index_of(block, g)
            # (a goto right after a jump, with no label or case between: it never runs)
            before = [n for n in block[:k] if not (isinstance(n, Line) and n.kind in ('blank', 'comment'))]
            if before and isinstance(before[-1], Line) and before[-1].kind == 'stmt' and JUMP.match(before[-1].text.strip()):
                del block[k]
                stats['goto_dead'] = stats.get('goto_dead', 0) + 1
                changed = True
                break
            fall = next_point(block, k + 1, places)
            same = fall[0] == 'at' and by_id[fall[1]] is not g and isinstance(by_id[fall[1]], Line) and \
                by_id[fall[1]].text.strip() == g.text.strip()
            if fall == target or same:
                del block[k]
                stats['goto_next'] = stats.get('goto_next', 0) + 1
                changed = True
                break
            indent = re.match(r'\s*', g.text).group(0)
            inner = enclosing(block, places, (Do, For, While, Switch))
            loop = enclosing(block, places, (Do, For, While))
            new = None
            if inner is not None and after_node(inner, places) == target:
                new = 'break;'
            elif loop is not None and loop_again(loop, places) == target:
                new = 'continue;'
            elif void and target == ('return',):
                new = 'return;'
            if new:
                block[k] = Line(indent + new, 'stmt')
                stats['goto_' + new[:-1]] = stats.get('goto_' + new[:-1], 0) + 1
                changed = True
                break
    drop_unnamed_labels(fn, stats)


def drop_dead_after_jump(fn, stats):
    """Plain statements right after a return, break, continue or goto in their block, before any label, case or other
    statement (an if, a loop, a switch: a label may be inside), taken away (2026-11-13, tidy2): nothing comes to them, so
    they never run. (The copies of a tail can leave its original so once its label goes.) Never the last line that names a
    game address (keep_addresses)."""
    dead = {}

    def visit(block):
        after_jump = False
        for n in block:
            if isinstance(n, Line) and n.kind in ('blank', 'comment'):
                continue
            if after_jump and isinstance(n, Line) and n.kind == 'stmt' and not is_declaration(n):
                dead[id(n)] = n
                continue
            after_jump = isinstance(n, Line) and n.kind == 'stmt' and JUMP.match(n.text.strip()) is not None
    walk_blocks(fn.body, visit)
    gone = keep_addresses(fn, set(dead), dead)
    if not gone:
        return

    def drop(block):
        before = len(block)
        block[:] = [n for n in block if id(n) not in gone]
        stats['dead_code'] = stats.get('dead_code', 0) + before - len(block)
    walk_blocks(fn.body, drop)


def ends_in_goto(block, label):
    real = real_nodes(block)
    return bool(real) and isinstance(real[-1], Line) and real[-1].text.strip() == 'goto %s;' % label


def ends_in_jump(block):
    real = real_nodes(block)
    return bool(real) and isinstance(real[-1], Line) and real[-1].kind == 'stmt' and JUMP.match(real[-1].text.strip()) is not None


def is_declaration(node):
    return isinstance(node, Line) and node.kind == 'stmt' and re.fullmatch(
        r'\s*((?:const )?[A-Za-z_][\w<> ]*?[\w>]) ([A-Za-z_]\w*)(?: = [^;]*|\{[^;]*\}|\([^;]*\))?;(?:\s*//.*)?', node.text) is not None \
        and not re.match(r'\s*(?:return|goto)\b', node.text)


def skip_over(fn, stats):
    """if (c) { X goto L; } A L:  ->  if (c) { X } else { A } L:   In general: an if whose every branch but one ends in
    `goto L;`, with the label L later in the same block and A the statements between: each branch ending in the goto loses
    it, and the one other branch (for an if with no else: its else) gets A at its end. Running is the same: a branch that
    jumped to L comes to L without A, the other runs A and comes to L. Only where A has no label or case in it (nothing else
    comes into A), no declaration, and the other branch doesn't end in a jump of its own (A would be dead there). A break or
    continue in A still belongs to the same loop or switch: an if is neither. An empty first branch is then turned round and
    an empty else dropped, as invert does."""
    changed = True
    while changed:
        changed = False
        for label, (lblock, lnode) in label_places(fn).items():
            li = index_of(lblock, lnode)
            for i in range(li - 1, -1, -1):
                node = lblock[i]
                if not isinstance(node, If):
                    continue
                branches = [b for _, b in node.arms] + [node.els if node.els is not None else []]
                with_goto = [b for b in branches if ends_in_goto(b, label)]
                if not with_goto or len(with_goto) != len(branches) - 1:
                    continue
                other = next(k for k, b in enumerate(branches) if not ends_in_goto(b, label))
                between = lblock[i + 1:li]
                if not real_nodes(between) or ends_in_jump(branches[other]) or \
                        any(isinstance(o, Line) and o.kind in ('label', 'case') for n in between for _, o in node_texts(n)) or \
                        any(is_declaration(n) for n in between):
                    continue
                for b in with_goto:
                    del b[index_of(b, real_nodes(b)[-1])]
                del lblock[i + 1:li]
                shift(between, 1)
                if other == len(node.arms):
                    node.els = between if node.els is None else node.els + between
                else:
                    node.arms[other] = (node.arms[other][0], node.arms[other][1] + between)
                if node.els is not None and is_empty(node.els):
                    node.els = None
                if len(node.arms) == 1 and is_empty(node.arms[0][1]) and node.els is not None:
                    node.arms = [(negate(node.arms[0][0], fn.types), node.els)]
                    node.els = None
                stats['skip'] = stats.get('skip', 0) + 1
                changed = True
                break
            if changed:
                break
    drop_unnamed_labels(fn, stats)


def hoist_common(fn, stats):
    """if (c) { X s } else { Y s }  ->  if (c) { X } else { Y } s   (2026-11-13, tidy2): the same last statement in every
    branch of an if that has an else (every arm of an else-if chain too), written once after it. Each way through ran its
    branch's own statements, then s, then went on after the if: so it does now. The same text means the same in every branch
    (no branch declares anything; a break, continue, return or goto means the same just after an if, which is neither a loop
    nor a switch). A label before s in a branch stays at that branch's end: a goto to it runs s after the if, as it did."""
    def visit(block):
        k = 0
        while k < len(block):
            node = block[k]
            k += 1
            if not isinstance(node, If) or node.els is None:
                continue
            moved = []
            while True:
                branches = [b for _, b in node.arms] + [node.els]
                lasts = [real_nodes(b)[-1] if real_nodes(b) else None for b in branches]
                if any(x is None or not isinstance(x, Line) or x.kind != 'stmt' or is_declaration(x) for x in lasts) or \
                        len({x.text.strip() for x in lasts}) != 1:
                    break
                for b, x in zip(branches, lasts):
                    del b[index_of(b, x)]
                moved.insert(0, Line(node.indent + lasts[0].text.strip(), 'stmt'))
            if moved:
                block[k:k] = moved
                stats['hoist'] = stats.get('hoist', 0) + len(moved)
                k += len(moved)
    walk_blocks(fn.body, visit)


FLAT_LENGTH = 100   # (the longest condition two ifs' conditions joined by && may make: past that the two ifs read better)


def flatten_ifs(fn, stats):
    """Two ifs in one (2026-11-13, tidy2), each only a way of writing the same tests in the same order:
      } else { if (d) { X } [else ...] }  ->  } else if (d) { X } [else ...]   (the else holds just that if)
      if (c) { if (d) { X } }             ->  if (c && d) { X }                  (neither has an else, the outer holds just
                                                                                 the inner; && tests d only when c held)"""
    def visit(block):
        for node in block:
            if not isinstance(node, If):
                continue
            while node.els is not None:
                real = real_nodes(node.els)
                if len(real) != 1 or not isinstance(real[0], If) or len(real_nodes(node.els)) != len([n for n in node.els if not (isinstance(n, Line) and n.kind == 'blank')]):
                    break
                inner = real[0]
                for _, b in inner.arms:
                    shift(b, -1)
                if inner.els is not None:
                    shift(inner.els, -1)
                node.arms = node.arms + inner.arms
                node.els = inner.els
                stats['else_if'] = stats.get('else_if', 0) + 1
            while len(node.arms) == 1 and node.els is None:
                body = node.arms[0][1]
                real = [n for n in body if not (isinstance(n, Line) and n.kind == 'blank')]
                if len(real) != 1 or not isinstance(real[0], If) or len(real[0].arms) != 1 or real[0].els is not None:
                    break
                inner = real[0]
                cond = wrap(node.arms[0][0]) + ' && ' + wrap(inner.arms[0][0])
                if len(cond) > FLAT_LENGTH:
                    break
                shift(inner.arms[0][1], -1)
                node.arms = [(cond, inner.arms[0][1])]
                stats['and_if'] = stats.get('and_if', 0) + 1
    walk_blocks(fn.body, visit)


# ---- All of it -------------------------------------------------------------------------------------------------------------

PASSES = ('invert', 'dead_out', 'unused', 'fold', 'loops', 'jumps', 'skip', 'hoist', 'tails', 'flatten')   # (the last four: 2026-11-13)


def tidy(name, cpp, named=(), passes=PASSES):
    """The function's C++ tidied, and what changed ({} when nothing did, or when it can't be read: then cpp is as it was)."""
    try:
        fn = Function(cpp)
        if fn.text() != cpp:
            return cpp, {'unreadable': 'round trip'}
        stats = {}
        for _ in range(4):
            before = fn.text()
            if 'invert' in passes:
                invert_empty_then(fn, stats)
            if 'dead_out' in passes:
                drop_dead_out_writes(fn, stats)
            if 'unused' in passes:
                drop_unused(fn, stats, named)
            if 'fold' in passes:
                fold_temps(fn, stats, named)
            if 'loops' in passes:
                recover_loops(fn, stats)
            if 'jumps' in passes:
                plain_jumps(fn, stats)
            if 'skip' in passes:
                skip_over(fn, stats)
            if 'hoist' in passes:
                hoist_common(fn, stats)
            if 'tails' in passes:
                copy_tails(fn, stats)
            if 'flatten' in passes:
                flatten_ifs(fn, stats)
            if fn.text() == before:
                break
        return fn.text(), stats
    except Unreadable as e:
        return cpp, {'unreadable': str(e)}
