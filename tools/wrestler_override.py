#!/usr/bin/env python3
r"""wrestler_override.py -- the per-wrestler override file, as the game reads it (2026-11-17, the user's decision).

THE FILE. `wrestler.override.json`, beside a wrestler's `wrestler.json`. The user or the editor writes it; the importers
never do, and a re-import never touches it (it makes wrestler.json again, from the player's ROM, and leaves every other
file of the folder alone). The game lays it over wrestler.json each time it reads him (exe arcade_wrestlers.cpp
apply_override), so a change made there survives every re-import.

HOW IT IS LAID OVER: a JSON Merge Patch (RFC 7386), applied to wrestler.json as read:
  O1  An object in the override goes into the object at the same place, key by key (recursively): only the keys it names
      change. {"body": {"nm_size": 70}} changes "body"'s nm_size and keeps the rest of "body".
  O2  Any other value -- a string, a number, true/false, a list -- REPLACES what wrestler.json has there (a list is
      replaced whole: to change one of his four "looks", give all four).
  O3  null DELETES the key: {"nick": null} takes his nickname out of wrestler.json (the game then does what it does
      without one: the man he is based on keeps his). There is no other way to delete.
  O4  Precedence: the override wins over wrestler.json, always; wrestler.json wins over the man it is "based_on" (as before).
  O5  His IDENTITY is the folder's: "id", "string_id", "builtin" and "replaces" in an override are left out (a warning
      here, a line in the game's log). His ID, his number, what he replaces stay what the importer wrote.
  O6  An override that isn't a JSON object is not used at all (an error here, a line in the game's log): he loads as his
      wrestler.json says. No override file: nothing changes.
  O7  Only wrestler.json is overridden (not moves.json, model\, photo.bin ...).

    python tools/wrestler_override.py <wrestlers folder>            check every override file under it (exit 1 on an error)
    python tools/wrestler_override.py --show <wrestler folder>      print his wrestler.json with the override laid over it
Python 3, standard library only: the editor can import it (merge_patch, effective, check_override, check_tree).
"""
import json
import os
import sys

OVERRIDE_FILE = "wrestler.override.json"
IDENTITY = ("id", "string_id", "builtin", "replaces")
# What the game reads from a wrestler.json (exe arcade_wrestlers.cpp's header says what each is); the importers' records
# of where things came from ("about", "source", "model", ...) are read by nobody and may be overridden freely.
GAME_KEYS = {"based_on", "name", "short_name", "nick", "height", "weight", "size", "music", "video", "packed_variants",
             "costumes", "looks", "moveset", "per_variant", "per_costume"}
NOTE_KEYS = {"about", "source", "model"}
TEXT_MAX = {"name": 20, "short_name": 10, "nick": 16}


def merge_patch(target, patch):
    """RFC 7386: patch laid over target (a new value; neither is changed)."""
    if not isinstance(patch, dict):
        return patch
    out = dict(target) if isinstance(target, dict) else {}
    for k, v in patch.items():
        if v is None:
            out.pop(k, None)
        else:
            out[k] = merge_patch(out.get(k), v)
    return out


def load(path):
    try:
        with open(path, encoding="utf-8-sig") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def usable(override):
    """the override as the game uses it: its identity keys left out (O5); None when it isn't an object (O6)."""
    if not isinstance(override, dict):
        return None
    return {k: v for k, v in override.items() if k not in IDENTITY}


def effective(folder):
    """his wrestler.json as the game reads it: with the folder's override laid over it (or as it is, without one)."""
    j = load(os.path.join(folder, "wrestler.json"))
    o = usable(load(os.path.join(folder, OVERRIDE_FILE))) if os.path.exists(os.path.join(folder, OVERRIDE_FILE)) else None
    return merge_patch(j, o) if isinstance(j, dict) and o else j


def check_override(folder):
    """(errors, warnings) for the folder's override file (none: ([], []))."""
    p = os.path.join(folder, OVERRIDE_FILE)
    if not os.path.exists(p):
        return [], []
    errs, warns = [], []
    o = load(p)
    if o is None:
        return ["isn't valid JSON: not used (O6)"], []
    if not isinstance(o, dict):
        return ["isn't a JSON object: not used (O6)"], []
    if not os.path.exists(os.path.join(folder, "wrestler.json")):
        warns.append("no wrestler.json beside it: the folder is no wrestler, so nothing reads it")
    for k in IDENTITY:
        if k in o:
            warns.append('"%s" is left out: a wrestler\'s identity is his folder\'s (O5)' % k)
    for k in o:
        if k not in GAME_KEYS and k not in NOTE_KEYS and k not in IDENTITY:
            warns.append('"%s" is not a key the game reads (a typo?)' % k)
    merged = effective(folder)
    if isinstance(merged, dict):
        for k, n in TEXT_MAX.items():
            v = merged.get(k)
            if k in o and v is not None and not isinstance(v, str):
                errs.append('"%s" must be text (or null)' % k)
            elif k in o and isinstance(v, str) and len(v) > n:
                warns.append('"%s" is %d characters: the game shows %d' % (k, len(v), n))
        for k, lo, hi in (("height", 0, 255), ("weight", 0, 65535), ("size", 0, 255), ("music", 0, 255), ("video", 0, 65535)):
            if k in o and o[k] is not None and not (isinstance(o[k], int) and not isinstance(o[k], bool) and lo <= o[k] <= hi):
                errs.append('"%s" must be a whole number %d-%d (or null)' % (k, lo, hi))
        if "looks" in o and o["looks"] is not None and not (isinstance(o["looks"], list) and len(o["looks"]) == 4):
            errs.append('"looks" replaces the whole list: give all four variants (O2)')
    return errs, warns


def check_tree(root):
    """[(the override's path, errors, warnings)] for every override file under a wrestlers folder (folders starting _ or .
    skipped, as the game does)."""
    out = []
    for d, dirs, files in os.walk(root):
        dirs[:] = sorted(x for x in dirs if not x.startswith(("_", ".")))
        if OVERRIDE_FILE in files:
            e, w = check_override(d)
            out.append((os.path.join(d, OVERRIDE_FILE), e, w))
    return out


def main(argv):
    if argv[:1] == ["--show"] and len(argv) == 2:
        print(json.dumps(effective(argv[1]), indent=1, ensure_ascii=False))
        return 0
    if len(argv) != 1:
        print(__doc__)
        return 2
    rows = check_tree(argv[0])
    bad = 0
    for p, e, w in rows:
        rel = os.path.relpath(p, argv[0])
        for x in e:
            print("ERROR   %s: %s" % (rel, x)); bad += 1
        for x in w:
            print("warning %s: %s" % (rel, x))
    print("%d override file(s), %d error(s)" % (len(rows), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
