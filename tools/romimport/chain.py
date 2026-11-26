"""chain.py (romimport, 2026-11-18): the ROM-only chain, step by step, each output compared with today's.

Runs in the romimport worktree, whose data\\ (and mod\\build, scratch, tools\\handlers\\work) point at the ROM-only tree
(link_romonly.ps1): only files made from the player's ROMs (extract.py, make_data.py) and our own code and data are there.
Each step is a generator or importer of the project, run unchanged; then each file it makes is compared byte for byte
with the developer's (the shared private\\data, private\\generated, private\\build\\mod\\build).

    python chain.py [--from <step>] [--only <step>[,<step>...]] [--list]

Prints one line a file: SAME / DIFFERENT / MISSING. Exit 1 when any differs."""
import argparse, filecmp, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))                    # the worktree
PRIV = os.path.dirname(os.path.dirname(SRC))                    # private\ (the worktree is private\wt\romimport)
REF_DATA = os.path.join(PRIV, "data")
REF_GEN = os.path.join(PRIV, "generated", "mod", "src")
REF_BUILD = os.path.join(PRIV, "build", "mod", "build")
PY = sys.executable
OUT = os.path.join(SRC, "scratch", "chain")                    # (the ROM-only tree's scratch: steps that take an out folder)
REF_WMLIB = os.path.join(PRIV, "scratch", "romimport", "wmlib_today")   # (wmlib.py run on today's captures, 2026-11-18: equal to the shipped bin)
REF_WRESTLERS = os.path.join(PRIV, "reference", "wrestlers_tested")
# The NEW references (the user's decisions A / B / C): a file made differently on purpose is compared with its copy under one
# of these folders, at the same place relative to private\ (each written by its job's newref.py after it checked that nothing
# else differs from the reference before it); every other file still with the old one.
#   romrender (A): wmport.bin (the arenas' page photos / VS backdrops; Bret's / Neidhart's Titantron frames removed), every
#                  import's photo.bin and its wrestler.json's note, the built-in ports' photo headers (tools\romrender\newref.py)
#   revmusic (B): content.bin / content_index.h and the folders' theme.bin / theme.meta of Revenge's song 0x214 played by us
#   reduxthemes (C): content.bin / content_index.h with Revenge Redux's themes played from the player's patch, not the MP3s
# (in the order the decisions were made: a later one's copy is the reference -- it was made on top of the earlier ones)
NEW_REFS = [os.path.join(PRIV, "reference", r) for r in ("romrender", "revmusic", "reduxthemes")]
def ref_of(p):
    """the reference for p: its copy under the latest NEW_REFS folder that has one"""
    for r in reversed(NEW_REFS):
        q = os.path.join(r, os.path.relpath(p, PRIV))
        if os.path.exists(q): return q
    return p

def d(*p): return os.path.join(SRC, "data", *p)
STAGE3D = d("arenaport", "stage3d")
SCENES = ["nitro", "souledout", "superbrawl", "bash", "havoc", "starrcade", "scene6", "scene7", "scene8"]

