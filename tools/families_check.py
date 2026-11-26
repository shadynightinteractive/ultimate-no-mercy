#!/usr/bin/env python3
r"""families_check.py -- the game's rules for families.json and nested wrestler folders, in Python (families, 2026-11-14),
for the editor. The FORMATS text (FORMATS_families.txt, "N. Families") says the same with examples; the tags match.

A FAMILY is one box on the Superstar Select: an ordered list of VARIANTS, each one wrestler's variant (one of his four
looks). The Type arrows step through them in the list's order; each variant plays as its own wrestler (his model, name,
moves, music, Titantron). Families live beside the wrestlers, never inside a wrestler's folder:

  <wrestlers>\families.json                 the wrestlers folder's own (read first)
  <wrestlers>\<collection...>\families.json  any collection folder's (not a wrestler's folder, not one starting _ or .),
                                             read after it, in case-insensitive path order (the user's D2)

  {
   "about": "anything (not read)",
   "families": [
    {"id": "chris_jericho",                                  optional: the family's name (the log, the editor)
     "unlisted": "off",                                      optional (U1): his other variants left out ("on": kept)
     "variants": [{"wrestler": "chris_jericho", "variant": 1},   his string ID (or his game number "0x0301"),
                  {"wrestler": "chris_jericho~2", "variant": 1}, and which of his variants, 1-4
                  ...]}
   ],
   "hidden": [{"wrestler": "kane"}, {"wrestler": "undertaker", "variant": 2}]     optional (H1-H3)
  }

  "wrestler" may also be a PAIR, {"id": "aki_man", "game_number": "0x0379"} (startup 2026-11-19; the user's approval of
  2026-11-19, "Approve, with a review" -- this checker learns that form and nothing else; review: the project's private
  notes, REVIEW_STARTUP_CHECKERS): his string ID when a wrestler has it, whatever number that wrestler has now (an import
  renumbered by an ID clash is still found); else the game number, read as a plain "wrestler" value is. As the exe's
  families_read who. Its other keys aren't read (a warning), and a clash ID in its "id" is warned of as a plain one.

What the game does (exe arcade_wrestlers.cpp "families", mod variants.c), rule for rule:
  F1  No families.json anywhere (or none with a "families" list): the families the game always had (DEFAULT below:
      Undertaker + Undertaker 1999, Big Show + The Giant, the Revenge ports' second looks). Once any file has a "families"
      list, the files are ALL there is (an empty list: no families at all) -- so a file should carry the default ones to
      keep them (--write-default writes them). A file with only "hidden" replaces nothing.
  F2  A file that isn't JSON, or whose "families" / "hidden" isn't a list: not read (the log says so).
  F3  A family needs a "variants" list; a variant needs "wrestler" and "variant" (1-4). A malformed one is left out (an
      ERROR). A well-formed one naming a wrestler who isn't there now (a deleted or not yet imported folder, an unknown ID)
      is left out WHILE he isn't (a WARNING): when his folder comes back he is back in his place. A family with none left
      is left out.
  F4  A wrestler is in ONE family: his variants in a later family (any file, in reading order) are left out. That is the
      only thing the order of families (and of files) changes: which family a wrestler named twice ends up in. Where a
      family's box is depends on its first wrestler alone (F6), never on where the family is in the file.
  F5  A variant (wrestler, variant) is in a family once: a repeat is left out.
  F6  The box is the FIRST variant's wrestler's (its "slot"): where his box would be on the pages. Every other wrestler of
      the family has no box of his own -- except for variants the family doesn't list (U1). No limit on how many.
  U1  PARTIAL family: a family listing only some of a wrestler's variants never makes the others vanish -- the first
      wrestler's unlisted variants go at the family's END; any other wrestler's unlisted ones keep a box of HIS OWN,
      where his box is (his Type arrows step through those). "unlisted": "off" on a family leaves them out (the default
      data says so for the Revenge ports' second looks, which show their first look only, as always). Hidden ones never.
  N1  NESTING: a folder with its own wrestler.json inside a wrestler's folder (any depth) is a wrestler like any other
      (ID, number, clash rules unchanged) in the OUTERMOST wrestler's family: the outer's variants 1-4, then each nested
      one's 1-4, nested ones in case-insensitive path order. Inside a wrestler's folder nothing else counts: no collection,
      no stock_roster.json or families.json there (N4). A nested one whose outer wrestler doesn't load: his own box.
  N2  The data wins: a families.json naming a nested wrestler puts him where it says.
  N3  A nested wrestler the data doesn't name joins the end of the outer wrestler's family (a data one if he has one).
  H1  "hidden": {"wrestler": W} hides him, {"wrestler": W, "variant": k} that variant. A malformed entry is an ERROR; a
      well-formed one naming nobody there now is a WARNING (it hides him when he is).
  H2  Hidden: no box, never stepped to, never drawn at random (Random, the Royal Rumble's field and its looks); whatever
      names him keeps him (titles, stories, presets, saves). A hidden first wrestler: the next visible variant heads the
      box in his place; every variant hidden: no box.
  H3  A wrestler in no family with some variants hidden: the Type arrows step through the rest.
  In the game, as it goes (not errors -- the file is fine):
  G1  A wrestler off the roster (an option off, no folder) or removed is left out of the family while he is; with only
      one wrestler left he has his own box again, as with no family.
  G2  A REMOVED first wrestler (a stock man whose folder is deleted): the family STAYS in his box's place, headed by the
      next variant in play (the user, 2026-11-14). Members in folders of their own are not removed with him; folders
      nested in his are deleted with it, so they are gone.
  G3  big_show (0x0201) is in play only with the Big Show option on: off, that number is Chris Benoit's box.
  G4  The select's "Type N" is the variant's own look (1-4); beside it the game shows its place in the family, "7 / 12".
  G5  A wrestler has four variants (the game's record has four looks). A full-detail (2K15) model is one model, found by
      his photo: all four variants draw it (with the folder's photo) -- list his variant 1 only, unless his looks differ.
  A1  (automerge, 2026-11-23) F2 > GAME SETTINGS > MERGE SAME WRESTLERS (the mod's option "auto_merge", off to start
      with; read at the start-up): ON, the game makes at start-up the families the roster editor's "Consolidate
      automatically..." makes with its ticked ("same") proposals, by the editor's own rule and data (its app/families.py,
      data/aliases.json, data/builtin_roster.json -- the game ships those two files in its data folder) -- as though the
      editor had written that families.json, but without writing anything: the files above are read first, a family
      they make stays (the others added at its end), wrestlers in two or more families are left as they are, hidden
      stays hidden. --auto-merge on|off|game checks with it (game: as the game folder's settings say); this checker
      asks the editor (beside it, or --editor) for the rule, it never has a copy of its own.

ERROR = something malformed the game leaves out (F2-F5, H1) -- an editor should never write one. WARNING = legal but
probably not meant, or not in play now (an ID with ~N, which changes when folders move; a wrestler not there now; a family
of one variant; default families missing under F1; G2/G3 cases).

Use:
  python families_check.py <wrestlers folder> [--file <families.json> ...] [--roster <stories\_roster.json>] [--json]
  python families_check.py --write-default <families.json>
  python families_check.py <wrestlers folder> --auto-merge on|off|game [--editor <the roster editor's folder>]   (A1)
  (library) check_families(root, files=None, roster_path=None) -> Report;  check_families_json(obj, known) -> (errors, warnings)
It reads the wrestlers folder with wrestler_check.py (beside this file: the editor's) for the IDs the game gives, its walk
swapped for nested_walk (N1).
Exit code: 0 no errors, 1 errors, 2 usage.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# F1: the default data, as the exe has it (default_families_json): (family id, [(wrestler, variant 1-4), ...], unlisted off)
_FOUR = (1, 2, 3, 4)
DEFAULT = [
    ("undertaker", [("undertaker", v) for v in _FOUR] + [("undertaker_1999", v) for v in _FOUR]),
    ("big_show", [("big_show", v) for v in _FOUR] + [("the_giant", v) for v in _FOUR]),
]
# (2026-11-15, revfix -- the user: the Revenge ports get exactly Revenge's own variants) The six second looks' families are
# gone from the default data, and the looks are hidden by the game whatever a file says (exe DROPPED_LOOKS).
DEFAULT_UNLISTED_OFF = set()
DROPPED_LOOKS = ("bret_hart_b", "randy_savage_b", "scott_steiner_b", "booker_t_b", "stevie_ray_b", "rey_mysterio_b")
BIG_SHOW = 0x0201
KNOWN_FAMILY_KEYS = {"id", "name", "about", "variants", "unlisted"}
KNOWN_VARIANT_KEYS = {"wrestler", "variant", "about"}


def default_json():
    fams = []
    for i, vs in DEFAULT:
        f = {"id": i}
        if i in DEFAULT_UNLISTED_OFF: f["unlisted"] = "off"
        f["variants"] = [{"wrestler": w, "variant": v} for w, v in vs]
        fams.append(f)
    return {"about": "The families the game has with no families.json anywhere (2026-11-14): each one select box, its "
                     "variants in the order the Type arrows step through them. Edit freely: once this file is here, it is "
                     "what the game reads.",
            "families": fams}


def shown_path(path, root):
    """path relative to root for messages; as it is when it can't be (another drive on Windows: point 6 of round 4)."""
    if not root: return path
    try: return os.path.relpath(path, root)
    except ValueError: return path


