#!/usr/bin/env python3
"""Jeff Jarrett's head as WrestleMania 2000 shows it, captured from its RAM (its Superstar Select with him on
it: gen_data/wm2k_jarrett/ram_c0.bin).

WM2000 doesn't show its picture files as they are: it makes each head piece's picture at run time, a
256-colour (CI8) picture and palette -- his face drawn from its features over a base face, his hair drawn
over his face and the back of his head in his hair colour (palette group 2), his moustache (groups 4-5) --
and the palette's group 0 his skin tone's colours. The select screen keeps several models (the
Superstars around the cursor); his is the one whose face piece has his hair drawn on. Its part structs
are 0x34 bytes, one per 8-byte model entry from 0x08, the picture (with its 8-byte header) at +0xC and
the 256-colour palette at +0x10.
-> gen_data/wm2k_jarrett/head.pkl: {model entry: (picture with header, 512-byte palette)}
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import struct, pickle
D = nmp("/home/devbox/work/gen_data/wm2k_jarrett/")
SLOTS = (0x20, 0x28, 0xA0)
COPIES = (0x8009C054, 0x8009BB40, 0x8009B62C, 0x8009B118)   # the select screen's models' part structs
if __name__ == "__main__":
    ram = open(D + "ram_c0.bin", "rb").read()
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    def piece(base, slot):
        s = base + (slot - 8) // 8 * 0x34
        pic, pal = r32(s + 0xC), r32(s + 0x10)
        w, h = ram[pic & 0x7FFFFF] + 1, ram[(pic & 0x7FFFFF) + 1] + 1
        return ram[pic & 0x7FFFFF:(pic & 0x7FFFFF) + 8 + w * h], ram[pal & 0x7FFFFF:(pal & 0x7FFFFF) + 512]
    his = [b for b in COPIES if any(piece(b, 0xA0)[1][64:96])]   # hair colours drawn on his face
    assert len(his) == 1, [hex(b) for b in his]
    out = {slot: piece(his[0], slot) for slot in SLOTS}
    for slot, (p, t) in out.items():
        print(hex(slot), p[:8].hex(), len(p), [g for g in range(16) if any(t[32 * g:32 * g + 32])])
    pickle.dump(out, open(D + "head.pkl", "wb"))
