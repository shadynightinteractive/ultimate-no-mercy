"""revfolders_check.py (phase3 p3revfolders, 2026-11-19): ri_cli revfolders' Revenge folders against the Python's.

    python revfolders_check.py <native out folder> [--reference <private\\reference>]

The reference: reference\\wrestlers_tested\\Revenge\\<sid>\\, with the overlays romrender then revmusic laid over it per file
(chain.py NEW_REFS; the third, reduxthemes, is a later step -- ri_redux -- so this step's theme files and "source"."theme" are
revmusic's). Default reference folder: the private folder two levels above this worktree.

Per file kind: SAME / DIFFERENT / missing counts (model\\files\\* as one kind). wrestler.json twice: byte for byte, and field by
field with the moveset's fields left out ("moveset", "source"."win_pose", "source"."abilities": ported separately), key order
included. moves.json only when the native run was given the moveset (ri_cli revfolders --moveset-from)."""
import collections, filecmp, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
MOVESET = {("moveset",), ("source", "win_pose"), ("source", "abilities")}

def main():
    out = sys.argv[1]
    ref = sys.argv[sys.argv.index("--reference") + 1] if "--reference" in sys.argv else os.path.join(os.path.dirname(os.path.dirname(SRC)), "reference")
    base = os.path.join(ref, "wrestlers_tested", "Revenge")
    ovr = [os.path.join(ref, r, "reference", "wrestlers_tested", "Revenge") for r in ("romrender", "revmusic")]
    def ref_of(rel):
        for r in reversed(ovr):
            q = os.path.join(r, rel)
            if os.path.exists(q): return q
        return os.path.join(base, rel)
    def kind(rel):
        k = rel.replace("\\", "/").split("/", 1)[1]
        return "model/files/*" if k.startswith("model/files/") else k
    same, diff, missing = collections.Counter(), collections.defaultdict(list), collections.defaultdict(list)
    ours = sorted(d for d in os.listdir(out) if os.path.isdir(os.path.join(out, d)))
    theirs = sorted(os.listdir(base))
    for sid in ours:
        for r_, _, fs in os.walk(os.path.join(out, sid)):
            for f in fs:
                rel = os.path.relpath(os.path.join(r_, f), out)
                q = ref_of(rel)
                if not os.path.exists(q): missing["(only ours) " + kind(rel)].append(sid); continue
                if filecmp.cmp(os.path.join(out, rel), q, shallow=False): same[kind(rel)] += 1
                else: diff[kind(rel)].append(sid)
        for r_, _, fs in os.walk(os.path.join(base, sid)):
            for f in fs:
                rel = os.path.relpath(os.path.join(r_, f), base)
                if not os.path.exists(os.path.join(out, rel)): missing["(not made) " + kind(rel)].append(sid)
    print("folders: %d made, %d in the reference%s" % (len(ours), len(theirs), "" if ours == theirs else
          " -- only ours %s, only theirs %s" % (sorted(set(ours) - set(theirs)), sorted(set(theirs) - set(ours)))))
    for k in sorted(set(same) | set(diff) | set(missing)):
        print("%-28s SAME %4d  DIFFERENT %3d %s%s" % (k, same[k], len(diff[k]), sorted(set(diff[k]))[:6],
              ("  missing %d %s" % (len(missing[k]), sorted(set(missing[k]))[:4])) if missing[k] else ""))
    # wrestler.json without the moveset's fields, key order included
    def strip(d, path=()):
        if isinstance(d, dict): return [(k, strip(v, path + (k,))) for k, v in d.items() if path + (k,) not in MOVESET]
        if isinstance(d, list): return [strip(x, path) for x in d]
        return d
    n = bad = 0
    for sid in ours:
        p = os.path.join(out, sid, "wrestler.json")
        if not os.path.exists(p) or not os.path.exists(os.path.join(base, sid)): continue
        n += 1
        a = strip(json.load(open(p, encoding="utf-8")))
        b = strip(json.load(open(ref_of(os.path.join(sid, "wrestler.json")), encoding="utf-8")))
        if a != b:
            bad += 1
            da, db = dict(a), dict(b)
            for k in dict.fromkeys(list(da) + list(db)):
                if da.get(k) != db.get(k): print("  %s %s\n     ours %s\n     ref  %s" % (sid, k, da.get(k), db.get(k)))
            if [x for x, _ in a] != [x for x, _ in b]: print("  %s key order %s / %s" % (sid, [x for x, _ in a], [x for x, _ in b]))
    print("wrestler.json without the moveset's fields: %d SAME, %d DIFFERENT (of %d)" % (n - bad, bad, n))
    allsame = not any(diff.values()) and not any(missing.values()) and not bad and ours == theirs
    # (run without the moveset: no moves.json, and wrestler.json differs in the moveset's fields only -- checked field by field)
    no_moveset = (not any(v for k, v in diff.items() if k != "wrestler.json") and not bad and ours == theirs and
                  not any(v for k, v in missing.items() if k != "(not made) moves.json"))
    print("ALL SAME" if allsame else "ALL SAME apart from the moveset (not given: moves.json and the moveset's fields)" if no_moveset
          else "NOT ALL SAME (see above)")
    return 0 if allsame or no_moveset else 1

if __name__ == "__main__":
    sys.exit(main())
