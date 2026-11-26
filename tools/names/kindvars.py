# (names3, 2026-11-12) Rule K: an m2c temp or var whose kind of record the release's field typing establishes (lift.field_kinds:
# every assignment of it is a record of that kind, by fields.json's evidence) is named after the kind: wrestler, ai_record ..
# (_2, _3 in order of the code). Names that clash, keywords and m2c-looking names are skipped. Writes kind_edits.json.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(merged, cands)
lift.field_param_kinds(cands, alln, kinds, sources)
KW = lift.CPP_KEYWORDS | {'main', 'field', 'P', 'Ref', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'index', 'step'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
SPECIAL = {'OSPfs': 'pfs', 'NSynth': 'synth'}
def snake(k): return SPECIAL.get(k) or re.sub(r'(?<!^)(?=[A-Z])', '_', k).lower()
out = {}; stats = collections.Counter(); bykind = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; cpp = cands[f]['cpp']
    typed = lift.field_kinds(f, cpp, alln.get(f), kinds, sources)
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    used = set(vars_.values()) | {info['name']}
    wanted = []
    for v, k in typed.items():
        if not isinstance(v, str) or v in vars_ or not re.match(r'(temp|var)_', v): continue
        pos = re.search(r'\b%s\b' % re.escape(v), cpp.split('{', 1)[1]).start() if re.search(r'\b%s\b' % re.escape(v), cpp.split('{', 1)[1]) else 0
        wanted.append((v, snake(k), pos))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in ids or nm in used or M2C.match(nm) or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm):
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1; bykind[cand] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + 'kind_edits.json', 'w'), indent=0)
print(stats, len(out)); print(bykind.most_common(40))
