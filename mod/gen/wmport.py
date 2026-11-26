#!/usr/bin/env python3
"""WrestleMania 2000's wrestlers in No Mercy: their models as WM2000 makes them.

A model is a descriptor (WM2000: 0x258 bytes, made by its 800178FC from a costume's looks; No Mercy: 0x270,
func_80018A3C) naming each part's mesh, texture and palette files, and its attachments. No Mercy's layout
of one keeps WM2000's parts where they were (0x08-0xA7: parts 1-20, 8 bytes each: mesh, texture,
palette, a byte); it added a second head piece (0xA8) and the split chest and arms (0xB0-0xC7), so the
attachments moved from 0xB0 to 0xC8. What WM2000 keeps after them that its skeleton reads goes where
skel.h says; skel.c then joins the parts WM2000's way.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import struct
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SKEL_WM_ATTACH, SKEL_WM_PLACE, SKEL_WM_HANDS, SKEL_WM_FLAGS, SKEL_MAGIC_AT = 0x25A, 0x260, 0x262, 0x264, 0x268
SKEL_MAGIC_WM = 0x574D324B

def nm_descriptor(wm, files=None):
    """WM2000's descriptor -> No Mercy's layout (files: WM2000 file number -> the number No Mercy reads it
    by; None keeps them)."""
    wm = bytes(wm)
    nm = bytearray(0x270)
    def f(v): return files[v] if files and v else v
    for k in range(1, 21):                       # parts 1-20
        o = 8 * k
        m, t, p = struct.unpack_from(">HHH", wm, o)
        struct.pack_into(">HHHBB", nm, o, f(m), f(t), f(p), wm[o + 6], wm[o + 7])
    for i in range(3):                           # the attachments
        o = 0xB0 + 8 * i
        m, t, p = struct.unpack_from(">HHH", wm, o)
        struct.pack_into(">HHHBB", nm, 0xC8 + 8 * i, f(m), f(t), f(p), wm[o + 6], wm[o + 7])
    nm[SKEL_WM_ATTACH:SKEL_WM_ATTACH + 6] = wm[0x24A:0x250]
    nm[SKEL_WM_PLACE:SKEL_WM_PLACE + 2] = wm[0x250:0x252]
    nm[SKEL_WM_FLAGS:SKEL_WM_FLAGS + 4] = wm[0x244:0x248]
    struct.pack_into(">I", nm, SKEL_MAGIC_AT, SKEL_MAGIC_WM)
    return bytes(nm)

# ---- a model as WM2000 has it in a match ---------------------------------------------------------------
WMF = nmp("/tmp/devbox-0/wm2k_files/%04x.bin")
def wfile(i): return open(WMF % i, "rb").read()
WM_DESC, WM_DESC_SIZE, WM_PARTS = 0x8009DF90, 0x258, 0x8009B0D0

def tex_size(hdr):
    """a texture file's size by its header (w-1, h-1, u16 colours, b4, b5, log2 w, log2 h): 256 colours =
    8 bits a pixel, else 4"""
    w, h = hdr[0] + 1, hdr[1] + 1
    return 8 + w * h * (8 if hdr[2] == 1 else 4) // 8
def pal_size(hdr):
    return 512 if hdr[2] == 1 else max(0x20, 2 * struct.unpack_from(">H", hdr, 2)[0])

def capture(ram, slot):
    """WM2000's model in a slot, from a RAM image of a match: its descriptor, and each part's
    {k, at (its 8 bytes in the descriptor), mesh (file), tex, pal (what its part struct holds: the picture
    WM2000 made, which is the file or the file drawn on)}"""
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    def rb(a, n): a &= 0x7FFFFF; return bytes(ram[a:a + n])
    d = rb(WM_DESC + slot * WM_DESC_SIZE, WM_DESC_SIZE)
    parts = []
    for k, at in [(k, 8 * k) for k in range(1, 21)] + [(22 + i, 0xB0 + 8 * i) for i in range(3)]:
        m, t, p = struct.unpack_from(">HHH", d, at)
        if not m: continue
        ps = WM_PARTS + slot * 0x514 + k * 0x34
        ta, pa = r32(ps + 0x20), r32(ps + 0x24)
        tex = pal = None
        if t and ta:
            tex = rb(ta, tex_size(rb(ta, 8)))
            if pa: pal = rb(pa, pal_size(tex[:8]))
        parts.append(dict(k=k, at=at, mesh=m, tex=tex, pal=pal, tex_id=t, pal_id=p))
    return d, parts

def nm_model(wm_desc, parts, new_id):
    """No Mercy's descriptor of a captured WM2000 model, its files named by new_id(bytes) -> ID"""
    nm = bytearray(nm_descriptor(wm_desc))
    for o in list(range(8, 0xB0, 8)) + list(range(0xC8, 0xE0, 8)): nm[o:o + 6] = bytes(6)
    for p in parts:
        o = p["at"] if p["k"] <= 20 else 0xC8 + 8 * (p["k"] - 22)
        m = new_id(wfile(p["mesh"]))
        t = new_id(p["tex"]) if p["tex"] else 0
        q = new_id(p["pal"]) if p["pal"] else 0
        struct.pack_into(">HHH", nm, o, m, t, q)
    return bytes(nm)

# ---- the port: who, their costumes, captured in WM2000's matches -> files and models ---------------------
# (name, WM2000 roster index). Each costume c is captured from a match WM2000 plays with him in it
# (/home/devbox/work/wmport/wmcap.py: its Superstar Select, his record in Jarrett's place, costume c).
PORTED = [("jarrett", 29), ("meanie", 41)]
CAP = nmp("/home/devbox/work/wmport/cap/k%d_c%d")
WMP_FILE0 = 0x5000          # our file IDs (files.c): from here, contiguous
OUT_BIN = nmp("/home/devbox/work/arcade/build/wmport.bin")
OUT_H = nmp("/home/devbox/work/arcade/src/wmport_files.h")
OUT_PKL = nmp("/home/devbox/work/gen_data/wmport_models.pkl")
OUT_VIDEOS = nmp("/home/devbox/work/gen_data/wmport_videos.pkl")   # name -> its video number

def looks0(k, c):
    """a costume's looks' first byte (WM2000 keeps it per slot: its hands' kind by it)"""
    import wm2k
    return wm2k.rbytes(wm2k.roster()[k]["cost"][c], 1)[0]

