#!/usr/bin/env python3
"""move_audit.py (revmoves2, 2026-11-20): every imported library move -- Revenge's and WrestleMania 2000's -- checked against
the No Mercy move it is played on. Reusable: run it again after any importer change.

    python qa\\move_audit.py static <game folder> [--out <report.txt>]
        every slot of every imported man that names a library move (wrestlers\\*\\*\\moves.json "<library>:<move>@<No
        Mercy move>"), from the install's own content\\movelib_revenge.bin / movelib_wm2000.bin and No Mercy's ROM:
        what the slot plays (its animations' end poses, the record's way on) against what No Mercy's own move does there.
    python qa\\move_audit.py scenarios <game folder> <out folder> [--men a,b,...] [--seconds 360] [--speed 8]
        one QA scenario a man (qa.ps1 <out folder>\\audit_<sid>.json -Installed -Lockstep): singles, him (slot 0, CPU)
        against The Rock (CPU), his moves with animations of their own forced one after another wherever the game picks a
        move of the same place (logic.c qa_fsub 3); state.csv's "forced" says which entry each is.
    python qa\\move_audit.py analyze <game folder> <run folder> [<run folder> ...] [--out <report.txt>] [--as <entry>]
        each forced move in those runs: both men's states through it and after it (until both rest: standing, lying, in
        the corner -- or 30 s), against what No Mercy's own move would leave them in. --as: a run whose scenario forces
        its entries as one without qa_fsub (state.csv "forced" then counts the picks without their entry).

Flags (a "mismatch"):
  STAND / LIE   a man ends standing where No Mercy's move leaves him lying, or lying where it leaves him standing (a move
                that ends after its animations: the state after it comes from its last animations' end poses -- state_after_
                animation: 0-2 / 13 / 49 standing (48), 3 staggering (51), 4 rising (50), 7 / 8 / 10 the corner (24 / 52),
                20-47 lying (49), others standing (1));
  KIND          the library's animations go on in another way than the No Mercy move: a pin's (its own move's record
                0x300: the bridge the pin's hold goes on from) on a move that doesn't pin, or the other way; a turnbuckle dive
                with a cover (0x200: its second role the landing into the cover) on a dive without one, or the other way;
  POSE          an animation's end pose (the state after it) isn't of the class of its own last key pose: the class of the No
                Mercy animation whose last key pose is nearest (that predicts No Mercy's own animations' class 99.0% of the
                time) -- standing in the header where the animation ends lying, or the other way: the man snaps up / down;
  ROLES         a move the game asks for more animations than its pair (a dive's five, a double team's three) that has only
                its pair (the rest No Mercy's own: movex_anim);
  KIND on       (2026-11-21, revmoves3) the No Mercy move goes on to another moveset entry by its record (+4, "way on": a
                go-behind 16362 / 16368 to the back grapple, 77) -- the slot's record as his moves.json "records" leave it --
                and a man the library's animations leave lying (or standing) where every No Mercy move that goes on there
                leaves him standing (or lying): the next move would start from a man down. (By the class, not the pose's
                kind: No Mercy's own go-behinds all end in kind 6, the library's that go on well in 0 / 6 / 13 alike.)
  KIND twin     (revmoves3) the library's animations are a No Mercy move's byte for byte but byte 0 (its pair, and its hold
                block when it has one: the same move in both games), and the move it is played on goes on otherwise (None
                ends / "on" goes on to another entry / "hold" / "pin": a hold's animations played as a pin, a move that ends
                played as one that goes on ...) -- his slot's record as his moves.json leaves it. Not flagged, counted
                as checked: a Revenge move whose own move data (its code's byte 15, a hold block: RevengeSays) says it
                ends, on a move that ends; fixes2's no-struggle slams on 16250 (by design);
  STUCK         (analyze) a man not at rest 30 s after the move began;
  SNAP          (analyze) the state after the move isn't what the animations it ended with end in (both men's end poses):
                the move's own -- or, when it went on to another move (its number changed, past its own steps), that move's;
                a dive's attacker by its landing / miss roles (who 1 ..), its man by none of the pair;
  PLACE         (analyze) the two men more than 300 apart, or a height that's neither the mat (90) nor the floor (0), when
                the move is over;
  PLACE on      (analyze, revmoves3) the move went on into the next (85 -> 95: the attacker as a move goes on) with the two
                men more than 80 apart -- the next move's animations are made for the men close (over the 80-man audit of
                2026-11-20 every go-behind that went on well started its back move 8-60 apart, No Mercy's own 12-60; the
                Revenge counters that don't go on in their own game 100-197 apart);
  SNAP on       (analyze, revmoves3) the move went on into the next (85 -> 95) from animations that end with a man lying.
                (Both only where No Mercy's own moves going on to that entry leave both men on their feet: not a slam's
                ground grapple, 236 / 240, which starts at a lying man.)
Set aside (counted, not judged): a reversal (the man turned it on him: at its start, or later -- the two men's roles
swapped while it played), the test's chained steps (its next step forced).
"""
import argparse, collections, csv, glob, hashlib, json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(SRC, "tools", "romimport"))

