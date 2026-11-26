#!/usr/bin/env python3
"""The new Superstars' looks (src/parts_ext.h): the body parts No Mercy Plus V3 made for them, as more
entries of the game's own part tables.

A costume's looks are kept packed (0x17 bytes, func_8001A9A0 / func_8001AC44) in fields too narrow for
the new parts (a lower body in 7 bits, a mask or beard in 5) and turned into a model by func_80018A3C
from the unpacked struct (0x3F bytes) through tables of parts. No Mercy Plus widened those fields and
tables; here the tables get the new entries after the game's own (gen/patches.py points the game's
code at the bigger tables, parts.c holds them), and a new Superstar's costume is kept packed as a key:
the 0x17 bytes with the one bit the game never uses (bit 183) set, which parts.c's unpacking turns
into the costume's whole struct.

  lower body  No Mercy Plus 128, 131 -> 128, 129
  upper body  107 -> 128 (the game's 107 is an empty one; 125-127 are none)
  face        82, 83 -> 128, 129 (No Mercy Plus drew the Headbangers' faces over the game's unused
              82 / 83: here they're new ones, the game's stay as they are for Create-a-Wrestler)
  beard       32, 33;  mask 32, 33
  colours of a part (func_8001858C's "param") 0x4B -> 0x80
Their files (No Mercy Plus's new ones, and its two faces) are the arcade's file IDs (files.c).

Inputs as gen/mkroster.py's. gen/checkparts.py checks the result: the game's code run with these
tables (the test blob) makes the same models as No Mercy Plus's code with its own.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct
sys.path.insert(0, nmp("/home/devbox/work/arcade/gen"))
import mkroster as R
W = R.W
OUT = W + "arcade/src/parts_ext.h"
VAN, NM = R.van, R.nm
EV, EN = R.EV, R.EN

def vr(a, n): a &= 0x7FFFFF; return bytes(VAN[a:a + n])
def nr(a, n): a &= 0x7FFFFF; return bytes(NM[a:a + n])
def u16(b, i=0): return struct.unpack_from(">H", b, i)[0]
def u32(b, i=0): return struct.unpack_from(">I", b, i)[0]
NMF = nmp("/home/devbox/nmplus/f/%04x.bin")
VANF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
def nmfile(i):
    try: return open(NMF % i, "rb").read()
    except OSError: return None
def vanfile(i):
    try: return open(VANF % i, "rb").read()
    except OSError: return None

# ---- the files: No Mercy Plus's new ones (0x5290-0x52DF) and the faces it drew over the game's ------
NM_BLOCK, NM_BLOCK_END, OUR_BLOCK = 0x5290, 0x52E0, 0x4C60
NM_FACES, NM_FACES_END, OUR_FACES = 0x1E16, 0x1E1A, 0x4CB0
NM_JJ, NM_JJ_END, OUR_JJ = 0x5128, 0x5134, 0x4CB4   # Jeff Jarrett's ring attire and face
OUR_END = 0x4CC0                       # (files.c FILES_END is above)
def nm_new(f): return NM_BLOCK <= f < NM_BLOCK_END or NM_FACES <= f < NM_FACES_END or NM_JJ <= f < NM_JJ_END
def our_file(f):
    if NM_BLOCK <= f < NM_BLOCK_END: return OUR_BLOCK + (f - NM_BLOCK)
    if NM_FACES <= f < NM_FACES_END: return OUR_FACES + (f - NM_FACES)
    if NM_JJ <= f < NM_JJ_END: return OUR_JJ + (f - NM_JJ)
    assert f < 0x4C03 and nmfile(f) == vanfile(f), "file %04x: No Mercy Plus's differs from the game's" % f
    return f
used = set()
def ref(f, companions=(-1, 0, 1, 2, 3)):
    """a file No Mercy Plus's part names: it and the ones next to it the game reads with it"""
    g = our_file(f)
    if g != f:
        for d in companions:
            if nmfile(f + d) is not None and nm_new(f + d):
                used.add(f + d)
    return g

