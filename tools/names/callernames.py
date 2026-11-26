# (fablenames, 2026-11-14) Abouts whose caller lists still give N64 names for functions that now have a confirmed name:
# "Callers func_80017444, func_80017568." / "Caller func_8012D408." / "Called by func_800DDB10." / "Only caller func_X."
# In such a list -- from the word Caller(s) / Called by up to the sentence's end -- each func_X that has an entry with guess
# false and is not the function itself is replaced by its name (the given abouts already write caller lists by name where the
# names were known when they were written). Nothing else in the about is touched: quoted source lines, addresses and notes
# elsewhere keep their func_X. Writes callernames_edits.json for put.py ({func: {"about": ...}}).
from paths import D, L, B, T, BASE, CANDS, CH
import json, re
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
LIST = re.compile(r"\b((?:[Oo]nly |[Ii]ts |[Tt]he )?(?:[Cc]allers?|[Cc]alled by|[Cc]alled from)(?: are| is|:)? )((?:(?:and |the |ovl_\w's |modrep_)?func_[0-9A-F]{8}(?:_ovl_\w)?(?: \([^()]*\))?(?:, | and |; )?)+)")
FN = re.compile(r'(?<!modrep_)\bfunc_[0-9A-F]{8}(?:_ovl_\w)?\b')
edits = {}; count = 0
for f, e in n.items():
    if f not in c or 'cpp' not in c[f]:
        continue
    def rep_list(m):
        def one(mm):
            global count
            g = mm.group(0)
            if g == f or g not in n or n[g].get('guess') or n[g]['name'].startswith('func_'):
                return g
            count += 1
            return n[g]['name']
        return m.group(1) + FN.sub(one, m.group(2))
    new = LIST.sub(rep_list, e['about'])
    if new != e['about']:
        edits[f] = {'about': new}
json.dump(edits, open(T + 'callernames_edits.json', 'w'), indent=0, ensure_ascii=False)
print(len(edits), 'abouts', count, 'mentions')
for f in list(edits)[:6]:
    print(f, '|', n[f]['about'][-160:], '\n   ->', edits[f]['about'][-200:])
