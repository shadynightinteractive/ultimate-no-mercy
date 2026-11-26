#!/usr/bin/env python3
"""wmlib.py (2026-11-13, wm2klib): the WrestleMania 2000 handler's built-in move library -- every WWF WrestleMania 2000
move its 58 Superstars' movesets use that No Mercy has no move for -- and those movesets mapped onto it.

Why: the WrestleMania 2000 handler (handlers\\wm2000\\game.py moveset_of: gen/mkroster.py port_wm_moveset) finds each of a
Superstar's moves among No Mercy's by its very animations; a move No Mercy has no move for went to a move of the same name,
a stand-in of the same kind, or his reference man's move. WrestleMania 2000's 705 moves in use: 547 are No Mercy moves
(the same animations, in order), 158 are not (wm2kall's work\\wm2000_moves.txt). Now those 158 are a library, as the
Revenge handler's (revlib.py, movelib2), and every slot that played a No Mercy move in place of one of them plays its own:
  - a library move is what the library format can carry of a move (exe arcade_wrestlers.cpp move_library_fill, mod
    movex.c lib_put): its first step's animation pair (sub-move 0: the attacker's file, the defender's the next) and, for
    a move that goes on, the step it goes on to (sub-move 1: its pair and the six files after it -- a hold's endings at
    + 2 / + 4 / + 6, as the game plays a hold).
  - (2026-11-13, movesteps) A move's other steps -- sub-moves 2, 4, 6, 8 (and the step each goes on to, 3, 5, 7, 9) -- are the
    game's own entries too: both games make a grapple's entries e + 1 .. e + 4 from entry e's move, its move + 2, + 4, + 6,
    + 8 (ovl_e's derived table, 0x8014DD34 in No Mercy, the same for the grapples in WrestleMania 2000: derived_table
    below), the variants the slot plays (its counter, its block ...). Each such step that No Mercy plays otherwise is a
    library move of its own, "wm2000:<move + k>" (its pair, and the hold it goes on to), and the folder's list names it on
    the derived entry: "wm2000:<move + k>@<No Mercy's move + k there>" -- the same tables as the game's (the list gives the
    entry the very move the game would derive), only the animations WrestleMania 2000's. So a WrestleMania 2000 move plays
    every step as in its game; nothing is capped (as many steps as the game's table makes, read from the ROM).
  - "wm2000:<move>" -- WrestleMania 2000's move number (a multiple of 10: its moves files' and its name table's number;
    a later step, its number + k), one library move per different content (the animations' bytes, headers and records):
    two WrestleMania 2000 moves with the same animations are one library move, named by the lower number
    (move_lib_wm2000.json "same_as").
  - (2026-11-13, movesteps) Its record: each slot that plays a WrestleMania 2000 move gets WrestleMania 2000's own record
    values (damage, timing ...) where the two games' records line up (bytes 0 .. 0x11: equal on 99%+ of the moves both games
    have, or a sure value of the value table -- 4 moves or more, 90% of them one No Mercy value: movelib2's rule for
    Revenge's damage; record_map below) and No Mercy's move there says otherwise: moves.json "records".
  - its animations come from the user's own ROM (read in place): a file No Mercy has byte for byte keeps No Mercy's own
    header and record for it (the game's file 0x2BF); (2026-11-20, revmoves2) so does a file No Mercy has but for its first
    byte -- the header's end pose and turn, which the games number differently (33 files: WrestleMania 2000's kind 11, 17,
    18 ... where No Mercy's file has 0) -- its bytes and record No Mercy's (the lowest-numbered such file, as for one the
    same throughout); a file No Mercy hasn't takes WrestleMania 2000's record (its file
    0x435: the same 36-byte layout) with the values the two games number differently mapped as revanims.py rec_conv does
    for Revenge (byte by byte, from the files both games have; frames as they are; sounds by the sound's own number).
  - the library is a content file of its own, movelib_wm2000.bin (the same layout as movelib_revenge.bin, NMMOVLIB 1:
    revlib.py), beside it in the game's content folder. The exe reads it after Revenge's: its moves get the numbers after
    Revenge's (1 .. 783 are Revenge's, the ones the mod's move_lib.h numbers), and its animations game file numbers only
    while a match plays them, from the same pool. No built-in port has a WrestleMania 2000 library move (the two built-in
    WrestleMania 2000 ports, Jeff Jarrett and The Blue Meanie, keep their No Mercy moves, so their folders stay the
    built-in ports' twins), so the mod needs no number of it.
Each Superstar's slot that the handler gave a No Mercy move by name, a stand-in or the reference's in place of a library
move now names it: "wm2000:<move>@<the No Mercy move it's played on>" (its record: damage, timing, how it goes on). The
No Mercy move is the slot's as the handler chose it, when it goes on as the WrestleMania 2000 move does (both end, or both
go on, and so on for the step after); else the No Mercy move of the slot's kind that goes on the same way and plays the
most of the same animations in its later steps; a slot with no such move, or whose reference holds no move there, keeps
what the handler gave it (rows_wm2000.json says why). The folder's packed moveset (wrestler.json "moveset") is unchanged:
a game without this library plays what it played before (a list ID the exe doesn't know keeps the packed moveset's move;
a derived entry's, the game's own).

    python wmlib.py lib <out folder>
        -> <out>\\movelib_wm2000.bin, <out>\\move_lib_wm2000.json (ID -> place, the usual No Mercy move, files, who has it),
           <out>\\rows_wm2000.json (every Superstar's slots that name a library move, and those that can't, with why)
    python wmlib.py moves <wrestlers folder> <rows_wm2000.json>
        rewrites each WrestleMania 2000 folder's moves.json (<folder>\\WrestleMania 2000\\<string id>\\) with its library
        moves; the two built-in ports' are left as they are
"""
import collections, hashlib, json, os, struct, sys
sys.dont_write_bytecode = True                # (the imports below include the live mod\\gen's: no __pycache__ written there)
HERE = os.path.dirname(os.path.abspath(__file__))
# (tools\\handlers in the project, beside this file; in an agent's folder, its copy of tools\\handlers)
H = HERE if os.path.isdir(os.path.join(HERE, "handlers")) else os.path.join(os.path.dirname(HERE), "tools", "handlers")
sys.path.insert(0, H)
from handlers.wm2000 import game as G
import wm2k, wmanim                           # (gen/wm2k.py, gen/wmanim.py: game.py puts mod\\gen on the path)
import mkroster as K

