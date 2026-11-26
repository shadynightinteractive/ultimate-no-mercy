from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json
names = json.load(open(BASE, encoding='utf8'))
g = json.load(open(T + 'graph.json'))
rows = []
for n, v in names.items():
    if v.get('guess') and n in g and not g[n]['new']:
        d = g[n]
        rows.append((len(d['callers']) * 3 + d['size'] / 40, n, len(d['callers']), d['size'], v['name']))
rows.sort(reverse=True)
open(T + 'rank.txt', 'w').write('\n'.join('%s %d %d %s' % (n, c, s, nm) for _, n, c, s, nm in rows))
import collections
print(collections.Counter(min(r[2], 10) for r in rows))
print(sum(1 for r in rows if r[3] > 300))
