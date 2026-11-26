#!/usr/bin/env python3
"""revlib.py (2026-11-13, movelib2): the Revenge handler's built-in move library -- the WHOLE of WCW/nWo Revenge's move list,
from the user's own ROM (its files as data\\extracted\\rev_files has them, and the boot RAM's tables) -- and every Revenge
Superstar's moveset mapped onto it.

Why: movelib (scratch\\agents\\movelib\\tool\\movelib.py) made the library of the 78 moves the 24 built-in Revenge ports use,
played from the mod's pack at fixed game file numbers (rev_move_anims.h: one copy per port, 378 pairs of which 125 differ).
The other 57 Revenge Superstars (handlers1's imports) played No Mercy's stand-ins. Now:
  - every move code of every Superstar's Revenge list that has an animation pair (the attacker's file and the defender's), and
    the hold block it goes on to (Revenge's codes 239-262: the loop's pair and the three endings', gen/revanims.py
    rev_block), is a library move "revenge:<attacker file>_<defender file>" (Revenge's numbers, hex) -- one per different
    content, however many men have it;
  - the animations are a content file of their own (movelib_revenge.bin, in the game's content\\ folder beside wmport.bin),
    read by the exe and given game file numbers only while a match plays them (exe arcade_wrestlers.cpp: the same pool as
    the folder models' and the folders' own moves), so the library can be as big as Revenge's list: no fixed numbers, no cap;
  - each Superstar's moveset is mapped slot by slot onto it the way gen/revanims.py did it for the 24 ports (man_rows): a
    slot whose No Mercy move is a stand-in, or kept the reference's, plays his own Revenge pair (and its hold).

    python revlib.py <out folder>
writes <out>\\movelib_revenge.bin (the library: moves, IDs, animations, records), <out>\\move_lib.h (for the mod: the count,
the stamp and the built-in ports' lists -- what movelib's move_lib.h had less the pack's numbers), <out>\\move_lib.json (ID ->
number, usual move, files, who has it: for importers) and <out>\\rows_all.json (every Superstar's rows), and checks:
  - the 24 ports' rows made here == gen/revanims.py's (data\\gen_data\\rev_anims.json), row for row;
  - every one of movelib's 78 library moves has the same number here, and its animations' bytes / header / record here are
    the pack's (mod build\\wmport.bin at rev_move_anims.h's numbers)."""
import collections, hashlib, json, os, re, struct, sys
sys.dont_write_bytecode = True               # (the imports below are the live data\\revport's: no __pycache__ written there)
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers import DEV     # (the Development folder: handlers/__init__.py -- 2026-11-13, final082: was movelib2's folder)
# (the mod folder whose pack, generators and headers it reads: REVLIB_MOD, else the project's mod; its pack WITH the Revenge
#  move animations -- build\wmport.bin.with_rma and src\wmport_files.h.with_rma when pack_drop.py has run there)
MOD = os.environ.get("REVLIB_MOD") or os.path.join(DEV, "mod")
os.environ.setdefault("NM_MOD_DIR", MOD)
sys.path.insert(0, os.path.join(MOD, "gen"))
import revanims as RA                         # (gen/revanims.py: byte 0, records, rev_block, the ports' rows)
K, RM = RA.K, RA.RM
# (slot_positions depends only on revmoves.PAIRS, and port_rev_moveset works it out again for every Superstar -- most of an
#  81-man run's time: kept once here)
_slot_positions = RM.slot_positions
_pos_cache = {}
def _slot_positions_once(pairs):
    k = tuple(sorted(pairs.items()))
    if k not in _pos_cache: _pos_cache[k] = _slot_positions(pairs)
    return _pos_cache[k]
RM.slot_positions = _slot_positions_once
from handlers.revenge import game as G

