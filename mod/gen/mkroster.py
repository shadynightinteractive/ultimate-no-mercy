#!/usr/bin/env python3
"""Builds the roster's new Superstars (src/roster_ext.h) from No Mercy Plus V3.

For each: the four costumes' attributes and names, and the moveset (their looks: gen/mkparts.py).
Movesets are unpacked with No Mercy Plus's own code (run on a RAM image of it, harness/mipsemu.py),
its move numbers mapped to the game's by the per-move animation records (files 0x69/0x1F0/0x1F1),
then packed with the game's own code (func_80005EC0) into a key the game keeps in the
roster: what the game's unpacking (func_80005EFC) turns back into the exact moveset (roster.c).

Inputs: gen_data/van_sel_ram.bin (the game at its Superstar Select), gen_data/nm_pick_ram.bin (No
Mercy Plus V3 at its Superstar Select), /home/devbox/nmplus/f (its files), nmport/*.pkl.
Big Show's moveset is made the same way (his NM+ ordinal 5).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, os, struct, pickle, collections
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from mipsemu import Emu
W = nmp("/home/devbox/work/")
OUT = W + "arcade/src/roster_ext.h"

# ---- the new Superstars: (key, No Mercy Plus ordinal, ID here, names, ordinal here) ----------------
# IDs: the group (bits 8-11) is the game's faction (ovl_e func_80132F28): Funaki with Taka (6),
# Gangrel with Edge and Christian (4, The Brood), the Headbangers in the unaffiliated group 3.
# Music and Titantron, without No Mercy Plus's themes and videos (the game's own) / with them. The game
# kept the empty music slots of the cut Gangrel (11) and Headbangers (23); Funaki is Kaientai with Taka
# (32 / 31) in the game. With NM+'s: its videos 46, 48, 55 (in the pack); Funaki's theme is NM+'s slot
# 87 (beyond the game's 58), played from slot 46 (an empty slot of the game: song 68), media.c.
EXT = [
    dict(key="funaki",   nm=65, id=0x060A, name="FUNAKI",   short="FUNAKI",   nick="", music=(32, 46), video=(31, 55)),
    dict(key="gangrel",  nm=76, id=0x0408, name="GANGREL",  short="GANGREL",  nick="", music=(11, 11), video=(0, 46)),
    dict(key="mosh",     nm=77, id=0x0309, name="MOSH",     short="MOSH",     nick="", music=(23, 23), video=(0, 48)),
    dict(key="thrasher", nm=78, id=0x030A, name="THRASHER", short="THRASHER", nick="", music=(23, 23), video=(0, 48)),
]
# Jeff Jarrett (from WrestleMania 2000, gen/wm2k.py): after Benoit (ext EXT_N), ext EXT_N + 1. His ID's
# group 3 (unaffiliated, as WM2000 has him); music slot 14 (no Superstar of the game's), which plays
# WM2000's theme for him while he's on (gen/mkcontent.py, media.c); his video WM2000's (gen/wmport.py).
import pickle
WM_VIDEO = pickle.load(open(nmp("/home/devbox/work/gen_data/wmport_videos.pkl"), "rb"))   # (gen/wmport.py: media.c)
JARRETT = dict(id=0x030B, music=(14, 14), video=(WM_VIDEO["jarrett"],) * 2)
# After him (ext EXT_N + 2..): Droz and Mideon (No Mercy Plus V3's ordinals 75 / 79 -- No Mercy Plus's alone since
# 2026-11-19, nmplus: their moves were filled from WrestleMania 2000's, its ordinals 31 / 14, whose own Droz and Mideon
# are its imported wrestlers now; no item needs two source games), and The Blue Meanie (WrestleMania 2000's
# ordinal 41). IDs: Droz with Albert (5), Mideon with the Acolytes he stood with in the Ministry (5),
# the Meanie with Al Snow (6, the J.O.B. Squad). Music: slots no Superstar of the game uses (7, 43, 29),
# playing No Mercy Plus's themes for Droz and Mideon (its slots 80 / 73) and WrestleMania 2000's for the
# Meanie (its slot 30) while each is on (gen/mkcontent.py, media.c), with No Mercy Plus's media or
# without, as Big Show's; their No Mercy Plus Titantron videos (45 / 47: none of the game's uses those)
# the same; the Meanie's, WrestleMania 2000's (gen/wmport.py: videos past the game's, media.c), as
# Jarrett's. Heights and weights: No Mercy Plus's / WrestleMania 2000's.
LATE = [
    dict(key="jarrett", name="JEFF JARRETT", short="JARRETT", id=0x030B, music=(14, 14), video=(WM_VIDEO["jarrett"],) * 2, h=13, w=130,
         wm=(29, 0x3F8, 0x3F7), ref=12),
    dict(key="droz", name="DROZ", short="DROZ", id=0x0509, music=(7, 7), video=(45, 45), h=16, w=170,
         nm=75, ref=40),   # (nmplus 2026-11-19: No Mercy Plus's alone -- no more wm=(31, 0x3DA, 0x3D9) fill)
    dict(key="mideon", name="MIDEON", short="MIDEON", id=0x050A, music=(43, 43), video=(47, 47), h=15, w=188,
         nm=79, ref=34),   # (nmplus: no more wm=(14, 0x40A, 0x409) fill)
    dict(key="meanie", name="THE BLUE MEANIE", short="MEANIE", id=0x060B, music=(29, 29), video=(WM_VIDEO["meanie"],) * 2, h=13, w=192,
         wm=(41, 0x40C, 0x40B), ref=26),
    # No Mercy Plus V3's (ordinals 80-84, 86, 89): their looks, photos, movesets, themes and videos. Their
    # music slots: the game's are all taken, so slots made past them (58-62: gen/mkcontent.py EXT_SLOTS,
    # media.c), each No Mercy Plus's for him -- his theme and entrance effects (the Posse's one theme;
    # Tiger Ali Singh's, as No Mercy Plus has it, Lo Down's, D'Lo Brown's and Chaz's team, which he
    # managed). Their videos (49-54) none of the game's Superstars uses. IDs: the Mean Street Posse and
    # Regal in the unaffiliated group 3, the 1999 Undertaker with the Undertaker (0), Raven with Al Snow
    # (6), Tiger Ali Singh with Rikishi and Too Cool (4). References: Road Dogg, X-Pac, Test, the
    # Undertaker, Al Snow, Ken Shamrock, the Godfather.
    dict(key="petegas", name="PETE GAS", short="GAS", id=0x030C, music=(58, 58), video=(50, 50), nm=80, ref=13),
    dict(key="rodney", name="RODNEY", short="RODNEY", id=0x030D, music=(58, 58), video=(50, 50), nm=81, ref=11),
    dict(key="joeyabs", name="JOEY ABS", short="ABS", id=0x030E, music=(58, 58), video=(50, 50), nm=82, ref=21),
    dict(key="taker99", name="UNDERTAKER", short="UNDERTAKER", id=0x000A, music=(59, 59), video=(51, 51), nm=83,
         ref=3),   # (nmplus: no more wm=(6, 0x42C, 0x42B) fill)
    dict(key="raven", name="RAVEN", short="RAVEN", id=0x060C, music=(60, 60), video=(49, 49), nm=84, ref=41),
    dict(key="regal", name="WILLIAM REGAL", short="REGAL", id=0x030F, music=(61, 61), video=(54, 54), nm=86, ref=25),
    dict(key="tiger", name="TIGER ALI SINGH", short="SINGH", id=0x0409, music=(62, 62), video=(52, 52), nm=89, ref=22),
    # From WCW/nWo Revenge (/home/devbox/work/revport, gen/wmport.py): Bret Hart and Jim Neidhart, with the
    # British Bulldog (6: their IDs in his group, each the others' ally). Their movesets Revenge's, in this
    # game's moves (port_rev_moveset: Dean Malenko, Bradshaw the references); their themes Revenge's
    # (gen/mkcontent.py: music slots made, 63 / 64); no Titantron video (Revenge has none: a converted
    # Superstar without a real one of his own has none, not a stand-in -- the user's call); heights and weights Revenge's.
    dict(key="brethart", name="BRET HART", short="HART", id=0x060D, music=(63, 63), video=(0, 0), h=13, w=134,
         rev=0x143, ref=15, allies=(0x060E, 0x0603)),
    dict(key="neidhart", name="JIM NEIDHART", short="NEIDHART", id=0x060E, music=(64, 64), video=(0, 0), h=14, w=173,
         rev=0x1C1, ref=34, allies=(0x060D, 0x0603)),
    # Hollywood Hogan, Kevin Nash and Scott Hall, from WCW/nWo Revenge the same way (revport/ports/PORTS.md: their
    # moveset files 0x17F / 0x1BE / 0x173; The Rock, the Undertaker, Triple H the references). IDs in Triple H's
    # group (1: the Kliq's, with X-Pac), each the others' ally. Music: Revenge has no theme of its own for any
    # Superstar (its first entrant's song, file 0x214, as Bret's and Neidhart's): music slots made for them,
    # 65-67 (gen/mkcontent.py); no Titantron video (the user's rule). Heights and weights Revenge's.
    dict(key="hogan", name="HOLLYWOOD HOGAN", short="HOGAN", id=0x0105, music=(65, 65), video=(0, 0), h=19, w=175,
         rev=0x17F, ref=1, allies=(0x0106, 0x0107)),
    dict(key="nash", name="KEVIN NASH", short="NASH", id=0x0106, music=(66, 66), video=(0, 0), h=25, w=267,
         rev=0x1BE, ref=3, allies=(0x0107, 0x0105, 0x0102)),
    dict(key="hall", name="SCOTT HALL", short="HALL", id=0x0107, music=(67, 67), video=(0, 0), h=19, w=187,
         rev=0x173, ref=10, allies=(0x0106, 0x0105, 0x0102)),
    # Randy Savage and Curt Hennig (2026-10-30, the user): data\revport\ports2\PORTS2.md -- moveset files 0x1EB / 0x17C,
    # references Shawn Michaels (9) and Kurt Angle (19); IDs 0x0108 / 0x0109 (group 1, the nWo's); music slots 68 / 69
    # (Revenge's first-entrant song until their own mp3s: gen/mkcontent.py's user music)
    dict(key="savage", name="RANDY SAVAGE", short="SAVAGE", id=0x0108, music=(68, 68), video=(0, 0), h=13, w=130,
         rev=0x1EB, ref=9, allies=(0x0106, 0x0105, 0x0107)),
    dict(key="hennig", name="MR. PERFECT", short="PERFECT", id=0x0109, music=(69, 69), video=(0, 0), h=15, w=162,
         rev=0x17C, ref=19, allies=(0x0105, 0x0107, 0x0106)),
    # The Giant (2026-10-31, the user: Big Show's alternate on the Superstar Select -- his own Superstar, ID, name, model,
    # moves and music): data\revport\ports3\PORTS3.md -- Revenge's model code 0x0002, moveset file 0x167; 7'4", 540 lbs
    # (a super heavyweight: func_80114CC4's class by the record's weight); Kane (4) the reference (the big men's stance,
    # the chokeslam's man); ID 0x010A (group 1, the nWo's: Hogan, Nash, Hall his allies); music slot 70 (Revenge's song
    # until his own file: mod\music\The Giant.mp3, Redux's -- its "nWo" song)
    dict(key="giant", name="THE GIANT", short="GIANT", id=0x010A, music=(70, 70), video=(0, 0), h=28, w=440,
         rev=0x167, ref=4, allies=(0x0105, 0x0106, 0x0107)),
    dict(key="sting", name="STING", short="STING", id=0x010B, music=(71, 71), video=(0, 0), h=15, w=152,
         rev=0x1F7, ref=1, allies=(0x010C, 0x010D)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="luger", name="LEX LUGER", short="LUGER", id=0x010C, music=(72, 72), video=(0, 0), h=16, w=170,
         rev=0x1A9, ref=21, allies=(0x010B, 0x0106, 0x0108)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="goldberg", name="GOLDBERG", short="GOLDBERG", id=0x010D, music=(73, 73), video=(0, 0), h=15, w=185,
         rev=0x16D, ref=2, allies=(0x010B, 0x010C)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="ricksteiner", name="RICK STEINER", short="R. STEINER", id=0x010E, music=(74, 74), video=(0, 0), h=11, w=148,
         rev=0x1DC, ref=34, allies=(0x010F,)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="scottsteiner", name="SCOTT STEINER", short="S. STEINER", id=0x010F, music=(75, 75), video=(0, 0), h=13, w=135,
         rev=0x1EE, ref=19, allies=(0x010E, 0x0105, 0x0107)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="booker", name="BOOKER T", short="BOOKER", id=0x0110, music=(76, 76), video=(0, 0), h=15, w=158,
         rev=0x140, ref=23, allies=(0x0111,)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="stevieray", name="STEVIE RAY", short="S. RAY", id=0x0111, music=(77, 77), video=(0, 0), h=17, w=192,
         rev=0x1F4, ref=33, allies=(0x0110,)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="rey", name="REY MYSTERIO", short="REY", id=0x0112, music=(78, 78), video=(0, 0), h=6, w=55,
         rev=0x1B8, ref=48, allies=(0x0106, 0x010C, 0x010B)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="piper", name="RODDY PIPER", short="PIPER", id=0x0113, music=(79, 79), video=(0, 0), h=13, w=127,
         rev=0x1D0, ref=6, allies=(0x010B,)),   # (ports4: data\revport\ports4\PORTS4.md)
    dict(key="bischoff", name="ERIC BISCHOFF", short="BISCHOFF", id=0x0114, music=(80, 80), video=(0, 0), h=12, w=85,
         rev=0x13A, ref=61, allies=(0x0105, 0x0106, 0x0107, 0x010A)),   # (ports4: data\revport\ports4\PORTS4.md)
]
ORD0 = 102          # after the 18 Create-a-Wrestler edit buffers (ordinals 84-101)
# No Mercy Plus's IDs -> ours (allies in the abilities). Its own new ones: ours where we have them.
NM_ID = {0x0005: 0x0201,   # NM+ Big Show -> ours (in Benoit's box)
         0x091E: 0x0005,   # NM+ Steven Richards
         0x0201: 0x0000}   # Benoit isn't on the arcade's roster
for e in EXT: NM_ID[0x0900 + (e["nm"] - 65 + 5)] = e["id"]
for e in LATE:
    if "nm" in e: NM_ID[0x0900 + (e["nm"] - 65 + 5)] = e["id"]

van = bytearray(open(W + "gen_data/van_sel_ram.bin", "rb").read())
struct.pack_into(">II", van, 0x5EFC, 0x27BDFFE0, 0xAFB00010)   # the rig's hook out of the game's unpack
nm = open(W + "gen_data/nm_pick_ram.bin", "rb").read()
for e in LATE:      # (height, weight: No Mercy Plus's)
    if "nm" in e and "h" not in e:
        s_ = 0x80401010 + e["nm"] * 0x50
        e["h"] = nm[(s_ + 6) & 0x7FFFFF]; e["w"] = struct.unpack_from(">H", nm, (s_ + 8) & 0x7FFFFF)[0]
EV, EN = Emu(van), Emu(nm)
def nmfile(i): return open(nmp("/home/devbox/nmplus/f/%04x.bin") % i, "rb").read()
NM_STRUCTS = 0x80401010          # NM+'s per-ordinal costume structs (0x14 bytes, 4 per ordinal)
NM_UNPACK = 0x80005F8C           # NM+'s func_80005EFC (v1.0 build)
VAN_UNPACK, VAN_PACK = 0x80005EFC, 0x80005EC0
BUF_M, BUF_P, BUF_S, BUF_A = 0x80780000, 0x80780100, 0x80781000, 0x80782000

def unpack(E, fn, moves, params):
    E.wbytes(BUF_M, moves[:0xB8]); E.wbytes(BUF_P, params[:0x20])
    E.wbytes(BUF_S, bytes(0x800)); E.wbytes(BUF_A, bytes(0x80))
    E.call(fn, [BUF_M, BUF_S, BUF_P, BUF_A])
    slots = [(E.r16(BUF_S + 6 * i), E.r8(BUF_S + 6 * i + 2), E.r8(BUF_S + 6 * i + 3), E.r8(BUF_S + 6 * i + 4)) for i in range(292)]
    ab = [struct.unpack(">h", E.rbytes(BUF_A + 2 * i, 2))[0] for i in range(55)]
    return slots, ab
def pack(slots, ab):
    E = EV
    E.wbytes(BUF_S, bytes(0x800)); E.wbytes(BUF_A, bytes(0x80)); E.wbytes(BUF_M, bytes(0xB8)); E.wbytes(BUF_P, bytes(0x20))
    for i, (m, f2, f3, f4) in enumerate(slots):
        E.w16(BUF_S + 6 * i, m); E.w8(BUF_S + 6 * i + 2, f2); E.w8(BUF_S + 6 * i + 3, f3); E.w8(BUF_S + 6 * i + 4, f4)
    for i, a in enumerate(ab): E.w16(BUF_A + 2 * i, a & 0xFFFF)
    E.call(VAN_PACK, [BUF_S, BUF_M, BUF_A, BUF_P])
    return E.rbytes(BUF_M, 0xB8) + E.rbytes(BUF_P, 0x20)

# the game's movesets (ordinals 0-83, as the roster has them)
VMOVES = EV.r32(0x80047E94)
van_sets = [bytes(van[(VMOVES + o * 0xD8) & 0x7FFFFF:(VMOVES + o * 0xD8 + 0xD8) & 0x7FFFFF]) for o in range(84)]
van_sets[14] = None    # (Benoit's place: Big Show's key in this RAM image)
def van_unpacked(o): return unpack(EV, VAN_UNPACK, van_sets[o][:0xB8], van_sets[o][0xB8:])

# move numbers: NM+ -> the game's, through the animation records (unique per move)
def _moverecs():
    """(the game's records, No Mercy Plus's): per move id/10, its 20-byte record in each of the files 0x69, 0x1F0,
    0x1F1. nmport/moverecs.pkl when it's there; else made from the files themselves (2026-10-28: the pkl wasn't
    kept -- made this way, roster_ext.h comes out byte for byte as it was)"""
    p = W + "nmport/moverecs.pkl"
    if os.path.exists(p): return pickle.load(open(p, "rb"))
    def recs(fmt):
        fs = [open(nmp(fmt) % f, "rb").read() for f in (0x69, 0x1F0, 0x1F1)]
        return [tuple(d[20 * i:20 * i + 20] for d in fs) for i in range(len(fs[0]) // 20)]
    return recs("/tmp/devbox-0/nmfiles/f/%04x.bin"), recs("/home/devbox/nmplus/f/%04x.bin")
vrecs, nrecs = _moverecs()
vnames, nnames = pickle.load(open(W + "nmport/names.pkl", "rb"))
rec_ix = {}
for i, r in enumerate(vrecs): rec_ix.setdefault(r, i)
def norm(s): return "".join(c for c in s.lower() if c.isalnum())
vname_ix = {}
for k, v in vnames.items(): vname_ix.setdefault(norm(v), k)
def map_move(m):
    if m in (0, 0xFFFF): return m, "same"
    i = m // 10
    if m % 10 == 0 and i < len(nrecs) and nrecs[i] in rec_ix: return rec_ix[nrecs[i]] * 10, "anim"
    nmn = nnames.get(m)
    if nmn and norm(nmn) in vname_ix: return vname_ix[norm(nmn)], "name"
    return None, nmn

# which moves each slot can hold in the game's packing (its value lists)
def slot_lists():
    lay = EV.r32(0x800442DC); recs = []
    p = lay
    while struct.unpack(">h", EV.rbytes(p, 2))[0] != -1:
        s = struct.unpack(">h", EV.rbytes(p, 2))[0]
        cs = [struct.unpack(">h", EV.rbytes(p + 2 + 2 * i, 2))[0] for i in range(4)]
        recs.append((s, cs)); p += 10
    ncat = max(c for _, cs in recs for c in cs) + 1
    widths = [struct.unpack(">h", EV.rbytes(0x800442E4 + 2 * c, 2))[0] for c in range(ncat)]
    vals = []
    for c in range(ncat):
        t = EV.r32(0x80045500 + 4 * c); vals.append([EV.r16(t + 2 * k) for k in range(1 << widths[c])])
    return recs, widths, vals
LAYOUT, WIDTHS, VALS = slot_lists()
CAN = {s: set(v for c in cs for v in VALS[c]) for s, cs in LAYOUT}

# ---- the game's packing of a moveset, in Python (func_800075DC's format): per layout slot, w + 5 bits:
# 3 flag bits, 2 bits choosing one of the slot's 4 value lists, w bits of index (w: the widest list)
class Bits:
    def __init__(s, b): s.b = b; s.pos = 0
    def read(s, n):
        v = 0
        for _ in range(n):
            byte = s.b[s.pos >> 3] if (s.pos >> 3) < len(s.b) else 0
            v = (v << 1) | ((byte >> (7 - (s.pos & 7))) & 1); s.pos += 1
        return v
def cells_of(packed):
    """the (list, index) each layout slot of a packed moveset reads"""
    b = Bits(packed); out = []
    for slot, cs in LAYOUT:
        w = max(WIDTHS[c] for c in cs); v = b.read(w + 5)
        out.append((slot, cs[(v >> w) & 3], v & ((1 << w) - 1)))
    return out
def pack_cells(choice, slots):
    """choice: slot -> (sel, idx); slots: slot -> (move, f2, f3, f4)"""
    bits = []
    for slot, cs in LAYOUT:
        w = max(WIDTHS[c] for c in cs); sel, idx = choice[slot]; _, f2, f3, f4 = slots[slot]
        v = (((f4 << 2) | (f3 << 1) | f2) << (w + 2)) | (sel << w) | idx
        bits += [(v >> (w + 4 - i)) & 1 for i in range(w + 5)]
    by = bytearray(0xB8)
    for i, x in enumerate(bits): by[i >> 3] |= x << (7 - (i & 7))
    return bytes(by)
# the list cells the game's own movesets use (every moves file the ROM's Superstars and edit slots name)
VAN_FILES = sorted(set(EV.r16(EV.r32(0x80047124 + 16 * o) + 0xA) for o in range(1, 102)))
USED = set()
for f in VAN_FILES:
    for slot, c, i in cells_of(open(nmp("/tmp/devbox-0/nmfiles/f/%04x.bin") % f, "rb").read()[:0xB8]): USED.add((c, i))
for o in range(84):
    if van_sets[o]:
        for slot, c, i in cells_of(van_sets[o][:0xB8]): USED.add((c, i))
def spare_cells(c):
    """cells of list c no moveset of the game reads and that hold nothing (a 0 / 0xFFFF that isn't the
    list's first): a moveset may have the game read a move there while it's unpacked"""
    v = VALS[c]
    return [i for i in range(len(v)) if v[i] in (0, 0xFFFF) and v.index(v[i]) != i and (c, i) not in USED]
def encode(slots):
    """-> packed 0xB8 bytes, [(list, index, move)] to set while it's unpacked"""
    choice = {}; temp = {}
    for slot, cs in LAYOUT:
        m = slots[slot][0]
        for sel, c in enumerate(cs):
            if m in VALS[c]: choice[slot] = (sel, VALS[c].index(m)); break
        else:
            for sel, c in enumerate(cs):
                cell = next((i for i in spare_cells(c) if temp.get((c, i), m) == m), None)
                if cell is not None:
                    temp[(c, cell)] = m; choice[slot] = (sel, cell); break
            else: raise SystemExit("slot %d: no room for move %d" % (slot, m))
    return pack_cells(choice, slots), sorted((c, i, m) for (c, i), m in temp.items())

def stem(s): return " ".join(w for w in s.lower().replace("/", " ").split() if not w.isdigit() and not (w[:-1].isdigit()))
def stand_in(slot, nmname, ref):
    if not nmname or nmname == "None" or slot not in CAN: return ref
    st = stem(nmname)
    cands = [m for m in CAN[slot] if m not in (0, 0xFFFF) and stem(vnames.get(m, "")) == st]
    if not cands:
        words = set(st.split())
        scored = [(len(words & set(stem(vnames.get(m, "")).split())), m) for m in CAN[slot] if m not in (0, 0xFFFF)]
        best = max(scored, default=(0, 0))
        if best[0] >= max(1, len(words) - 1) and best[0] >= 1: cands = [m for sc, m in scored if sc == best[0]]
    if not cands: return ref
    return ref if ref in cands else cands[0]   # (ref None: no fallback)

# hand-picked stand-ins (v0.17.1): Big Show's Showstopper -> the Huge Chokeslam, his two unnamed slots
OVERRIDE = {"bigshow": {70: 1520, 104: 3590}}
WM_SLOT = {86: 86, 90: 88, 94: 90, 98: 92}
WM_BLOCK = range(86, 102)
WM_SLOT_INV = {v: k for k, v in WM_SLOT.items()}
def wm_fill(wm_moves, i):
    """WrestleMania 2000's move for slot i, when this game has it (its very animations: gen/wmanim.py)"""
    import wmanim
    i0 = WM_SLOT_INV.get(i, i) if i in WM_BLOCK else i
    if i in WM_BLOCK and i not in WM_SLOT_INV: return None
    m = wm_moves[i0][0]
    if m in (0, 0xFFFF) or i not in CAN: return None
    kind = sorted(x for x in CAN[i] if x not in (0, 0xFFFF))
    c = [x for x in wmanim.nm_move(m)[0] if x in CAN[i] or (kind and kind[0] <= x <= kind[-1])]
    return (([x for x in c if x in CAN[i]]) or c or [None])[0]
def port_moveset(key, nmo, ref_ord, report, wm=None):
    """wm: (moves file, abilities file) of the Superstar in WrestleMania 2000: what No Mercy Plus gives him
    that this game hasn't, his WM2000 move for the slot when this game has that one"""
    s = NM_STRUCTS + nmo * 0x50
    nslots, nab = unpack(EN, NM_UNPACK, nmfile(EN.r16(s + 0xA)), nmfile(EN.r16(s + 0xC)))
    rslots, _ = van_unpacked(ref_ord)
    if wm:
        import wm2k
        wm_moves = wm2k.unpack_moves(wm2k.wfile(wm[0]), wm2k.wfile(wm[1]))[0]
    exact = []; approx = []; n_exact = n_sub = 0
    for i, (m, f2, f3, f4) in enumerate(nslots):
        v, how = map_move(m)
        if m == 0xFFFF and i in CAN and 0xFFFF not in CAN[i]: v = 0     # "none": the game's way is 0 there
        if v not in (None, 0, 0xFFFF) and i in CAN and v not in CAN[i]:
            # a move of the game the slot's lists lack (NM+'s lists are wider): fine if it's of the slot's
            # kind (within the range of moves the slot's lists hold); else NM+ means something else by it
            kind = sorted(x for x in CAN[i] if x not in (0, 0xFFFF))
            if not (kind and kind[0] <= v <= kind[-1]): v, how = None, "(NM+'s own use of the slot)"
        if v is None:
            # no counterpart in the game (an animation No Mercy Plus added): a move of the same kind
            # the slot can hold (by name: "DDT 04" -> a DDT), else the reference Superstar's
            # (a move of the same kind by name first: what No Mercy Plus meant; then WrestleMania 2000's)
            v = OVERRIDE.get(key, {}).get(i); src = "override"
            if not v:
                v = stand_in(i, how, None); src = "stand-in"
            if not v and wm and i != 252:
                v = wm_fill(wm_moves, i); src = "WM2000"
            if not v: v = rslots[i][0]; src = "reference"
            report.append("  %s slot %3d: NM+ %5d %-28s -> %5d %s (%s)" % (key, i, m, how, v, vnames.get(v, ""), src))
            n_sub += 1
        elif m not in (0, 0xFFFF): n_exact += 1
        exact.append((v, f2, f3, f4))
    for i in range(292):     # "none" (0xFFFF) where the game's lists have no such value: 0, as the game has it
        if exact[i][0] == 0xFFFF and i in CAN and 0xFFFF not in CAN[i]: exact[i] = (0,) + exact[i][1:]
    ab = list(nab)
    ab[0:3] = body_fields(key, nab, nslots[252][0], ref_ord, report)
    for k in (31, 32, 33, 34):
        if ab[k] in NM_ID: ab[k] = NM_ID[ab[k]]
        elif 0x905 <= ab[k] <= 0x91E: ab[k] = 0          # NM+'s own Superstars we don't have
    if key == "bigshow": ab[31:35] = [0x101, 0x902, 0x001, 0]     # Triple H, Shane, The Rock
    return exact, ab, report_line(key, n_exact, n_sub, ab, report)
# Abilities 0-2: the game's are (0, the standing pose: 0 normal, 1 boxing guard, 2 martial-arts guard,
# 3 the big men's, 4 the ladies who don't wrestle; the weight class: 0 light, 1 heavy, 2 Andre's).
# No Mercy Plus put its own things in these three (and the standing pose in move slot 252, with its own
# poses), so a Superstar's are made from the game's Superstars whom No Mercy Plus gives the same three
# and the same pose: the reference Superstar's when he's one of them, else the most of them have.
# (the game's Superstars No Mercy Plus has as they are: not its Big Show, who took Steven Richards' ID)
NM_SAME = [o for o in range(1, 65) if van_sets[o] and EN.r16(NM_STRUCTS + o * 0x50) not in NM_ID and
           EN.r16(NM_STRUCTS + o * 0x50) == EV.r16(EV.r32(0x80047124 + 16 * o))]
def body_fields(key, nab, stance, ref_ord, report):
    from collections import Counter
    like = []
    for o in NM_SAME:
        s = NM_STRUCTS + o * 0x50
        nsl, nb = unpack(EN, NM_UNPACK, nmfile(EN.r16(s + 0xA)), nmfile(EN.r16(s + 0xC)))
        if nb[0:3] == nab[0:3] and nsl[252][0] == stance: like.append(o)
    if not like:        # (a pose of No Mercy Plus's own: the ones with the same three, else the reference's)
        for o in NM_SAME:
            s = NM_STRUCTS + o * 0x50
            nsl, nb = unpack(EN, NM_UNPACK, nmfile(EN.r16(s + 0xA)), nmfile(EN.r16(s + 0xC)))
            if nb[0:3] == nab[0:3]: like.append(o)
    if not like: like = [ref_ord]
    vals = {o: tuple(van_unpacked(o)[1][0:3]) for o in like}
    pick = vals[ref_ord] if ref_ord in vals else Counter(vals.values()).most_common(1)[0][0]
    report.append("  %s: standing pose / weight class %s (as No Mercy Plus has: %s %s, like %d of the game's: %s)"
                  % (key, list(pick), nab[0:3], nnames.get(stance), len(like), sorted(set(vals.values()))))
    return list(pick)
# ---- a Superstar of WrestleMania 2000 (gen/wm2k.py): its moveset in this game's moves --------------
# WM2000's moves are this game's ancestors: the same slots (292), the same kinds of moves; a move is
# found here by its name (the games' own move-name tables). A move this game hasn't (his own finisher,
# taunts and entrance, a few counters): a hand-picked one, else a move of the same kind the slot can
# hold (by name), else the reference Superstar's. Abilities: WM2000 has 50, this game 55; matched by
# comparing the 35 Superstars both games have: 0-30 the same (but 18: always 3 here, 19: 0), 35-49 ->
# 39-40, 42-47, 49-54; 31-34 allies (WM2000's IDs: none kept), 41 and 48 are 0 for most here.
# A move is found here first by its animations (gen/wmanim.py: No Mercy kept WM2000's animation files,
# Jeff Jarrett's own among them -- his Stroke is this game's "Front Russian Sweep", his taunts and poses
# its "Taunt 001"..), then by its name, else a move of the same kind the slot can hold (by name), else
# the reference Superstar's.
# Slots: the same but for one block (the 34 Superstars both games have line them up): WM2000's grapples
# 86, 90, 94, 98 are this game's 86, 88, 90, 92 (its 87-101 otherwise: the reference's).
WM_SLOT = {86: 86, 90: 88, 94: 90, 98: 92}
WM_BLOCK = range(86, 102)
def port_wm_moveset(key, mv_file, ab_file, ref_ord, override, report):
    import wm2k, wmanim
    wslots, wab = wm2k.unpack_moves(wm2k.wfile(mv_file), wm2k.wfile(ab_file))
    rslots, rab = van_unpacked(ref_ord)
    ix = {}
    for src in (wm2k.NM_NAMES, vnames):
        for k, v in sorted(src.items()):
            if k % 10 == 0 and k not in ix.setdefault(norm(v), []): ix[norm(v)].append(k)
    out = list(rslots); n = collections.Counter()
    def fits(i, v):
        if i not in CAN: return False
        if v in CAN[i]: return True
        kind = sorted(x for x in CAN[i] if x not in (0, 0xFFFF))
        return bool(kind) and kind[0] <= v <= kind[-1]
    for i0, (m, f2, f3, f4) in enumerate(wslots):
        if i0 in WM_BLOCK and i0 not in WM_SLOT: continue
        i = WM_SLOT.get(i0, i0)
        name = wm2k.WM_NAMES.get(m - m % 10, "") if m not in (0, 0xFFFF) else ""
        fl = None
        if i in override.get(key, {}):
            v, how = override[key][i], "override"
            if isinstance(v, tuple): v, fl = v[0], v[1:]
        elif m in (0, 0xFFFF): v, how = m, "none"
        else:
            v = how = None
            c, _ = wmanim.nm_move(m)
            c = [x for x in c if fits(i, x)]
            if c: v, how = (([x for x in c if x in CAN[i]]) or c)[0], "animation"
            if v is None:
                for k in (norm(name), norm(name) + "01"):
                    c = [x + m % 10 for x in ix.get(k, []) if fits(i, x + m % 10)]
                    if c: v, how = (([x for x in c if x in CAN[i]]) or c)[0], "name"; break
            if v is None:
                v = stand_in(i, name, rslots[i][0]) if i in CAN else rslots[i][0]
                how = "stand-in" if v != rslots[i][0] else "reference"
                if how == "reference": f2, f3, f4 = rslots[i][1:]
            if how != "animation":
                report.append("  %s slot %3d: WM2000 %5d %-34s -> %5d %-28s (%s)" % (key, i, m, name, v, vnames.get(v - v % 10, wm2k.NM_NAMES.get(v - v % 10, "")), how))
        n[how] += 1
        if v == 0xFFFF and i in CAN and 0xFFFF not in CAN[i]: v = 0
        out[i] = (v, f2, f3, f4)
    ab = list(rab)
    for j in range(31): ab[j] = wab[j]
    ab[18], ab[19] = 3, 0
    for j in range(31, 39): ab[j] = 0
    for j, w in ((39, 35), (40, 36), (42, 37), (43, 38), (44, 39), (45, 40), (46, 41), (47, 42),
                 (49, 44), (50, 45), (51, 46), (52, 47), (53, 48), (54, 49)): ab[j] = wab[w]
    ab[41] = 0; ab[48] = 0
    ab[0:3] = rab[0:3]                   # standing pose, weight class: the reference's
    report.append("  %s: %s" % (key, dict(n)))
    return out, ab
WM_OVERRIDE = {}

# ---- a Superstar of WCW/nWo Revenge (/home/devbox/work/revport/revmoves.py): his moveset in this game's
# moves, by animation. Revenge keeps a Superstar's animations in a list (his file + 2); the Superstars both
# games have line this game's slots up with places in that list (where the same animation is, file for
# file); his move for a slot is this game's move whose animation is his there (the very file: "exact";
# else the nearest pose for pose, kept when near enough: "near"); otherwise the reference Superstar's.
# Abilities: the reference's (Revenge's ratings don't line up with this game's), allies his own.
REV_NEAR = 0.3
def port_rev_moveset(key, rev_file, ref_ord, allies, override, report):
    sys.path.insert(0, nmp("/home/devbox/work/revport"))
    import revmoves as RM
    rslots, rab = van_unpacked(ref_ord)
    pos = RM.slot_positions(RM.PAIRS)
    def fits(i, v):
        if i not in CAN: return True
        if v in CAN[i]: return True
        kind = sorted(x for x in CAN[i] if x not in (0, 0xFFFF))
        return bool(kind) and kind[0] <= v <= kind[-1]
    pool = RM.slot_pool(CAN)
    allm = sorted(set(m for s_ in pool.values() for m in s_))
    rl = RM.revlist(rev_file)
    out = list(rslots); n = collections.Counter()
    for i in range(292):
        how = "reference"; v = rslots[i][0]; d = None
        fl = None
        if i in override.get(key, {}):
            v, how = override[key][i], "override"
            if isinstance(v, tuple): v, fl = v[0], v[1:]
        elif i in pos and rslots[i][0] not in (0, 0xFFFF):
            pr = RM.predict(rl, {i: pos[i]}, {i: [m for m in allm if fits(i, m)]}).get(i)
            if pr and (pr[1] == "exact" or pr[2] <= REV_NEAR): v, how, d = pr[0], pr[1], pr[2]
        if how != "reference":
            report.append("  %s slot %3d: Revenge list %3d -> %5d %-28s (%s %s; reference %d %s)" % (key, i, pos.get(i, (-1,))[0], v, vnames.get(v - v % 10, ""), how, d, rslots[i][0], vnames.get(rslots[i][0] - rslots[i][0] % 10, "")))
        n[how] += 1
        if v == 0xFFFF and i in CAN and 0xFFFF not in CAN[i]: v = 0
        out[i] = (v,) + (tuple(fl) if fl is not None else tuple(rslots[i][1:]))
    ab = list(rab)
    ab[31:35] = list(allies) + [0] * (4 - len(allies))
    if key in REV_WEIGHT: ab[2:5] = REV_WEIGHT[key]  # (the weight class: his own, not his reference's)
    if key in REV_POSE: ab[1] = REV_POSE[key]        # (the standing pose: his own)
    report.append("  %s: %s" % (key, dict(n)))
    return out, ab
# Their weight class (ability 2: 0 light, 1 heavy): Revenge's heavyweights. (Bret's reference is a light one:
# every strong grapple turned into a long struggle, as on a man too heavy for him.)
REV_WEIGHT = {"brethart": [1, 1, 0], "neidhart": [1, 1, 0],   # (abilities 2-4: a heavyweight's, as most of the roster's)
              "hogan": [1, 1, 0], "nash": [1, 1, 0], "hall": [1, 1, 0], "savage": [1, 1, 0], "hennig": [1, 1, 0],
              "giant": [2, 0, 0], "sting": [1, 1, 0], "luger": [1, 1, 0], "goldberg": [1, 1, 0], "ricksteiner": [1, 1, 0], "scottsteiner": [1, 1, 0], "booker": [1, 1, 0], "stevieray": [1, 1, 0], "piper": [1, 1, 0]}   # (The Giant: Andre the Giant's, the game's one ability 2 of 2)
# The standing pose (ability 1: 0 normal ... 3 the big men's -- the Undertaker's, Kane's, Test's, Andre's): Kevin
# Nash's the big men's (his reference's too; set here so it stays his whatever the reference)
REV_POSE = {"nash": 3, "luger": 0}
# slot -> move, or (move, its three flags) where the reference has nothing to go by
REV_OVERRIDE = {
    # Neidhart's ground special: Revenge's (code 200, the Boston Crab: No Mercy's 11750, its files byte for
    # byte); his reference (ordinal 34) has none -- a special as Bret's reference's is (flags 0, 0, 1)
    "neidhart": {157: (11750, 0, 0, 1)},
    # Bret's front grapples 25 and 50: Revenge's body slam is byte for byte No Mercy's 960 -- the slam a
    # wrestler struggles through on a man too big for him (a chain: 960 -> 961 ...) -- every one of his
    # grapples looked a struggle. Not the Scoop Slam (270) either: a strong grapple's slot plays its move's
    # variants (the counter, the block, ...: move + 2, + 4 ...), and 270 has only the one -- the others fell
    # back to the game's stand-off, both shoving and letting go: a struggle again. His reference's moves
    # (16250, the Back Body Flip; 930), which have them.
    "brethart": {25: 16250, 50: 930},
    # Hogan's front grapple 30 and Nash's 25 and 50: the same Revenge body slam (960) -- each made a slam with the
    # variants a strong grapple's slot plays and no struggle: the Back Body Flip (16250, as Bret's 25: Steve
    # Austin's, Jericho's, Angle's ...) and, for Nash's 50, the Sidewalk Slam (1020: the Back Body Flip's own
    # counter and block, 16252 / 16254's animations; his move). (No 960-969 is left in the three: checked by main().)
    # The references' own moves where gen/revanims.py has nothing of his for the slot (the Rock's, the Undertaker's:
    # the user, "Hogan's strong punch and stomps were The Rock's"), the like of them anyone has: Hogan's strong
    # strike (188) the Big Boot, his stomps (213 / 214) Stomp 01, his Rock Spinebuster (94) the Spinebuster; Nash's
    # Undertaker Punch the Big Boot.
    # Hogan's leg drop, his finisher (the user, 2026-10-30: "Hogan needs a leg drop. It must be a special finisher"):
    # his ground special (155, the grapple at a downed man's head with SPECIAL) the Russian Neck Drop's move (16750: a
    # special grapple with a blow -- its record's at frame 68, the man's reaction at 72, him left lying) played with his
    # leg drop (gen/revanims.py legdrop_pair: his Revenge drop, 7980's animation, landing at 68). (Tried: the drop
    # itself in 155 -- the man got up; the People's Elbow special's move with it -- cut short, no blow; 211 -- not the
    # special: the user's tests 2026-10-30 / 31)
    "hogan": {30: 16250, 94: 3390, 188: 5690, 213: 8670, 214: 9120},
    "nash": {25: 16250, 50: 1020, 188: 5690},
    # Savage's and Hennig's 25 and 50: the same body slam -- Nash's pair for Savage; for Hennig his reference's own
    # 50 (380, Kurt Angle's). Savage's strong strike (188): Punch 01, the game's most usual (HBK's is a jab; his
    # Revenge one, as revanims.py took it, a punch down at a fallen man -- the user, 2026-10-30)
    "savage": {25: 16250, 50: 1020, 188: 6200},
    # Hennig's winning taunt (235: the game plays it where the winner stands -- ovl_e func_800E67F0, the taunt state's
    # phase 4): Revenge's win pose (his list 351) is his turnbuckle taunt, 11240 (Taunt 008, the game's top-turnbuckle
    # slot 232's), and played mid-ring he climbed an invisible corner (the user, 2026-10-31). None of the game's own
    # Superstars has a corner, apron or outside taunt there; his own Revenge standing taunt (Taunt 016, his 228-230)
    "hennig": {25: 16250, 50: 380, 235: 15020},
    # The Giant's front grapple 30: the same body slam (960) -- the Back Body Flip, as Hogan's 30
    "giant": {30: 16250},
    "sting": {188: 6200, 235: 15350},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out; 235 the winning taunt his own standing one -- Revenge's win pose is a corner one, as Hennig's)
    "goldberg": {188: 6200, 235: 15370},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out; 235 the winning taunt his own standing one -- Revenge's win pose is a corner one, as Hennig's)
    "luger": {235: 9880},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out; 235 the winning taunt his own standing one -- Revenge's win pose is a corner one, as Hennig's)
    "ricksteiner": {235: 15360},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out)
    "scottsteiner": {30: 16250, 235: 15470},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out)
    "booker": {235: 15380},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out)
    "rey": {235: 15410},   # (ports4: the reference's signature strike out / Revenge's body slam 960 out)
}
REV_NO_STRUGGLE = ("hogan", "nash", "hall", "savage", "hennig", "giant", "sting", "luger", "goldberg", "ricksteiner", "scottsteiner", "booker", "stevieray", "rey", "piper", "bischoff")      # (main(): no move 960-969 in their movesets)
# The Revenge ports' second IDs (gen/revbanks.py: a man's looks past four costumes, in his box -- src/variants.c):
# his own LATE entry and lists under a new key and ID (the same name, moves, abilities, music, allies)
import revbanks
for _b in revbanks.BANKS:
    if any(e["key"] == _b["key"] for e in LATE): continue
    LATE.append(dict(next(e for e in LATE if e["key"] == _b["base"]), key=_b["key"], id=_b["id"]))
    for _t in (REV_WEIGHT, REV_POSE, REV_OVERRIDE):
        if _b["base"] in _t: _t[_b["key"]] = _t[_b["base"]]
    if _b["base"] in REV_NO_STRUGGLE: REV_NO_STRUGGLE += (_b["key"],)
