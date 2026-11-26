#!/usr/bin/env python3
# storyc.py: a story (JSON) -> a story pack (story.c: the game's Story Mode data + ours). The prototype of
# the exe's compiler (same format). STORY_NOTES.md: the engine's formats; STORY_DESIGN.md: the design.
#
#   python3 gen/storyc.py story.json out.pack
#   python3 gen/storyc.py --header <stories folder> <out.h>   every *.json there (not test_*.json / _*.json) made into
#                                                             packs in a C header (src/story_packs.h: build_mod.ps1)
#   python3 gen/storyc.py --roster <out.json>               the roster (names -> Superstar IDs) for the exe's own
#                                                             compiler (arcade_stories.cpp: <program folder>\stories\_roster.json)
# mod\stories\README.md: the JSON format, for authors.
# (startup 2026-11-19) A Superstar may also be named by the PAIR {"id": "<string ID>", "game_number": "0x...."}, read exactly as
# the exe's compiler reads it (Pack.ref_object) -- the user's approval of 2026-11-19, "Approve, with a review": this compiler
# learns that form and nothing else (review: the project's private notes, REVIEW_STARTUP_CHECKERS). The game's own stories
# name Superstars by string ID; a name ("THE ROCK") is still read for older stories.
import json, struct, sys, os, re, glob

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT0 = 0xE000
MAGIC = 0x4E4D5350

# the championship a story runs as: the list's row = the engine's championship (0x800A9579 -> 0x80097ED1;
# func_800ED28C: 1 has a partner, 6 the women's)
BELTS = {"world": 0, "tag": 1, "ic": 2, "intercontinental": 2, "european": 3, "hardcore": 4, "lightheavy": 5,
         "light heavyweight": 5, "womens": 6, "women's": 6}
# match types: (the engine's type, its slots)
TYPES = {"single": (0, 2), "tag": (1, 4), "triple": (2, 3), "handicap": (3, 3), "fourway": (13, 4),
         "rumble": (9, 36), "single_runin": (14, 4), "cage": (4, 2), "ladder": (5, 2), "ironman": (17, 2),
         "handicap_runin": (18, 4)}
# a run-in (the game's type 14 / 18 matches): the slot says whose side (slot 3 helps slot 1, the opponents; slot 2
# helps slot 0, the player), the part's low byte WHEN he comes (table 0x800F8BA0: data/storyup/result) -- by default
# 0:30 for the opponents' man, 0:35 for the player's (the bytes the compilers always wrote)
RUNIN = {"opponents": 0x0002, "player": 0x0003}
RUNIN_AT = {15: 1, 30: 2, 35: 3, 40: 4, 45: 5, 60: 6, 70: 7, 90: 8, 120: 9, 180: 10}
# a match's script (2026-11-02; data/storyup/result): triggers that act on the match as it plays
MS_TIME, MS_HEALTH, MS_OUTSIDE, MS_HOLD, MS_FINISHER = 1, 2, 3, 4, 5
MA_END, MA_RUNIN, MA_NODQ, MA_NOCOUNT = 1, 2, 3, 4
END_HOW = {"pinfall": 1, "pin": 1, "submission": 2, "tko": 3, "dq": 4, "countout": 5, "count-out": 5, "count out": 5,
           "ladder": 6, "escape": 7}
X_MSCRIPT = 7
# a match's rules: node +E, the engine's rule preset (0x800F5E78 + 12 e: +0 the format -- 0 one on one, 1 tag, 2 three
# men, 0x0D four or more); by match type the game's own (its nodes' card texts name them; data\storyscenes\SCENES.md).
# A type's first is its default. (single: 0x0F, as before.)
RULES = {"single": [("normal", 0x0F), ("no dq", 0x19), ("hardcore", 0x11), ("submission", 0x13), ("anywhere", 0x15),
                    ("street fight", 0x16), ("pinfall only", 0x17), ("first blood", 0x18)],
         "tag": [("normal", 0x1A), ("tornado", 0x1B), ("no dq", 0x1E), ("hardcore", 0x20)],
         "triple": [("normal", 0x23), ("hardcore", 0x24)],
         "handicap": [("normal", 0x25), ("hardcore", 0x26)],
         "fourway": [("normal", 0x28), ("no dq", 0x2A)],
         "rumble": [("normal", 0x29)], "cage": [("normal", 0x2B)], "ladder": [("normal", 0x2B)],
         "ironman": [("normal", 0x2C)]}
RULES["single_runin"] = RULES["single"]; RULES["handicap_runin"] = RULES["handicap"]
# a match held backstage: node +A bits 4-6 the place (func_800EE3C4: 5 the arena's ring; 0..4 the backstage sets 20..24),
# bit 7 set (the game's own: 0x90 / 0xA0 / 0xB0)
LOCATIONS = {"ring": 0x50, "pool hall": 0x90, "parking lot": 0xA0, "boiler room": 0xB0}

# ---- 3D scenes (ovl_b's scene scripts, 0x80125CF8: 4-byte commands; data\storyscenes\SCENES.md has the evidence) ----
# places (op 0x10, low nibble; ovl_b 0x80125870: place -> the set loaded): 0 black (narration), 1..5 backstage (sets
# 20, 24, 23, 21, 22), 6 the match's arena's stage, 7 its ring
PLACES = {"hall": 1, "hallway": 1, "locker room": 2, "dressing room": 2, "boiler room": 3, "pool hall": 4,
          "sports lounge": 4, "bar": 4, "parking lot": 5, "garage": 5, "stage": 6, "entrance": 6, "ramp": 6, "ring": 7}
# a place's spots ("at": 1, 2, ... -> the engine's marks, 0x801259AC: x, y, z, facing), the game's own most used first:
# spots 1 and 2 face each other
SPOTS = {1: [59, 60, 62, 64, 65, 63], 2: [38, 39, 40, 44, 45, 46, 47, 48], 3: [77, 79, 76, 78], 4: [51, 52, 49, 50, 53, 54],
         5: [66, 67, 69, 68, 70, 72], 6: [33, 34, 35, 36, 37], 7: [3, 4, 9, 1, 2, 5, 6, 7, 8, 10]}
# a place's doors: walking in (anim "walk" / "run" at mark m goes to mark m + 1)
DOORS = {1: [55, 57], 5: [70, 72, 74]}
DOORCAM = {55: 77, 57: 83, 70: 112, 72: 114, 74: 113}             # the game's shot of each door as a man comes in
# what a Superstar does (op 0x20: the engine's scene animations, ovl_b 0x8012588C)
ANIMS = {"stand": 2, "mic": 3, "talk": 4, "entrance": 0, "tag entrance": 26, "walk": 10, "run": 22,
         "shout": 5, "point": 6, "interview": 7, "shrug": 8, "rub face": 9, "turn away": 11, "drink": 13, "laugh": 14,
         "punch": 15, "fist up": 16, "arms up": 17, "hurt": 23, "hold out mic": 27, "swing": 28, "fall": 29, "down": 30,
         "get up": 33, "bring it": 41,
         # (2026-11-02) his own taunts: scene animations 18..21 / 24 that story.c points at the actor's moveset entries
         # 227..230 (his four standing taunts) / 235 (his winner's taunt) -- data/storyup/tron
         "taunt": 18, "taunt 1": 18, "taunt 2": 19, "taunt 3": 20, "taunt 4": 21, "win taunt": 24}
TAUNTS = (18, 19, 20, 21, 24)
WALKS = (10, 22)
# the camera (op 0x60: low nibble the actors framed, 0..3 a bit each; the shot: one of the menus' camera paths, fixed
# places in the set -- the rig's survey, data\storyscenes\shots\sheet_r2_cams_*.png): close on one man by his mark,
# two men by their pair of marks, else the place's own (a mark the walks end on: as the spot it's on)
CLOSE = {59: 79, 56: 79, 60: 81, 62: 84, 64: 107, 65: 106, 38: 89, 39: 93, 44: 94, 42: 93, 77: 119, 79: 118,
         51: 99, 52: 101, 50: 97, 53: 103, 66: 111, 71: 111, 67: 109, 70: 115, 0: 73, 33: 73, 34: 75, 37: 75,
         3: 67, 4: 65, 1: 59, 2: 60, 5: 71, 7: 67, 28: 61}
PAIR = {(59, 60): 78, (56, 60): 78, (64, 65): 106, (38, 39): 87, (45, 47): 96, (77, 79): 118, (51, 52): 101,
        (66, 67): 117, (67, 71): 117, (33, 34): 76, (3, 4): 58}