def ensure_capture(k, c):
    import subprocess
    d = CAP % (k, c)
    if not os.path.exists(d + "/ram.bin"):
        subprocess.check_call([_sys.executable, nmp("/home/devbox/work/wmport/wmcap.py"), str(k), str(c)])
    return open(d + "/ram.bin", "rb").read()

# No Mercy's palettes are 256 colours (0x200 bytes) a part, and its code puts colours into them past the first 16 --
# the face's and the skin's decals (func_8000CAF8: 2 * at bytes in, up to 0x200), a part's palette made that size. A
# WrestleMania 2000 / Revenge picture's palette as captured is its own colours only (0x20, 0x40, 0xA0 bytes): made at
# that size, as a model loaded in the middle of a Royal Rumble has it, the decal ran over the next thing in the game's
# heap -- its display list's end: the renderer drew it over and over, the game froze (2026-10-27). So every palette a
# model names is No Mercy's size here: its colours, then unused ones. (gen/nmpal.py does this to a pack made before.)
NM_PAL_SIZE = 0x200
def nm_palettes(data, models):
    """the palette files the models name (each part's third u16) made NM_PAL_SIZE bytes; -> how many"""
    n = 0
    pals = set()
    for nm in models.values():
        for k in range(1, 28):
            o = 8 * k if k <= 20 else (0xA8 + 8 * (k - 21))
            q = struct.unpack_from(">H", nm, o + 4)[0]
            if q >= WMP_FILE0 and q - WMP_FILE0 < len(data): pals.add(q - WMP_FILE0)
    for i in sorted(pals):
        if len(data[i]) < NM_PAL_SIZE:
            data[i] = bytes(data[i]) + bytes(NM_PAL_SIZE - len(data[i])); n += 1
    return n

class Files:
    """our files: each (data) once; a picture's palette the file before it (as the game's are)"""
    def __init__(self): self.data = []; self.by = {}
    def one(self, b):
        if b not in self.by: self.by[b] = WMP_FILE0 + len(self.data); self.data.append(b)
        return self.by[b]
    def pair(self, tex, pal):
        key = ("pair", tex, pal)
        if key not in self.by:
            self.by[key] = WMP_FILE0 + len(self.data); self.data += [pal, tex]
        q = self.by[key]
        return q + 1, q

# Their Titantron videos: WM2000's entrance table (its Superstar Select's RAM): video v's (frame file, time)
# list is the word at 0x80102070 + 12 v, as No Mercy's at 0x800F50CC + 12 v; frames 64 x 64 CI8 TEX files.
# Ours are videos VIDEO_PORT0 on (media.c: past the game's 57), each frame named by the frame k of its
# video, shown with No Mercy's carrier file k (tv_carriers.h) swapped for it.
WM_ENTR_RAM = nmp("/home/devbox/work/gen_data/wm2k_entr_ram.bin")
VIDEO_PORT0, TV_CARRIERS, FRAME_SIZE = 58, 72, 4624

def wm_video(v):
    """WM2000's video v: [(file, time)] and the end's time (the entry to go back to)"""
    ram = open(WM_ENTR_RAM, "rb").read()
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    r16 = lambda a: struct.unpack_from(">H", ram, a & 0x7FFFFF)[0]
    a = r32(0x80102070 + 12 * v); out = []
    while r16(a) != 0xFFFF:
        out.append((r16(a), r16(a + 2))); a += 4
        assert len(out) < 256
    return out, r16(a + 2)