# ---- the tables: (game's address, No Mercy Plus's, entry size, the game's count) ------------------
T = dict(
    lower_kind=(0x8004CF98, 0x8040C220, 1, 0x80), lower_desc=(0x8004D018, 0x8040BE00, 4, 0x80),
    lower_fem=(0x8004FFAA, 0x804098F0, 1, 0x80),
    upper_kind=(0x8004EB98, 0x8040ECA0, 1, 0x7D), upper_desc=(0x8004EC18, 0x8040E850, 4, 0x7D),
    face_tex=(0x8004D254, 0x8004BF00, 2, 0x80), face_b1=(0x8004D354, 0x8004C110, 1, 0x76),
    face_b2=(0x8004D3CC, 0x8004C220, 1, 0x76), face_tv=(0x8004FE8C, 0x8040F700, 2, 0x80),
    fh_tex=(0x8004D528, 0x8004C340, 2, 0x20), fh_b=(0x8004D568, 0x8004C3E0, 1, 0x20),
    mask_desc=(0x8004EF84, 0x8004E2C0, 4, 0x20), mask_b=(0x8004F004, 0x8004E3E0, 1, 0x20),
    param_kind=(0x80049804, 0x8040CD00, 1, 0x48), param_files=(0x800496E4, 0x8040C820, 4, 0x48),
)
# the new entries: our index -> No Mercy Plus's
NEW = dict(lower={128: 128, 129: 131, 130: 143, 131: 151}, upper={0x80: 107}, face={0x80: 82, 0x81: 83, 0x82: 132},
           fh={32: 32, 33: 33}, mask={32: 32, 33: 33}, param={0x80: None})
LOWER_N, UPPER_EXT0, FACE_EXT0, FACE_EXT_N = 0x84, 0x80, 0x80, 3
FH_N, MASK_N, PARAM_EXT0, PARAM_EXT_N = 0x22, 0x22, 0x80, 1
def nm_entry(name, i):
    _, na, sz, _ = T[name]; b = nr(na + sz * i, sz)
    return b[0] if sz == 1 else (u16(b) if sz == 2 else u32(b))
def van_entries(name, n=None):
    va, _, sz, cnt = T[name]; n = n or cnt
    b = vr(va, sz * n)
    return [b[i] if sz == 1 else (u16(b, 2 * i) if sz == 2 else u32(b, 4 * i)) for i in range(n)]

# the colours of a part: func_8001858C(model, colours, type, param): param -> kind (a byte); the kind's
# lists (the game's three tables of kinds, which No Mercy Plus kept) and the param's files
VAN_PK = (0x80049598, 0x8004955C, 0x8004958C); NM_PK = (0x80049638, 0x800495FC, 0x8004962C)
nm_params = {}
def param(p):
    if p == 0 or p < 0x48: return p
    if p not in nm_params:
        assert len(nm_params) < PARAM_EXT_N, "more colour params than PARAM_EXT_N"
        nm_params[p] = PARAM_EXT0 + len(nm_params)
        NEW["param"][nm_params[p]] = p
    return nm_params[p]

def lower_desc(nmi):
    d = bytearray(nr(nm_entry("lower_desc", nmi), 0x24))
    for i in range(0, 0x22, 2):
        f = u16(d, i)
        if f: struct.pack_into(">H", d, i, ref(f))
    d[0x23] = param(d[0x23])
    return bytes(d)
def upper_desc(nmi):
    s = nr(nm_entry("upper_desc", nmi), 0x1A)
    assert u16(s, 0x16) == 0 and s[0x18] == 0, "No Mercy Plus's upper body %d uses its 12th file" % nmi
    d = bytearray(0x18)
    for i in range(0, 0x16, 2):
        f = u16(s, i)
        if f: struct.pack_into(">H", d, i, ref(f))
    d[0x17] = param(s[0x19])
    return bytes(d)
def mask_desc(nmi):
    s = nr(nm_entry("mask_desc", nmi), 8); d = bytearray(8)
    for i in range(0, 8, 2):
        f = u16(s, i)
        if f: struct.pack_into(">H", d, i, ref(f))
    return bytes(d)

