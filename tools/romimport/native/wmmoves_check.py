#!/usr/bin/env python3
"""wmmoves_check.py (phase3 p3wmmoves, 2026-11-19): ri_cli wmmoves's output against the Python reference, byte for byte.

    python wmmoves_check.py <ri_cli wmmoves out folder> <private folder> [--bb <ri_cli bbmoves out folder>]

  - the chain step "wmlib" (tools\\handlers\\wmlib.py lib): movelib_wm2000.bin, move_lib_wm2000.json, rows_wm2000.json against the
    files chain.py compares them with (build\\mod\\build\\movelib_wm2000.bin, scratch\\romimport\\wmlib_today\\*.json, each through
    chain.py's ref_of: the latest NEW_REFS copy wins) and against the last ROM-only run's (romonly\\scratch\\chain\\wmlib);
  - each of the 56 men (wm2k_import.py one() + wm2k_refresh.py): <out>\\<string id>\\moves.json against the reference folder's
    (reference\\wrestlers_tested\\WrestleMania 2000\\<string id>\\, the romrender overlay's file winning where it has one), and
    <out>\\<string id>\\moveset.json against the reference wrestler.json's "moveset", "source"."moveset_fallbacks",
    "source"."stance" and "source"."win_pose" (a note neither has is equal).
Prints SAME / DIFFERENT per item and the counts; exit 1 when anything differs."""
import json, os, sys

def main():
    out, priv = sys.argv[1], sys.argv[2]
    new_refs = [os.path.join(priv, "reference", r) for r in ("romrender", "revmusic", "reduxthemes")]
    def ref_of(p):
        for r in reversed(new_refs):
            q = os.path.join(r, os.path.relpath(p, priv))
            if os.path.exists(q): return q
        return p
    bad = 0
    def same_file(a, b):
        return os.path.exists(a) and os.path.exists(b) and open(a, "rb").read() == open(b, "rb").read()
    lib_refs = {"movelib_wm2000.bin": os.path.join(priv, "build", "mod", "build"),
                "move_lib_wm2000.json": os.path.join(priv, "scratch", "romimport", "wmlib_today"),
                "rows_wm2000.json": os.path.join(priv, "scratch", "romimport", "wmlib_today")}
    for f, d in lib_refs.items():
        r1 = ref_of(os.path.join(d, f)); r2 = os.path.join(priv, "romonly", "scratch", "chain", "wmlib", f)
        s1, s2 = same_file(os.path.join(out, f), r1), same_file(os.path.join(out, f), r2)
        print("%-24s %s (chain reference) / %s (last ROM-only run)" % (f, "SAME" if s1 else "DIFFERENT", "SAME" if s2 else "DIFFERENT"))
        bad += not s1
    coll = os.path.join(priv, "reference", "wrestlers_tested", "WrestleMania 2000")
    over = os.path.join(priv, "reference", "romrender", "reference", "wrestlers_tested", "WrestleMania 2000")
    def ref_file(sid, f):
        p = os.path.join(over, sid, f)
        return p if os.path.exists(p) else os.path.join(coll, sid, f)
    n = {"moves.json": [0, 0], "moveset": [0, 0], "moveset_fallbacks": [0, 0], "stance": [0, 0], "win_pose": [0, 0]}
    men = sorted(d for d in os.listdir(out) if os.path.isdir(os.path.join(out, d)))
    for sid in men:
        diffs = []
        ok = same_file(os.path.join(out, sid, "moves.json"), ref_file(sid, "moves.json"))
        n["moves.json"][0 if ok else 1] += 1
        if not ok: diffs.append("moves.json")
        mine = json.load(open(os.path.join(out, sid, "moveset.json")))
        w = json.load(open(ref_file(sid, "wrestler.json"), encoding="utf-8"))
        theirs = {"moveset": w.get("moveset"), "moveset_fallbacks": w.get("source", {}).get("moveset_fallbacks"),
                  "stance": w.get("source", {}).get("stance"), "win_pose": w.get("source", {}).get("win_pose")}
        for k, v in theirs.items():
            ok = mine.get(k) == v
            n[k][0 if ok else 1] += 1
            if not ok: diffs.append(k)
        print("%-28s %s" % (sid, "SAME" if not diffs else "DIFFERENT: " + ", ".join(diffs)))
        bad += bool(diffs)
    print("men: %d; %s" % (len(men), ", ".join("%s %d SAME / %d DIFFERENT" % (k, s, d) for k, (s, d) in n.items())))
    # (--bb <ri_cli bbmoves out>: Badd Blood's Vader and Goldust -- imports2.py -- against the Imported folders, the latest
    #  NEW_REFS overlay's file winning)
    if len(sys.argv) > 4 and sys.argv[3] == "--bb":
        for sid in ("vader", "goldust"):
            def imp(f):
                p = os.path.join(priv, "reference", "wrestlers_tested", "Imported", sid, f)
                return ref_of(p)
            o = os.path.join(sys.argv[4], sid)
            ok_m = same_file(os.path.join(o, "moves.json"), imp("moves.json"))
            m = json.load(open(os.path.join(o, "moveset.json"))); w = json.load(open(imp("wrestler.json"), encoding="utf-8"))
            ok_p = m["moveset"] == w["moveset"]; ok_f = m["moveset_fallbacks"] == w["source"].get("moveset_fallbacks")
            print("%-28s moves.json %s, moveset %s, moveset_fallbacks %s" % ((sid,) + tuple("SAME" if x else "DIFFERENT" for x in (ok_m, ok_p, ok_f))))
            bad += not (ok_m and ok_p and ok_f)
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
