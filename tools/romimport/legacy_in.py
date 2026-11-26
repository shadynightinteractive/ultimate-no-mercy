"""legacy_in.py (romimport, 2026-11-18): copies the few inputs no ROM holds into the ROM-only data folder -- ONLY so the chain
can prove everything else equal to today's build. Each is replaced by the user's decisions of 2026-11-17 (a new reference):

  gen_data\\rev_songs\\0214.wav            an emulator audio recording of Revenge's song 0x214               -> B: we play it (done)

(revmusic, 2026-11-18: decision B -- Revenge's song 0x214 is played by us from its sequence in the ROM, mod/gen/revplay.py;
the recording is no longer copied in: INPUTS is empty. It stays in the legacy folder as the record.)

(romrender, 2026-11-18: the user REMOVED Bret Hart's and Jim Neidhart's Titantron -- revport\\cap\\tron_bret, tron_neidhart,
screenshots of Revenge's entrances: like any wrestler without one, they have none unless the player's folder gives one;
gen/wmport.py keeps their frame files' places as empty files. No longer copied in.)

(romrender, 2026-11-18: decision A -- these screenshots are no longer inputs: arenaport\\pics\\<arena>_stage.png and
wmarena\\pics\\*.png (the ARENA page photos / VS backdrops), handlers_rig\\revenge\\cap\\cc_*.png, handlers_rig\\wm2000\\cap\\
sel_*.png, handlers_rig\\imports2\\cap\\p1.png and goldust_photo.* (the imports' photos): all made by tools\\romrender from the
ROM-built models and meshes instead. They stay in the legacy folder as the developer's record; this no longer copies them.)

They live in private\\romonly\\legacy_inputs\\data (the developer's copies), never in the player's build.

The music files (reduxthemes, 2026-11-18): the ROM-only tree's mod\\music (link_romonly.ps1: romonly\\player_music) gets only
the player's OWN songs -- PLAYER_SONGS, the 3 real-world songs the user supplied (optional for a player, in no ROM) -- and
never the 15 MP3s the project once rendered from Revenge Redux: decision C builds those themes from the player's Redux patch
in roms\\ (gen/reduxmusic.py). Any other music file found there is taken out (the folder is this tree's own).

    python legacy_in.py <data folder> [<legacy folder>]"""
import os, shutil, sys

INPUTS = []                  # (0214.wav was here: decision B, played by us)
INPUT_FOLDERS = []           # (the Titantron shots were here: removed, above)

# the player's own songs (real-world recordings the user supplied: no ROM has them), copied from the developer's music folder
PLAYER_SONGS = ["Bret Hart.mp3", "Mr Perfect.mp3", "Undertaker.mp3"]
MUSIC_EXT = (".mp3", ".wav", ".ogg", ".flac", ".m4a")
def player_music(music, supplied):
    """romonly\\player_music: exactly PLAYER_SONGS (only when 'music' is the ROM-only tree's own folder)"""
    real = os.path.realpath(music)
    if os.path.basename(real) != "player_music" or "romonly" not in real.split(os.sep):
        print("mod\\music is not the ROM-only tree's player_music (link_romonly.ps1): left alone"); return 1
    out = []
    for f in sorted(os.listdir(music)):
        if f.lower().endswith(MUSIC_EXT) and f not in PLAYER_SONGS: os.remove(os.path.join(music, f)); out.append(f)
    for f in PLAYER_SONGS:
        shutil.copyfile(os.path.join(supplied, f), os.path.join(music, f))
    print("music files: the player's own %d songs (%s)%s" % (len(PLAYER_SONGS), ", ".join(PLAYER_SONGS),
          "; taken out: %s" % ", ".join(out) if out else ""))
    return 0

def main():
    out = sys.argv[1]
    src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))), "romonly", "legacy_inputs", "data")
    files = list(INPUTS)
    for d in INPUT_FOLDERS: files += [os.path.join(d, f) for f in sorted(os.listdir(os.path.join(src, d)))]
    n = 0
    for rel in files:
        p = os.path.join(out, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
        shutil.copyfile(os.path.join(src, rel), p); n += 1
    print("legacy inputs copied in: %d files (none left: decisions A / B replaced them)" % n)
    here = os.path.dirname(os.path.abspath(__file__))
    priv = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(here))))
    bad = player_music(os.path.join(os.path.dirname(os.path.dirname(here)), "mod", "music"),
                       os.path.join(priv, "supplied", "mod", "music"))
    return 0 if n == len(files) and not bad else 1

if __name__ == "__main__":
    sys.exit(main())