def load_json(path):
    """nlohmann's parse as the loader calls it: no comments, no NaN; a UTF-8 BOM is fine. None if it isn't valid JSON."""
    try:
        with open(path, "rb") as f: raw = f.read()
        return json.loads(raw.decode("utf-8-sig"), parse_constant=lambda c: (_ for _ in ()).throw(ValueError(c)))
    except (OSError, ValueError, UnicodeDecodeError):
        return None


def _wc():
    import wrestler_check
    return wrestler_check


def number_of(v):
    return _wc().number_of(v)


def clean_id(s):
    return _wc().clean_id(s)


def nested_walk(root, nested_in):
    """wrestler_check.walk with the NESTING route (N1): folders with a wrestler.json at any depth, INSIDE other wrestlers'
    folders too (nested_in: such a folder -> the outermost wrestler's folder it is in); inside a wrestler's folder nothing
    else counts (no collection, no stock_roster.json); _ and . folders skipped with all they hold; links not followed."""
    found, collections = [], []
    if os.path.exists(os.path.join(root, "stock_roster.json")): collections.append(root)
    def go(d, top):
        try: names = sorted(os.listdir(d))
        except OSError: return
        for name in names:
            p = os.path.join(d, name)
            if name.startswith(("_", ".")) or not os.path.isdir(p) or os.path.islink(p): continue
            if os.path.exists(os.path.join(p, "wrestler.json")):
                found.append(p)
                if top: nested_in[p] = top
                go(p, top or p)
            elif top: go(p, top)
            else:
                if os.path.exists(os.path.join(p, "stock_roster.json")): collections.append(p)
                go(p, None)
    go(root, None)
    return found, collections