def build_tables():
    tabs, descs = {}, {}
    LOW = sorted(NEW["lower"]); FAC = sorted(NEW["face"])
    tabs["lower_kind"] = van_entries("lower_kind") + [nm_entry("lower_kind", NEW["lower"][i]) for i in LOW]
    tabs["lower_fem"] = van_entries("lower_fem") + [nm_entry("lower_fem", NEW["lower"][i]) for i in LOW]
    tabs["lower_desc"] = van_entries("lower_desc") + ["lower_%d" % i for i in LOW]
    for i in LOW: descs["lower_%d" % i] = lower_desc(NEW["lower"][i])
    # (the game's are 0x7D long, and 0x7D-0x7F is "none": a packed upper body can be those)
    tabs["upper_kind"] = van_entries("upper_kind") + [0] * 3 + [nm_entry("upper_kind", NEW["upper"][0x80])]
    tabs["upper_desc"] = van_entries("upper_desc") + [0] * 3 + ["upper_128"]
    descs["upper_128"] = upper_desc(NEW["upper"][0x80])
    # faces: the picture pair (the table's file and the one after it)
    tabs["face_tex"] = van_entries("face_tex") + [ref(nm_entry("face_tex", NEW["face"][i]), (0, 1)) for i in FAC]
    for n in ("face_b1", "face_b2"):     # (the game's are 0x76 long; 0x76-0x7F aren't read: patches.py)
        tabs[n] = van_entries(n) + [0] * (0x80 - 0x76) + [nm_entry(n, NEW["face"][i]) for i in FAC]
    tabs["face_tv"] = van_entries("face_tv") + [nm_entry("face_tv", NEW["face"][i]) for i in FAC]
    tabs["fh_tex"] = van_entries("fh_tex") + [ref(nm_entry("fh_tex", NEW["fh"][i]), (0, 1, 2, 3)) for i in (32, 33)]
    tabs["fh_b"] = van_entries("fh_b") + [nm_entry("fh_b", NEW["fh"][i]) for i in (32, 33)]
    tabs["mask_desc"] = van_entries("mask_desc") + ["mask_%d" % i for i in (32, 33)]
    for i in (32, 33): descs["mask_%d" % i] = mask_desc(NEW["mask"][i])
    tabs["mask_b"] = van_entries("mask_b") + [nm_entry("mask_b", NEW["mask"][i]) for i in (32, 33)]
    # the new colour params (named by the descriptors above)
    pk = van_entries("param_kind") + [0] * (PARAM_EXT0 - 0x48)
    pf = van_entries("param_files") + [0] * (PARAM_EXT0 - 0x48)
    for ours in range(PARAM_EXT0, PARAM_EXT0 + PARAM_EXT_N):
        p = NEW["param"].get(ours)
        if p is None: pk.append(0); pf.append(0); continue
        k = nm_entry("param_kind", p); pk.append(k)
        # the kind's own lists must be the game's
        cnt = struct.unpack(">b", nr(NM_PK[2] + k, 1))[0]
        assert cnt == struct.unpack(">b", vr(VAN_PK[2] + k, 1))[0]
        for vt, nt, n in ((VAN_PK[0], NM_PK[0], 2 * cnt),):
            assert vr(u32(vr(vt + 4 * k, 4)), n) == nr(u32(nr(nt + 4 * k, 4)), n), "param kind %d's list" % k
        for typ in range(12):          # (the body types: 0x80048A48's values)
            vp, np_ = u32(vr(u32(vr(VAN_PK[1] + 4 * k, 4)) + 4 * typ, 4)), u32(nr(u32(nr(NM_PK[1] + 4 * k, 4)) + 4 * typ, 4))
            if not (0x80000000 <= vp < 0x80800000 and 0x80000000 <= np_ < 0x80800000): continue
            assert vr(vp, 2 * cnt) == nr(np_, 2 * cnt), "param kind %d type %d" % (k, typ)
        lst = nr(nm_entry("param_files", p), 2 * cnt)
        descs["param_%d" % ours] = b"".join(struct.pack(">H", ref(u16(lst, 2 * i), (0, 1)) if u16(lst, 2 * i) else 0) for i in range(cnt))
        pf.append("param_%d" % ours)
    tabs["param_kind"] = pk; tabs["param_files"] = pf
    return tabs, descs

