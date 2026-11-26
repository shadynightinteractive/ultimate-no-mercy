"""The WCW/nWo Revenge handler (2026-11-12, prototype): where Revenge keeps its wrestlers, and how one becomes a standard
wrestler folder. Shared AKI work (the file table, models, pictures, photos, folders) is in ../aki; what is one man's
alone is in wrestlers/<string id>.json (the per-wrestler fixes, as data).

Inputs, all made from the user's own ROMs:
  - the Revenge ROM (its files: meshes, animations, movesets) -- aki.romtable;
  - a boot state's RAM (Revenge's roster tables are built at boot: names, heights, weights, the costume table);
  - per costume, a capture of a Revenge match with him in it (the model as Revenge builds it, its colours applied):
    captures.json says how each is made in the rig from the ROM. Today's are the ports' research captures;
  - the No Mercy ROM's own files (No Mercy's hand meshes and hand picture: aki/skintone.py nm_hands);
  - WrestleMania 2000's finger picture (Revenge's own fingers came out striped: gen/wmport.py rev_fingers).
PROTOTYPE NOTE: the model steps call gen/wmport.py's functions (rev_capture, ci8, mirror, rev_fingers, nm_skin) and the
moveset steps gen/mkroster.py's (port_rev_moveset, pack, unpack) as a library, unchanged, so a folder made here can be
byte-identical to today's baked port; the migration moves them into aki/.
"""
import glob, json, os, re, struct, sys
from .. import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
HERE = os.path.dirname(os.path.abspath(__file__))
from ..aki import model as M
from ..aki import skintone as ST      # (2026-11-15, skinqa)
from ..aki import handfit as HF      # (2026-11-16, womenfix2 round 2)
from ..aki import facefit as FF      # (2026-11-16, womenfix2 round 4: per-wrestler model fixes as data)

BOOT_RAM = os.path.join(DEV, "data", "revport", "attires", "cap", "v_boot_ram.bin")
MOVESETS_PKL = os.path.join(DEV, "data", "revport", "rev_movesets.pkl")

def fixes():
    """string id -> his fixes (wrestlers/*.json)"""
    out = {}
    for p in sorted(glob.glob(os.path.join(HERE, "wrestlers", "*.json"))):
        j = json.load(open(p)); out[j["string_id"]] = j
    return out

# (2026-11-13, modcheck) This handler reads Revenge's tables by the CLEAN game's knowledge (its roster at 0x8003A594, its
# body words, its costume table, files by number). A modded Revenge -- the installed game folder has a "Revenge Redux"
# archive -- may have moved or changed any of them, so: the ROM (REVROM, else the user's in roms\revenge) must be the clean
# USA one, and the RAM image this reads must be that ROM's (its main code word for word); a mod needs its remap first
# (tool\modcheck.py tables --family revenge: it diffs the file table and files) and a handler of its own.
CLEAN_SHA1 = "e1711a2511394b9357b5f1ac8ca5cc17bd674836"     # WCW/nWo Revenge (USA), as the handlers' files were made from
def check_clean(ram):
    from ..aki import modtables as MTB
    p = os.environ.get("REVROM") or os.path.join(DEV, "roms", "revenge", "WCW-nWo Revenge (USA).z64")
    rom = MTB.read_rom(p)
    import hashlib
    if hashlib.sha1(rom).hexdigest() != CLEAN_SHA1:
        raise MTB.ModTableChanged("%s isn't the clean WCW/nWo Revenge (USA) this handler's tables are known for: a mod? run "
                                  "tool\\modcheck.py tables --family revenge for it, and give it a handler" % os.path.basename(p))
    if ram[0x400:0x400 + 0x10000] != rom[0x1000:0x1000 + 0x10000]:
        raise MTB.ModTableChanged("the RAM image isn't this Revenge's (its main code differs from the ROM's)")

