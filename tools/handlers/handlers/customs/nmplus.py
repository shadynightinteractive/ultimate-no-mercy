"""No Mercy Plus V3, the customs case (2026-11-13, imports2): a No Mercy mod read against the game it patches.

No Mercy Plus V3 is an xdelta for No Mercy v1.0 (roms\\No Mercy Plus V3.xdelta, Delta Patcher). Applied, it is
data\\nmplus\\nmplus.z64 (68 MB: the cartridge made bigger) -- checked: all 9 of the patch's target windows' adler32 sums,
which the patch file carries, match that ROM's. Its files are in a table of its own (aki/romtable.py "nmplus_v3"); its code is
the game's with things moved 0x90 (its moveset unpack is the game's 0x80005EFC at 0x80005F8C, its model builder the game's
0x80018A3C at 0x80018ACC); it widened the looks fields (a lower body past 127, a mask past 31, boots past 63) and its part
tables, and added files (0x5000 on) for the new parts (mod gen/mkparts.py explains the format).

GOLDUST in No Mercy Plus V3 (looked for, 2026-11-13): he is NOT on its roster -- no record, names entry (file 2 has none),
moveset, photo, theme or Titantron of his. He is there as Create-a-Wrestler parts, bought in its SmackDown Mall (file
0x4AA9: "Goldust 1" / "Goldust 2", each [Ring Attire, Boots, Mask]): ring attires "Goldust 1" / "Goldust 2" (lower bodies
139 / 140, file 0x4A63's list), boots "Goldust" (65, file 0x4A5A), masks (the face paint) "Goldust 1" / "Goldust 2" (36 / 37,
file 0x4A62). So his folder is what a player of No Mercy Plus would make of him: those parts on a base -- No Mercy Plus's own
Test (its tall, long-blond-haired man: Goldust's build and hair) -- and his model is the one No Mercy Plus's own builder makes
of those looks, run on its RAM image (its code there is its ROM's, word for word: checked), whole, as gen/mkovr.py makes
Droz's and Mideon's. Everything else a Superstar has, No Mercy Plus hasn't for him (see imports2.py: where it comes from).
"""
import os, pickle, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
from .. import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
sys.path.insert(0, DEV)
from ..aki import romtable as RT
from ..aki import modtables as MTB

# (2026-11-13, modcheck) the mod's remap -- tool\modcheck.py tables for No Mercy Plus V3 against No Mercy v1.0 (with both RAM
# images: the parts probe). Every part number this handler puts in looks from STOCK knowledge goes through it.
REMAP = os.environ.get("NMPLUS_REMAP") or os.path.join(HERE, "..", "..", "work", "nmplus", "remap.json")   # (tools\handlers\work)


NMPLUS_ROM = os.path.join(DEV, "data", "nmplus", "nmplus.z64")
NMPLUS_RAM = os.path.join(DEV, "data", "gen_data", "nm_pick_ram.bin")     # (its Superstar Select: gen/mkroster.py's input)
NM_STRUCTS = 0x80401010           # its per-ordinal costume structs (0x14 bytes, 4 per ordinal; gen/mkroster.py)
BUILD = 0x80018ACC                # its model builder (the game's func_80018A3C)
# The appearance entry (0x26 bytes, the format of file 1 and of these lists' numbers) -> the looks struct (gen/mkparts.py)
SRC2UN = {0: 0, 1: 1, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 0xA, 0xA: 0xB, 0x1C: 0xC, 0x1D: 0xF, 0x1E: 0x12,
          0x1F: 0x13, 0x20: 0x15, 0x21: 0x18, 0x22: 0x1B, 0xC: 0x1E, 0xD: 0x1F, 0x11: 0x21, 0x12: 0x22, 0x13: 0x24,
          0x14: 0x25, 0xF: 0x27, 0x10: 0x28, 0x15: 0x2A, 0x16: 0x2B, 0x17: 0x2D, 0x18: 0x2E, 0x19: 0x30, 0x1A: 0x31,
          0x1B: 0x32, 0xE: 0x33, 0x23: 0x36, 0x24: 0x37, 0xB: 0x39, 0x25: 0x3C}
APP_FIELDS = ["body", "skin", "ring", "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr", "entr_c1",
              "entr_c2", "weapon", "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c",
              "relbow", "relbow_c", "lknee", "lknee_c", "rknee", "rknee_c", "boots", "boots_c1", "boots_c2",
              "head", "face", "hair", "hair_c", "front_hair", "facial_hair", "mask", "hat", "hat_c", "photo"]
