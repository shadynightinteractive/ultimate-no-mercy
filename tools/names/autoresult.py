# (names3, 2026-11-12) Rule V: an m2c temp / var whose every read is `return v;` (or `return (type) v;`), however many times
# it is written, holds the function's result: `result` (_2, _3 in order of the code when there are more). Names that clash
# with any identifier of the function are skipped. Writes result_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
out = {}; stats = collections.Counter()
for f, e in sorted(n.items()):
    if f not in c or 'cpp' not in c[f] or f.startswith('modrep_'): continue
    code = re.sub(r'//.*', '', c[f]['cpp']); vars_ = e.get('vars', {})
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', code)); used = set(vars_.values()) | {e['name']}
    found = []
    for t in sorted(set(re.findall(r'\b((?:temp|var)_\w+)\b', code)) - set(vars_)):
        occ = [m.start() for m in re.finditer(r'(?<![\w.>])%s\b' % re.escape(t), code)]
        writes = [m.start() for m in re.finditer(r'(?<![\w.>])%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), code)]
        decl = re.search(r'(?m)^\s+(?:const )?[\w<>* ]+?\s%s;$' % re.escape(t), code)
        reads = [o for o in occ if o not in writes and not (decl and decl.start() <= o <= decl.end())]
        if not reads or not writes: continue
        if all(re.fullmatch(r'\s*return (?:\(\w+\) )?%s;' % re.escape(t), code[code.rfind('\n', 0, o) + 1:code.find('\n', o)]) for o in reads):
            found.append((occ[0], t))
    k = 0
    for _, t in sorted(found):
        k += 1
        nm = 'result' if k == 1 else 'result_%d' % k
        if nm in ids or nm in used: stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + 'result_edits.json', 'w'), indent=0)
print(stats, len(out))