NM_ANIMS_FILE, NM_RECIX_FILE, NM_RECS_FILE = 0x1F0, 0x1F1, 0x1EF
NM_ANIM0 = 0x3322
LIBS = (("revenge", "movelib_revenge.bin"), ("wm2000", "movelib_wm2000.bin"))

# ---- No Mercy's moves ---------------------------------------------------------------------------------------------------
class NoMercy:
    def __init__(self, game):
        os.environ.setdefault("UNM_ROMS", os.path.join(game, "roms"))
        from roms import Roms
        self.F = Roms().files("nomercy")
        if self.F is None: sys.exit("no No Mercy ROM in %s" % os.environ["UNM_ROMS"])
        self.t1, self.t2, self.recs = self.F(NM_ANIMS_FILE), self.F(NM_RECIX_FILE), self.F(NM_RECS_FILE)
    def s16(self, b, m): return struct.unpack(">h", b[2 * m:2 * m + 2])[0] if 0 <= 2 * m + 2 <= len(b) else 0
    def flags(self, m):
        r = self.s16(self.t2, m)
        return struct.unpack(">I", self.recs[36 * r + 0x20:36 * r + 0x24])[0] if r > 0 else 0
    def file(self, m): return self.s16(self.t1, m) + NM_ANIM0
    def way_on(self, m):
        """the moveset entry move m goes on to (its record's +4: 1880 -> 240, 16368 -> 77), 0 none"""
        r = self.s16(self.t2, m)
        return struct.unpack(">h", self.recs[36 * r + 4:36 * r + 6])[0] if r > 0 else 0
    def goes_on(self, m): return bool(self.flags(m) & 0x1E00) or self.way_on(m) != 0
    def pose(self, f): return struct.unpack(">H", self.F(f)[:2])[0] >> 10
    def pose_by_key(self, key):
        """the end-pose class of the No Mercy animation whose last key pose is nearest key"""
        import numpy as np
        if not hasattr(self, "K"):
            ks, cl = [], []
            for g in range(0x2DD9, 0x2DD9 + 0x169B):
                d = self.F(g)
                n = (len(d) - 4) // 0x62
                if n > 0: ks.append(np.frombuffer(d[4 + 0x62 * (n - 1):4 + 0x62 * n], np.uint8)); cl.append(after_pose(self.pose(g)))
            self.K, self.CL, self.cache = np.array(ks).astype(np.int32), cl, {}
        if key not in self.cache:
            self.cache[key] = self.CL[int(np.abs(self.K - np.frombuffer(key, np.uint8).astype(np.int32)).sum(1).argmin())]
        return self.cache[key]
    def last_step(self, m):
        """the move the chain of continuations from m ends on (flags & 0x1E00: it goes on to m + 1)"""
        for _ in range(8):
            if not self.flags(m) & 0x1E00: return m
            m += 1
        return m

# ---- the libraries as the install has them ------------------------------------------------------------------------------
def read_lib(path):
    """{ID: {"base", "files": [header word of each animation from the pair on], "n": roles, "block": bool}}"""
    if not os.path.exists(path): return {}
    b = open(path, "rb").read()
    u32 = lambda a: struct.unpack_from("<I", b, a)[0]
    n, na, sb, db = u32(12), u32(16), u32(24), u32(28)
    mv, an = 32, 32 + 16 * n
    st = an + 44 * na; da = st + sb
    hdr = lambda k: struct.unpack(">H", b[da + u32(an + 44 * k):da + u32(an + 44 * k) + 2])[0]
    def key(k):
        d = b[da + u32(an + 44 * k):da + u32(an + 44 * k) + u32(an + 44 * k + 4)]
        n = (len(d) - 4) // 0x62
        return d[4 + 0x62 * (n - 1):4 + 0x62 * n] if n > 0 else None
    def body(k):
        d = b[da + u32(an + 44 * k):da + u32(an + 44 * k) + u32(an + 44 * k + 4)]
        return hashlib.md5(d[1:]).digest() if len(d) > 8 else None
    more = {}
    x = da + db
    if b[x:x + 8] == b"NMLIBX01":
        for i in range(u32(x + 8)):
            row, a0, c = struct.unpack_from("<III", b, x + 12 + 12 * i)
            more[row] = (a0, c)
    out = {}
    for i in range(n):
        name, base, ground, pair, block = struct.unpack_from("<IHHII", b, mv + 16 * i)
        e = b.index(b"\0", st + name); mid = b[st + name:e].decode()
        if pair == 0xFFFFFFFF: out[mid] = {"base": base, "files": [], "n": 0, "block": False}; continue
        ix = [pair, pair + 1] + ([more[i][0] + k for k in range(more[i][1])] if i in more else [])
        out[mid] = {"base": base, "files": [hdr(k) for k in ix], "keys": [key(k) for k in ix], "n": len(ix), "block": block != 0xFFFFFFFF,
                    "bodies": [body(k) for k in ix], "block_bodies": [body(block + k) for k in range(8)] if block != 0xFFFFFFFF else []}
    return out

