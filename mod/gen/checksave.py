#!/usr/bin/env python3
"""Checks that the save goes out without the new Superstars (src/save.c), on the test blob's code run
on a RAM image of the game (harness/mipsemu.py): the game's own functions that put the titles, the
presets and the story in the save image and write it (func_800E37D4, func_800E4218, func_800E3E64)
are run with new Superstars in them; what func_80004EE8 hands the writing (func_80004714) is looked
at, and the image after. Run after build_test.sh.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct, subprocess
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from mipsemu import Emu
W = nmp("/home/devbox/work/")
A = W + "arcade/"
van = bytearray(open(W + "gen_data/van_sel_ram.bin", "rb").read())
blob = open(A + "build/test/arcade_test.bin", "rb").read()
syms = {}
for l in subprocess.run(["llvm-nm", A + "build/test/arcade_test.elf"], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
ram = bytearray(van)
ram[0x700000:0x700000 + len(blob)] = blob
bss_end = max(v for v in syms.values() if v < 0x80800000)
ram[0x700000 + len(blob):(bss_end & 0x7FFFFF) + 0x1000] = bytes((bss_end & 0x7FFFFF) + 0x1000 - 0x700000 - len(blob))
def redirect(a, sym): ram[a & 0x7FFFFF:(a & 0x7FFFFF) + 8] = struct.pack(">II", 0x08000000 | ((syms[sym] & 0x0FFFFFFF) >> 2), 0)
redirect(0x80004EE8, "patch_func_80004EE8")
redirect(0x8000AE78, "roster_id_to_ord")
redirect(0x8000AE38, "roster_ord_to_id")
E = Emu(ram)
IMG = E.r32(0x80062C20)
written = []
def wr(e):          # func_80004714: what would be written
    written.append(e.rbytes(IMG, 0x7170)); e.r[2] = 0; return True
def ok(e): e.r[2] = 0; return True
E.hooks[0x80004714] = wr; E.hooks[0x800049E8] = wr; E.hooks[0x8000423C] = ok; E.hooks[0x800043D0] = ok
E.hooks[0x80003F3C] = ok
bad = 0
def check(c, what):
    global bad
    print(("ok   " if c else "FAIL ") + what)
    if not c: bad += 1
def bits(b, pos, n):
    v = 0
    for i in range(n): v |= ((b[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) << i
    return v
def title_ords(img, t):
    out = []
    for i in range(6):
        e = img[0x7018 + 0x26 * t + 6 * i:][:6]
        out.append((bits(e, 0, 7), bits(e, 9, 7)))
    return out

# titles: a new Superstar holds title 0, another was a champion before; title 1 a tag team with one
T = 0x800A3EB0
E.wbytes(T, bytes(7 * 0x34))
def tentry(t, i, a, b, n):
    e = T + 0x34 * t + 8 * i
    E.w16(e, n); E.w8(e + 2, a); E.w8(e + 3, b); E.w16(e + 6, 3)
tentry(0, 0, 102, 0, 7); tentry(0, 1, 10, 0, 6); tentry(0, 2, 104, 0, 5); tentry(0, 3, 20, 0, 4)
tentry(1, 0, 30, 105, 9); tentry(1, 1, 31, 32, 8)
tentry(2, 0, 40, 0, 2)
E.call(0x800E37D4, [])
w = written[-1]
check(title_ords(w, 0)[:3] == [(10, 0), (20, 0), (0, 0)], "title 0 written: %s" % title_ords(w, 0))
check(title_ords(w, 1)[:2] == [(31, 32), (0, 0)], "title 1 written: %s" % title_ords(w, 1))
check(title_ords(w, 2)[:1] == [(40, 0)], "title 2 written as it is")
img = E.rbytes(IMG, 0x7170)
check(title_ords(img, 0)[:4] == [(102, 0), (10, 0), (104, 0), (20, 0)], "title 0 in the image after: %s" % title_ords(img, 0)[:4])
check(max(b for t in range(7) for e in title_ords(w, t) for b in e) < 102, "no new Superstar in the titles written")

# presets: preset 0 has Funaki in it, preset 1 the game's own
P = 0x800BD5A8
E.wbytes(P, bytes(15 * 0x22))
for i in range(15): E.w8(P + 0x22 * i, 0xFF)
def preset(i, ids):
    p = P + 0x22 * i
    E.w8(p, 1); E.w8(p + 0xE, 0)
    for k, d in enumerate(ids): E.w16(p + 2 + 2 * k, d)
preset(0, [0x0001, 0x060A, 0, 0]); preset(1, [0x0001, 0x0101, 0, 0])
NAMES = 0x80786000
E.wbytes(NAMES, bytes(0x40))
E.call(0x800E4218, [NAMES])
w = written[-1]
check(w[0x6E84] == 0xFF and not any(w[0x6E85:0x6E92]), "preset 0 (Funaki) written as unused")
check(w[0x6E92] == 1 and bits(w[0x6E93:], 5, 7) == 1, "preset 1 written as it is")
img = E.rbytes(IMG, 0x7170)
check(img[0x6E84] == 1 and bits(img[0x6E85:], 5 + 9, 7) == 102, "preset 0 in the image after (Funaki, ordinal %d)" % bits(img[0x6E85:], 14, 7))

# a story with Gangrel in it
E.w32(0x80097EC8, 1)
S = 0x800A95A0
E.wbytes(S, bytes(16 * 24))
E.w16(S, 0x0408)
E.call(0x800E3E64, [])
w = written[-1]
check(w[0x6F91] == 0xFF and not any(w[0x6F98:0x7018]), "the story (Gangrel's) written as none")
img = E.rbytes(IMG, 0x7170)
check(img[0x6F91] == 1 and img[0x6F98] == 103, "the story in the image after")
E.w16(S, 0x0001)
E.call(0x800E3E64, [])
w = written[-1]
check(w[0x6F91] == 1 and w[0x6F98] == 1, "a story without a new Superstar written as it is")
print("FAILED: %d" % bad if bad else "all right")
sys.exit(1 if bad else 0)