# ---- the costumes --------------------------------------------------------------------------------
# the appearance table's 0x26 bytes -> the unpacked struct (func_8000B60C)
SRC2UN = {0: 0, 1: 1, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 0xA, 0xA: 0xB, 0x1C: 0xC, 0x1D: 0xF, 0x1E: 0x12,
          0x1F: 0x13, 0x20: 0x15, 0x21: 0x18, 0x22: 0x1B, 0xC: 0x1E, 0xD: 0x1F, 0x11: 0x21, 0x12: 0x22, 0x13: 0x24,
          0x14: 0x25, 0xF: 0x27, 0x10: 0x28, 0x15: 0x2A, 0x16: 0x2B, 0x17: 0x2D, 0x18: 0x2E, 0x19: 0x30, 0x1A: 0x31,
          0x1B: 0x32, 0xE: 0x33, 0x23: 0x36, 0x24: 0x37, 0xB: 0x39, 0x25: 0x3C}
# the packed fields (func_8001AC44): (offset in the unpacked struct, bits), in order
FIELDS = [(0, 5), (1, 3), (3, 7), (4, 5), (5, 5), (6, 7), (7, 5), (8, 5), (9, 5), (0xA, 5), (0xB, 5), (0xC, 4),
          (0xF, 7), (0x12, 5), (0x13, 3), (0x15, 6), (0x18, 5), (0x1B, 5), (0x1E, 3), (0x1F, 5), (0x21, 3),
          (0x22, 5), (0x24, 3), (0x25, 5), (0x27, 3), (0x28, 5), (0x2A, 4), (0x2B, 5), (0x2D, 4), (0x2E, 5),
          (0x30, 6), (0x31, 5), (0x32, 5), (0x33, 5), (0x36, 3), (0x37, 5), (0x39, 5), (0x3C, 7)]
assert sum(b for _, b in FIELDS) == 183 and sorted(o for o, _ in FIELDS) == sorted(SRC2UN.values())
REC, SRC, UN = 0x80780000, 0x80780200, 0x80780400

def van_unpack(packed):
    EV.wbytes(UN, bytes(0x40)); EV.wbytes(REC, packed); EV.call(0x8001AC44, [REC, UN]); return EV.rbytes(UN, 0x3F)
def van_pack(un):
    EV.wbytes(UN, un + b"\0"); EV.wbytes(REC, bytes(0x18)); EV.call(0x8001A9A0, [UN, REC]); return EV.rbytes(REC, 0x17)

def costume(nm_app):
    APP = R.nmfile(1)
    return costume_src(APP[nm_app * 0x26:nm_app * 0x26 + 0x26])
# Jeff Jarrett (gen/mkroster.py: from WrestleMania 2000; his looks: No Mercy Plus's parts for him, its
# face "Jarrett" (132) and ring attire "Jarrett A" / "Jarrett B" (lower bodies 143 / 151), on Billy
# Gunn's build, short blond hair), in the appearance format with No Mercy Plus's numbers
# The appearance format's 38 bytes, by name (as VPW Studio documents them: github.com/AKI-Club/VPWStudio,
# GameSpecific/NoMercy/DefaultCostumeData.cs; the items' names are the game's own Create-a-Wrestler
# lists, files 0x4A56-0x4A66). Colours (as the game draws them): 1 black, 2 white, 3 red, 4 blue (0: an
# item's own; the boots' is silver).
APP_FIELDS = ["body", "skin", "ring", "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr", "entr_c1",
              "entr_c2", "weapon", "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c",
              "relbow", "relbow_c", "lknee", "lknee_c", "rknee", "rknee_c", "boots", "boots_c1", "boots_c2",
              "head", "face", "hair", "hair_c", "front_hair", "facial_hair", "mask", "hat", "hat_c", "photo"]
def app(base, **kw):
    b = bytearray(base)
    for k, v in kw.items(): b[APP_FIELDS.index(k)] = v
    return bytes(b)
