import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Arrays indexed by a variable times a stride in the decompiled code (Ghidra out/): (&DAT_x)[v * k] and
# (v * k + -0x7f.......) forms. Output: base, element stride in bytes, functions.
import re, glob, collections, os, sys, pickle
OUT = nmp('/home/devbox/work/ghidra/out')
TYPES = {'': 1}
rx1 = re.compile(r'\(\s*(\w+\s*\*\s*)?\(?&DAT_([0-9a-f]{8})\)?\s*\)?\[\s*(\w+)\s*\*\s*(0x[0-9a-f]+|\d+)\s*\]')
rx2 = re.compile(r'(\w+)\s*\*\s*(0x[0-9a-f]+|\d+)\s*\+\s*-0x(7f[0-9a-f]{6})\b')
decl = re.compile(r'^\s*(?:undefined[1248]?|u?int|u?short|u?char|byte|float|bool|longlong|ulonglong|undefined)\s*\*?\s*(\w+)', re.M)
# element size of DAT_ by its use: a cast type before the index is rare; use declared type in globals is unknown, so
# take the stride as index * element; we record the raw multiplier and infer bytes from the access width guess.
ELEM = {}
for l in open(nmp('/home/devbox/work/ghidra/datatypes.txt')):
    a, n, t = l.split()
    ELEM[int(a, 16)] = int(n) if not t.startswith(('pointer', 'string')) else 4
res = collections.defaultdict(lambda: collections.defaultdict(set))
for p in glob.glob(OUT + '/*.c'):
    fn = os.path.basename(p)[:-2]
    s = open(p).read()
    for m in rx1.finditer(s):
        base = int(m.group(2), 16); k = int(m.group(4), 0)
        k *= ELEM.get(base, 1)
        res[(base, 'idx', k)][fn].add(m.group(0)[:60])
    for m in rx2.finditer(s):
        k = int(m.group(2), 0); base = (0x100000000 - int(m.group(3), 16)) & 0xFFFFFFFF
        res[(base, 'bytes', k)][fn].add(m.group(0)[:60])
pickle.dump({k: {f: sorted(v) for f, v in d.items()} for k, d in res.items()}, open(nmp('/home/devbox/work/six/kb/arrays.pkl'), 'wb'))
lo = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0; hi = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xFFFFFFFF
for (b, kind, k), d in sorted(res.items()):
    if lo <= b < hi:
        print('%08x %-5s x%-6x nf=%-3d %s' % (b, kind, k, len(d), ' '.join(sorted(d)[:4])))