WM_ANIM0, WM_ANIM_END = 0x1609, 0x2848        # WrestleMania 2000's animation files (value + 0x19DB)
WM_ANIM_RECS = 0x435                          # their 36-byte records, from WM_ANIM0
WM_MOVE_RECS, WM_REC_BYTES = 0x3AD, 32        # its move records (No Mercy's are 36 bytes: file 0x1EF), flags at + 0x1C
NM_ANIM0, NM_ANIM_N, NM_ANIM_RECS = 0x2DD9, 0x169B, 0x2BF
NM_MOVE_RECS = 0x1EF
GOES_ON = 0x1E00                              # (a move record's flags: it goes on to its next step -- movex.c)
FRAME_AT = (15, 16, 18, 20, 22, 24, 26, 28, 30, 32)   # (record bytes that are frame numbers: as revanims.py has them)
SOUND_AT = (20, 22, 24, 26, 28, 30)           # (record bytes that are a frame, then the sound played at it)
PORTED = ("jeff_jarrett", "the_blue_meanie")  # (the built-in ports' folders: kept the built-in ports' twins)
NOT_SAME = "name", "stand-in", "reference"    # (port_wm_moveset's ways to a move No Mercy hasn't)
NMF = wmanim.nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")

def nm_file(f): return open(NMF % f, "rb").read()

# ---- (2026-11-13, movesteps) the steps of a move a slot plays: the game's derived entries -------------------------------
def derived_table(rom):
    """ovl_e's table of the entries made from another entry's move, read from a ROM of the engine (No Mercy's at ovl_e
    0x8014DD34; WrestleMania 2000's the same layout): [(entry, n, step)] -- entries entry + 1 .. entry + n get entry's move +
    step, + 2 step ... (the game's func_8013CBAC; mod moveset_unpack.h mu_match_build). A `to` > 0 (one entry, `to`, gets the
    move + step) is in neither game's table; it is kept as (entry, -to, step) with n < 0 and left out by steps_of."""
    start = b"".join(struct.pack(">hhh", e, -4, 2) for e in (5, 10, 15))     # (both games' tables begin so)
    at = rom.find(start)
    if at < 0: raise SystemExit("wmlib: the derived entries' table isn't in this ROM")
    out = []
    while True:
        e, to, step = struct.unpack_from(">hhh", rom, at)
        if e == -1: return out
        out.append((e, max(1, -to) if to <= 0 else -to, step)); at += 6
_NM_DERIVED = None
def nm_derived():
    global _NM_DERIVED
    if _NM_DERIVED is None: _NM_DERIVED = derived_table(open(wmanim.nmp("/home/devbox/work/nomercy.z64"), "rb").read())
    return _NM_DERIVED
def steps_of(table):
    """{base entry: [(entry, k, step) ...]}: each derived entry, its place k after the base and the step"""
    out = {}
    for e, n, step in table:
        if n > 0: out[e] = [(e + k, k, step) for k in range(1, n + 1)]
    return out

def record_map(wm, MV, min_n=4, share=0.9, ident=0.99, nbytes=0x12):
    """WrestleMania 2000's move record byte -> No Mercy's, for bytes 0 .. 0x11 (the two layouts line up there): from every step
    of every move both games have (the very animations, wmanim.nm_move), per byte "same" (equal on 99%+) or the sure values
    of the value table (>= 4 moves, >= 90% of them one No Mercy value). {byte: None (same) | {wm value: nm value}}"""
    P = []
    for b in range(0, len(wmanim.WM1) - 9, 10):
        c, _ = wmanim.nm_move(b)
        if not c: continue
        for k in range(10):
            if wmanim.WM1[b + k] > 0 and MV.wm_rec(b + k) and MV.nm_rec(c[0] + k): P.append((b + k, c[0] + k))
    out = {}
    for k in range(nbytes):
        eq = sum(1 for a, b in P if MV.wm_rec(a)[k] == MV.nm_rec(b)[k])
        if eq >= ident * len(P): out[k] = None; continue
        by = collections.defaultdict(collections.Counter)
        for a, b in P: by[MV.wm_rec(a)[k]][MV.nm_rec(b)[k]] += 1
        out[k] = {v: c.most_common(1)[0][0] for v, c in by.items() if sum(c.values()) >= min_n and c.most_common(1)[0][1] >= share * sum(c.values())}
    return out, len(P)

