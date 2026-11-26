"""extract.py (romimport, 2026-11-17): the games' files, unpacked from the player's ROMs, into a ROM-only data folder.

    python extract.py <data folder> [--compare <the project's data folder>]

Writes, from the ROMs alone (roms.py, by fingerprint; a game whose ROM is missing is skipped):
  extracted\\nmfiles\\f\\%04x.bin   No Mercy (Rev 1)        extracted\\rev_files\\%04x.bin   WCW/nWo Revenge
  extracted\\wm2k_files\\%04x.bin   WrestleMania 2000       nmplus\\nmplus.z64 + nmplus\\f\\%04x.bin   No Mercy Plus V3
  baddblood\\files\\%04x.bin        Badd Blood
the layout the generators read (nmpaths.py). --compare: every file against the same file in the other data folder
(the project's extraction): SAME / DIFFERENT / missing there."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roms import Roms

LAYOUT = [("nomercy", "extracted/nmfiles/f"), ("revenge", "extracted/rev_files"), ("wm2000", "extracted/wm2k_files"),
          ("nmplus_v3", "nmplus/f"), ("baddblood", "baddblood/files")]

def main():
    out = sys.argv[1]
    cmp_to = sys.argv[sys.argv.index("--compare") + 1] if "--compare" in sys.argv else None
    R = Roms()
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    for game, sub in LAYOUT:
        if only and game not in only: continue
        F = R.files(game)
        if F is None: print("%-10s no ROM: skipped" % game); continue
        d = os.path.join(out, sub); os.makedirs(d, exist_ok=True)
        same = diff = absent = 0; diffs = []
        for f in range(1, F.count() + 1):
            b = F(f); name = "%04x.bin" % f
            if b is None:                         # (an entry with no bytes: no file, as the project's extraction)
                if cmp_to and os.path.exists(os.path.join(cmp_to, sub, name)): diff += 1; diffs.append(f)
                continue
            with open(os.path.join(d, name), "wb") as w: w.write(b)
            if cmp_to:
                p = os.path.join(cmp_to, sub, name)
                if not os.path.exists(p): absent += 1
                elif open(p, "rb").read() == b: same += 1
                else: diff += 1; diffs.append(f)
        print("%-10s %d files -> %s%s" % (game, F.count(), sub, ("   vs the project's: %d SAME, %d DIFFERENT %s, %d not there"
              % (same, diff, ["0x%X" % x for x in diffs[:8]], absent)) if cmp_to else ""))
    b = R.rom("nmplus_v3")
    if b is not None:
        p = os.path.join(out, "nmplus", "nmplus.z64"); open(p, "wb").write(b)
        if cmp_to:
            q = os.path.join(cmp_to, "nmplus", "nmplus.z64")
            print("nmplus.z64 vs the project's: %s" % ("SAME" if os.path.exists(q) and open(q, "rb").read() == b else "DIFFERENT"))

if __name__ == "__main__":
    main()
