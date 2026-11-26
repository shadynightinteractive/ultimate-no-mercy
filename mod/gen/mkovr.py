#!/usr/bin/env python3
"""Models made otherwise than the game's tables would, put in place of the one the game makes of a costume
(src/parts_ovr.h; parts.c parts_model_post): each the bytes of a model it names.

Droz, Mideon, the Mean Street Posse, the 1999 Undertaker, Raven, William Regal and Tiger Ali Singh:
the model No Mercy Plus's own func_80018A3C makes of each costume (run on its RAM image,
harness/mipsemu.py), whole. It's the same in every state the game makes models in but for byte 0x254
(that one is ours). Its files the game has the same are named as they are; its own (new or drawn over
the game's) are the arcade's (OVR_FILE0 on), served from here.

Jeff Jarrett and The Blue Meanie: WrestleMania 2000's models, whole (gen/wmport.py).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct, pickle, os
sys.path.insert(0, nmp("/home/devbox/work/arcade/gen")); sys.path.insert(0, nmp("/home/devbox/work/harness"))
import mkparts as P, mkroster as R, wm2k
from mipsemu import Emu
W = nmp("/home/devbox/work/")
OUT = W + "arcade/src/parts_ovr.h"
OVR_FILE0 = 0x4D00
FILE_FIELDS = set(pickle.load(open(W + "gen_data/model_file_fields.pkl", "rb")))   # (/tmp/devbox-0/filefields.py)
nm = bytearray(open(W + "gen_data/nm_pick_ram.bin", "rb").read())
EN = Emu(nm)
GLOB_N = (0x800A9EA0, 0x800931E4, 0x80099508)
STATES = [(0, 0, 0, 0), (1, 1, 3, 0x20), (1, 0, 3, 0), (0, 1, 2, 0x20)]
UN, OUTB = 0x80781000, 0x80782000
def nm_model(un, st):
    for a, v in zip(GLOB_N, st[:3]): EN.w8(a, v)
    EN.wbytes(UN, un + b"\0"); EN.wbytes(OUTB, bytes(0x270)); EN.w8(OUTB + 0x254, st[3])
    EN.call(0x80018ACC, [UN, OUTB], max_steps=3_000_000)
    return EN.rbytes(OUTB, 0x270)

files = {}        # our ID -> data
ours_of = {}      # (source, id) -> our ID
by_data = {}
def serve(src, fid, data):
    if (src, fid) not in ours_of:
        if data in by_data: ours_of[(src, fid)] = by_data[data]   # (the same file: one ID)
        else:
            nid = max(files) + 1 if files else OVR_FILE0
            ours_of[(src, fid)] = by_data[data] = nid; files[nid] = data
    return ours_of[(src, fid)]

KEEP_OURS = set(range(0x254, 0x258))          # the state byte (and its neighbour) the game's model has
models = []       # (costume, model bytes, copy mask)
# ---- Droz, Mideon --------------------------------------------------------------------------------
# A file the model names may be read with the ones next to it (a picture's palette is the file after it,
# ...): No Mercy Plus's file f is served with f-1..f+3 at the same distances from it, where one of them
# isn't the game's (runs of them that touch are one run).
COMP = range(-1, 4)
def nm_differs(f): b = P.nmfile(f); return b is not None and b != P.vanfile(f)
# A costume with an entrance attire is made a model of twice: as it is, and with none (the game clears
# the looks' entrance attire, field 0x09, for the match: parts.c tells the two apart by it).
nm_models = []
for name, o, _ in P.OVR + P.OVR2:
    for c, un9 in [(c, e) for c in range(4) for e in sorted({P.costume_src(P.nm_app(o, c))[0][9], 0}, reverse=True)]:
        un = bytearray(P.costume_src(P.nm_app(o, c))[0]); un[9] = un9; un = bytes(un)
        ms = [nm_model(un, st) for st in STATES]
        # (bytes that differ by state: an entrance attire, there only before the match -- the game's own
        # model of the costume has it the same: those are left to it)
        vary = set(k for k in range(0x270) if len(set(m[k] for m in ms)) > 1) - KEEP_OURS
        nm_models.append((name, c, ms[0], vary, un9))
want = set()
for name, c, d, _, _ in nm_models:
    for k in range(0, 0x270, 2):
        v = struct.unpack_from(">H", d, k)[0]
        if k in KEEP_OURS or v < 0x800 or v >= 0x7802 or P.nmfile(v) is None: continue
        if any(nm_differs(v + j) for j in COMP):
            assert k in FILE_FIELDS, "%s %d: %04x at %x, not a file's place" % (name, c, v, k)
            want.update(v + j for j in COMP if P.nmfile(v + j) is not None)
nm_ours = {}
for f in sorted(want):
    if f - 1 in nm_ours: nm_ours[f] = nm_ours[f - 1] + 1
    else: nm_ours[f] = max(files) + 2 if files else OVR_FILE0          # (a gap between runs)
    files[nm_ours[f]] = P.nmfile(f); ours_of[("nm", f)] = nm_ours[f]
for name, c, m0, vary, un9 in nm_models:
    d = bytearray(m0)
    for k in range(0, 0x270, 2):
        v = struct.unpack_from(">H", d, k)[0]
        if k not in KEEP_OURS and v in nm_ours and k in FILE_FIELDS: struct.pack_into(">H", d, k, nm_ours[v])
    mask = bytes(0 if k in KEEP_OURS or k in vary else 1 for k in range(0x270))
    models.append((P.COSTUME0[name] + c, bytes(d), mask, un9))

# ---- Jeff Jarrett and The Blue Meanie: WrestleMania 2000's models, whole -------------------------------
# Each costume's model as WrestleMania 2000 has it in a match (gen/wmport.py: its meshes, its pictures as
# it drew them, its descriptor in this game's layout, marked WM2000's: skel.c joins its parts that way).
# Its files are the arcade's own from WMP_FILE0 (build/wmport.bin); byte 0x254 is the game's.
wmm = pickle.load(open(W + "gen_data/wmport_models.pkl", "rb"))
# Bret Hart and Jim Neidhart, Hollywood Hogan, Kevin Nash and Scott Hall: WCW/nWo Revenge's, the same way (its
# models are WM2000's kind: gen/wmport.py).
for name, c0 in (("jarrett", 4 * len(R.EXT)), ("meanie", P.COSTUME0["meanie"]),
                 ("brethart", P.COSTUME0["brethart"]), ("neidhart", P.COSTUME0["neidhart"]),
                 ("hogan", P.COSTUME0["hogan"]), ("nash", P.COSTUME0["nash"]), ("hall", P.COSTUME0["hall"]),
                 ("savage", P.COSTUME0["savage"]), ("hennig", P.COSTUME0["hennig"]), ("giant", P.COSTUME0["giant"]), ("sting", P.COSTUME0["sting"]), ("luger", P.COSTUME0["luger"]), ("goldberg", P.COSTUME0["goldberg"]), ("ricksteiner", P.COSTUME0["ricksteiner"]), ("scottsteiner", P.COSTUME0["scottsteiner"]), ("booker", P.COSTUME0["booker"]), ("stevieray", P.COSTUME0["stevieray"]), ("rey", P.COSTUME0["rey"]), ("piper", P.COSTUME0["piper"]), ("bischoff", P.COSTUME0["bischoff"])) + tuple((_b["key"], P.COSTUME0[_b["key"]]) for _b in __import__("revbanks").BANKS):
    for c in range(4):
        mask = bytes(0 if k == 0x254 else 1 for k in range(0x270))
        models.append((c0 + c, wmm[(name, c)], mask, None))

# ---- out --------------------------------------------------------------------------------------------
def cb(b, per=32): return ",\n".join("    " + ", ".join("0x%02X" % x for x in b[i:i + per]) for i in range(0, len(b), per))
L = ["// generated by gen/mkovr.py: whole models (No Mercy Plus V3's, WrestleMania 2000's) in place",
     "// of the game's (parts.c parts_model_post), and their files",
     "#define OVR_N %d" % len(models), "#define OVR_FILE0 0x%X" % OVR_FILE0, "#define OVR_FILES_N %d" % len(files), "#define OVR_FILE_END 0x%X" % (max(files) + 1)]
L.append("static const u8 ovr_costume[OVR_N] = { %s };" % ", ".join(str(c) for c, _, _, _ in models))
L.append("// the looks' entrance attire (field 0x09) each is for (0xFF: the costume's, whatever it is)")
L.append("static const u8 ovr_entr[OVR_N] = { %s };" % ", ".join("0xFF" if e is None else str(e) for _, _, _, e in models))
L.append("// each: the model, then which of its bytes are put in (bit per byte)")
L.append("static const u8 ovr_model[OVR_N][0x270] __attribute__((aligned(4))) = {")
for _, d, _, _ in models: L.append("  {\n%s\n  }," % cb(d))
L.append("};")
L.append("static const u8 ovr_mask[OVR_N][0x270 / 8] = {")
for _, _, m, _ in models:
    L.append("    { %s }," % ", ".join("0x%02X" % sum(m[i + j] << j for j in range(8)) for i in range(0, 0x270, 8)))
L.append("};")
for fid, b in sorted(files.items()):
    L.append("static const u8 ovf_%04x[%d] __attribute__((aligned(8))) = {\n%s\n};" % (fid, len(b), cb(b)))
L.append("static const PartFile ovr_files[OVR_FILES_N] = {")
names = {f: "%s %s" % (s_, ("%04x" % i) if isinstance(i, int) else (i if isinstance(i, str) else "")) for (s_, i), f in ours_of.items()}
L += ["    { 0x%04X, %d, ovf_%04x },   // %s" % (fid, len(b), fid, names.get(fid, "(read with the one before)")) for fid, b in sorted(files.items())]
L.append("};")
open(OUT, "w").write("\n".join(L) + "\n")
print("%d models, %d files (%d bytes), IDs 0x%X-0x%X" % (len(models), len(files), sum(len(b) for b in files.values()), OVR_FILE0, max(files)))
