# (names5, 2026-11-13) Rule CP, after rule C (copyvars.py): an m2c var that is only ever the source place of rule C's copied_<off>
# words (`copied_x = M2C_FIELD(v, ..)` / lift_unaligned_load32(v, ..)) is named copy_from; one that is only ever their destination
# (`M2C_FIELD(v, ..) = copied_x` / lift_unaligned_store32(v, ..)) copy_to (_2, _3 in order of the code). Every write of the var
# must be a plain `v = ...;` or a step `v += C;` / `v -= C;` (a pointer walking the two blocks), and every other use of it a
# compare (the loop's end test) or a `lift_out_<reg> = v;` mirror. Writes copyptr_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    vars_ = dict(info.get('vars', {}))
    copied = {k for k, v in vars_.items() if re.fullmatch(r'copied_[0-9a-f]+(?:_\d+)?', v)}
    if not copied: continue
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    lines = raw.split('\n')
    role = collections.defaultdict(set); first = {}
    for i, l in enumerate(lines):
        for t in copied:
            m = re.match(r'\s*%s = (?:\([\w<> ]+\) )?(?:M2C_FIELD\((var_\w+), |lift_unaligned_load32\((var_\w+), )' % re.escape(t), l)
            if m: v = m.group(1) or m.group(2); role[v].add('from'); first.setdefault(v, i)
            m = re.match(r'\s*(?:M2C_FIELD\((var_\w+), P<[\w<> ]+> , (?:0x[0-9A-F]+|\d+)\) = (?:\([\w<> ]+\) )?%s;|lift_unaligned_store32\((var_\w+), (?:0x[0-9A-F]+|\d+), %s\);)$' % (re.escape(t), re.escape(t)), l)
            if m: v = m.group(1) or m.group(2); role[v].add('to'); first.setdefault(v, i)
    wanted = []
    for v, r in role.items():
        if v in vars_ or len(r) != 1: continue
        ok = True
        for i, l in enumerate(lines):
            for mm in re.finditer(r'(?<![\w.>])%s\b' % re.escape(v), l):
                s = l.strip()
                if re.match(r'(?:const )?[\w<>* ]+ %s;$' % re.escape(v), s): continue
                if re.match(r'%s = [^;]*;$' % re.escape(v), s) and s.count(v) == 1: continue
                if re.match(r'%s [-+]= (?:0x[0-9A-F]+|\d+);$' % re.escape(v), s): continue
                if re.match(r'lift_out_\w+ = (?:\([\w<> ]+\) )?%s;$' % re.escape(v), s): continue
                after = l[mm.end():].lstrip(); before = l[:mm.start()].rstrip()
                if re.search(r'(?:M2C_FIELD|lift_unaligned_(?:load|store)32)\($', before) and any(re.search(r'\b%s\b' % c, l) for c in copied): continue
                if re.match(r'(?:!=|==|<|>|<=|>=) ', after) or re.search(r'(?:!=|==|<|>|<=|>=)$', before): continue
                ok = False
        if not ok: stats['other_use'] += 1; continue
        wanted.append((v, 'copy_' + list(r)[0], first[v]))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in ids or nm in used:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/copyptr_edits.json', 'w'), indent=0)
print(stats, len(out))
