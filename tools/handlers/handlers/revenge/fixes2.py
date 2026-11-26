"""The Revenge handler's 2026-11-14 fixes (revfix), for the never-ported Superstars (aki_import.py --all and --refresh):

  skipped(code)       Revenge records that are another man (skip.json: Jackreen and Sister Sherri are Sting in Revenge itself).
  photo(folder)       his select photo RENDERED from his model as his folder has it (tools\\romrender: the user's decision A,
                      romrender 2026-11-18). Before: a crop of Revenge's own Costume Change screen (an emulator screenshot,
                      rig\\cc_all.py), and before that a scale of one of his model's pictures ("his face picture").
  own_slots(...)      the slots that still had the reference's move (The Rock's, Kane's for the big men) after the handler's
                      mapping -- port_rev_moveset keeps the reference's where its vote finds no move, and revlib.py leaves
                      strikes the reference's (a strike is one animation, the library holds pairs) -- given the move of the
                      slot's kind (the moves No Mercy's men hold there: revmoves.slot_pool) whose animation is, byte for byte,
                      one of his own Revenge animations: his Revenge move, which No Mercy has too. The Rock's Rock Punch (188)
                      and Rock Stomps (213 / 214) were what every one of them threw.
"""
import collections, json, os, struct, sys
from .. import DEV, RIG
HERE = os.path.dirname(os.path.abspath(__file__))

def skip_list():
    return {int(k, 16): v for k, v in json.load(open(os.path.join(HERE, "skip.json"), encoding="utf-8"))["codes"].items()}

def skipped(code):
    """the reason he gets no folder, or None"""
    v = skip_list().get(code)
    return None if v is None else "%s: Revenge shows %s" % (v["name"], v["shows"])

# ---- the photo ------------------------------------------------------------------------------------------------------------

def photo(folder):
    """(photo.bin bytes, how): his photo RENDERED from his model as his folder has it (tools\romrender, the user's decision A,
    romrender 2026-11-18 -- was a crop of Revenge's Costume Change screen, rig\cc_all.py), or (None, why not)"""
    from ..aki import photo as PH
    return PH.photo_rendered(folder)

# ---- his own moves where the reference's were left -----------------------------------------------------------------------
_ctx = {}
def _context():
    if not _ctx:
        sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
        sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))     # (tools\handlers: revlib.py)
        import revlib as RL
        RM, K = RL.RM, RL.K
        pool = RM.slot_pool(K.CAN)
        first = {}
        for s, ms in pool.items():
            for m in ms:
                if 0 <= m < len(RM.NM1) and RM.NM1[m] > 0: first[m] = RM.nmhash(RM.NM1[m] + 0x3322)
        derived = set()
        for e, n, st in RL.derived_table():
            for k in range(1, n + 1): derived.add(e + k)
        groups = {e: [e + k for k in range(1, n + 1)] for e, n, st in RL.derived_table()}
        _ctx.update(RL=RL, RM=RM, K=K, t=RL.tables(), pool=pool, first=first, pos=RM.slot_positions(RM.PAIRS),
                    derived=derived, groups=groups, ref={o: K.van_unpacked(o)[0] for o in (1, 4)})
    return _ctx