class Anims:
    """WrestleMania 2000's animation files as No Mercy plays them: (bytes, header, record)"""
    def __init__(self, wm):
        self.wm = wm
        self.nrec = nm_file(NM_ANIM_RECS); self.wrec = wm.files(WM_ANIM_RECS)
        self.nm_of = {}                       # (WM2000 file -> No Mercy's file with the very same bytes)
        nm_by = {}
        for g in range(NM_ANIM0, NM_ANIM0 + NM_ANIM_N):
            p = NMF % g
            if os.path.exists(p): nm_by.setdefault(hashlib.md5(open(p, "rb").read()).hexdigest(), g)
        for f in range(WM_ANIM0, WM_ANIM_END):
            b = wm.files(f)
            if len(b) > 8:
                g = nm_by.get(hashlib.md5(b).hexdigest())
                if g is not None: self.nm_of[f] = g
        self.conv, self.snd, self.b0 = self.learn()
        # (2026-11-20, revmoves2) a file No Mercy has but for the first byte: No Mercy's (the rules above are learned from the
        # files the same throughout, as before)
        self.tail_of = {}
        nm_tail = {}
        for g in range(NM_ANIM0, NM_ANIM0 + NM_ANIM_N):
            p = NMF % g
            if os.path.exists(p):
                d = open(p, "rb").read()
                if len(d) > 8: nm_tail.setdefault(d[1:], g)
        for f in range(WM_ANIM0, WM_ANIM_END):
            b = wm.files(f)
            if len(b) > 8 and f not in self.nm_of and b[1:] in nm_tail: self.tail_of[f] = nm_tail[b[1:]]
        self.cache = {}
    def nm_rec(self, g): return self.nrec[36 * (g - NM_ANIM0):36 * (g - NM_ANIM0) + 36]
    def wm_rec(self, f): return self.wrec[36 * (f - WM_ANIM0):36 * (f - WM_ANIM0) + 36]
    def learn(self):
        """per record byte, WrestleMania 2000's value -> No Mercy's (the most usual among the files both games have), the
        sounds' numbers, and the first byte of a file (its end pose and speed: the same in both, checked)"""
        m = [collections.defaultdict(collections.Counter) for k in range(36)]
        sm = collections.defaultdict(collections.Counter)
        b0 = collections.defaultdict(collections.Counter)
        for f, g in self.nm_of.items():
            wr, nr = self.wm_rec(f), self.nm_rec(g)
            for k in range(36): m[k][wr[k]][nr[k]] += 1
            for a in SOUND_AT:
                if nr[a] and nr[a] == wr[a]: sm[wr[a + 1]][nr[a + 1]] += 1
            b0[self.wm.files(f)[0]][nm_file(g)[0]] += 1
        return ([{v: c.most_common(1)[0][0] for v, c in mk.items()} for mk in m],
                {v: c.most_common(1)[0][0] for v, c in sm.items()},
                {v: c.most_common(1)[0][0] for v, c in b0.items()})
    def ok(self, f): return WM_ANIM0 <= f < WM_ANIM_END and len(self.wm.files(f)) > 8
    def get(self, f):
        """(bytes, header, record) of WM2000 file f, as No Mercy plays it"""
        if f not in self.cache:
            if f in self.nm_of or f in self.tail_of:   # (No Mercy has it byte for byte, or but for byte 0: its own header and record)
                g = self.nm_of.get(f, self.tail_of.get(f)); d = nm_file(g)
                self.cache[f] = (d, d[:4], self.nm_rec(g))
            else:
                d = bytearray(self.wm.files(f)); d[0] = self.b0.get(d[0], d[0])
                wr = self.wm_rec(f)
                rec = bytearray(self.conv[k].get(wr[k], wr[k]) for k in range(36))
                for k in FRAME_AT: rec[k] = wr[k]
                for a in SOUND_AT: rec[a + 1] = self.snd.get(wr[a + 1], wr[a + 1])
                self.cache[f] = (bytes(d), bytes(d[:4]), bytes(rec))
        return self.cache[f]

class Moves:
    """the two games' move tables: animations (files 0x3AE / 0x1F0), records (0x3AF / 0x1F1 -> 0x3AD / 0x1EF)"""
    def __init__(self, wm):
        self.wrec = wm.files(WM_MOVE_RECS); self.nrec = nm_file(NM_MOVE_RECS)
    def wm_flags(self, m):
        r = wmanim.WM2[m]
        return struct.unpack_from(">I", self.wrec, WM_REC_BYTES * r + 0x1C)[0] if r > 0 else 0
    def nm_flags(self, m):
        r = wmanim.NM2[m] if 0 <= m < len(wmanim.NM2) else 0
        return struct.unpack_from(">I", self.nrec, 36 * r + 0x20)[0] if r > 0 else 0
    def wm_shape(self, b):
        """how WrestleMania 2000's move b goes on: (step 0 goes on, step 1 goes on)"""
        return (bool(self.wm_flags(b) & GOES_ON), bool(wmanim.WM1[b + 1] > 0 and self.wm_flags(b) & GOES_ON and self.wm_flags(b + 1) & GOES_ON))
    def nm_shape(self, m):
        return (bool(self.nm_flags(m) & GOES_ON), bool(self.nm_flags(m) & GOES_ON and self.nm_flags(m + 1) & GOES_ON))
    # (2026-11-13, movesteps: a move's whole record -- None for a move with none)
    def wm_rec(self, m):
        r = wmanim.WM2[m] if 0 <= m < len(wmanim.WM2) else 0
        return self.wrec[WM_REC_BYTES * r:WM_REC_BYTES * r + WM_REC_BYTES] if r > 0 else None
    def nm_rec(self, m):
        r = wmanim.NM2[m] if 0 <= m < len(wmanim.NM2) else 0
        return self.nrec[36 * r:36 * r + 36] if r > 0 else None

