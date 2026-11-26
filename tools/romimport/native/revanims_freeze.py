#!/usr/bin/env python3
"""revanims_freeze.py (phase3 p3ports, 2026-11-19): the FREEZE RULE's table for the built-in Revenge ports' movesets and
their Revenge animations (gen/mkroster.py port_rev_moveset, gen/revanims.py), and its developer check.

The 18 built-in Revenge ports (gen/mkroster.py LATE "rev": Bret Hart .. Eric Bischoff; their second IDs, gen/revbanks.py, use
their base's moveset file) get their moves by animation features (data\\revport\\revmoves.py predict: the pool move whose
first animation is his byte for byte, "exact", else the nearest by feature distance, "near", kept when the rounded distance
is 0.3 or less). Those distances are numpy floats over the feature pickles: costly to port float-exact, so the CHOICES are
frozen here, as indexes, move numbers and the rounded distance the Python writes into rev_anims.json's "stand_in" text --
no game bytes: tools\\romimport\\frozen\\rev_port_picks.json
  "picks":    {moveset file: {slot: [move, "exact" | "near", rounded distance]}}  port_rev_moveset's predict for each slot of
              revmoves.slot_positions(PAIRS) (frozen\\rev_moveset_picks.json "positions"), where it is taken
  "carriers": {moveset file: {slot: move | null}}   gen/revanims.py main()'s predict for a stand-in move that goes on where
              Revenge's code ends (the nearest move of the slot's pool that ends), as the Python asks it
Everything else (the reference's moves, the overrides, the hashes, the blocks, the records, the files' bytes) the native code
(ri_ports.cpp) computes from the ROMs.

    python revanims_freeze.py write <python worktree> <frozen json> <scratch>   make the table (runs gen/revanims.py main() with
                                                                                its outputs sent to <scratch>)
    python revanims_freeze.py check <python worktree> <frozen json> <scratch>   the developer check: the table is the Python's own
        choice (every predict again), and gen/revanims.py run with predict answered from the table writes the same
        rev_anims.json and movex_data.h as with its own (both into <scratch>)
Run with the reference toolchain's Python (numpy); it only reads the worktree (no bytecode written)."""
import inspect, json, os, sys
sys.dont_write_bytecode = True

def setup(wt):
    for p in (wt, os.path.join(wt, "mod", "gen"), os.path.join(wt, "data", "revport")): sys.path.insert(0, p)
    import mkroster as K, revmoves as RM, revanims as RA
    return K, RM, RA

def port_files(K):
    return sorted({e["rev"] for e in K.LATE if "rev" in e})

def picks_of(K, RM, pos, f):
    """port_rev_moveset's predict for every slot of `pos` (its pool: every pool move the slot fits), where it is taken"""
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
        if pr and (pr[1] == "exact" or pr[2] <= K.REV_NEAR): out[str(i)] = [pr[0], pr[1], pr[2]]
    return out

def run_revanims(RA, scratch, tag):
    """gen/revanims.py main() with its two outputs in <scratch>\\<tag>_*"""
    RA.OUT_JSON = os.path.join(scratch, tag + "_rev_anims.json")
    RA.OUT_H = os.path.join(scratch, tag + "_movex_data.h")
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()): RA.main()
    return open(RA.OUT_JSON, "rb").read(), open(RA.OUT_H, "rb").read()

def main():
    mode, wt, frozen, scratch = sys.argv[1:5]
    os.makedirs(scratch, exist_ok=True)
    K, RM, RA = setup(wt)
    real_pred, real_pos = RM.predict, RM.slot_positions
    pos = real_pos(RM.PAIRS)
    files = port_files(K)
    # the carriers: revanims main()'s own predict calls (the ones made from main, not from port_rev_moveset)
    carriers = {}
    def spy(rl, p, pool):
        r = real_pred(rl, p, pool)
        if inspect.stack()[1].function == "main":
            f = next(f for f in files if RM.revlist(f) == rl)
            (s,) = p.keys()
            carriers.setdefault("0x%X" % f, {})[str(s)] = r[s][0] if s in r else None
        return r
    RM.predict = spy
    try: own = run_revanims(RA, scratch, "py")
    finally: RM.predict = real_pred
    table = {"picks": {"0x%X" % f: picks_of(K, RM, pos, f) for f in files},
             "carriers": {k: carriers[k] for k in sorted(carriers)}}
    if mode == "write":
        j = {"about": ("The built-in Revenge ports' heuristic choices, frozen (FREEZE RULE): made by tools\\romimport\\native\\"
                       "revanims_freeze.py write from gen\\mkroster.py port_rev_moveset's predict (data\\revport\\revmoves.py: "
                       "exact, or near <= REV_NEAR, with the rounded distance) and gen\\revanims.py main()'s predict for the moves "
                       "that end; checked by revanims_freeze.py check. Indexes, move numbers and rounded distances only."),
             **table}
        with open(frozen, "w", encoding="utf-8", newline="\n") as fh: json.dump(j, fh, indent=1)
        print("%s: %d moveset files, %d carriers" % (frozen, len(files), sum(len(v) for v in carriers.values())))
        return 0
    j = json.load(open(frozen, encoding="utf-8"))
    bad = 0
    for k in ("picks", "carriers"):
        if j[k] != json.loads(json.dumps(table[k])): print("%s: DIFFERENT from the Python's own choices" % k); bad += 1
        else: print("%s: SAME (the Python's own choices)" % k)
    # revanims with the table instead of predict / slot_positions
    fpos = {int(s): tuple(v) for s, v in json.load(open(os.path.join(os.path.dirname(frozen), "rev_moveset_picks.json")))["positions"].items()}
    def table_pred(rl, p, pool):
        f = "0x%X" % next(f for f in files if RM.revlist(f) == rl)
        (s,) = p.keys()
        if inspect.stack()[1].function == "main":
            m = j["carriers"][f][str(s)]
            return {} if m is None else {s: (m, "near", 0.0)}
        v = j["picks"][f].get(str(s))
        return {s: (v[0], v[1], v[2])} if v else {}
    RM.predict, RM.slot_positions = table_pred, (lambda pairs: dict(fpos))
    try: frz = run_revanims(RA, scratch, "frozen")
    finally: RM.predict, RM.slot_positions = real_pred, real_pos
    for name, a, b in (("rev_anims.json", own[0], frz[0]), ("movex_data.h", own[1], frz[1])):
        print("%s with the table: %s" % (name, "SAME" if a == b else "DIFFERENT")); bad += a != b
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