def own_slots(slots, rev_file, ref_ord):
    """slots: his moves.json slots (move or library ID, three flags). Each slot whose move is still the reference's -> the
    move of the slot's kind whose first animation is one of his Revenge list's, byte for byte (the voted place's code first,
    then a code's own first animation, then the lowest move). A continuation entry the game derives (e + 1 ..) is left, and
    so is a slot whose continuations name library moves made from its move. -> (new slots, [(slot, old, new, his code)],
    [slots left the reference's: no such move of his])"""
    c = _context()
    RM, t, pool, first, pos = c["RM"], c["t"], c["pool"], c["first"], c["pos"]
    # (2026-11-15, womenfix: any reference -- a woman's is Stephanie McMahon, ordinal 51 -- not only The Rock's and Kane's)
    if ref_ord not in c["ref"]: c["ref"][ref_ord] = c["K"].van_unpacked(ref_ord)[0]
    ref = c["ref"][ref_ord]
    rl = RM.revlist(rev_file)
    his = collections.defaultdict(list)             # (his animation's content -> [(code, list place)])
    for code in range(len(t) - 1):
        for q in range(t[code], t[code + 1]):
            if rl[q]: his[RM.revh.get(RM.ranim(rl[q]))].append((code, q))
    out = [list(s) for s in slots]
    changed, left = [], []
    for s in range(len(out)):
        m0 = out[s][0]
        if isinstance(m0, str) or m0 in (0, 0xFFFF) or m0 != ref[s][0] or s in c["derived"]: continue
        if any(isinstance(out[d][0], str) for d in c["groups"].get(s, ())): continue
        cand = []
        for m in sorted(pool.get(s, ())):
            h = first.get(m)
            for code, q in his.get(h, ()) if h else ():
                cand.append((0 if (s in pos and pos[s][0] == q) else 1, 0 if q == t[code] else 1, m, code))
        if not cand: left.append(s); continue
        if any(x[2] == m0 for x in cand): continue          # (the reference's move is his own too: his Revenge animation)
        cand.sort()
        m = cand[0][2]
        if m != m0:
            out[s][0] = m
            changed.append((s, m0, m, cand[0][3]))
    return out, changed, left

# ---- a slam the game would make a struggle (2026-11-14, revfix) -----------------------------------------------------------
# Revenge's body slam is, animation for animation, No Mercy's 960 -- but No Mercy's 960 is the slam a wrestler struggles
# through on a man too big for him (it goes on to 961, the struggle, and its later steps 966 / 968 have no animation), so a
# strong grapple slot given 960 made every slam a struggle (the user on the ports, 2026-10-30: "every strong grapple looked a
# struggle"; gen/mkroster.py REV_OVERRIDE swapped the 24 ports' to the Back Body Flip by hand). For every Revenge man now: his
# own Revenge slam -- its animation pair in the built-in Revenge library -- played on a strong grapple that ends as Revenge's
# does, No Mercy's Back Body Flip (16250: no struggle, its counter and block at 16252 / 16254), the carrier the ports' fix
# chose. The library ID is found by the pair's content in content\movelib_revenge.bin (the one the 0.82 game reads).
STRUGGLE = range(960, 970)
CARRIER = 16250
_lib = {}
def library_ids(path=None):
    """{(attacker bytes, defender bytes): library ID} for the library's pair moves without a hold -- each animation's bytes
    after the first (2026-11-20, revmoves2: the library's first byte is the No Mercy twin's where it has one -- movelib_rev.py
    xref -- so a pair is found by the rest, which both games' files share; the same IDs as by all the bytes: 581 of 581)"""
    if _lib: return _lib
    p = path or os.environ.get("REV_MOVELIB") or os.path.join(DEV, "mod", "build", "movelib_revenge.bin")
    b = open(p, "rb").read()
    u32 = lambda a: struct.unpack_from("<I", b, a)[0]
    assert b[:8] == b"NMMOVLIB" and u32(8) == 1, p
    n, na, sbytes = u32(12), u32(16), u32(24)
    mv, an = 32, 32 + 16 * n
    st = an + 44 * na; da = st + sbytes
    anim = lambda k: b[da + u32(an + 44 * k): da + u32(an + 44 * k) + u32(an + 44 * k + 4)]
    for i in range(n):
        at = mv + 16 * i
        name, pair, block = u32(at), u32(at + 8), u32(at + 12)
        if pair == 0xFFFFFFFF or block != 0xFFFFFFFF: continue
        e = b.index(b"\0", st + name)
        _lib.setdefault((anim(pair)[1:], anim(pair + 1)[1:]), b[st + name:e].decode())
    return _lib

