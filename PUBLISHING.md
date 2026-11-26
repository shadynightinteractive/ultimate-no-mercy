# Publishing this repository

This tree is what goes on GitHub: everything the project made, with the game's full folder structure, and nothing taken
from a game.

## Decided (the user, 2026-11-18 and 2026-11-23)

- **Where:** the public repository `shadynightinteractive/ultimate-no-mercy` on GitHub, plus a GitHub Release whose zip
  is the player's download.
- **What:** the source, INCLUDING the converted C++: `exe/WWFNoMercyRecomp/src/game/lifted.cpp` (with
  `lifted_changed.inc`, which it includes: the game's own N64 code converted to C++ by `tools/lift/lift.py` and
  random-tested against the original), `src/game/mod_native.cpp` (the mod's code converted, including the mod's modified
  copies of game functions) and `six_blob.h` / `six_rompatch.h` (the six-man engine's own MIPS code and patch words, ours).
  `tools/release/release_audit.py` decides what may go out (below).
- **Licence:** GNU GPL version 3 (`LICENSE`, the FSF's text unchanged; the same text as `exe/WWFNoMercyRecomp/COPYING`).
  SPDX: `GPL-3.0-only`. The port this project is built on (WWF No Mercy: Recompiled) is GPLv3 and does not say "or any
  later version", so neither does this project. Vendored libraries keep their own licences, beside them.
- **No ROMs and no game data**, in the repository or in the Release zip. The player supplies their own ROMs; the game's
  first start builds everything else from them on the player's PC.
- **A fresh history.** The public repository starts from ONE commit made from this tree's files (`git archive` of the
  branch into a new folder, `git init`, one commit). The private development history is never pushed: earlier commits
  hold material that was later taken out (commentary transcripts, the game's move names) and must not be published.
- **The Release zip** is made by `tools/release/make_package.ps1` (docs/RELEASE.md), which runs the package audit and
  zips only a CLEAN package. A SHA-256 list of the zip goes with it.

Decided (2026-11-19, relcheck): the VADPCM port (`exe/WWFNoMercyRecomp/src/romimport/ri_vadpcm.cpp` / `.h`: the N64 SDK
audio tools' algorithms as the sm64 decompilation reconstructs them, CC0 1.0) goes out with the rest, credited in
`CREDITS.md` and in the files' headers. CC0 covers only the decompilers' own rights: the tools are a reconstruction of
Nintendo's / SGI's programs (CREDITS.md's notes). The linear solve the tools take from *Numerical Recipes in C* (`lud` /
`lubksb`) is not in the port: it was rewritten from the mathematics as our own `lu_decompose` / `lu_solve` (2026-11-19,
relclean, the user's decision), every encoded theme still the same bytes.

## What is in, what is out

IN (tracked): all code, tools, importers and their hand-made per-wrestler fix data (`tools/handlers/handlers/**`), the
editor checkers, tests and scenarios, docs, fonts with their licences, the folder structure of the game (`mod/wrestlers`,
`mod/arenas`, `mod/texturepacks` with their READMEs and `_example` folders), `families.json` / `select_pages.json` /
`aliases.json` wherever we author them, arena definitions, the commentary recipe (`exe/WWFNoMercyRecomp/assets/commentary`:
our ids and tags only -- NOT the transcripts, the user's decision 2026-11-18; the game's move names are made from the
player's ROM), the stories, names.json / fields.json, the converted C++ (above).

OUT (git-ignored; they live in the developer's private folder beside this tree and are linked in,
`tools/layout/private_links.txt`): the ROMs; everything extracted or captured from games (`data/`, the rig's saved
states); everything our tools make from them -- RecompiledFuncs, the mods' Stage 1 translation (`src/game/mod_aot`), the
RSP audio code, the mod's game-data headers (`mod/src/*_files.h`, photos, parts, ...), wrestler folders, content packs,
commentary audio; the third-party toolchain; build outputs, caches and test runs.

A player gets the OUT part on their own PC from their own ROMs: the game's first start makes the mod's game-data tables
from the No Mercy ROM, and the content packs and the wrestler folders of the other games from the other ROMs in `roms\`
(`exe/WWFNoMercyRecomp/src/main/arcade_firstrun.cpp`, `src/romimport/`); the commentary from a WWE '13 disc image, if
there is one (`tools/commentary/README.md`).

## Building from a fresh clone: what it needs besides this tree

Measured 2026-11-17. A clone of this repository does NOT build alone yet. Besides the toolchain (`toolchain\*`) and
RecompiledFuncs (N64Recomp's translation of the player's ROM), the build reads, from the private folder:

| needed | made from | by |
|---|---|---|
| `exe/WWFNoMercyRecomp/rsp/nomercy_audio.cpp` | the ROM's audio microcode | RSPRecomp (`rsp/nomercy_audio.toml`) |
| `six-engine/NoMercySix/nomercy.z64` (the six-man ROM) | the player's stock ROM | six-engine/work's build (the game also makes it at start) |
| the mod's game-data headers (`mod/src/*_files.h`, photos, parts, content index, ...) | the extracted files in `data/` | `mod/gen` generators |
| `exe/WWFNoMercyRecomp/src/game/mod_aot` | the built mod + the six-man ROM | `build_exe.ps1` (`tools/aot/aot.py`) -- made every build |
| SDL2's development files | SDL's release | CMake downloads them when `third_party/sdl2-src` is not there |
| the FFmpeg build (`third_party/ffmpeg`) | `third_party/ffmpeg-src` (in the tree) | `tools/ffmpeg_build/build_ffmpeg.ps1` |

Making a clone build from the player's ROMs alone is open work.

## Before every push

    python tools/release/release_audit.py            # rules + ROM-byte scan (uses ../private/roms when present)

It must end `RESULT: CLEAN`. It checks what git would publish (`git ls-files -co --exclude-standard`): no ROM or disc
image, no extracted type inside a wrestler or arena folder, no linked private path, no unreviewed binary, and no run of
35+ bytes shared with a ROM -- in the files as they are and in every text file's numeric arrays turned back into bytes.
In an exported copy with no `../private` beside it, give the ROMs with `--rom <file>` (one each).

Also run the privacy scan: nothing may name the developer's PC -- no user or computer name, no path outside the project
(`C:\Users\...`, the folder the project sits in), no hardware IDs (controller vendor/product IDs, device paths), no machine
specs, no e-mail address but placeholders -- in every file (text and binary strings), in the docs, logs and scenarios, and
in the commit's metadata. The developer's private notes say how ("Checks").

**Share this tree only through git** (`git push`, `git archive`). Zipping or copying the folder itself would follow the
links into the private folder.
