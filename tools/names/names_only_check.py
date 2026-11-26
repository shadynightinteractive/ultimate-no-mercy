# (namesprep, 2026-11-14) Offline stand-in for the project's check-names (which compiles lifted.cpp with and without the names
# and compares the machine code; this folder has no compiler setup for that). For each function in changes.json: its C++ as
# released (with the base names.json) and with the changes applied, both rendered as lift.py's release does (fielded_cpp +
# named_cpp), comments dropped, split into tokens. They must be the same token for token, except identifiers renamed, one to
# one, the same way everywhere in the function. Anything else -- a number, an operator, a type, a field or a call changed, two
# names merged into one -- is reported. 0 problems expected. The project's check-names still has the last word when it merges.
# Usage: python names_only_check.py        (NAMES_CHANGES=<file> to check another changes file)
from paths import D, L, B, T, BASE, CANDS, CH
import json, os, re, sys
os.environ.setdefault('LIFT_MODREP', '0')
sys.path.insert(0, L)
import lift

base = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(base); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
TOKEN = re.compile(r'[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+[uUlL]*|\d+\.?\d*(?:[eE][-+]?\d+)?[fFuUlL]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|::|->|<<=|>>=|[-+*/%&|^!=<>]=|&&|\|\||<<|>>|\+\+|--|\S')


def rendered(names):
    alln = lift.with_copy_names(names, cands)
    lift.field_param_kinds(cands, alln, kinds, sources)
    out = {}
    for f in ch:
        if f in cands and 'cpp' in cands[f]:
            v = dict(cands[f], cpp=lift.fielded_cpp(f, cands[f]['cpp'], alln.get(f), kinds, sources))
            out[f] = lift.named_cpp(f, v, alln.get(f))
    return out


# The record macros of the release (lift.fields_preamble: WRESTLER(w, x), GAME_VAR(tv_type), AI_RECORD_IN(array, bytes, name)
# ...): the arguments a macro only ever uses qualified -- field::Wrestler::x, field::var::tv_type -- are field names, never a
# local, so a local may have the same name (names5's did; check-names agreed). Those arguments are compared as field names,
# apart from the locals: FIELD_ARGS = {macro: the argument positions that are field names}.
FIELD_ARGS = {}
for line in lift.fields_preamble(kinds, sources):
    m = re.match(r'#define (\w+)\(([^)]*)\) (.*)$', line)
    if m:
        params = [p.strip() for p in m.group(2).split(',')]
        FIELD_ARGS[m.group(1)] = {i for i, p in enumerate(params)
                                  if re.search(r'\b%s\b' % p, m.group(3))
                                  and not re.search(r'(?<!::)(?<!\w)%s\b(?!##)' % p, m.group(3).replace(p + '##', '##'))}


def tokens(cpp):
    cpp = re.sub(r'/\*.*?\*/', ' ', cpp, flags=re.S)
    cpp = re.sub(r'//[^\n]*', ' ', cpp)
    toks = TOKEN.findall(cpp)
    out, stack = [], []          # stack: [macro or call name, argument index]
    for i, t in enumerate(toks):
        if t == '(':
            stack.append([toks[i - 1] if i and re.fullmatch(r'[A-Za-z_]\w*', toks[i - 1]) else '', 0])
        elif t == ')' and stack:
            stack.pop()
        elif t == ',' and stack:
            stack[-1][1] += 1
        elif (stack and stack[-1][1] in FIELD_ARGS.get(stack[-1][0], ()) and toks[i - 1] in ('(', ',')
              and i + 1 < len(toks) and toks[i + 1] in (',', ')')):
            t = 'field::' + t
        out.append(t)
    return out


before, after = rendered(base), rendered(merged)
problems = []
renamed = 0
for f in sorted(ch):
    if f not in before:
        problems.append((f, 'not converted'))
        continue
    a, b = tokens(before[f]), tokens(after[f])
    if len(a) != len(b):
        problems.append((f, 'token count differs', len(a), len(b)))
        continue
    fwd, back = {}, {}
    for x, y in zip(a, b):
        ident = re.fullmatch(r'[A-Za-z_]\w*', x) and re.fullmatch(r'[A-Za-z_]\w*', y)   # (field::x isn't one)
        if x == y and not ident:
            continue
        if not ident or (x != y and (x in lift.CPP_KEYWORDS or y in lift.CPP_KEYWORDS)):
            problems.append((f, 'changed beyond a name', x, y))
            break
        if fwd.setdefault(x, y) != y or back.setdefault(y, x) != x:
            problems.append((f, 'not one to one', x, fwd[x], y, back[y]))
            break
    renamed += sum(1 for x, y in fwd.items() if x != y)
print('checked', len(ch), 'functions;', renamed, 'identifiers renamed;', len(problems), 'problems')
for p in problems[:40]:
    print(' ', p)