# Jeff Jarrett (gen/mkroster.py: from WrestleMania 2000). His build cross-referenced: WrestleMania 2000's
# body size and this game's body type count the same (0 Skinny 1 .. 7 Fat 2): his 3 is Medium 2. His
# attire as WrestleMania 2000 has it in the ring, in this game's parts: No Mercy Plus V3's tights for him
# ("Jarrett A" / "Jarrett B": lower bodies 143 / 151), black knee pads ("Knee Pad 1"), black wristbands
# ("Wrist Band 1"), silver pull-on boots with their black band ("Pull-ons 05"); two costumes in his
# black tank top ("Tanktop 1"). His head is WrestleMania 2000's (gen/mkovr.py), on this game's neck.
PHOTO_JARRETT = 101
JJ_BASE = bytes.fromhex("02 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 01 02 00 00 00 00 07 00 08 00 1c 00 00 01 09 09 02 09 00 00 00 00 09")
def jj(lower, top):
    return app(JJ_BASE, body=3, ring=lower, upper=1 if top else 0, upper_c1=1, upper_c2=1, wrist=1, wrist_c=1,
               lknee=1, lknee_c=1, rknee=1, rknee_c=1, boots=28, boots_c1=0, boots_c2=1,
               face=132, hair=2, front_hair=0, photo=PHOTO_JARRETT)
JARRETT_APP = [jj(143, True), jj(151, True), jj(143, False), jj(151, False)]   # (in his tank top first, as WM2000)
# Droz and Mideon (No Mercy Plus V3's ordinals 75 / 79) and The Blue Meanie (WrestleMania 2000's): their
# models are made whole elsewhere (gen/mkovr.py: No Mercy Plus's own, WM2000's parts) and put in place
# of the game's (parts.c parts_model_post). Their costumes here are what the rest of the game reads of
# the looks (the build, the skin tone, the photo...), with the parts the game's tables haven't put to
# one they have (the model the game makes of it isn't the one shown).
PHOTO_DROZ, PHOTO_MIDEON, PHOTO_MEANIE = 102, 103, 104
OVR = [("droz", 75, PHOTO_DROZ), ("mideon", 79, PHOTO_MIDEON)]
def nm_app(o, c):
    app = EN.r16(R.NM_STRUCTS + o * 0x50 + 0x14 * c + 0xE)
    return R.nmfile(1)[app * 0x26:app * 0x26 + 0x26]
def tame(src, photo):
    """an appearance (No Mercy Plus's numbers) with only the game's own parts: what isn't, the nearest"""
    b = bytearray(src)
    if b[2] >= 0x80:                        # lower body: the game's of the same kind
        kind, fem = nm_entry("lower_kind", b[2]), nm_entry("lower_fem", b[2])
        b[2] = next(i for i in range(0x80) if van_entries("lower_kind")[i] == kind and van_entries("lower_fem")[i] == fem)
    if 0x7D <= b[5] < 0x80 or b[5] >= 0x80: b[5] = 0x7D if b[5] >= 0x7D else b[5]   # upper: none
    if b[0x1D] >= 0x76: b[0x1D] = 0         # face
    # the hair, beard and mask: the model has No Mercy Plus's (its own); in the looks, ones the game reads
    # well elsewhere too (No Mercy Plus's hair 25, Droz's, hangs the game: it isn't this game's 25)
    b[0x1E] = 2; b[0x20] = 0; b[0x21] = 0; b[0x22] = 0
    b[0x25] = photo
    return bytes(b)
def vapp(o, c):
    a = EV.r16(EV.r32(0x80047124 + 16 * o + 4 * c) + 0xE)
    return vanfile(1)[a * 0x26:a * 0x26 + 0x26]
