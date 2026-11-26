# Making a release package

The release package is the folder (and zip) a player downloads. The user's rule (2026-11-14): **no game data shipped, ROM(s)
needed.** So the package holds only what this repository makes and the third-party programs the game runs with; everything
made from a game is made on the player's own PC from the player's own ROMs.

## Steps

From the source folder, in Windows PowerShell:

    .\build_mod.ps1                          # the mod  -> mod\build\mod_six\nomercy_arcade.nrm (+ stories)
    .\build_exe.ps1                          # the exe  -> exe\build\Ultimate No Mercy.exe (+ SDL2, FFmpeg DLLs)
    .\tools\release\make_package.ps1         # the package -> scratch\release\UltimateNoMercy-<version>\ (+ .zip)

`make_package.ps1` checks the exe was built from this very mod (`tools\aot\aot.py --check`), copies the files below into a
fresh folder, runs the audit and zips the folder only when the audit is CLEAN. `-NoZip`: the folder only. `-Out <folder>`:
another place.

The audit is `python tools\release\release_audit.py --package <folder>` (the same as for the repository, on the package's
files): no ROM or closed archive, no content pack, nothing extracted in a wrestler or arena folder, no binary that is not the
program, its DLLs, the mods or the UI assets (`PACKAGE_ALLOWED`), and no run of 35+ bytes shared with any of the player's ROMs
(`..\private\roms`, or the linked `roms\`) or with the content packs and commentary pack (`--forbid`), in every byte order,
inside zips and .nrm files too. Its report: `<package>.audit.txt`. A package the audit refuses must not go out.

Also check privacy before anything goes out: no user name, computer name or path outside the project in any file (see the
private folder's notes, "Checks").

## What is in the package

| in the package | from |
|---|---|
| `Ultimate No Mercy.exe`, `SDL2.dll`, `avcodec/avformat/avutil/swresample/swscale-*.dll`, `FFmpeg-*.txt` | `exe\build` (build_exe.ps1) |
| `firstrun\` (`plan.bin`, `mod_binary.holes`, `mod.json`, `mod_syms.bin`, `hdrfill.bin`, `romimport\`): the mod with its game-data tables zero and the plan the first start fills them by; the importers' own data | `tools\firstrun\mkplan.py` from `mod\build\mod_six` (build_mod.ps1); `tools\romimport`, `tools\handlers\handlers` |
| `mods\aki_*.nrm`, `assets\` (fonts with their licences -- `Inter_OFL.txt`, `NotoEmoji_OFL.txt`, `promptfont\LICENSE.txt` --, icons, `recomp.rcss`, `rt_lights.json`, the AkiMods, the commentary recipe) | `exe\WWFNoMercyRecomp\assets` |
| `README.txt` (the player's first page) | `release\README.txt` |
| `roms\README.txt` (where the ROM goes) | `release\roms_README.txt` |
| `ARCADE README.txt` | `mod\README.txt` |
| `COPYING`, `recompcontrollerdb.txt`, `gamecontrollerdb.txt` + `gamecontrollerdb_LICENSE.txt` | `exe\WWFNoMercyRecomp` |
| `SDL2_LICENSE.txt` | SDL2's development files (`third_party\sdl2-src\COPYING.txt`) |
| `stories\` (the samples, `_roster.json`, `_voices.json`, the schema) | `mod\build\stories` (build_mod.ps1 from `mod\stories`) |
| `select_pages.json` | `mod\select_pages.json` |
| `wrestlers\README.txt`, `wrestlers\_example\wrestler.json`, `arenas\README.txt`, `arenas\_example\arena.json`, `texturepacks\README.txt` | `mod\wrestlers`, `mod\arenas`, `mod\texturepacks` |
| `portable.txt` (empty: saves and settings beside the program) | made by the script |

## What is NOT in it, and where a player gets it

| left out | why | how a player gets it (today) |
|---|---|---|
| the ROM | game data | the player's own, in `roms\` (README.txt) |
| `content\content.bin`, `wmport.bin`, `movelib_revenge.bin`, `movelib_wm2000.bin`, `modextra.bin` | made from the other games' ROMs (Revenge, WrestleMania 2000, No Mercy Plus V3, Revenge Redux) | the first start makes them from the player's ROMs in `roms\` (`src\main\arcade_firstrun.cpp`, `src\romimport\ri_build.cpp`); again only when the set of ROMs changes, one is missing, or a new release's extras layout differs (`modextra.bin`, relprep 2026-11-23) |
| (`hd.bin`, `hd_rock.bin`, `native_view.pak`) | the two WWE 2K15 test slots' models and pictures (third-party) | **removed** (no2k15, 2026-11-23): the slots are out of the game; nothing reads these files now |
| `commentary.pak*`, `commentary\*.pak` | WWE '13 audio | optional: the game makes `commentary\wwe13.pak` on the first start that finds the player's WWE '13 (Wii) image in `roms\` (RVZ/WIA, or a plain .iso/.wbfs with the player's own Wii key file; no key ships); what ships is the recipe `assets\commentary\wwe13\` (ours: which disc line makes which clip, the transcripts) -- `tools\commentary\README.md` |
| `wrestlers\<collection>\...` (Revenge, WrestleMania 2000, Imported) | extracted from ROMs | the first start imports them from the player's ROMs, once per ROM (`src\romimport`); never over a folder that is there |
| the player's saves and settings | the player's | made by the game |

## Still open

- Signing the exe (SmartScreen), a Linux / Steam Deck package.
- A clone of the repository does not build from the player's ROMs alone yet (PUBLISHING.md, "Building from a fresh clone").

Settled since (2026-11-23): the content packs and the wrestler folders are made on the player's PC by the first start; the
mod ships with its game-data tables zero (`firstrun\mod_binary.holes`); `six_blob.h` and the converted code are published
(PUBLISHING.md); the fonts' OFL texts are in `assets\`.
