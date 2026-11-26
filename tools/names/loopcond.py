# (names5, 2026-11-13) Rule LC: an m2c temp written once as a comparison (`t = a < b;`, any of < <= > >= == !=, casts allowed) and
# read only by a do-while's test `} while (t != 0);` is whether the loop goes round again: it is named loop_again (_2, _3 in order
# of the code); read only by `} while (t == 0);` it is named loop_done. lift_out_<reg> mirrors don't count as reads. Names that
# clash with any identifier of the function are skipped. Raw C++. Writes loopcond_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
from rulelib import root_name   # (fablenames, 2026-11-14: x for x_2 only when x is a name of the function -- names can now end in an offset or a constant: record_half_4, x_minus_10)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
CMP = re.compile(r'^[^;=<>!]+? (?:<|<=|>|>=|==|!=) [^;=<>!]+$')
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
        if not CMP.match(val) or '<<' in val or '>>' in val: continue
        occ = [j for j, y in enumerate(lines) if re.search(r'(?<![\w.>])%s\b' % re.escape(t), y)]
        rest = [j for j in occ if j != i and not re.match(r'(?:const )?[\w<>* ]+ %s;$' % re.escape(t), lines[j])
                and not re.match(r'lift_out_\w+ = (?:\([\w<> ]+\) )?%s;$' % re.escape(t), lines[j])]
        if not rest: stats["unread"] += 1; continue
        r = lines[rest[0]] if len(rest) == 1 else ''
        if r == '} while (%s != 0);' % t: cand = 'loop_again'
        elif r == '} while (%s == 0);' % t: cand = 'loop_done'
        else:
            # Rule CN: otherwise, when both sides are named (a local, a parameter, or a small constant on the right), the
            # comparison's own words: a_below_b (<), a_at_most_b (<=), a_above_b (>), a_at_least_b (>=), a_is_b (==), a_not_b (!=).
            mm = re.fullmatch(r'(?:\(\w+\) )?(\w+) (<|<=|>|>=|==|!=) (?:\(\w+\) )?(\w+?)U?', val)
            if not mm: stats['other'] += 1; continue
            a, op, b = mm.groups()
            def nm_of(x):
                if x in vars_: x = vars_[x]
                if re.fullmatch(r'-?(?:0x[0-9A-F]+|\d+)', x):
                    v = int(x, 0)
                    return str(v) if 0 <= v <= 9999 else None
                if M2C.match(x) or x.startswith('lift_') or not re.fullmatch(r'[a-z]\w*', x): return None
                return root_name(x, set(vars_.values()))
            na, nb = nm_of(a), nm_of(b)
            if not na or not nb or na[0].isdigit(): stats['unnamed_side'] += 1; continue
            cand = '%s_%s_%s' % (na, {'<': 'below', '<=': 'at_most', '>': 'above', '>=': 'at_least', '==': 'is', '!=': 'not'}[op], nb)
            if len(cand) > 36: stats['long'] += 1; continue
        wanted.append((t, cand, i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in ids or nm in used:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/loopcond_edits.json', 'w'), indent=0)
print(stats, len(out))