SHOT1 = {1: 82, 2: 94, 3: 120, 4: 101, 5: 108, 6: 74, 7: 63}      # one man, a mark with no shot of its own
SHOT2 = {1: 82, 2: 86, 3: 118, 4: 101, 5: 108, 6: 76, 7: 57}      # two
SHOT3 = {1: 82, 2: 94, 3: 120, 4: 98, 5: 108, 6: 74, 7: 63}       # three or four, and "wide"
EFFECTS = {"music": 7, "quiet": 8}
GAME = {"win": 0x1, "lose": 0x2, "notlose": 0x4}    # the engine's result masks (0x800F8DC4 entries)
OPS = {"==": 0, "!=": 1, "<": 2, "<=": 3, ">": 4, ">=": 5}
C_VAR, C_CHOICE, C_CHANCE, C_ROLE_IS = 1, 2, 3, 4
# (2026-11-02, the story upgrade) more terms, and a condition as a little program (postfix: the branch's flags & 1)
C_GAME, C_HOW, C_ROLE_IN = 5, 6, 7           # a game result mask; how the last match ended; a role's man in a group
C_AND, C_OR, C_NOT = 0x10, 0x11, 0x12
COND_DEPTH = 16                              # story.c's evaluator stack
# how a match ended (story.c: from the match's own result, 0x800A10EC as it ends; data/storyup/result, proved in the
# rig: pinfall 0x4000, submission 0x2000, DQ 0x20000, count-out 0x80000, TKO 0x10000, ladder 0x40000, cage escape
# 0x2000000, ironman decision 0x1000000, 0x1000 no winner -- a draw, as time limit and double count-out)
HOW = {"pinfall": 1, "pin": 1, "submission": 2, "tap out": 2, "dq": 3, "disqualification": 3, "countout": 4,
       "count-out": 4, "count out": 4, "tko": 5, "ladder": 6, "escape": 7, "draw": 8, "decision": 9}
S_SET, S_ADD, S_ROLE_COPY, S_ROLE_SET = 1, 2, 3, 4
S_STORE = 5                                  # a variable = the last question's answer (1, 2 ...)
VARS_MAX = 32                                # story.c STORY_VARS
# voice tokens: {who.field} -> ESC, 0x80 | field (| 0x40: a named Superstar), then the role (0x80 | role) or the
# Superstar (0x80 | id >> 6, 0x80 | id & 63); story.c fills them in as the text is served (the player known by then)
VOICE_FIELDS = {"nickname": 1, "catchphrase": 2, "signoff": 3, "finisher": 4, "insult": 5}
VOICE_KEYS = {"nickname": 1, "catchphrases": 2, "signoff": 3, "finisher": 4, "insult": 5}   # (_voices.json's)
VOICE_MAX = {1: 20, 2: 40, 3: 40, 4: 24, 5: 16}  # characters (a signoff: two lines of 40 at most)
ESC = "\x1b"
# text limits (the game's own: speech bubbles of 3 lines, narration pages of 7; a line what fits on the screen -- QA
# story_width: 48 of any ordinary text, the question's options, in bigger letters, 28; a {name} prints the Superstar's
# short name, 10 letters at most)
LINE_MAX, OPTION_MAX, NAME_LEN = 48, 28, 10
LINES = {"say": 3, "page": 7, "requirement": 3, "ask": 5}
# the pack's extension sections (after the arena list; their count in the header's pad[1]): kind -> its records
X_RECAST, X_GROUPDEF, X_GROUPMEM, X_VARIANT, X_VOICE, X_SCENEOP = 1, 2, 3, 4, 5, 6
SO_COND, SO_SET, SO_STORE, SO_TRON, SO_CAM = 1, 2, 3, 4, 5   # scene ops (op 0x7F, param 0xF000 | op: story.c story_scene_cond)
COMMENTARY = {"jr": "JR: ", "king": "KING: "}
COMMENTARY_SHOT = 67                         # the ring shot that frames JR and King at their table (data/storyup/tron: project.py)
NODE_DEF = dict(a=0x50, b=0x02, c=0x00, e=0x0F, f=0x00, flags=0x0008)
# a match's arena: node +A's low nibble -- the venue byte (0..9) the match is held in, as Exhibition numbers them
ARENAS = ["raw is war", "no mercy", "smackdown", "king of the ring", "summerslam", "survivor series", "royal rumble",
          "wrestlemania", "backlash", "armageddon"]
# ... and the arenas of ours after them (arena.c: arena 10 + k, played in the game's venue XARENA_BASE): the nibble is
# the base venue, the arena itself in the pack's arena list (node, arena) -- before its strings; its count at +0x38
def xarenas():
    src = open(os.path.join(HERE, "..", "src", "arena_files.h")).read()
    base = int(re.search(r"#define XARENA_BASE (\d+)", src).group(1))
    m = re.search(r"xarena_names\[XARENA_N\] = \{([^}]*)\}", src)
    return base, ([n.lower() for n in re.findall(r'"([^"]+)"', m.group(1))] if m else [])

def roster():
    r = {}
    for x in json.load(open(os.path.join(HERE, "roster_base.json"))):
        r[x["name"].strip().upper()] = x["id"]; r.setdefault(x.get("short", "").strip().upper(), x["id"])
    src = open(os.path.join(HERE, "..", "src", "roster_ext.h")).read()
    for m in re.finditer(r'\{\s*0x([0-9A-Fa-f]{4}),\s*\d+,\s*"([^"]+)"', src):
        r.setdefault(m.group(2).strip().upper(), int(m.group(1), 16))
    r.pop("", None)
    # (2026-11-09: each Superstar's string ID, the way stories name them from now on; names keep working. gen/roster_ids.json:
    #  {id: number}, frozen -- mkids.py adds new Superstars, never renames one.)
    ids = os.path.join(HERE, "roster_ids.json")
    if os.path.exists(ids):
        for sid, n in json.load(open(ids)).items(): r["@ID:" + sid] = n
    # (no2k15, 2026-11-23) the built-in Superstars taken out for good (gen/retired_ids.json): his string ID, name and old game
    # number -> his stand-in's game number ("@RETIRED:<key>"), so a story that still names him plays the stand-in (lookup)
    for x in retired():
        stand = r.get("@ID:" + x["stand_in"])
        if not stand: raise SystemExit("retired_ids.json: %s's stand-in %r isn't a built-in Superstar" % (x["id"], x["stand_in"]))
        for k in (x["id"], x["name"].strip().upper(), "0x%04X" % int(x["game_number"], 16)): r["@RETIRED:" + k] = stand
    return r

def retired():
    """gen/retired_ids.json's list (no2k15, 2026-11-23): [{"id", "game_number", "name", "stand_in"}]; none if it isn't there"""
    p = os.path.join(HERE, "retired_ids.json")
    return json.load(open(p, encoding="utf-8"))["retired"] if os.path.exists(p) else []

# (no2k15, 2026-11-23) A Superstar a story names who was taken out for good (gen/retired_ids.json): his stand-in's number, as
# the exe's arcade_roster.h retired_stand_in -- by his string ID or name when nobody on the roster has it, by his old number
# when no "@ID:" has that number now (a folder that has it is his own wrestler: a folder always comes first). 0: not one.
def retired_stand_in(R, w=None, number=0):
    if w is not None:
        for k in (w, re.sub(r"[ -]", "_", w).lower(), w.strip().upper()):
            if ("@RETIRED:" + k) in R: return R["@RETIRED:" + k]
        return 0
    if number and ("@RETIRED:0x%04X" % number) in R and not any(k.startswith("@ID:") and v == number for k, v in R.items()):
        return R["@RETIRED:0x%04X" % number]
    return 0

# (startup 2026-11-19) the pair form's two halves, as the exe reads them (arcade_stories.cpp ref_object, arcade_roster.h):
# "0x...." -> the number after the 0x as C's strtol(text + 2, &end, 16) reads it, accepted only when it reads to the end
# (leading spaces, a sign and a second 0x are strtol's own); anything else 0
def _hex_after_0x(t):
    if not (len(t) > 2 and t[0] == "0" and t[1] in "xX"): return 0
    rest, i = t[2:], 0
    while i < len(rest) and rest[i] in " \t\n\v\f\r": i += 1
    neg = False
    if i < len(rest) and rest[i] in "+-": neg = rest[i] == "-"; i += 1
    hexd = "0123456789abcdefABCDEF"
    if rest[i:i + 1] == "0" and rest[i + 1:i + 2] in ("x", "X") and rest[i + 2:i + 3] != "" and rest[i + 2] in hexd: i += 2
    j = i
    while j < len(rest) and rest[j] in hexd: j += 1
    if j == i or j != len(rest): return 0            # (nothing read, or not read to the end: *end != 0)
    v = int(rest[i:j], 16)
    return -v if neg else v
# a string ID's game number: as written, else as the exe keys IDs (sid_key: A-Z lower case, ' ' and '-' as '_'); 0 if none
def _number_of_sid(R, sid):
    k = "@ID:" + sid
    if k not in R: k = "@ID:" + "".join("_" if c in " -" else (chr(ord(c) + 32) if "A" <= c <= "Z" else c) for c in sid)
    v = R.get(k)
    return v if isinstance(v, int) and not isinstance(v, bool) and 0 < v < 0x1000 else 0

def close_to(name, R):
    """the roster's IDs and names nearest an unknown one, for the message"""
    import difflib
    keys = [k[4:] for k in R if k.startswith("@ID:")] + [k for k in R if not k.startswith("@")]
    return difflib.get_close_matches(name.strip().lower() if name.strip().islower() else name.strip().upper(), keys, n=4, cutoff=0.6)

# groups of Superstars ("player in dx"): gen/roster_groups.json, {group: [Superstars' string IDs]} (the exe: _roster.json's
# "@GROUP:<NAME>": [IDs], made by --roster); a group's IDs in its file order. (startup 2026-11-19: the members by string ID
# -- the user: references by folder / string ID, never the name on screen; a name is still read, as before)
def groups_file(): return os.path.join(HERE, "roster_groups.json")
def groups(R):
    g = {}
    for name, members in json.load(open(groups_file(), encoding="utf-8")).items():
        ids = []
        for m in members:
            if ("@ID:" + m) in R: ids.append(R["@ID:" + m]); continue
            if m.upper() not in R: raise SystemExit("roster_groups.json: %r: who is %r?" % (name, m))
            ids.append(R[m.upper()])
        g[name.strip().upper()] = ids
    return g

