# Ultimate No Mercy -- the developer guide (the source tree's README until the public release)

A native PC port of WWF No Mercy (N64) with the Ultimate No Mercy mod: the game's code converted to C++
(non-matching decompilation in progress), the mod (C), the importers that make wrestler and arena folders from the
player's own ROMs, a native renderer, the tests and the release pipeline.

## The three folders (2026-11-17)

| Folder | What |
|---|---|
| `source\` (this one) | everything the project made: a git repository, relative paths only, no game data. See `PUBLISHING.md` |
| `private\` (beside it) | never shared: the ROMs, the files extracted from them and everything our tools make from them, the third-party toolchain, build outputs, caches, test runs, notes. Linked into this tree at the paths the tools expect (`tools\layout\private_links.txt`, made by `tools\layout\link_private.ps1`; all git-ignored) |
| `game\` (beside it) | the playable install: `deploy.ps1` puts the build there, the importers write its wrestler folders. `$env:UNM_GAME` names another |

A fresh checkout: put the private folder beside it (or set `UNM_PRIVATE`) and run `tools\layout\link_private.ps1`.

## Do this

Open PowerShell in this folder:

| Command | Does |
|---|---|
| `.\build_mod.ps1` | builds the mod -> `mod\build\mod_six\nomercy_arcade.nrm` |
| `.\build_exe.ps1` | builds the game exe -> `exe\build\Ultimate No Mercy.exe` (first time ~10 min) |
| `.\build_test.ps1 -Movex` | builds the test-rig blob -> `mod\build\test6\arcade_test.bin` |
| `.\deploy.ps1` | backs up the game's current mod to `backups\<version>`, puts the new one in (`-Exe`: the exe too) |

## QA: testing in the real game

**Play Test Build.cmd** (double-click): the newest build, played by you in the QA copy of the game and recorded (`qa.ps1 -Record`); close the game when done.

`.\qa.ps1 <scenario>` plays a scenario in the exe and mod just built -- in its own copy of the game (`qa\game`; the
game itself is never touched) -- and checks it. A scenario (`qa\scenarios\<name>.json`; the format: the top of
`qa\qa_tool.py`) says the match: the two Superstars, costumes, arena, who controls each (the CPU or a scripted
controller), SPECIAL at the bell, other mod options, and what must be true (checks on the game's state, which the
exe writes 20 times a second). The mod's hidden QA mode sets the match up directly (no menus, no coins).
Each run: `qa\runs\<date>_<time>_<scenario>\` -- `report.txt` (PASS / FAIL, each check), `contact.png` (the
screenshots on one page), `shots\`, `state.csv`, the game's log. `python qa\qa_tool.py names` lists the
Superstars and arenas a scenario can name.

**The rule: nothing is called fixed until a scenario that showed the problem passes in the real game, and the
report and screenshots have been looked at.**

A new version: bump `version` in `mod\recomp\mod.toml` and the version line + changelog in `mod\README.txt`
(the player's "ARCADE README.txt"), build, deploy.

## What's where

| Folder | What |
|---|---|
| `mod\` | **the mod's source**: `src\` (C, runs inside the game), `gen\` (Python that makes data and headers), `recomp\mod.toml` (manifest, options), `README.txt` (player README + changelog), **`NOTES.md` (the technical notes: read first)**, `music\` (entrance themes: `<Superstar's name>.mp3`, built into the mod), `build\` (output; `content.bin`, `wmport.bin` are made by `gen\mkcontent.py` / `gen\wmport.py`) |
| `exe\` | the game exe: `WWFNoMercyRecomp\` (the PC port's source, our changes in), `patches\` (those changes as patches, and the CMake toolchain file), `commentary-sim\` (a desk test of the commentary director) |
| `six-engine\` | the six-man engine: `NoMercySix\` (its recomp project and ROM), `work\` (the scripts and data that made it; `db.pkl` is read by the mod build) |
| `libs\` | third-party sources: `AkiMods\` (mod linker script + headers), `N64Recomp\` (RecompModTool) |
| `roms\` | No Mercy Rev 1 and v1.0, the NoMercySix ROM, No Mercy Plus V3 (xdelta), WrestleMania 2000, WCW/nWo Revenge, WCW/nWo Revenge Redux (its release .zip or xdelta; optional: its entrance themes) |
| `commentary\` | `README.txt`: the commentary as a spreadsheet to edit -- the spreadsheet itself (`commentary.csv`, every line with its transcript), its clips and the transcripts are the developer's, in the private folder's `data\commentary_text\` (not published: the user's decision); the game's recipe is `exe\WWFNoMercyRecomp\assets\commentary\wwe13\` (`tools\commentary\README.md`) |
| `data\` | inputs the generators read: files extracted from each game, No Mercy Plus unpacked, the WM2000 / Revenge ports' work and captures, commentary audio, RAM captures (`gen_data\`) |
| `rig\` | the headless test rig: `harness\` (Python; `rig.py`), `core\` (the parallel-n64 emulator core), `states\` (saved states) |
| `toolchain\` | python 3.10 (+ numpy, scipy, pillow), llvm-mingw, zig 0.13, cmake, ninja, ffmpeg, `bin\RecompModTool.exe`, `mingw\` (DirectX-Headers, FreeType); `env.ps1` puts them on PATH |
| `backups\` | each deployed version's mod + README, old crash reports, the old Linux build scripts |
| `scratch\` | made by scripts as they run (emulator work folders, temporary files): safe to delete |
| `nmpaths.py` | where every file is: the scripts name files by their old Linux paths and this maps them here |

## Python scripts

Run them with the toolchain's Python: `. .\toolchain\env.ps1` once in a PowerShell window, then `python mod\gen\x.py`.
A new script that reads project files starts with the same two lines the others have (they find `nmpaths.py`)
and names files through `nmp(...)`.

## Commentary

To edit it as a spreadsheet: `data\commentary_text\commentary.csv` (private; see `commentary\README.txt`); `python mod\gen\comcsv.py import`
then `.\deploy.ps1 -Commentary`. Below: the lower-level way.

The game's `commentary.pak` (+ `.1`, `.2`, `.3`) is the master copy of the commentary: every line JR and King can
say, what it's for, and its audio. To change it:

```
. .\toolchain\env.ps1
python mod\gen\compak.py unpack .. scratch\commentary          # -> lines.tsv, moves.tsv, clips\NNNN.ogg
#   edit lines.tsv (one row per line: clip, event, wrestler, speaker, weight, text, ...; compak.py's top explains
#   each field) -- delete rows to drop lines, change fields to re-file them
python mod\gen\compak.py add scratch\commentary new_line.wav   # a new clip (any audio file): prints its number
python mod\gen\compak.py pack scratch\commentary mod\build\commentary.pak
.\deploy.ps1 -Commentary                                        # backs up the game's pack, puts this one in
```
Unpacking and packing again gives the same files byte for byte. How the lines are chosen and played (timing,
who answers whom, splicing) is the exe's code: `exe\WWFNoMercyRecomp\src\main\commentary\director.cpp`.
No AI-voiced lines: the AI voice's 13 name clips were taken out of the pack and its model deleted (2026-11-14,
the user). The pack is the user's own copy of other games' audio and is never shipped; a player builds it from
their own WWE '13 disc (design and tools: `scratch\agents\commentary\`).
More lines from WWE games (not set up yet): Legends of WrestleMania's whole set is here (`data\commentary_audio\lowm\mp3`,
4794 clips, each titled with the game's own label; 1213 are in the pack). WWE '13: only the clips in the pack, its
soundbank layout (`wwe13\big.xml`) and vgmstream's source (`wwe13\vgm`); more needs the disc image again (the old
extraction scripts weren't kept). Speech-to-text: whisper.cpp's source is in `wwe13\wcpp` (to build with the
toolchain; its model, ggml-small.en, ~190 MB, to download). New clips are heard and kept by hand before they go in. (`mod\gen\mkcommentary.py` made the pack from the sorted
WWE '13 / Legends dialogue the first time; some of its inputs weren't kept, so it can't remake it from scratch.)

## Known gaps

* `rig\core\win\parallel_n64_libretro.dll` is libretro's stock build: the old Linux core's memory-trace functions
  (`retro_trace_*`, used by research scripts in `six-engine\work`, `data\wmport`, `data\revport`) aren't in it.
* Not carried over from the old setup (only needed to remake the commentary splices / one data pack, not to
  build): `nmbank.pkl`, the `names\`, `leads\`, `elim\`, `names2\`, `ainames\` splice work, the whisper.cpp build.

## Custom stories

The game reads `stories\*.json` in the game folder (the exe's `exe\WWFNoMercyRecomp\src\main\arcade_stories.cpp`
compiles them when the Custom Story list opens: the same packs as `mod\gen\storyc.py`, byte for byte). The samples
are `mod\stories\*.json`; `build_mod.ps1` checks them and puts them, with `_roster.json` (the Superstars' names),
in `mod\build\stories\`, which `qa.ps1` and `deploy.ps1` copy to the game. Authoring guide: `mod\stories\README.md`.
The "Custom Story" picture is made by `mod\gen\labeltex.py` (letters cut from the ROM's own labels) -> `src\label_tex.h`.