class Known:
    """The wrestlers the game knows, as the exe's scan leaves them: string ID -> game number; number -> string ID; who is
    removed (by number); and which numbers are built-in."""
    def __init__(self):
        self.sids, self.whose, self.removed, self.builtin = {}, {}, set(), set()
        self.nests = []          # (top number, [nested numbers in order]) -- the NESTING route (N1-N4)
        self.nest_notes = []

    def who_one(self, v):
        """A plain "wrestler" value -> his game number (0: nobody), as the exe's families_read who_one: a number someone has,
        else a string ID."""
        n = number_of(v)
        if 0 < n < 0x1000: return n if n in self.whose else 0
        if not isinstance(v, str): return 0
        return self.sids.get(clean_id(v), 0)

    def who(self, v):
        """The "wrestler" value -> his game number (0: nobody), as the exe's families_read who. (startup 2026-11-19, the
        user's approval of 2026-11-19, "Approve, with a review": the PAIR form, and only it, is new.) A pair {"id": "aki_man",
        "game_number": "0x0379"}: his string ID when a wrestler has it (whatever number that wrestler has now), else the
        game number as a plain value is read; anything not an object reads as it always did."""
        if not isinstance(v, dict): return self.who_one(v)
        by_number = self.who_one(v["game_number"]) if "game_number" in v else 0
        if not isinstance(v.get("id"), str): return by_number
        sid = clean_id(v["id"])
        return self.sids[sid] if sid in self.sids else by_number

    def name(self, n):
        return self.whose.get(n, "0x%04X" % n)