# ---- WCW/nWo Revenge's (/home/devbox/work/revport): the same engine before WM2000, its models made the
# same way (meshes and pictures in the same formats; WM2000's joins: its 8000BDD0), so a model of it is
# a WM2000-layout descriptor with SKEL_WM_REV in its flags (skel.c: Revenge's few differences). A model:
# parts 1-19 (the body: its attire's), 4 and 5 (the head, its mask's) and 20 (on the head: hair, ...);
# each part's mesh (the file its 8000498C reads), picture and palette (as the match has them: the
# attire's colours applied) from a Revenge match (revport/logcalls.py: its tag match, Bret Hart and Jim
# Neidhart in slots 0 and 1; OVR picks a slot's costume).
SKEL_WM_REV = 0x80000000
REV_FILES = nmp("/tmp/devbox-0/rev_files/%04x.bin")
# Revenge's moves the ported Superstars do as they did there (src/movex.c): name -> (the attacker's animation
# file, the defender's) -- Revenge's list values + 0x110B (revport/revmoves.py). In the pack one after the
# other: No Mercy plays the defender's as the attacker's file + 1.
# (A file's first byte differs between the games for the same motion -- Revenge 0 is No Mercy's 4, 28 its
# 80 ... -- and No Mercy plays one with Revenge's first byte with its limbs stretched: a file brought over gets
# No Mercy's, revport/revmoves.py's table.)
REV_MOVE_ANIMS = {}
REV_ANIMS_JSON = nmp("/home/devbox/work/gen_data/rev_anims.json")   # (gen/revanims.py: the stand-ins' own animations)
REV_CAP = nmp("/home/devbox/work/revport/cap/%s")
REV_PARTS = 0x80073EB0      # 0x3F4 a slot, 0x2C a part: +0x18 picture, +0x1C palette
# (name, costume) -> (capture, its slot). His four costumes are WCW/nWo Revenge's own four: its costume table
# (*(0x8003A594) + 42 x (4 x select index + costume): attire, mask, three colour rows -- what its Superstar Select
# steps with C-left / C-right and its match loads; data\revport\attires\ATTIRES.md), in its order. The captures
# at_<pair><costume> (attires\cap: the Decision screen's slot costumes poked, rv.py; REV_CAP_COSTUME); the ports'
# own (L12, hh_c0, ..., the default costume 0) where they are the same. A look he had before that Revenge doesn't
# give him: gen/revbanks.py (a second ID of his, his costumes past four).
REV_PORTED = {
    # his black and pink, the pink, the pink trunks with his shades, the jacket and jeans with his shades
    "brethart": [("L12", 0), ("at_bn1", 0), ("at_bn2", 0), ("at_bn3", 0)],
    # his singlet in pink, blue, gold and red
    "neidhart": [("L12", 1), ("at_bn1", 1), ("at_bn2", 1), ("at_bn3", 1)],
    # his black Hollywood tights, the same with a second bandana, the yellow Hulkamania trunks with his blond hair, the nWo shirt
    "hogan": [("hh_c0", 0), ("at_hh1", 0), ("at_hh2", 0), ("at_hh3", 0)],
    # his black tank top, the red Wolfpac one, the nWo shirt and jeans, the Wolfpac one in a darker red
    "nash": [("hh_c0", 1), ("at_hh1", 1), ("at_hh2", 1), ("at_hh3", 1)],
    # his trunks, the Outsider shirt, the trunks in green, the shirt with yellow trunks
    "hall": [("ho_c0", 0), ("at_ho1", 0), ("at_ho2", 0), ("at_ho3", 0)],
    # his red tights and bandana, the MADNESS shirt, the orange tiger tights, the MADNESS shirt with his shades
    "savage": [("sv_c0", 0), ("at_sv1", 0), ("at_sv2", 0), ("at_sv3", 0)],
    # his singlet in blue, orange, green and white
    "hennig": [("sv_c0", 1), ("at_sv1", 1), ("at_sv2", 1), ("at_sv3", 1)],
    # his black singlet, the nWo shirt and jeans, the red singlet, the purple singlet
    "giant": [("gi_c0", 0), ("at_gi1", 0), ("at_gi2", 0), ("at_gi3", 0)],
    # his Wolfpac black and red with the red face paint, the same in black and white with the Crow's white paint, the surfer's green tights, the Crow's black coat
    "sting": [("sl_c0", 0), ("at_sl1", 0), ("at_sl2", 0), ("at_sl3", 0)],
    # his black tights, the black trunks, the red trunks, the stars and stripes trunks
    "luger": [("sl_c0", 1), ("at_sl1", 1), ("at_sl2", 1), ("at_sl3", 1)],
    # his trunks in black, purple, gold and grey
    "goldberg": [("gp_c0", 0), ("at_gp1", 0), ("at_gp2", 0), ("at_gp3", 0)],
    # his black singlet, the red singlet, the black jacket, the pink singlet
    "ricksteiner": [("st_c0", 0), ("at_st1", 0), ("at_st2", 0), ("at_st3", 0)],
    # his white singlet with his shades, the same without, the red singlet with his dark hair, the black singlet
    "scottsteiner": [("st_c0", 1), ("at_st1", 1), ("at_st2", 1), ("at_st3", 1)],
    # his red Harlem Heat singlet, the black flames tights, the gold singlet, the purple flames tights
    "booker": [("hz_c0", 0), ("at_hz1", 0), ("at_hz2", 0), ("at_hz3", 0)],
    # his red Harlem Heat singlet, the nWo shirt and jeans, the gold singlet, the purple flames tights
    "stevieray": [("hz_c0", 1), ("at_hz1", 1), ("at_hz2", 1), ("at_hz3", 1)],
    # his blue and red pants and mask, the black and white, the orange, the purple bodysuit and its mask
    "rey": [("rb_c0", 0), ("at_rb1", 0), ("at_rb2", 0), ("at_rb3", 0)],
    # his trunks in blue, red and orange, the jacket
    "piper": [("gp_c0", 1), ("at_gp1", 1), ("at_gp2", 1), ("at_gp3", 1)],
    # his black suit, the nWo shirt and jeans, the grey suit, the jacket
    "bischoff": [("rb_c0", 1), ("at_rb1", 1), ("at_rb2", 1), ("at_rb3", 1)]}
import revbanks      # (the second IDs: their costumes, gen/revbanks.py)
for _b in revbanks.BANKS: REV_PORTED[_b["key"]] = _b["caps"]
REV_CAPTURES = {"L12": "", "bret_c1": "0:-1:1",          # capture -> OVR (from cap/n11: A, 1500 frames)
                "hh_c0": "", "hh_c1": "0:20:0,1:26:0", "hh_c2": "0:21:1", "hh_c3": "0:19:2", "ho_c0": "", "ho_c1": "0:28:0",
                "sv_c0": "", "sv_c1": "0:23:1", "sv_c2": "0:24:3", "sv_c3": "0:22:0", "hn_c0": "",
                "gi_c0": "", "gi_c1": "0:1:0", "gi_c2": "0:2:0", "sl_c0": "", "sl_c1": "0:2:0,1:3:0", "sl_c2": "0:1:2,1:4:0", "sl_c3": "0:2:3", "gp_c0": "", "st_c0": "", "st_c1": "0:1:0,1:5:0", "st_c2": "0:2:0,1:7:0", "hz_c0": "", "hz_c1": "0:13:0,1:3:0", "rb_c0": "", "rb_c1": "0:4:0"}
REV_CAP_START = {"hh_c0": "h11", "hh_c1": "h11", "hh_c2": "h11", "hh_c3": "h11", "ho_c0": "o11", "ho_c1": "o11",
                 "sv_c0": "s11", "sv_c1": "s11", "sv_c2": "s11", "sv_c3": "s11", "hn_c0": "s12",
                 "gi_c0": "g11", "gi_c1": "g11", "gi_c2": "g11", "sl_c0": "p_sl", "sl_c1": "p_sl", "sl_c2": "p_sl", "sl_c3": "p_sl", "gp_c0": "p_gp", "st_c0": "p_st", "st_c1": "p_st", "st_c2": "p_st", "hz_c0": "p_hh", "hz_c1": "p_hh", "rb_c0": "p_rb", "rb_c1": "p_rb"}   # (else n11)