# (name, [command], cwd, [(made, reference)])
STEPS = [
    ("make_data", [PY, os.path.join(HERE, "make_data.py"), d(), "--check", REF_DATA], SRC, []),
    ("stage3d_scenes", [PY, "extract.py", "--ram", "../rev/cap/ent0_ram.bin"], STAGE3D,
     [(os.path.join(STAGE3D, "scenes.json"), os.path.join(REF_DATA, "arenaport", "stage3d", "scenes.json"))]),
] + [("stage3d_%s" % n, [PY, "extract.py", "--scene", str(k)], STAGE3D,
      [(os.path.join(STAGE3D, n, "meshes.json"), os.path.join(REF_DATA, "arenaport", "stage3d", n, "meshes.json"))])
     for k, n in enumerate(SCENES)] + [
    ("wm3d_scenes", [PY, "wm3d.py", "--ram", "rig/cap/close1_ram.bin"], d("wmarena"),
     [(d("wmarena", "wm3d_scenes.json"), os.path.join(REF_DATA, "wmarena", "wm3d_scenes.json"))]),
    ("wm_venues", [PY, os.path.join("all", "venues_json.py")], d("wmarena"),
     [(d("wmarena", "wm_venues.json"), os.path.join(REF_DATA, "wmarena", "wm_venues.json"))]),
    ("mkroster", [PY, os.path.join("mod", "gen", "mkroster.py")], SRC,
     [(os.path.join(SRC, "mod", "src", "roster_ext.h"), os.path.join(REF_GEN, "roster_ext.h"))]),
    ("mkparts", [PY, os.path.join("mod", "gen", "mkparts.py")], SRC,
     [(os.path.join(SRC, "mod", "src", "parts_ext.h"), os.path.join(REF_GEN, "parts_ext.h")),
      (os.path.join(SRC, "mod", "build", "parts.pkl"), os.path.join(REF_BUILD, "parts.pkl"))]),
    ("movelib_rev", [PY, os.path.join(HERE, "movelib_rev.py"), os.path.join(OUT, "movelib_revenge.bin")], SRC,
     [(os.path.join(OUT, "movelib_revenge.bin"), os.path.join(REF_BUILD, "movelib_revenge.bin"))]),
    ("wmlib", [PY, os.path.join("tools", "handlers", "wmlib.py"), "lib", os.path.join(OUT, "wmlib")], SRC,
     [(os.path.join(OUT, "wmlib", "movelib_wm2000.bin"), os.path.join(REF_BUILD, "movelib_wm2000.bin")),
      (os.path.join(OUT, "wmlib", "move_lib_wm2000.json"), os.path.join(REF_WMLIB, "move_lib_wm2000.json")),
      (os.path.join(OUT, "wmlib", "rows_wm2000.json"), os.path.join(REF_WMLIB, "rows_wm2000.json"))]),
    ("nm_import", [PY, os.path.join("tools", "handlers", "nm_import.py"), "--out", os.path.join(OUT, "wrestlers")], SRC,
     [(os.path.join(OUT, "wrestlers", "No Mercy"), os.path.join(REF_WRESTLERS, "No Mercy"))]),
    ("revanims", [PY, os.path.join("mod", "gen", "revanims.py")], SRC,
     [(d("gen_data", "rev_anims.json"), os.path.join(REF_DATA, "gen_data", "rev_anims.json")),
      (d("gen_data", "movex_data.h"), os.path.join(REF_DATA, "gen_data", "movex_data.h"))]),
    # WrestleMania 2000's models, built by its own code (wmmodels.py): the importer's and wmport's match RAM images; checked on
    # what they read (descriptor, parts' meshes / pictures / palettes, scale, hands' kinds) against every capture
    # (+ Badd Blood's Vader: the same code)
    ("wmmodels", [PY, os.path.join(HERE, "wmmodels.py"), d(), "--check", REF_DATA], SRC, []),
    # Revenge's models, built by its own code (revmodels.py): the ports' captures and the AKI importer's auto.pkl
    ("revmodels", [PY, os.path.join(HERE, "revmodels.py"), d(), "--check", REF_DATA], SRC, []),
    # the inputs no ROM holds (screenshots), ONLY to prove the rest equal to today's: legacy_in.py says which decision (A)
    # replaces each (the 0x214 recording is no longer one: decision B, played by us)
    ("legacy_in", [PY, os.path.join(HERE, "legacy_in.py"), d()], SRC, []),
    # content.bin: gen/mkcontent.py (the SDK's tabledesign / vadpcm_enc through WSL, as the developer's build; Revenge's 0x214
    # played by us from the ROM -- decision B, NEW_REFS revmusic --; Revenge Redux's themes from the player's patch in roms\
    # -- decision C, NEW_REFS reduxthemes --; the 3 real songs the player's own music files: mod\music = romonly\player_music)
    ("mkcontent", [PY, os.path.join("mod", "gen", "mkcontent.py")], SRC,
     [(os.path.join(SRC, "mod", "build", "content.bin"), os.path.join(REF_BUILD, "content.bin")),
      (os.path.join(SRC, "mod", "src", "content_index.h"), os.path.join(REF_GEN, "content_index.h"))]),
    # wmport.bin: gen/wmport.py, then the arenas' walls (arenas_collide.py reads the pack), then wmport.py again (it writes
    # the walls' headers from them) -- as the developer's build did
    ("wmport1", [PY, os.path.join("mod", "gen", "wmport.py")], SRC, []),
    ("arenas_collide", [PY, os.path.join("mod", "gen", "arenas_collide.py")], SRC,
     [(d("stagecollide", "result"), os.path.join(REF_DATA, "stagecollide", "result"))]),
    ("wmport", [PY, os.path.join("mod", "gen", "wmport.py")], SRC,
     [(os.path.join(SRC, "mod", "build", "wmport.bin"), os.path.join(REF_BUILD, "wmport.bin")),
      (d("gen_data", "wmport_models.pkl"), os.path.join(REF_DATA, "gen_data", "wmport_models.pkl")),
      (d("gen_data", "wmport_videos.pkl"), os.path.join(REF_DATA, "gen_data", "wmport_videos.pkl"))] +
     [(os.path.join(SRC, "mod", "src", h), os.path.join(REF_GEN, h)) for h in (
         "wmport_files.h", "rev_move_anims.h", "arena_files.h", "arena_entrance_files.h", "arena_raw_files.h",
         "arena_ringstage_files.h", "arena_ring_files.h", "arena_screen_files.h", "arena_stage_files.h",
         "arena_table_files.h", "arena_walls_files.h")]),
    ("mkovr", [PY, os.path.join("mod", "gen", "mkovr.py")], SRC,
     [(os.path.join(SRC, "mod", "src", "parts_ovr.h"), os.path.join(REF_GEN, "parts_ovr.h"))]),
    # every imported wrestler folder (import_all.ps1 and what it needs first: imports.py), vs the tested folders
    ("imports", [PY, os.path.join(HERE, "imports.py"), os.path.join(OUT, "imports")], SRC, []),
    # the built-in ports' select photos, rendered (tools/romrender/mkportphotos.py, romrender 2026-11-18): the mod's headers
    ("portphotos", [PY, os.path.join("tools", "romrender", "mkportphotos.py"), os.path.join(OUT, "portphotos")], SRC,
     [(os.path.join(OUT, "portphotos", h), os.path.join(REF_GEN, h)) for h in ("rev_photos.h", "jarrett_photo.h", "meanie_photo.h")]),
    # Revenge Redux's themes added to folders later (the first-launch rules: once per new input, recorded in
    # content\firstrun.json; the player's themes never written over; no patch -> Revenge's own): proved on made-up folders
    ("reduxthemes", [PY, os.path.join(HERE, "reduxthemes.py"), "--selftest", os.path.join(OUT, "reduxthemes_selftest")], SRC, []),
]