def no_struggle(slots, rev_file):
    """slots -> (new slots, [(slot, old, new)]): each strong grapple slot (a base of the game's derived groups) holding a
    960-969 struggle slam -- as a number, or a library ID played on it -- gets his own Revenge pair at the slot's place in
    his list, on the carrier; its derived entries left to the game (they had none of their own)."""
    c = _context()
    RL, RM, t, pos = c["RL"], c["RM"], c["t"], c["pos"]
    rl = RM.revlist(rev_file)
    AN = c.setdefault("AN", RL.Anims())
    lib = library_ids()
    out = [list(s) for s in slots]
    changed = []
    for s, kids in c["groups"].items():
        v = out[s][0]
        on = int(v.split("@")[1].split("/")[0]) if isinstance(v, str) and "@" in v else v
        if not isinstance(on, int) or on not in STRUGGLE or s not in pos: continue
        if any(isinstance(out[d][0], str) for d in kids): continue
        j = pos[s][0]
        if not (rl[j] and rl[j + 1]): continue
        fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
        if not (AN.ok(fa) and AN.ok(fd)): continue
        mid = lib.get((AN.get(fa)[0][1:], AN.get(fd)[0][1:]))
        if not mid: continue
        out[s][0] = "%s@%d" % (mid, CARRIER)
        changed.append((s, v, out[s][0]))
    return out, changed

# ---- No Mercy's weight class for him (2026-11-15, revfix -- the user's standing rule: imports keep their source game's
# looks, moves, sizes and themes, but the universal mechanics are always No Mercy's) -------------------------------------
# A moveset's abilities 2-4 are No Mercy's weight class: how the engine has him lift, struggle and resist (the user saw it on
# Bret, 2026-10-30: a light class made every strong grapple on a heavier man a long struggle). The handler gave every
# never-ported man his reference's -- The Rock's or Kane's, a heavyweight's (1, 1, 0) -- whatever he weighs: Juventud
# Guerrera at 165 lbs and the women as heavyweights. No Mercy's own men by weight (stock folders' movesets): every one
# under 231 lbs is class 0, almost all (0, 2, 2) (Taka, Scotty, X-Pac, Jeff Hardy, Benoit, Guerrero ...); 231 .. 399 class 1,
# mostly (1, 1, 0); the 400 lbs men (Mark Henry, Rikishi, Viscera) and the biggest (Kane, Undertaker) (1, 0, 0); the women
# (0, 1, 2) (Ivory, Trish, Terri, Stephanie, Tori ...). Andre the Giant's (2, 0, 0) is one man's, not a band. So his class
# is the band his own Revenge weight falls in, as No Mercy would give a man of it (a woman's: body type 15).
def nm_weight_class(lbs, woman=False):
    if woman: return [0, 1, 2]
    if lbs is None: return None                 # (no weight of his own: his base's, whose class he keeps)
    if lbs < 231: return [0, 2, 2]
    if lbs >= 400: return [1, 0, 0]
    return [1, 1, 0]

def repack_class(moveset_hex, cls):
    """his packed moveset with abilities 2-4 = cls (the moves' bytes as they are; checked by the game's own unpacking)
    -> (hex, the old abilities 2-4)"""
    import mkroster as K
    b = bytes.fromhex(moveset_hex)
    slots, ab = K.unpack(K.EV, K.VAN_UNPACK, b[:0xB8], b[0xB8:])
    old = list(ab[2:5])
    ab = list(ab); ab[2:5] = cls
    params = K.pack([tuple(s) for s in slots], ab)[0xB8:]
    nb = b[:0xB8] + params
    s2, ab2 = K.unpack(K.EV, K.VAN_UNPACK, nb[:0xB8], nb[0xB8:])
    assert list(ab2) == ab, (ab2, ab)
    return nb.hex(), old

# ---- his abilities (2026-11-15, revfix -- the user: what Revenge defines, from Revenge; the rest No Mercy's neutral values
# for his weight band, never his reference's) ------------------------------------------------------------------------------
# The handler gave every never-ported man his reference's whole ability block (The Rock's or Kane's): six men stood as Kane
# (ability 1 = 3, the big men's stance), every one had The Rock's or Kane's own style values. Revenge's own block is a
# 64-byte file per man (his moveset file, the first of its three: f), laid out otherwise than No Mercy's 55 abilities and
# not mapped yet -- so for now each ability is No Mercy's neutral value for his band (nm_band_abilities.json: the value most
# of No Mercy's own men of that band have), his weight class the band's (nm_weight_class, the same table), his allies his own.
def band_of(lbs, woman=False):
    if woman: return "woman"
    if lbs is None: return "heavy"          # (no weight on his Revenge record -- a manager: the game gives him his base's,
                                            #  The Rock's 275 lbs, a heavyweight's)
    return "light" if lbs < 231 else ("super" if lbs >= 400 else "heavy")
