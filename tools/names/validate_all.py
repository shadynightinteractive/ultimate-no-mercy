from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections, sys
sys.path.insert(0, L)
n = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
cands = json.load(open(CANDS))
new = set()
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor'.split())
RES = {'field', 'P', 'Ref', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'lift_rdram', 's8', 's16', 's32', 's64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64', 'std', 'main'}
merged = dict(n); merged.update(ch)
problems = []
cnt = collections.Counter(v['name'] for v in merged.values())
for f, e in ch.items():
    if f in new: problems.append((f, 'touches a new-in-0.79 function'))
    if f not in cands or 'cpp' not in cands[f]: problems.append((f, 'not converted')); continue
    if set(e) != {'about', 'guess', 'name', 'vars'}: problems.append((f, 'keys', set(e)))
    if cnt[e['name']] > 1: problems.append((f, 'duplicate function name', e['name']))
    if chr(13) in e['about'] or any(l.rstrip().endswith(chr(92)) for l in e['about'].split(chr(10))): problems.append((f, 'about has CR or a line ending in a backslash'))
    code = re.sub(r'//.*', '', cands[f]['cpp'])
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', code))
    orig = (n.get(f) or {}).get('vars', {})
    vals = list(e['vars'].values())
    for k, v in e['vars'].items():
        if k not in ids: problems.append((f, 'key missing', k))
        if orig.get(k) == v: continue
        if not re.fullmatch(r'[a-z_][a-z0-9_]*', v) or v in KW or v in RES: problems.append((f, 'bad name', k, v))
        if v in ids and v != k: problems.append((f, 'collides', k, v))
        if vals.count(v) > 1: problems.append((f, 'dup var', v))
        if v == e['name']: problems.append((f, 'var equals function name', v))
        if re.fullmatch(r'(sp[0-9A-F]+|var_\w+|temp_\w+|phi_\w+|arg\d+)', v): problems.append((f, 'm2c-looking', v))
    if e['name'] != (n.get(f) or {}).get('name') and e['name'] in ids and e['name'] != f: problems.append((f, 'name collides with id'))
print(len(ch), 'entries;', len(problems), 'problems'); print(problems[:30])
