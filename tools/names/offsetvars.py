# (names5, 2026-11-13) Rule O: an m2c temp written once as `X * C` or `X << C` (C a constant, in the release's fielded C++), where X
# is a named local or parameter, and read only inside address sums (`(t + ...)` / `(... + t)`, or `t + ...` as a whole value), holds
# X's byte offset in a table of C-byte records (or 1 << C): it is named <x>_offset (X's name without a trailing _2.. counter);
# _2, _3 in order of the code. Clashes with any identifier of the function, keywords and m2c-looking names are skipped.
# Writes offset_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
from rulelib import root_name   # (fablenames, 2026-11-14: x for x_2 only when x is a name of the function -- names can now end in an offset or a constant: record_half_4, x_minus_10)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(merged, cands)
lift.field_param_kinds(cands, alln, kinds, sources)
KW = lift.CPP_KEYWORDS | {'main', 'field', 'P', 'Ref', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'index', 'step'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; v = cands[f]
    try:
        fc = lift.fielded_cpp(f, v['cpp'], alln.get(f), kinds, sources)
        text = lift.named_cpp(f, dict(v, cpp=fc), alln.get(f))
    except Exception:
        stats['render_error'] += 1; continue
    body = re.sub(r'//.*', '', text.split('{', 1)[1])
    raw = re.sub(r'//.*', '', v['cpp'])
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw)) | set(re.findall(r'\b[A-Za-z_]\w*\b', body))
    vars_ = dict(info.get('vars', {}))
    used = set(vars_.values()) | {info['name']}
    lines = body.split('\n')
    wanted = []
    for i, l in enumerate(lines):
        m = re.match(r'\s*((?:temp|var)_\w+) = (?:\(\w+\) )?\(?(?:\(\w+\) )*([a-z]\w*)\)? (\*|<<) (0x[0-9A-F]+|\d+);$', l)
        if not m: continue
        t, x, op, c = m.groups()
        if t in vars_ or M2C.match(x) or x.startswith('lift_') or x in KW: continue
        writes = [j for j, y in enumerate(lines) if re.match(r'\s*%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), y)]
        if writes != [i]: continue
        reads = []
        ok = True
        for j, y in enumerate(lines):
            if j == i or re.match(r'\s*(?:const )?[\w<>* ]+? %s;$' % re.escape(t), y): continue
            for mm in re.finditer(r'(?<![\w.>])%s\b' % re.escape(t), y):
                reads.append(j)
                s, e = mm.start(), mm.end()
                before = y[:s].rstrip(); after = y[e:].lstrip()
                if not (after.startswith('+ ') or before.endswith('+')):
                    ok = False
        if not ok or not reads:
            stats['other_use'] += 1; continue
        base = root_name(x, set(vars_.values()))
        wanted.append((t, base + '_offset', i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in ids or nm in used or M2C.match(nm) or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm) or len(nm) > 40:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/offset_edits.json', 'w'), indent=0)
print(stats, len(out))
c = collections.Counter(v.rstrip('_0123456789') for d in out.values() for v in d.values())
print(c.most_common(40))
