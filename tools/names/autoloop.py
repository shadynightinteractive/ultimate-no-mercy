# Loop counters (var_ set to 0 once, stepped by 1, compared) -> i, j, k; stack places only passed by address to a named
# callee -> that callee's parameter name.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
new = set()
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main field P ctx frame game NULL rdram index abs min max exp log sqrt sin cos floor time signal remove rename free select link read write open close clock round trunc div rand pow y0 y1 j0 j1 yn jn step'.split())
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
def split_args(s):
    out, depth, cur = [], 0, ''
    for ch_ in s:
        if ch_ in '(<[': depth += 1
        if ch_ in ')>]': depth -= 1
        if ch_ == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch_
    if cur.strip(): out.append(cur.strip())
    return out
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f in new or f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'):
        continue
    info = merged[f]; cpp = cands[f]['cpp']
    vars_ = dict(info.get('vars', {}))
    code = re.sub(r'//.*', '', cpp)
    raw_ids = set(re.findall(r'\b[A-Za-z_]\w*\b', code))
    used = set(vars_.values()) | {info['name']}
    props = {}
    # loop counters
    counters = []
    for v in sorted(set(re.findall(r'\b(var_\w+)\b', code))):
        if v in vars_: continue
        assigns = re.findall(r'(?<![\w.>])%s\s*((?:[-+*/&|^]|<<|>>)?=)(?!=)\s*([^;]*);' % v, code)
        incs = re.findall(r'\b%s\s*(\+\+|--)|(\+\+|--)\s*%s\b' % (v, v), code)
        if incs or not assigns: continue
        kinds = [(op, rhs.strip()) for op, rhs in assigns]
        if kinds.count(('=', '0')) != 1 or any(k not in (('=', '0'), ('+=', '1')) for k in kinds) or ('+=', '1') not in kinds:
            continue
        if not re.search(r'\b%s\s*(<|<=|>=|>|!=)\s*' % v, code) and not re.search(r'(<|<=|>=|>|!=)\s*(\(\w+\)\s*)?%s\b' % v, code):
            continue
        first = re.search(r'(?<![\w.>])%s\s*=\s*0;' % v, code).start()
        counters.append((first, v))
    letters = [n for n in ('i', 'j', 'k') if n not in raw_ids and n not in used]
    for (_, v), n in zip(sorted(counters), letters):
        props[v] = n; used.add(n); stats['counter'] += 1
    # stack places passed by address only
    for m in re.finditer(r'Ref<[\w<> ]+> (sp[0-9A-F]+)\(frame\.at\(0x[0-9A-F]+\)\);', code):
        sp = m.group(1)
        if sp in vars_: continue
        uses = [u.start() for u in re.finditer(r'\b%s\b' % sp, code)]
        addr = [u.start() for u in re.finditer(r'&%s\b' % sp, code)]
        if len(uses) - 1 != len(addr) or not addr:
            continue
        pnames = set()
        for call in re.finditer(r'lift_call<[\w<> ]+>\(ctx, ::(func_\w+)\)\(', code):
            depth, i = 1, call.end()
            while depth and i < len(code):
                depth += {'(': 1, ')': -1}.get(code[i], 0); i += 1
            args = split_args(code[call.end():i - 1])
            for k, a in enumerate(args):
                if re.fullmatch(r'(\(\w+\)\s*)?&%s' % sp, a):
                    ce = merged.get(call.group(1))
                    pn = ce.get('vars', {}).get('arg%d' % k) if ce and not ce.get('guess') else None
                    pnames.add(pn)
        if len(pnames) == 1 and None not in pnames:
            n = pnames.pop()
            if n and n not in used and n not in raw_ids and n not in KW and not M2C.match(n):
                props[sp] = n; used.add(n); stats['out_param'] += 1
    if props:
        out[f] = props
json.dump(out, open(T + 'autoloop.json', 'w'), indent=1)
print(stats, len(out))
