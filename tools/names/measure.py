from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys
names = json.load(open(BASE, encoding='utf8'))
if len(sys.argv) > 1:   # apply a changes file on top
    ch = json.load(open(sys.argv[1], encoding='utf8'))
    names = dict(names); names.update(ch)
cands = json.load(open(CANDS))
new = set()
conv = {n for n, v in cands.items() if 'cpp' in v and not n.startswith('modrep_')}
M2C = re.compile(r'\b(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)\b')
def m2c_locals(n):
    cpp = cands[n]['cpp']
    body = cpp.split('{', 1)[1]
    head = cpp.split('{', 1)[0]
    ids = set(M2C.findall(cpp))
    return ids
print('names.json entries', len(names))
print('guess true', sum(1 for v in names.values() if v.get('guess')))
print('guess true & converted', sum(1 for n, v in names.items() if v.get('guess') and n in conv))
print('guess true & converted & not new', sum(1 for n, v in names.items() if v.get('guess') and n in conv and n not in new))
print('converted (non-modrep)', len(conv), ' of which new in 079', len(conv & new))
print('converted without entry', len(conv - set(names)), ' excluding new', len(conv - set(names) - new))
tot_unnamed = tot = 0; novars = 0; partial = 0; fully = 0
for n in conv & set(names):
    ids = m2c_locals(n)
    if not ids: continue
    named = set(names[n].get('vars', {}))
    un = ids - named
    tot += len(ids); tot_unnamed += len(un)
    if not (ids & named): novars += 1
    elif un: partial += 1
    else: fully += 1
print('entries (converted) with m2c locals: none named', novars, ' some named', partial, ' all named', fully)
print('m2c locals in named converted functions', tot, ' unnamed', tot_unnamed)
