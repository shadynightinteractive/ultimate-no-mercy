#!/usr/bin/env python3
"""The test rig's ROM (harness/rig.py: NM_ROM): the game's, with the mod's packs where the test build
reads them (test/hooks_test.c): the content pack at 0x02000000 (past the cartridge's end), the ported
Superstars' files (build/wmport.bin) at 0x03E00000 (past it)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys
A = nmp("/home/devbox/work/arcade/")
out = sys.argv[1] if len(sys.argv) > 1 else nmp("/home/devbox/work/nomercy_full.z64")
rom = bytearray(open(nmp("/home/devbox/work/nomercy.z64"), "rb").read())
wm = open(A + "build/wmport.bin", "rb").read()
rom += open(A + "build/content.bin", "rb").read()
assert len(rom) <= 0x3E00000
rom += bytes(0x3E00000 - len(rom)) + wm
rom += bytes(-len(rom) % 0x100000)
open(out, "wb").write(rom)
print(out, hex(len(rom)))
