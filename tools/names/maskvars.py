# (names4, 2026-11-12) Rule M: an m2c temp written once as `X & C` in the release's fielded C++, where X is a named local or
# a named record field read (MACRO(r, field)) and C a single bit, holds that bit: it is named <x>_bit<n> (X's name without a
# trailing _2.. counter); C = 0xFF / 0xFFFF give <x>_low_byte / <x>_low_half. Duplicates _2, _3; clashes, keywords and
# m2c-looking names skipped. Writes mask_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
from rulelib import root_name   # (fablenames, 2026-11-14: x for x_2 only when x is a name of the function -- names can now end in an offset or a constant: record_half_4, x_minus_10)
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
        m = re.match(r'\s*((?:temp|var)_\w+) = (?:\(\w+\) )?(.+?) & (0x[0-9A-F]+|\d+)U?;$', l)
        if not m: continue
        t, x, c = m.groups()
        x = re.sub(r'\bGAME_VAR(?:_AS)?\((\w+)(?:, [\w<>* ]+)?\)', r'GAME_VAR(game, \1)', x)   # (names5) a global's name, not its type
        if t in vars_: continue
        c = int(c, 0)
        mm = re.fullmatch(r'(?:(?!M2C_FIELD|LIFT_)[A-Z][A-Z0-9_]*?)(?:_AS)?\((?:[^()]|\([^()]*\))+?, ([a-z]\w*)(?:, [\w<>* ]+)?\)', x)
        if mm: base = mm.group(1)
        elif re.fullmatch(r'[a-z]\w*', x) and not M2C.match(x): base = root_name(x, set(vars_.values()))
        else: continue
        if M2C.match(base) or base.startswith('lift_'): continue
        writes = [j for j, y in enumerate(lines) if re.match(r'\s*%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), y)]
        if len(writes) != 1: continue
        if c and c & (c - 1) == 0: cand = '%s_bit%d' % (base, c.bit_length() - 1)
        elif c == 0xFF: cand = base + '_low_byte'
        elif c == 0xFFFF: cand = base + '_low_half'
        else: continue
        wanted.append((t, cand, i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in ids or nm in used or M2C.match(nm) or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm):
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/mask_edits.json', 'w'), indent=0)
print(stats, len(out))
print(collections.Counter(re.sub(r'_\d+$', '', x) for d in out.values() for x in d.values()).most_common(30))