GOES_ON = RA.GOES_ON
BLOCK0 = RA.BLOCK0                            # (239: the first hold block code)
HAND = [dict(id="revenge:slam_sharpshooter", base=1880, ground=157,
             about="Revenge's code 58 -> 200: the slam (No Mercy's 1880 byte for byte), then the ground grapple at his legs picks 157")]

# ---- (2026-11-13, movesteps) a move's later steps -------------------------------------------------------------------------
# No Mercy makes a grapple's entries e + 1 .. e + 4 from entry e's move: its move + 2, + 4, + 6, + 8 (ovl_e's derived table,
# 0x8014DD34: the game's func_8013CBAC; mod moveset_unpack.h mu_match_build) -- the variants the slot plays (its counter, its
# block ...). Revenge keeps its lists the same way: a grapple slot's code c, then its variants at codes c + 1 .. c + 4 (checked
# on the moves both games have byte for byte, every Revenge list: code c + k is No Mercy's move + 2k for 1,435 of them --
# scratch\agents\movesteps\tool\revgroups2.py). Before, a library move replaced only entry e (and the step it goes on to),
# and the entries after it played the No Mercy stand-in's variants. Now each such entry whose Revenge variant No Mercy plays
# otherwise names the library move of the man's own code c + k (its pair, and the hold it goes on to): "revenge:<pair>@<No
# Mercy's move + 2k there>" -- the very move the game derives there (the tables stay the game's), Revenge's animations.
def derived_table():
    """ovl_e's derived entries from the user's No Mercy ROM: [(entry, n, step)] -- entries entry + 1 .. entry + n get entry's
    move + step, + 2 step ... (n > 0; a single `to` entry, which the game's table hasn't, would be skipped)"""
    rom = open(RA.nmp("/home/devbox/work/nomercy.z64"), "rb").read()
    at = rom.find(b"".join(struct.pack(">hhh", e, -4, 2) for e in (5, 10, 15)))
    if at < 0: raise SystemExit("revlib: the derived entries' table isn't in the No Mercy ROM")
    out = []
    while True:
        e, to, step = struct.unpack_from(">hhh", rom, at)
        if e == -1: return out
        if to <= 0: out.append((e, max(1, -to), step))
        at += 6

def man_steps(rl, t, pos, pool, AN, ids, slots, derived, lookup, tally):
    """his list's later steps (a dict entry -> ID to add to `ids`): for each derived group whose base entry e is a Revenge code
    c's place in his list (slot_positions), entry e + k plays his code c + k's pair where No Mercy's move there (the list's move
    at e, + step x k) plays other animations -- with the hold Revenge's goes on to (rev_block) when No Mercy's goes on; a No
    Mercy step that has no animation, or goes on where Revenge's ends, is played on the same step of the slot's kind's move
    that has one and ends the same way. lookup(fa, fd, block files) -> the library move, or None."""
    out = {}
    for e, n, st in derived:
        if e not in pos: continue
        j = pos[e][0]
        if j >= t[238]: continue
        c = max(i for i in range(BLOCK0) if t[i] <= j)
        if j != t[c]: continue
        at = ids.get(e)
        v = int(at.split("@")[1].split("/")[0]) if at else slots[e][0]
        if v in (0, 0xFFFF): continue
        for k in range(1, n + 1):
            ck, nv = c + k, v + st * k
            if ck >= BLOCK0: break
            jj = t[ck]
            nm_has = 0 <= nv < len(RM.NM1) and RM.NM1[nv] > 0
            rev_has = t[ck + 1] - t[ck] >= 2 and rl[jj] and rl[jj + 1] and AN.ok(RM.ranim(rl[jj])) and AN.ok(RM.ranim(rl[jj + 1]))
            if not rev_has:
                tally["Revenge has no pair for the step: No Mercy's kept" if nm_has else "neither has the step"] += 1; continue
            fa, fd = RM.ranim(rl[jj]), RM.ranim(rl[jj + 1])
            if nm_has and RM.revh.get(fa) == RM.nmhash(RM.NM1[nv] + 0x3322) and RM.revh.get(fd) == RM.nmhash(RM.NM1[nv] + 0x3323):
                tally["No Mercy's plays it already"] += 1; continue
            on = nv
            goes = nm_has and bool(RA.nm_flags(nv) & GOES_ON)
            fs = None
            if goes:
                d, _sc, _nx = RA.rev_block(rl, t, ck)
                if d is not None:
                    fs = [RM.ranim(rl[t[d] + q]) for q in range(8)]
                    if not all(AN.ok(f) for f in fs): fs = None
            if not nm_has or (goes and fs is None):
                # (the same step of a move the slot can hold, that has it and ends -- Revenge's step has no hold here)
                c2 = [b for b in sorted(pool.get(e, ())) if b not in (0, 0xFFFF) and b % 10 == 0 and b + st * k < len(RM.NM1)
                      and RM.NM1[b + st * k] > 0 and not (RA.nm_flags(b + st * k) & GOES_ON)]
                if not c2: tally["no move of the slot's kind has the step and ends: No Mercy's kept"] += 1; continue
                on = min(c2, key=lambda b: abs(b - (v - v % 10))) + st * k
                fs = None
            x = lookup(fa, fd, fs)
            if x is None: tally["not in the library (a bug)"] += 1; continue
            out[e + k] = "%s@%d" % (x["id"], on)
            tally["library" + ("" if on == nv else ", on another move's step")] += 1
    return out

