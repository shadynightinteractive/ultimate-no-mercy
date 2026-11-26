#!/usr/bin/env python3
"""revmoves_check.py (phase3 p3wmmoves, 2026-11-19): ri_cli revmoves's output against the Python reference, byte for byte.

    python revmoves_check.py <ri_cli revmoves out folder> <private folder>

For each never-ported Revenge man (aki_import.py revenge --all --rest-only ... then --refresh): <out>\\<sid>\\moves.json against
the tested folder's (reference\\wrestlers_tested\\Revenge\\<sid>\\, the latest NEW_REFS overlay's copy winning: chain.py ref_of),
and <out>\\<sid>\\moveset.json against that wrestler.json's "moveset", "source"."win_pose", "source"."abilities" (a note
neither has is equal). The tested folders carry an old escaping bug in "source"."abilities" (imports.py only_abilities_note:
45 known); so that note is compared with the Python's current output too -- the ROM-only chain's last run
(romonly\\scratch\\chain\\imports\\Revenge) -- and a difference only from the tested folder is counted apart as "known".
Exit 1 when anything else differs."""
import json, os, sys

def main():
    out, priv = sys.argv[1], sys.argv[2]
    new_refs = [os.path.join(priv, "reference", r) for r in ("romrender", "revmusic", "reduxthemes")]
    def ref_of(p):
        for r in reversed(new_refs):
            q = os.path.join(r, os.path.relpath(p, priv))
            if os.path.exists(q): return q
        return p
    tested = os.path.join(priv, "reference", "wrestlers_tested", "Revenge")
    current = os.path.join(priv, "romonly", "scratch", "chain", "imports", "Revenge")
    n = {k: [0, 0] for k in ("moves.json", "moveset", "win_pose", "abilities")}
    known = bad = 0
    men = sorted(d for d in os.listdir(out) if os.path.isdir(os.path.join(out, d)))
    for sid in men:
        diffs = []
        a, b = os.path.join(out, sid, "moves.json"), ref_of(os.path.join(tested, sid, "moves.json"))
        ok = os.path.exists(a) and os.path.exists(b) and open(a, "rb").read() == open(b, "rb").read()
        n["moves.json"][0 if ok else 1] += 1
        if not ok: diffs.append("moves.json")
        mine = json.load(open(os.path.join(out, sid, "moveset.json"), encoding="utf-8"))
        w = json.load(open(ref_of(os.path.join(tested, sid, "wrestler.json")), encoding="utf-8"))
        cur_p = os.path.join(current, sid, "wrestler.json")
        cur = json.load(open(cur_p, encoding="utf-8")) if os.path.exists(cur_p) else {}
        for k in ("moveset", "win_pose", "abilities"):
            v = w.get(k) if k == "moveset" else w.get("source", {}).get(k)
            ok = mine.get(k) == v
            if not ok and k == "abilities" and mine.get(k) == cur.get("source", {}).get(k):
                known += 1; n[k][0] += 1; continue          # (the tested folder's old escaping; equal to the Python's current text)
            n[k][0 if ok else 1] += 1
            if not ok: diffs.append(k)
        print("%-22s %s" % (sid, "SAME" if not diffs else "DIFFERENT: " + ", ".join(diffs)))
        bad += bool(diffs)
    print("men: %d; %s; abilities equal to the Python's current text but not the tested folder's (the known escaping): %d" % (
        len(men), ", ".join("%s %d SAME / %d DIFFERENT" % (k, s, d) for k, (s, d) in n.items()), known))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
