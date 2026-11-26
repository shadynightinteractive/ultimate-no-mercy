#!/usr/bin/env python3
"""Checks the new body parts (gen/mkparts.py, src/parts.c, gen/patches.py) on the test blob's own code,
run on a RAM image of the game (harness/mipsemu.py):

  1. parts.c's unpacking (func_8001AC44's) gives what the game's does for every costume of the game,
     and a new Superstar's whole struct for its key;
  2. the game's func_80018A3C (a costume's model) changed to read the bigger tables makes the same
     model as the game's own for every costume of the game, in each of the states it reads;
  3. ... and for each new Superstar's costume the model No Mercy Plus's func_80018A3C makes of it with
     its own tables (No Mercy Plus's RAM image), its files as the arcade's IDs.

Run after build_test.sh.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct, pickle, subprocess, itertools
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from mipsemu import Emu
W = nmp("/home/devbox/work/")
A = W + "arcade/"
van = bytearray(open(W + "gen_data/van_sel_ram.bin", "rb").read())
nm = bytearray(open(W + "gen_data/nm_pick_ram.bin", "rb").read())
blob = open(A + "build/test/arcade_test.bin", "rb").read()
syms = {}
for l in subprocess.run(["llvm-nm", A + "build/test/arcade_test.elf"], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
P = pickle.load(open(A + "build/parts.pkl", "rb"))
FMAP = P["files"]                                  # No Mercy Plus's file -> ours

def j(a, t): return struct.pack(">II", 0x08000000 | ((t & 0x0FFFFFFF) >> 2), 0)
ours = bytearray(van)
ours[0x700000:0x700000 + len(blob)] = blob
bss_end = max(v for v in syms.values() if v < 0x80800000)
ours[0x700000 + len(blob):(bss_end & 0x7FFFFF) + 0x1000] = bytes((bss_end & 0x7FFFFF) + 0x1000 - 0x700000 - len(blob))
for fn in ("func_80018A3C", "func_8001858C"):
    a = int(fn[5:], 16); ours[a & 0x7FFFFF:(a & 0x7FFFFF) + 8] = j(a, syms["patch_" + fn])
game = bytearray(ours)        # (the game's own: the same RAM, its functions as they are)
for fn in ("func_80018A3C", "func_8001858C"):
    a = int(fn[5:], 16) & 0x7FFFFF; game[a:a + 8] = van[a:a + 8]
EO, EV, EN = Emu(ours), Emu(game), Emu(nm)
UN, OUTB, PK = 0x80781000, 0x80782000, 0x80783000
GLOB_V = (0x800A8970, 0x80091CB4, 0x80097FD8)
GLOB_N = (0x800A9EA0, 0x800931E4, 0x80099508)
STATES = [(0, 0, 0, 0), (1, 1, 3, 0x20), (1, 0, 3, 0), (0, 1, 2, 0x20)]

def model(E, fn, un, glob, st):
    for a, v in zip(glob, st[:3]): E.w8(a, v)
    E.wbytes(UN, un + b"\0"); E.wbytes(OUTB, bytes(0x270)); E.w8(OUTB + 0x254, st[3])
    E.call(fn, [UN, OUTB], max_steps=3_000_000)
    return E.rbytes(OUTB, 0x270)
def unpack(E, fn, packed):
    E.wbytes(PK, packed + b"\0"); E.wbytes(UN, bytes(0x40)); E.call(fn, [PK, UN]); return E.rbytes(UN, 0x3F)

bad = 0
# 1 + 2: the game's costumes
APP = open(nmp("/tmp/devbox-0/nmfiles/f/0001.bin"), "rb").read()
REC = 0x80784000
n_app = len(APP) // 0x26
for i in range(n_app):
    EV.wbytes(REC, bytes(0x80)); EV.wbytes(0x80785000, APP[i * 0x26:i * 0x26 + 0x26])
    EV.call(0x8000B60C, [REC, 0x80785000]); packed = EV.rbytes(REC + 0x31, 0x17)
    a, b = unpack(EV, 0x8001AC44, packed), unpack(EO, syms["parts_unpack"], packed)
    if a != b: bad += 1; print("unpack differs: appearance %d" % i)
    for st in STATES:
        x, y = model(EV, 0x80018A3C, a, GLOB_V, st), model(EO, 0x80018A3C, a, GLOB_V, st)
        if x != y: bad += 1; print("model differs: appearance %d state %s at %s" % (i, st, [hex(k) for k in range(0x270) if x[k] != y[k]][:8]))
import random
random.seed(1)
for t in range(3000):     # packed looks of any bits (a Create-a-Wrestler's are any), but bit 183
    packed = bytearray(random.getrandbits(8) for _ in range(0x17)); packed[0x16] &= 0x7F
    a, b = unpack(EV, 0x8001AC44, bytes(packed)), unpack(EO, syms["parts_unpack"], bytes(packed))
    if a != b: bad += 1; print("unpack differs:", packed.hex())
    if t < 300:
        st = STATES[t % len(STATES)]
        x, y = model(EV, 0x80018A3C, a, GLOB_V, st), model(EO, 0x80018A3C, a, GLOB_V, st)
        if x != y: bad += 1; print("model differs: random %d at %s" % (t, [hex(k) for k in range(0x270) if x[k] != y[k]][:8]))
print("the game's costumes: %d appearances, 3000 random looks checked" % n_app)

# 3: the new Superstars' costumes
def mapped(b):
    b = bytearray(b)
    for k in range(0, 0x270, 2):
        v = struct.unpack_from(">H", b, k)[0]
        if v in FMAP: struct.pack_into(">H", b, k, FMAP[v])
    return bytes(b)
for n, (key, un, nm_un) in enumerate(zip(P["keys"], P["uns"], P["nm_uns"])):
    got = unpack(EO, syms["parts_unpack"], key)
    full = bytearray(0x3F)
    for o in set(range(0x3F)) - {2, 0xD, 0xE, 0x10, 0x11, 0x14, 0x16, 0x17, 0x19, 0x1A, 0x1C, 0x1D, 0x20, 0x23, 0x26, 0x29, 0x2C, 0x2F, 0x34, 0x35, 0x38, 0x3A, 0x3B, 0x3D, 0x3E}:
        full[o] = un[o]
    if got != bytes(full): bad += 1; print("costume %d: key unpacks wrong" % n)
    for st in STATES:
        x = model(EO, 0x80018A3C, got, GLOB_V, st)
        y = mapped(model(EN, 0x80018ACC, nm_un, GLOB_N, st))
        d = [k for k in range(0, 0x270, 2) if x[k:k + 2] != y[k:k + 2]]
        if d:
            bad += 1
            print("costume %d (%d/%d) state %s: %s" % (n, n // 4, n % 4, st, [(hex(k), x[k:k + 2].hex(), y[k:k + 2].hex()) for k in d]))
print("new costumes: %d checked" % len(P["keys"]))
print("FAILED: %d" % bad if bad else "all the same")
sys.exit(1 if bad else 0)
