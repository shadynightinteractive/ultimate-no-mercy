"""The WWF WrestleMania 2000 handler (2026-11-12): DESIGNED, NOT WRITTEN YET. What it will do, and where today's code is.

WrestleMania 2000 is the closest of the AKI games to No Mercy (No Mercy's model descriptor is WM2000's with parts added),
and the one where the most can come straight from the ROM without a match capture:
  - roster:   its record table (RAM 0x800411B0, 0x4C records: name, ID, ordinal, weight, moves file, abilities file, music,
              video, four costumes' looks) -- gen/wm2k.py roster(), read from a RAM image of it;
  - model:    WM2000's own model builder (0x800178FC: looks -> descriptor) run in the project's MIPS interpreter on that RAM
              image -- gen/wm2k.py model(). The pictures "as the match draws them" (some are drawn on at load) still come
              from a capture (gen/wmport.py capture(), data/wmport/wmcap.py) -- or from running its picture step the same way;
  - moveset:  gen/mkroster.py port_wm_moveset (WM2000's moveset files -> No Mercy's moves; its 4-list layout is No Mercy's);
  - theme:    its music bank (gen/mkcontent.py wm_theme: the song and its instrument from WM2000's ROM) -- a record's music
              byte is a SLOT of its entrance table (0x8010206C, 12 bytes a slot, the sound at + 8), never the sound itself:
              Jarrett's slot 23 plays sound 22, The Blue Meanie's 30 plays 35 (mkcontent.py wm_slot_sound; the Badd Blood
              handler's theme_sound reads the same table; 2026-11-13);
  - video:    its entrance table's frame lists (gen/wmport.py wm_video: 64 x 64 CI8 frames, the same format as No Mercy's);
  - photo:    its own select photos are the same format as No Mercy's.
Per-wrestler fixes (today: PORTED [("jarrett", 29), ("meanie", 41)], LATE's jarrett / meanie rows -- reference, music
slot, heights) become wrestlers/<string id>.json as revenge/wrestlers does.
The shared part with revenge/: everything in ../aki (descriptor, pool, nm_palettes, the No Mercy skin and hands, photo,
folder). The WM2000 hand pictures (hands.c) are WM2000's data: the WM2000 handler would export them for the AKI hands
feature, and the Revenge handler's finger fix (rev_fingers) takes them from there instead of needing a WM2000 capture.
"""

# ---- (2026-11-12, later the same night) the prototype: the two ports there are, Jeff Jarrett and The Blue Meanie ----------
# As the Revenge handler: the shared AKI core (../aki) does the folder, the pool, the photo; gen/wmport.py's functions are
# called as a library so the folders can be byte-identical to today's baked ports. WM2000's own: its capture (a match's
# RAM: its descriptor in WM2000's layout, each part's picture as WM2000 drew it), the descriptor's change of layout
# (nm_descriptor), its hands' kind (a byte per looks, from its match RAM's table at 0x80041CC4).
import collections, glob, json, os, struct, sys
from .. import DEV, RIG     # (2026-11-13, wm2kall: the Development folder found from here, not written in -- handlers/__init__.py)
sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
HERE = os.path.dirname(os.path.abspath(__file__))
from ..aki import model as M
from ..aki import skintone as ST
from ..aki import handfit as HF      # (2026-11-16, womenfix2 round 2)

def fixes():
    out = {}
    for p in sorted(glob.glob(os.path.join(HERE, "wrestlers", "*.json"))):
        j = json.load(open(p)); out[j["string_id"]] = j
    return out

def photo(fx):
    return open(os.path.join(HERE, "wrestlers", fx["string_id"] + ".photo.bin"), "rb").read()