def known_from_folder(root, roster_path=None):
    """Known from a wrestlers folder, by the editor's wrestler_check (rules 0.82 with removal: what the game ships)."""
    wc = _wc()
    k = Known()
    R = wc.builtin_roster(roster_path)
    for key in sorted(R):
        v = R[key]
        if key.startswith("@ID:") and isinstance(v, int) and not isinstance(v, bool) and 0 < v < 0x1000:
            k.sids[key[4:]] = v; k.whose[v] = key[4:]; k.builtin.add(v)
    if root and os.path.isdir(root):
        nested_in = {}
        plain_walk = wc.walk
        wc.walk = lambda r: nested_walk(r, nested_in)       # (the game's scan, nested folders too: N1)
        try: rep = wc.check_folder(root, "0.82", roster_path, True)
        finally: wc.walk = plain_walk
        number = {os.path.normpath(e.folder): e.number for e in rep.entries if e.status in ("loaded", "stock", "stock_data") and e.number}
        by_top = {}
        for inner, top in nested_in.items(): by_top.setdefault(top, []).append(inner)
        for top in sorted(by_top):
            rel = lambda p: os.path.relpath(p, top).replace("\\", "/")
            kids_paths = sorted(by_top[top], key=lambda p: (wc.lower_ascii(rel(p)), rel(p).encode("utf-8", "surrogateescape")))
            tn = number.get(os.path.normpath(top))
            kids = []
            for p in kids_paths:
                n = number.get(os.path.normpath(p))
                if not n: continue
                if not tn: k.nest_notes.append("%s is nested in %s, which isn't loaded: no family, a wrestler of his own" % (os.path.relpath(p, root), os.path.relpath(top, root)))
                else: kids.append(n)
            if tn and kids: k.nests.append((tn, kids))
        for e in rep.entries:
            if e.status in ("loaded", "stock", "stock_data") and e.sid and e.number:
                k.sids[e.sid] = e.number
                if e.status == "loaded" or e.number not in k.whose: k.whose[e.number] = e.sid
        for sid, n, _ in rep.removed: k.removed.add(n)
    return k


def family_files(root):
    """The families.json files in the order the game reads them (F1): the folder's own, then the collections'."""
    wc = _wc()
    out = []
    if os.path.isfile(os.path.join(root, "families.json")): out.append(root)
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        keep = []
        for d in sorted(dirnames):
            if d.startswith("_") or d.startswith(".") or os.path.islink(os.path.join(dirpath, d)): continue
            p = os.path.join(dirpath, d)
            if os.path.isfile(os.path.join(p, "wrestler.json")): continue        # (a wrestler's folder: nothing in it is read)
            if os.path.isfile(os.path.join(p, "families.json")): found.append(p)
            keep.append(d)
        dirnames[:] = keep
    rel = lambda p: os.path.relpath(p, root).replace("\\", "/")
    found.sort(key=lambda p: (wc.lower_ascii(rel(p)), rel(p).encode("utf-8", "surrogateescape")))
    return [os.path.join(p, "families.json") for p in out + found]


class Report:
    def __init__(self):
        self.files = []          # (path, errors, warnings)
        self.families = []       # (where, family id, slot number, [(number, variant 1-4)]) -- what the game ends up with
        self.hidden = {}         # number -> mask of hidden variants (bit k: variant k + 1; 0xF: he is hidden)
        self.notes = []
        self.from_default = False

    @property
    def errors(self): return [(f, e) for f, es, _ in self.files for e in es]

    @property
    def warnings(self): return [(f, w) for f, _, ws in self.files for w in ws] + [("", n) for n in self.notes]


def check_hidden_json(j, known, hidden):
    """A file's "hidden" list (H1) into hidden (number -> mask) -> (errors, warnings)."""
    errs, warns = [], []
    if "hidden" not in j: return errs, warns
    if not isinstance(j["hidden"], list): return ['"hidden" must be a list: not read (H1)'], warns
    for k, h in enumerate(j["hidden"]):
        at = "hidden %d" % (k + 1)
        if not isinstance(h, dict) or "wrestler" not in h:
            errs.append('%s: needs "wrestler": not read (H1)' % at); continue
        n = known.who(h["wrestler"])
        if not n:
            warns.append("%s: no wrestler %s now: hides him when he is there (H1)" % (at, json.dumps(h["wrestler"]))); continue
        for key in h:
            if key not in ("wrestler", "variant", "about"): warns.append('%s: "%s" isn\'t read' % (at, key))
        if "variant" in h:
            look = number_of(h["variant"])
            if not 1 <= look <= 4: errs.append('%s: "variant" must be 1-4: not read (H1)' % at); continue
            hidden[n] = hidden.get(n, 0) | (1 << (look - 1))
        else:
            hidden[n] = 0xF
        warns += pair_warnings(at, h["wrestler"])
        if isinstance(_sid_of(h["wrestler"]), str) and "~" in _sid_of(h["wrestler"]):
            warns.append('%s: %s is a clash ID (two folders, one ID): it changes if folders move -- give that folder an "id" of its own' % (at, _sid_of(h["wrestler"])))
    return errs, warns


