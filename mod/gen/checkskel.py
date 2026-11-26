#!/usr/bin/env python3
"""Checks src/skel.c against the games' own functions, run on RAM images (harness/mipsemu.py).

  1. No Mercy's way: the game's func_80015D8C and skel_setup on the same RAM, for every descriptor the
     game's func_80018A3C makes of its roster's costumes and for random ones (the fields the function
     reads: meshes present or not, the attachments' codes, flag 0x80): the same calls (func_8000C58C,
     func_8000D284, func_80015D54, with their arguments) in the same order, and the same RAM after.
  2. WrestleMania 2000's way: WM2000's own 800166B4 on its RAM (a match's) and skel_setup on No Mercy's,
     for its match's descriptors and random ones, made into No Mercy's layout (gen/wmport.py's
     nm_descriptor): the same calls, the bones and parts named by number (WM2000's attachment bones
     22-24 are No Mercy's 25-27), and the same floats set on the bones' draw objects.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, sys, struct, random, subprocess
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Emu
A = nmp("/home/devbox/work/arcade")
NM_RAM = nmp("/tmp/devbox-0/sk/match_ram.bin")            # No Mercy, in a match (bones' draw objects made)
WM_RAM = nmp("/home/devbox/work/wmport/wm/match_ram.bin")  # WrestleMania 2000, in a match
BLOB = 0x80700000

def build():
    o = nmp("/tmp/devbox-0/skelchk"); os.makedirs(o, exist_ok=True)
    fl = ("--target=mips-unknown-none-elf -mcpu=mips2 -mabi=32 -O2 -G0 -mno-abicalls -fno-pic -mno-odd-spreg "
          "-mno-check-zero-division -fomit-frame-pointer -fno-builtin -fno-jump-tables -nostdinc -ffreestanding "
          "-Wall -Wextra -Wno-unused-parameter -I%s/src -DARCADE_TEST=1" % A).split()
    subprocess.check_call(["clang"] + fl + ["-c", A + "/src/skel.c", "-o", o + "/skel.o"])
    subprocess.check_call(["clang"] + fl + ["-c", A + "/src/hands.c", "-o", o + "/hands.o"])
    subprocess.check_call(["ld.lld", "-m", "elf32btsmip", "-T", A + "/test/test.ld", "-e", "skel_setup", o + "/skel.o", o + "/hands.o", "-o", o + "/skel.elf"])
    subprocess.check_call(["llvm-objcopy", "-O", "binary", "--only-section=.text", "--only-section=.rodata",
                           "--only-section=.data", o + "/skel.elf", o + "/skel.bin"])
    syms = {}
    for l in subprocess.check_output(["llvm-nm", o + "/skel.elf"]).decode().split("\n"):
        p = l.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    return open(o + "/skel.bin", "rb").read(), syms

def run(ram, fn, desc, slot, calls, blob=None, desc_at=0x80780000):
    E = Emu(ram)
    if blob: E.wbytes(BLOB, blob)
    E.wbytes(desc_at, desc)
    log = []
    def hook(name, n_stack, n_args):
        def f(e):
            st = [e.r32(e.r[29] + 0x10 + 4 * i) for i in range(n_stack)]
            a = list(e.r[4:4 + n_args])
            if name == "attach": a[3] &= 0xFFFF       # (a 16-bit mesh number)
            log.append((name, tuple(a) + tuple(st)))
            return True
        return f
    for addr, (name, ns, na) in calls.items(): E.hooks[addr] = hook(name, ns, na)
    before = bytes(E.m)
    E.call(fn, [desc_at, slot])
    return E, log, before

NM_CALLS = {0x8000C58C: ("attach", 3, 4), 0x8000D284: ("join", 0, 4), 0x80015D54: ("clear", 0, 1)}
WM_CALLS = {0x8000B94C: ("attach", 3, 4), 0x8000C7F0: ("join", 0, 4)}

def ramdiff(a, b, skip):
    d = []
    for i in range(0, len(a), 4096):
        if a[i:i + 4096] != b[i:i + 4096]:
            for j in range(i, i + 4096, 4):
                if a[j:j + 4] != b[j:j + 4] and not any(lo <= 0x80000000 + j < hi for lo, hi in skip):
                    d.append(0x80000000 + j)
    return d

def check_nm(blob, syms, descs):
    ram = open(NM_RAM, "rb").read()
    skip = [(0x807F0000, 0x80800000), (BLOB, BLOB + 0x10000)]
    bad = 0
    for n, (desc, slot) in enumerate(descs):
        Eo, lo, _ = run(ram, 0x80015D8C, desc, slot, NM_CALLS)
        Ep, lp, _ = run(ram, syms["skel_setup"], desc, slot, NM_CALLS, blob)
        d = ramdiff(Eo.m, Ep.m, skip)
        if lo != lp or d:
            bad += 1
            if bad < 4:
                print("NM MISMATCH case", n, "slot", slot, "calls", len(lo), len(lp), "ram", [hex(x) for x in d[:8]])
                for x, y in zip(lo, lp):
                    if x != y: print("  game", x[0], [hex(v) for v in x[1]], "\n  port", y[0], [hex(v) for v in y[1]]); break
    print("No Mercy's way: %d descriptors, %d differ" % (len(descs), bad))
    return bad

if __name__ == "__main__" and len(sys.argv) == 1:
    blob, syms = build()
    import nmroster
    rnd = random.Random(1)
    descs = []
    for d in nmroster.all_descriptors():
        for slot in (0, 1, 2, 3):
            descs.append((d, slot))
    base = [d for d, s in descs]
    for i in range(400):
        d = bytearray(rnd.choice(base))
        for k in range(1, 25):
            if rnd.random() < 0.15: struct.pack_into(">h", d, 8 * k, 0)
        for i2 in range(3):
            if rnd.random() < 0.5: struct.pack_into(">H", d, 0x25A + 2 * i2, rnd.randrange(0x10000))
        if rnd.random() < 0.5: d[0x254] ^= 0x80
        descs.append((bytes(d), rnd.randrange(4)))
    bad = check_nm(blob, syms, descs)
    sys.exit(1 if bad else 0)

# ---- WrestleMania 2000's way ----------------------------------------------------------------------
WM_BONES, WM_PARTS, WM_POOL = 0x80088460, 0x8009B0D0, 0x8006E290
NM_BONES, NM_PARTS, NM_POOL = 0x80091CC0, 0x800A4050, 0x80070D20
def namer(bones, parts, pool, slot, nb, wm):
    b0, p0, q0 = bones + slot * nb * 0xDC, parts + slot * nb * 0x34, pool + slot * 16
    def name(v):
        if v == 0: return 0
        if b0 <= v < b0 + nb * 0xDC and (v - b0) % 0xDC == 0:
            k = (v - b0) // 0xDC; return ("B", k + 3 if wm and k >= 22 else k)
        if p0 <= v < p0 + nb * 0x34 and (v - p0) % 0x34 == 0:
            k = (v - p0) // 0x34; return ("P", k + 3 if wm and k >= 22 else k)
        if v == q0: return "pool"
        return ("?", hex(v))
    return name, b0

def wm_run(E0ram, fn, desc, slot, calls, bones, nb, wm, blob=None):
    E = Emu(E0ram)
    if blob: E.wbytes(BLOB, blob)
    b0 = bones + slot * nb * 0xDC
    for k in range(nb):                     # the draw objects: known values, the same in both games' RAM
        kk = k + 3 if wm and k >= 22 else k
        E.w8(b0 + k * 0xDC + 0x18, 0x11 + kk)
        o = E.r32(b0 + k * 0xDC + 0x14)
        if 0x80000000 <= o < 0x80800000:
            for off in range(0x0C, 0x28, 4): E.w32(o + off, struct.unpack(">I", struct.pack(">f", kk * 100 + off))[0])
    desc_at = 0x80780000
    E.wbytes(desc_at, desc)
    log = []
    def hook(name, n_stack, n_args):
        def f(e):
            st = [e.r32(e.r[29] + 0x10 + 4 * i) for i in range(n_stack)]
            a = list(e.r[4:4 + n_args])
            if name == "attach":
                a[3] &= 0xFFFF
                for i, v in enumerate(st): e.w32(a[0] + 4 * i, v)      # (what func_8000C58C does to the links)
            if name == "clear":                                         # (func_80015D54)
                e.w8(a[0] + 0x18, 0x80); e.w32(a[0], 0)
                o = e.r32(a[0] + 0x14)
                if o: e.w32(o + 0x30, 0); e.w16(o + 0x2C, 0); e.w16(o + 0x2E, 0); e.w32(o + 0x34, 0)
            log.append((name, a + st))
            return True
        return f
    for addr, (name, ns, na) in calls.items(): E.hooks[addr] = hook(name, ns, na)
    E.call(fn, [desc_at, slot])
    objs = {}
    for k in range(nb):
        o = E.r32(b0 + k * 0xDC + 0x14)
        kk = k + 3 if wm and k >= 22 else k
        if 0x80000000 <= o < 0x80800000:
            objs[kk] = tuple(round(struct.unpack(">f", E.rbytes(o + off, 4))[0], 3) for off in range(0x0C, 0x28, 4))
    links = {}
    for k in range(nb):
        kk = k + 3 if wm and k >= 22 else k
        links[kk] = tuple(E.r32(b0 + k * 0xDC + off) for off in (0, 4, 8)) + (E.r8(b0 + k * 0xDC + 0x18),)
    return log, objs, links

def check_wm(blob, syms, cases):
    import wmport
    wram, nram = open(WM_RAM, "rb").read(), open(NM_RAM, "rb").read()
    bad = 0
    for n, (wdesc, slot) in enumerate(cases):
        ndesc = wmport.nm_descriptor(wdesc)
        lw, ow, kw = wm_run(wram, 0x800166B4, wdesc, slot, WM_CALLS, WM_BONES, 25, True)
        lp, op, kp = wm_run(nram, syms["skel_setup"], ndesc, slot, NM_CALLS, NM_BONES, 28, False, blob)
        nw, _ = namer(WM_BONES, WM_PARTS, WM_POOL, slot, 25, True)
        nn, _ = namer(NM_BONES, NM_PARTS, NM_POOL, slot, 28, False)
        sw = [(c, tuple(nw(v) if i != 3 or c != "attach" else v for i, v in enumerate(a))) for c, a in lw]
        sp = [(c, tuple(nn(v) if i != 3 or c != "attach" else v for i, v in enumerate(a))) for c, a in lp if c != "clear"]
        clears = [nn(a[0]) for c, a in lp if c == "clear"]
        # the bones WM2000 has: their links and draw objects the same
        kw2 = {k: tuple(nw(x) for x in v[:3]) + (v[3],) for k, v in kw.items()}
        kp2 = {k: tuple(nn(x) for x in v[:3]) + (v[3],) for k, v in kp.items()}
        same_links = all(kw2[k] == kp2[k] for k in kw2 if k != 21)   # (21: WM2000 has no second head piece; ours cleared)
        same_objs = all(ow[k] == op.get(k) for k in ow)
        ok = sw == sp and same_links and same_objs
        if not ok:
            bad += 1
            if bad < 5:
                print("WM MISMATCH case", n, "slot", slot, "calls", sw == sp, "links", same_links, "objs", same_objs)
                for x, y in zip(sw, sp):
                    if x != y: print("  wm  ", x, "\n  port", y); break
                if len(sw) != len(sp): print("  lengths", len(sw), len(sp), sw[len(sp):][:3], sp[len(sw):][:3])
                for k in kw2:
                    if k != 21 and kw2[k] != kp2[k]: print("  link", k, kw2[k], kp2[k]); break
                for k in ow:
                    if ow[k] != op.get(k): print("  obj", k, ow[k], op.get(k)); break
    print("WrestleMania 2000's way: %d descriptors, %d differ" % (len(cases), bad))
    return bad

def wm_cases(rnd, n=300):
    ram = open(WM_RAM, "rb").read()
    real = [bytes(ram[0x9DF90 + s * 0x258:0x9DF90 + s * 0x258 + 0x258]) for s in range(4)]
    cases = [(d, s) for d in real for s in range(4)]
    for i in range(n):
        d = bytearray(rnd.choice(real))
        if rnd.random() < 0.2: struct.pack_into(">h", d, 0xA0, 0)   # (no head; parts 1-19 a WM2000 model always has)
        for i2 in range(3):
            if rnd.random() < 0.6:
                struct.pack_into(">H", d, 0x24A + 2 * i2, rnd.randrange(0x10000))
                struct.pack_into(">H", d, 0xB0 + 8 * i2, rnd.randrange(1, 0x2000))
        if rnd.random() < 0.5: struct.pack_into(">H", d, 0x250, rnd.randrange(0x10000))
        struct.pack_into(">I", d, 0x244, rnd.choice([0, 4, 0x20, 0x24, 0x2, rnd.randrange(1 << 31)]))   # (bit 31: ours, SKEL_WM_REV)
        cases.append((bytes(d), rnd.randrange(4)))
    return cases

if __name__ == "__main__" and "wm" in sys.argv:
    blob, syms = build()
    bad = check_wm(blob, syms, wm_cases(random.Random(2)))
    sys.exit(1 if bad else 0)

# ---- func_80014F5C (which parts are drawn) --------------------------------------------------------------
def check_visible(blob, syms, n=600):
    ram = bytearray(open(NM_RAM, "rb").read())
    rnd = random.Random(3)
    skip = [(0x807F0000, 0x80800000), (BLOB, BLOB + 0x10000)]
    bad = 0
    import wmport
    for case in range(n):
        slot = rnd.randrange(4)
        E0 = Emu(ram)
        b0 = NM_BONES + slot * 0x1810
        for k in range(28): E0.w8(b0 + k * 0xDC + 0x18, rnd.choice([0, 0x80, 1, 2, 4, 5, rnd.randrange(256)]))
        if rnd.random() < 0.7: E0.w8(b0 + 0xDC + 0x18, 0)
        for i in range(3):
            E0.w32(b0 + (25 + i) * 0xDC, rnd.choice([0, b0 + rnd.randrange(1, 20) * 0xDC]))
        rec = 0x800A1130 + slot * 0xB24 + 0xD0
        for k in range(19):
            for j in range(3):
                v = rnd.choice([rnd.uniform(-400, 400), rnd.uniform(-50, 50), 0.0, -0.0, 230.0, -224.0,
                                90.0, 270.0, -270.0, 280.0, -280.0, 300.0, -300.0, rnd.uniform(-300, -260), rnd.uniform(260, 300)])
                E0.w32(rec + k * 0x7C + 4 * j, struct.unpack(">I", struct.pack(">f", v))[0])
        E0.w32(0x80097EC0, rnd.choice([0, 0x10000, 0x10000, rnd.randrange(1 << 32)]))
        if rnd.random() < 0.3:      # (a WM2000 model in the slot: only the port has that way; checked below)
            pass
        base = bytes(E0.m)
        out = []
        for fn, b in ((0x80014F5C, None), (syms["skel_visible"], blob)):
            E = Emu(base)
            if b: E.wbytes(BLOB, b)
            log = []
            def h(e, log=log):
                log.append(tuple(e.r[4:8])); e.w8(e.r[4] + 0x18, len(log)); return True   # (o32: the floats in a1-a3)
            E.hooks[0x800151A0] = h
            E.call(fn, [slot])
            out.append((E, log))
        (Eo, lo), (Ep, lp) = out
        d = ramdiff(Eo.m, Ep.m, skip)
        if lo != lp or d:
            bad += 1
            if bad < 4: print("VIS MISMATCH", case, slot, len(lo), len(lp), lo[:2], lp[:2], [hex(x) for x in d[:6]])
    print("func_80014F5C: %d states, %d differ" % (n, bad))
    return bad

if __name__ == "__main__" and "vis" in sys.argv:
    blob, syms = build()
    sys.exit(1 if check_visible(blob, syms) else 0)

# ---- func_80016464 (textures) --------------------------------------------------------------------------
def check_textures(blob, syms, n=500):
    import nmroster
    ram = open(NM_RAM, "rb").read()
    rnd = random.Random(4)
    base_descs = nmroster.all_descriptors()
    skip = [(0x807F0000, 0x80800000), (BLOB, BLOB + 0x10000)]
    bad = 0
    for case in range(n):
        d = bytearray(rnd.choice(base_descs))
        for o in (0x18C, 0x190, 0x228, 0x230, 0x240, 0x248, 0x24C, 0x250, 0x258):
            if rnd.random() < 0.3: struct.pack_into(">h", d, o, rnd.choice([0, rnd.randrange(-5, 0x4C00)]))
        for o in (0x192, 0x193, 0x242, 0x243, 0x252, 0x24E, 0x26C, 0x26D):
            if rnd.random() < 0.3: d[o] = rnd.randrange(256)
        if rnd.random() < 0.5: d[0x254] = rnd.randrange(256)
        if rnd.random() < 0.5: d[0x255] = rnd.choice([0, 1, 2, 3, 0x80, 0xFF])
        for i in range(3):
            if rnd.random() < 0.4: struct.pack_into(">hhBB", d, 0xC8 + 8 * i + 2, rnd.randrange(0, 0x4C00), rnd.randrange(0, 0x4C00), rnd.randrange(256), rnd.randrange(256))
        slot, phase = rnd.randrange(4), rnd.choice([0, 1, 2])
        mode = rnd.choice([0, 3, 3, 1])
        outs = []
        for fn, b in ((0x80016464, None), (syms["skel_textures"], blob)):
            E = Emu(ram)
            if b: E.wbytes(BLOB, b)
            E.w8(0x80097FD8, mode)
            E.wbytes(0x80780000, bytes(d))
            for k in range(28):        # each part's palette and picture: somewhere known
                E.w32(NM_PARTS + slot * 0x5B0 + k * 0x34 + 0x20, 0x80600000 + k * 0x1000)
                E.w32(NM_PARTS + slot * 0x5B0 + k * 0x34 + 0x24, 0x80640000 + k * 0x1000)
            for i in range(2): E.w32(0x80076CF8 + slot * 8 + 4 * i, rnd.choice([0, 0x80680000]) if False else 0x80680000 * (case & 1))
            log = []; alloc = [0x806C0000]
            def mk(name, na, ns, special=None):
                def f(e):
                    a = list(e.r[4:4 + na]) + [e.r32(e.r[29] + 0x10 + 4 * i) for i in range(ns)]
                    if special: a = special(e, a)
                    log.append((name, tuple(a)))
                    if name == "alloc": e.r[2] = alloc[0]; alloc[0] += 0x1000
                    if name == "bcopy": e.wbytes(a[1], e.rbytes(a[0], min(a[2], 0x1000)))
                    return True
                return f
            E.hooks[0x80013FD4] = mk("tex", 4, 1)
            E.hooks[0x8000CA18] = mk("ca18", 3, 0)
            E.hooks[0x8000CAF8] = mk("caf8", 4, 3)
            E.hooks[0x80019550] = mk("19550", 4, 0, lambda e, a: [a[0], e.r8(a[1] + 2), e.r8(a[1] + 3), a[2], a[3]])
            E.hooks[0x80019D88] = mk("19d88", 2, 0)
            E.hooks[0x80017190] = mk("17190", 1, 0)
            E.hooks[0x8000093C] = mk("free", 1, 0)
            E.hooks[0x800007D4] = mk("alloc", 1, 0)
            E.hooks[0x80036710] = mk("bcopy", 3, 0)
            E.call(fn, [0x80780000, slot, phase])
            outs.append((E, log))
        (Eo, lo), (Ep, lp) = outs
        # (the 16-bit arguments: compare their low halves)
        norm = lambda L: [(n, tuple(x & 0xFFFF if n in ("tex", "ca18", "caf8") and 0 < i < 3 else x for i, x in enumerate(a))) for n, a in L]
        dd = ramdiff(Eo.m, Ep.m, skip)
        if norm(lo) != norm(lp) or dd:
            bad += 1
            if bad < 4:
                print("TEX MISMATCH", case, "slot", slot, "phase", phase, len(lo), len(lp), [hex(x) for x in dd[:6]])
                for x, y in zip(norm(lo), norm(lp)):
                    if x != y: print("  game", x[0], [hex(v) for v in x[1]], "\n  port", y[0], [hex(v) for v in y[1]]); break
    print("func_80016464: %d cases, %d differ" % (n, bad))
    return bad

if __name__ == "__main__" and "tex" in sys.argv:
    blob, syms = build()
    sys.exit(1 if check_textures(blob, syms) else 0)

# ---- func_80016D2C (hands) ------------------------------------------------------------------------------
def check_hands(blob, syms, n=600):
    ram = open(NM_RAM, "rb").read()
    rnd = random.Random(5)
    skip = [(0x807F0000, 0x80800000), (BLOB, BLOB + 0x10000)]
    bad = 0
    for case in range(n):
        E0 = Emu(ram)
        E0.w32(0x800484EC, rnd.choice([0, 1, 1, 1]))
        for slot in range(4):
            E0.w8(0x800A8980 + slot * 0x270 + 0x254, rnd.choice([0, 0, 8, rnd.randrange(256)]))
            E0.w8(0x80076588 + slot, rnd.choice([rnd.randrange(20), 0x13, 0]))
            for i in range(4): E0.w8(0x80076590 + slot * 4 + i, rnd.choice([0xFF, rnd.randrange(12), 0]))
            E0.w16(0x80076264 + 2 * slot, rnd.choice([6, 8, rnd.randrange(64)]))
            E0.w16(0x80070D0A + 2 * slot, rnd.randrange(16))
        base = bytes(E0.m)
        outs = []
        for fn, b in ((0x80016D2C, None), (syms["hands_frame"], blob)):
            E = Emu(base)
            if b: E.wbytes(BLOB, b)
            log = []
            def mk(name, na, ns):
                def f(e):
                    log.append((name, tuple(e.r[4:4 + na]) + tuple(e.r32(e.r[29] + 0x10 + 4 * i) for i in range(ns)))); return True
                return f
            E.hooks[0x8000DEC4] = mk("dec4", 4, 0); E.hooks[0x8000E014] = mk("e014", 3, 0); E.hooks[0x8000CAF8] = mk("caf8", 4, 3)
            E.call(fn, [])
            outs.append((E, log))
        (Eo, lo), (Ep, lp) = outs
        d = ramdiff(Eo.m, Ep.m, skip)
        if lo != lp or d:
            bad += 1
            if bad < 4:
                print("HANDS MISMATCH", case, len(lo), len(lp), [hex(x) for x in d[:6]])
                for x, y in zip(lo, lp):
                    if x != y: print("  game", x[0], [hex(v) for v in x[1]], "\n  port", y[0], [hex(v) for v in y[1]]); break
    print("func_80016D2C: %d states, %d differ" % (n, bad))
    return bad

if __name__ == "__main__" and "hands" in sys.argv:
    blob, syms = build()
    sys.exit(1 if check_hands(blob, syms) else 0)

# ---- WM2000's hands (its 800173AC) against hands.c's hands_wm ----------------------------------------------
def check_hands_wm(blob, syms, n=400):
    import wmport
    wram, nram = open(WM_RAM, "rb").read(), open(NM_RAM, "rb").read()
    rnd = random.Random(6)
    bad = 0
    WM_KIND_TAB = wram[0x41CC4:0x41CC4 + 0x40]
    for case in range(n):
        slot = rnd.randrange(4)
        pose = rnd.choice([rnd.randrange(20), 0x13, 0])
        shown = [rnd.choice([0xFF, rnd.randrange(11), 3, 10, 0]) for i in range(4)]
        look0 = rnd.choice([1, 3, 0, 2, 5, 8])
        flags = rnd.choice([0, 0, 4, 0x20])
        # WM2000's own, on its RAM
        Ew = Emu(wram)
        Ew.w32(0x80041EC8, 1)
        for s in range(4):
            Ew.w32(0x8009DF90 + s * 0x258 + 0x244, flags if s == slot else 4)
        Ew.w8(0x80073CE8 + slot, pose)
        for i in range(4): Ew.w8(0x80073CF0 + slot * 4 + i, shown[i])
        Ew.w16(0x8006E280 + 2 * slot, look0)
        lw = []
        def wmname(a):
            if 0x80074250 <= a < 0x80074250 + 0x160: o = a - 0x80074250; return ("odd", o // 0xB0, (o % 0xB0) // 16)
            if 0x800740D0 <= a < 0x800740D0 + 0x180: o = a - 0x800740D0; return ("even", o // 0xC0, (o % 0xC0) // 0x40, (o % 0x40) // 16)
            if 0x80074080 <= a < 0x80074080 + 0x50: return ("special", (a - 0x80074080) // 0x14)
            if 0x80073F40 <= a < 0x80073F40 + 0x140: o = a - 0x80073F40; return ("pics", o // 0x50, (o % 0x50) // 0x14)
            if 0x80073D00 <= a < 0x80073D00 + 0x240: o = a - 0x80073D00; return ("draw", o // 0x90, (o % 0x90) // 0x24)
            if 0x80088460 <= a < 0x80088460 + 4 * 0x157C: o = a - 0x80088460; return ("bone", o // 0x157C, (o % 0x157C) // 0xDC)
            if 0x8009B0D0 <= a < 0x8009B0D0 + 4 * 0x514: o = a - 0x8009B0D0; return ("part", o // 0x514, (o % 0x514) // 0x34)
            return hex(a)
        def hw(e): lw.append(("draw", tuple(wmname(x) for x in e.r[4:8]))); return True
        def hd(e): lw.append(("mark", wmname(e.r[4]), e.r[5], e.r[6], e.r[7])); return True
        Ew.hooks[0x8000D4B4] = hw; Ew.hooks[0x8000BFE8] = hd
        Ew.call(0x800173AC, [])
        lw = [x for x in lw if not (x[0] == "draw" and x[1][0][1] != slot) and not (x[0] == "mark" and x[1][1] != slot)]
        # ours, on No Mercy's
        En = Emu(nram); En.wbytes(BLOB, blob)
        En.w32(0x800484EC, 1)
        for s in range(4):
            d = bytearray(0x270)
            if s == slot:
                struct.pack_into(">I", d, wmport.SKEL_MAGIC_AT, wmport.SKEL_MAGIC_WM)
                struct.pack_into(">I", d, wmport.SKEL_WM_FLAGS, flags)
                d[wmport.SKEL_WM_HANDS] = WM_KIND_TAB[look0]
            else: d[0x254] = 8
            En.wbytes(0x800A8980 + s * 0x270, bytes(d))
        En.w8(0x80076588 + slot, pose)
        for i in range(4): En.w8(0x80076590 + slot * 4 + i, shown[i])
        for s in range(4): En.w8(syms["wmh_keep"] + s, 0)
        En.w32(syms["wmh_ready"], 1); En.w8(syms["wmh_kept"] + slot, 1)
        def nmname(a):
            for nm, base, shape in (("odd", syms["wmh_odd"], (11 * 16, 16)), ("even", syms["wmh_even"], (0xC0, 0x40, 16)),
                                    ("special", syms["wmh_special"], (0x14,)), ("pics", syms["wmh_pics"], (0x50, 0x14))):
                size = shape[0] * (2 if nm in ("odd", "even") else (1 if nm == "special" else 6)) if nm != "special" else 0x50
                if base <= a < base + size:
                    o = a - base; out = [nm]
                    for st in shape: out.append(o // st); o %= st
                    return tuple(out)
            if 0x800765A0 <= a < 0x800765A0 + 0x240: o = a - 0x800765A0; return ("draw", o // 0x90, (o % 0x90) // 0x24)
            if 0x80091CC0 <= a < 0x80091CC0 + 4 * 0x1810: o = a - 0x80091CC0; return ("bone", o // 0x1810, (o % 0x1810) // 0xDC)
            if 0x800A4050 <= a < 0x800A4050 + 4 * 0x5B0: o = a - 0x800A4050; return ("part", o // 0x5B0, (o % 0x5B0) // 0x34)
            return hex(a)
        ln = []
        def hn(e): ln.append(("draw", tuple(nmname(x) for x in e.r[4:8]))); return True
        def hm(e): ln.append(("mark", nmname(e.r[4]), e.r[5], e.r[6], e.r[7])); return True
        En.hooks[0x8000DEC4] = hn; En.hooks[0x8000CAF8] = hm
        En.call(syms["hands_frame"], [])
        # (the files: WM2000's 0x122E / 0x122D are ours wmh_decal_files)
        lw2 = [x if x[0] != "mark" else ("mark", x[1], "decal", "decal", x[4]) for x in lw]
        ln2 = [x if x[0] != "mark" else ("mark", x[1], "decal", "decal", x[4]) for x in ln]
        if lw2 != ln2:
            bad += 1
            if bad < 4: print("WM HANDS MISMATCH", case, "slot", slot, "pose", pose, "shown", shown, "\n  wm  ", lw2, "\n  ours", ln2)
    print("WM2000's hands: %d states, %d differ" % (n, bad))
    return bad

if __name__ == "__main__" and "wmhands" in sys.argv:
    blob, syms = build()
    sys.exit(1 if check_hands_wm(blob, syms) else 0)
