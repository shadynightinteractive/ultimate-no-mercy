from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
pass
import to_asm
names = json.load(open(BASE, encoding='utf8'))
cands = json.load(open(CANDS))
fns = to_asm.read_functions()
callers = collections.defaultdict(set)
for n, f in fns.items():
    for c in set(f['calls'].values()):
        callers[c].add(n)
new = set()
conv = {n for n, v in cands.items() if 'cpp' in v}
data = {}
for n in sorted(conv):
    v = cands[n]
    data[n] = {'size': int(v['size']), 'callers': sorted(callers.get(n, ())), 'callees': sorted(set(fns[n]['calls'].values())) if n in fns else [],
               'new': n in new}
json.dump(data, open(T + 'graph.json', 'w'), indent=0)
print(len(data))
