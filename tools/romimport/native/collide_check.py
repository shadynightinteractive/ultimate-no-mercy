#!/usr/bin/env python3
"""collide_check.py (phase3 p3collide, 2026-11-19): the developer's check of ri_collide (mod/gen/arenas_collide.py made
natively) against the Python chain's result files, in one command.

    python tools/romimport/native/collide_check.py --ri <ri_cli.exe> --private <the private folder> [--keep]

Runs  ri_cli collide <private>/roms <pack> <headers> <out> <this tree's mod/gen/arenas_collide.py> --wmport-h <wmport_files.h>
with the inputs the chain's arenas_collide step had: the pack <private>/romonly/mod_build/wmport.bin, the headers of
<private>/generated/mod/src, wmport_files.h from the newest reference overlay that has it (chain.py ref_of:
<private>/reference/romrender/generated/mod/src/ first). <out> is <private>/scratch/collide_check (emptied first).
Compares <out>/<n>.json with <private>/romonly/data/stagecollide/result/<n>.json byte for byte, n = 0 .. 22 (the key too:
it hashes this tree's arenas_collide.py, so a changed .py shows as DIFFERENT keys -- said per file). Prints one line per
arena, SAME / DIFFERENT, and the total; exit 1 if any differs.
"""
import argparse, json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OVERLAYS = ["romrender", "revmusic", "reduxthemes"]    # (chain.py NEW_REFS: the first that has a file is its reference)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ri", required=True)
    ap.add_argument("--private", required=True)
    ap.add_argument("--keep", action="store_true", help="keep the scratch output folder as it is (no emptying first)")
    a = ap.parse_args()
    P = a.private
    hdrs = os.path.join(P, "generated", "mod", "src")
    wmport_h = os.path.join(hdrs, "wmport_files.h")
    for o in OVERLAYS:
        q = os.path.join(P, "reference", o, "generated", "mod", "src", "wmport_files.h")
        if os.path.exists(q):
            wmport_h = q
            break
    out = os.path.join(P, "scratch", "collide_check")
    if not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    py = os.path.join(SRC, "mod", "gen", "arenas_collide.py")
    t0 = time.time()
    r = subprocess.run([a.ri, "collide", os.path.join(P, "roms"), os.path.join(P, "romonly", "mod_build", "wmport.bin"), hdrs, out, py,
                        "--wmport-h", wmport_h], capture_output=True, text=True)
    secs = time.time() - t0
    if r.returncode:
        print("ri_cli collide failed: %s" % (r.stderr or r.stdout).strip()[-500:])
        return 1
    print(r.stdout.rstrip())
    ref = os.path.join(P, "romonly", "data", "stagecollide", "result")
    same = 0
    bad = []
    for n in range(23):
        mine, theirs = os.path.join(out, "%d.json" % n), os.path.join(ref, "%d.json" % n)
        if not os.path.exists(mine):
            msg = "MISSING"
        elif not os.path.exists(theirs):
            msg = "(no reference)"
        else:
            A, B = open(mine, "rb").read(), open(theirs, "rb").read()
            if A == B:
                msg = "SAME"
            else:
                ja, jb = json.loads(A), json.loads(B)
                keys = [k for k in jb if ja.get(k) != jb.get(k)] + [k for k in ja if k not in jb]
                msg = "DIFFERENT (%s)" % ", ".join(keys or ["text only"])
        if msg == "SAME":
            same += 1
        else:
            bad.append(n)
        print("   %-28s %2d.json" % (msg, n))
    print("%d of 23 SAME%s; ri_cli collide %.2f s" % (same, "" if not bad else ", DIFFERENT: %s" % bad, secs))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
