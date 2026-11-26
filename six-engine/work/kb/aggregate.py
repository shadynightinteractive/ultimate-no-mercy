import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Aggregates the review batches: arrays by (base, stride) with their classes, loops by class, masks, tables.
import glob, collections, sys
A = collections.defaultdict(lambda: {'cls': collections.Counter(), 'fn': set(), 'ev': [], 'cnt': set()})
L = collections.defaultdict(list); M = []; T = []; C = []; N = []; F = {}
def h(x):
    x = x.strip().split()[0] if x.strip() else ''
    try: return int(x, 16)
    except ValueError: return None
for p in sorted(glob.glob(nmp('/home/devbox/work/six/kb/review/*.out'))):
    for line in open(p):
        f = [x.strip() for x in line.rstrip('\n').split(' | ')]
        k = f[0]
        try:
            if k == 'FUNC': F[f[1]] = f[2] if len(f) > 2 else ''
            elif k == 'LOOP': L[f[3].split()[0] if len(f) > 3 else '?'].append(f)
            elif k == 'ARRAY':
                b, s = h(f[1]), h(f[2])
                if b is None: continue
                a = A[(b, s)]; a['cls'][f[4].split('/')[0] if len(f) > 4 else '?'] += 1
                a['fn'].add(f[5] if len(f) > 5 else '?'); a['ev'].append(f[6] if len(f) > 6 else '')
                if len(f) > 3 and f[3]: a['cnt'].add(f[3])
            elif k == 'MASK': M.append(f)
            elif k == 'TABLE': T.append(f)
            elif k == 'CONST': C.append(f)
            elif k == 'NOTE': N.append(f)
        except IndexError: pass
if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'summary'
    if what == 'summary':
        print('functions', len(F), 'arrays', len(A), 'loops', {k: len(v) for k, v in L.items()}, 'masks', len(M), 'tables', len(T), 'consts', len(C), 'notes', len(N))
    elif what == 'arrays':
        for (b, s), a in sorted(A.items(), key=lambda x: (x[0][0], x[0][1] or 0)):
            print('%08x x%-6s %-24s cnt=%-6s nf=%-2d %s | %s' % (b, ('%x' % s) if s is not None else '?', dict(a['cls']), ','.join(sorted(a['cnt'])) or '-', len(a['fn']), ' '.join(sorted(a['fn'])[:3]), a['ev'][0][:110]))
    elif what in ('loops',):
        for cls, ls in sorted(L.items()):
            for f in ls: print(cls, '|', ' | '.join(f[1:]))
    elif what == 'masks':
        for f in M: print(' | '.join(f[1:]))
    elif what == 'tables':
        for f in T: print(' | '.join(f[1:]))
    elif what == 'consts':
        for f in C: print(' | '.join(f[1:]))
    elif what == 'notes':
        for f in N: print(' | '.join(f[1:]))