# the Superstars' voices (mod\stories\_voices.json; the game's stories\_voices.json): {name: {"id": "0x0001",
# "nickname": ..., "catchphrases": [...], "signoff", "finisher", "insult", "taunts"}}, "default" for anyone else ->
# {id: {field: [strings]}} (id 0 the default)
def voices_file(): return os.path.join(HERE, "..", "stories", "_voices.json")
def voices():
    out = {}
    path = voices_file()
    try: j = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError): return out        # (none, or not JSON: no voices, as the exe)
    if not isinstance(j, dict): return out
    for name, d in j.items():
        if not isinstance(d, dict): continue
        if name == "default": vid = 0
        else:
            i = d.get("id")
            if not isinstance(i, str) or not re.fullmatch(r"0x[0-9A-Fa-f]{1,4}", i): continue
            vid = int(i[2:], 16)
            if not 0 < vid < 0x1000: continue
        f = {}
        for k, fld in VOICE_KEYS.items():
            v = d.get(k)
            v = [v] if isinstance(v, str) else v if isinstance(v, list) else []
            v = [x for x in v if isinstance(x, str)][:15]
            if v: f[fld] = v
        out[vid] = f
    return out

# ---- a 3D scene (a place, the cast on screen, what they do and say, a question), the pages (narration) before it ----
# One scene: one script and one cast -- the men on screen first (actors 0..3: op 0x20's low nibble, the camera's bits),
# then anyone else named in its text. What the compiler knows as it goes (the place, each actor's mark and animation,
# who spoke last) picks the camera's shots and the talking.
class Scene:
    def __init__(sc, pk):
        sc.pk = pk; sc.cast = []; sc.out = []
        sc.place = 0; sc.real = 0; sc.on = {}; sc.doing = {}; sc.speaker = None; sc.dirty = False

    def actor(sc, w, ctx):
        code = sc.pk.who(w)
        if not code: raise SystemExit("%s: who is %r?" % (ctx, w))
        if code not in sc.cast:
            if len(sc.cast) >= 4: raise SystemExit("a scene shows 4 Superstars at most")
            sc.cast.append(code)
        return sc.cast.index(code)

    def gather(sc, steps):                  # the actors, in the order the steps name them
        for st in steps:
            if not isinstance(st, dict): raise SystemExit("a scene step %r isn't a dict" % (st,))
            if "place" in st:
                for c in cast_list(st.get("cast", [])): sc.actor(c.get("who"), "cast")
            for k in ("who", "with"):
                if k in st: sc.actor(st[k], "a step")
            if "on" in st:
                for w in (st["on"] if isinstance(st["on"], list) else [st["on"]]): sc.actor(w, "camera")
            if "if" in st:
                sc.gather(steps_of(st.get("then", []))); sc.gather(steps_of(st.get("else", [])))

    def cmd(sc, b0, b1, p): sc.out.append(struct.pack(">BBH", b0, b1, p))
    def line(sc, op, t):
        sc.cmd(op, 0xFF, sc.pk.vtext(t, sc.cast, "page" if op == 0x32 else "say"))

    def restore(sc):                        # back from black (narration) or after an "if": the place and its men again
        if not sc.real: raise SystemExit("a scene step before any place")
        sc.cmd(0x10 | sc.real, 0x00, 0x0002)
        for a in sorted(sc.on): sc.cmd(0x20 | a, 0x00, (sc.on[a] << 8) | sc.doing[a])
        sc.place = sc.real; sc.dirty = False; sc.speaker = None
    def need3d(sc):
        if sc.place == 0 or sc.dirty: sc.restore()

    def mark_of(sc, st, a):
        if "mark" in st: return num(st["mark"], 0, 98, "mark")
        if "at" in st:
            sp = spots(sc.place)
            return sp[num(st["at"], 1, len(sp), "at (this place's spots)") - 1]
        return sc.on.get(a)

    def camera(sc, acts, shot):
        mask = 0
        for a in acts: mask |= 1 << a
        sc.cmd(0x60 | mask, 0x00, shot)
    def shot(sc, v, acts):
        marks = [sc.on[a] for a in acts if a in sc.on]
        if v is True or v == "auto": return shot_for(sc.place, marks)
        if isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 0xFFFF: return v
        if v == "close": return shot_for(sc.place, marks[:1] or [-1])
        if v == "two": return shot_for(sc.place, (marks + [-1, -1])[:2])
        if v == "wide": return SHOT3[place7(sc.place)]
        raise SystemExit("camera %r: close, two, wide or a number" % (v,))

    def frame(sc, a, st):                   # a speaker: the camera on him, him talking, the one before him standing again
        if a is None or a not in sc.on: return
        b = sc.speaker
        if b is not None and b != a and b in sc.on and sc.doing[b] in (ANIMS["mic"], ANIMS["talk"]):
            sc.doing[b] = ANIMS["stand"]
            sc.cmd(0x20 | b, 0x00, (sc.on[b] << 8) | ANIMS["stand"])
        cam = st.get("camera", True)
        if cam is not False: sc.camera([a], sc.shot(cam, [a]))
        an = anim_of(st["do"]) if "do" in st else ANIMS["mic"] if sc.place in (6, 7, 8) else ANIMS["talk"]
        sc.doing[a] = an
        sc.cmd(0x20 | a, 0x00, (sc.on[a] << 8) | an)
        sc.speaker = a

    def steps(sc, steps):
        for k, st in enumerate(steps):
            try: sc.step(st)
            except SystemExit as e: raise SystemExit("step %d: %s" % (k + 1, e))

    def step(sc, st):
            if not isinstance(st, dict): raise SystemExit("%r isn't a step (a dict)" % (st,))
            if "place" in st:
                pl = place_of(st["place"])
                sc.cmd(0x10 | pl, 0x00, num(st.get("fade", 2), 0, 3, "fade"))
                sc.place = sc.real = pl; sc.on = {}; sc.doing = {}; sc.speaker = None; sc.dirty = False
                acts = []
                for i, c in enumerate(cast_list(st.get("cast", []))):
                    a = sc.actor(c.get("who"), "cast")
                    m = sc.mark_of(c, -1)
                    if m is None:
                        sp = spots(pl)
                        if i >= len(sp): raise SystemExit("cast: say where (\"at\")")
                        m = sp[i]
                    an = anim_of(c.get("do", "stand"))
                    sc.cmd(0x20 | a, wait_of(c.get("wait"), 0), (m << 8) | an)
                    sc.on[a] = m; sc.doing[a] = an; acts.append(a)
                cam = st.get("camera", True)
                if acts and cam is not False: sc.camera(acts, sc.shot(cam, acts))
            elif "narrate" in st:
                if sc.place != 0 or sc.dirty: sc.cmd(0x10, 0x00, 0x0002)
                sc.place = 0; sc.dirty = False
                for t in texts(st["narrate"]): sc.line(0x32, t)
            elif "tron" in st:                  # a man on the Titantron: the game's own Titantron cut (effect 4: his
                # entrance video full screen, the line's bubble over it; story.c / media.c pick his video -- with none,
                # the bubble alone); he's named, not on screen. Away from the arena (its ring / stage): the bubble alone.
                if "say" not in st: raise SystemExit("tron: what he says (\"say\")")
                code = sc.pk.who(st["tron"])
                if not code: raise SystemExit("tron: who is %r?" % (st["tron"],))
                if code not in sc.cast: sc.cast.append(code)
                k = sc.cast.index(code)
                if sc.place == 0: sc.restore()
                if sc.place not in (6, 7, 8):       # (the fallback: never a reason to refuse the story)
                    print("warning: tron %r: no Titantron backstage (ring or stage only): the bubble alone" % (st["tron"],), file=sys.stderr)
                arena = sc.place in (6, 7, 8)
                for t in texts(st["say"]):
                    if arena:          # (in the ring: story.c's shot of the arena's own screen, close -- it skips the next
                        # command, the game's full-screen cut, which is for an arena with no screen of its own)
                        sc.cmd(0x70, 0x01, sc.pk.sceneop(SO_TRON, 0, 0, k | (0x40 if sc.place == 6 else 0)))
                        sc.cmd(0x54, 0x00, 0)
                    sc.line(0x30, t)
                if arena:
                    sc.cmd(0x70, 0x00, sc.pk.sceneop(SO_TRON, 0, 0, 0x80))
                    acts = sorted(sc.on)
                    if acts: sc.camera(acts, sc.shot(True, acts))
            elif "commentary" in st:            # JR / King at the announce table (the ring's: announce.c draws them there):
                # the game's own ring shot with the table in frame, his lines in bubbles, "JR: ...", then the camera back on the men
                c = st["commentary"]
                if not isinstance(c, str) or c.lower() not in COMMENTARY: raise SystemExit("commentary %r: \"jr\" or \"king\"" % (c,))
                if "say" not in st: raise SystemExit("commentary: what he says (\"say\")")
                if sc.real != 7: raise SystemExit("commentary: in the ring (place \"ring\"), where the announce table is")
                if sc.place == 0 or sc.dirty: sc.restore()
                pre = COMMENTARY[c.lower()]
                acts = sorted(sc.on)
                sc.cmd(0x70, 0x01, sc.pk.sceneop(SO_CAM, 0, 0, 0))   # (story.c's close shot of the table; it skips the next)
                sc.camera(acts, COMMENTARY_SHOT)
                for t in texts(st["say"]):
                    if isinstance(t, str): t = pre + t
                    elif isinstance(t, dict): t = {k: pre + v if isinstance(v, str) else v for k, v in t.items()}
                    sc.line(0x30, t)
                if acts: sc.camera(acts, sc.shot(True, acts))
            elif "say" in st:
                a = sc.actor(st["who"], "say") if "who" in st else None
                if a is not None: sc.need3d()
                sc.frame(a, st)
                for t in texts(st["say"]): sc.line(0x30, t)
            elif "ask" in st:
                opts = st.get("options")
                if not isinstance(opts, list) or not 2 <= len(opts) <= 4: raise SystemExit("ask: 2 to 4 options")
                q = st["ask"]
                if not isinstance(q, str) or not all(isinstance(o, str) for o in opts): raise SystemExit("ask: text")
                a = sc.actor(st["who"], "ask") if "who" in st else None
                if a is not None: sc.need3d()
                sc.frame(a, st)
                for o in opts:
                    if "\n" in o: raise SystemExit("ask: an option is one line")
                    if len(re.sub(r"\{([^{}]+)\}", lambda m_: "X" * NAME_LEN, o)) > OPTION_MAX:
                        raise SystemExit("ask: option %r: %d characters at most (a {name} counts %d)" % (o, OPTION_MAX, NAME_LEN))
                sc.pk.fit("\n".join([q] + opts), "ask")
                sc.cmd(0x40 | len(opts), 0x00, sc.pk.text(sc.pk.names("\n".join([q] + opts), sc.cast)))
                if "store" in st:               # the answer kept in a variable (1, 2 ...): a scene op after the question
                    v = st["store"]
                    if not isinstance(v, str) or v not in sc.pk.vars: raise SystemExit("store %r: a variable (\"vars\")" % (v,))
                    sc.cmd(0x70, 0x00, sc.pk.sceneop(SO_STORE, 0, 0, sc.pk.vars[v]))
            elif "if" in st:
                m = re.match(r"choice\s*(==|=)?\s*(\d+)$", st["if"].strip()) if isinstance(st["if"], str) else None
                if m and int(m.group(2)) < 1: raise SystemExit("if: choices count from 1")
                if m:                           # (as before: the engine's own choice test)
                    n = int(m.group(2)) - 1
                    ops = (("then", 0x79), ("else", 0x78))       # (0x79: skip unless the choice; 0x78: if it)
                else:                           # any condition: a scene op of ours (story.c story_scene_cond)
                    if not isinstance(st["if"], str): raise SystemExit("if %r: a condition" % (st["if"],))
                    mask, cs, prog = sc.pk.cond(st["if"], program=True)
                    n = sc.pk.sceneop(SO_COND, len(sc.pk.conds), len(cs), 0)
                    sc.pk.conds += cs
                    # (0x71: skip when it fails; else 0x74: skip when it held -- the then's answer, not asked again --
                    # or, with no then, 0x70: asked, skip when it holds)
                    ops = (("then", 0x71), ("else", 0x74 if "then" in st else 0x70))
                keep = (sc.out, sc.place, sc.real, dict(sc.on), dict(sc.doing), sc.dirty)
                changed = False
                for key, op in ops:
                    if key not in st: continue
                    sc.out = []; sc.place, sc.real, sc.on, sc.doing, sc.dirty = keep[1], keep[2], dict(keep[3]), dict(keep[4]), keep[5]
                    sc.speaker = None
                    sc.steps(steps_of(st[key]))
                    sub = sc.out
                    changed = changed or (sc.place, sc.real, sc.on) != (keep[1], keep[2], keep[3])     # (a place, or men moved)
                    if len(sub) > 255: raise SystemExit("if: 255 commands at most")
                    keep[0].append(struct.pack(">BBH", op, len(sub), n))
                    keep[0].extend(sub)
                sc.out, sc.place, sc.real, sc.on, sc.doing, sc.dirty = keep
                sc.speaker = None
                if changed: sc.dirty = True
            elif "set" in st:                   # variables / roles changed in the scene (a branch's "set")
                ss = sc.pk.setops(st["set"])
                sc.cmd(0x70, 0x00, sc.pk.sceneop(SO_SET, len(sc.pk.sets), len(ss), 0))
                sc.pk.sets += ss
            elif "who" in st:
                sc.need3d()
                a = sc.actor(st["who"], "a step")
                an = anim_of(st.get("do", "stand"))
                if an in WALKS:
                    if "mark" in st: m = num(st["mark"], 0, 97, "mark")
                    else:
                        doors = DOORS.get(place7(sc.place), [])
                        if not doors: raise SystemExit("walk: this place has no door (\"mark\" for one)")
                        m = doors[num(st.get("door", 1), 1, len(doors), "door") - 1]
                    if st.get("camera", True) is not False and m in DOORCAM: sc.camera([a], DOORCAM[m])
                    sc.cmd(0x20 | a, wait_of(st.get("wait"), 60), (m << 8) | an)
                    sc.on[a] = m + 1; sc.doing[a] = ANIMS["stand"]
                elif an in (ANIMS["entrance"], ANIMS["tag entrance"]):
                    if sc.place != 6: raise SystemExit("an entrance is made on the stage")
                    if an == ANIMS["tag entrance"] and ("with" not in st or sc.actor(st["with"], "with") != a + 1):
                        raise SystemExit("a tag entrance: \"with\" his partner, the next man in the scene's cast")
                    sc.cmd(0x20 | a, wait_of(st.get("wait"), 0xFF), an)
                    sc.on.pop(a, None); sc.doing.pop(a, None)
                else:
                    m = sc.mark_of(st, a)
                    if m is None: raise SystemExit("%r isn't on screen: say where (\"at\")" % (st["who"],))
                    sc.cmd(0x20 | a, wait_of(st.get("wait"), 120 if an in TAUNTS else 0), (m << 8) | an)
                    sc.on[a] = m; sc.doing[a] = an
                if sc.speaker == a: sc.speaker = None
            elif "camera" in st:
                sc.need3d()
                if "on" in st: acts = [sc.actor(w, "camera") for w in (st["on"] if isinstance(st["on"], list) else [st["on"]])]
                else: acts = sorted(sc.on)
                if not acts: raise SystemExit("camera: on whom?")
                sc.camera(acts, sc.shot(st["camera"], acts))
            elif "music" in st:
                if st["music"] not in ("on", "off"): raise SystemExit("music: \"on\" or \"off\"")
                sc.cmd(0x50 | (EFFECTS["music"] if st["music"] == "on" else EFFECTS["quiet"]), 0x00, 0)
            elif "wait" in st:
                sc.cmd(0x53, num(st["wait"], 1, 254, "wait (frames)"), 0)
            elif "effect" in st:
                sc.cmd(0x50 | num(st["effect"], 0, 10, "effect"), 0x00, 0)
            elif "raw" in st:
                r = st["raw"]
                if not isinstance(r, str) or not re.match(r"[0-9A-Fa-f]{8}$", r): raise SystemExit("raw %r: 8 hex digits" % (r,))
                sc.out.append(bytes.fromhex(r))
            else:
                raise SystemExit("%r: a step is place, who, say, tron, commentary, ask, narrate, if, set, camera, music, wait, effect or raw" % (st,))