# (no2k15, 2026-11-23) The two WWE 2K15 full-detail test slots that were here -- STONE COLD 2K15 (0x011A) and THE ROCK 2K15
# (0x011B), the experiments of 2026-11-03 -- are removed: the user, 2026-11-14, "the HD / 2K15 models can be REMOVED from what
# ships (they were only for testing)", and 2026-11-23, "remove them unless they exist as character folders" (none does). The
# full-detail engine path stays (src/hd.c: a wrestler folder's own hd.bin). A LATE entry may still say same=<No Mercy ordinal>
# (his moveset that man's exactly, below); none does now.

def report_line(key, n_exact, n_sub, ab, report):
    report.append("  %s: %d moves exact, %d stand-ins; allies %s" % (key, n_exact, n_sub, [hex(x) for x in ab[31:35]]))

def keyed(key, exact, ab):
    """the packed moveset (moves: our packing, with the cells the game reads its extra moves from;
    abilities: the game's own packing) -> (0xD8 bytes, cells); checked by the game's own unpacking"""
    moves, cells = encode(exact)
    params = pack(exact_encodable(exact), ab)[0xB8:]
    tabs = [(EV.r32(0x80045500 + 4 * c) + 2 * i, EV.r16(EV.r32(0x80045500 + 4 * c) + 2 * i)) for c, i, m in cells]
    for (c, i, m), (a, _) in zip(cells, tabs): EV.w16(a, m)
    back, bab = unpack(EV, VAN_UNPACK, moves, params)
    for a, old in tabs: EV.w16(a, old)
    bad = [i for i in range(292) if back[i] != exact[i]]
    assert not bad, (key, bad[:10], [(back[i], exact[i]) for i in bad[:5]])
    assert bab == ab, (key, bab, ab)
    return moves + params, cells
