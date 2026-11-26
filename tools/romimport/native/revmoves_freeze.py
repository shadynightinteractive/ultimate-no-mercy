#!/usr/bin/env python3
"""revmoves_freeze.py (phase3 p3wmmoves, 2026-11-19): the FREEZE RULE's table for the Revenge movesets, and its developer check.

gen/mkroster.py port_rev_moveset finds a Revenge man's move for each No Mercy slot by animation features (data\\revport\\
revmoves.py): slot_positions -- where each slot sits in a Revenge list, voted by five men both games have, by feature distance
-- and predict -- his move for the slot: the pool move whose first animation is his there byte for byte ("exact"), else the
nearest by feature distance ("near", kept when the rounded distance is 0.3 or less). Those distances are floats over numpy
feature arrays (revport\\anim.py, nm_feats.pkl / rev_feats.pkl): costly to port float-exact. So the CHOICES are frozen, as
indexes and move numbers only (no game bytes): tools\\romimport\\frozen\\rev_moveset_picks.json
  "positions": {slot: [list place, exact votes, votes]}            revmoves.slot_positions(revmoves.PAIRS)
  "picks": {moveset file: {slot: No Mercy move}}                    port_rev_moveset's predict result for each slot in
                                                                    "positions", where it is taken (exact, or near <= REV_NEAR)
Everything else of the moveset (the reference's moves and flags, the abilities, the packing, the library IDs, fixes2's own
slots / slams / win pose / band abilities, the records) the native code (ri_revmoves.cpp) computes from the ROMs.

    python revmoves_freeze.py write <python worktree> <frozen json> <Revenge folders>   make the table (the files: the folders'
                                                                                     wrestler.json "source"."moveset_file")
    python revmoves_freeze.py check <python worktree> <frozen json> <Revenge folders>   the developer check: the table is the
        Python's own choice (slot_positions and every predict), and port_rev_moveset with predict / slot_positions answered
        from the table gives every man's slots and abilities exactly as the Python's own (for his reference: 4 Kane, 1 The
        Rock, 51 Stephanie McMahon, as his folder's based_on says)
Run with the reference toolchain's Python (numpy); it only reads the worktree."""
import json, os, sys
sys.dont_write_bytecode = True

def setup(wt):
    sys.path.insert(0, wt); sys.path.insert(0, os.path.join(wt, "mod", "gen")); sys.path.insert(0, os.path.join(wt, "data", "revport"))
    import mkroster as K, revmoves as RM
    return K, RM

def men(folders):
    out = []
    for sid in sorted(os.listdir(folders)):
        p = os.path.join(folders, sid, "wrestler.json")
        if not os.path.exists(p): continue
        w = json.load(open(p, encoding="utf-8"))
        b = w.get("based_on")
        ref = 4 if b in ("0x0004", "kane") else 51 if b in ("0x0702", "stephanie_mcmahon") else 1
        out.append((sid, int(w["source"]["moveset_file"], 16), ref))
    return out

def choices(K, RM, pos, f):
    """port_rev_moveset's predict for every slot of `pos` (its pool: every pool move the slot fits), taken or not"""
    pool = RM.slot_pool(K.CAN)
    allm = sorted(set(m for s_ in pool.values() for m in s_))
    def fits(i, v):
        if i not in K.CAN: return True
        if v in K.CAN[i]: return True
        kind = sorted(x for x in K.CAN[i] if x not in (0, 0xFFFF))
        return bool(kind) and kind[0] <= v <= kind[-1]
    rl = RM.revlist(f)
    out = {}
    for i in sorted(pos):
        pr = RM.predict(rl, {i: pos[i]}, {i: [m for m in allm if fits(i, m)]}).get(i)
        if pr and (pr[1] == "exact" or pr[2] <= K.REV_NEAR): out[str(i)] = pr[0]
    return out

def main():
    mode, wt, frozen, folders = sys.argv[1:5]
    K, RM = setup(wt)
    pos = RM.slot_positions(RM.PAIRS)
    M = men(folders)
    files = sorted({f for _, f, _ in M})
    if mode == "write":
        j = {"about": ("The Revenge movesets' heuristic choices, frozen (FREEZE RULE): made by tools\\romimport\\native\\revmoves_freeze.py "
                       "write from data\\revport\\revmoves.py slot_positions(PAIRS) and gen\\mkroster.py port_rev_moveset's predict "
                       "(exact, or near <= REV_NEAR); checked by revmoves_freeze.py check. Indexes and move numbers only."),
             "positions": {str(s): list(pos[s]) for s in sorted(pos)},
             "picks": {"0x%X" % f: choices(K, RM, pos, f) for f in files}}
        with open(frozen, "w", encoding="utf-8", newline="\n") as fh: json.dump(j, fh, indent=1)
        print("%s: %d positions, %d moveset files" % (frozen, len(pos), len(files)))
        return 0
    j = json.load(open(frozen, encoding="utf-8"))
    bad = 0
    fpos = {int(s): tuple(v) for s, v in j["positions"].items()}
    if fpos != {s: tuple(v) for s, v in pos.items()}: print("positions: DIFFERENT"); bad += 1
    else: print("positions: SAME (%d slots)" % len(pos))
    for f in files:
        c = choices(K, RM, pos, f)
        if c != j["picks"].get("0x%X" % f): print("picks 0x%X: DIFFERENT" % f); bad += 1
    print("picks: %d files %s" % (len(files), "SAME" if not bad else "checked"))
    # port_rev_moveset answered from the table == the Python's own, for each man
    real_pred, real_pos = RM.predict, RM.slot_positions
    for sid, f, ref in M:
        a = K.port_rev_moveset(sid, f, ref, (), {sid: {}}, [])
        picks = j["picks"]["0x%X" % f]
        RM.slot_positions = lambda pairs: {int(s): tuple(v) for s, v in j["positions"].items()}
        RM.predict = lambda rl, p, pool: {i: (picks[str(i)], "exact", 0.0) for i in p if str(i) in picks}
        try: b = K.port_rev_moveset(sid, f, ref, (), {sid: {}}, [])
        finally: RM.predict, RM.slot_positions = real_pred, real_pos
        if list(map(tuple, a[0])) != list(map(tuple, b[0])) or list(a[1]) != list(b[1]): print("%s: DIFFERENT with the table" % sid); bad += 1
    print("port_rev_moveset with the table: %d men %s" % (len(M), "SAME" if not bad else "-- %d DIFFERENT" % bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
