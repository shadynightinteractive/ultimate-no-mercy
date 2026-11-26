from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, subprocess, sys
a = json.load(open(T + 'autoloop.json'))
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
edits = {}
for f, vs in a.items():
    cur = (ch.get(f) or names[f]).get('vars', {})
    add = {k: v for k, v in vs.items() if k not in cur and v not in cur.values()}
    if add:
        edits[f] = {'vars+': add}
json.dump(edits, open(T + 'loop_edits.json', 'w'), indent=0)
print(len(edits), sum(len(e['vars+']) for e in edits.values()))
