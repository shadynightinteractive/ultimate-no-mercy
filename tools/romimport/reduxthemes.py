"""reduxthemes.py (reduxthemes, 2026-11-18): Revenge Redux's entrance themes put into the wrestler folders when the player adds
the Redux patch -- under the user's rules (2026-11-18), the Python reference for the first-launch builder (Phase 3):

  * built once per new input: what was done is recorded in <content>\\firstrun.json under "redux_themes" (the Redux ROM's
    SHA-1, the state, the theme files written and their SHA-256); a later run with the same input does nothing and writes
    nothing. A run that stops half way is marked "failed" and done again next time.
  * existing content always wins: a folder's theme is replaced only when it is still the one the importer gave it --
    Revenge's own theme (song 0x214, handlers\\revenge\\theme.py theme_files) -- or the one this step wrote, as its record
    says. A theme the player changed or supplied, or took out, is never touched (logged "kept").
  * no Redux patch in roms\\: nothing is written; those Superstars keep Revenge's own theme.
A folder imported while the patch is there gets Redux's theme from the importer itself (theme.py write_own); this step is
for a patch added later. The themes are gen/reduxmusic.py's (played from the patch as Redux plays it; C++ twin revplay.cpp).
The 3 real songs (Bret Hart, Mr Perfect, Undertaker) are the player's own music files, not part of this.

    python reduxthemes.py <wrestlers folder> <content folder> [--roms <folder>]
    python reduxthemes.py --selftest <empty scratch folder>      (the rules, on made-up folders, with the real ROMs)"""
import argparse, hashlib, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
KEY = "redux_themes"

def sha256(b): return hashlib.sha256(b).hexdigest()
def read(p):
    try: return open(p, "rb").read()
    except OSError: return None
def write_whole(p, b):
    """written whole: a temporary file, then renamed over"""
    t = p + ".tmp"
    open(t, "wb").write(b); os.replace(t, p)

def load_record(content):
    p = os.path.join(content, "firstrun.json")
    try: rec = json.load(open(p, encoding="utf-8"))
    except (OSError, ValueError): rec = {}
    return rec if isinstance(rec, dict) else {}
def save_record(content, rec):
    os.makedirs(content, exist_ok=True)
    write_whole(os.path.join(content, "firstrun.json"), (json.dumps(rec, indent=1) + "\n").encode("utf-8"))

def folders(wrestlers):
    """(folder, its ID, its source.code) for every wrestler folder (a wrestler.json; folders starting with _ skipped, as the
    game does)"""
    for r, ds, fs in os.walk(wrestlers):
        ds[:] = sorted(d for d in ds if not d.startswith("_"))
        if "wrestler.json" in fs:
            try: w = json.load(open(os.path.join(r, "wrestler.json"), encoding="utf-8"))
            except (OSError, ValueError): continue
            fid = w.get("string_id") or w.get("id")
            src = w.get("source") if isinstance(w.get("source"), dict) else {}
            if isinstance(fid, str): yield r, fid, src.get("code") if src.get("game") == "WCW/nWo Revenge" else None