def build_model(fx, report):
    import wmport as W
    from ..revenge.game import _PoolAsFiles
    pool = M.Pool()
    k = fx["wm2000"]["roster_index"]
    kinds = open(W.nmp("/home/devbox/work/wmport/wm/match_ram.bin"), "rb").read()[0x41CC4:0x41CC4 + 0x40]
    descs = []
    for c in range(4):
        wd, parts = W.capture(W.ensure_capture(k, c), 0)
        nm = bytearray(W.nm_descriptor(wd))
        for o in list(range(8, 0xB0, 8)) + list(range(0xC8, 0xE0, 8)): nm[o:o + 6] = bytes(6)
        for p in parts:
            o = p["at"] if p["k"] <= 20 else 0xC8 + 8 * (p["k"] - 22)
            m = pool.one(W.wfile(p["mesh"]))
            t, q = pool.pair(p["tex"], p["pal"]) if p["tex"] else (0, 0)
            struct.pack_into(">HHH", nm, o, m, t, q)
        nm[W.SKEL_WM_HANDS] = kinds[W.looks0(k, c)]
        descs.append(bytes(nm))
    F = _PoolAsFiles(pool)
    skins = {}         # (his skin tone across his costumes: one with no bare skin takes another's -- nm_skin)
    # (2026-11-15, skinqa: not aki/skintone.py's steps here -- these two are the built-in ports' twins, made exactly as the
    #  build makes them, and p0_wm2000.py proves it; the build's own skin step would take the same change: skinqa's proposal)
    descs = [W.nm_skin(d, F, "%s %d" % (fx["key"], c), report, skins, fx["key"]) for c, d in enumerate(descs)]
    pals = set()
    for d in descs:
        for kk in range(1, 28):
            o = 8 * kk if kk <= 20 else (0xA8 + 8 * (kk - 21))
            q = struct.unpack_from(">H", d, o + 4)[0]
            if pool.ours(q): pals.add(q)
    for q in pals:
        b = pool.data[q - M.POOL0]
        if len(b) < W.NM_PAL_SIZE: pool.data[q - M.POOL0] = bytes(b) + bytes(W.NM_PAL_SIZE - len(b))
    return descs, pool

def moveset(fx, report):
    import mkroster as K
    return K.port_wm_moveset(fx["key"], int(fx["wm2000"]["moves_file"], 16), int(fx["wm2000"]["abilities_file"], 16),
                             fx["moveset"]["reference_ordinal"], K.WM_OVERRIDE, report)