def tables():
    ram = open(RA.nmp("/tmp/devbox-0/rev_bm_ram.bin"), "rb").read()       # (Revenge's code -> list place table, 0x80030570)
    return [struct.unpack_from(">h", ram, 0x30570 + 2 * i)[0] for i in range(279)]

class Anims:
    """Revenge animation files as No Mercy plays them: byte 0 made No Mercy's (B0), the 4-byte header, the record (REC)"""
    def __init__(self):
        self.B0 = RA.byte0_table(); self.REC = RA.rec_conv(); self.cache = {}
    def ok(self, f): return f is not None and open(RA.REV % f, "rb").read()[0] in self.B0
    def get(self, f):
        if f not in self.cache:
            d = bytearray(open(RA.REV % f, "rb").read()); d[0] = self.B0[d[0]]
            self.cache[f] = (bytes(d), bytes(d[:4]), self.REC(f))
        return self.cache[f]

def man_rows(key, rl, rep, rs, t, pos, pool, AN, rev_own=None):
    """gen/revanims.py main()'s rows for one Superstar (the same steps, for any of them): his slots that play his own Revenge
    pair -- a stand-in ("near") slot's, his REV_OWN variants, a slot that kept the reference's move -- each {slot, fa, fd,
    code, move, cont: the block's 8 files or None, carrier}. rep: port_rev_moveset's report lines; rs: the reference's slots."""
    rows = []
    for l in rep:
        if "slot" not in l or "(near" not in l: continue
        slot = int(l.split("slot")[1].split(":")[0]); j = pos[slot][0]
        if j >= t[238]: continue
        c = max(i for i in range(239) if t[i] <= j)
        n = t[c + 1] - t[c]
        if j != t[c] or n < 2 or not rl[j] or not rl[j + 1]: continue
        fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
        if not (AN.ok(fa) and AN.ok(fd)): continue
        rows.append(dict(slot=slot, fa=fa, fd=fd, code=c, move=int(l.split("->")[1].split()[0]), why="near"))
    for slot, code in (rev_own or {}).items():
        for k in range(5):
            j = t[code + k]
            if not rl[j] or not rl[j + 1]: continue
            fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
            if not (AN.ok(fa) and AN.ok(fd)): continue
            rows.append(dict(slot=slot + k, fa=fa, fd=fd, code=code + k, move=rs[slot + k][0], why="own"))
    done = {r["slot"] for r in rows} | {int(l.split("slot")[1].split(":")[0]) for l in rep if "slot" in l and ":" in l.split("slot")[1][:6]}
    for slot in range(292):
        rm = rs[slot][0]
        if slot in done or slot not in pos or rm in (0, 0xFFFF): continue
        if re.search(r"double|doomsday", K.vnames.get(rm - rm % 10, ""), re.I): continue
        j = pos[slot][0]
        if j >= t[238] or not rl[j]: continue
        c = max(i for i in range(239) if t[i] <= j)
        if j != t[c]: continue
        fa = RM.ranim(rl[j])
        if not AN.ok(fa): continue
        if not RA.slot_kind_ok(slot, fa, pos, pool): continue
        if t[c + 1] - t[c] >= 2 and rl[j + 1]:
            fd = RM.ranim(rl[j + 1])
            if not AN.ok(fd): continue
        else: continue                 # (a strike: left the reference's, as gen/revanims.py does)
        rows.append(dict(slot=slot, fa=fa, fd=fd, code=c, move=rm, why="reference"))
    for r in rows:
        m = r["move"]
        r["cont"] = None
        if m in (0, 0xFFFF) or not (RA.nm_flags(m) & GOES_ON): continue
        d, sc, nxt = RA.rev_block(rl, t, r["code"])
        if d is not None:
            fs = [RM.ranim(rl[t[d] + k]) for k in range(8)]
            if not all(AN.ok(f) for f in fs): continue
            r["cont"] = fs; r["block"] = d
        else:
            base = [v for v in sorted(pool.get(r["slot"], ())) if not (RA.nm_flags(v) & GOES_ON)]
            pr = RM.predict(rl, {r["slot"]: pos[r["slot"]]}, {r["slot"]: base}).get(r["slot"]) if base else None
            if pr: r["carrier"] = pr[0]
    return rows

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    t = tables()
    AN = Anims()
    pos = RM.slot_positions(RM.PAIRS)
    pool = RM.slot_pool(K.CAN)
    old = json.load(open(os.path.join(DEV, "data", "gen_data", "rev_anims.json")))
    fx = G.fixes()
    roster = G.roster()
    # ---- every Superstar's moveset and rows -------------------------------------------------------------------------
    men = []        # (string id, key, his Revenge list file, slots, rows, ported LATE key or None)
    late_keys = {e["key"]: e for e in K.LATE if "rev" in e}
    for sid, f in fx.items():                       # (the ported ones: their own fixes, as aki_import writes them)
        rep = []
        slots, ab = G.moveset(f, rep)
        rl = RM.revlist(int(f["revenge"]["moveset_file"], 16))
        rs = K.van_unpacked(f["moveset"]["reference_ordinal"])[0]
        men.append(dict(sid=sid, key=f["key"], rev=int(f["revenge"]["moveset_file"], 16), slots=slots, ported=True,
                        rows=man_rows(f["key"], rl, rep, rs, t, pos, pool, AN, RA.REV_OWN.get(f["key"]))))
    ported_codes = {int(x["revenge"]["code"], 16) for x in fx.values()}
    for e in roster:                                 # (the rest, as aki_import --all makes them)
        sid = G.slug(e["name"])
        if e["code"] in ported_codes or sid in fx or not e["moveset_file"]: continue
        ref = 4 if (e["weight"] or 0) >= 200 else 1
        f = {"key": sid, "moveset": {"reference_ordinal": ref, "allies": []}, "revenge": {"moveset_file": "0x%X" % e["moveset_file"]}}
        rep = []
        slots, ab = G.moveset(f, rep)
        rl = RM.revlist(e["moveset_file"])
        rs = K.van_unpacked(ref)[0]
        men.append(dict(sid=sid, key=sid, rev=e["moveset_file"], slots=slots, ported=False,
                        rows=man_rows(sid, rl, rep, rs, t, pos, pool, AN)))
    # ---- check: the ports' rows as gen/revanims.py made them -----------------------------------------------------------
    bad = 0
    want = collections.defaultdict(list)
    for r in old["rows"]: want[r["key"]].append(r)
    for m in men:
        if not m["ported"]: continue
        got = [(r["slot"], "%s_%04X_%04X" % (m["key"].upper(), r["fa"], r["fd"]), r["code"], r["move"],
                ("%s_B%d_%04X_%04X" % (m["key"].upper(), r["block"], r["cont"][0], r["cont"][1])) if r.get("cont") else None,
                r.get("carrier", 0)) for r in m["rows"]]
        was = [(r["slot"], r["name"], r["code"], r["move"], r.get("cont"), r.get("carrier", 0)) for r in want[m["key"]]]
        if got != was:
            bad += 1
            print("  ROWS DIFFER %s: %d here, %d revanims'" % (m["key"], len(got), len(was)))
            for a, b in zip(got, was):
                if a != b: print("     ", a, "|", b)
    print("ports' rows against gen/revanims.py's: %d of %d men different (%d rows)" % (bad, sum(m["ported"] for m in men), len(old["rows"])))
    # ---- the library: every Revenge code with a pair, in every list, and the block it goes on to ----------------------
    def pair_key(fa, fd): return (AN.get(fa), AN.get(fd))
    def block_key(fs): return tuple(AN.get(f) for f in fs) if fs else None
    combos = {}          # (pair content, block content) -> {fa, fd, cont files, who}
    def add(fa, fd, fs, who):
        k = (pair_key(fa, fd), block_key(fs))
        c = combos.setdefault(k, dict(fa=fa, fd=fd, cont=fs, who=set()))
        if (fa, fd) < (c["fa"], c["fd"]): c.update(fa=fa, fd=fd, cont=fs)     # (named by its lowest Revenge files)
        c["who"].add(who)
        return k
    for m in men:
        rl = RM.revlist(m["rev"])
        for c in range(BLOCK0):
            j, n = t[c], t[c + 1] - t[c]
            if n < 2 or not rl[j] or not rl[j + 1]: continue
            fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
            if not (AN.ok(fa) and AN.ok(fd)): continue
            d, sc, nxt = RA.rev_block(rl, t, c)
            fs = [RM.ranim(rl[t[d] + k]) for k in range(8)] if d is not None else None
            if fs and not all(AN.ok(f) for f in fs): fs = None
            add(fa, fd, fs, "%s code %d" % (m["sid"], c))
            if fs: add(fa, fd, None, "%s code %d (without its hold)" % (m["sid"], c))
        for r in m["rows"]: r["key"] = add(r["fa"], r["fd"], r.get("cont"), "%s slot %d" % (m["sid"], r["slot"]))
    # (movelib's 78: the same numbers -- their content from the pack, to find them among these)
    ml = json.load(open(os.path.join(HERE, "move_lib.json")))     # (movelib's library index, beside this script)
    # (the pack as movelib had it -- the Revenge animations at their fixed numbers, and its move_lib.h with the ports' lists:
    #  scratch\agents\movelib\build\mod, read only; this agent's mod copy has the pack without them now, tool\pack_drop.py)
    mod = MOD
    rma_h = open(os.path.join(mod, "src", "rev_move_anims.h")).read()
    rma = {mm.group(1): int(mm.group(2), 16) for mm in re.finditer(r"#define RMA_(\w+) 0x([0-9A-F]+)u", rma_h)}
    rma_first = int(re.search(r"#define RMA_FIRST 0x([0-9A-F]+)u", rma_h).group(1), 16)
    wmp_h = open(next(p for p in (os.path.join(mod, "src", "wmport_files.h.with_rma"), os.path.join(mod, "src", "wmport_files.h")) if os.path.exists(p))).read()
    wmp0 = int(re.search(r"#define WMP_FILE0 0x([0-9A-F]+)u", wmp_h).group(1), 16)
    wmp = [(int(a, 16), int(b)) for a, b in re.findall(r"\{ 0x([0-9A-F]+), (\d+) \}", wmp_h.split("wmp_files[WMP_FILES_N][2] = {")[1].split("};")[0])]
    pack = open(next(p for p in (os.path.join(mod, "build", "wmport.bin.with_rma"), os.path.join(mod, "build", "wmport.bin")) if os.path.exists(p)), "rb").read()
    F = {f["name"]: f for f in old["files"]}
    def pack_anim(n, name, k):
        o, sz = wmp[n - wmp0]
        return (pack[o:o + sz], bytes.fromhex(F[name]["hdr"][k]), bytes.fromhex(F[name]["rec"][k]))
    def pack_pair(name): return (pack_anim(rma[name], name, 0), pack_anim(rma[name] + 1, name, 1))
    lib, by_key, bad_pack = [], {}, 0
    for h in HAND: lib.append(dict(h, num=len(lib) + 1))
    for mid, v in sorted(ml.items(), key=lambda kv: kv[1]["num"]):
        if not v.get("pair"): continue
        pk = pack_pair(v["pair"])
        bk = None
        if v["cont"]:
            # (the block's 8 files: its 4 pairs one after another in the pack from the first's number, each pair named
            #  after its own files)
            nb = rma[v["cont"]]
            names_at = {rma[nm]: nm for nm in rma}
            bk = tuple(pack_anim(nb + k, names_at[nb + (k & ~1)], k & 1) for k in range(8))
        k = (pk, bk)
        if k not in combos:
            bad_pack += 1; print("  movelib's %s (%d): its pack content is no Revenge code's here" % (mid, v["num"])); continue
        if v["num"] != len(lib) + 1: raise SystemExit("movelib's numbers aren't 1.. in order")
        c = combos[k]
        lib.append(dict(id=mid, num=v["num"], base=v["base"], key=k, fa=c["fa"], fd=c["fd"], cont=c["cont"], who=c["who"]))
        by_key[k] = lib[-1]
    print("movelib's 78: %d not found with the same content (bytes, header, record) here" % bad_pack)
    # the rest, in a stable order: by the Revenge files that name them
    names = {m["id"] for m in lib}
    rest = sorted((k for k in combos if k not in by_key), key=lambda k: (combos[k]["fa"], combos[k]["fd"], combos[k]["cont"] or []))
    for k in rest:
        c = combos[k]
        nm = "revenge:%04x_%04x" % (c["fa"], c["fd"])
        if nm in names: nm += "~plain" if not c["cont"] else "~%04x" % c["cont"][0]
        if nm in names: raise SystemExit("two library moves named " + nm)
        names.add(nm)
        lib.append(dict(id=nm, num=len(lib) + 1, base=0, key=k, fa=c["fa"], fd=c["fd"], cont=c["cont"], who=c["who"]))
        by_key[k] = lib[-1]
    # each move's usual No Mercy move: the one most rows play it on (else, for a move no row has, the slot's of whoever has it
    # -- 0: a list must say "@<move>")
    use = collections.defaultdict(collections.Counter)
    for m in men:
        for r in m["rows"]: use[r["key"]][r.get("carrier") or r["move"]] += 1
    for x in lib:
        if "key" in x and not x["base"] and use[x["key"]]: x["base"] = use[x["key"]].most_common(1)[0][0]
    # ---- every Superstar's list as library IDs -----------------------------------------------------------------------
    for m in men:
        ids = {}
        # (a row with a move in its place -- Revenge's ends where the slot's goes on -- names both: the slot's, which the game
        #  numbers its continuations by, and the one put in its place after the unpack, as the built-in port's row does)
        for r in m["rows"]: ids[r["slot"]] = ("%s@%d/%d" % (by_key[r["key"]]["id"], r["move"], r["carrier"]) if r.get("carrier") else
                                              "%s@%d" % (by_key[r["key"]]["id"], r["move"]))
        if m["key"] in ("brethart", "sting", "brethart_b"): ids[70] = "revenge:slam_sharpshooter@%d" % m["slots"][70][0]
        m["ids"] = ids
    # (2026-11-13, movesteps) the later steps of the never-ported men's moves (man_steps). The ported men's lists stay their
    # built-in port's (their folders are its twins: the list twin checks); their later steps are the port's, No Mercy's.
    derived = derived_table()
    def lookup(fa, fd, fs): return by_key.get((pair_key(fa, fd), block_key(fs) if fs else None))
    step_tally = collections.Counter()
    for m in men:
        if m["ported"]: continue
        m["steps"] = man_steps(RM.revlist(m["rev"]), t, pos, pool, AN, m["ids"], m["slots"], derived, lookup, step_tally)
        m["ids"].update(m["steps"])
    print("later steps (the never-ported men's derived entries): %s" % dict(step_tally.most_common()))
    # (a second look -- "bret_hart_b" -- plays his base's rows, as the game gives the built-in one his base's: roster.c cells_k)
    for m in men:
        b = fx.get(m["sid"], {}).get("second_look_of")
        if b: m["ids"] = dict(next(x for x in men if x["sid"] == b)["ids"])
    # ---- the animations, one copy each -----------------------------------------------------------------------------
    anim_ix, anims = {}, []
    def anim_of(content):
        if content not in anim_ix: anim_ix[content] = len(anims); anims.append(content)
        return anim_ix[content]
    for x in lib:
        if "key" not in x: x["pair_ix"] = x["block_ix"] = 0xFFFFFFFF; continue
        pk, bk = x["key"]
        x["pair_ix"] = anim_of(pk[0]); anim_of(pk[1])
        if anim_ix[pk[1]] != x["pair_ix"] + 1:      # (a pair is two in a row: the defender's the attacker's + 1)
            x["pair_ix"] = len(anims); anims += [pk[0], pk[1]]
        if bk:
            x["block_ix"] = len(anims)
            ix = [anim_ix.get(a) for a in bk]
            if all(i is not None for i in ix) and ix == list(range(ix[0], ix[0] + 8)): x["block_ix"] = ix[0]
            else: anims += list(bk)
        else: x["block_ix"] = 0xFFFFFFFF
    # ---- movelib_revenge.bin ----------------------------------------------------------------------------------------
    #  "NMMOVLIB", u32 version 1, u32 moves, u32 animations, u32 the stamp (below), u32 string bytes, u32 data bytes
    #  moves[]:  u32 its ID's place in the strings, u16 the No Mercy move it's usually on, u16 ground (0: none),
    #            u32 its pair's first animation (the defender's the next; 0xFFFFFFFF: none -- a move made by hand),
    #            u32 its hold block's first animation (8 in a row; 0xFFFFFFFF: none)
    #  anims[]:  u32 its place in the data, u32 its size, 36 bytes its record (the header is the file's first 4 bytes)
    #  strings (each ID, 0-ended), data (each animation, 8-aligned). All little-endian (the exe's own order).
    strings = bytearray(); data = bytearray()
    mv = bytearray(); an = bytearray()
    for x in lib:
        off = len(strings); strings += x["id"].encode() + b"\0"
        mv += struct.pack("<IHHII", off, x["base"], x.get("ground", 0), x["pair_ix"], x["block_ix"])
    for a in anims:
        b, hdr, rec = a
        assert b[:4] == hdr and len(rec) == 36
        an += struct.pack("<II", len(data), len(b)) + rec
        data += b + bytes(-len(b) % 8)
    stamp = int(hashlib.md5(bytes(mv) + bytes(strings)).hexdigest()[:8], 16)
    blob = b"NMMOVLIB" + struct.pack("<IIIIII", 1, len(lib), len(anims), stamp, len(strings), len(data)) + mv + an + strings + data
    open(os.path.join(out, "movelib_revenge.bin"), "wb").write(blob)
    print("movelib_revenge.bin: %d library moves (%d made by hand), %d animations, %.1f MB, stamp %08X" % (
        len(lib), len(HAND), len(anims), len(blob) / 1e6, stamp))
    # ---- move_lib.h (the mod): the count and stamp, the built-in ports' lists (movelib's, the same numbers) ------------
    old_lib_h = open(os.path.join(mod, "src", "move_lib.h")).read()
    # (newhome 2026-11-17: the mod's move_lib.h has had no built-in ports' lists since movelib2 -- the ports play from the
    #  library like everyone -- so the section is kept only when it is there; revlib stopped here before)
    try: ports = old_lib_h[old_lib_h.index("// The built-in ports' lists"):old_lib_h.index("// the pack's animations' header")]
    except ValueError: ports = ""
    jk = next(x["num"] for x in lib if x["id"] == "revenge:1544_1545")
    L = ["// generated by revlib.py (2026-11-13, movelib2: tools\\handlers -- the Revenge handler's library, the",
         "// whole of Revenge's move list from the user's ROM): the built-in move libraries' count and stamp, and the built-in",
         "// ports' lists. The library's moves themselves -- their animations, records and holds -- are the content file",
         "// movelib_revenge.bin, which the exe reads and serves (game file numbers given while a match plays them): movex.c asks",
         "// the exe for a library move by its number (1 = the first).",
         "#define MOVELIB_N %d" % len(lib),
         "#define MOVELIB_STAMP 0x%08Xu   // (the library's moves and IDs: the exe's must be the same, else no library moves)" % stamp,
         "#define MOVELIB_NASH_JK %d   // (revenge:1544_1545, Kevin Nash's jackknife: QA counts its man's thud, movex.c anim_recpart)" % jk,
         "// The library's moves made by hand (numbers 1 .. MOVELIB_HAND_N): no animation of their own -- the No Mercy move's --",
         "// and the ground grapple's pick after it (movex.c LibMove { file, then_file, ground }). Played without the exe.",
         "#define MOVELIB_HAND_N %d" % len(HAND),
         "static const LibMove move_hand[MOVELIB_HAND_N] = {"] + [
         "    { 0, 0, %d },   // %d %s (on %d): %s" % (h["ground"], k + 1, h["id"], h["base"], h["about"]) for k, h in enumerate(HAND)] + ["};", ""]
    open(os.path.join(out, "move_lib.h"), "w", newline="\n").write("\n".join(L) + ports)
    # ---- move_lib.json and rows_all.json (for importers and the report) ------------------------------------------------
    json.dump({x["id"]: dict(num=x["num"], base=x["base"], fa=x.get("fa"), fd=x.get("fd"), cont=x.get("cont"),
                             who=sorted(x.get("who", ()))[:12], n_who=len(x.get("who", ())))
               for x in lib}, open(os.path.join(out, "move_lib.json"), "w"), indent=0)
    json.dump({m["sid"]: dict(key=m["key"], ported=m["ported"], rev="0x%X" % m["rev"], ids={str(k): v for k, v in sorted(m["ids"].items())},
                              rows=[dict(slot=r["slot"], code=r["code"], fa=r["fa"], fd=r["fd"], move=r["move"], why=r["why"],
                                         cont=r.get("cont"), carrier=r.get("carrier", 0)) for r in m["rows"]])
               for m in men}, open(os.path.join(out, "rows_all.json"), "w"), indent=0)
    n57 = [m for m in men if not m["ported"]]
    print("men: %d (%d ported, %d not); rows %d (%d of the never-ported men's: %d of them have some)" % (
        len(men), len(men) - len(n57), len(n57), sum(len(m["rows"]) for m in men), sum(len(m["rows"]) for m in n57),
        sum(1 for m in n57 if m["rows"])))
    return bad + bad_pack

if __name__ == "__main__":
    sys.exit(1 if main() else 0)