# His parts, by No Mercy Plus's lists (the SmackDown Mall's two Goldust packs). Costumes: the two packs as sold, then each
# ring attire with the other pack's face paint. Colours 0: the parts' own (gold and black).
RING = (139, 140); MASK = (36, 37); BOOTS = 65
HAIR, FRONT_HAIR = 2, 0          # "Short 1-A", none (No Mercy Plus's lists 0x4A5D / 0x4A5E: goldust_apps says why)

# No Mercy Plus's own Create-a-Wrestler lists against the stock game's (nmplus_parts.json, made by scripts\mk_caw_tables.py
# from both ROMs): a looks value is an index into ITS lists, which replaced many of the stock game's entries and added more
# -- never read one by the stock game's numbering.
import json as _json
PARTS = _json.load(open(os.path.join(HERE, "nmplus_parts.json")))
def part_name(field, value, which="nmplus"):
    """what looks value `value` of field `field` (an APP_FIELDS name) is in No Mercy Plus ("stock": in the stock game)"""
    lf = PARTS["field_list"].get(field)
    if lf is None: return None
    return PARTS["lists"][lf][which].get(str(value), "(none)" if value == 0 else "(not in its list)")
COSTUMES = [(RING[0], MASK[0]), (RING[1], MASK[1]), (RING[0], MASK[1]), (RING[1], MASK[0])]
STATES = [(0, 0, 0, 0), (1, 1, 3, 0x20), (1, 0, 3, 0), (0, 1, 2, 0x20)]     # (gen/mkovr.py: the states the game builds in)
GLOB_N = (0x800A9EA0, 0x800931E4, 0x80099508)
KEEP_OURS = set(range(0x254, 0x258))

class NMPlus:
    def __init__(self):
        self.files = RT.RomFiles(NMPLUS_ROM, "nmplus_v3")
        self.ram = open(NMPLUS_RAM, "rb").read()
        sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
        from mipsemu import Emu
        self.Emu = Emu
        n = 0x3FC00                    # (its code in the RAM image is its ROM's, word for word -- checked here every run)
        assert self.ram[0x400:0x400 + n] == self.files.rom[0x1000:0x1000 + n], "the RAM image isn't this No Mercy Plus"
        if not os.path.exists(REMAP):
            raise MTB.ModTableChanged("No Mercy Plus: no remap at %s -- run tool\modcheck.py tables for it first (a mod's "
                                      "part numbers can't be used without it)" % REMAP)
        self.remap = MTB.Remap.load(REMAP)
        if self.remap.d["mod_sha1"] != self.files.sha1:
            raise MTB.ModTableChanged("the remap %s is for another ROM (%s), not this No Mercy Plus (%s)"
                                      % (REMAP, self.remap.d["mod_sha1"][:12], self.files.sha1[:12]))
    def costume_struct(self, o, c):
        a = (NM_STRUCTS + o * 0x50 + c * 0x14) & 0x7FFFFF
        k = ("id", "ordinal", "music", "video", "height", "spare6", "weight", "moves_file", "abilities_file", "appearance", "names", "spare12")
        return dict(zip(k, struct.unpack_from(">HHBBBBHHHHHH", self.ram, a)))
    def name(self, entry):
        f = self.files(2); o = struct.unpack_from(">H", f, 2 * entry)[0]
        return f[o:f.index(b"\0", o)].decode("latin1")
    def appearance(self, e): return self.files(1)[0x26 * e:0x26 * e + 0x26]
    def find(self, name):
        for o in range(1, 100):
            s = self.costume_struct(o, 0)
            try:
                if self.name(s["names"]) == name: return o
            except Exception: pass
        raise SystemExit("No Mercy Plus has no %s" % name)
    def roster_names(self):
        out = []
        for o in range(1, 100):
            try: out.append(self.name(self.costume_struct(o, 0)["names"]))
            except Exception: out.append(None)
        return out
    def model(self, un, st):
        """its builder on the looks struct, in one of the game's states -> the 0x270-byte descriptor (gen/mkovr.py nm_model)"""
        E = self.Emu(self.ram)
        for a, v in zip(GLOB_N, st[:3]): E.w8(a, v)
        E.wbytes(0x80781000, bytes(un) + b"\0"); E.wbytes(0x80782000, bytes(0x270)); E.w8(0x80782000 + 0x254, st[3])
        E.call(BUILD, [0x80781000, 0x80782000], max_steps=3_000_000)
        return E.rbytes(0x80782000, 0x270)

def un_of(app):
    un = bytearray(0x3F)
    for s, d in SRC2UN.items(): un[d] = app[s]
    return bytes(un)

