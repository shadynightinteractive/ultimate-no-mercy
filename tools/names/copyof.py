# (names5, 2026-11-13) Rule V: an m2c temp written once, as a plain copy of a named value -- `t = x;` or `t = (type) x;`, x a
# named local, parameter or stack slot -- holds x again (the compiler reloads or keeps a second copy): it takes x's name with the
# next free counter (x_2, x_3 ...), the convention the field-read names already follow (spirit, spirit_2 ...). lift_out_<reg>
# mirrors don't count as writes. Names that clash with any identifier of the function are skipped. Raw C++ with the entry's
# names. Repeat it to follow chains (t2 = t1 once t1 is named). Writes copyof_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
from rulelib import root_name   # (fablenames, 2026-11-14: x for x_2 only when x is a name of the function -- names can now end in an offset or a constant: record_half_4, x_minus_10)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    lines = [l.strip() for l in raw.split('\n')]
    new = {}
    for i, s in enumerate(lines):
        # (casts allowed, a narrowing one too: `t = (s32) (s16) x;` is x again for the game's values)
        m = re.match(r'(temp_\w+) = (?:\([\w<> ]+\) )*(\*?)(\w+);$', s)
        if not m: continue
        t, star, x = m.groups()
        if t in vars_ or t in new: continue
        nx = vars_.get(x)
        if nx is None:
            if M2C.match(x) or x in ('ctx', 'frame') or not re.fullmatch(r'[a-z]\w*', x) or x.startswith('lift_'): continue
            nx = x
        if M2C.match(nx) or not re.fullmatch(r'[a-z]\w*', nx): continue
        if star:
            # Rule D: `t = *p;` with p a walker named <y>_at (rule W, walkvars.py) is the y it points at: named y (y_2 ...)
            if not nx.endswith('_at') or len(nx) < 5: continue
            nx = nx[:-3]
            if nx not in used and nx not in ids:
                used.add(nx); new[t] = nx; stats['named_deref'] += 1; continue
        writes = [y for y in lines if re.match(r'%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), y)]
        if len(writes) != 1: stats['many_writes'] += 1; continue
        base = root_name(nx, set(vars_.values()))
        k = 2
        while '%s_%d' % (base, k) in used or '%s_%d' % (base, k) in ids: k += 1
        nm = '%s_%d' % (base, k)
        if len(nm) > 40: continue
        used.add(nm); new[t] = nm; stats['named'] += 1
    if new: out[f] = new
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/copyof_edits.json', 'w'), indent=0)
print(stats, len(out))