def fits(i, v):
    """No Mercy move v can be in slot i (port_wm_moveset's rule: the slot's lists, or a move of the slot's kind)"""
    if i not in K.CAN: return False
    if v in K.CAN[i]: return True
    kind = sorted(x for x in K.CAN[i] if x not in (0, 0xFFFF))
    return bool(kind) and kind[0] <= v <= kind[-1]

def wm_moves(wm, e):
    """(2026-11-13, movesteps) WrestleMania 2000's own moveset of record e, by No Mercy's slot: {slot: its move} (its
    strikes' slots moved as port_wm_moveset moves them)"""
    ws, _ = wm2k.unpack_moves(wm.files(e["moves"]), wm.files(e["abilities"]))
    out = {}
    for i0, (m, *_r) in enumerate(ws):
        if i0 in K.WM_BLOCK and i0 not in K.WM_SLOT: continue
        out[K.WM_SLOT.get(i0, i0)] = m
    return out
WM_SLOT_INV = {v: k for k, v in K.WM_SLOT.items()}

def same_steps(b, v):
    """how many of WrestleMania 2000's move b's ten steps play the very animation No Mercy's move v's do"""
    t = [wmanim.nm_anim(x) for x in wmanim.WM1[b:b + 10]]
    u = wmanim.NM1[v - v % 10:v - v % 10 + 10]
    return sum(1 for x, y in zip(t, u) if x is not None and x == y and x)

def nm_plays(AN, x, v):
    """No Mercy's move v plays library move x's animations as they are: its first step's pair, and (x going on) its next
    step's pair and the six files after it -- the very No Mercy files the library's are copies of"""
    f = wmanim.NM1[v] + 0x3322 if 0 <= v < len(wmanim.NM1) and wmanim.NM1[v] else None
    if f is None or AN.nm_of.get(x["fa"]) != f or AN.nm_of.get(x["fa"] + 1) != f + 1: return False
    if not x["cont"]: return True
    g = wmanim.NM1[v + 1] + 0x3322 if wmanim.NM1[v + 1] else None
    return g is not None and all(AN.nm_of.get(c) == g + k for k, c in enumerate(x["cont"]))

def men(wm):
    """(string id, fixes, roster record) of each of the 58, by roster index"""
    fx = G.fixes(); R = wm.roster()
    return [(sid, fx[sid], R[fx[sid]["wm2000"]["roster_index"]]) for sid in sorted(fx, key=lambda s: fx[s]["wm2000"]["roster_index"])]

def how_of(rep):
    """port_wm_moveset's report lines -> per slot how it mapped it ({slot: (WM2000 move, No Mercy move, how)}); a slot with no
    line is "animation" (No Mercy's move with the very animations) or none"""
    how = {}
    for l in rep:
        if " slot " not in l or "WM2000" not in l: continue
        i = int(l.split(" slot ")[1].split(":")[0])
        wmv = int(l.split("WM2000")[1].split()[0]); nmv = int(l.split("->")[1].split()[0])
        how[i] = (wmv, nmv, l.rsplit("(", 1)[1].rstrip(") \n"))
    return how

def mapped(wm, e, f, sid):
    """his slots as the handler maps them, his abilities, and per slot how (how_of)"""
    rep = []
    slots, ab = G.moveset_of(wm, e, f["moveset"]["reference_ordinal"], rep, sid)
    return slots, ab, how_of(rep)

ABOUT = ("his moveset, slot by slot: WrestleMania 2000's moves file mapped to No Mercy's moves (gen/mkroster.py "
         "port_wm_moveset: by the very animation, then by name, else a stand-in or his reference's); a slot whose "
         "WrestleMania 2000 move No Mercy has no move for names it in the built-in WrestleMania 2000 library "
         "(\"wm2000:<its move>@<the No Mercy move it's played on>\": %d of his slots, the later steps of his moves -- a "
         "grapple's variants, the entries after it -- included; tools\\handlers\\wmlib.py); \"records\": WrestleMania "
         "2000's own record values (damage, timing) for the slots that play its moves, where its record says otherwise")

def slots_with_ids(slots, ids):
    """moves.json's slots: the handler's, each slot with a library move naming it by ID"""
    out = [list(s) for s in slots]
    for i, v in ids.items(): out[int(i)][0] = v["id"]
    return out