REV_CAP_PORTS = nmp("/home/devbox/work/revport/ports/cap/%s")   # (where the three's captures were made)
REV_CAP_PORTS2 = nmp("/home/devbox/work/revport/ports2/cap/%s")  # (... and Savage's and Hennig's)
REV_CAP_PORTS3 = nmp("/home/devbox/work/revport/ports3/cap/%s")  # (... and The Giant's)
REV_CAP_PORTS4 = nmp("/home/devbox/work/revport/ports4/cap/%s")  # (... and ports4's ten: Sting .. Bischoff)
REV_CAP_ATTIRES = nmp("/home/devbox/work/revport/attires/cap/%s")  # (... and every port's costumes 1-3)
# the attires captures: tag -> (start state, costume): made again by attires/rv.py, the Decision screen's slot costume
# (0x8007EF80 + 0x10 slot, byte; its copy 0x8007E930 + 4 slot) poked for both slots (attires/cap_att.sh)
REV_CAP_COSTUME = {"at_bn1": ("n11", 1), "at_bn2": ("n11", 2), "at_bn3": ("n11", 3), "at_gi1": ("g11", 1), "at_gi2": ("g11", 2), "at_gi3": ("g11", 3), "at_gp1": ("p_gp", 1), "at_gp2": ("p_gp", 2), "at_gp3": ("p_gp", 3), "at_hh1": ("h11", 1), "at_hh2": ("h11", 2), "at_hh3": ("h11", 3), "at_ho1": ("o11", 1), "at_ho2": ("o11", 2), "at_ho3": ("o11", 3), "at_hz1": ("p_hh", 1), "at_hz2": ("p_hh", 2), "at_hz3": ("p_hh", 3), "at_rb1": ("p_rb", 1), "at_rb2": ("p_rb", 2), "at_rb3": ("p_rb", 3), "at_sl1": ("p_sl", 1), "at_sl2": ("p_sl", 2), "at_sl3": ("p_sl", 3), "at_st1": ("p_st", 1), "at_st2": ("p_st", 2), "at_st3": ("p_st", 3), "at_sv1": ("s11", 1), "at_sv2": ("s11", 2), "at_sv3": ("s11", 3)}

def rev_ensure(tag):
    import subprocess
    d = REV_CAP % tag
    if not os.path.exists(d + "_ram.bin") and os.path.exists(REV_CAP_PORTS % tag + "_ram.bin"): d = REV_CAP_PORTS % tag
    if not os.path.exists(d + "_ram.bin") and os.path.exists(REV_CAP_PORTS2 % tag + "_ram.bin"): d = REV_CAP_PORTS2 % tag
    if not os.path.exists(d + "_ram.bin") and os.path.exists(REV_CAP_PORTS3 % tag + "_ram.bin"): d = REV_CAP_PORTS3 % tag
    if not os.path.exists(d + "_ram.bin") and os.path.exists(REV_CAP_PORTS4 % tag + "_ram.bin"): d = REV_CAP_PORTS4 % tag
    if not os.path.exists(d + "_ram.bin") and os.path.exists(REV_CAP_ATTIRES % tag + "_ram.bin"): d = REV_CAP_ATTIRES % tag
    if not os.path.exists(d + "_ram.bin") and tag in REV_CAP_COSTUME:   # (attires: its costume poked)
        st, c = REV_CAP_COSTUME[tag]
        subprocess.check_call([_sys.executable, "rv.py", "p:" + st] + ["poke8:%x:%d" % (0x8007EF80 + 0x10 * k, c) for k in (0, 1)] +
                              ["poke:%x:%d" % (0x8007E930 + 4 * k, c) for k in (0, 1)] + ["A", "w1500", "w30", "save:" + tag],
                              cwd=nmp("/home/devbox/work/revport/attires"), env=dict(os.environ, HOOK3="1"))
        d = REV_CAP_ATTIRES % tag
    if not os.path.exists(d + "_ram.bin"):     # (made again: the start state must be in revport/cap -- h11 / o11 are in ports/cap)
        env = dict(os.environ, OVR=REV_CAPTURES[tag]) if REV_CAPTURES[tag] else dict(os.environ)
        subprocess.check_call([_sys.executable, "logcalls.py", REV_CAP_START.get(tag, "n11"), tag, "A", "w1500"], cwd=nmp("/home/devbox/work/revport"), env=env)
    import pickle
    return open(d + "_ram.bin", "rb").read(), pickle.load(open(d + "_calls.pkl", "rb"))