def parse_id(v):
    """"<lib>:<name>[~x]@<m>[/<n>]" -> (library ID, the move the match plays: n if given, else m)"""
    lid, _, at = v.partition("@")
    if not at: return lid, None
    m, _, n = at.partition("/")
    return lid, int(n or m)

RECORDS = {}       # (sid -> {slot: {byte: value}}: moves.json "records", his slots' record bytes changed)
def men(game):
    """(source folder, sid, {slot: ID}) for every man whose moves.json names a library move"""
    for p in sorted(glob.glob(os.path.join(game, "wrestlers", "*", "*", "moves.json"))):
        j = json.load(open(p, encoding="utf-8"))
        ids = {k: s[0] for k, s in enumerate(j.get("slots", [])) if isinstance(s[0], str) and ":" in s[0]}
        RECORDS[os.path.basename(os.path.dirname(p))] = {int(k): {int(b): v & 0xFF for b, v in r.items()} for k, r in j.get("records", {}).items()}
        if ids: yield os.path.basename(os.path.dirname(os.path.dirname(p))), os.path.basename(os.path.dirname(p)), ids

def slot_flags(NM, sid, slot, m):
    """the flags of the record his slot plays: No Mercy's move m's, with his moves.json "records" bytes 0x20-0x23 laid over"""
    b = bytearray(struct.pack(">I", NM.flags(m)))
    for k, v in RECORDS.get(sid, {}).get(slot, {}).items():
        if 0x20 <= k < 0x24: b[k - 0x20] = v
    return struct.unpack(">I", bytes(b))[0]

def slot_way_on(NM, sid, slot, m):
    """the entry his slot's record goes on to (+4): No Mercy's move m's, with his moves.json "records" bytes 4 / 5 laid over
    (the WrestleMania 2000 handler's from its own record, revmoves3's for Revenge's counters that end)"""
    b = bytearray(struct.pack(">h", NM.way_on(m)))
    for k, v in RECORDS.get(sid, {}).get(slot, {}).items():
        if 4 <= k < 6: b[k - 4] = v
    return struct.unpack(">h", bytes(b))[0]

def slot_goes_on(NM, sid, slot, m):
    """his slot's move goes on (a hold, a pin ... by its flags 0x1E00, or to another entry by its record's +4)"""
    return bool(slot_flags(NM, sid, slot, m) & 0x1E00) or slot_way_on(NM, sid, slot, m) != 0