def _sid_of(w):
    """(startup 2026-11-19) the string ID a "wrestler" value names: itself, or a pair's "id" -- for the clash-ID warning, which
    the pair form gets as a plain string ID does (nothing looser)"""
    return w.get("id") if isinstance(w, dict) else w


def pair_warnings(at, w):
    """(startup 2026-11-19) A pair {"id", "game_number"}: what in it the game doesn't read (its other keys; an "id" that
    isn't text) -- warnings, as for a variant's own keys. Not a pair: nothing."""
    if not isinstance(w, dict): return []
    out = ['%s: "wrestler" {...}: "%s" isn\'t read (only "id" and "game_number")' % (at, k) for k in w if k not in ("id", "game_number")]
    if "id" in w and not isinstance(w["id"], str): out.append('%s: "wrestler" {...}: "id" should be text (his string ID)' % at)
    return out


def check_families_json(j, known, taken=None, where="families.json", hidden=None):
    """One file's "families" -> (errors, warnings, families kept as (id, slot, [(number, variant)])). taken: number -> where
    he is already (F4), updated; hidden: number -> mask (H2: hidden variants left out, the slot kept)."""
    errs, warns, kept = [], [], []
    taken = {} if taken is None else taken
    hidden = hidden or {}
    if not isinstance(j, dict): return ["isn't a JSON object: not read (F2)"], warns, kept
    for key in j:
        if key not in ("about", "families", "hidden"): warns.append('"%s" isn\'t read' % key)
    if "families" not in j: return errs, warns, kept
    if not isinstance(j["families"], list): return ['"families" must be a list: not read (F2)'], warns, kept
    for fi, fj in enumerate(j["families"]):
        fid = fj.get("id") if isinstance(fj, dict) and isinstance(fj.get("id"), str) else ""
        label = "family %d%s" % (fi + 1, " (%s)" % fid if fid else "")
        if not isinstance(fj, dict) or not isinstance(fj.get("variants"), list):
            errs.append('%s: needs a "variants" list: left out (F3)' % label); continue
        for key in fj:
            if key not in KNOWN_FAMILY_KEYS: warns.append('%s: "%s" isn\'t read' % (label, key))
        if "id" in fj and not isinstance(fj["id"], str): warns.append('%s: "id" should be text' % label)
        if "unlisted" in fj and fj["unlisted"] not in ("off", "on"): warns.append('%s: "unlisted" is "off" or "on" (U1)' % label)
        vs, seen, mine, slot, big = [], [], [], 0, False
        for k, v in enumerate(fj["variants"]):
            at = "%s, variant %d" % (label, k + 1)
            if not isinstance(v, dict) or "wrestler" not in v or "variant" not in v:
                errs.append('%s: needs "wrestler" and "variant": left out (F3)' % at); continue
            for key in v:
                if key not in KNOWN_VARIANT_KEYS: warns.append('%s: "%s" isn\'t read' % (at, key))
            n = known.who(v["wrestler"])
            look = number_of(v["variant"])
            warns += pair_warnings(at, v["wrestler"])
            if not n:
                warns.append("%s: no wrestler %s now: left out while he isn't there (F3)" % (at, json.dumps(v["wrestler"]))); continue
            if not 1 <= look <= 4:
                errs.append('%s: "variant" must be 1-4 (a wrestler\'s four looks): left out (F3)' % at); continue
            if not isinstance(v["variant"], int) or isinstance(v["variant"], bool):
                warns.append('%s: "variant" should be a plain number (1-4), not %s' % (at, json.dumps(v["variant"])))
            if n in taken:
                errs.append("%s: %s is in %s already: left out (F4: a wrestler is in one family)" % (at, known.name(n), taken[n])); continue
            if (n, look) in seen:
                errs.append("%s: %s's variant %d is in it already: left out (F5)" % (at, known.name(n), look)); continue
            seen.append((n, look))
            if isinstance(_sid_of(v["wrestler"]), str) and "~" in _sid_of(v["wrestler"]):
                warns.append('%s: %s is a clash ID (two folders, one ID): it changes if folders move -- give that folder an "id" of its own'
                             % (at, _sid_of(v["wrestler"])))
            if not slot:
                slot = n
                if n in known.removed:
                    warns.append("%s: %s, the family's first, is removed (his folder deleted): the family stays in his box, headed by "
                                 "the next one in play (G2)" % (at, known.name(n)))
            elif n in known.removed and n not in mine:
                warns.append("%s: %s is removed: left out while he is (G1)" % (at, known.name(n)))
            if n == BIG_SHOW: big = True
            if n not in mine: mine.append(n)
            if hidden.get(n, 0) & (1 << (look - 1)): continue            # (H2: hidden -- never stepped to)
            vs.append((n, look))
        if not slot:
            errs.append("%s: no variant left: left out (F3)" % label); continue
        if big: warns.append("%s: big_show is in play only with the Big Show option on; off, that number is Chris Benoit's box (G3)" % label)
        if len(seen) == 1: warns.append("%s: only one variant: the box can't step anywhere" % label)
        if not vs: warns.append("%s: every variant of it is hidden: no box (H2)" % label)
        name = fid or known.name(slot)
        for n in mine: taken[n] = "%s, family %s" % (where, name)
        kept.append((name, slot, vs, fj.get("unlisted") == "off"))
    return errs, warns, kept