# "teams": [[...], [...]] -> (the match type, its slots): the matches the engine runs (data/storyup/six: no six-man --
# the engine's teams are two of two at most): 1 on 1 (single, or its "type": cage / ladder / ironman), 2 on 2 (tag),
# 1 on 1 on 1 (triple), four of one (fourway), 1 on 2 / 2 on 1 (handicap: the pair first, the one man third)
TEAM_TYPES = {(1, 1): "single", (2, 2): "tag", (1, 1, 1): "triple", (1, 1, 1, 1): "fourway", (1, 2): "handicap",
              (2, 1): "handicap"}
def teams_of(m):
    if "slots" in m: raise SystemExit("a match has slots or teams, not both")
    tm = m["teams"]
    if not isinstance(tm, list) or not all(isinstance(t, list) for t in tm):
        raise SystemExit("teams %r: a list of teams, each a list" % (tm,))
    shape = tuple(len(t) for t in tm)
    if shape not in TEAM_TYPES:
        raise SystemExit("teams %r: 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1 or four of one" % (tm,))
    t = TEAM_TYPES[shape]
    ty = m.get("type", t)
    if ty != t and not (t == "single" and ty in ("cage", "ladder", "ironman")):
        raise SystemExit("teams %r make a %s match, not %r" % (tm, t, ty))
    slots = tm[1] + tm[0] if shape == (1, 2) else [x for team in tm for x in team]
    return ty, slots

def clock_of(t):     # "1:30" / "90" -> seconds
    m = re.fullmatch(r"(\d+):(\d\d)", t) if isinstance(t, str) else None
    if m and int(m.group(2)) < 60 and len(m.group(1)) <= 3: return int(m.group(1)) * 60 + int(m.group(2))
    if isinstance(t, str) and re.fullmatch(r"\d{1,4}", t): return int(t)
    raise SystemExit("time %r: M:SS or seconds" % (t,))