class Library:
    """the library (its moves, from the 58's movesets) and each Superstar's slots on it (slot_ids)"""
    def __init__(self, wm):
        self.wm = wm
        self.differ = G.use_rom_files(wm)
        self.AN, self.MV = Anims(wm), Moves(wm)
        self.who = men(wm)
        AN, MV = self.AN, self.MV
        # ---- the moves the 58 use that No Mercy has no move for ----------------------------------------------------------
        used = collections.defaultdict(set)
        for sid, f, e in self.who:
            slots, _ = wm2k.unpack_moves(wm.files(e["moves"]), wm.files(e["abilities"]))
            for m, *_ in slots:
                if m not in (0, 0xFFFF): used[m - m % 10].add(sid)
        self.bases = sorted(b for b in used if not wmanim.nm_move(b)[0])
        # ---- the library: one move per different content ---------------------------------------------------------------
        self.lib, self.by_key, self.of_base, self.none = [], {}, {}, []
        for b in self.bases: self.lib_move(b)
        # ---- (2026-11-13, movesteps) each man's slots, and the later steps of the moves they play ---------------------------
        # (the game's derived entries: No Mercy's table for the entries his list fills, WrestleMania 2000's for the steps its
        #  game plays there -- the same for the grapples; record_map: WrestleMania 2000's record values No Mercy's way)
        self.nm_steps, self.wm_steps = steps_of(nm_derived()), steps_of(derived_table(wm.files.rom))
        self.nm_derived_entries = {d for st in self.nm_steps.values() for d, _k, _s in st}
        self.recmap, self.rec_pairs = record_map(wm, MV)
        self.maps = {}
        n_base = len(self.lib)
        for sid, f, e in self.who:
            if sid in PORTED: continue
            slots, ab, how = mapped(wm, e, f, sid)
            self.maps[sid] = (slots, how, wm_moves(wm, e))
            ids, kept, same = self.base_ids(sid, how)
            for d, st in self.variants(sid, ids).items():
                if st.get("wv") is not None and st["why"] == "library": self.lib_move(st["wv"])
        self.n_steps_lib = len(self.lib) - n_base

    def lib_move(self, b):
        """the library move of WrestleMania 2000's move b (a base move, or a later step b + k): its first step's pair and, b
        going on, the step after it (pair and six more files) -- one per different content (self.by_key); None if it has no
        animation pair (self.none says why)"""
        AN, MV = self.AN, self.MV
        if b in self.of_base: return self.of_base[b]
        v0 = wmanim.WM1[b]
        fa = v0 + 0x19DB
        if v0 <= 0 or not (AN.ok(fa) and AN.ok(fa + 1)):
            self.none.append((b, "its first step has no animation pair (value %d)" % v0)); self.of_base[b] = None; return None
        go0, go1 = MV.wm_shape(b)
        cont = None
        if go0 and wmanim.WM1[b + 1] > 0:
            f1 = wmanim.WM1[b + 1] + 0x19DB
            if all(AN.ok(f1 + k) for k in range(8)): cont = [f1 + k for k in range(8)]
            else: self.none.append((b, "its next step's files aren't all there: played without them"))
        key = ((AN.get(fa), AN.get(fa + 1)), tuple(AN.get(x) for x in cont) if cont else None)
        if key in self.by_key:
            self.by_key[key]["same_as"].append(b); self.of_base[b] = self.by_key[key]; return self.of_base[b]
        x = dict(id="wm2000:%d" % b, b=b, fa=fa, cont=cont, key=key, shape=(go0, go1), same_as=[], who=set(), use=collections.Counter())
        self.lib.append(x); self.by_key[key] = x; self.of_base[b] = x
        return x

    def base_ids(self, sid, how, count=False):
        """his slots that play a library move ({slot: {id, wm, was, on, how}}), those that keep the handler's move and why,
        and those whose No Mercy move plays the library move's very animations already (wm2klib's slot_ids: the slots
        themselves; slot_ids adds their later steps and the records)"""
        AN, MV, none = self.AN, self.MV, dict(self.none)
        ids, kept, same = {}, {}, {}
        if sid in PORTED: return ids, kept, same
        for i, (wmv, v, h) in sorted(how.items()):
            if h not in NOT_SAME: continue
            b = wmv - wmv % 10
            if self.of_base.get(b) is None:
                kept[i] = "WrestleMania 2000's %d: %s" % (wmv, none.get(b, "no library move")); continue
            if wmv % 10:
                kept[i] = "WrestleMania 2000's %d is a later step of a move: no library move" % wmv; continue
            x = self.of_base[b]
            if v in (0, 0xFFFF):
                kept[i] = "the slot holds no move in No Mercy (his reference's is %d)" % v; continue
            if len([m for m in K.CAN.get(i, ()) if m not in (0, 0xFFFF)]) == 1:
                # (a slot whose lists hold one move: No Mercy's engine move there for every Superstar -- 255's 12130 -- not a
                #  move of his own; WrestleMania 2000's engine move there is kept out)
                kept[i] = "the slot holds one move for everyone in No Mercy (%d): an engine move, kept" % v; continue
            on, why = v, h
            want = x["shape"] if x["cont"] else (False, False)      # (a library move with no next step: on a move that ends)
            if MV.nm_shape(v) != want:
                # (No Mercy's move goes on otherwise than WrestleMania 2000's: the slot's kind's move that goes on the same way,
                #  most like it in its later steps -- in the slot's own lists first, then the nearest)
                c = [m for m in range(0, len(wmanim.NM1) - 9, 10)
                     if fits(i, m) and wmanim.NM1[m] > 0 and MV.nm_shape(m) == want]
                if not c:
                    kept[i] = "no No Mercy move of the slot's kind goes on as WrestleMania 2000's %d does" % wmv; continue
                on = max(c, key=lambda m: (same_steps(b, m), m in K.CAN[i], -abs(m - v)))
                why = "%s; on %d (the slot's %d goes on otherwise)" % (h, on, v)
            if on == v and nm_plays(AN, x, v):
                # (No Mercy's move here plays these very animations -- the files, headers and records: what the library
                #  carries of the move -- so the slot already is it; no library move needed)
                same[i] = "No Mercy's %d plays WrestleMania 2000's %d's animations already" % (v, wmv); continue
            ids[i] = dict(id="%s@%d" % (x["id"], on), wm=wmv, was=v, on=on, how=why)
            if count: x["who"].add(sid); x["use"][on] += 1
        return ids, kept, same

    def variants(self, sid, ids):
        """(2026-11-13, movesteps) the later steps of his slots' moves: each derived entry d of a slot e that holds a
        WrestleMania 2000 move m -- its game gives d m + k (k = d's place x the step) there, No Mercy's list the move at e + k
        -- {d: {base, k, wv: WrestleMania 2000's step, nv: No Mercy's move there, on: the move the list gives d, why}}, why:
        "library" (a library move of wv, on `on`), "same" (No Mercy's nv plays wv's very animations), or why No Mercy's is
        kept ("WrestleMania 2000 has no such step" ...)"""
        AN, MV = self.AN, self.MV
        slots, how, wm_at = self.maps[sid]
        out = {}
        for e, steps in sorted(self.nm_steps.items()):
            m = wm_at.get(e)
            v = ids[e]["on"] if e in ids else slots[e][0]
            if m in (None, 0, 0xFFFF) or v in (0, 0xFFFF): continue
            wsteps = {k: st for _d, k, st in self.wm_steps.get(WM_SLOT_INV.get(e, e), [])}
            for d, k, st in steps:
                nv = v + st * k
                if k not in wsteps:
                    out[d] = dict(base=e, k=k, wv=None, nv=nv, on=nv, why="WrestleMania 2000's slot makes no such entry: No Mercy's kept"); continue
                wv = m + wsteps[k] * k
                r = dict(base=e, k=k, wv=wv, nv=nv, on=nv)
                out[d] = r
                if wv >= len(wmanim.WM1) or wmanim.WM1[wv] <= 0:
                    # (neither game has the step: nothing to do; No Mercy has one WrestleMania 2000 hasn't: kept, and said)
                    no_nm = not (0 <= nv < len(wmanim.NM1)) or wmanim.NM1[nv] <= 0
                    r["why"] = "none" if no_nm else "WrestleMania 2000 has no step %d of this move (its %d has no animation): No Mercy's %d kept" % (wv % 10, wv, nv); continue
                x = self.lib_move(wv)
                if x is None:
                    r["why"] = "WrestleMania 2000's %d: %s" % (wv, dict(self.none).get(wv, "no library move")); continue
                want = x["shape"] if x["cont"] else (False, False)
                on = nv
                if not (0 <= nv < len(wmanim.NM1)) or wmanim.NM1[nv] <= 0 or MV.nm_rec(nv) is None or MV.nm_shape(nv) != want:
                    # (No Mercy's step there has no animation, or goes on otherwise: the same step of a move of the slot's kind
                    #  that has it and goes on the same way, most like WrestleMania 2000's move in its steps -- the list gives d
                    #  that move, as no packed moveset could)
                    c = [b for b in range(0, len(wmanim.NM1) - 9, 10) if fits(e, b) and b + st * k < len(wmanim.NM1)
                         and wmanim.NM1[b + st * k] > 0 and MV.nm_rec(b + st * k) is not None and MV.nm_shape(b + st * k) == want]
                    if not c:
                        r["why"] = "no No Mercy move of slot %d's kind has a step %d that goes on as WrestleMania 2000's %d does: No Mercy's %d kept" % (e, k * st, wv, nv); continue
                    b = max(c, key=lambda b: (same_steps(m - m % 10, b), b in K.CAN.get(e, ()), -abs(b - (v - v % 10))))
                    on = b + st * k
                if on == nv and nm_plays(AN, x, nv):
                    r["why"] = "same"; continue
                r.update(on=on, why="library", x=x)
        return out

    def records(self, wm_move, nm_move):
        """WrestleMania 2000's record for its move, No Mercy's way (record_map), where it says otherwise than No Mercy's
        move's: {"<byte>": value}"""
        a, b = self.MV.wm_rec(wm_move), self.MV.nm_rec(nm_move)
        if a is None or b is None: return {}
        out = {}
        for k, t in self.recmap.items():
            v = a[k] if t is None else t.get(a[k])
            if v is not None and v != b[k]: out[str(k)] = v
        return out

    def slot_ids(self, sid, how):
        """his slots that play a library move ({slot: {id, wm, was, on, how}}), those that keep the handler's move and why,
        and those whose No Mercy move plays the library move's very animations already -- each entry of the 292, the game's
        derived entries too (2026-11-13, movesteps: a move's later steps, `variants`) -- and (movesteps) the record changes,
        {slot: {byte: value}}: every entry that plays a WrestleMania 2000 move's step (a library move, or No Mercy's with the
        very animations) whose record WrestleMania 2000 has otherwise (self.records)"""
        ids, kept, same = self.base_ids(sid, how, count=True)
        recs = {}
        if sid in PORTED: return ids, kept, same, recs
        slots, _how, wm_at = self.maps[sid]
        # (the slots themselves: a library move's, or one that plays WrestleMania 2000's move's animations -- the handler's
        #  "animation" (every step the very animations: the placed ones too) or "same")
        for i in range(len(slots)):
            m = wm_at.get(i)
            if m in (None, 0, 0xFFFF) or i in self.nm_derived_entries: continue
            if len([x for x in K.CAN.get(i, ()) if x not in (0, 0xFFFF)]) == 1: continue     # (an engine slot: kept)
            h = how.get(i, (m, slots[i][0], "animation"))[2]
            if i in ids: r = self.records(m, ids[i]["on"])
            elif (i in same or h not in NOT_SAME) and slots[i][0] not in (0, 0xFFFF): r = self.records(m, slots[i][0])
            else: continue
            if r: recs[i] = r
        for d, st in self.variants(sid, ids).items():
            if st["why"] == "library":
                x = st["x"]
                ids[d] = dict(id="%s@%d" % (x["id"], st["on"]), wm=st["wv"], was=st["nv"], on=st["on"],
                              how="step %d of slot %d's WrestleMania 2000 %d" % (st["wv"] % 10, st["base"], st["wv"] - st["wv"] % 10)
                                  + ("" if st["on"] == st["nv"] else "; on %d (No Mercy's %d has no such step)" % (st["on"], st["nv"])))
                x["who"].add(sid); x["use"][st["on"]] += 1
                r = self.records(st["wv"], st["on"])
            elif st["why"] == "same":
                same[d] = "No Mercy's %d plays WrestleMania 2000's %d's animations already (step %d of slot %d)" % (st["nv"], st["wv"], st["wv"] % 10, st["base"])
                r = self.records(st["wv"], st["nv"])
            elif st["why"] == "none": r = {}
            else:
                kept[d] = st["why"]; r = {}
            if r: recs[d] = r
        return ids, kept, same, recs

    def write(self, out, rows):
        """movelib_wm2000.bin (revlib.py's layout), move_lib_wm2000.json and rows_wm2000.json into out; a summary printed"""
        AN, lib, none = self.AN, self.lib, self.none
        # ---- the animations, one copy each -------------------------------------------------------------------------------
        anim_ix, anims = {}, []
        def anim_of(c):
            if c not in anim_ix: anim_ix[c] = len(anims); anims.append(c)
            return anim_ix[c]
        for x in lib:
            pk, bk = x["key"]
            x["pair_ix"] = anim_of(pk[0]); anim_of(pk[1])
            if anim_ix[pk[1]] != x["pair_ix"] + 1:       # (a pair is two in a row: the defender's the attacker's + 1)
                x["pair_ix"] = len(anims); anims += [pk[0], pk[1]]
            if bk:
                ix = [anim_ix.get(a) for a in bk]
                if all(k is not None for k in ix) and ix == list(range(ix[0], ix[0] + 8)): x["block_ix"] = ix[0]
                else: x["block_ix"] = len(anims); anims += list(bk)
            else: x["block_ix"] = 0xFFFFFFFF
            x["base"] = x["use"].most_common(1)[0][0] if x["use"] else 0
        # (2026-11-20, revmoves) a double team's third animation, the man's (No Mercy asks the lead's move for file + 2):
        # after the pair, as No Mercy has it -- for a move usually played in a double team's place (entries 163-165's
        # lists), a copy after every other animation, named in the extension after the data (ri_wmmoves.cpp the same)
        more = []
        for k, x in enumerate(lib):
            if x["base"] and any(x["base"] in K.CAN.get(e, ()) for e in (163, 164, 165)) and AN.ok(x["fa"] + 2):
                more.append((k, len(anims), 1)); anims.append(AN.get(x["fa"] + 2))
        # ---- movelib_wm2000.bin: revlib.py's layout ---------------------------------------------------------------------
        strings = bytearray(); data = bytearray(); mv = bytearray(); an = bytearray()
        for x in lib:
            off = len(strings); strings += x["id"].encode() + b"\0"
            mv += struct.pack("<IHHII", off, x["base"], 0, x["pair_ix"], x["block_ix"])
        for b_, hdr, rec in anims:
            assert b_[:4] == hdr and len(rec) == 36
            an += struct.pack("<II", len(data), len(b_)) + rec
            data += b_ + bytes(-len(b_) % 8)
        stamp = int(hashlib.md5(bytes(mv) + bytes(strings)).hexdigest()[:8], 16)
        blob = b"NMMOVLIB" + struct.pack("<IIIIII", 1, len(lib), len(anims), stamp, len(strings), len(data)) + mv + an + strings + data
        if more: blob += b"NMLIBX01" + struct.pack("<I", len(more)) + b"".join(struct.pack("<III", *m) for m in more)
        os.makedirs(out, exist_ok=True)
        open(os.path.join(out, "movelib_wm2000.bin"), "wb").write(blob)
        files = set(); new_files = set()
        for x in lib:
            for f in [x["fa"], x["fa"] + 1] + (x["cont"] or []):
                files.add(f)
                if f not in AN.nm_of: new_files.add(f)
        json.dump({x["id"]: dict(place=k + 1, base=x["base"], wm_move=x["b"], name=wm2k.WM_NAMES.get(x["b"], ""), same_as=x["same_as"],
                                 files=["0x%04X" % f for f in [x["fa"], x["fa"] + 1] + (x["cont"] or [])],
                                 no_mercy_files=["0x%04X" % AN.nm_of[f] if f in AN.nm_of else None for f in [x["fa"], x["fa"] + 1] + (x["cont"] or [])],
                                 goes_on=bool(x["cont"]), who=sorted(x["who"]), n_slots=sum(x["use"].values()))
                   for k, x in enumerate(lib)}, open(os.path.join(out, "move_lib_wm2000.json"), "w"), indent=1)
        json.dump({sid: dict(ported=r["ported"], ids={str(i): v for i, v in r["ids"].items()}, kept={str(i): v for i, v in r["kept"].items()},
                             same={str(i): v for i, v in r["same"].items()},
                             records={str(i): v for i, v in sorted(r.get("records", {}).items())},
                             handler={str(i): dict(wm=w, nm=v, how=h) for i, (w, v, h) in sorted(r["how"].items()) if h in NOT_SAME})
                   for sid, r in rows.items()}, open(os.path.join(out, "rows_wm2000.json"), "w"), indent=1)
        # (by how the handler had mapped each slot: what each way came to)
        tally = collections.Counter(); moves_by = collections.defaultdict(set)
        for r in rows.values():
            for i, (wmv, v, h) in r["how"].items():
                if h not in NOT_SAME: continue
                k = "library" if i in r["ids"] else "same" if i in r["same"] else "kept"
                tally[(h, k)] += 1; moves_by[(h, k)].add(wmv)
        for h in NOT_SAME:
            print("  %-9s slots: %s" % (h, ", ".join("%s %d (%d moves)" % (k, tally[(h, k)], len(moves_by[(h, k)])) for k in ("library", "same", "kept"))))
        sr = set().union(*(moves_by[(h, k)] for h in ("stand-in", "reference") for k in ("library", "same", "kept")))
        own = set().union(*(moves_by[(h, "library")] for h in ("stand-in", "reference")))
        print("  stand-in / reference moves: %d distinct; %d now played as the library's in at least one slot" % (len(sr), len(own)))
        kr = collections.Counter(v.split(" (its")[0] if "has no step" in v else v.split(":")[0] if v.startswith("WrestleMania") else v.split(" (")[0] for r in rows.values() for v in r["kept"].values())
        for k, n in kr.most_common(): print("  kept: %4d %s" % (n, k))
        n_rows = sum(len(r["ids"]) for r in rows.values()); n_kept = sum(len(r["kept"]) for r in rows.values())
        dist = {v["wm"] for r in rows.values() for v in r["ids"].values()}
        print("ROM: %d animation files differ from the project's extraction" % self.differ)
        print("moves in use No Mercy has no move for: %d; library moves: %d (%d the same as another's), %d with no animation" % (
            len(self.bases), len(lib), sum(len(x["same_as"]) for x in lib), len(none)))
        print("movelib_wm2000.bin: %d moves (%d go on), %d animations (%d files, %d of them No Mercy hasn't), %d bytes, stamp %08X" % (
            len(lib), sum(1 for x in lib if x["cont"]), len(anims), len(files), len(new_files), len(blob), stamp))
        print("slots: %d play a library move (%d distinct WrestleMania 2000 moves, %d men), %d kept the handler's" % (
            n_rows, len(dist), sum(1 for r in rows.values() if r["ids"]), n_kept))
        for b, why in none: print("  no library move: %d %s: %s" % (b, wm2k.WM_NAMES.get(b, "?"), why))
        # (2026-11-13, movesteps: the later steps -- the derived entries -- and the records)
        st = collections.Counter()
        for r in rows.values():
            for d in self.nm_derived_entries:
                st["library" if d in r["ids"] else "same" if d in r["same"] else ("kept: " + r["kept"][d].split(":")[0].split(" (")[0]) if d in r["kept"] else "none"] += 1
        print("later steps (derived entries of slots with a WrestleMania 2000 move): %s; %d library moves of steps (%d of them on another No Mercy step)" % (
            dict(st.most_common()), self.n_steps_lib, sum(1 for r in rows.values() for d, v in r["ids"].items() if d in self.nm_derived_entries and v["on"] != v["was"])))
        rb = collections.Counter(b for r in rows.values() for x in r.get("records", {}).values() for b in x)
        print("records: %d entries of %d men get WrestleMania 2000's values (bytes: %s); record_map from %d move steps: %s" % (
            sum(len(r.get("records", {})) for r in rows.values()), sum(1 for r in rows.values() if r.get("records")),
            dict(sorted(rb.items(), key=lambda kv: int(kv[0]))), self.rec_pairs,
            {k: ("same" if t is None else "%d sure values" % len(t)) for k, t in self.recmap.items()}))

