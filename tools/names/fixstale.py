# (names3, 2026-11-12) Abouts that call a function "not converted" when it now is (and is named): the mention becomes
# "name (func_X; the rest of the note)" or "name (func_X)". Writes stale_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
pat = re.compile(r'\b(func_[0-9A-F]{8}(?:_ovl_\w)?)(?: \((?:not converted|unconverted)([;,][^)]*)?\)|, not converted)')
edits = {}
for f, e in n.items():
    def rep(m):
        g = m.group(1)
        if g not in c or 'cpp' not in c[g] or g not in n or n[g]['name'].startswith('func_'):
            return m.group(0)
        rest = (m.group(2) or '').lstrip(';, ').strip()
        return '%s (%s%s)' % (n[g]['name'], g, '; ' + rest if rest else '')
    new = pat.sub(rep, e['about'])
    if new != e['about']:
        edits[f] = {'about': new}
json.dump(edits, open(T + 'stale_edits.json', 'w'), indent=0, ensure_ascii=False)
print(len(edits))
for f in list(edits)[:4]: print(f, edits[f]['about'][:300])