# ---- the roster, as Revenge builds it at boot ---------------------------------------------------------------------
def roster(ram_path=BOOT_RAM):
    """every Revenge Superstar: code (the model code its loader takes), name, short name, height (inches over 5'0"),
    weight (lbs - 100), his four costumes (attire, mask, colour rows), his moveset file (his files f..f+2) and his body
    (2026-11-13, revbody: body.py -- Revenge's frame and scale for him, from its body word)"""
    import pickle
    from . import body as B
    r = open(ram_path, "rb").read()
    check_clean(r)
    bt = B.Tables(r)
    r32 = lambda a: struct.unpack_from(">I", r, a & 0x7FFFFF)[0]
    def cstr(a):
        b = r[a & 0x7FFFFF:(a & 0x7FFFFF) + 40]
        return b.split(b"\0")[0].decode("latin1")
    table = r32(0x8003A594)
    sets = {(g, row): f for g, row, f in pickle.load(open(MOVESETS_PKL, "rb"))[0]}
    out = []
    for g in range(11):
        p, n = r32(0x8003A4F8 + 8 * g), struct.unpack_from(">H", r, (0x8003A4FC + 8 * g) & 0x7FFFFF)[0]
        for i in range(n):
            rec = r32(p + 4 * i)
            code = struct.unpack_from(">H", r, rec & 0x7FFFFF)[0]
            ix = r[(rec & 0x7FFFFF) + 2]
            if code >> 8 >= 10: continue                     # (groups 10..: managers and the like)
            full = cstr(r32(rec + 8))
            # (Revenge's own marks: <...> the short name's letters, {...} put in the short name only -- "<S>cott{.}< Steiner>"
            #  is Scott Steiner, S. Steiner; "Diamond Dallas Page{DDP}" is DDP)
            name = re.sub(r"\{[^}]*\}", "", full).replace("<", "").replace(">", "").strip()
            short = "".join(a or b for a, b in re.findall(r"<([^>]*)>|\{([^}]*)\}", full)).strip() or name
            h = re.match(r"(\d+)'(\d+)", cstr(r32(rec + 12)))
            w = re.match(r"(\d+)", cstr(r32(rec + 16)))
            cost = []
            for c in range(4):
                e = (table & 0x7FFFFF) + 42 * (4 * ix + c)
                b = r[e:e + 42]
                cost.append(dict(mask=b[36], attire=b[37], colours=b[38:41].hex()))
            hv = (int(h.group(1)) - 5) * 12 + int(h.group(2)) if h else None
            wv = int(w.group(1)) - 100 if w else None
            if hv is not None and not 0 <= hv <= 255: hv = None      # (managers and the like: 0'0", 0 lbs -- his base's then)
            if wv is not None and wv < 0: wv = None
            out.append(dict(code=code, index=ix, name=name, short=short,
                            height=hv, weight=wv, costumes=cost,
                            moveset_file=sets.get((code >> 8, (code & 0xFF) - 1)), body=bt.body(code)))
    return out

def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")

# ---- (rostertags, 2026-11-21) what he is on the roster, from Revenge's own groups (its roster's group = model code >> 8): 8 its
# managers, 9 its valets (body.py: the people who don't wrestle, frame 4), the rest its wrestlers. wrestler.json "class" (the
# random pools leave out a valet or a manager: mod src/roster.c roster_not_drawn); ri_revimport.cpp rev_class is its twin.
CLASS_FROM = {
 "valet": "Revenge's own group 9 (its model codes 0x9xx): its valets, who don't wrestle",
 "manager": "Revenge's own group 8 (its model codes 0x8xx): its managers, who don't wrestle",
 "wrestler": "Revenge's own wrestlers' groups (its model codes 0x0xx-0x7xx)",
}
def wrestler_class(e):
    """roster entry e -> (class, class_from)"""
    g = e["code"] >> 8
    c = "valet" if g == 9 else "manager" if g == 8 else "wrestler"
    return c, CLASS_FROM[c]

# ---- his model: per costume, from a capture --------------------------------------------------------------------------
_skins = {}          # who -> the skin tone seen on his costumes so far (nm_skin: a costume with no bare skin takes his other's)

class _PoolAsFiles:
    """gen/wmport.py's nm_skin takes its Files: the same numbering, so it gets the pool under that name"""
    def __init__(self, pool): self.p = pool
    @property
    def data(self): return self.p.data
    @property
    def by(self): return self.p.by
    def one(self, b): return self.p.one(b)
    def pair(self, t, q): return self.p.pair(t, q)

