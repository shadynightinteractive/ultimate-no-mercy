# (names5, 2026-11-13) Rule G: a stack slot loaded from the display-list parameter (`spNN = *p;`, p the parameter named
# display_list, list, dl or gfx) is the drawer's cursor into the display list -- the N64 idiom `Gfx *gfx = *gfxp;` -- and is
# named gfx (gfx_cursor when gfx is taken). Raw C++ with the entry's names. Writes gfxslot_edits.json for put.py.
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
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    for m in re.finditer(r'(?m)^\s*(sp[0-9A-F]+) = \*(arg\d+);$', raw):
        s, a = m.groups()
        if s in vars_ or vars_.get(a) not in ('display_list', 'list', 'dl', 'gfx'): continue
        for nm in ('gfx', 'gfx_cursor'):
            if nm not in used and nm not in ids:
                used.add(nm); out.setdefault(f, {})[s] = nm; stats['named'] += 1; break
        else: stats['skipped'] += 1
        break
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/gfxslot_edits.json', 'w'), indent=0)
print(stats, len(out))