# ---- (2026-11-13, wm2kall) the whole roster ----------------------------------------------------------------------------
# Every Superstar WrestleMania 2000 has -- its records 1-50 (wrestlers, managers, officials, the announcers) and 67-74 (the
# women): 58. (Record 0 and 75 are the game's dummies; 51-66 its sixteen empty Create-a-Wrestler slots, "ORIGINAL A-P",
# no moves file.) Read from the user's own ROM, in place (the .zip is read in memory, nothing unpacked to disk):
#   record    the roster's records are the main segment's static data (RAM 0x80000400 = ROM 0x1000): name (the select
#             screen's markup -- names()), game ID, height, weight, moves file, abilities file, music slot, video, four
#             costumes' looks (0x24 bytes each). The same reading as the Badd Blood handler's, which checked it against
#             gen/wm2k.py roster() on WrestleMania 2000's own RAM image, record for record.
#   model     each costume as WrestleMania 2000 builds it in a match: rig\wm_capture_all.py (its Tag Match, four men a
#             match, the costume forced) -> the descriptor and each part's picture as the match drew them; meshes from
#             this ROM's files; then gen/wmport.py nm_descriptor and aki/skintone.py nm_hands (No Mercy's hands in his
#             own skin; his skin WrestleMania 2000's) -- build_model_caps.
#   body      his looks' byte 8 is WrestleMania 2000's body type, the same scale as No Mercy's Create-a-Wrestler's 0-7
#             (Skinny 1 .. Fat 2): found with its own model builder (0x800178FC, run on looks with that byte changed: the
#             descriptor's 0x249 and the build's bytes 0x113 / 0x117 follow it, no mesh changes -- byte 0, the build,
#             picks the meshes) and seen on the roster: X-Pac, Shawn Michaels, Too Sexy, Too Hot 0; Jericho 2; Austin, The
#             Rock, Triple H, Kane, Jarrett 3; Undertaker, Big Show, Lawler, Patterson 5; Mark Henry 6; Viscera, The Blue
#             Meanie 7 -- No Mercy's own types for the men both games have, near enough (its specials 8-12 aside). The
#             women (byte 0 = 7: a woman's body) get No Mercy's women's type, 15, as the Revenge handler's valets.
#   moveset   gen/mkroster.py port_wm_moveset (WrestleMania 2000's moves -> No Mercy's by the very animation, then by name,
#             else a stand-in or the reference's), given this ROM's moves, abilities, move names and animation files.
#             (2026-11-13, wm2klib) A move No Mercy has no move for (by its animations) is in the built-in WrestleMania 2000
#             library (..\..\wmlib.py: content\movelib_wm2000.bin, from this ROM), and his moves.json names it there --
#             "wm2000:<move>@<No Mercy move>", the No Mercy move the handler chose (or one of the slot's kind that goes on
#             the same way) giving its record; the folder's packed moveset keeps the No Mercy moves.
#   theme     his record's music byte is a SLOT of the entrance table (0x8010206C, in the select screen's RAM); the slot's
#             sound in the music bank, made a folder theme at the rate the game plays it (the Badd Blood handler's
#             theme_folder: themefix2's measurement, 28819 x 2^((tuning + 48) / 12)).
#   video     his record's video: the entrance table's frame list (0x80102070 + 12 v) and its 64 x 64 CI8 frames (No
#             Mercy's own Titantron format) -- 0 is none (the managers, the announcers, two of the women).
#   photo     a crop of his own render on WrestleMania 2000's Superstar Select (rig\wm_sel.py, his first costume), the
#             background taken away by the plate (the same screen without his model) -- aki/photo.py.
# Per-wrestler fixes are data: wrestlers\<string id>.json (his roster index, the No Mercy Superstar his moves are made on,
# and any field set by hand, with its reason).
import hashlib, io, re, zipfile
from ..aki import romtable as RT

GAME = "WWF WrestleMania 2000"
ZIP = os.path.join(DEV, "roms", "WWF_WrestleMania_2000.zip")
MAIN_VRAM, MAIN_ROM, MAIN_END = 0x80000400, 0x1000, 0x144AA0
ROSTER, ROSTER_N = 0x800411B0, 0x4C
HAND_KINDS = 0x80041CC4                  # (a byte per looks build, byte 0: the hands' kind -- static data of the main segment)
REAL = list(range(1, 51)) + list(range(67, 75))
COLLECTION = "WrestleMania 2000"
LOOKS_BODY, LOOKS_BUILD, BUILD_WOMAN = 8, 0, 7
NM_WOMAN = 15

def rom_bytes(path=None):
    """the ROM image: a .z64 as it is, or the one inside a .zip -- read in memory, never unpacked to disk"""
    path = path or ZIP
    if path.lower().endswith(".zip"):
        z = zipfile.ZipFile(path)
        return z.read([n for n in z.namelist() if n.lower().endswith((".z64", ".n64", ".v64"))][0])
    return open(path, "rb").read()

class _Files(RT.RomFiles):
    """aki/romtable.py's reader on ROM bytes already read (from the archive)"""
    def __init__(self, rom, game):
        self.rom = rom; self.sha1 = hashlib.sha1(rom).hexdigest()
        tab, n, base, _ = RT.GAMES[game]
        self.n, self.base = n, base
        self.t = struct.unpack(">%dI" % n, rom[tab:tab + 4 * n]); self.cache = {}

