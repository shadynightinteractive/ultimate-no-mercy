#!/usr/bin/env python3
"""The Blue Meanie as WrestleMania 2000 shows him, from its RAM (gen_data/wm2k_meanie/): ram_sel.bin with him
under the cursor of its Superstar Select (his white T-shirt look), ram_pick.bin just after he's picked
with A (the look he's picked in). The cursor's Superstar is the select screen's fourth model (part
structs at 0x8009B118, 0x34 bytes each, from model entry 0x08: picture with its 8-byte header at +0xC,
256-colour palette at +0x10; gen/wmcapture.py). Every part's picture and palette are as WM2000 makes
them for him (it draws his face paint, hair and shirt on); the meshes are its files (his model's).
Costumes: 0 and 2 the select's look, 1 and 3 the picked one.
-> gen_data/wm2k_meanie/head.pkl: {costume: {model entry: (mesh file, picture, 512-byte palette)}}
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct, pickle
sys.path.insert(0, nmp("/home/devbox/work/arcade/gen"))
import wm2k
D = nmp("/home/devbox/work/gen_data/wm2k_meanie/")
BASE = 0x8009B118
def pal(t, pic):
    # a 256-colour picture's palette: its groups (16 colours each) the picture's pixels reach, the others
    # (left as they were in WM2000's memory) its first; a 16-colour picture's: its 16, in every group
    if not pic[2]: return t[:32] * 16
    top = max(pic[8:]) >> 4
    return b"".join(t[32 * g:32 * g + 32] if g <= top else t[:32] for g in range(16))
def grab(ram, desc):
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    out = {}
    for e in range(20):
        at = 8 + 8 * e
        mesh = struct.unpack_from(">H", desc, at)[0]
        if not mesh: continue
        s = BASE + e * 0x34
        p, t = r32(s + 0xC) & 0x7FFFFF, r32(s + 0x10) & 0x7FFFFF
        w, h = ram[p] + 1, ram[p + 1] + 1
        assert (1 << ram[p + 6], 1 << ram[p + 7]) == (w, h) and ram[p + 2:p + 4] in (b"\x00\x20", b"\x01\x00"), (hex(at), ram[p:p + 8].hex())
        n = w * h if ram[p + 2] else w * h // 2              # (0x100: 8 bits a pixel; 0x20: 4)
        pic = ram[p:p + 8 + n]
        out[at] = (mesh, pic, pal(ram[t:t + 512], pic), ram[t:t + 512])
    return out
if __name__ == "__main__":
    ro = wm2k.roster()
    desc = wm2k.model(wm2k.rbytes(ro[41]["cost"][0], 0x24))
    sel, pick = (grab(open(D + f, "rb").read(), desc) for f in ("ram_sel.bin", "ram_pick.bin"))
    out = {0: sel, 1: pick, 2: sel, 3: pick}
    for at, (m, p, t, raw) in sorted(sel.items()):
        print(hex(at), hex(m), p[:8].hex(), len(p), [g for g in range(16) if any(raw[32 * g:32 * g + 32])], pick[at][1] != p, max(p[8:]))
    for c in out: out[c] = {at: v[:3] for at, v in out[c].items()}
    pickle.dump(out, open(D + "head.pkl", "wb"))
