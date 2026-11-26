"""ri_check.py (phase3, 2026-11-19): every native step of the ROM importer against the Python reference, in one command.

    python tools/romimport/native/ri_check.py --ri <ri_cli.exe> --private <the private folder> [--only <step>[,<step>...]]

ri_cli.exe: tools/romimport/native/build_ri.ps1's. The private folder holds the ROMs (roms\), the ROM-only tree the Python chain
writes (romonly\: its data\ and mod_build\) and the references (reference\, generated\, build\mod\build\). Each step runs the
C++ into a scratch folder (<private>\scratch\ri_check) and compares what it wrote with the reference, byte for byte (pickles
by value). Prints one line a file: SAME / DIFFERENT / MISSING; exit 1 when anything differs.
Steps: images, arenascan, models, data, movelib_rev, themes, reduxtest, hdrfill (needs --plan: mkplan's out folder)."""
import argparse, filecmp, json, os, pickle, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
IMAGES = [("van_sel", "gen_data/van_sel_ram.bin"), ("wm2k_ram", "gen_data/wm2k_ram.bin"), ("nm_pick", "gen_data/nm_pick_ram.bin"),
          ("rev_bm", "extracted/rev_bm_ram.bin"), ("wm_match", "wmport/wm/match_ram.bin"), ("wm2k_entr", "gen_data/wm2k_entr_ram.bin"),
          ("wm_sel1", "handlers_rig/wm2000/cap/sel1_ram.bin"), ("bb_vsel", "handlers_rig/imports2/cap/vsel_ram.bin"),
          ("rev_boot", "revport/attires/cap/v_boot_ram.bin"), ("rev_ent0", "arenaport/rev/cap/ent0_ram.bin"),
          ("wm_close1", "wmarena/rig/cap/close1_ram.bin"), ("wm_fly1", "wmarena/rig/cap/fly1_ram.bin"),
          ("rev_arena", "arenaport/rev/cap/m0_ram.bin")]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ri", required=True); ap.add_argument("--private", required=True)
    ap.add_argument("--only"); ap.add_argument("--plan")
    a = ap.parse_args()
    P = a.private; roms = os.path.join(P, "roms"); data = os.path.join(P, "romonly", "data")
    out = os.path.join(P, "scratch", "ri_check"); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
    tri = os.path.join(SRC, "tools", "romimport")
    bad = 0
    def run(*args):
        r = subprocess.run([a.ri] + [str(x) for x in args], capture_output=True, text=True)
        if r.returncode: print("   ri_cli %s failed: %s" % (args[0], (r.stderr or r.stdout).strip()[-300:]))
        return r
    def same(made, ref, label):
        nonlocal bad
        if not os.path.exists(made): msg = "MISSING"
        elif not os.path.exists(ref): msg = "(no reference)"
        else: msg = "SAME" if filecmp.cmp(made, ref, shallow=False) else "DIFFERENT"
        if msg != "SAME": bad += 1
        print("   %-10s %s" % (msg, label))
    steps = (a.only or "images,arenascan,models,data,movelib_rev,themes,reduxtest,hdrfill").split(",")
    if "images" in steps:
        print("== images (make_data.py's RAM images)")
        for name, rel in IMAGES:
            run("image", roms, os.path.join(tri, "loadmaps"), name, os.path.join(out, "img", name + ".bin"))
            same(os.path.join(out, "img", name + ".bin"), os.path.join(data, rel), rel)
    if "arenascan" in steps:
        print("== arenascan (stage3d extract.py, wm3d.py, venues_json.py)")
        run("arenascan", roms, tri, os.path.join(out, "arena"))
        for r_, _, fs in os.walk(os.path.join(out, "arena")):
            for f in fs:
                rel = os.path.relpath(os.path.join(r_, f), os.path.join(out, "arena"))
                same(os.path.join(r_, f), os.path.join(data, rel), rel)
    if "models" in steps:
        print("== models (wmmodels.py, revmodels.py ports)")
        run("models", roms, tri, os.path.join(out, "mdl"), "all"); run("models", roms, tri, os.path.join(out, "mdl"), "baddblood")
        run("models", roms, tri, os.path.join(out, "mdl"), "revports")
        n = d = 0
        for r_, _, fs in os.walk(os.path.join(out, "mdl")):
            for f in fs:
                p = os.path.join(r_, f); rel = os.path.relpath(p, os.path.join(out, "mdl"))
                if f.endswith("_calls.txt"):
                    calls = [tuple(int(x, 16) for x in l.split()) for l in open(p)]
                    ref = [tuple(c[1:]) for c in pickle.load(open(os.path.join(data, rel[:-10] + "_calls.pkl"), "rb"))]
                    ok = calls == ref
                else: ok = filecmp.cmp(p, os.path.join(data, rel), shallow=False)
                n += 1; d += not ok
                if not ok: print("   DIFFERENT  " + rel)
        bad += d; print("   %d files, %s" % (n, "ALL SAME" if not d else "%d DIFFERENT" % d))
    if "data" in steps:
        print("== data (make_data.py names.pkl, rev_movesets.pkl)")
        run("data", roms, tri, os.path.join(out, "dat"))
        a_ = pickle.load(open(os.path.join(data, "nmport", "names.pkl"), "rb")); b_ = json.load(open(os.path.join(out, "dat", "names.json"), encoding="utf-8"))
        ok = all(list(x.items()) == [(k, v) for k, v in y] for x, y in zip(a_, b_)); bad += not ok
        print("   %-10s names.pkl" % ("SAME" if ok else "DIFFERENT"))
        m = pickle.load(open(os.path.join(data, "revport", "rev_movesets.pkl"), "rb")); j = json.load(open(os.path.join(out, "dat", "rev_movesets.json")))
        ok = [tuple(r) for r in j[0]] == m[0] and all(list(m[1][(r[0], r[1])]) == l for r, l in zip(j[0], j[1])); bad += not ok
        print("   %-10s rev_movesets.pkl" % ("SAME" if ok else "DIFFERENT"))
    if "movelib_rev" in steps:
        print("== movelib_rev (movelib_rev.py)")
        run("movelib_rev", roms, os.path.join(SRC, "tools", "handlers", "handlers", "revenge", "library_index.json"), os.path.join(out, "movelib_revenge.bin"))
        same(os.path.join(out, "movelib_revenge.bin"), os.path.join(P, "romonly", "mod_build", "movelib_revenge.bin"), "movelib_revenge.bin")
    if "themes" in steps:
        print("== themes (handlers revenge theme.py: Revenge's 0x214, Redux by model code)")
        run("theme", roms, "0214", os.path.join(out, "t214"))
        ref = os.path.join(P, "reference", "revmusic", "reference", "wrestlers_tested", "Revenge", "aki_man")
        for f in ("theme.bin", "theme.meta"): same(os.path.join(out, "t214", f), os.path.join(ref, f), "0214 " + f)
        R = os.path.join(P, "reference", "reduxthemes", "reference", "wrestlers_tested", "Revenge")
        for d_ in sorted(os.listdir(R))[:12]:
            if not os.path.exists(os.path.join(R, d_, "theme.bin")): continue
            w = json.load(open(os.path.join(P, "reference", "wrestlers_tested", "Revenge", d_, "wrestler.json"), encoding="utf-8"))
            run("theme", roms, "redux:" + w["source"]["code"], os.path.join(out, "tr", d_))
            for f in ("theme.bin", "theme.meta"): same(os.path.join(out, "tr", d_, f), os.path.join(R, d_, f), "redux %s %s" % (d_, f))
    if "reduxtest" in steps:
        print("== reduxtest (reduxthemes.py --selftest's rules)")
        r = run("reduxtest", roms, os.path.join(out, "rtest"))
        ok = "SELFTEST PASS" in r.stdout; bad += not ok
        print("   %-10s the 6 scenarios" % ("PASS" if ok else "FAIL"))
    if "hdrfill" in steps and a.plan:
        print("== hdrfill (the mod's extras holes from the headers' text)")
        r = subprocess.run([sys.executable, os.path.join(SRC, "tools", "firstrun", "hdrfill_check.py"), "--ri", a.ri, "--plan", a.plan,
                            "--headers", os.path.join(P, "generated", "mod", "src"), "--source", os.path.join(SRC, "mod", "src")], capture_output=True, text=True)
        print("   " + (r.stdout.strip().splitlines() or ["?"])[-1]); bad += r.returncode != 0
    print("ALL SAME" if not bad else "%d NOT SAME" % bad)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