def check_families(root, files=None, roster_path=None, known=None):
    rep = Report()
    known = known or known_from_folder(root, roster_path)
    files = files if files is not None else family_files(root)
    loaded = []
    for path in files:
        shown = shown_path(path, root)
        j = load_json(path)
        if j is None or not isinstance(j, dict):
            rep.files.append((shown, ["isn't valid JSON: not read (F2)" if j is None else "isn't a JSON object: not read (F2)"], [])); continue
        if "families" not in j and "hidden" not in j:
            rep.files.append((shown, [], ['has neither "families" nor "hidden": nothing in it is read (F2)'])); continue
        e, w = check_hidden_json(j, known, rep.hidden)
        loaded.append((shown, j, e, w))
    taken, any_list, off_ids = {}, False, []
    for shown, j, e, w in loaded:
        errs, warns, kept = check_families_json(j, known, taken, shown, rep.hidden)
        if isinstance(j.get("families"), list): any_list = True
        rep.files.append((shown, e + errs, w + warns))
        rep.families += [(shown, n, sl, vs) for n, sl, vs, off in kept]
        off_ids += [n for n, sl, vs, off in kept if off]
    if not any_list:
        rep.from_default = True
        _, _, kept = check_families_json(default_json(), known, taken, "the default data", rep.hidden)
        rep.families += [("(the default data)", n, sl, vs) for n, sl, vs, off in kept]
        off_ids += [n for n, sl, vs, off in kept if off]
        rep.notes.append("no families.json with a \"families\" list: the game uses the families it always had (F1)")
    else:
        mentioned = {n for _, _, _, vs in rep.families for n, _ in vs}
        lost = [i for i, vs in DEFAULT if not any(known.who(w) in mentioned for w, _ in vs[4:])]
        if lost:
            rep.notes.append("the default families %s aren't in any file: with a families.json the game has only what the files "
                             "say (F1) -- those wrestlers get boxes of their own" % ", ".join(lost))
    # N2/N3: the nested folders, after the data (the data wins)
    where_of = {}
    for i, (_, _, s, vs) in enumerate(rep.families):
        for n in [s] + [x for x, _ in vs]: where_of.setdefault(n, i)
    for top, kids in known.nests:
        join = []
        for k in kids:
            if k in where_of:
                rep.notes.append("%s is nested in %s's folder, but the data puts him in family %s: there -- the data wins (N2)"
                                 % (known.name(k), known.name(top), rep.families[where_of[k]][1]))
            else: join.append(k)
        if not join: continue
        four = lambda n: [(n, v) for v in (1, 2, 3, 4) if not rep.hidden.get(n, 0) & (1 << (v - 1))]
        if top in where_of:
            i = where_of[top]
        else:
            rep.families.append(("(nested folders)", known.name(top), top, four(top)))
            i = len(rep.families) - 1
            where_of[top] = i
        for k in join:
            rep.families[i][3].extend(four(k)); where_of[k] = i
        rep.notes.append("%s's folder holds %s: in his family, after him (N1)" % (known.name(top), ", ".join(known.name(k) for k in join)))
    rep.notes += known.nest_notes
    # U1: a partial family -- the first wrestler's unlisted variants at its end, another's in a box of his own
    for i in range(len(rep.families)):
        where, name, slot, vs = rep.families[i]
        if where.startswith("(") and where != "(the default data)" or name in off_ids: continue
        for w in list(dict.fromkeys(x for x, _ in vs)):
            have = {v for x, v in vs if x == w}
            rest = [(w, v) for v in (1, 2, 3, 4) if v not in have and not rep.hidden.get(w, 0) & (1 << (v - 1))]
            if not rest: continue
            if w == slot:
                vs.extend(rest)
                rep.notes.append("family %s: %s's variants it doesn't list go at its end: %s (U1)" % (name, known.name(w), ", ".join(str(v) for _, v in rest)))
            else:
                rep.families.append(("(his own box: variants family %s doesn't list)" % name, known.name(w), w, rest))
                rep.notes.append("%s's variants family %s doesn't list keep his own box: %s (U1)" % (known.name(w), name, ", ".join(str(v) for _, v in rest)))
    # H3: a wrestler in no family with some of his variants hidden: a family of his own of the rest
    for n, mask in sorted(rep.hidden.items()):
        if mask != 0xF and n not in where_of:
            rep.families.append(("(his variants not hidden)", known.name(n), n, [(n, v) for v in (1, 2, 3, 4) if not mask & (1 << (v - 1))]))
    for n, mask in sorted(rep.hidden.items()):
        if mask == 0xF and n in where_of and rep.families[where_of[n]][2] == n:
            rest = rep.families[where_of[n]][3]
            rep.notes.append("%s is hidden and heads family %s: %s" % (known.name(n), rep.families[where_of[n]][1],
                             "its box stays where his is, headed by %s (H2)" % known.name(rest[0][0]) if rest else "every variant of it is hidden: no box (H2)"))
    # (2026-11-17) The override files (wrestler.override.json; wrestler_override.py beside this file: rules O1-O7). They can't
    # change a wrestler's identity (O5), so they change nothing above; their own errors and warnings are listed with the files.
    try:
        import wrestler_override
    except ImportError:
        wrestler_override = None
    if wrestler_override and root and os.path.isdir(root):
        for p, e, w in wrestler_override.check_tree(root):
            rep.files.append((shown_path(p, root), e, w))
    return rep