COSTUME0 = {"droz": 4 * len(R.EXT) + 4, "mideon": 4 * len(R.EXT) + 8, "meanie": 4 * len(R.EXT) + 12}
OVR_APP = [tame(nm_app(o, c), ph) for _, o, ph in OVR for c in range(4)]
# The Blue Meanie (WrestleMania 2000's): his build cross-referenced as Jeff Jarrett's -- his body size 7 is
# this game's Fat 2 (Viscera's body type). His attire as WrestleMania 2000 has it in the ring, in this
# game's parts: a white T-shirt ("Plain"), black shorts ("Basket Shorts"), white knee pads ("Knee Pad 1"),
# blue boots ("Boots 01"); his head is WrestleMania 2000's (gen/mkovr.py: his face paint, goatee, blue
# hair). Two of his costumes in a black T-shirt (WrestleMania 2000's other look of him, the J.O.B. one).
MEANIE_APP = [app(JJ_BASE, body=7, skin=2, ring=111, ring_c1=1, ring_c2=1, upper=36, upper_c1=2 if c % 2 == 0 else 1,
                  upper_c2=2 if c % 2 == 0 else 1, wrist=0, wrist_c=0, lknee=1, lknee_c=2, rknee=1, rknee_c=2,
                  boots=1, boots_c1=4, boots_c2=4 if c < 2 else 1, face=132, hair=2, front_hair=0, facial_hair=0,
                  mask=0, photo=PHOTO_MEANIE) for c in range(4)]
# No Mercy Plus V3's Mean Street Posse, 1999 Undertaker, Raven, William Regal, Tiger Ali Singh (after the
# Meanie: gen/mkroster.py LATE): whole No Mercy Plus models, as Droz's and Mideon's; photos 105 on.
OVR2 = [(k, o, 105 + i) for i, (k, o) in enumerate((("petegas", 80), ("rodney", 81), ("joeyabs", 82), ("taker99", 83),
                                                     ("raven", 84), ("regal", 86), ("tiger", 89)))]
for i, (k, o, ph) in enumerate(OVR2): COSTUME0[k] = 4 * len(R.EXT) + 16 + 4 * i
# (costumes alike once only the game's parts are named are told apart by their hat's colour: they've no hat)
OVR2_APP = [app(tame(nm_app(o, c), ph), hat_c=c) for _, o, ph in OVR2 for c in range(4)]
assert all(a[APP_FIELDS.index("hat")] == 0 for a in OVR2_APP)
# Bret Hart and Jim Neidhart, from WCW/nWo Revenge (after the No Mercy Plus ones: gen/mkroster.py LATE):
# their models Revenge's, whole (gen/wmport.py, gen/mkovr.py), so their costumes here are only what the
# rest of the game reads of the looks: the build (Bret Medium 2, as Jarrett; Neidhart Fat 1), the skin,
# black tights and boots; the photo. (Told apart by the hat's colour, as the No Mercy Plus ones.)
PHOTO_BRET, PHOTO_NEIDHART = 112, 113
# Hollywood Hogan, Kevin Nash, Scott Hall (after them, the same way): Hogan and Nash Thick 2 (5: the Undertaker's,
# Bradshaw's), Hall Medium 1 (2: Triple H's); photos 114-116 (revport/mkphoto.py)
PHOTO_HOGAN, PHOTO_NASH, PHOTO_HALL = 114, 115, 116
# Randy Savage, Curt Hennig: Medium 2 (3, as Bret's); photos 117, 118 (revport/ports2/mkphoto_ports.py)
PHOTO_SAVAGE, PHOTO_HENNIG = 117, 118
# The Giant: Thick 2 (5, as Nash, the Undertaker); photo 119 (revport/ports3/mkphoto_ports.py)
PHOTO_GIANT = 119
PHOTO_STING = 120   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_LUGER = 121   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_GOLDBERG = 122   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_RICKSTEINER = 123   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_SCOTTSTEINER = 124   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_BOOKER = 125   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_STEVIERAY = 126   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_REY = 127   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_PIPER = 128   # (ports4: revport/ports4/mkphoto_ports.py)
PHOTO_BISCHOFF = 129   # (ports4: revport/ports4/mkphoto_ports.py)
REV_LATE = [("brethart", 3, PHOTO_BRET), ("neidhart", 6, PHOTO_NEIDHART),
            ("hogan", 5, PHOTO_HOGAN), ("nash", 5, PHOTO_NASH), ("hall", 2, PHOTO_HALL),
            ("savage", 3, PHOTO_SAVAGE), ("hennig", 3, PHOTO_HENNIG), ("giant", 5, PHOTO_GIANT), ("sting", 4, PHOTO_STING), ("luger", 4, PHOTO_LUGER), ("goldberg", 5, PHOTO_GOLDBERG), ("ricksteiner", 4, PHOTO_RICKSTEINER), ("scottsteiner", 4, PHOTO_SCOTTSTEINER), ("booker", 3, PHOTO_BOOKER), ("stevieray", 5, PHOTO_STEVIERAY), ("rey", 0, PHOTO_REY), ("piper", 2, PHOTO_PIPER), ("bischoff", 1, PHOTO_BISCHOFF)]