def apply(wrestlers, content, roms=None):
    if roms: os.environ["UNM_ROMS"] = os.path.abspath(roms)
    sys.path.insert(0, os.path.join(SRC, "mod", "gen")); sys.path.insert(0, HERE)
    sys.path.insert(0, os.path.join(SRC, "tools", "handlers"))
    import reduxmusic
    rom = reduxmusic.redux_rom()
    if rom is None:
        print("no WCW/nWo Revenge Redux patch in roms\\: nothing to do (the Revenge Superstars keep Revenge's own theme)")
        return 0
    rec = load_record(content)
    mine = rec.get(KEY) if isinstance(rec.get(KEY), dict) else {}
    inp = hashlib.sha1(rom).hexdigest()
    if mine.get("input") == inp and mine.get("state") == "done":
        print("Revenge Redux's themes: nothing new (done for this patch already)")
        return 0
    wrote = dict(mine.get("wrote", {}))
    rec[KEY] = {"input": inp, "state": "failed", "wrote": wrote}       # (until done: a stop half way is done again)
    save_record(content, rec)
    from handlers.revenge import theme as TH
    try:
        std = TH.theme_files()[:2]                  # Revenge's own theme as the importer writes it
    except Exception as e:                          # (no vanilla Revenge ROM: only this step's own files are replaced)
        print("(Revenge's own theme can't be made here: %s -- only themes this step wrote are replaced)" % e); std = None
    done = kept = same = 0
    for d, fid, code in folders(wrestlers):
        c = reduxmusic.code_of_folder(fid, code)            # (a port by his ID; any other Revenge man by his model code)
        song = reduxmusic.song_for(rom, c) if c is not None else None
        if song is None: continue
        rel = os.path.relpath(d, wrestlers).replace("\\", "/")
        pb, pm = os.path.join(d, "theme.bin"), os.path.join(d, "theme.meta")
        cur = (read(pb), read(pm))
        new = TH.redux_files(song)
        if cur == new[:2]: same += 1; continue
        ours = (std is not None and cur == std) or \
               (rel in wrote and None not in cur and sha256(cur[0] + cur[1]) == wrote[rel])
        others = [f for f in os.listdir(d) if f.lower().startswith("theme.") and f.lower() not in ("theme.bin", "theme.meta")]
        if not ours or others:
            print("  %s: kept the theme there (the player's: never written over)" % rel); kept += 1; continue
        write_whole(pb, new[0]); write_whole(pm, new[1])
        wrote[rel] = sha256(new[0] + new[1])
        print("  %s: %s" % (rel, new[2])); done += 1
    rec[KEY] = {"input": inp, "state": "done", "wrote": wrote}
    save_record(content, rec)
    print("Revenge Redux's themes: %d folders given theirs, %d already had it, %d kept the player's" % (done, same, kept))
    return 0