def build_model(fx, report, base_fx=None, twin=None, woman=False):
    """his four costumes' descriptors (files numbered by a Pool of his own) -> (descriptors, pool). base_fx: a second
    look's base man (his skin tone is the base's). woman: she gets No Mercy's women's hands, not the men's (2026-11-15,
    womenfix: aki/skintone.py WOMAN_KIT -- Revenge's valets had a man's arms and fists)"""
    import wmport as W
    pool = M.Pool()
    # (WrestleMania 2000's fingers, Jeff Jarrett's: the picture rev_fingers recolours to his skin)
    wm_ref = {p["k"]: (p["tex"], p["pal"]) for p in W.capture(W.ensure_capture(W.PORTED[0][1], 0), 0)[1]}
    descs = []
    for c in range(4):
        cap = fx["costumes"][c % len(fx["costumes"])]
        nm = bytearray(0x270)
        if "got" in cap: got = cap["got"]           # (2026-11-12: a capture rig/capture_all.py made, already read)
        else:
            ram, calls = W.rev_ensure(cap["capture"])
            got = W.rev_capture(ram, calls, cap["slot"])
        for k, (m, tex, pal) in got.items():
            if k in (9, 10, 17, 18) and tex:                  # (his hands: WM2000's meshes' pictures, hands.c)
                tex, pal = W.ci8(tex, pal)
                if k in (9, 17): tex = W.mirror(tex)
                if k in (10, 18):
                    palm_t, palm_p = W.ci8(got[k - 1][1], got[k - 1][2])
                    tex, pal = W.rev_fingers(wm_ref[k][0], wm_ref[k][1], palm_t, palm_p)
            t, q = pool.pair(tex, pal) if tex else (0, 0)
            struct.pack_into(">HHH", nm, 8 * k, pool.one(open(W.REV_FILES % m, "rb").read()), t, q)
        struct.pack_into(">I", nm, W.SKEL_WM_FLAGS, W.SKEL_WM_REV)
        nm[W.SKEL_WM_HANDS] = 0
        struct.pack_into(">I", nm, W.SKEL_MAGIC_AT, W.SKEL_MAGIC_WM)
        descs.append(bytes(nm))
    if fx.get("skin", "no_mercy") == "no_mercy":       # (No Mercy's hands and skin tone: the user's call, 0.35.12)
        who = base_fx["key"] if base_fx else fx["key"]
        F = _PoolAsFiles(pool)
        # (2026-11-16, skintone2: aki/skintone.py nm_hands -- No Mercy's hands in his own skin; every other part as Revenge
        #  draws it: the user's rule, imports keep their source game's skin tone. Not for a built-in port's TWIN (twin=None: his costumes name the build's captures), which stays exactly the build's
        #  model (p0_check.py); twin=False -- aki_import.py --ports-universal, the ports made as folders to replace the
        #  built-ins -- takes the steps like every other man.)
        if twin is None: twin = any("capture" in cap for cap in fx["costumes"])
        try:
            if twin: descs = [W.nm_skin(d, F, "%s %d" % (fx["key"], c), report, _skins, who) for c, d in enumerate(descs)]
            else:
                descs = [ST.nm_hands(W, d, F, "%s %d" % (fx["key"], c), report, _skins, who, ST.WOMAN_KIT if woman else None)
                         for c, d in enumerate(descs)]
                descs = HF.fit_wrists(descs, pool, report, fx["key"], W.nmf)    # (2026-11-16, womenfix2 round 2: aki/handfit.py)
                descs = FF.apply(descs, pool, report, fx["key"])    # (2026-11-16, womenfix2 round 4: model_fixes.json, aki/facefit.py)
        except SystemExit as ex:     # (no bare skin to take a tone from -- a suit, a bodysuit: his own skin and hands, as Goldberg's)
            report.append("%s: %s -- Revenge's own skin and hands kept" % (fx["key"], ex))
    # (No Mercy's palettes are 0x200 bytes a part: the decals run past a short one -- gen/wmport.py nm_palettes)
    pals = set()
    for d in descs:
        for k in range(1, 28):
            o = 8 * k if k <= 20 else (0xA8 + 8 * (k - 21))
            q = struct.unpack_from(">H", d, o + 4)[0]
            if pool.ours(q): pals.add(q)
    for q in pals:
        b = pool.data[q - M.POOL0]
        if len(b) < W.NM_PAL_SIZE: pool.data[q - M.POOL0] = bytes(b) + bytes(W.NM_PAL_SIZE - len(b))
    return descs, pool

# ---- his moveset ------------------------------------------------------------------------------------------------------
def moveset(fx, report):
    """(the 292 slots, abilities): Revenge's list mapped to No Mercy's moves (gen/mkroster.py port_rev_moveset, with his
    fixes' reference, allies, overrides, weight class, stance)"""
    import mkroster as K
    key, mv = fx["key"], fx["moveset"]
    if mv.get("weight_class"): K.REV_WEIGHT[key] = mv["weight_class"]
    else: K.REV_WEIGHT.pop(key, None)
    if mv.get("stance") is not None: K.REV_POSE[key] = mv["stance"]
    else: K.REV_POSE.pop(key, None)
    ov = {key: {int(s): (tuple(v) if isinstance(v, list) else v) for s, v in mv.get("slot_overrides", {}).items()}}
    allies = tuple(int(a, 16) for a in mv.get("allies", []))
    return K.port_rev_moveset(key, int(fx["revenge"]["moveset_file"], 16), mv["reference_ordinal"], allies, ov, report)

def pack_plain(slots, ab, ref_ord):
    """a moveset the game's own unpacking reads without cells: a slot whose move its lists lack gets the reference's.
    -> (0xD8 bytes, how many slots fell back)"""
    import mkroster as K
    rslots, _ = K.van_unpacked(ref_ord)
    fixed = [((m if (i not in K.CAN or m in K.CAN[i]) else rslots[i][0]),) + tuple(s[1:]) for i, s in enumerate(slots) for m in (s[0],)]
    # (a slot the packing can't say -- the game's unpacking gives something else back -- takes the reference's whole entry,
    #  move and flags, and the moveset is packed again, until it comes back as packed)
    for _ in range(4):
        packed = K.pack(fixed, ab)
        back, bab = K.unpack(K.EV, K.VAN_UNPACK, packed[:0xB8], packed[0xB8:])
        bad = [i for i in range(292) if tuple(back[i]) != tuple(fixed[i])]
        if not bad: break
        for i in bad: fixed[i] = tuple(rslots[i])
    n = sum(1 for a, b in zip(fixed, slots) if tuple(a) != tuple(b))
    return packed, n, bad
