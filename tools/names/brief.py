# Compact view for confirming a guess: about, signature, calls (named), constants, returns.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS)); g = json.load(open(T + 'graph.json'))
for f in sys.argv[1:]:
    e = n[f]; s = c[f]['cpp']
    print('=' * 80); print(f, e['name'], 'size', c[f]['size'], 'callers', len(g[f]['callers']), 'guess', e.get('guess'))
    print('ABOUT:', e['about']); print('VARS:', e.get('vars'))
    print('SIG:', s.split('{')[0].strip())
    calls = collections.Counter(re.findall(r'::(\w+)\)\(', s))
    print('CALLS:', ', '.join('%s=%s x%d' % (k, n.get(k, {}).get('name', '-'), v) for k, v in calls.most_common()))
    print('ADDR:', ' '.join(sorted(set(re.findall(r'0x80[0-9A-F]{6}', s)))[:40]))
    print('RET:', ' | '.join(sorted(set(l.strip() for l in s.split('\n') if l.strip().startswith('return')))[:12]))
    print('LINES:', s.count('\n'))
