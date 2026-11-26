"""photos_check.py (phase3 p3photos, 2026-11-19): the native photo headers (exe src\\romimport\\ri_photos.cpp) against their
references, byte for byte.

  rev_photos.h, jarrett_photo.h, meanie_photo.h   chain.py step "portphotos" (tools\\romrender\\mkportphotos.py): reference
                                                  by chain.py ref_of (<private>\\reference\\romrender\\generated\\mod\\src)
  photos_nm.h, mideon_photo.h, droz_photo.h,      the lost producers (no chain step): <private>\\generated\\mod\\src
  bigshow_photo.h, tv_carriers.h                  (photos_lost.py proves the same rules in Python)

    python photos_check.py --ri <ri_cli.exe> --private <private> [--out <folder>]
Runs `ri_cli photos <private>\\roms <tools\\romimport> <out>` (default <private>\\scratch\\photos_check) and prints SAME /
DIFFERENT (with the first differing line) for each header; exit 1 if any differs or is missing."""
import argparse, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
PORTS = ["rev_photos.h", "jarrett_photo.h", "meanie_photo.h"]
LOST = ["photos_nm.h", "mideon_photo.h", "droz_photo.h", "bigshow_photo.h", "tv_carriers.h"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ri", required=True); ap.add_argument("--private", required=True); ap.add_argument("--out")
    a = ap.parse_args()
    P = a.private
    out = a.out or os.path.join(P, "scratch", "photos_check")
    t0 = time.time()
    r = subprocess.run([a.ri, "photos", os.path.join(P, "roms"), os.path.join(SRC, "tools", "romimport"), out],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(r.stdout.rstrip())
    if r.returncode: sys.exit("ri_cli photos failed")
    print("ri_cli photos: %.1f s" % (time.time() - t0))
    NEW_REFS = [os.path.join(P, "reference", n) for n in ("romrender", "revmusic", "reduxthemes")]
    def ref_of(name):                                # (chain.py ref_of: the latest NEW_REFS overlay holding it wins)
        p = os.path.join(P, "generated", "mod", "src", name)
        for root in reversed(NEW_REFS):
            q = os.path.join(root, os.path.relpath(p, P))
            if os.path.exists(q): return q
        return p
    bad = 0
    for name in PORTS + LOST:
        ref, got = ref_of(name), os.path.join(out, name)
        if not os.path.exists(got): print("%-16s MISSING" % name); bad += 1; continue
        a_, b_ = open(got, "rb").read(), open(ref, "rb").read()
        if a_ == b_: print("%-16s SAME  (%d bytes; %s)" % (name, len(a_), os.path.relpath(ref, P))); continue
        bad += 1
        la, lb = a_.split(b"\n"), b_.split(b"\n")
        i = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        print("%-16s DIFFERENT (%d vs %d bytes; first at line %d)\n    native: %r\n    ref:    %r" %
              (name, len(a_), len(b_), i + 1, la[i][:160] if i < len(la) else b"", lb[i][:160] if i < len(lb) else b""))
    print("ALL SAME" if not bad else "%d DIFFERENT" % bad)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