class WM2000:
    def __init__(self, path=None):
        rom = rom_bytes(path)
        if rom[:4] != b"\x80\x37\x12\x40" or rom[0x20:0x34] != b"WRESTLEMANIA 2000   ":
            raise SystemExit("not WWF WrestleMania 2000 (a .z64, big-endian): %s" % (path or ZIP))
        self.files = _Files(rom, "wm2000")
        img = bytearray(0x800000)
        img[MAIN_VRAM & 0x7FFFFF:(MAIN_VRAM & 0x7FFFFF) + MAIN_END - MAIN_ROM] = rom[MAIN_ROM:MAIN_END]
        self.ram = bytes(img)
    def r32(self, a): return struct.unpack_from(">I", self.ram, a & 0x7FFFFF)[0]
    def rb(self, a, n): a &= 0x7FFFFF; return self.ram[a:a + n]
    def cstr(self, a): a &= 0x7FFFFF; return self.ram[a:self.ram.index(b"\0", a)].decode("latin1")
    def roster(self):
        from ..baddblood.game import BaddBlood
        return BaddBlood.roster(self)          # (the same record layout: the Badd Blood handler reads it for both)
    def hand_kinds(self): return self.rb(HAND_KINDS, 0x40)

def names(raw):
    """the select screen's markup -> (full name, short name): text in {} is shown only in the short name, text in <> in both,
    the rest only in the full: "{STONE COLD}STEVE AUSTIN" -> STEVE AUSTIN / STONE COLD, "THE <ROCK>" -> THE ROCK / ROCK,
    "<J>EFF{.}< HARDY>" -> JEFF HARDY / J. HARDY, "{DOUBLE }<J>EFF JARRETT" -> JEFF JARRETT / DOUBLE J (on its select)"""
    full = re.sub(r"\{[^}]*\}", "", raw).replace("<", "").replace(">", "").strip()
    short = "".join(a or b for a, b in re.findall(r"\{([^}]*)\}|<([^>]*)>", raw)).strip()
    return full, short or full

def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")

# ---- (rostertags, 2026-11-21) what he is on the roster, from WrestleMania 2000's own record: its women's records (IDs 0x0Axx),
# its group 9 (IDs 0x09xx: its officials and announcers), its height code 37 for one who doesn't wrestle; else a wrestler. His
# fixes' "class" (with "class_why") wins: the handler's word where the record can't tell (an announcer from an official, an
# owner who wrestles, Chyna among No Mercy's Superstars). wrestler.json "class"; ri_wmimport.cpp wm_class is its twin.
CLASS_FROM = {
 "woman": "WrestleMania 2000's own women's records (67-74, its IDs 0x0Axx)",
 "official": "WrestleMania 2000's own group 9 (its IDs 0x09xx): its officials and announcers, who don't wrestle",
 "manager": "WrestleMania 2000's own non-wrestler's height code (37) on his record, and no weight (600): one who doesn't wrestle",
 "wrestler": "WrestleMania 2000's own wrestlers' records (a height and weight of his own)",
}
def wrestler_class(e, fx):
    """his record e and his fixes fx -> (class, class_from)"""
    if "class" in fx: return fx["class"], fx["class_why"]
    g = e["id"] >> 8
    c = "woman" if g == 0x0A else "official" if g == 0x09 else "manager" if e["height"] == 37 else "wrestler"
    return c, CLASS_FROM[c]

def body_type(e, c=0):
    """(No Mercy body type, why) for his costume c's looks"""
    lk = e["looks"][c]
    if lk[LOOKS_BUILD] == BUILD_WOMAN:
        return NM_WOMAN, "WrestleMania 2000's women's build (looks byte 0 = 7): No Mercy's women's type"
    return lk[LOOKS_BODY], "WrestleMania 2000's body type (looks byte 8 = %d: No Mercy's %s)" % (
        lk[LOOKS_BODY], {0: "Skinny 1", 1: "Skinny 2", 2: "Medium 1", 3: "Medium 2", 4: "Thick 1", 5: "Thick 2", 6: "Fat 1",
                         7: "Fat 2"}.get(lk[LOOKS_BODY], "?"))