def num(v, lo, hi, what):
    if isinstance(v, bool) or not isinstance(v, int) or not lo <= v <= hi: raise SystemExit("%s %r: %d to %d" % (what, v, lo, hi))
    return v
def texts(v): return v if isinstance(v, list) else [v]
def steps_of(sc):
    if isinstance(sc, list): return sc
    if isinstance(sc, dict):
        head = {k: sc[k] for k in ("place", "cast", "fade", "camera") if k in sc}
        return ([head] if "place" in head else []) + list(sc.get("steps", []))
    raise SystemExit("a scene %r isn't a list or a dict" % (sc,))
def cast_list(c):
    if not isinstance(c, list): raise SystemExit("cast %r isn't a list" % (c,))
    return [x if isinstance(x, dict) else {"who": x} for x in c]
def place7(pl): return 7 if pl == 8 else pl
def spots(pl):
    if place7(pl) not in SPOTS: raise SystemExit("a step before any place")
    return SPOTS[place7(pl)]
def place_of(v):
    if isinstance(v, int) and not isinstance(v, bool) and 1 <= v <= 8: return v
    if isinstance(v, str) and v.lower() in PLACES: return PLACES[v.lower()]
    raise SystemExit("place %r (one of %s)" % (v, ", ".join(PLACES)))
def anim_of(v):
    if isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 60: return v
    if isinstance(v, str) and v.lower() in ANIMS: return ANIMS[v.lower()]
    raise SystemExit("do %r (one of %s)" % (v, ", ".join(ANIMS)))
def wait_of(v, dflt):
    if v is None: return dflt
    if v is True: return 0xFF
    if v is False: return 0
    return num(v, 0, 254, "wait (frames, or true: till it's done)")
def shot_for(pl, marks):
    p = place7(pl)
    if len(marks) == 1: return CLOSE.get(marks[0], SHOT1[p])
    if len(marks) == 2: return PAIR.get(tuple(sorted(marks)), SHOT2[p])
    return SHOT3[p]

