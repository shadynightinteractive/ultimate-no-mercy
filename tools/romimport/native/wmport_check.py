"""wmport_check.py (phase3 p3wmport, 2026-11-19): the native wmport step (exe src\\romimport ri_wmport + ri_wmarenas +
ri_wmvenues + ri_collide) against the Python chain's outputs, byte for byte (pickles by value).

chain.py's steps "wmport1", "arenas_collide", "wmport" (mod\\gen\\wmport.py twice, arenas_collide.py between) and the
with_rma run imports.py makes (WMPORT_REV_ANIMS=1). Run twice natively:
  1. ri_cli wmport ... --collide-py <arenas_collide.py>: the collision step natively (its results' key with the .py's bytes,
     as the Python's), the pack, the 11 headers, the models / videos;
  2. ri_cli wmport ... --rma --collide: the pack with the Revenge move animations (wmport.bin.with_rma, wmport_files.h.with_rma).
Compared with:
  <private>\\romonly\\mod_build\\wmport.bin (= reference\\romrender\\build\\mod\\build\\wmport.bin), the headers by chain.py
  ref_of (reference\\romrender\\generated\\mod\\src, else generated\\mod\\src), romonly\\data\\gen_data\\wmport_models.pkl,
  wmport_videos.pkl (ref_of), romonly\\data\\stagecollide\\result\\<n>.json, romonly\\mod_build\\wmport.bin.with_rma and the
  chain's wmport_files.h.with_rma (--rma-h: the chain worktree's mod\\src copy; skipped if not given).
The input rev_anims.json (the revanims step's output) is the ROM-only tree's (romonly\\data\\gen_data).

    python wmport_check.py --ri <ri_cli.exe> --private <private> [--rma-h <wmport_files.h.with_rma>]"""
import argparse, os, pickle, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
HEADERS = ["wmport_files.h", "rev_move_anims.h", "arena_files.h", "arena_entrance_files.h", "arena_raw_files.h",
           "arena_ringstage_files.h", "arena_ring_files.h", "arena_screen_files.h", "arena_stage_files.h",
           "arena_table_files.h", "arena_walls_files.h"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ri", required=True); ap.add_argument("--private", required=True); ap.add_argument("--rma-h")
    a = ap.parse_args()
    P = a.private
    NEW_REFS = [os.path.join(P, "reference", r) for r in ("romrender", "revmusic", "reduxthemes")]
    def ref_of(p):
        for r in reversed(NEW_REFS):
            q = os.path.join(r, os.path.relpath(p, P))
            if os.path.exists(q): return q
        return p
    out = os.path.join(P, "scratch", "wmport_check")
    roms, data = os.path.join(P, "roms"), os.path.join(SRC, "tools", "romimport")
    ra = os.path.join(P, "romonly", "data", "gen_data", "rev_anims.json")
    for sub, extra in (("pack", ["--collide-py", os.path.join(SRC, "mod", "gen", "arenas_collide.py")]), ("rma", ["--rma", "--collide"])):
        r = subprocess.run([a.ri, "wmport", roms, data, ra, os.path.join(out, sub)] + extra, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode: print(r.stdout[-2000:]); sys.exit("ri_cli wmport failed")
        print(r.stdout.strip().splitlines()[-1])
    bad = 0
    def same(name, mine, ref):
        nonlocal bad
        ok = os.path.exists(ref) and open(mine, "rb").read() == open(ref, "rb").read()
        bad += not ok
        print("%-34s %s" % (name, "SAME" if ok else "DIFFERENT"))
    same("wmport.bin", os.path.join(out, "pack", "wmport.bin"), ref_of(os.path.join(P, "romonly", "mod_build", "wmport.bin")))
    for h in HEADERS:
        same(h, os.path.join(out, "pack", "src", h), ref_of(os.path.join(P, "generated", "mod", "src", h)))
    rm = pickle.load(open(os.path.join(P, "romonly", "data", "gen_data", "wmport_models.pkl"), "rb"))
    mm = {}
    for line in open(os.path.join(out, "pack", "models.txt")):
        n, c, h = line.split(); mm[(n, int(c))] = bytes.fromhex(h)
    ok = rm == mm and list(rm) == list(mm); bad += not ok
    print("%-34s %s" % ("wmport_models.pkl (%d models)" % len(rm), "SAME" if ok else "DIFFERENT"))
    rv = pickle.load(open(ref_of(os.path.join(P, "romonly", "data", "gen_data", "wmport_videos.pkl")), "rb"))
    mv = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(out, "pack", "videos.txt"))}
    ok = rv == mv; bad += not ok
    print("%-34s %s" % ("wmport_videos.pkl", "SAME" if ok else "DIFFERENT %s %s" % (rv, mv)))
    rdir = os.path.join(P, "romonly", "data", "stagecollide", "result")
    nres = 0
    for f in sorted(os.listdir(rdir), key=lambda x: int(x.split(".")[0])):
        mine = os.path.join(out, "pack", "result", f)
        nres += 1
        if not (os.path.exists(mine) and open(mine, "rb").read() == open(os.path.join(rdir, f), "rb").read()):
            bad += 1; print("stagecollide result %s DIFFERENT" % f)
    print("%-34s %d checked" % ("stagecollide results", nres))
    same("wmport.bin.with_rma", os.path.join(out, "rma", "wmport.bin"), os.path.join(P, "romonly", "mod_build", "wmport.bin.with_rma"))
    if a.rma_h: same("wmport_files.h.with_rma", os.path.join(out, "rma", "src", "wmport_files.h"), a.rma_h)
    print("ALL SAME" if not bad else "%d DIFFERENT" % bad)
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
