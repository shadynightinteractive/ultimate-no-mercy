# (names2, 2026-11-12) Two more deterministic local-name rules, on top of namesq's autovars / autocall / autoloop:
#  A. a temp written once and read once, as a whole argument of a call to a confirmed (guess false) function whose
#     parameter there has a name: it takes that name (the value handed over as `name`);
#  R. a temp written once and read once, by `return temp;`: `result`.
# Duplicates get _2, _3 (in order of the code); names that clash with any identifier of the function, a keyword or an
# m2c-looking name are skipped. Writes arg_edits.json for put.py ({func: {"vars+": {...}}}).
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main field P Ref ctx frame game NULL rdram index abs min max exp log sqrt sin cos floor time signal remove rename free select link read write open close clock round trunc div rand pow y0 y1 j0 j1 yn jn step'.split())
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
GENERIC = {'i', 'j', 'k', 'n', 'value', 'a', 'b', 'x0', 'unused'}

def split_args(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '(<[': depth += 1
        if c in ')>]': depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += c
    if cur.strip(): out.append(cur.strip())
    return out

def params_of(f):
    if f not in cands or 'cpp' not in cands[f]: return None
    head = cands[f]['cpp'].split('{', 1)[0]
    m = re.search(r'\((.*)\)\s*$', head.strip(), re.S)
    if not m: return None
    ps = [p for p in split_args(m.group(1)) if 'recomp_context' not in p and 'uint8_t* rdram' not in p]
    return [p.split()[-1].lstrip('*&') for p in ps]

def call_args(code, start):
    """The argument list of the call whose '(' is at start."""
    depth = 0
    for i in range(start, len(code)):
        if code[i] == '(': depth += 1
        elif code[i] == ')':
            depth -= 1
            if depth == 0: return code[start + 1:i]
    return None

out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; cpp = cands[f]['cpp']
    code = re.sub(r'//.*', '', cpp)
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', code))
    used = set(vars_.values()) | {info['name']}
    wanted = []
    temps = sorted({t for t in re.findall(r'\b(temp_\w+|var_\w+)\b', code) if t not in vars_})
    for t in temps:
        occ = [m.start() for m in re.finditer(r'(?<![\w.>])%s\b' % re.escape(t), code)]
        writes = re.findall(r'(?<![\w.>])%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), code)
        decl = re.search(r'(?m)^\s+(?:const )?[\w<>* ]+?\s%s;$' % re.escape(t), code)
        if len(writes) != 1 or len(occ) != (3 if decl else 2): continue
        last = occ[-1]
        line = code[code.rfind('\n', 0, last) + 1:code.find('\n', last)]
        if re.fullmatch(r'\s*return (?:\(\w+\) )?%s;' % re.escape(t), line):
            wanted.append((t, 'result', last)); stats['return'] += 1; continue
        # a whole argument of a call to a named function?
        for m in re.finditer(r'::(\w+)(?:<[^>()]*>)?\)\(', code):
            s = m.end() - 1
            args = call_args(code, s)
            if args is None or not (s < last < s + len(args) + 2): continue
            al = split_args(args)
            k = [i for i, a in enumerate(al) if re.fullmatch(r'(?:\((?:s32|u32|s16|u16|s8|u8|f32|P<[\w<> ]+>)\) ?)?%s' % re.escape(t), a)]
            if len(k) != 1: break
            callee = m.group(1)
            ce = merged.get(callee)
            ps = params_of(callee)
            if not ce or ce.get('guess') or not ps or len(ps) != len(al): break
            pname = ce.get('vars', {}).get(ps[k[0]])
            if pname and pname not in GENERIC and not M2C.match(pname):
                wanted.append((t, pname, last)); stats['argument'] += 1
            break
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in ids or nm in used or M2C.match(nm) or nm.startswith('lift_') or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm):
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + 'arg_edits.json', 'w'), indent=0)
print(stats, len(out))
c = collections.Counter(re.sub(r'_\d+$', '', v) for d in out.values() for v in d.values())
print(c.most_common(40))