_ON_CLASSES = {}
def on_classes(NM, n):
    """the end-pose classes (attacker's, man's) of every No Mercy move whose record goes on to entry n"""
    if not _ON_CLASSES:
        for m in range(0, len(NM.t2) // 2):
            w = NM.way_on(m)
            f = NM.file(m)
            if w and f != NM_ANIM0:
                _ON_CLASSES.setdefault(w, (set(), set()))
                _ON_CLASSES[w][0].add(after_pose(NM.pose(f))); _ON_CLASSES[w][1].add(after_pose(NM.pose(f + 1)))
    return _ON_CLASSES.get(n, (set(), set()))

# ---- what a move leaves the men in --------------------------------------------------------------------------------------
def after_pose(k):
    """state_after_animation's state for end pose k, as a category: S standing, L lying, R rising, C corner"""
    if k in (0, 1, 2, 13, 49) or k == 3 or k >= 50 or k in (5, 6, 9, 11, 12, 14, 15, 16, 17, 18, 19): return "S"
    if k == 4: return "R"
    if k in (7, 8, 10): return "C"
    if 20 <= k <= 47: return "L"
    return "S"

PIN, COVER = 0x300, 0x200
DIVES = range(12300, 12540)          # (No Mercy's turnbuckle dives, entries 250-274's moves)

def expect(NM, m):
    """what No Mercy's move m does: (goes on: None / "pin" / "hold" / "on", attacker after, man after)"""
    fl = NM.flags(m)
    last = NM.last_step(m)
    on = "pin" if fl & PIN == PIN else "hold" if NM.flags(last) & 0x400 else "on" if NM.goes_on(m) or NM.goes_on(last) else None
    g = NM.file(last)
    return on, after_pose(NM.pose(g)), after_pose(NM.pose(g + 1))

def slot_kind(NM, sid, slot, m):
    """how his slot's move goes on, as expect() says for No Mercy's: None / "pin" / "hold" / "on" -- his record's bytes laid over"""
    fl = slot_flags(NM, sid, slot, m) if slot is not None else NM.flags(m)
    w = slot_way_on(NM, sid, slot, m) if slot is not None else NM.way_on(m)
    if fl & PIN == PIN: return "pin"
    if fl & 0x1E00 and NM.flags(NM.last_step(m)) & 0x400: return "hold"
    return "on" if fl & 0x1E00 or w else None

_TWINS = {}
def twin_moves(NM, L):
    """No Mercy's moves whose animations are the library move's byte for byte but byte 0 (the end pose's numbering): its
    pair, and its hold block when it has one (the move + 1's eight files)"""
    if not _TWINS:
        bod = {}
        for g in range(0x2DD9, 0x2DD9 + 0x169B):
            d = NM.F(g)
            if len(d) > 8: bod[g] = hashlib.md5(d[1:]).digest()
        _TWINS["bod"] = bod
        by = collections.defaultdict(list)
        for m in range(0, len(NM.t1) // 2):
            f = NM.file(m)
            if f != NM_ANIM0 and f in bod and f + 1 in bod: by[(bod[f], bod[f + 1])].append(m)
        _TWINS["by"] = by
    bod = _TWINS["bod"]
    out = []
    for m in _TWINS["by"].get(tuple(L["bodies"][:2]), []):
        if L["block_bodies"]:
            g = NM.file(m + 1)
            if not (NM.flags(m) & 0x1E00) or [bod.get(g + k) for k in range(8)] != L["block_bodies"]: continue
        out.append(m)
    return out

class RevengeSays:
    """(revmoves3) what WCW/nWo Revenge's own move data says a library move does after its animations: its code's 24 bytes
    (the man's Revenge file + 1) byte 15, the code it goes on to (0: none), and a hold block whose loop starts where it ends
    (gen\\revanims.py rev_block) -- his own list's codes holding the pair, else every list's (the developer tree's
    data\\extracted, the folders' "source"."moveset_file"). says() is None when the data isn't there."""
    def __init__(self, game):
        self.ok, self.lists = False, {}
        try:
            os.environ.setdefault("NM_MOD_DIR", os.path.join(SRC, "mod"))
            sys.path.insert(0, os.path.join(SRC, "mod", "gen"))
            import revanims as RA
            ex = os.path.join(SRC, "data", "extracted")
            ram = open(os.path.join(ex, "rev_bm_ram.bin"), "rb").read()
            self.T = [struct.unpack_from(">h", ram, 0x30570 + 2 * i)[0] for i in range(279)]
            rev = lambda f: open(os.path.join(ex, "rev_files", "%04x.bin" % f), "rb").read()
            self.RA = RA
            for p in glob.glob(os.path.join(game, "wrestlers", "Revenge", "*", "wrestler.json")):
                f = json.load(open(p, encoding="utf-8")).get("source", {}).get("moveset_file")
                if f: f = int(f, 16); self.lists[os.path.basename(os.path.dirname(p))] = (struct.unpack(">763H", rev(f + 2)), rev(f + 1))
            self.ok = bool(self.lists)
        except Exception: self.ok = False
    def says(self, sid, lid):
        """"ends" / "goes on" / "hold" (into a hold block) / None, for revenge:<fa>_<fd>"""
        import re
        mm = re.fullmatch(r"revenge:([0-9a-f]+)_([0-9a-f]+)(~[0-9a-f]+)?", lid)
        if not self.ok or not mm or sid not in self.lists: return None
        fa, fd, T = int(mm.group(1), 16) - 0x110B, int(mm.group(2), 16) - 0x110B, self.T
        def codes(rl): return [c for c in range(239) if rl[T[c]] == fa and fd in rl[T[c] + 1:T[c + 1] + 1]]
        where = [(sid, c) for c in codes(self.lists[sid][0])] or [(s, c) for s, (rl, _) in self.lists.items() for c in codes(rl)]
        if not where: return None
        if any(self.lists[s][1][24 * c + 15] for s, c in where): return "goes on"
        if any(self.RA.rev_block(self.lists[s][0], T, c)[0] is not None for s, c in where): return "hold"
        return "ends"

def lib_plays(NM, L, m, sid=None, slot=None):
    """what the library move L played on m does after its pair (None when the move goes on: its continuation decides) --
    his slot's record as his moves.json leaves it, when the slot is given"""
    if (slot_goes_on(NM, sid, slot, m) if slot is not None else NM.goes_on(m)): return None
    return after_pose(L["files"][0] >> 10), after_pose(L["files"][1] >> 10)

CHECKED = collections.Counter()   # (static: what was looked at and found as its source game plays it, by why)
# (a strike's next step: entries 187, 189 .. 203, which the game makes from the strike at 186, 188 .. 202 -- ovl_e's derived
#  table, (186 .. 202, -1, 2). The game plays whatever move is there as a strike (the strike states 34-36), never going on to
#  a hold or another entry: revmoves3's run 2026-11-21_012205 -- Brickowski's 187 on 6852, a move that goes on to its hold --
#  played it in the strike states (36 -> 1) and never went on, as 011701 on 5902, a move that ends. No Mercy's own twins of
#  it, 6902 / 7342, sit in such steps too: 197 / 199 / 203, the +2 of 6900 / 7340 at 196 / 198 / 202.)
STRIKE_STEPS = set(range(187, 204, 2))
def static_audit(game):
    global RS
    NM = NoMercy(game)
    RS = RevengeSays(game)
    libs = {name: read_lib(os.path.join(game, "content", fn)) for name, fn in LIBS}
    rows = []
    for src, sid, ids in men(game):
        for slot, v in sorted(ids.items()):
            lid, m = parse_id(v)
            lib = libs.get(lid.split(":")[0], {})
            L = lib.get(lid) or lib.get(lid.split("~")[0])
            if L is None or m is None or not L["files"]: continue
            flags = []
            on, ea, ed = expect(NM, m)
            lp = lib_plays(NM, L, m, sid, slot)
            w = slot_way_on(NM, sid, slot, m)
            if w and not (slot_flags(NM, sid, slot, m) & 0x1E00):
                # (KIND on: the next move starts from where its animations leave the men)
                for who, h, cls in (("attacker", L["files"][0], on_classes(NM, w)[0]), ("man", L["files"][1], on_classes(NM, w)[1])):
                    c = after_pose(h >> 10)
                    if cls and c in ("S", "L") and c not in cls and cls <= {"S", "L"}:
                        flags.append("KIND on: %d goes on to entry %d, the %s %s at the end (No Mercy's moves that go on there: %s)" % (m, w, who, "lying" if c == "L" else "standing", "/".join(sorted(cls))))
            if lp and on is None:
                if lp[0] != ea: flags.append("attacker %s" % ({"S": "STAND", "L": "LIE"}.get(lp[0], lp[0]) + " (No Mercy: %s)" % ea))
                if lp[1] != ed: flags.append("man %s" % ({"S": "STAND", "L": "LIE"}.get(lp[1], lp[1]) + " (No Mercy: %s)" % ed))
            tw = twin_moves(NM, L) if None not in L["bodies"][:2] else []
            if tw and m not in tw:
                # (KIND twin: the library's animations are No Mercy's own moves' -- the same move in both games -- and the
                #  move it is played on goes on otherwise: a hold played as a pin, a move that ends played as one that goes on)
                kinds = {slot_kind(NM, None, None, t) for t in tw}
                k = slot_kind(NM, sid, slot, m)
                if k not in kinds:
                    # (whatever Revenge does, the user, 2026-11-21: a move Revenge's own data says ends, played on a move
                    #  that ends, is the source game's -- checked, not a fault; fixes2's no-struggle slams on 16250 are the
                    #  handler's on purpose. A slam No Mercy's twin takes to the ground grapple (236 / 240 / 244) stays
                    #  flagged: Revenge's byte 15 doesn't say that way on.)
                    rs = RS.says(sid, lid) if lid.startswith("revenge:") else None
                    ground = {NM.way_on(t) for t in tw} & {236, 240, 244}
                    if k is None and rs == "ends" and not ground: CHECKED["the twin goes on, Revenge's own move ends (as Revenge)"] += 1
                    elif slot in STRIKE_STEPS: CHECKED["a strike's next step: played as a strike whatever the move goes on to (Revenge's own data: %s)" % rs] += 1
                    elif k is None and m == 16250 and lid.startswith("revenge:"): CHECKED["fixes2's no-struggle slam on 16250 (by design; Revenge's own data: %s)" % rs] += 1
                    else:
                        flags.append("KIND twin: its animations are No Mercy's %s (%s), played on %d (%s)%s" % (
                            "/".join(str(t) for t in tw[:4]) + ("..." if len(tw) > 4 else ""), "/".join(sorted(str(x) for x in kinds)), m, k,
                            "; Revenge's own data: %s" % rs if rs else ""))
            bf, mf = NM.flags(L["base"]), slot_flags(NM, sid, slot, m)
            if L["base"] and (bf & PIN == PIN) != (mf & PIN == PIN): flags.append("KIND pin: its own move %d %s, played on %d %s" % (L["base"], "pins" if bf & PIN == PIN else "doesn't pin", m, "pins" if mf & PIN == PIN else "doesn't"))
            if m in DIVES and L["n"] >= 5:
                # (a five-role dive's kind from its roles: the cover kind's second role ends standing with no turn and its
                #  third / fourth lying -- No Mercy's 12420 .. 12530 with 0x200 -- the other kind's second a landing with a turn)
                cover = after_pose(L["files"][2] >> 10) == "L" and after_pose(L["files"][3] >> 10) == "L"
                if cover != bool(mf & COVER): flags.append("KIND dive: its animations a dive %s a cover, played on %d %s" % ("with" if cover else "without", m, "with one" if mf & COVER else "without one"))
            if m in DIVES and L["n"] < 5 and NM.flags(m) & 0x2000: flags.append("ROLES: a dive with only its pair")
            for r, (h, k) in enumerate(zip(L["files"], L["keys"])):
                if k is None: continue
                c, nc = after_pose(h >> 10), NM.pose_by_key(k)
                if c != nc and {c, nc} & {"L"}: flags.append("POSE role %d: ends %s, its last pose %s" % (r, c, nc))
            rows.append((src, sid, slot, v, flags))
    return rows

def cmd_static(a):
    rows = static_audit(a.game)
    bad = [r for r in rows if r[4]]
    kinds = collections.Counter()
    for r in bad:
        for f in r[4]: kinds[f.split(" (")[0].split(":")[0]] += 1
    lines = ["move_audit static: %s" % a.game,
             "library slots %d (%d men); with a mismatch %d" % (len(rows), len({(r[0], r[1]) for r in rows}), len(bad)),
             "by kind: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())),
             "checked, as the source game plays it: " + (", ".join("%s %d" % kv for kv in sorted(CHECKED.items())) or "none") +
             ("" if RS.ok else " (Revenge's own move data not found: nothing checked against it)"), ""]
    for src, sid, slot, v, flags in bad: lines.append("%-17s %-24s %3d %-34s %s" % (src, sid, slot, v, "; ".join(flags)))
    out = "\n".join(lines) + "\n"
    if a.out: open(a.out, "w", encoding="utf-8").write(out)
    print(out if not a.out else "\n".join(lines[:3]))

# ---- the engine run ------------------------------------------------------------------------------------------------------
def cmd_scenarios(a):
    os.makedirs(a.out, exist_ok=True)
    want = set(a.men.split(",")) if a.men else None
    n = 0
    for src, sid, ids in men(a.game):
        if want and sid not in want: continue
        w = json.load(open(os.path.join(a.game, "wrestlers", src, sid, "wrestler.json"), encoding="utf-8"))
        wid = w.get("id")
        s = {"about": "move_audit (revmoves2): %s (%s, %s) against The Rock, his %d library slots' moves forced one after another "
                      "where the game picks a move of the same place (qa_fsub 3); qa\\move_audit.py analyze reads the run" % (sid, wid, src, len(ids)),
             "match": "singles", "wrestlers": [wid, "THE ROCK"], "costumes": [0, 0], "control": ["cpu", "cpu"],
             "arena": "RAW IS WAR", "matches": 0, "seconds": a.seconds, "speed": a.speed, "shots": 0,
             "force": {"slot": 0, "entries": [0, 291], "as": 0}, "options": {"qa_fsub": 3},
             "expect": [{"name": "the match started", "when": "any", "check": "stage == 2 and match_frames != 0xFFFF and match_frames > 30"},
                        {"name": "his moves were forced", "when": "end", "check": "(forced & 0xFFFF) > 0"}]}
        json.dump(s, open(os.path.join(a.out, "audit_%s.json" % sid), "w"), indent=1)
        n += 1
    print("%d scenarios in %s" % (n, a.out))

MOVE_STATES = {85, 86, 89, 90, 91, 92, 93, 94, 95, 96, 99, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
               127, 128, 129, 130, 131, 132, 133, 135, 136, 137, 156, 157, 160, 161, 66, 67, 68, 69, 72, 73, 74, 75, 70, 71, 54, 55}
# (the first state a man is in once the move is over, as a class: standing, or down -- lying, knocked down, or getting up
#  from it (50 rising, 37); 48 goes on by the end pose (X: either); any other state (pulled up, a strike, another move) X; 78, the attacker recovering after his own move, follows No Mercy's own moves ending him standing or lying alike (The Rock's in these runs: 118 / 219): either: the first rest state, not the ones after)
REST = {1: "S", 48: "X", 51: "S", 53: "S", 46: "S", 3: "S", 49: "L", 79: "L", 78: "X", 50: "L", 37: "L", 24: "C", 52: "C"}
ATTACKER = {85, 89, 91, 93, 95, 129, 131, 160, 127}   # (slot 0 in one of these: he plays the move, not a reversal of it)
# (a move's two sides, the one doing it first: (85, 86) a grapple move, 95 / 96 as it goes on into the next, 70 / 71 a man
#  on the mat picked up, 72 / 73 a hold, 74 / 75 a pin ... -- slot 0 on the second side with the man on the first: they swapped)
ROLE_PAIRS = {(85, 86), (89, 90), (91, 92), (93, 94), (95, 96), (129, 130), (160, 161), (70, 71), (72, 73), (74, 75)}
def dist_of(r):
    x0, z0, x1, z1 = (num(r[c]) for c in ("s0_x", "s0_z", "s1_x", "s1_z"))
    return ((x0 - x1) ** 2 + (z0 - z1) ** 2) ** 0.5 if None not in (x0, z0, x1, z1) else None

def num(v):
    try: return int(v, 0)
    except Exception:
        try: return float(v)
        except Exception: return None

def cmd_analyze(a):
    NM = NoMercy(a.game)
    libs = {name: read_lib(os.path.join(a.game, "content", fn)) for name, fn in LIBS}
    slots_of = {}
    for src, sid, ids in men(a.game): slots_of[sid] = ids
    id_of = {}
    for p in glob.glob(os.path.join(a.game, "wrestlers", "*", "*", "wrestler.json")):
        w = json.load(open(p, encoding="utf-8")); id_of[num(str(w.get("id")))] = os.path.basename(os.path.dirname(p))
    out, tot, seen_slots, all_slots = [], collections.Counter(), set(), set()
    for run in a.runs:
        rows = list(csv.DictReader(open(os.path.join(run, "state.csv"))))
        if not rows: continue
        sid = id_of.get(num(rows[-1]["s0_id"]))
        ids = slots_of.get(sid, {})
        all_slots |= {(sid, k) for k in ids}
        starts = []
        last = None
        for i, r in enumerate(rows):
            f = num(r.get("forced", "0")) or 0
            # (a scenario's plain force -- its entries done as one, no qa_fsub -- counts its picks without the entry: --as)
            if f and (f & 0xFFFF) != (last or 0) & 0xFFFF and (f >> 16 or a.as_entry): starts.append((i, f >> 16 or a.as_entry))
            last = f
        for j, (i0, e) in enumerate(starts):
            v = ids.get(e)
            if v is None: continue
            seen_slots.add((sid, e))
            i1 = starts[j + 1][0] if j + 1 < len(starts) else len(rows)
            lid, m = parse_id(v)
            # (the move begins where he is in it: its move number and a move's state -- not the game's measuring of his
            #  moves at the bell, nor what he did before the grab)
            go = next((i for i in range(i0, min(i1, i0 + 600)) if num(rows[i]["s0_move"]) == m and num(rows[i]["s0_phase"]) in MOVE_STATES), None)
            if go is None: continue
            i0 = go
            path = {0: [], 1: []}; after = {0: None, 1: None}; moved = {0: False, 1: False}
            end_row = None
            swapped, last_mv, on_gap = False, m, None
            for i in range(i0, min(i1, i0 + 600)):
                r = rows[i]
                for k in (0, 1):
                    ph = num(r["s%d_phase" % k])
                    if not path[k] or path[k][-1] != ph: path[k].append(ph)
                    if ph in MOVE_STATES: moved[k] = True
                    elif moved[k] and after[k] is None and ph is not None:
                        after[k] = (ph, i, num(r["s%d_state" % k]))   # (the first state past the move, and the one he left)
                p0, p1 = num(r["s0_phase"]), num(r["s1_phase"])
                if (p1, p0) in ROLE_PAIRS and after[0] is None: swapped = True   # (his side and the man's swapped while his move played: a reversal)
                if p0 in MOVE_STATES and after[0] is None and num(r["s0_move"]) is not None: last_mv = num(r["s0_move"])
                if on_gap is None and p0 == 95 and i > i0 and num(rows[i - 1]["s0_phase"]) == 85:
                    on_gap = dist_of(r)                                   # (PLACE on: the next move starts here)
                if after[0] and after[1]: end_row = max(after[0][1], after[1][1]); break
            flags = []
            if not (moved[0] or moved[1]): continue           # (the forced pick never started a move: a grab that missed)
            if path[0] and path[0][0] not in ATTACKER and path[0][0] not in range(113, 126):
                tot["reversed (the man turned it on him)"] += 1
                continue
            if swapped:
                tot["reversed (the man turned it on him)"] += 1
                continue
            if end_row is None and i1 < i0 + 600:
                # (the next forced pick came while this move was still on: the move's own next step -- a way on to another
                #  entry, +4 -- was picked, and the test forced its next move there: not this move's own ending)
                tot["chained (its next step forced by the test)"] += 1
                out.append((sid, e, v, [p for p in path[0] if p is not None][:14], [p for p in path[1] if p is not None][:14], ["(chained: next step forced)"]))
                continue
            on, ea, ed = expect(NM, m)
            # (a pin of the move's own: 74 / 75 straight from it -- not a cover (66 / 68) the man makes after it)
            obs_on = "pin" if 74 in path[0] else "hold" if 72 in path[0] or 127 in path[0] else None
            L = libs.get(lid.split(":")[0], {}).get(lid) or libs.get(lid.split(":")[0], {}).get(lid.split("~")[0])
            own = lib_plays(NM, L, m, sid, e) if L and L["files"] else None
            slot_on = on if slot_goes_on(NM, sid, e, m) else None   # (his slot's record: a way on cleared ends it after its pair)
            # (the move it ended with: its number changed past its own steps -- the game went on to another move: a counter
            #  grapple to the move it grabs into, a running catch to its drop (3180 -> 3182) -- judged by that move's
            #  animations: his library move there if one slot of his names it, else No Mercy's own)
            went = last_mv if last_mv is not None and not (m <= last_mv <= NM.last_step(m)) else None
            if went is not None:
                tot["went on to another move"] += 1
                hosts = {(parse_id(x)[0], k) for k, x in ids.items() if parse_id(x)[1] == went}
                if len(hosts) == 1:
                    (lid2, k2), = hosts
                    L2 = libs.get(lid2.split(":")[0], {}).get(lid2) or libs.get(lid2.split(":")[0], {}).get(lid2.split("~")[0])
                    own = lib_plays(NM, L2, went, sid, k2) if L2 and L2["files"] else None
                    slot_on = expect(NM, went)[0] if slot_goes_on(NM, sid, k2, went) else None
                elif not hosts:
                    g = NM.file(NM.last_step(went))
                    own = None if NM.goes_on(went) else (after_pose(NM.pose(g)), after_pose(NM.pose(g + 1)))
                    slot_on = expect(NM, went)[0]
                else: own, slot_on = None, expect(NM, went)[0]
                on, ea, ed = expect(NM, went)
            own_a = {own[0]} if own else set()
            own_d = {own[1]} if own else set()
            if m in DIVES and went is None and L and L["files"]:
                # (a turnbuckle dive: the pair is his flight and his landing, then the miss / the landing on the man -- the
                #  library's roles, past them No Mercy's own (movex_anim): he ends by any of roles 1 .., the man by none)
                own_a = {after_pose(h >> 10) for h in L["files"][1:]} | {after_pose(NM.pose(NM.file(m) + r)) for r in range(L["n"], 5)}
                own_d = set()
            if end_row is None: flags.append("STUCK")
            else:
                ca, cd = REST.get(after[0][0], "X"), REST.get(after[1][0], "X")
                if slot_on is None and obs_on is None:
                    # SNAP: the state after it isn't what its own last animations end in (the clip); DIFF: it ends otherwise
                    # than No Mercy's own move there (the source game's move: reported, not a fault by itself)
                    for who, k, c, own_c, e_c in (("attacker", 0, ca, own_a, ea), ("man", 1, cd, own_d, ed)):
                        oc = {{"R": "L"}.get(x, x) for x in own_c} & {"S", "L"}
                        # (the rows are ~25 frames apart: a state he passed through unseen is the one the row says he left --
                        #  when that one is of his animation's class, he was in it first: no snap. Only ever a SNAP let go.)
                        left = REST.get(after[k][2], "X") if after[k][2] not in MOVE_STATES else "X"
                        if oc and c in ("S", "L") and c not in oc and left not in oc: flags.append("SNAP %s: %s after it, its animation%s ends %s" % (who, c, " (that it went on to: %d)" % went if went is not None else "", "/".join(sorted(oc))))
                        elif on is None and e_c in ("S", "L") and c in ("S", "L") and c != e_c: flags.append("DIFF %s %s (No Mercy: %s)" % (who, "STAND" if c == "S" else "LIE", e_c))
                # (PLACE on / SNAP on: only into a next move No Mercy's own moves go on to with both men on their feet -- a
                #  go-behind's back grapple 77, 85, 70, 81, 183 ...; not a slam's ground grapple at a lying man, 236 / 240,
                #  where No Mercy's own leave the man lying and the attacker at his head)
                dest = slot_way_on(NM, sid, e, m)
                stand_on = not dest or all(c and c <= {"S", "R"} for c in on_classes(NM, dest))
                if stand_on and on_gap is not None and on_gap > 80: flags.append("PLACE on: %.0f apart as it went on into the next move" % on_gap)
                if stand_on and on_gap is not None and L and L["files"]:
                    # (SNAP on: it went on into the next move -- the attacker as a move goes on, 95, the man 96: both on their
                    #  feet, as every No Mercy move that goes on by its record leaves them -- from animations that end with a
                    #  man lying)
                    for who, h in (("attacker", L["files"][0]), ("man", L["files"][1])):
                        if after_pose(h >> 10) == "L": flags.append("SNAP on: the %s lying at its animation's end, then in the next move at once" % who)
                if on == "pin" and obs_on != "pin": flags.append("KIND: No Mercy's move pins, it didn't")
                if on is None and obs_on == "pin" and m not in DIVES: flags.append("KIND: it pinned, No Mercy's move doesn't")
                r = rows[end_row]
                x0, z0, x1, z1 = (num(r[c]) for c in ("s0_x", "s0_z", "s1_x", "s1_z"))
                if None not in (x0, z0, x1, z1) and ((x0 - x1) ** 2 + (z0 - z1) ** 2) ** 0.5 > 300: flags.append("PLACE: %.0f apart" % (((x0 - x1) ** 2 + (z0 - z1) ** 2) ** 0.5))
                for k in (0, 1):
                    y = num(r["s%d_y" % k])
                    if y is not None and min(abs(y), abs(y - 90)) > 15: flags.append("PLACE: slot %d at height %.0f" % (k, y))
            for f in flags: tot[f.split(" (")[0].split(":")[0]] += 1
            if not flags: tot["ok"] += 1
            out.append((sid, e, v, [p for p in path[0] if p is not None][:14], [p for p in path[1] if p is not None][:14], flags))
    bad = [o for o in out if o[5] and not o[5][0].startswith("(chained") and not all(f.startswith("DIFF") for f in o[5])]
    lines = ["move_audit analyze: %d runs, %d forced moves played (%d of %d library slots), %d with a mismatch" % (len(a.runs), len(out), len(seen_slots), len(all_slots), len(bad)),
             "by kind: " + ", ".join("%s %d" % kv for kv in sorted(tot.items())), ""]
    for sid, e, v, p0, p1, flags in out:
        lines.append("%s %-24s %3d %-34s him %s | man %s" % ("!!" if flags else "  ", sid, e, v, p0, p1) + ("   " + "; ".join(flags) if flags else ""))
    text = "\n".join(lines) + "\n"
    if a.out: open(a.out, "w", encoding="utf-8").write(text)
    print("\n".join(lines[:2]))

def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("static"); s.add_argument("game"); s.add_argument("--out", default="")
    s = sp.add_parser("scenarios"); s.add_argument("game"); s.add_argument("out"); s.add_argument("--men", default="")
    s.add_argument("--seconds", type=int, default=360); s.add_argument("--speed", type=int, default=8)
    s = sp.add_parser("analyze"); s.add_argument("game"); s.add_argument("runs", nargs="+"); s.add_argument("--out", default="")
    s.add_argument("--as", dest="as_entry", type=int, default=0, help="the entry a plain force (no qa_fsub) does its picks as")
    a = ap.parse_args()
    {"static": cmd_static, "scenarios": cmd_scenarios, "analyze": cmd_analyze}[a.cmd](a)

if __name__ == "__main__":
    main()
