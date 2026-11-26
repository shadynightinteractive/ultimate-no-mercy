# (names5, 2026-11-13) Rule X: an m2c temp written once with a simple expression of ONE named value x (a named local or
# parameter; casts allowed) and a constant is named by the expression's own words, so the reader sees what it holds:
#   x + c -> x_plus_c, x - c -> x_minus_c (c up to 16)      x / c -> x_over_c      x % c -> x_mod_c
#   x & (2^n - 1) -> x_low_n_bits (n 2..7; 1, 8 and 16 bits are rule M's)          (constants in decimal)
# x's own _2.. counter is dropped. Duplicates _2, _3 in order of the code. Names that clash with any identifier of the function or
# are longer than 36 characters are skipped. Raw C++ with the entry's names. Writes expr_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
from rulelib import root_name   # (fablenames, 2026-11-14: x for x_2 only when x is a name of the function -- names can now end in an offset or a constant: record_half_4, x_minus_10)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
EXPR = re.compile(r'(?:\(\w+\) )?\(?(?:\(\w+\) )*(\w+)\)? (\+|-|/|%|&) (0x[0-9A-F]+|\d+)U?')
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    lines = [l.strip() for l in raw.split('\n')]
    wanted = []
    for i, s in enumerate(lines):
        m = re.match(r'(temp_\w+) = (.+);$', s)
        if not m or m.group(1) in vars_: continue
        t, val = m.groups()
        mm = EXPR.fullmatch(val)
        if not mm: continue
        x, op, c = mm.groups(); c = int(c, 0)
        if len([j for j, y in enumerate(lines) if re.match(r'%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), y)]) != 1: continue
        nx = vars_.get(x, x)
        if M2C.match(nx) or nx.startswith('lift_') or not re.fullmatch(r'[a-z]\w*', nx): stats['unnamed'] += 1; continue
        nx = root_name(nx, set(vars_.values()))
        if op == '+' and 0 < c <= 16: cand = '%s_plus_%d' % (nx, c)
        elif op == '-' and 0 < c <= 16: cand = '%s_minus_%d' % (nx, c)
        elif op == '/' and c > 1: cand = '%s_over_%d' % (nx, c)
        elif op == '%' and c > 1: cand = '%s_mod_%d' % (nx, c)
        elif op == '&' and c in (3, 7, 15, 31, 63, 127): cand = '%s_low_%d_bits' % (nx, c.bit_length())
        else: stats['other_op'] += 1; continue
        if len(cand) > 36: stats['long'] += 1; continue
        wanted.append((t, cand, i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in ids or nm in used:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/expr_edits.json', 'w'), indent=0)
print(stats, len(out))
c = collections.Counter(re.sub(r'_\d+$', '', v) for d in out.values() for v in d.values())
print(c.most_common(30))
