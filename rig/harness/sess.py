import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, os, pickle
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from n64 import N64
ST = nmp("/home/devbox/work/states")
os.makedirs(ST, exist_ok=True)
_m = None
def m():
    global _m
    if _m is None:
        _m = N64(nmp("/home/devbox/work/nomercy.z64"), nmp("/home/devbox/work/sys"))
        _m.run(1)
    return _m
def save(name):
    pickle.dump(m().save(), open(f"{ST}/{name}.st", "wb"))
def load(name):
    m().load(pickle.load(open(f"{ST}/{name}.st", "rb")))
def shot(name):
    return m().shot(nmp(f"/home/devbox/work/shots/{name}.png"))
