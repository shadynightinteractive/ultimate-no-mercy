# (fablenames, 2026-11-14) Rule N2: newvals.py's rule N (an m2c temp written once and read once, by a store into a named place of
# the release's fielded C++, carries that place's new value: new_<field>) with the stored-to place read by the macro's own
# parameters. newvals took the macro's last simple argument as the field, which is wrong for the shapes with more arguments:
# GAME_ARRAY(scene_actor_animations, temp_a0) = t gave new_temp_a0, MENU_ROW_VIA(row, style, y) = t gave new_style (the anchor),
# OBJECT_SHAPE_IN(object_shapes, off, z) = t gave new_object_offset_2. Here, for the temps newvals.py picked (new_edits.json:
# its conditions on the temp are unchanged), the one store `PLACE = t;` (a cast allowed) is found again and named by PLACE:
#   KIND(address, name) / KIND_AS(..)      new_<name>            KIND_VIA(at, anchor, name)      new_<name>
#   KIND_IN(array, bytes, name)            new_<kind>_<name>     .._AT(.., index, name) constant index: .._at_<index>
#   GAME_VAR(name) / GAME_VAR_AS(..)       new_<name>            GAME_TABLE / GAME_ARRAY(name, ..)  new_<name>_entry
# A temp whose store is not found as a whole statement (newvals counted a lift_out mirror as the read) is left out.
# Duplicates _2, _3 in order of the code; clashes with any identifier of the function are skipped. Writes new2_edits.json.
from paths import D, L, B, T, BASE, CANDS, CH
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
lift.field_param_kinds(cands, lift.with_copy_names(merged, cands), kinds, sources)
picked = json.load(open(T + 'new_edits.json'))
# (names083, 2026-11-14) A variable with "count" above 1 in fields.json (camera_matrix: 16 floats) is an array: a read of it at
# its own address, GAME_VAR(name), is its element 0, named <name>_entry_0 -- <name> alone would say the whole array.
VAR_COUNT = {v['name']: int(v.get('count', 1)) for v in json.load(open(L + 'fields.json', encoding='utf8')).get('_variables', {}).values()}
MACROS = {}
for line in lift.fields_preamble(kinds, sources):
    m = re.match(r'#define (\w+)\(([^)]*)\) ', line)
    if m:
        MACROS[m.group(1)] = [p.strip() for p in m.group(2).split(',')]

def split_args(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '(<[': depth += 1
        elif c in ')>]':
            depth -= 1
            if depth < 0: return None
        if c == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += c
    if depth: return None
    return out + [cur.strip()]

def place_name(macro, args):
    ps = MACROS.get(macro)
    if not ps or args is None or len(ps) != len(args): return None
    a = dict(zip(ps, args))
    nm = a.get('name')
    if not nm or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm): return None
    if macro in ('GAME_VAR', 'GAME_VAR_AS'): return nm + '_entry_0' if VAR_COUNT.get(nm, 1) > 1 else nm   # (names083: element 0 of an array variable)
    if macro.startswith('GAME_'): return nm + '_entry'
    root = macro
    for p, suf in (('type', '_AS'), ('index', '_AT'), ('array', '_IN'), ('anchor', '_VIA')):
        if p in a:
            if not root.endswith(suf): return None
            root = root[:-len(suf)]
    tail = ''
    if 'index' in a:
        if not re.fullmatch(r'\d+|0x[0-9A-Fa-f]+', a['index']): return None
        tail = '_at_%d' % int(a['index'], 0)
    if 'array' in a:
        pre = root.lower()
        return (nm if nm.startswith(pre) else pre + '_' + nm) + tail
    return nm + tail

out = {}; stats = collections.Counter()
for f, e in sorted(picked.items()):
    info = merged[f]; cpp = cands[f]['cpp']
    fc = lift.fielded_cpp(f, cpp, info, kinds, sources)
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    used = set(vars_.values()) | {info['name']}
    wanted = []
    for t in e['vars+']:
        if t in vars_: continue
        ms = list(re.finditer(r'(?m)^\s+([A-Z][A-Z0-9_]*)\((.*)\) = (?:\([^()]*\) ?)?%s;$' % re.escape(t), fc))
        if len(ms) != 1:
            stats['no single store'] += 1; continue
        nm = place_name(ms[0].group(1), split_args(ms[0].group(2)))
        if not nm:
            stats['place not read'] += 1; continue
        wanted.append((t, 'new_' + nm, ms[0].start()))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: x[2]):
        seen[cand] += 1
        name_ = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if name_ in ids or name_ in used or name_ in lift.CPP_KEYWORDS or len(name_) > 40:
            stats['skipped'] += 1; continue
        used.add(name_); out.setdefault(f, {})[t] = name_; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + 'new2_edits.json', 'w'), indent=0)
print(stats, len(out))
