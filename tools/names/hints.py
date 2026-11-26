# Lines across the mod's sources and the exe's hand-written code that mention a function (its address), for naming.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import sys, os, re, glob
D = D
files = []
for pat in ['mod/src/*.c', 'mod/src/*.h', 'mod/recomp/*.c', 'mod/gen/*.py', 'mod/*.md',
            'exe/WWFNoMercyRecomp/src/main/*.cpp', 'exe/WWFNoMercyRecomp/src/main/*.h', 'exe/WWFNoMercyRecomp/src/game/raytracing.cpp']:
    files += glob.glob(D + pat)
texts = {}
for p in files:
    try: texts[p] = open(p, encoding='utf8', errors='replace').read().split('\n')
    except Exception: pass
lim = int(os.environ.get('HL', '10'))
for f in sys.argv[1:]:
    a = f.split('_')[-1]
    pat = re.compile(r'(?i)(func_%s|0x%s)\b' % (a, a))
    hits = []
    for p, ls in texts.items():
        for i, l in enumerate(ls):
            if pat.search(l):
                hits.append('%s:%d: %s' % (p.replace(D, ''), i + 1, l.strip()[:300]))
    print('== %s (%d hits)' % (f, len(hits)))
    print('\n'.join(hits[:lim]))