def exact_encodable(exact):
    """the moveset with the moves the game's lists lack left out (only the abilities are packed from it)"""
    return [(m if (i not in CAN or m in CAN[i]) else 0, f2, f3, f4) for i, (m, f2, f3, f4) in enumerate(exact)]

def c_arr(t, name, vals, per=16, fmt="%d"):
    s = "static const %s %s[%d] = {\n" % (t, name, len(vals))
    for i in range(0, len(vals), per): s += "    " + ", ".join(fmt % v for v in vals[i:i + per]) + ",\n"
    return s + "};\n"

def main():
    report = []
    APP = open(nmp("/home/devbox/nmplus/f/0001.bin"), "rb").read()
    VAPP = open(nmp("/tmp/devbox-0/nmfiles/f/0001.bin"), "rb").read()
    ref = {"funaki": 49, "gangrel": 29, "mosh": 41, "thrasher": 41}   # Taka, Edge, Al Snow
    out = ["// generated by gen/mkroster.py from No Mercy Plus V3: the roster's new Superstars",
           "#define EXT_N %d" % len(EXT), "#define EXT_ORD0 %d" % ORD0, ""]
    rows = []; looks = []; slots_m = []; slots_f = []; abil = []; keys = []
    for k, e in enumerate(EXT):
        s = NM_STRUCTS + e["nm"] * 0x50
        cos = []
        for c in range(4):
            a = s + 0x14 * c
            music, video, height = EN.r8(a + 4), EN.r8(a + 5), EN.r8(a + 6)
            weight, app = EN.r16(a + 8), EN.r16(a + 0xE)
            look = APP[app * 0x26:app * 0x26 + 0x26]
            cos.append((music, video, height, weight)); looks.append(look)
        exact, ab, _ = port_moveset(e["key"], e["nm"], ref[e["key"]], report)
        keys.append(keyed(e["key"], exact, ab))
        m, v, h, w = cos[0]
        rows.append('    { 0x%04X, %d, "%s", "%s", "%s", { %d, %d }, { %d, %d }, %d, %d },   // NM+ ordinal %d (its music %d, video %d)'
                    % (e["id"], ORD0 + k, e["name"], e["short"], e["nick"], e["music"][0], e["music"][1],
                       e["video"][0], e["video"][1], h, w, e["nm"], m, v))
    bexact, bab, _ = port_moveset("bigshow", 5, 4, report)        # Big Show (NM+ ordinal 5), Kane the reference
    keys.append(keyed("bigshow", bexact, bab))
    # Jeff Jarrett and The Blue Meanie, from WrestleMania 2000 (Billy Gunn, Rikishi the references for what
    # it has that this game hasn't); Droz and Mideon from No Mercy Plus (WrestleMania 2000's where it
    # has what No Mercy Plus has that this game hasn't; Albert, Bradshaw the references)
    for e in LATE:
        if "same" in e:
            lx, lab = van_unpacked(e["same"]); lx = list(lx)
            # (2026-11-10) His moveset is the game's man's exactly, every slot. Two of his standing taunts used to change
            # places here, only so his bytes were unlike every other moveset's (a moveset was known by its bytes). That
            # put his standing taunt where his special taunt belongs: the user, in play, "Austin doesn't have his 'special'
            # taunt when he has special, it's just his regular standing taunt", and "His taunts should match stock
            # Austins." roster.c now knows a moveset by whose it is, so the swap is gone. keyed() below checks that the
            # game's own unpacking gives back every one of his 292 slots.
            print("  %s's taunts 227-230: %s (the game's own, as they are)" % (e["key"], [lx[i][0] for i in range(227, 231)]))
        elif "nm" in e:
            lx, lab, _ = port_moveset(e["key"], e["nm"], e["ref"], report, wm=e["wm"][1:] if "wm" in e else None)
        elif "rev" in e:
            lx, lab = port_rev_moveset(e["key"], e["rev"], e["ref"], e["allies"], REV_OVERRIDE, report)
            # (the taunts played where he stands -- 227-230 and the winning one, 235 -- never one of the corner's, the
            # top turnbuckle's, the apron's or the outside's (only slots 231-234 hold those in the game's movesets))
            spot = set().union(*(CAN[k] for k in (231, 232, 233, 234))) - {0, 0xFFFF}
            bad = [(i, lx[i][0]) for i in (227, 228, 229, 230, 235) if lx[i][0] in spot]
            assert not bad, (e["key"], "a corner / apron / outside taunt played where he stands", bad)
            if e["key"] in REV_NO_STRUGGLE:
                bad = [(i, m) for i, (m, _, _, _) in enumerate(lx) if 960 <= m <= 969]
                assert not bad, (e["key"], "the struggling body slam in", bad)
        else:
            lx, lab = port_wm_moveset(e["key"], e["wm"][1], e["wm"][2], e["ref"], WM_OVERRIDE, report)
        keys.append(keyed(e["key"], lx, lab))
    # (2026-11-10) The keys no longer have to be unlike every other moveset. roster.c knows a moveset by whose it is
    # (moves_whose), not by its bytes, so a key may be the same as another's or as one of the game's own. The user, of
    # the rule that was here: "Wait, what, that sounds like a limit. We don't want limits."
    l0 = len(keys) - len(LATE)
    # A second ID's (gen/revbanks.py) is his base's, the same man: roster.c is told so by this list, each LATE entry's row
    # of moveset_key -- his own, or his base's. Still checked, because roster.c fills the second ID's moveset from his
    # own row and sets the cells from his base's: the two rows must be one and the same, bytes and cells.
    late_row = []
    for k, e in enumerate(LATE):
        b_ = revbanks.BY.get(e["key"])
        late_row.append(l0 + (next(j for j, x in enumerate(LATE) if x["key"] == b_["base"]) if b_ else k))
        assert keys[l0 + k] == keys[late_row[k]], (e["key"], "a second ID's moveset isn't his base's")
    # Not a rule, a warning. roster.c still goes by the bytes where it can't tell whose a moveset is (a Create-a-
    # Wrestler's copy, a folder wrestler's own moveset), and takes the first row with those bytes. Two rows with the
    # same bytes and different cells would each be right for their own man, and the later one wrong in such a copy.
    for k, (p, c) in enumerate(keys):
        for j, (q, d) in enumerate(keys[:k]):
            if p[:0xB8] == q[:0xB8] and c != d:
                report.append("  WARNING: movesets %d and %d have the same bytes and different cells %s / %s: each is right for its own"
                              " wrestler; a copy whose owner roster.c can't tell gets moveset %d's" % (j, k, d, c, j))
    out.append("typedef struct { u16 id; u8 ord; const char* name; const char* short_name; const char* nick;")
    out.append("                 u8 music[2], video[2];   // [0] the game's, [1] with No Mercy Plus's themes and videos")
    out.append("                 u8 height; u16 weight; } ExtWrestler;   // as a record has them: inches over 5'0\", pounds - 100")
    out.append("static const ExtWrestler ext_wrestlers[EXT_N] = {"); out += rows; out.append("};")
    out.append("// after Benoit: Jeff Jarrett, Droz, Mideon, The Blue Meanie")
    out.append("#define EXT_LATE_N %d" % len(LATE))
    out.append("static const ExtWrestler ext_late[EXT_LATE_N] = {")
    for k, e in enumerate(LATE):
        out.append('    { 0x%04X, %d, "%s", "%s", "", { %d, %d }, { %d, %d }, %d, %d },'
                   % (e["id"], ORD0 + len(EXT) + 1 + k, e["name"], e["short"], e["music"][0], e["music"][1], e["video"][0], e["video"][1], e["h"], e["w"]))
    out.append("};")
    # (their looks: gen/mkparts.py)
    out.append("// whose moveset each of them has: his own row of moveset_key, or -- a second ID, gen/revbanks.py -- his base's (roster.c moves_whose)")
    out.append("static const u8 ext_late_moveset[EXT_LATE_N] = { " + ", ".join(str(r) for r in late_row) + " };")
    out.append("// the movesets as the roster keeps them, packed (the new Superstars', then Big Show's): the game")
    out.append("// unpacks one right with the cells listed set in its lists of moves while it does (roster.c)")
    out.append("#define MOVESETS_N %d" % len(keys))
    out.append("#define MOVESET_BIG_SHOW %d" % (len(keys) - len(LATE) - 1))
    out.append("#define MOVESET_LATE0 %d   // (the Superstars after Benoit's, in ext_late's order)" % (len(keys) - len(LATE)))
    out.append("static const u8 moveset_key[MOVESETS_N][0xD8] = {")
    for p, _ in keys: out.append("    { " + ", ".join("0x%02X" % b for b in p) + " },")
    out.append("};")
    mc = max(1, max(len(c) for _, c in keys))
    out.append("typedef struct { u8 list, cell; u16 move; } MoveCell;")
    out.append("#define MOVESET_CELLS %d" % mc)
    out.append("static const u8 moveset_ncells[MOVESETS_N] = { " + ", ".join(str(len(c)) for _, c in keys) + " };")
    out.append("static const MoveCell moveset_cells[MOVESETS_N][MOVESET_CELLS] = {")
    for _, c in keys: out.append("    { " + ", ".join("{ %d, %d, %d }" % x for x in c) + " },")
    out.append("};")
    open(OUT, "w").write("\n".join(out) + "\n")

    for k, (_, c) in enumerate(keys): report.append("  moveset %d: %d cells %s" % (k, len(c), c))
    print("\n".join(report))

if __name__ == "__main__":
    main()