# ---- the rules, proved on made-up folders (copies of nothing: theme files made here) ----------------------------------------
def selftest(tmp):
    assert not os.path.exists(tmp) or not os.listdir(tmp), "an EMPTY scratch folder"
    sys.path.insert(0, os.path.join(SRC, "mod", "gen")); sys.path.insert(0, HERE)
    sys.path.insert(0, os.path.join(SRC, "tools", "handlers"))
    from roms import Roms
    from handlers.revenge import theme as TH
    import reduxmusic
    std = TH.theme_files()[:2]
    W, C, E = os.path.join(tmp, "wrestlers"), os.path.join(tmp, "content"), os.path.join(tmp, "no_roms")
    os.makedirs(E)
    def man(rel, fid, theme, code=None, game="WCW/nWo Revenge"):
        d = os.path.join(W, rel); os.makedirs(d, exist_ok=True)
        w = {"id": fid, "name": fid.upper()}
        if code: w["source"] = {"game": game, "code": code}
        json.dump(w, open(os.path.join(d, "wrestler.json"), "w"))
        if theme: open(os.path.join(d, "theme.bin"), "wb").write(theme[0]); open(os.path.join(d, "theme.meta"), "wb").write(theme[1])
    players = (b"the player's own theme", b"\0" * 0xD8)
    man("Revenge/kevin_nash", "kevin_nash", std)                         # the importer's theme: replaced
    man("Revenge/scott_steiner", "scott_steiner", std)
    man("Revenge/scott_steiner/scott_steiner_b", "scott_steiner_b", std) # a second look: his base's song
    man("Revenge/sting", "sting", players)                               # the player's: kept
    man("Revenge/booker_t", "booker_t", None)                            # theme taken out by the player: kept so
    man("Revenge/jim_neidhart", "jim_neidhart", std)                     # a port Redux gives a song (0x2A): replaced
    man("Revenge/chris_benoit", "chris_benoit", std, "0x009")            # a never-ported man, by his model code (0x24)
    man("Revenge/aki_man", "aki_man", std, "0x601")                      # "Original 1" (0x10): Revenge's own -- untouched
    man("No Mercy/benoit_lookalike", "chris_benoit_nm", std, "0x009", "WWF No Mercy")   # not from Revenge: untouched
    snap = lambda: {os.path.relpath(os.path.join(r, f), tmp): read(os.path.join(r, f)) for r, _, fs in os.walk(tmp) for f in fs}
    run = lambda roms: subprocess.run([sys.executable, os.path.abspath(__file__), W, C, "--roms", roms], capture_output=True, text=True)
    ok = True
    def check(what, cond):
        nonlocal ok
        ok &= bool(cond); print("%s  %s" % ("PASS" if cond else "FAIL", what))
    a = snap(); r1 = run(E)
    check("no Redux patch: nothing written, no record", r1.returncode == 0 and snap() == a)
    roms = Roms().folder
    r2 = run(roms); b = snap()
    R = reduxmusic.redux_rom(); rf = lambda name: TH.redux_files(reduxmusic.song_of(R, name))
    rx = rf("Kevin Nash")[:2]
    check("the patch: the importer's themes replaced by Redux's (Nash, Neidhart, Scott Steiner + his second look, Benoit by code)",
          r2.returncode == 0 and (b["wrestlers/Revenge/kevin_nash/theme.bin".replace("/", os.sep)], b["wrestlers/Revenge/kevin_nash/theme.meta".replace("/", os.sep)]) == rx
          and b[os.path.join("wrestlers", "Revenge", "scott_steiner", "scott_steiner_b", "theme.bin")] == rf("Scott Steiner")[0]
          and b[os.path.join("wrestlers", "Revenge", "scott_steiner", "theme.bin")] == rf("Scott Steiner")[0]
          and b[os.path.join("wrestlers", "Revenge", "jim_neidhart", "theme.bin")] == rf("Jim Neidhart")[0]
          and b[os.path.join("wrestlers", "Revenge", "chris_benoit", "theme.bin")] == TH.redux_files(0x24)[0])
    check("the player's theme kept (Sting), a taken-out theme not put back (Booker T), no Redux song / not Revenge untouched",
          b[os.path.join("wrestlers", "Revenge", "sting", "theme.bin")] == players[0]
          and not os.path.exists(os.path.join(W, "Revenge", "booker_t", "theme.bin"))
          and b[os.path.join("wrestlers", "Revenge", "aki_man", "theme.bin")] == std[0]
          and b[os.path.join("wrestlers", "No Mercy", "benoit_lookalike", "theme.bin")] == std[0])
    recd = json.load(open(os.path.join(C, "firstrun.json")))[KEY]
    check("recorded in content\\firstrun.json (input, done, the 5 folders written)",
          recd["state"] == "done" and len(recd["wrote"]) == 5)
    r3 = run(roms)
    check("a second start with nothing new: no file written", r3.returncode == 0 and snap() == b and "nothing new" in r3.stdout)
    # the player changes Nash's theme; then a NEW input (another Redux version, simulated by the record)
    open(os.path.join(W, "Revenge", "kevin_nash", "theme.bin"), "wb").write(b"the player's Nash")
    rec = json.load(open(os.path.join(C, "firstrun.json"))); rec[KEY]["input"] = "an older patch"
    open(os.path.join(C, "firstrun.json"), "w").write(json.dumps(rec, indent=1) + "\n")
    r4 = run(roms); c = snap()
    check("a new input later: the player's changed theme kept (Nash), this step's own still its own",
          r4.returncode == 0 and c[os.path.join("wrestlers", "Revenge", "kevin_nash", "theme.bin")] == b"the player's Nash"
          and c[os.path.join("wrestlers", "Revenge", "scott_steiner", "theme.bin")] == rf("Scott Steiner")[0])
    for r_ in (r1, r2, r3, r4):
        if r_.returncode: print(r_.stdout, r_.stderr)
    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("wrestlers", nargs="?"); ap.add_argument("content", nargs="?")
    ap.add_argument("--roms"); ap.add_argument("--selftest")
    a = ap.parse_args()
    if a.selftest: sys.exit(selftest(a.selftest))
    if not (a.wrestlers and a.content): ap.error("<wrestlers folder> <content folder>")
    sys.exit(apply(a.wrestlers, a.content, a.roms))