def band_abilities(lbs, woman, allies):
    """the 55 abilities a man of this band gets (his allies, 31-34, put in)"""
    b = band_of(lbs, woman)
    if b is None: return None
    ab = list(json.load(open(os.path.join(HERE, "nm_band_abilities.json")))["bands"][b]["abilities"])
    if b == "woman": ab[1] = NM_LADY_STANCE     # (2026-11-16, womenfix2: Revenge's women are valets -- see below)
    ab[31:35] = (list(allies) + [0, 0, 0, 0])[:4]
    return ab
# (2026-11-16, womenfix2) Ability 1 is the standing pose, and the Superstar Select and the match play its stance animation:
# load_actor_moveset (0x800E7C18) makes it the actor's flags (0 -> | 8, 1 -> 0x21, 2 -> 0x11, 3 -> 0x81, 4 -> 0x201) and
# actor_animation_file (0x800E609C) the file (0x2DD9 the fighting stance; 4: 0x2DDF, "the ladies who don't wrestle").
# The woman band's value (the most of No Mercy's ten women's) was a 5-5 tie between 0 (Lita, Ivory, Mae Young, Chyna, Tori)
# and 4 (Stephanie, Trish, Terri, Linda, The Kat), which fell to 0: Elizabeth and Kimberly stood in the men's fighting
# crouch, fists up, knees bent -- the user's "deformed" body (short heavy legs, wide torso, fists at chest height). Revenge's
# women (group 9) are its valets -- they don't wrestle -- so they stand as No Mercy's non-wrestling women do.
NM_LADY_STANCE = 4
def repack_abilities(moveset_hex, ab):
    """his packed moveset with these abilities (the moves' bytes as they are; checked by the game's own unpacking) -> (hex, old)"""
    import mkroster as K
    b = bytes.fromhex(moveset_hex)
    slots, old = K.unpack(K.EV, K.VAN_UNPACK, b[:0xB8], b[0xB8:])
    nb = b[:0xB8] + K.pack([tuple(s) for s in slots], list(ab))[0xB8:]
    s2, ab2 = K.unpack(K.EV, K.VAN_UNPACK, nb[:0xB8], nb[0xB8:])
    assert list(ab2) == list(ab) and [tuple(x) for x in s2] == [tuple(x) for x in slots], "the packing changes his abilities"
    return nb.hex(), list(old)

# ---- his winning taunt (2026-11-15, revfix) -------------------------------------------------------------------------------
# Slot 235 is where the winner stands (ovl_e func_800E67F0's phase 4: mid-ring). Several Revenge men's win pose is a
# turnbuckle taunt -- a move No Mercy's men have only in slot 232, the top-turnbuckle taunt -- and played mid-ring he climbs
# an invisible corner (the user on Mr. Perfect, 2026-10-31: the ports had it swapped by hand). For every man now: a 235
# move No Mercy keeps only in 232 gives way to his own standing taunt, his slot 228's (else 229's, 230's).
def win_pose(slots):
    """-> (slots, (old, new) or None)"""
    c = _context()
    RM = c["RM"]
    if "taunt_slots" not in c:
        where = collections.defaultdict(set)
        for o in RM.NM:
            for s in range(224, 236):
                m = RM.NM[o][1][s][0]
                if m not in (0, 0xFFFF): where[m].add(s)
        c["taunt_slots"] = where
    out = [list(s) for s in slots]
    v = out[235][0]
    m = int(v.split("@")[1].split("/")[0]) if isinstance(v, str) and "@" in v else v
    if not isinstance(m, int) or c["taunt_slots"].get(m) != {232}: return out, None
    for s in (228, 229, 230):
        if out[s][0] not in (0, 0xFFFF):
            out[235][0] = out[s][0]
            return out, (v, out[s][0])
    return out, None
