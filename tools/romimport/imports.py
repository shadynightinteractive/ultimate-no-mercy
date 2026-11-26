"""imports.py (romimport, 2026-11-18): every imported wrestler folder, made in the ROM-only tree the way
tools\\handlers\\import_all.ps1 makes them, and compared with the tested folders (private\\reference\\wrestlers_tested).

What import_all.ps1 expects to be there first, made here from the ROMs:
  mod\\build\\movelib_revenge.bin, movelib_wm2000.bin   the chain's movelib_rev / wmlib outputs (equal to the shipped ones)
  mod\\build\\wmport.bin.with_rma + mod\\src\\wmport_files.h.with_rma
                                                    the pack WITH the Revenge move animations (gen/wmport.py with
                                                    WMPORT_REV_ANIMS=1; pack_drop.py's names) -- revlib.py finds
                                                    movelib's 78 library moves there; the normal pack is put back after
  tools\\handlers\\work\\revenge_library             revlib.py (its rows_all.json, move_lib.json, movelib_revenge.bin)
  ...\\revenge_library\\records_damage.json          frozen\\records_damage.json (our record changes; its producer,
                                                    movelib2's recdiff.py --damage, is in the old folder's scratch only)
  tools\\handlers\\work\\baddblood, nmplus            modcheck.py tables (the mods' remaps, from the ROMs + the xdeltas)
Then import_all.ps1 -Out <out> (the toolchain's Python, as the tested folders were made).

The comparison: every file byte for byte. Known: 45 Revenge wrestler.json differ only in the note "source.abilities" --
the tested folders carry an old escaping bug there (HANDOFF, newhome2: the game never reads "source"); counted apart.

    python imports.py <out folder>"""
import filecmp, json, os, shutil, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
PRIV = os.path.dirname(os.path.dirname(SRC))
REF = os.path.join(PRIV, "reference", "wrestlers_tested")
NEW_REFS = [os.path.join(PRIV, "reference", r) for r in ("romrender", "revmusic", "reduxthemes")]   # (chain.py NEW_REFS)
def ref_of(p):
    for r in reversed(NEW_REFS):
        q = os.path.join(r, os.path.relpath(p, PRIV))
        if os.path.exists(q): return q
    return p
ROMS = os.path.join(PRIV, "roms")
PY = sys.executable

def run(cmd, env=None, log=None):
    r = subprocess.run(cmd, cwd=SRC, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if log: open(log, "w").write(r.stdout)
    if r.returncode: print(r.stdout[-2000:]); raise SystemExit("%s failed (exit %d)" % (os.path.basename(cmd[1]), r.returncode))
    return r.stdout

def only_abilities_note(p, q):
    a, b = json.load(open(p)), json.load(open(q))
    out = []
    def walk(x, y, path):
        if isinstance(x, dict) and isinstance(y, dict):
            for k in set(x) | set(y): walk(x.get(k), y.get(k), path + "/" + k)
        elif x != y: out.append(path)
    walk(a, b, "")
    return out == ["/source/abilities"]

def main():
    out = os.path.abspath(sys.argv[1])
    logs = os.path.join(out + "_prep"); os.makedirs(logs, exist_ok=True)
    mb, ms, work = os.path.join(SRC, "mod", "build"), os.path.join(SRC, "mod", "src"), os.path.join(SRC, "tools", "handlers", "work")
    ch = os.path.join(SRC, "scratch", "chain")
    shutil.copyfile(os.path.join(ch, "movelib_revenge.bin"), os.path.join(mb, "movelib_revenge.bin"))
    shutil.copyfile(os.path.join(ch, "wmlib", "movelib_wm2000.bin"), os.path.join(mb, "movelib_wm2000.bin"))
    # the pack with the Revenge move animations, then the normal one back
    keep = {p: open(p, "rb").read() for p in (os.path.join(mb, "wmport.bin"), os.path.join(ms, "wmport_files.h"))}
    run([PY, os.path.join("mod", "gen", "wmport.py")], env=dict(os.environ, WMPORT_REV_ANIMS="1"), log=os.path.join(logs, "wmport_rma.log"))
    for p in keep:
        shutil.copyfile(p, p + ".with_rma"); open(p, "wb").write(keep[p])
    lib = os.path.join(work, "revenge_library"); os.makedirs(lib, exist_ok=True)
    print(run([PY, os.path.join("tools", "handlers", "revlib.py"), lib], log=os.path.join(logs, "revlib.log")).strip().splitlines()[-1])
    shutil.copyfile(os.path.join(HERE, "frozen", "records_damage.json"), os.path.join(lib, "records_damage.json"))
    mc = os.path.join("tools", "handlers", "modcheck.py")
    run([PY, mc, "tables", "--family", "wm2000", "--base", os.path.join(ROMS, "WWF_WrestleMania_2000.zip"),
         "--mod", os.path.join(ROMS, "WWF WarZone - Badd Blood (USA) (v1.0).z64"),
         "--patch", os.path.join(ROMS, "WarZone Badd Blood (Non Homebrew ID).xdelta"), "--out", os.path.join(work, "baddblood")],
        log=os.path.join(logs, "modcheck_baddblood.log"))
    run([PY, mc, "tables", "--family", "nomercy", "--base", os.path.join(ROMS, "WWF No Mercy (USA) v1.0.z64"),
         "--patch", os.path.join(ROMS, "No Mercy Plus V3.xdelta"), "--base-ram", os.path.join("data", "gen_data", "van_sel_ram.bin"),
         "--mod-ram", os.path.join("data", "gen_data", "nm_pick_ram.bin"), "--out", os.path.join(work, "nmplus")],
        log=os.path.join(logs, "modcheck_nmplus.log"))
    if os.path.exists(out): shutil.rmtree(out)
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(SRC, "tools", "handlers", "import_all.ps1"),
                        "-Out", out], cwd=SRC, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    print(r.stdout.strip())
    if r.returncode: raise SystemExit("import_all.ps1 failed")
    bad = 0
    for col in ("No Mercy", "Revenge", "WrestleMania 2000", "Imported"):
        pa, pb = os.path.join(out, col), os.path.join(REF, col)
        fa = {os.path.relpath(os.path.join(r_, f), pa) for r_, _, fs in os.walk(pa) for f in fs}
        fb = {os.path.relpath(os.path.join(r_, f), pb) for r_, _, fs in os.walk(pb) for f in fs}
        diff = [f for f in sorted(fa & fb) if not filecmp.cmp(os.path.join(pa, f), ref_of(os.path.join(pb, f)), shallow=False)]
        known = [f for f in diff if f.endswith("wrestler.json") and only_abilities_note(os.path.join(pa, f), ref_of(os.path.join(pb, f)))]
        other = sorted(set(diff) - set(known)) + sorted(fa ^ fb)
        bad += len(other)
        print("%-18s %5d files: %s%s" % (col, len(fa), "SAME" if not other else "%d DIFFERENT %s" % (len(other), other[:5]),
                                          " (+ %d known: the note source.abilities only)" % len(known) if known else ""))
    print("ALL SAME" if not bad else "%d NOT SAME" % bad)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