def same(a, b):
    """files: byte for byte; folders: every file in either, byte for byte (-> True, or the first difference)"""
    if os.path.isfile(a): return filecmp.cmp(a, ref_of(b), shallow=False) or "differs"
    fa = {os.path.relpath(os.path.join(r, f), a) for r, _, fs in os.walk(a) for f in fs}
    fb = {os.path.relpath(os.path.join(r, f), b) for r, _, fs in os.walk(b) for f in fs}
    if fa != fb: return "files: %d only made, %d only in the reference" % (len(fa - fb), len(fb - fa))
    for f in sorted(fa):
        if not filecmp.cmp(os.path.join(a, f), ref_of(os.path.join(b, f)), shallow=False): return f
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="frm"); ap.add_argument("--only"); ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for s in STEPS: print(s[0])
        return 0
    names = [s[0] for s in STEPS]
    run = names[names.index(a.frm):] if a.frm else names
    if a.only: run = [n for n in names if n in a.only.split(",")]
    bad = 0
    os.makedirs(OUT, exist_ok=True)
    for name, cmd, cwd, outs in STEPS:
        if name not in run: continue
        if name == "reduxthemes": shutil.rmtree(cmd[-1], ignore_errors=True)       # (its scratch: empty each time)
        t = time.time()
        r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        tail = r.stdout.strip().splitlines()[-3:] if r.stdout.strip() else []
        print("== %s (%.0f s, exit %d)" % (name, time.time() - t, r.returncode), flush=True)
        if not outs and name != "make_data" and tail and not r.returncode: print("   " + tail[-1])
        if name in ("make_data",): print("\n".join("   " + l for l in r.stdout.strip().splitlines()))
        if r.returncode:
            print("\n".join("   " + l for l in tail)); bad += 1; continue
        for made, ref in outs:
            if not os.path.exists(made): msg = "MISSING"
            elif not os.path.exists(ref): msg = "(no reference)"
            else:
                r_ = same(made, ref); msg = "SAME" if r_ is True else "DIFFERENT (%s)" % r_
            if msg != "SAME": bad += 1
            print("   %-10s %s" % (msg, os.path.relpath(made, SRC)), flush=True)
    print("ALL SAME" if not bad else "%d NOT SAME" % bad)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