def ci8(tex, pal):
    """a CI4 picture as CI8 (WM2000's hands are: hands.c keeps them as its own)"""
    if tex[2] == 1: return tex, pal       # (8 bits a pixel already -- tex_size's rule: a 4-bit picture may have 32
                                          #  colours, two rows of 16: Booker T's, Stevie Ray's, Scott Steiner's hands, ports4)
    w, h = tex[0] + 1, tex[1] + 1
    px = bytearray()
    for b in tex[8:8 + w * h // 2]: px += bytes((b >> 4, b & 15))
    return tex[:2] + b"\x01\x00" + tex[4:8] + bytes(px), pal[:512] + bytes(512 - len(pal[:512]))   # (its first row indexed)

def mirror(tex):
    """a CI8 picture flipped left to right (Revenge's palm: WM2000's the other way round, and its hands'
    meshes are WM2000's: hands.c)"""
    w, h = tex[0] + 1, tex[1] + 1
    px = tex[8:]
    return tex[:8] + b"".join(px[y * w:(y + 1) * w][::-1] for y in range(h))

def _colors(pal):
    out = []
    for i in range(len(pal) // 2):
        v = struct.unpack_from(">H", pal, 2 * i)[0]
        out.append(((v >> 11) << 3, ((v >> 6) & 31) << 3, ((v >> 1) & 31) << 3))
    return out
def _lum(c): return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
def rev_fingers(wm_tex, wm_pal, palm_tex, palm_pal):
    """a Revenge model's finger picture as WrestleMania 2000's (hands.c draws its fingers with WM2000's
    meshes, whose texture coordinates are laid out for WM2000's finger pictures: Revenge's own, its red
    creases across where WM2000's knuckles are, came out as orange-and-red striped fists): WM2000's picture
    (CI8), its colours replaced by the model's own skin, light for light -- each WM2000 colour by the palm's
    colour at the same place in the order of brightness (pixel for pixel)"""
    w, h = wm_tex[0] + 1, wm_tex[1] + 1
    wpx = wm_tex[8:8 + w * h]
    wc = _colors(wm_pal); pc = _colors(palm_pal)
    pw, ph = palm_tex[0] + 1, palm_tex[1] + 1
    ppx = palm_tex[8:8 + pw * ph]
    palm = sorted((_lum(pc[i]), pc[i]) for i in ppx)
    order = sorted(set(wpx), key=lambda i: _lum(wc[i]))
    count = {i: 0 for i in order}
    for i in wpx: count[i] += 1
    newpal = bytearray(wm_pal)
    seen = 0
    for i in order:
        mid = (seen + count[i] / 2) / len(wpx); seen += count[i]
        r, g, b = palm[min(len(palm) - 1, int(mid * len(palm)))][1]
        struct.pack_into(">H", newpal, 2 * i, ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1)
    return wm_tex, bytes(newpal)

# ---- No Mercy's hands and skin on them (the user's call, 0.35.12) -----------------------------------------
# Each ported model's hands (parts 9 10 17 18) are No Mercy's own: its hand meshes, drawn by hands.c No
# Mercy's way (SKEL_WM_NMHANDS), with its hand picture in the No Mercy skin tone nearest the model's skin.
# No Mercy keeps one set of skin pictures and picks the tone by a palette row (its descriptor's byte 0x255:
# 8 tones); here the row is copied to the top of the palette (WrestleMania 2000's pictures are whole). The
# model's other parts that are bare skin (not tights, clothes, face, hair) get No Mercy's picture of the
# same part in that tone when it is the same size; the parts that are partly skin (a strap, a band on an
# arm) keep their own picture with its skin colours brought to the tone.
NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
def nmf(i): return open(NMF % i, "rb").read()
NM_HAND_MESH = {9: 0x1112, 10: 0x1113, 17: 0x1119, 18: 0x111A}
NM_HAND_PIC = (0x296E, 0x296D)
NM_SKIN_PIC = {1: (0x2962, 0x2961), 2: (0x2960, 0x295F), 3: (0x295E, 0x295D), 6: (0x2972, 0x2971),
               7: (0x2970, 0x296F), 8: (0x2974, 0x2973), 11: (0x296C, 0x296B), 12: (0x296A, 0x2969),
               13: (0x2972, 0x2971), 14: (0x2970, 0x296F), 15: (0x2974, 0x2973), 16: (0x296C, 0x296B),
               19: (0x296A, 0x2969)}      # (No Mercy's descriptor of a bare-chested costume: gen/nmhands notes)
SKIN_KEEP = (4, 5, 20)                    # the head: its own
REV_SKIN = ("goldberg", "nash")           # (the user, 2026-11-03, a try: Revenge's own skin and hands, all costumes; no nm_skin. no2k15: the 2K15 slots gone)
def _rgb(v): return ((v >> 11) << 3, ((v >> 6) & 31) << 3, ((v >> 1) & 31) << 3)
def _pix(tex):
    """a picture's pixels as palette indices"""
    w, h = tex[0] + 1, tex[1] + 1
    if tex[2] == 1: return list(tex[8:8 + w * h])
    out = []
    for b in tex[8:8 + w * h // 2]: out += [b >> 4, b & 15]
    return out
def _skin(c):
    import colorsys
    r, g, b = c
    hh, ss, vv = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return (hh < 50 / 360 or hh > 345 / 360) and 0.15 <= ss <= 0.78 and vv >= 0.22 and r > b + 25
def _bake(pal, row):
    b = bytearray(pal); b[0:0x20] = pal[0x20 * row:0x20 * row + 0x20]; return bytes(b)
def _tone_avg(row):
    """No Mercy's skin in palette row `row`: the arms' and hands' pictures' mean colour"""
    acc = [0, 0, 0]; n = 0
    for t, p in ((0x296C, 0x296B), (0x296A, 0x2969), (0x296E, 0x296D)):
        tex, pal = nmf(t), _bake(nmf(p), row)
        cs = [_rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(16)]
        for i in _pix(tex): acc = [a + c for a, c in zip(acc, cs[i])]; n += 1
    return tuple(a / n for a in acc)
def _opaque(tex, pal):
    """every pixel of a picture opaque (its colour's 5551 alpha bit set)"""
    n = len(pal) // 2
    return all(i < n and struct.unpack_from(">H", pal, 2 * i)[0] & 1 for i in _pix(tex))
def nm_skin(model, F, name, report, skins=None, who=None):
    """a ported model (its descriptor, files in F) with No Mercy's hands and skin (skins: who -> the skin tone seen
    on his costumes so far; a costume that shows no bare skin -- Sting's Crow coat, ports4 -- takes his other one's)"""
    d = bytearray(model)
    fid = {v: k for k, v in F.by.items() if isinstance(v, int)}   # (our ID -> bytes)
    def data(i): return F.data[i - WMP_FILE0] if i >= WMP_FILE0 else nmf(i)
    parts = {}
    for k in range(1, 20):
        m, t, q = struct.unpack_from(">HHH", d, 8 * k)
        if not t or k in SKIN_KEEP: continue
        tex, pal = data(t), data(q)
        cs = [_rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(len(pal) // 2)]
        px = _pix(tex)
        sk = [i for i in px if i < len(cs) and _skin(cs[i])]
        parts[k] = dict(tex=tex, pal=pal, cs=cs, frac=len(sk) / max(1, len(px)),
                        avg=tuple(sum(cs[i][j] for i in sk) / max(1, len(sk)) for j in range(3)), n=len(sk))
    full = [k for k, p in parts.items() if p["frac"] >= 0.95 and k not in NM_HAND_MESH]
    src = full or [k for k, p in parts.items() if p["frac"] >= 0.5]
    # (attires: a costume's own colours can pass for skin -- a gold or red singlet, green tights -- and gave that costume
    #  another tone than his others: his tone from the parts bare in his costume 0 -- his arms -- where this one has them)
    #  (none of those bare in this one -- a shirt, a bodysuit, a jacket; or none bare in his costume 0 at all: his costume
    #  0's tone; a second ID, gen/revbanks.py, is his base's: build_all)
    keep = None
    if skins is not None and who in REV_PORTED:
        if (who, "bare") not in skins: skins[(who, "bare")] = set(full)
        else:
            bare = [k for k in skins[(who, "bare")] if k in parts and parts[k]["frac"] >= 0.95]
            if bare: src = bare
            elif who in skins: keep = skins[who]
    tot = sum(parts[k]["n"] for k in src)
    if keep is not None:
        have = keep; report.append("%s: none of his costume 0's bare parts bare -- his costume 0's tone" % name)
    elif tot:
        have = tuple(sum(parts[k]["avg"][j] * parts[k]["n"] for k in src) / tot for j in range(3))
        if skins is not None and who not in skins: skins[who] = have
    elif skins is not None and who in skins:
        have = skins[who]; report.append("%s: no bare skin in this costume -- his other costume's tone" % name)
    else:
        raise SystemExit("%s: no bare skin in any costume so far (its skin tone unknown)" % name)
    tones = [_tone_avg(r) for r in range(8)]
    row = min(range(8), key=lambda r: sum((a - b) ** 2 for a, b in zip(tones[r], have)))
    tone = tones[row]
    gain = [tone[j] / max(1.0, have[j]) for j in range(3)]
    for k, mesh in NM_HAND_MESH.items():                 # the hands
        t, q = F.pair(nmf(NM_HAND_PIC[0]), _bake(nmf(NM_HAND_PIC[1]), row))
        struct.pack_into(">HHH", d, 8 * k, mesh, t, q)
    done = []
    for k, p in parts.items():
        if k in NM_HAND_MESH or p["frac"] < 0.3: continue        # (under that: other colours that look like skin)
        m = struct.unpack_from(">H", d, 8 * k)[0]
        nt = NM_SKIN_PIC.get(k)
        # bare skin, a picture the same size -- and opaque all over: No Mercy's thigh picture (0x2970) has a 12 x 13
        # transparent corner its own mesh never samples, and a ported thigh's UVs (laid out for its own picture) do:
        # The Giant's singlet costumes had a hole in the top of each thigh (the user, 0.38.12). Such a part keeps its
        # own picture, recoloured to No Mercy's skin as the part-skin ones are.
        if p["frac"] >= 0.95 and nt and nmf(nt[0])[:2] == p["tex"][:2] and _opaque(nmf(nt[0]), _bake(nmf(nt[1]), row)):
            t, q = F.pair(nmf(nt[0]), _bake(nmf(nt[1]), row)); done.append((k, "No Mercy's"))
        else:                                                              # part skin: its skin recoloured
            pal = bytearray(p["pal"])
            for i, c in enumerate(p["cs"]):
                if not _skin(c): continue
                v = struct.unpack_from(">H", pal, 2 * i)[0]
                r, g, b = [min(255, int(c[j] * gain[j])) for j in range(3)]
                struct.pack_into(">H", pal, 2 * i, ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (v & 1))
            t, q = F.pair(p["tex"], bytes(pal)); done.append((k, "recoloured %.0f%%" % (100 * p["frac"])))
        struct.pack_into(">HHH", d, 8 * k, m, t, q)
    struct.pack_into(">I", d, SKEL_WM_FLAGS, struct.unpack_from(">I", d, SKEL_WM_FLAGS)[0] | SKEL_WM_NMHANDS)
    report.append("%s: skin %s -> No Mercy's tone %d %s; %s" % (name, tuple(int(x) for x in have), row,
                  tuple(int(x) for x in tone), done))
    return bytes(d)
SKEL_WM_NMHANDS = 0x40000000

def rev_capture(ram, calls, slot):
    """a Revenge model in a slot: {k: (mesh file, picture, palette)}"""
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    def rb(a, n): a &= 0x7FFFFF; return bytes(ram[a:a + n])
    # The match's load is the log's last pass (each starts with part 1). An earlier pass's part it doesn't load isn't his: a masked head (Rey's: 4 and 5 the
    # whole head, no 20) would keep the part 20 of the head the slot held before (Sting's hair, 0xA63, the user 2026-11-03; attires/ATTIRES.md).
    mesh = {}
    for tag, a0, a1, a2 in calls:
        if tag != 2: continue
        o = a0 - (REV_PARTS + slot * 0x3F4)
        if 0 <= o < 0x3F4 and o % 0x2C == 0:
            if o // 0x2C == 1: mesh = {}
            mesh[o // 0x2C] = a1 & 0xFFFF
    out = {}
    for k in range(1, 21):
        if k not in mesh: continue
        ps = REV_PARTS + slot * 0x3F4 + k * 0x2C
        ta, pa = r32(ps + 0x18), r32(ps + 0x1C)
        tex = rb(ta, tex_size(rb(ta, 8))) if ta else None
        pal = rb(pa, pal_size(tex[:8])) if tex and pa else None
        out[k] = (mesh[k], tex, pal)
    return out

# Revenge has no Titantron videos. Bret Hart's and Jim Neidhart's were once made of their own entrances in it (emulator
# screenshots: a frame every quarter second of each walking to the ring, 72 each); the user (2026-11-18) REMOVED them: like
# any wrestler without one, they have no Titantron unless the player puts one in their folder (the folder-driven Titantron,
# media.c). Their 144 frame files stay in the pack as EMPTY files, so no other file's number moves (the headers hold the
# numbers); nothing reads them. (A turntable render of each, REV_TRON_RENDERED, was tried and retired the same day.)
REV_TRON_RETIRED = 144
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "romrender"))
def nm_files_reader():
    import rr_model
    return rr_model.nm_file_reader(os.path.dirname(nmp("/tmp/devbox-0/nmfiles/f/0000.bin")))

def rev_port_desc(name, c, F, wm_ref):
    """a Revenge port's descriptor, costume c (REV_PORTED's capture), its files into F (build_all's loop, and the scratch
    pack tools/romrender/mkportphotos.py renders his select photo from)"""
    caps = REV_PORTED[name]
    tag, slot = caps[c % len(caps)]
    ram, calls = rev_ensure(tag)
    nm = bytearray(0x270)
    got = rev_capture(ram, calls, slot)
    for k, (m, tex, pal) in got.items():
        if k in (9, 10, 17, 18) and tex:                  # (its hands: WM2000's, hands.c)
            tex, pal = ci8(tex, pal)
            if k in (9, 17): tex = mirror(tex)
            if k in (10, 18):                             # (the fingers: WM2000's, in his skin)
                palm_t, palm_p = ci8(got[k - 1][1], got[k - 1][2])
                tex, pal = rev_fingers(wm_ref[k][0], wm_ref[k][1], palm_t, palm_p)
        t, q = F.pair(tex, pal) if tex else (0, 0)
        struct.pack_into(">HHH", nm, 8 * k, F.one(open(REV_FILES % m, "rb").read()), t, q)
    struct.pack_into(">I", nm, SKEL_WM_FLAGS, SKEL_WM_REV)
    nm[SKEL_WM_HANDS] = 0
    struct.pack_into(">I", nm, SKEL_MAGIC_AT, SKEL_MAGIC_WM)
    return bytes(nm)

def build_all():
    import wm2k
    F = Files(); models = {}; videos = []; video_of = {}
    for name, k in PORTED:
        for c in range(4):
            ram = ensure_capture(k, c)
            wd, parts = capture(ram, 0)
            nm = bytearray(nm_descriptor(wd))
            for o in list(range(8, 0xB0, 8)) + list(range(0xC8, 0xE0, 8)): nm[o:o + 6] = bytes(6)
            for p in parts:
                o = p["at"] if p["k"] <= 20 else 0xC8 + 8 * (p["k"] - 22)
                m = F.one(wfile(p["mesh"]))
                t, q = F.pair(p["tex"], p["pal"]) if p["tex"] else (0, 0)
                struct.pack_into(">HHH", nm, o, m, t, q)
            kinds = open(nmp("/home/devbox/work/wmport/wm/match_ram.bin"), "rb").read()[0x41CC4:0x41CC4 + 0x40]
            nm[SKEL_WM_HANDS] = kinds[looks0(k, c)]
            models[(name, c)] = bytes(nm)
    # WM2000's hands (src/hands.c): its pose meshes, hand pictures, pose 0x13's pictures and mark, from
    # its tables (in its RAM: 0x80041C5C, 0x80041CAC, 0x80041CB4, 0x80041CBC; 800173AC's 0x122E/0x122D)
    wram = open(nmp("/home/devbox/work/wmport/wm/match_ram.bin"), "rb").read()
    r32 = lambda a: struct.unpack_from(">I", wram, a & 0x7FFFFF)[0]
    r16 = lambda a: struct.unpack_from(">H", wram, a & 0x7FFFFF)[0]
    odd = [[F.one(wfile(r16(r32(0x80041C5C + 4 * sd) + 2 * p))) for p in range(11)] for sd in range(2)]
    even = [[[F.one(wfile(r16(r32(r32(0x80041CAC + 4 * sd) + 4 * t) + 2 * p))) for p in range(4)] for t in range(3)] for sd in range(2)]
    def tp(i): return F.pair(wfile(i + 1), wfile(i))
    pics = [tp(r16(0x80041CB4 + 2 * i)) for i in range(4)]
    special = [tp(r16(0x80041CBC + 2 * i)) for i in range(4)]
    decal = F.pair(wfile(0x122E), wfile(0x122D))
    for name, k in PORTED:      # their videos
        wv = wm2k.roster()[k]["video"]
        if wv:
            lst, back = wm_video(wv)
            frames = []
            for f, _ in lst:
                if f not in frames: frames.append(f)
            assert len(frames) <= TV_CARRIERS, (name, len(frames))
            ids = []
            for f in frames:
                d = wfile(f); assert len(d) == FRAME_SIZE and d[:3] == b"TEX", (name, f)
                ids.append(F.one(d))
            video_of[name] = VIDEO_PORT0 + len(videos)
            videos.append((name, wv, [(frames.index(f), t) for f, t in lst], back, ids))
    wm_ref = {p["k"]: (p["tex"], p["pal"]) for p in capture(ensure_capture(PORTED[0][1], 0), 0)[1]}   # (WM2000's fingers: Jarrett's)
    F.data += [b""] * REV_TRON_RETIRED          # (Bret's / Neidhart's retired Titantron frames: empty, above)
    for name, caps in REV_PORTED.items():     # Revenge's
        for c in range(4):
            models[(name, c)] = rev_port_desc(name, c, F, wm_ref)
    # (no2k15, 2026-11-23) The two WWE 2K15 test slots' stand-in models (Goldberg's parts, No Mercy's hands: src/hd.c drew their
    # full-detail models over them) are removed with the slots. The one pair of files they were the first to make -- No Mercy's
    # hand picture, its palette baked (3) -- stays at its rows: the Revenge ports' No Mercy hands (nm_skin, below) use it, and so
    # every file after it keeps its number (wmport.bin and wmport_files.h as before; exe src/romimport/ri_wmport.cpp the same).
    F.pair(nmf(NM_HAND_PIC[0]), _bake(nmf(NM_HAND_PIC[1]), 3))
    report = []
    if not os.environ.get("NO_NMSKIN"):
        skins = {}
        for key in list(models):
            if key[0] in REV_SKIN: continue
            models[key] = nm_skin(models[key], F, "%s %d" % key, report, skins, revbanks.BY[key[0]]["base"] if key[0] in revbanks.BY else key[0])
        print("\n".join(report))
    rev_anims = {}
    pairs = [(name, fa, fd, None, None) for name, (fa, fd) in REV_MOVE_ANIMS.items()]
    if os.path.exists(REV_ANIMS_JSON):
        import json
        pairs += [(f["name"], f["fa"], f["fd"], f["b0a"], f["b0d"]) if f.get("src") not in ("nm", "join") else
                  (f["name"], f["pa"], f["pd"], None, None) if f["src"] == "join" else
                  (f["name"], -f["fa"], -f["fd"], None, None) for f in json.load(open(REV_ANIMS_JSON))["files"]]
    for name, fa, fd, b0a, b0d in pairs:     # (no sharing: the pair must stay together; No Mercy's first byte)
        rev_anims[name] = WMP_FILE0 + len(F.data)
        # (2026-11-13, movelib2: these are the built-in move library's now -- tools\handlers\revlib.py writes
        #  them, one copy each, to content\movelib_revenge.bin, and the exe gives them numbers while a match plays them -- so
        #  the pack keeps their numbers, every later file's the same, with no bytes)
        if not os.environ.get("WMPORT_REV_ANIMS"):
            F.data += [b"", b""]
            continue
        if isinstance(fa, str):               # (gen/revanims.py's joined files)
            F.data += [open(nmp(fa), "rb").read(), open(nmp(fd), "rb").read()]   # (nmp: the json may name sandbox paths)
            continue
        src = REV_FILES if fa > 0 else nmp("/tmp/devbox-0/nmfiles/f/%04x.bin"); fa, fd = abs(fa), abs(fd)   # (tests: a pair of No Mercy's own)
        da, dd = bytearray(open(src % fa, "rb").read()), bytearray(open(src % fd, "rb").read())
        if b0a is not None: da[0] = b0a
        if b0d is not None: dd[0] = b0d
        F.data += [bytes(da), bytes(dd)]
    import arenas     # WCW/nWo Revenge's arenas (src/arena.c): their files in this pack, src/arena_files.h
    arows = arenas.build(F, WMP_FILE0)
    print("arenas: %s" % ", ".join("%s %d pieces" % (r[0], len(r[2])) for r in arows))
    import arenas_entrance   # ... and in the entrances' close shots (src/arena_entrance.c), the same pictures
    print("arena close-ups: %s" % ", ".join("%s %d pieces" % (r[0], len(r[2])) for r in arenas_entrance.build(F, arows)))
    import arenas_raw        # RAW IS WAR's match stage set made its entrances' close set (src/arena.c arena_raw_tables)
    print("RAW IS WAR's stage set: %d parts" % len(arenas_raw.build(F)))
    import arenas_stages     # ... and No Mercy's other arenas' (src/arena.c arena_stage_tables)
    print("No Mercy's arenas' stage sets: %s" % ", ".join("%d: %d parts" % (v, len(r)) for v, r in sorted(arenas_stages.build(F).items())))
    print("palettes made No Mercy's size: %d" % nm_palettes(F.data, models))
    blob = bytearray(); offs = []
    for b in F.data:
        offs.append((len(blob), len(b))); blob += b + bytes(-len(b) % 8)
    open(OUT_BIN, "wb").write(blob)
    L = ["// generated by gen/wmport.py: WrestleMania 2000's Superstars' files (build/wmport.bin, in the mod)",
         "#define WMP_FILE0 0x%Xu" % WMP_FILE0, "#define WMP_FILES_N %d" % len(F.data),
         "#define WMP_PACK_SIZE 0x%Xu" % len(blob),
         "static const u32 wmp_files[WMP_FILES_N][2] = {   // offset in the pack, size; file WMP_FILE0 + i"]
    L += ["    { 0x%06X, %d }," % o for o in offs]
    L.append("};")
    open(OUT_H.replace("wmport_files.h", "rev_move_anims.h"), "w").write(
        "// generated by gen/wmport.py: Revenge's moves' animations (movex.c): the attacker's file (the defender's the next)\n"
        + "#define RMA_FIRST 0x%Xu   // (the pairs one after another from here, gen/revanims.py's order)\n" % (min(rev_anims.values()) if rev_anims else 0)
        + "#define RMA_FILES_N %d\n" % (2 * len(rev_anims))
        + "".join("#define RMA_%s 0x%Xu\n" % (name, f) for name, f in rev_anims.items()))
    L.append("// WrestleMania 2000's hands (hands.c): pose meshes [side][pose], [side][kind][pose]; pictures (tex, pal)")
    L.append("static const u16 wmh_odd_files[2][11] = { %s };" % ", ".join("{ %s }" % ", ".join("0x%X" % x for x in r) for r in odd))
    L.append("static const u16 wmh_even_files[2][3][4] = { %s };" % ", ".join("{ %s }" % ", ".join("{ %s }" % ", ".join("0x%X" % x for x in t) for t in r) for r in even))
    L.append("static const u16 wmh_pic_files[4][2] = { %s };" % ", ".join("{ 0x%X, 0x%X }" % x for x in pics))
    L.append("static const u16 wmh_special_files[4][2] = { %s };" % ", ".join("{ 0x%X, 0x%X }" % x for x in special))
    L.append("static const u16 wmh_decal_files[2] = { 0x%X, 0x%X };" % decal)
    # (handsrom 2026-11-25) WrestleMania 2000's own table of its hands' poses -- the pose mesh each hand piece takes in each of
    # the 20 poses: rows at its RAM's 0x80041D0C, 0x80041CE4, 0x80041CF8, 0x80041CD0 (ROM 0x4290C, 0x428E4, 0x428F8, 0x428D0;
    # wm2k.RAM, the image in its attract demo) -- it was written in hands.c (wm_pose); now made here with its hands' other tables
    L.append("// WrestleMania 2000's hand poses (hands.c): the pose mesh of hand piece [i] in pose [p] (its RAM 0x80041D0C, 0x80041CE4, 0x80041CF8, 0x80041CD0)")
    L.append("static const s8 wmh_pose[4][20] = { %s };" % ", ".join(
        "{ %s }" % ", ".join("%d" % struct.unpack_from(">b", wm2k.RAM, a + k)[0] for k in range(20)) for a in (0x41D0C, 0x41CE4, 0x41CF8, 0x41CD0)))
    # the videos (media.c)
    vlen = max(len(v[2]) for v in videos) + 1; vfr = max(len(v[4]) for v in videos)
    L.append("// Their Titantron videos (media.c): video WMP_VIDEO0 + i: (frame k, time) ..., (0xFFFF, the entry to go back")
    L.append("// to); frame k of it the file wmp_video_frames[i][k] (64 x 64 CI8 TEX)")
    L.append("#define WMP_VIDEO0 %d" % VIDEO_PORT0)
    L.append("#define WMP_VIDEOS_N %d" % len(videos))
    L.append("#define WMP_VIDEO_LEN %d" % vlen)
    L.append("#define WMP_VIDEO_FRAMES %d" % vfr)
    L.append("static const u16 wmp_video[WMP_VIDEOS_N][WMP_VIDEO_LEN][2] = {")
    for name, wv, lst, back, ids in videos:
        L.append("    { %s },   // %s (WM2000's video %d)" % (", ".join("{ %d, 0x%X }" % e for e in lst + [(0xFFFF, back)]), name, wv))
    L.append("};")
    L.append("static const u16 wmp_video_frames[WMP_VIDEOS_N][WMP_VIDEO_FRAMES] = {")
    for name, wv, lst, back, ids in videos:
        L.append("    { %s },   // %s" % (", ".join("0x%X" % x for x in ids), name))
    L.append("};")
    open(OUT_H, "w").write("\n".join(L) + "\n")
    import pickle
    pickle.dump(models, open(OUT_PKL, "wb"))
    pickle.dump(video_of, open(OUT_VIDEOS, "wb"))
    print("%d models, %d videos, %d files, %d bytes" % (len(models), len(videos), len(F.data), len(blob)))
    return models

if __name__ == "__main__":
    build_all()
