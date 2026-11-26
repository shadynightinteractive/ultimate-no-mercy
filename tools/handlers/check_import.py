#!/usr/bin/env python3
"""check_import (modcheck, 2026-11-13): the checks run as part of EVERY import from a mod or another game.

  1. the mod table diff (once per mod):  python tool\\modcheck.py tables ... --out work\\<mod>   -> work\\<mod>\\remap.json
     The handler loads that remap and stops (ModTableChanged) if it uses a stock meaning the mod changed -- the import itself
     fails loudly; nothing to run here.
  2. the visual check of every folder it wrote:
        python tool\\check_import.py --dir <wrestlers folder> --who <folder name>=<source pictures prefix>[,...]
                                     --family nm|cross [--view 0.62] [--box x0,y0,x1,y1] [--qa]
     For each folder: its QA scenario (tool\\scenario.py: the Superstar Select tour, every costume), the QA run in our
     game (run_mine.ps1, -Dir <wrestlers folder>: it waits for a free machine as RULES.md says) when --qa is given (else
     the newest run of that scenario in runs.txt), then the comparison with the source game's pictures
     <prefix>_c<k>.png / <prefix>_c<k>_plate.png (or <prefix>_plate.png for No Mercy's family) made in the rig:
        No Mercy Plus (and any No Mercy mod):  rig\\nm_sel.py <prefix> <4 appearances>   (scripts\\nm_refs.py)
        WCW/nWo Revenge:                        rig\\rv_shots.py <string id>
        Badd Blood / WrestleMania 2000:         rig\\bb.py (its select; plates: the median of the page's other men)
     -> work\\visual\\<name>.txt and the side-by-side pictures; exit 1 if any costume is a MISMATCH or missing.
  3. a texture pack that comes with the mod:  python tool\\texcheck.py check / renders  (see there)
"""
import argparse, json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
# (the folder with the QA runner and its scenarios -- modcheck's run_mine.ps1 -- NM_IMPORT_CHECK_DIR, else tools\handlers\work;
#  the QA runs are the project's qa\runs. 2026-11-13, final082)
MC = os.environ.get("NM_IMPORT_CHECK_DIR") or os.path.join(HERE, "work")
sys.path.insert(0, HERE)
import visual as V, scenario as SC

def newest_run(scn):
    """the scenario's newest QA run (qa\\runs\<date>_<time>_<scenario>, with its report)"""
    import glob
    ds = [d for d in sorted(glob.glob(os.path.join(__import__("handlers").DEV, "qa", "runs", "*_" + scn)))
          if os.path.exists(os.path.join(d, "report.txt"))]
    return os.path.normpath(ds[-1]) if ds else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True); ap.add_argument("--who", required=True)
    ap.add_argument("--family", choices=("nm", "cross"), default="nm"); ap.add_argument("--view", default="0.62")
    ap.add_argument("--box", default="0,0,1,1"); ap.add_argument("--qa", action="store_true")
    a = ap.parse_args()
    bad = 0
    for item in a.who.split(","):
        sid, prefix = item.split("=")
        folder = next((os.path.join(r, sid) for r, ds, _ in os.walk(a.dir) if sid in ds), None)
        if folder is None: print("no folder %s under %s" % (sid, a.dir)); bad += 1; continue
        w = json.load(open(os.path.join(folder, "wrestler.json"))); wid = int(w["id"], 0)
        scn = "mc_imp_" + sid
        SC.sel(scn, wid, "(%s: the import check)" % sid)
        if a.qa:
            subprocess.run(["powershell", "-NoProfile", "-File", os.path.join(MC, "run_mine.ps1"), "-Scenarios", scn, "-Dir", a.dir])
        run = newest_run(scn)
        if not run: print("%s: no QA run of %s yet (--qa runs it)" % (sid, scn)); bad += 1; continue
        n = len(w.get("looks", [])) or 4
        if a.family == "nm":
            ok = V.check_nm(sid, run, wid, prefix, n, os.path.join(MC, "work", "visual"))
        else:
            src = [(prefix + "_c%d.png" % c, prefix + "_c%d_plate.png" % c) for c in range(n)]
            ok = V.check_cross(sid, run, wid, src, os.path.join(MC, "work", "visual"), a.view, n,
                               tuple(float(v) for v in a.box.split(",")))
        bad += 0 if ok else 1
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