class Pack:
    def __init__(s, story):
        s.st = story; s.R = roster()
        s.strings = []; s.nodes = {}; s.parts = []; s.scenes = []; s.cmds = []
        s.branches = []; s.conds = []; s.sets = []
        s.vars = {}; s.var_init = []
        s.roles = {"player": 0, "partner": 1, "champion": 2, "champion2": 3}
        s.role_init = [0] * 8
        s.xarena = []                                   # (node, arena) for the arenas of ours
        s.ext = {k: [] for k in (X_RECAST, X_GROUPDEF, X_GROUPMEM, X_VARIANT, X_VOICE, X_SCENEOP, X_MSCRIPT)}
        s.gidx = {}; s.G = None; s.voice_used = set()
        # (a title on the line: the engine moves the old champion to role 4 when the belt changes hands
        # (func_800EBE0C) -- the story's own roles from 5 then)
        r0 = 5 if any(isinstance(d, dict) and isinstance(d.get("match"), dict) and "title" in d["match"]
                      for d in story.get("nodes", {}).values()) else 4
        for i, (name, who) in enumerate(story.get("cast", {}).items()):
            if r0 + i > 7: raise SystemExit("at most %d cast roles" % (8 - r0))
            s.roles[name] = r0 + i
            if isinstance(who, list):       # a recast: the first of these who isn't the player (or another role's man)
                if not who: raise SystemExit("cast %r: an empty list" % name)
                ids = [s.who(x) for x in who]
                if any(not c or c & 0x8000 for c in ids): raise SystemExit("cast %r: a list of Superstars (no roles)" % name)
                if len(ids) > 8: raise SystemExit("cast %r: 8 Superstars at most" % name)
                s.role_init[r0 + i] = ids[0]
                if len(ids) > 1: s.ext[X_RECAST] += [struct.pack(">BBH", r0 + i, 0, c) for c in ids]
            else: s.role_init[r0 + i] = s.who(who)
        for name, v in story.get("vars", {}).items():
            s.vars[name] = len(s.var_init); s.var_init.append(int(v))
        if len(s.var_init) > VARS_MAX: raise SystemExit("at most %d variables" % VARS_MAX)

    def text(s, t):
        s.strings.append(t); return TEXT0 + len(s.strings) - 1

    # (startup 2026-11-19 -- the user's approval of 2026-11-19, "Approve, with a review": the compilers learn ONLY the new
    #  reference form, nothing else gets looser; the review: private notes REVIEW_STARTUP_CHECKERS) A Superstar named by both
    #  his string ID and his game number, {"id": "aki_man", "game_number": "0x0379"}, exactly as the exe's compiler reads it
    #  (arcade_stories.cpp ref_object, arcade_roster.h resolve_ref): the number when that wrestler IS aki_man, else the string
    #  ID; a string ID nobody has leaves the number to decide. 0 if neither finds anyone.
    def ref_object(s, w):
        number = 0
        if "game_number" in w:
            g = w["game_number"]
            if isinstance(g, int) and not isinstance(g, bool): number = g        # (a JSON integer; true / false are not)
            elif isinstance(g, str): number = _hex_after_0x(g)
            if number <= 0 or number >= 0x1000: raise SystemExit("game_number %r isn't a game number (0x1 .. 0xFFF)" % (g,))
        sid = w["id"] if isinstance(w.get("id"), str) else ""
        if not number and not sid: raise SystemExit("%r: a Superstar needs \"id\" (his string ID) and/or \"game_number\"" % (w,))
        if not sid: return retired_stand_in(s.R, number=number) or number
        by_sid = _number_of_sid(s.R, sid)
        if not by_sid: return retired_stand_in(s.R, sid) or retired_stand_in(s.R, number=number) or number   # (no2k15: retired_ids.json)
        return by_sid

    def who(s, w):        # a slot's part code
        if w in (None, "", "-"): return 0
        if isinstance(w, dict):          # (startup 2026-11-19: string ID + game number, ref_object -- as the exe's who)
            n = s.ref_object(w)
            if n: return n
            raise SystemExit("unknown Superstar: %r" % (w,))
        if not isinstance(w, str): raise SystemExit("unknown Superstar or role: %r" % (w,))
        if w in s.roles: return 0x8000 + s.roles[w]
        n = s.lookup(w)
        if n: return n
        raise SystemExit("unknown Superstar or role: %r (closest: %s)" % (w, ", ".join(close_to(w, s.R))))
    def lookup(s, w):     # string ID ("stone_cold"), else a name (stories before IDs), else "0x0340"; 0 if none (2026-11-09)
        if ("@ID:" + w) in s.R: return s.R["@ID:" + w]
        l = re.sub(r"[ -]", "_", w).lower()          # (an ID in capitals or with spaces, as the exe takes it, 2026-11-12)
        if ("@ID:" + l) in s.R: return s.R["@ID:" + l]
        if w.upper() in s.R: return s.R[w.upper()]
        if re.fullmatch(r"0[xX][0-9A-Fa-f]{1,3}", w): return retired_stand_in(s.R, number=int(w, 16)) or int(w, 16)
        return retired_stand_in(s.R, w)      # (no2k15, 2026-11-23: one taken out for good -- his stand-in; 0 if not one)
    def star(s, w):       # a Superstar by ID or name (not a role) -> his ID
        if isinstance(w, dict):          # (startup 2026-11-19: string ID + game number, ref_object -- as the exe's star)
            n = s.ref_object(w)
            if n: return n
            raise SystemExit("%r isn't a Superstar on the roster" % (w,))
        if isinstance(w, str) and w not in s.roles:
            n = s.lookup(w)
            if n: return n
            raise SystemExit("%r isn't a Superstar on the roster (closest: %s)" % (w, ", ".join(close_to(w, s.R))))
        raise SystemExit("%r isn't a Superstar on the roster" % (w,))

    def group(s, name):   # a group of Superstars ("dx") -> its index in the pack (its members put in once)
        if s.G is None: s.G = groups(s.R)
        g = name.strip().upper() if isinstance(name, str) else ""
        if g not in s.G: raise SystemExit("no group %r (%s)" % (name, ", ".join(sorted(k.lower() for k in s.G))))
        if g not in s.gidx:
            s.gidx[g] = len(s.ext[X_GROUPDEF])
            s.ext[X_GROUPDEF].append(struct.pack(">HH", len(s.ext[X_GROUPMEM]), len(s.G[g])))
            s.ext[X_GROUPMEM] += [struct.pack(">HH", i, 0) for i in s.G[g]]
        return s.gidx[g]

    def sceneop(s, kind, first, n, a):   # -> the 0x7F command's param
        if len(s.ext[X_SCENEOP]) >= 0x1000: raise SystemExit("too many scene tests / sets")
        s.ext[X_SCENEOP].append(struct.pack(">BBHHH", kind, n, first, a, 0))
        return 0xF000 | (len(s.ext[X_SCENEOP]) - 1)

    # a {...} in text: ("name", who) or ("voice", who, field)
    @staticmethod
    def token(inner):
        if "." in inner:
            w, f = inner.rsplit(".", 1)
            f = f.strip().lower()
            if f in VOICE_FIELDS: return ("voice", w.strip(), VOICE_FIELDS[f])
            if f == "name": return ("name", w.strip())
        return ("name", inner)
    def voice(s, w, field):   # a voice token's bytes (ESC ...)
        s.voice_used.add(field)
        if isinstance(w, str) and w in s.roles: return ESC + chr(0x80 | field) + chr(0x80 | s.roles[w])
        i = s.star(w)
        return ESC + chr(0xC0 | field) + chr(0x80 | (i >> 6)) + chr(0x80 | (i & 63))
    # a text's fit (LINES, LINE_MAX: a {name} counts NAME_LEN letters, a voice token its field's longest)
    def fit(s, t, what):
        if not isinstance(t, str): raise SystemExit("text %r isn't a string" % (t,))
        def rep(m):
            k = s.token(m.group(1))
            if k[0] == "voice": return "X" * VOICE_MAX[k[2]] + ("\n" + "X" * VOICE_MAX[3] if k[2] == 3 else "")
            return "X" * NAME_LEN
        ls = re.sub(r"\{([^{}]+)\}", rep, t).split("\n")
        if len(ls) > LINES[what]: raise SystemExit("%r: %d lines at most here" % (t, LINES[what]))
        for l in ls:
            if len(l) > LINE_MAX:
                raise SystemExit("%r: %d characters a line at most (a {name} counts %d, a voice token its longest)" % (t, LINE_MAX, NAME_LEN))
    # a line of text, or one per player: {"THE ROCK": "...", "group:dx": "...", "default": "..."} -> its text id
    def vtext(s, t, cast, what):
        if isinstance(t, dict):
            if "default" not in t: raise SystemExit("lines by Superstar need a \"default\"")
            tid = s.vtext(t["default"], cast, what)
            for k, v in t.items():
                if k == "default": continue
                if not isinstance(k, str): raise SystemExit("%r: a Superstar or group:<name>" % (k,))
                key = 0x8000 | s.group(k[6:]) if k.lower().startswith("group:") else s.star(k)
                if not isinstance(v, str): raise SystemExit("text %r isn't a string" % (v,))
                vid = s.vtext(v, cast, what)
                s.ext[X_VARIANT].append(struct.pack(">HHHH", tid - TEXT0, key, vid - TEXT0, 0))
            return tid
        s.fit(t, what)
        return s.text(s.names(t, cast))

    def part(s, codes, extra=0):
        codes = (list(codes) + [0] * 5)[:5]
        s.parts.append(struct.pack(">5HH", *codes, extra)); return len(s.parts)     # (index from 1)

    # names in text: {player}, {rival}, {KANE} ... -> the engine's %cNN: in a scene the cast's NN-th, on the
    # match card the match's NN-th slot
    def names(s, text, cast, add=True):
        def sub(m):
            k = s.token(m.group(1))
            if k[0] == "voice": return s.voice(k[1], k[2])
            code = s.who(k[1])
            if code not in cast:
                if not add: raise SystemExit("%r isn't in this match" % m.group(1))
                cast.append(code)
            return "%%c%02d" % (cast.index(code) + 1)
        return re.sub(r"\{([^{}]+)\}", sub, text)

    # a scene: narration pages (the black page, a line each). The engine's %cNN names are the cast of the scene
    # loaded last (func_800ED628 -> 0x800BD900), so the match card's requirement text after it names from that
    # same cast: "also" are texts named with it (returned converted).
    def narration(s, pages, also=()):
        if not pages: return (0, list(also)) if also else 0
        pos = len(s.cmds); cast = []
        s.cmds.append(struct.pack(">BBH", 0x10, 0x00, 0x0002))
        for p in pages: s.cmds.append(struct.pack(">BBH", 0x32, 0xFF, s.vtext(p, cast, "page")))
        conv = [s.names(t, cast) for t in also]
        s.cmds.append(struct.pack(">BBH", 0x00, 0xFF, 0x0000))
        first = 0
        for c in cast:
            k = s.part([c])
            if not first: first = k
        if len(cast) > 8: raise SystemExit("a scene names 8 Superstars at most")
        s.scenes.append(struct.pack(">HHBB", first, pos, len(cast), 5))
        return (len(s.scenes), conv) if also else len(s.scenes)

    # a 3D scene ("scene": class Scene above), the pages (narration) before it: one scene, one cast; "also" as narration's
    def scene3d(s, pages, scd, also=()):
        sc = Scene(s)
        steps = steps_of(scd)
        sc.gather(steps)
        pos = len(s.cmds)
        if pages:
            sc.cmd(0x10, 0x00, 0x0002)
            for p in pages: sc.line(0x32, p)
        sc.steps(steps)
        sc.cmd(0x00, 0xFF, 0x0000)
        conv = [s.names(t, sc.cast) for t in also]
        if len(sc.cast) > 8: raise SystemExit("a scene names 8 Superstars at most")
        s.cmds += sc.out
        first = 0
        for c in sc.cast:
            k = s.part([c])
            if not first: first = k
        s.scenes.append(struct.pack(">HHBB", first, pos, len(sc.cast), 5))
        return len(s.scenes), conv

    # a condition: terms with and / or / not and ( ). With "and" alone (as before) -> (the game's mask, [our terms]),
    # all to hold; otherwise (or program=True) a postfix program in the conds -> (0, [records], True)
    @staticmethod
    def cond_tokens(expr):
        toks, words, i, n = [], [], 0, len(expr)
        def flush():
            if words: toks.append(("t", " ".join(words))); words.clear()
        while i < n:
            c = expr[i]
            if c in " \t\r\n": i += 1; continue
            if c in "()": flush(); toks.append((c, c)); i += 1; continue
            j = i
            while j < n and expr[j] not in " \t\r\n()": j += 1
            w = expr[i:j]; i = j
            if w.lower() in ("and", "or", "not"): flush(); toks.append((w.lower(), w))
            else: words.append(w)
        flush()
        return toks
    def term(s, term):    # one term -> (game mask, [records]: all to hold)
        rec = lambda kind, op=0, a=0, b=0: struct.pack(">BBHi", kind, op, a, b)
        low = term.lower()
        if low in GAME: return GAME[low], []
        for pre, g in (("win by ", 0x1), ("lose by ", 0x2), ("ended by ", 0)):
            if low.startswith(pre):
                h = low[len(pre):].strip()
                if h not in HOW: raise SystemExit("%r: by %s" % (term, ", ".join(HOW)))
                return g, [rec(C_HOW, OPS["=="], 0, HOW[h])]
        m = re.match(r"choice\s*(==|=)?\s*(\d+)$", low)
        if m:
            if int(m.group(2)) < 1: raise SystemExit("%r: choices count from 1" % term)
            return 0, [rec(C_CHOICE, 0, 0, int(m.group(2)) - 1)]
        m = re.match(r"chance\s+(\d+)%?$", low)
        if m: return 0, [rec(C_CHANCE, 0, 0, int(m.group(1)))]
        m = re.match(r"(\w+)\s*(==|!=|<=|>=|<|>)\s*(-?\d+)$", term)
        if m and m.group(1) in s.vars:
            return 0, [rec(C_VAR, OPS[m.group(2)], s.vars[m.group(1)], int(m.group(3)))]
        for kw in (" is ", " in "):
            k = low.find(kw)
            if k > 0:
                r, w = term[:k].strip(), term[k + 4:].strip()
                if r not in s.roles: raise SystemExit("%r: %r isn't a role (player, partner, a cast role)" % (term, r))
                if kw == " is ": return 0, [rec(C_ROLE_IS, OPS["=="], s.roles[r], s.star(w))]
                return 0, [rec(C_ROLE_IN, 0, s.roles[r], s.group(w))]
        raise SystemExit("can't read the condition %r" % term)
    def cond(s, expr, program=False):
        if not isinstance(expr, str): raise SystemExit("can't read the condition %r" % (expr,))
        toks = s.cond_tokens(expr)
        if not toks: raise SystemExit("can't read the condition %r" % expr)
        if not program and not any(k in ("or", "not", "(", ")") for k, _ in toks):
            if len(toks) % 2 == 0 or any(k != ("t" if i % 2 == 0 else "and") for i, (k, _) in enumerate(toks)):
                raise SystemExit("can't read the condition %r" % expr)
            mask, out = 0, []
            for k, t in toks[0::2]:
                m_, o_ = s.term(t); mask |= m_; out += o_
            return mask, out, False
        rec = lambda kind, op=0, a=0, b=0: struct.pack(">BBHi", kind, op, a, b)
        pos = [0]
        def peek(): return toks[pos[0]][0] if pos[0] < len(toks) else None
        def unary():      # -> (records, stack depth)
            k = peek()
            if k == "not":
                pos[0] += 1; r, d = unary(); return r + [rec(C_NOT)], d
            if k == "(":
                pos[0] += 1; r, d = disj()
                if peek() != ")": raise SystemExit("%r: a ( without its )" % expr)
                pos[0] += 1; return r, d
            if k == "t":
                t = toks[pos[0]][1]; pos[0] += 1
                m_, o_ = s.term(t)
                r = ([rec(C_GAME, 0, 0, m_)] if m_ else []) + o_
                return r + [rec(C_AND)] * (len(r) - 1), min(len(r), 2)
            raise SystemExit("%r: a term missing%s" % (expr, " before %r" % toks[pos[0]][1] if k else " at the end"))
        def binary(sub, op, code):
            r, d = sub()
            while peek() == op:
                pos[0] += 1; r2, d2 = sub(); r, d = r + r2 + [rec(code)], max(d, d2 + 1)
            return r, d
        def conj(): return binary(unary, "and", C_AND)
        def disj(): return binary(conj, "or", C_OR)
        out, depth = disj()
        if pos[0] != len(toks): raise SystemExit("%r: %r out of place" % (expr, toks[pos[0]][1]))
        if depth > COND_DEPTH or len(out) > 255: raise SystemExit("%r: too long" % expr)
        return 0, out, True

    def setops(s, d):
        if not isinstance(d, dict): raise SystemExit("set %r isn't a dict" % (d,))
        out = []
        for k, v in d.items():
            if k in s.roles:          # a role takes another's Superstar ("rival": "partner") or a named one
                if isinstance(v, str) and v in s.roles: out.append(struct.pack(">BBh", S_ROLE_COPY, s.roles[k], s.roles[v]))
                else: out.append(struct.pack(">BBh", S_ROLE_SET, s.roles[k], s.who(v)))
                continue
            if k not in s.vars: raise SystemExit("unknown variable %r" % k)
            if isinstance(v, str) and v[:1] in "+-": out.append(struct.pack(">BBh", S_ADD, s.vars[k], int(v)))
            else: out.append(struct.pack(">BBh", S_SET, s.vars[k], int(v)))
        return out

    def node(s, n, d, idx):     # a node -> its 18 bytes (its scenes, parts, branches made as it goes)
            if not isinstance(d, dict): raise SystemExit("isn't a node (a dict)")
            title = s.text(d.get("chapter", ""))
            reqtext = d.get("requirement", "")
            s.fit(reqtext, "requirement")
            if "scene" in d:
                scene, (reqtext,) = s.scene3d(d.get("pages", []), d["scene"], (reqtext,))
            else:
                if re.search(r"\{[^{}]+\}", reqtext) and not d.get("pages"):
                    raise SystemExit("a requirement that names Superstars needs pages or a scene before the match (the names come from them)")
                scene, (reqtext,) = s.narration(d.get("pages", []), (reqtext,))
            m = d["match"]
            if not isinstance(m, dict): raise SystemExit("match %r isn't a dict" % (m,))
            tname = m.get("type", "single")
            if "teams" in m: tname, tslots = teams_of(m)
            if not isinstance(tname, str) or tname not in TYPES: raise SystemExit("match type %r" % (tname,))
            typ, nslots = TYPES[tname]
            slots = tslots if "teams" in m else list(m.get("slots", ["player", "-"]))
            runins = m.get("runins", [])
            rslot = {}                  # (slot -> its part's low byte: when he comes)
            if runins:
                # singles: one a side (the game's type 14: slot 2 the player's man, slot 3 the opponents'); a
                # handicap match: one, for the opponents (type 18: slot 3)
                if not isinstance(runins, list) or not all(isinstance(r, dict) and "who" in r for r in runins):
                    raise SystemExit("runins: a list of {\"who\": ...}")
                if typ == 0 and len(runins) <= 2: typ, nslots = TYPES["single_runin"]; base = slots[:2]
                elif typ == 3 and len(runins) == 1: typ, nslots = TYPES["handicap_runin"]; base = slots[:3]
                else: raise SystemExit("run-ins: two in a singles match, one in a handicap match")
                who_at = {}
                for r in runins:
                    side = r.get("helps", "opponents")
                    if side not in RUNIN: raise SystemExit("run-in helps %r: player or opponents" % (side,))
                    if typ == 18 and side != "opponents": raise SystemExit("a handicap match's run-in helps the opponents")
                    i = 3 if side == "opponents" else 2
                    if i in rslot: raise SystemExit("run-ins: one for each side")
                    at = RUNIN[side]
                    if "at" in r:
                        t = r["at"]
                        sec = t if isinstance(t, int) and not isinstance(t, bool) else clock_of(t)
                        if sec not in RUNIN_AT:
                            raise SystemExit("run-in at %r: one of %s" % (t, ", ".join("%d:%02d" % (k // 60, k % 60) for k in sorted(RUNIN_AT))))
                        at = RUNIN_AT[sec]
                    rslot[i] = at; who_at[i] = r["who"]
                slots = base + [who_at.get(i, "-") for i in range(len(base), nslots)]
            if len(slots) != nslots and typ != 9: raise SystemExit("a %s match takes %d" % (m.get("type"), nslots))
            p0 = None
            # a slot: a role / Superstar, or a list of them -- the engine takes the first that can be in the
            # match (not the player's own Superstar, ...), so a list gives a stand-in
            def cands(w): return [c for c in (s.who(x) for x in (w if isinstance(w, list) else [w])) if c]
            for i, w in enumerate(slots):
                extra = rslot.get(i, 0)
                cs_ = cands(w)
                if len(cs_) > 5: raise SystemExit("5 candidates a slot at most")
                k = s.part(cs_, extra)
                if p0 is None: p0 = k
            if not isinstance(m.get("script", []), list) or len(m.get("script", [])) > 16:
                raise SystemExit("script: a list of 16 triggers at most")
            for k, tr in enumerate(m.get("script", [])):
                try: s.trigger(tr, idx[n], typ, rslot)
                except SystemExit as e: raise SystemExit("script %d: %s" % (k + 1, e))
            req = s.text(reqtext)
            b0 = len(s.branches)
            for bi, b in enumerate(d.get("branches", [])):
                try: s.branch(b, idx)
                except SystemExit as e: raise SystemExit("branch %d: %s" % (bi + 1, e))
            if not s.branches[b0:] or struct.unpack(">I", s.branches[-1][:4])[0] or s.branches[-1][6]:
                s.branches.append(struct.pack(">IHBBhHHHBBxx", 0, 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0))   # (the default: the end)
            nd = dict(NODE_DEF)
            if "retries" in d: nd["f"] = (nd["f"] & 0xF0) | (int(d["retries"]) & 0xF)
            # the rules (node +E): the type's own unless named
            rl = RULES.get(tname, [])
            if "rules" in m:
                rn = [e for k, e in rl if k == str(m["rules"]).lower()]
                if not rn: raise SystemExit("rules %r (for %s: %s)" % (m["rules"], tname, ", ".join(k for k, e in rl)))
                nd["e"] = rn[0]
            elif rl: nd["e"] = rl[0][1]
            # a title on the line (node +C: the belt + 1)
            if "title" in m:
                tb = str(m["title"]).lower()
                if tb not in BELTS: raise SystemExit("title %r: one of %s" % (m["title"], ", ".join(BELTS)))
                nd["c"] = (nd["c"] & 0xF0) | (BELTS[tb] + 1)
            # a match backstage (node +A bits 4-7)
            if "location" in m:
                ln = str(m["location"]).lower()
                if ln not in LOCATIONS: raise SystemExit("location %r: one of %s" % (m["location"], ", ".join(LOCATIONS)))
                nd["a"] = (nd["a"] & 0x0F) | LOCATIONS[ln]
            if "arena" in m:
                an = str(m["arena"]).lower()
                xbase, xnames = xarenas()
                if an in ARENAS: nd["a"] = (nd["a"] & 0xF0) | ARENAS.index(an)
                elif an in xnames:
                    nd["a"] = (nd["a"] & 0xF0) | xbase
                    s.xarena.append((idx[n], 10 + xnames.index(an)))
                else: raise SystemExit("arena %r (one of %s)" % (m["arena"], ", ".join(ARENAS + xnames)))
            nd.update(d.get("raw", {}))
            return struct.pack(">HHHHHBBBBBBH", title, scene, req, p0, b0, nd["a"], nd["b"], nd["c"], typ,
                               nd["e"], nd["f"], nd["flags"])

    # a match's trigger: {"when": ..., "do": ...} -> X_MSCRIPT (u16 node, u8 when, u8 its role, u16 its number, u8 do,
    # u8 a1, a2, a3, u16 0): story.c story_match_frame acts on it once, as the match plays
    def trigger(s, tr, node, typ, rslot):
        if not isinstance(tr, dict) or "when" not in tr or "do" not in tr: raise SystemExit("%r: {\"when\": ..., \"do\": ...}" % (tr,))
        w = tr["when"]
        if not isinstance(w, str): raise SystemExit("when %r" % (w,))
        w1 = " ".join(w.split()); low = w1.lower()
        def role(x):
            x = x.strip() if isinstance(x, str) else x
            if not isinstance(x, str) or x not in s.roles: raise SystemExit("%r: a role (player, a cast role) in this match" % (x,))
            return s.roles[x]
        ck, cr, cv = 0, 0, 0
        if low.startswith("time "): ck, cv = MS_TIME, clock_of(low[5:])
        elif low.startswith("health "):
            m_ = re.match(r"health (.+?) ?< ?(\d+)%?$", w1)
            if not m_ or not 1 <= int(m_.group(2)) <= 99: raise SystemExit("when %r: \"health <role> < 30%%\"" % (w,))
            ck, cr, cv = MS_HEALTH, role(m_.group(1)), int(m_.group(2))
        elif low.endswith(" outside"): ck, cr = MS_OUTSIDE, role(w1[:-8])
        elif low.endswith(" in a hold"): ck, cr = MS_HOLD, role(w1[:-10])
        elif low.startswith("finisher by "): ck, cr = MS_FINISHER, role(w1[12:])
        else: raise SystemExit("when %r: time M:SS, health <role> < N%%, <role> outside, <role> in a hold, finisher by <role>" % (w,))
        d_ = tr["do"]
        a1 = a2 = a3 = 0
        if d_ == "end":
            a1 = role(tr.get("winner"))
            if "loser" in tr: a2 = role(tr["loser"])
            elif typ in (0, 4, 5, 14, 17): a2 = 0xFF      # (one on one: the other man)
            else: raise SystemExit("end: \"loser\" (this match has more than two)")
            h = tr.get("how", "pinfall")
            if not isinstance(h, str) or h not in END_HOW: raise SystemExit("end: how %r (%s)" % (h, ", ".join(END_HOW)))
            ak, a3 = MA_END, END_HOW[h]
        elif d_ == "runin":
            side = tr.get("side", "opponents" if 3 in rslot else "player")
            if not isinstance(side, str) or side not in RUNIN or (3 if side == "opponents" else 2) not in rslot:
                raise SystemExit("runin: the match's run-in for %r (\"runins\")" % (side,))
            ak, a1 = MA_RUNIN, 1 if side == "opponents" else 0
        elif d_ == "no dq": ak = MA_NODQ
        elif d_ == "no countout": ak = MA_NOCOUNT
        else: raise SystemExit("do %r: end, runin, no dq or no countout" % (d_,))
        s.ext[X_MSCRIPT].append(struct.pack(">HBBHBBBBH", node, ck, cr, cv, ak, a1, a2, a3, 0))

    def branch(s, b, idx):
        if not isinstance(b, dict): raise SystemExit("%r isn't a branch (a dict)" % (b,))
        if "if" in b: mask, cs, prog = s.cond(b["if"])
        else: mask, cs, prog = 0, [], False
        ss = s.setops(b.get("set", {}))
        nxt = b.get("next", "end")
        nxt = 0xFFFF if nxt == "end" else 0 if nxt == "gameover" else idx[nxt]
        after = s.scene3d(b.get("pages", []), b["scene"])[0] if "scene" in b else s.narration(b.get("pages", []))
        s.branches.append(struct.pack(">IHBBhHHHBBxx", mask, len(s.conds), len(cs), 0, int(b.get("money", 0)),
                                      nxt, after, len(s.sets), len(ss), 1 if prog else 0))
        s.conds += cs; s.sets += ss

    # the voices the story's tokens use: for each field used, every Superstar's (ID order; 0 the default) -- the
    # player and the roles are known only as it runs
    def voice_tables(s):
        if not s.voice_used: return
        V = voices()
        for f in sorted(s.voice_used):
            for vid in sorted(V):
                lines = V[vid].get(f)
                if not lines: continue
                first = len(s.strings)
                s.strings += lines
                s.ext[X_VOICE].append(struct.pack(">HBBHH", vid, f, len(lines), first, 0))

    def build(s):
        st = s.st
        order = list(st["nodes"].keys())
        idx = {n: i + 1 for i, n in enumerate(order)}        # node indexes from 1, in the file's order
        # chapters: a run of nodes with the same chapter title
        ch_first, last = [], None
        for n in order:
            t = st["nodes"][n].get("chapter", last)
            if t != last: ch_first.append(idx[n]); last = t
        if len(ch_first) > 11: raise SystemExit("at most 11 chapters")
        nodes = []
        for n in order:
            try: nodes.append(s.node(n, st["nodes"][n], idx))
            except SystemExit as e: raise SystemExit("node %r: %s" % (n, e))
        s.voice_tables()
        start = idx[st.get("start", order[0])]
        chapters = [start, start] + ch_first + [len(order) + 1] * (12 - len(ch_first))
        hdr = struct.pack(">IHHHHHHHHHHHH", MAGIC, 1, BELTS[st.get("championship", "world")], start, len(nodes),
                          len(s.parts), len(s.scenes), len(s.cmds), len(s.branches), len(s.conds), len(s.sets),
                          len(s.strings), len(s.var_init))
        secs = [(k, s.ext[k]) for k in sorted(s.ext) if s.ext[k]]
        hdr += struct.pack(">14H", *chapters[:14]) + struct.pack(">HH", len(s.xarena), len(secs)) + b"\0" * 4
        assert len(hdr) == 64, len(hdr)
        out = bytearray(hdr)
        def put(chunks):
            for c in chunks: out.extend(c)
            while len(out) % 4: out.append(0)
        put(nodes); put(s.parts); put(s.scenes); put(s.cmds); put(s.branches); put(s.conds); put(s.sets)
        put([struct.pack(">i", v) for v in s.var_init])
        out.extend(struct.pack(">8H", *s.role_init))
        for nd_, a in s.xarena: out.extend(struct.pack(">HH", nd_, a))
        for k, recs in secs:                            # (the extension sections: kind, count, records)
            out.extend(struct.pack(">HH", k, len(recs)))
            for r in recs: out.extend(r)
        offs, blob = [], bytearray()
        for t in s.strings:
            offs.append(len(blob)); blob.extend(t.encode("latin1", "replace") + b"\0")
        out.extend(struct.pack(">%dH" % len(offs), *offs)); out.extend(blob)
        while len(out) % 4: out.append(0)
        return bytes(out)

# _voices.json's lines must fit the limits the compilers count them at (VOICE_MAX; a signoff two lines, the rest one)
def check_voices():
    for vid, f in voices().items():
        for fld, lines in f.items():
            for t in lines:
                ls = t.split("\n")
                if len(ls) > (2 if fld == 3 else 1) or any(len(l) > VOICE_MAX[fld] for l in ls):
                    raise SystemExit("_voices.json: 0x%04X's %s %r: %d characters a line at most%s" % (
                        vid, [k for k, v in VOICE_KEYS.items() if v == fld][0], t, VOICE_MAX[fld], ", two lines" if fld == 3 else ", one line"))

def check(st, name):
    for k in ("nodes",):
        if k not in st: raise SystemExit("%s: no %r" % (name, k))
    if st.get("championship", "world").lower() not in BELTS:
        raise SystemExit("%s: championship %r: one of %s" % (name, st.get("championship"), ", ".join(sorted(BELTS))))
    st["championship"] = st.get("championship", "world").lower()
    for n, d in st["nodes"].items():
        if "match" not in d: raise SystemExit("%s: node %r has no match" % (name, n))
        for b in d.get("branches", []):
            nx = b.get("next", "end")
            if nx not in ("end", "gameover") and nx not in st["nodes"]:
                raise SystemExit("%s: node %r: a branch goes to %r, which isn't a node" % (name, n, nx))
    menu = st.get("menu", "Custom Story")
    if len(menu) > 24: raise SystemExit("%s: menu %r: 24 letters at most" % (name, menu))

def header(folder, out):
    check_voices()
    files = [f for f in sorted(glob.glob(os.path.join(folder, "*.json")))
             if not os.path.basename(f).startswith(("test_", "_")) and os.path.basename(f) != "story.schema.json"]
    lines = ["// Made by gen/storyc.py (build_mod.ps1) from the stories in mod/stories: don't edit. story.c reads it.",
             "#define STORY_PACKS_N %d" % len(files)]
    ents = []
    for i, f in enumerate(files):
        st = json.load(open(f, encoding="utf-8"))
        name = os.path.basename(f)
        check(st, name)
        try: data = Pack(st).build()
        except SystemExit as e: raise SystemExit("%s: %s" % (name, e))
        lines.append("// %s: %s (%d bytes)" % (name, st.get("title", ""), len(data)))
        lines.append("static const u8 story_pack_%d[%d] __attribute__((aligned(16))) = {" % (i, len(data)))
        for k in range(0, len(data), 24):
            lines.append("    " + ", ".join("0x%02X" % b for b in data[k:k + 24]) + ",")
        lines.append("};")
        menu = st.get("menu", "Custom Story").replace("\\", "\\\\").replace('"', '\\"')
        ents.append('    { story_pack_%d, "%s", %d },' % (i, menu, BELTS[st["championship"]]))
        print("  %s -> %d bytes (%s)" % (name, len(data), st.get("title", "")))
    if not ents: ents.append('    { 0, "", 0 },')
    lines.append("static const struct { const u8* data; const char* menu; u8 belt; } story_packs[] = {")
    lines += ents
    lines.append("};")
    txt = "\n".join(lines) + "\n"
    if not os.path.exists(out) or open(out, encoding="utf-8").read() != txt:
        open(out, "w", encoding="utf-8", newline="\n").write(txt)
    print("%s: %d stories" % (out, len(files)))

if __name__ == "__main__":
    if sys.argv[1] == "--header":
        header(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "--roster":
        os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
        r = roster()
        # (the arenas of ours, for the exe's compiler: "@ARENA:<NAME>" -> 10 + k, "@ARENA_BASE" -> the venue they play in)
        xbase, xnames = xarenas()
        r["@ARENA_BASE"] = xbase
        for k, an in enumerate(xnames): r["@ARENA:" + an.upper()] = 10 + k
        # (the groups: "@GROUP:<NAME>" -> their IDs, in order)
        for g, ids in groups(roster()).items(): r["@GROUP:" + g] = ids
        with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
            json.dump(r, f, indent=0, sort_keys=True)
        print("%s: %d names" % (sys.argv[2], len(r)))
    else:
        st = json.load(open(sys.argv[1], encoding="utf-8"))
        check(st, os.path.basename(sys.argv[1]))
        data = Pack(st).build()
        open(sys.argv[2], "wb").write(data)
        print("%s: %d bytes" % (sys.argv[2], len(data)))