# ---- A1: MERGE SAME WRESTLERS (automerge, 2026-11-23) -----------------------------------------------------------------------
def auto_merge_setting(game_folder):
    """F2's MERGE SAME WRESTLERS as a game folder keeps it (mod_config/nomercy_arcade.json, "storage": "auto_merge"): True /
    False; None when the file isn't there or doesn't say (the game's default then: off)."""
    j = load_json(os.path.join(game_folder, "mod_config", "nomercy_arcade.json"))
    v = j.get("storage", {}).get("auto_merge") if isinstance(j, dict) and isinstance(j.get("storage"), dict) else None
    if v is None: return None
    if isinstance(v, str): return v.strip().lower() in ("1", "on", "true", "yes")
    return bool(v)


def editor_families(editor=None):
    """The roster editor's app/families.py (its automatic rule): from --editor, else the editor this checker sits in (its
    validators folder), else None."""
    for kit in ([editor] if editor else []) + [os.path.join(HERE, "..")]:
        app = os.path.normpath(os.path.join(kit, "app"))
        if os.path.isfile(os.path.join(app, "families.py")):
            if app not in sys.path: sys.path.insert(0, app)
            import families as ed_families
            return ed_families
    return None


def auto_merge(root, editor=None):
    """A1: what MERGE SAME WRESTLERS makes of this wrestlers folder at the game's start-up -- the editor's proposals of the
    kind "same" that it ticks, and the families.json document its Consolidate would write for them (None: nothing to do).
    Nothing is written. -> (proposals, document) or raises LookupError when the editor isn't there."""
    ed = editor_families(editor)
    if ed is None: raise LookupError("the roster editor (its app/families.py) isn't found: give --editor")
    import wrestlers as ed_wrestlers
    fam = ed.Families(ed_wrestlers.Wrestlers(root))
    props = [p for p in fam.propose() if p["kind"] == "same"]
    keys = [p["key"] for p in props if p["on"]]
    if not keys: return props, None
    st = fam.state()
    st.merge = bool(st.other_files)          # (families.json in collection folders too: the game reads them all at once)
    if st.merge: st.merged_others = []
    out = fam.consolidate(keys, dry=True, st=st)
    return props, json.loads(out["text"])


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Check families.json the way Ultimate No Mercy reads it.")
    ap.add_argument("folder", nargs="?", help="the wrestlers folder")
    ap.add_argument("--file", action="append", help="check these files (in this order) instead of the folder's own")
    ap.add_argument("--roster", help="a game's stories\\_roster.json (default: made by the validator's storyc.py)")
    ap.add_argument("--write-default", metavar="PATH", help="write the default families (F1) as a families.json and stop")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--auto-merge", choices=("on", "off", "game"), default="off",
                    help="F2's MERGE SAME WRESTLERS (A1): on, off, or as the game folder above the wrestlers folder says")
    ap.add_argument("--editor", help="the roster editor's folder (its app/families.py has the automatic rule)")
    a = ap.parse_args(argv)
    if a.write_default:
        with open(a.write_default, "w", encoding="utf-8", newline="\n") as f:
            json.dump(default_json(), f, indent=1, ensure_ascii=False); f.write("\n")
        print("wrote %s: %d families" % (a.write_default, len(DEFAULT))); return 0
    if not a.folder or not os.path.isdir(a.folder): print("not a folder: %s" % a.folder); return 2
    known = known_from_folder(a.folder, a.roster)
    on = a.auto_merge == "on" or (a.auto_merge == "game" and auto_merge_setting(os.path.dirname(os.path.abspath(a.folder))))
    files = a.file
    if on:                                   # (A1: the families as the game makes them at start-up, the editor's file read in their place)
        import tempfile
        try: props, doc = auto_merge(a.folder, a.editor)
        except LookupError as e: print(e); return 2
        for p in props if not a.json else []:
            print("auto-merge: %s %s: %s" % (p["change"], p["key"], " + ".join(known.name(n) for n in p["members"])))
        if doc is not None:
            tmp = tempfile.mkdtemp(prefix="families_auto_")
            files = [os.path.join(tmp, "families.json")]
            with open(files[0], "w", encoding="utf-8") as f: json.dump(doc, f)
    try: rep = check_families(a.folder, files, a.roster, known)
    finally:
        if files and files is not a.file:      # (A1's own file: a temporary one, never in the wrestlers folder)
            import shutil
            shutil.rmtree(os.path.dirname(files[0]), ignore_errors=True)
    if a.json:
        print(json.dumps({"from_default": rep.from_default,
                          "files": [{"file": f, "errors": e, "warnings": w} for f, e, w in rep.files],
                          "families": [{"file": f, "id": n, "slot": known.name(sl), "variants": [{"wrestler": known.name(x), "number": "0x%04X" % x, "variant": v}
                                                                                                  for x, v in vs]} for f, n, sl, vs in rep.families],
                          "hidden": [{"wrestler": known.name(x), "variants": [v for v in (1, 2, 3, 4) if m & (1 << (v - 1))]} for x, m in sorted(rep.hidden.items())],
                          "notes": rep.notes}, indent=1, ensure_ascii=False))
    else:
        for f, es, ws in rep.files:
            print("%s: %d errors, %d warnings" % (f, len(es), len(ws)))
            for x in es: print("    ERROR   " + x)
            for x in ws: print("    warning " + x)
        for f, n, sl, vs in rep.families:
            print("family %-20s %2d variants: %s" % (n, len(vs), ", ".join("%s %d" % (known.name(x), v) for x, v in vs)))
        for x, m in sorted(rep.hidden.items()):
            print("hidden %s%s" % (known.name(x), "" if m == 0xF else " variant " + ", ".join(str(v) for v in (1, 2, 3, 4) if m & (1 << (v - 1)))))
        for n in rep.notes: print("note: " + n)
        print("%d families, %d variants, %d errors" % (len(rep.families), sum(len(vs) for _, _, _, vs in rep.families), len(rep.errors)))
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