# (2026-11-13, modcheck) a field set to a number the handler KNOWS from the stock game -- 0 for "none", say -- is a claim
# that the number means in this mod what it means in the game: set_app_stock checks each one against the remap's parts
# probe and stops if the mod builds something else there. Numbers read from the mod itself (its Test's looks, its Mall's
# packs) are the mod's own and need no check: pass those to set_app as they are.
def set_app_stock(remap, base, why, **kw):
    for k, v in kw.items():
        remap.stock("parts." + k, v, "%s: %s = %d" % (why, k, v))
    return set_app(base, **kw)

def set_app(base, **kw):
    b = bytearray(base)
    for k, v in kw.items(): b[APP_FIELDS.index(k)] = v
    return bytes(b)

def goldust_apps(nmp_):
    """his four costumes' appearance entries, in No Mercy Plus's numbers: its Test's (body, skin, head, hair) with the
    Goldust packs, and nothing else on (no top, pads, bands, gloves, entrance attire)

    (2026-11-13, after the user's "Goldust looks way off": the upper body was 0x7D, the game's own "none" -- its upper-body
    table has nothing at 125-127 -- but No Mercy Plus widened that table and put real tops there: its 125 is MIDEON's
    T-shirt (its Create-a-Wrestler list 0x4A65 names it), which covered the Goldust suit's chest and arms with a black shirt
    and Mideon's green eye. No Mercy Plus's "None" is 0, as its Test wears. Seen in No Mercy Plus itself, in the rig
    (rig\\nm.py, cap\\nmgdA_c*.png with 0x7D: the same shirt; nmgdB_c*.png with 0: the whole gold-and-black suit).)

    (2026-11-13, the user again: "long hair and Mideon's shirt -- looks like the mod replaced some CAW items". Every field is
    now read through No Mercy Plus's own lists (nmplus_parts.json, scripts\\mk_caw_tables.py): Test's hair is "Long 2" (12)
    with front hair "Back 13" (21), his own long blond hair, which hung out under the Goldust face paint. Goldust in 1999-2000
    -- Badd Blood's Goldust shows it (rig cap\\bbgd0.png) -- has short slicked blond hair, and No Mercy Plus's Goldust paint
    covers the whole head: hair "Short 1-A" (2), no front hair (0). In No Mercy Plus's own select, every short hair looks
    the same under the paint (cap\\hv_*.png); only the long ones hang out.)"""
    base = nmp_.appearance(nmp_.costume_struct(nmp_.find("TEST"), 0)["appearance"])
    upper = int(os.environ.get("MODCHECK_OLD_UPPER", "0"), 0)     # (modcheck's negative test: 0x7D, the old value)
    own = dict(hair=HAIR, front_hair=FRONT_HAIR)
    if os.environ.get("MODCHECK_OLD_HAIR"): own = {}             # (modcheck's negative test: the hair left to the base, as at first)
    out = []
    for ring, mask in COSTUMES:
        # his packs' parts and his hair: No Mercy Plus's own numbers (its Mall, file 0x4AA9; its lists by name) -- not stock claims
        a = set_app(base, ring=ring, mask=mask, boots=BOOTS, **own)
        # nothing else on: "none" / "plain" as the game's numbers mean them -- each checked against the mod's tables (modcheck)
        a = set_app_stock(nmp_.remap, a, "goldust: nothing else on", ring_c1=0, ring_c2=0, upper=upper, upper_c1=0, upper_c2=0,
                          entr=0, entr_c1=0, entr_c2=0, weapon=0, gloves=0, gloves_c=0, tattoo=0, wrist=0, wrist_c=0,
                          lelbow=0, lelbow_c=0, relbow=0, relbow_c=0, lknee=0, lknee_c=0, rknee=0, rknee_c=0, boots_c1=0,
                          boots_c2=0, facial_hair=0, hat=0, hat_c=0)
        check_from_base(base, a, BASE_KEEPS, "goldust")
        out.append(a)
    return base, out

# (2026-11-13, modcheck) A part the handler leaves to the BASE wrestler is a part of the base's on HIM -- the first import
# gave Goldust Test's long hair that way: no table was wrong, the hair was simply Test's. So the fields a costume keeps
# from its base must be named: these are Test's build (body, skin, head shape, face, hair colour) and the photo; any other
# field still the base's, unless set to it on purpose, stops the import.
BASE_KEEPS = ("body", "skin", "head", "face", "hair_c", "photo")
def check_from_base(base, app, keeps, who):
    left = [f for i, f in enumerate(APP_FIELDS) if f not in keeps and app[i] == base[i] and base[i] != 0
            and f not in SET_ON_PURPOSE.get(who, ())]
    if left:
        raise MTB.ModTableChanged("%s: %s still the base wrestler's (%s) -- name each one the costume should take from its "
                                  "base (BASE_KEEPS), or set it" % (who, ", ".join(left),
                                  ", ".join("%s %d" % (f, base[APP_FIELDS.index(f)]) for f in left)))