def build(out):
    wm = G.WM2000()
    L = Library(wm)
    rows = {}
    for sid, f, e in L.who:
        if sid in PORTED:
            rows[sid] = dict(ported=True, ids={}, kept={}, same={}, how={}); continue
        slots, ab, how = mapped(wm, e, f, sid)
        ids, kept, same, recs = L.slot_ids(sid, how)
        rows[sid] = dict(ported=False, ids=ids, kept=kept, same=same, how=how, records=recs)
    L.write(out, rows)
    return rows

def moves(wrestlers, rows_path):
    """each folder's moves.json with its library moves (the handler's mapping, the slots that play a library move by ID)"""
    rows = json.load(open(rows_path))
    wm = G.WM2000(); G.use_rom_files(wm)
    n = 0
    for sid, f, e in men(wm):
        r = rows[sid]
        if r["ported"]: continue
        d = os.path.join(wrestlers, G.COLLECTION, sid)
        p = os.path.join(d, "moves.json")
        if not os.path.exists(p): print("  %s: no folder" % sid); continue
        j = json.load(open(p))
        slots, ab, how = mapped(wm, e, f, sid)
        # (the handler's mapping: every slot that names no library move, but those the slot rule now places (movesteps: the
        #  handler's "animation, placed" -- they were a stand-in before) -- else he was edited, or made by another import)
        placed = {i for i, (_w, _v, h) in how.items() if h.startswith("animation, placed")}
        if any(list(s) != o for i, (s, o) in enumerate(zip(slots, j["slots"])) if not isinstance(o[0], str) and i not in placed) or list(ab) != j["abilities"]:
            raise SystemExit("%s: moves.json isn't the handler's mapping any more (edited, or another import): not rewritten" % sid)
        mj = {"about": ABOUT % len(r["ids"]), "slots": slots_with_ids(slots, r["ids"]), "abilities": list(ab)}
        if r.get("records"): mj["records"] = r["records"]
        json.dump(mj, open(p, "w"))
        n += 1
    print("moves.json rewritten: %d folders" % n)

if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "lib": build(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "moves": moves(sys.argv[2], sys.argv[3])
    else: raise SystemExit(__doc__)
