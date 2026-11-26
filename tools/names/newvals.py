# (names4, 2026-11-12) Rule N: an m2c temp written once (no call in the value) and read once, by a store into a named
# record field in the release's fielded C++ (`WRESTLER(w, phase) = temp;`, any record macro, a cast allowed), carries the
# field's new value: it is named new_<field> (_2, _3 in order of the code). Clashes with any identifier of the function,
# keywords and m2c-looking names are skipped. Writes new_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift, pickle
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(merged, cands)
lift.field_param_kinds(cands, alln, kinds, sources)
KW = lift.CPP_KEYWORDS | {'main', 'field', 'P', 'Ref', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'index', 'step'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
MAC = '|'.join(sorted({k['macro'] for k in json.load(open(L + 'fields.json')).values() if isinstance(k, dict) and k.get('macro')}, key=len, reverse=True))
STORE = re.compile(r'^\s*(?:(?!M2C_FIELD|LIFT_)[A-Z][A-Z0-9_]*?)(?:_AS)?\((?:[^()]|\([^()]*\))+?, (\w+)(?:, [\w<>* ]+)?\) = (?:\([\w<>* ]+\) ?)?(\w+);$')
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; v = cands[f]
    try:
        fc = lift.fielded_cpp(f, v['cpp'], alln.get(f), kinds, sources)
        text = lift.named_cpp(f, dict(v, cpp=fc), alln.get(f))
    except Exception as ex:
        stats['render_error'] += 1; continue
    body = re.sub(r'//.*', '', text.split('{', 1)[1])
    raw = re.sub(r'//.*', '', v['cpp'])
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw)) | set(re.findall(r'\b[A-Za-z_]\w*\b', body))
    vars_ = dict(info.get('vars', {}))
    used = set(vars_.values()) | {info['name']}
    wanted = []
    lines = body.split('\n')
    for i, l in enumerate(lines):
        l = re.sub(r'\bGAME_VAR(?:_AS)?\((\w+)(?:, [\w<>* ]+)?\)', r'GAME_VAR(game, \1)', l)   # (names5) a global's name, not its type
        m = STORE.match(l)
        if not m: continue
        fld, t = m.group(1), m.group(2)
        if not re.fullmatch(r'(temp|var)_\w+', t) or t in vars_ or not re.fullmatch(r'[a-z][a-z0-9_]*', fld): continue
        occ = [j for j, x in enumerate(lines) if re.search(r'(?<![\w.>])%s\b' % re.escape(t), x) and not re.match(r'\s*lift_out_\w+ = %s;' % re.escape(t), x)]
        decl = [j for j in occ if re.match(r'\s*(?:const )?[\w<>* ]+? %s;$' % re.escape(t), lines[j])]
        rest = [j for j in occ if j not in decl]
        writes = [j for j in rest if re.match(r'\s*%s = [^;]*;$' % re.escape(t), lines[j])]
        if len(rest) != 2 or len(writes) != 1 or i not in rest or writes[0] == i: continue
        val = lines[writes[0]].split('=', 1)[1]
        if re.search(r'\b[a-z_]\w*\(', re.sub(r'\b[A-Z][A-Z0-9_]*\(|P<', '', val)): stats['call_in_value'] += 1; continue
        if lines[writes[0]].count(t) != 1 or l.count(t) != 1: continue
        wanted.append((t, 'new_' + fld, writes[0]))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in ids or nm in used or M2C.match(nm) or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm):
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/new_edits.json', 'w'), indent=0)
print(stats, len(out))
print(collections.Counter(re.sub(r'_\d+$', '', x) for d in out.values() for x in d.values()).most_common(30))
