import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# The linker symbols for a layout: A_<old base> = the array's place in that build (build_rom's layout), and every
# game function / data address the rewrites name (func_XXXXXXXX = 0xXXXXXXXX).
#   gen_syms.py oracle|full OUT.ld SRC...
import sys, os, re
sys.path.insert(0, nmp('/home/devbox/work/six'))
layout, out, srcs = sys.argv[1], sys.argv[2], sys.argv[3:]
if layout == 'oracle': sys.argv.append('--oracle')
os.environ.pop('SIX_ONLY', None); os.environ.pop('SIX_MAIN', None)
import build_rom
lines = []
moved = {b: n for b, s, n, w in build_rom.OVLE_NEW + build_rom.MAIN_NEW}
used = set()
for f in srcs: used |= set(re.findall(r'\b(A|func|D)_([0-9A-Fa-f]{8})\b', open(f).read()))
for f in srcs:              # the engine's own blocks (build_rom.EXTRA): X_<name>
    for nm in sorted(set(re.findall(r'\bX_([a-z][a-z0-9_]*)\b', open(f).read()))):
        lines.append('X_%s = 0x%08X;' % (nm, build_rom.EXTRA[nm]))
for kind, a in sorted(used):
    v = int(a, 16)
    if kind == 'A':
        if v not in moved: raise SystemExit('A_%s is not a moved array' % a)
        v = moved[v]
    lines.append('%s_%s = 0x%08X;' % (kind, a, v))
open(out, 'w').write('\n'.join(lines) + '\n')