import revbanks      # (the second IDs: the base's body and photo, gen/revbanks.py)
REV_LATE += [(_b["key"],) + next((bd, ph) for k, bd, ph in REV_LATE if k == _b["base"]) for _b in revbanks.BANKS]
# (no2k15, 2026-11-23: the two 2K15 test slots, last here -- "stonecold15" and "rock15" -- are removed: gen/mkroster.py)
for i, (k, body, ph) in enumerate(REV_LATE): COSTUME0[k] = 4 * len(R.EXT) + 16 + 4 * len(OVR2) + 4 * i
REV_APP = [app(JJ_BASE, body=body, ring=111, ring_c1=0, ring_c2=0, upper=0, upper_c1=0, upper_c2=0,
               wrist=0, wrist_c=0, lknee=0, lknee_c=0, rknee=0, rknee_c=0, boots=1, boots_c1=0, boots_c2=0,
               face=132, hair=2, front_hair=0, facial_hair=0, mask=0, hat=0, hat_c=8 + 4 * i + c, photo=ph)
           for i, (k, body, ph) in enumerate(REV_LATE) for c in range(4)]
# (a second ID -- gen/revbanks.py -- has his base's photo: its hat colour, cut to 5 bits in the key, 16 from his, so
#  no costume of the two packs alike; no hat is worn)
_ri = {k: i for i, (k, bd, ph) in enumerate(REV_LATE)}
for _b in revbanks.BANKS:
    for _c in range(4): REV_APP[4 * _ri[_b["key"]] + _c] = app(REV_APP[4 * _ri[_b["key"]] + _c], hat_c=(8 + 4 * _ri[_b["base"]] + _c + 16) & 31)
ALL_LATE_APP = JARRETT_APP + OVR_APP + MEANIE_APP + OVR2_APP + REV_APP
def costume_src(src):
    un = bytearray(0x3F)
    for s, d in SRC2UN.items(): un[d] = src[s]
    nm_un = bytes(un)
    inv = lambda m: {v: k for k, v in m.items()}
    for off, part in ((3, "lower"), (6, "upper"), (0xF, "face"), (0x18, "fh"), (0x1B, "mask")):
        un[off] = inv(NEW[part]).get(un[off], un[off])
    # the key: its fields as the game's packing keeps them (cut to their widths), and bit 183
    cut = bytearray(un)
    for o, b in FIELDS: cut[o] &= (1 << b) - 1
    key = bytearray(van_pack(bytes(cut)))
    assert not key[0x16] & 0x80
    key[0x16] |= 0x80
    assert van_unpack(bytes(key)) == bytes(cut), "the key doesn't unpack to its fields"
    return nm_un, bytes(un), bytes(key)

def c_bytes(b, per=24):
    return ",\n".join("    " + ", ".join("0x%02X" % x for x in b[i:i + per]) for i in range(0, len(b), per))

