"""No Mercy's own model descriptors: every costume of its appearance file (file 1, 0x26 bytes each) made
into looks (func_8000B60C, func_8001AC44) and a model (func_80018A3C), with the game's code on a RAM image
of its Superstar Select (harness/mipsemu.py)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from mipsemu import Emu
VAN = nmp("/home/devbox/work/gen_data/van_sel_ram.bin")
APP = nmp("/tmp/devbox-0/nmfiles/f/0001.bin")
UN, OUTB, PK, REC, SRC = 0x80781000, 0x80782000, 0x80783000, 0x80784000, 0x80785000
_E = None
def emu():
    global _E
    if _E is None: _E = Emu(open(VAN, "rb").read())
    return _E
def looks(app):
    E = emu()
    E.wbytes(REC, bytes(0x80)); E.wbytes(SRC, app)
    E.call(0x8000B60C, [REC, SRC]); packed = E.rbytes(REC + 0x31, 0x17)
    E.wbytes(PK, packed + b"\0"); E.wbytes(UN, bytes(0x40)); E.call(0x8001AC44, [PK, UN]); return E.rbytes(UN, 0x3F)
def model(un):
    E = emu()
    E.wbytes(UN, un + b"\0"); E.wbytes(OUTB, bytes(0x270))
    E.call(0x80018A3C, [UN, OUTB], max_steps=3_000_000)
    return E.rbytes(OUTB, 0x270)
def all_descriptors():
    app = open(APP, "rb").read()
    out = []
    for i in range(len(app) // 0x26):
        a = app[i * 0x26:i * 0x26 + 0x26]
        if not any(a): continue
        try: out.append(model(looks(a)))
        except Exception as e: print("costume", i, e)
    return out
if __name__ == "__main__":
    d = all_descriptors(); print(len(d), "descriptors")
