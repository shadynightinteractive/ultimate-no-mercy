import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Finds the moved arrays whose move changes the game (oracle2: same menu state, both games load the match).
import subprocess, sys, os
F = sys.argv[1] if len(sys.argv) > 1 else '260'
sys.path.insert(0, nmp('/home/devbox/work/six'))
import spec
N = len(spec.OVLE)
def ok(sub):
    env = dict(os.environ, SIX_ONLY=','.join(map(str, sub)) if sub else '999', SIX_OUT=nmp('/tmp/devbox-0/six_bis.z64'), TP_TAG='bis')
    subprocess.run([sys.executable, nmp('/home/devbox/work/six/build_rom.py'), '--oracle'], env=env, capture_output=True, check=True)
    out = subprocess.run([sys.executable, nmp('/home/devbox/work/six/oracle2.py'), nmp('/tmp/devbox-0/six_bis.z64'), F], env=env, capture_output=True, text=True).stdout
    return 'IDENTICAL' in out
subprocess.run([sys.executable, nmp('/home/devbox/work/six/oracle2.py'), 'stock', F], env=dict(os.environ, TP_TAG='bis'), capture_output=True)
bad = []
def search(cands):
    if not cands or ok(cands): return
    if len(cands) == 1:
        bad.append(cands[0]); print('BAD', cands[0], spec.OVLE[cands[0]], flush=True); return
    h = len(cands) // 2
    search(cands[:h]); search(cands[h:])
search(list(range(N)))
print('bad:', bad)