SET_ON_PURPOSE = {}                  # (who -> fields set to the base's own value on purpose)

def build(nmp_, game_files, report):
    """-> (descriptors, files, fields per costume, keep per costume, apps): his model as No Mercy Plus builds it. A file
    field naming a file this game has the same keeps its number; one naming a file only No Mercy Plus has (or has
    otherwise) gets a file of his folder, with the file after it (a palette's picture is the palette's number + 1)."""
    ff = set(pickle.load(open(os.path.join(DEV, "data", "gen_data", "model_file_fields.pkl"), "rb")))
    base, apps = goldust_apps(nmp_)
    def ours(v):
        if v < 0x800 or v >= 0x7802: return False
        try: b = nmp_.files(v)
        except Exception: return False
        if not b: return False
        g = game_files(v) if v <= game_files.count() else None
        return g != b
    descs, keeps, named = [], [], set()
    for c, app in enumerate(apps):
        un = un_of(app)
        ms = [nmp_.model(un, st) for st in STATES]
        vary = set(k for k in range(0x270) if len(set(m[k] for m in ms)) > 1) - KEEP_OURS
        d = ms[0]
        for k in range(0, 0x270, 2):
            v = struct.unpack_from(">H", d, k)[0]
            if k in KEEP_OURS or k in vary or not ours(v): continue
            assert k in ff, "costume %d: %04x at %x, not a file's place" % (c, v, k)
            named.add(v)
        descs.append(d); keeps.append(sorted(KEEP_OURS | vary))
    want = set()
    for v in named:
        want.add(v)
        if ours(v + 1): want.add(v + 1)
    order = sorted(want); index = {v: i for i, v in enumerate(order)}
    fields, out = [], []
    for c, d in enumerate(descs):
        d = bytearray(d); fl = []
        for k in range(0, 0x270, 2):
            v = struct.unpack_from(">H", d, k)[0]
            if k in KEEP_OURS or k in keeps[c] or v not in index or k not in ff: continue
            fl.append([k, index[v]]); struct.pack_into(">H", d, k, 0)
        out.append(bytes(d)); fields.append(fl)
    report.append("goldust: No Mercy Plus's model, %d costumes, %d of its files (%s)" % (len(out), len(order),
                  " ".join("%04x" % v for v in order)))
    return out, [nmp_.files(v) for v in order], order, fields, keeps, base, apps

def tame(app, photo, remap=None, log=None):
    """the looks the game itself reads beside the folder model (gen/mkparts.py tame): No Mercy Plus's parts past the game's
    tables put to the game's of the same kind, so the game's own model of him (made first, then replaced) is sound

    (2026-11-13, modcheck) with the remap: every value kept from No Mercy Plus's looks is the MOD's number, used here as the
    game's -- so each goes through remap.to_stock: kept if the mod builds the same with it, else the game's number that
    builds the same, else (a part only the mod has) the game's plain 0 for it, said in the log -- his own model is drawn,
    not this; but the body type (his belt, his hands) and the skin must mean the same, so those two stop instead."""
    b = bytearray(app)
    set_here = set()
    if b[2] >= 0x80: b[2] = 0; set_here.add(2)    # ring attire: the game's first (the model has his own)
    if b[5] >= 0x7D: b[5] = 0x7D; set_here.add(5)  # upper body: none
    if b[0x1D] >= 0x76: b[0x1D] = 0; set_here.add(0x1D)
    if b[0x19] >= 64: b[0x19] = 1; set_here.add(0x19)    # boots: past the game's 6 bits
    b[0x1E] = 2; b[0x20] = 0; b[0x21] = 0; b[0x22] = 0    # (hair, front hair, beard, mask: ones the game reads well)
    b[0x25] = photo
    set_here |= {0x1E, 0x20, 0x21, 0x22, 0x25}
    if remap is not None:
        for i, name in enumerate(APP_FIELDS):
            if i in set_here: continue
            try: b[i] = remap.to_stock("parts." + name, b[i], "goldust's looks for the game: %s kept" % name)
            except MTB.ModTableChanged:
                if name in ("body", "skin"): raise
                if log is not None: log.append("tame: %s %d is No Mercy Plus's own (no stock part builds it): the game's 0 "
                                               "in the looks (his own model is what's drawn)" % (name, b[i]))
                b[i] = 0
    return bytes(b)