def main():
    tabs, descs = build_tables()
    out = ["// generated by gen/mkparts.py from No Mercy Plus V3: the new Superstars' body parts, as more",
           "// entries of the game's part tables (parts.c, gen/patches.py), and their costumes",
           "#define LOWER_N 0x%X" % LOWER_N, "#define UPPER_EXT0 0x%X" % UPPER_EXT0,
           "#define FACE_EXT0 0x%X" % FACE_EXT0, "#define FACE_EXT_N %d" % FACE_EXT_N,
           "#define FH_N 0x%X" % FH_N, "#define MASK_N 0x%X" % MASK_N,
           "#define PARAM_EXT0 0x%X" % PARAM_EXT0, "#define PARAM_EXT_N %d" % PARAM_EXT_N, ""]
    for name, d in sorted(descs.items()):
        out.append("static const u8 pd_%s[%d] __attribute__((aligned(4))) = {\n%s\n};" % (name, len(d), c_bytes(d)))
    ctype = {1: "u8", 2: "u16", 4: "const void*"}
    for name, vals in tabs.items():
        va, _, sz, cnt = T[name]
        cells = []
        for v in vals:
            if isinstance(v, str): cells.append("pd_%s" % v)
            elif sz == 4: cells.append("(const void*)0x%08Xu" % v if v else "0")
            else: cells.append("0x%X" % v)
        body = ",\n".join("    " + ", ".join(cells[i:i + 8]) for i in range(0, len(cells), 8))
        out.append("// the game's 0x%08X (%d), then the new" % (va, cnt))
        out.append("%s parts_%s[%d] __attribute__((aligned(4))) = {\n%s\n};" % ("const void* const" if sz == 4 else "const " + ctype[sz], name, len(vals), body))
    # the costumes
    keys, uns, report = [], [], []
    for k, e in enumerate(R.EXT):
        for c in range(4):
            app = EN.r16(R.NM_STRUCTS + e["nm"] * 0x50 + 0x14 * c + 0xE)
            nm_un, un, key = costume(app)
            keys.append(key); uns.append(un)
    for src in ALL_LATE_APP:
        nm_un, un, key = costume_src(src)
        keys.append(key); uns.append(un)
    assert len(set(keys)) == len(keys), "two costumes alike"
    out.append("// the costumes, as kept (packed, bit 183 set) and unpacked")
    out.append("#define EXT_COSTUMES %d" % len(keys))
    n0 = 4 * len(R.EXT)
    out.append("#define COSTUME_JARRETT %d   // (then Droz's, Mideon's, The Blue Meanie's: 4 each)" % n0)
    out.append("#define COSTUME_DROZ %d" % (n0 + 4))
    out.append("#define COSTUME_MIDEON %d" % (n0 + 8))
    out.append("#define COSTUME_MEANIE %d" % (n0 + 12))
    out.append("static const u8 ext_looks_key[EXT_COSTUMES][0x17] = {")
    out += ["    { %s }," % ", ".join("0x%02X" % b for b in kk) for kk in keys]
    out.append("};")
    out.append("static const u8 ext_looks[EXT_COSTUMES][0x3F] = {")
    out += ["    { %s }," % ", ".join("0x%02X" % b for b in u) for u in uns]
    out.append("};")
    # the files
    files = sorted(used)
    out.append("// the files (No Mercy Plus's) at the arcade's IDs")
    total = 0
    for f in files:
        d = nmfile(f); total += len(d)
        out.append("static const u8 pf_%04x[%d] __attribute__((aligned(8))) = {\n%s\n};" % (f, len(d), c_bytes(d, 32)))
    out.append("typedef struct { u16 id; u16 size; const u8* data; } PartFile;")
    out.append("#define PART_FILES_N %d" % len(files))
    out.append("static const PartFile part_files[PART_FILES_N] = {")
    out += ["    { 0x%04X, %d, pf_%04x },   // No Mercy Plus's %04x" % (our_file(f), len(nmfile(f)), f, f) for f in files]
    out.append("};")
    open(OUT, "w").write("\n".join(out) + "\n")
    print("parts: %d descriptors, %d files (%d bytes), params %s" % (len(descs), len(files), total, nm_params))
    import pickle
    pickle.dump(dict(keys=keys, uns=uns, nm_uns=[costume(EN.r16(R.NM_STRUCTS + e["nm"] * 0x50 + 0x14 * c + 0xE))[0]
                for e in R.EXT for c in range(4)] + [costume_src(s)[0] for s in ALL_LATE_APP], files={f: our_file(f) for f in files}),
                open(W + "arcade/build/parts.pkl", "wb"))

if __name__ == "__main__":
    main()