def build_model_caps(wm, k, caps, report, key):
    """his four costumes, each a (match RAM, slot) of rig\\wm_capture_all.py's -> (descriptors, pool): the WM2000 port's
    steps (build_model above), with his slot's descriptor and pictures and this ROM's meshes"""
    import wmport as W
    from ..revenge.game import _PoolAsFiles
    pool = M.Pool()
    kinds = wm.hand_kinds()
    looks = wm.roster()[k]["looks"]
    descs = []
    for c, (ram, slot) in enumerate(caps):
        if ram[HAND_KINDS & 0x7FFFFF:(HAND_KINDS & 0x7FFFFF) + 0x40] != kinds:
            raise SystemExit("%s costume %d: the match's hands' kinds table isn't the ROM's" % (key, c))
        wd, parts = W.capture(ram, slot)
        nm = bytearray(W.nm_descriptor(wd))
        for o in list(range(8, 0xB0, 8)) + list(range(0xC8, 0xE0, 8)): nm[o:o + 6] = bytes(6)
        for p in parts:
            o = p["at"] if p["k"] <= 20 else 0xC8 + 8 * (p["k"] - 22)
            m = pool.one(wm.files(p["mesh"]))
            t, q = pool.pair(p["tex"], p["pal"]) if p["tex"] else (0, 0)
            struct.pack_into(">HHH", nm, o, m, t, q)
        nm[W.SKEL_WM_HANDS] = kinds[looks[c][0]]
        descs.append(bytes(nm))
    F = _PoolAsFiles(pool)
    skins = {}
    # (2026-11-15, womenfix: a costume of WrestleMania 2000's women's build -- looks byte 0 = 7, body_type above -- gets No
    #  Mercy's women's hands, not the men's: aki/skintone.py WOMAN_KIT)
    kits = [ST.WOMAN_KIT if looks[c][LOOKS_BUILD] == BUILD_WOMAN else None for c in range(len(descs))]
    # (2026-11-16, skintone2: skinned() -> aki/skintone.py nm_hands -- No Mercy's hands in his own skin; every other part
    #  as WrestleMania 2000 draws it: the user's rule, imports keep their source game's skin tone)
    descs = skinned(descs, F, pool, report, skins, key, kits)
    descs = HF.fit_wrists(descs, pool, report, key, W.nmf)    # (2026-11-16, womenfix2 round 2: aki/handfit.py -- the wrists onto the hands)
    pals = set()
    for d in descs:
        for kk in range(1, 28):
            o = 8 * kk if kk <= 20 else (0xA8 + 8 * (kk - 21))
            q = struct.unpack_from(">H", d, o + 4)[0]
            if pool.ours(q): pals.add(q)
    for q in pals:
        b = pool.data[q - M.POOL0]
        if len(b) < W.NM_PAL_SIZE: pool.data[q - M.POOL0] = bytes(b) + bytes(W.NM_PAL_SIZE - len(b))
    return descs, pool

def head_tone(d, pool):
    """his face's skin colour (the head, parts 4 and 5 -- the parts nm_skin keeps as they are): the mean of its colours
    gen/wmport.py's skin test takes for skin, weighted by how much of the picture each covers; None if none"""
    import wmport as W
    acc, n = [0.0, 0.0, 0.0], 0
    for k in (4, 5):
        t, q = struct.unpack_from(">HH", d, 8 * k + 2)
        if not (t and pool.ours(t) and pool.ours(q)): continue
        pal = pool.data[q - M.POOL0]
        cs = [W._rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(len(pal) // 2)]
        for i in W._pix(pool.data[t - M.POOL0]):
            if i < len(cs) and W._skin(cs[i]): acc = [a + c for a, c in zip(acc, cs[i])]; n += 1
    return tuple(a / n for a in acc) if n else None

def skinned(descs, F, pool, report, skins, key, kits=None):
    """nm_hands on each costume (No Mercy's hands in his skin). A costume showing no bare skin takes his other costumes'
    tone (nm_skin's own rule) -- so those go after the ones that show some; a man with none bare in any costume (Viscera:
    sleeves, gloves) takes his face's (head_tone). (2026-11-13, wm2kall: nm_skin stopped on WrestleMania 2000's Viscera)"""
    import wmport as W
    kits = kits or [None] * len(descs)       # (each costume's No Mercy hands: None a man's, WOMAN_KIT a woman's)
    out, later = [None] * len(descs), []
    for c, d in enumerate(descs):
        try: out[c] = ST.nm_hands(W, d, F, "%s %d" % (key, c), report, skins, key, kits[c])
        except SystemExit: later.append(c)
    if later and key not in skins:
        tone = next((h for h in (head_tone(descs[c], pool) for c in range(len(descs))) if h), None)
        if tone is None: raise SystemExit("%s: no bare skin in any costume, nor in his face: say his tone in his fixes" % key)
        skins[key] = tone
        report.append("%s: no bare skin in any costume -- his face's tone %s" % (key, tuple(int(x) for x in tone)))
    for c in later: out[c] = ST.nm_hands(W, descs[c], F, "%s %d" % (key, c), report, skins, key, kits[c])
    return out

_files_set = None
def use_rom_files(wm):
    """gen/wm2k.py's and gen/wmanim.py's WrestleMania 2000 files (the project's extraction) swapped for this ROM's own: its
    moves, move names (0x94-0x97), animation index (0x3AE / 0x3AF) and animations (wmanim's hashes). -> how many of the
    animation files differ from the extraction's (0 for the user's clean ROM)"""
    global _files_set
    import wm2k, wmanim
    if _files_set is wm: return 0
    wm2k.wfile = wm.files
    wm2k.WM_NAMES = wm2k._by_move(wm2k._names((0x95, 0x96, 0x97), wm.files), wm.files(0x94))
    differ = 0
    for f in range(0x1609, 0x2848):
        if f in wmanim.wm_h:
            b = wm.files(f); h = wmanim.wm_h[f]
            if len(h) == 3:
                n = (hashlib.md5(b).hexdigest(), len(b), b[:4])
                differ += n != tuple(h); wmanim.wm_h[f] = n
    wmanim.WM1 = [struct.unpack_from(">h", wm.files(0x3AE), o)[0] for o in range(0, len(wm.files(0x3AE)), 2)]
    wmanim.WM2 = [struct.unpack_from(">h", wm.files(0x3AF), o)[0] for o in range(0, len(wm.files(0x3AF)), 2)]
    _files_set = wm
    return differ

def moveset_of(wm, e, ref_ord, report, key):
    """his moves and abilities files -> No Mercy's 292 slots and 55 abilities (gen/mkroster.py port_wm_moveset), with the
    moves No Mercy has that its rule wouldn't place put in their slots (place_own, 2026-11-13, movesteps)"""
    import mkroster as K
    use_rom_files(wm)
    out, ab = K.port_wm_moveset(key, e["moves"], e["abilities"], ref_ord, K.WM_OVERRIDE, report)
    return place_own(wm, e, out, report, key), ab

# (2026-11-13, movesteps) The slot rule. port_wm_moveset puts a WrestleMania 2000 move in its slot as the No Mercy move with the
# very same animations (every one of its ten steps: wmanim.nm_move) only when that move "fits" the slot -- it is in one of
# the slot's four lists (what No Mercy's packing can say there), or its number lies between the lowest and highest of
# them -- else it went to a move of the same name, a stand-in or his reference's. But the packing's lists are what the
# game's menus offer, not what its engine plays: the match plays whatever move an entry holds (mod moveset_unpack.h: a
# folder's list gives any move to any slot), and WrestleMania 2000 -- the same engine, the same 292 slots, the same tables
# -- plays this very move from this very slot. So the move goes in its slot when the game allows it there, that is when
# No Mercy's record of it says what WrestleMania 2000's does of how the slot comes to it and goes on from it:
#   - its kind (record byte 3: equal in the two games' records on 99.7% of the moves both have -- wmlib.record_map), and
#   - the way it goes on (flags & 0x1E00, for its first step and the step after: a hold's grab still goes to a hold);
#   - and the slot isn't one the game fills with its own move for everyone (one move in all its lists: 255's 12130).
# Damage and timing may differ between the two games' records: the folder's moves.json "records" give it WrestleMania 2000's
# (wmlib.py). A move that can't go in is said in the report line ("not placed: ..."), and keeps what it had.
GOES_ON = 0x1E00
PLACED_WAS = {}   # (string id -> {slot: the slot as the old rule had it}: for the packed moveset, wm2k_import.py)
def place_own(wm, e, out, report, key):
    PLACED_WAS.pop(key, None)
    import mkroster as K, wm2k, wmanim
    ws, _ = wm2k.unpack_moves(wm.files(e["moves"]), wm.files(e["abilities"]))
    flags_at = {}
    for i0, (m, f2, f3, f4) in enumerate(ws):
        if i0 in K.WM_BLOCK and i0 not in K.WM_SLOT: continue
        flags_at[K.WM_SLOT.get(i0, i0)] = (f2, f3, f4)
    wrec = wm.files(0x3AD)
    nrec = open(wmanim.nmp("/tmp/devbox-0/nmfiles/f/01ef.bin"), "rb").read()
    def wr(m):
        r = wmanim.WM2[m] if 0 <= m < len(wmanim.WM2) else 0
        return wrec[32 * r:32 * r + 32] if r > 0 else None
    def nr(m):
        r = wmanim.NM2[m] if 0 <= m < len(wmanim.NM2) else 0
        return nrec[36 * r:36 * r + 36] if r > 0 else None
    def goes(rec, at): return bool(rec and struct.unpack_from(">I", rec, at)[0] & GOES_ON)
    def shape_wm(m): return (goes(wr(m), 0x1C), goes(wr(m), 0x1C) and goes(wr(m + 1), 0x1C))
    def shape_nm(m): return (goes(nr(m), 0x20), goes(nr(m), 0x20) and goes(nr(m + 1), 0x20))
    out = list(out)
    hows = collections.Counter()
    for n, l in enumerate(report):
        if not l.startswith("  %s slot " % key) or "WM2000" not in l: continue
        how = l.rsplit("(", 1)[1].rstrip(") \n")
        if how not in ("name", "stand-in", "reference"): continue
        i = int(l.split(" slot ")[1].split(":")[0]); m = int(l.split("WM2000")[1].split()[0])
        c = wmanim.nm_move(m)[0]
        if not c: continue
        lists = [x for x in K.CAN.get(i, ()) if x not in (0, 0xFFFF)]
        why = []
        if len(lists) <= 1: why.append("the slot holds one move for everyone in No Mercy")
        ok = []
        for x in c:
            if not nr(x) or not wr(m): why.append("%d has no record" % x); continue
            if nr(x)[3] != wr(m)[3]: why.append("No Mercy's %d is another kind of move (record byte 3: %d, WrestleMania 2000's %d)" % (x, nr(x)[3], wr(m)[3])); continue
            if shape_nm(x) != shape_wm(m): why.append("No Mercy's %d goes on otherwise" % x); continue
            ok.append(x)
        if len(lists) <= 1 or not ok:
            report[n] = l.rstrip() + " -- not placed: " + "; ".join(sorted(set(why)))
            continue
        x = ok[0]
        PLACED_WAS.setdefault(key, {})[i] = tuple(out[i])     # (what the old rule gave: the packed moveset keeps it)
        out[i] = (x,) + tuple(flags_at.get(i, out[i][1:]))
        report[n] = "  %s slot %3d: WM2000 %5d %-34s -> %5d %-28s (animation, placed: No Mercy's lists hold it in other slots only; was %d by %s)" % (
            key, i, m, wm2k.WM_NAMES.get(m - m % 10, ""), x, K.vnames.get(x - x % 10, wm2k.NM_NAMES.get(x - x % 10, "")), int(l.split("->")[1].split()[0]), how)
        hows[how] += 1
    if hows:
        for n, l in enumerate(report):
            if l.startswith("  %s: {" % key):
                d = eval(l.split(": ", 1)[1])
                for h, k in hows.items(): d[h] -= k
                d["animation"] = d.get("animation", 0) + sum(hows.values())
                report[n] = "  %s: %s" % (key, {h: k for h, k in d.items() if k})
    return out
