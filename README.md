# Ultimate No Mercy

An unofficial, fan-made native PC port of **WWF No Mercy** (Nintendo 64) with the **Ultimate No Mercy** arcade mod, and
a modded decompilation of the game in progress.

> **No game data is included, here or in the releases.** You need your own copy of the game: the program is built from,
> and runs on, the ROM you supply. This project is not affiliated with or endorsed by Nintendo, THQ, WWE, AKI Corporation
> or any other rights holder. All trademarks belong to their owners.

## What it is

- **A native PC port.** It is built on [WWF No Mercy: Recompiled](https://github.com/jessetbh/WWFNoMercyRecomp), which
  statically recompiles the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp) and runs it on
  [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) with the [RT64](https://github.com/rt64/rt64) renderer.
- **A decompilation in progress (non-matching).** The game's own code is being converted to readable C++
  (`exe/WWFNoMercyRecomp/src/game/lifted.cpp`), each converted function random-tested against the original; what is not
  converted yet still runs as N64Recomp's translation. The mod's code is converted the same way (`mod_native.cpp`).
- **The Ultimate No Mercy mod.** An arcade around the game (coins, attract mode, the operator menu), Royal Rumble, up to 32
  in the ring, Lumberjack matches, the six-man engine, custom stories, your own wrestler and arena folders, and more
  Superstars and arenas made from your own copies of other AKI games. `mod/README.txt` (the release's
  `ARCADE README.txt`) describes it.
- **Windows for now.** Linux, macOS and Steam Deck are goals, not available yet.

## Playing

1. Download the zip from this repository's **Releases** page and unzip it anywhere you can write to (not Program Files).
   Saves, settings and the log stay in that folder.
2. Put your **WWF No Mercy (USA) (Rev 1)** ROM in the `roms` folder: a `.z64`, `.n64` or `.v64` file, or a `.zip` holding
   one. SHA-1 of the `.z64`: `91CEE3D096F4A76644D8B35B9AEAD6448909ABD1`. The original USA release (Rev 0, "v1.0") is not
   accepted as the main ROM.
3. Optional, also in `roms` -- each adds its own Superstars, arenas, moves or themes:

   | ROM | gives |
   |---|---|
   | WCW/nWo Revenge (USA) | Revenge's wrestlers and its moves; the built-in ports |
   | WWF WrestleMania 2000 (USA) (a `.zip` is fine) | WrestleMania 2000's wrestlers, moves and arenas |
   | WWF No Mercy (USA) v1.0 + `No Mercy Plus V3.xdelta` (the patch) | No Mercy Plus V3's Superstars, themes and videos |
   | the WCW/nWo Revenge Redux patch (its release `.zip`, or the `.xdelta`) with Revenge | Redux's entrance themes |
   | WWF WarZone - Badd Blood (USA) (v1.0), with WrestleMania 2000 | Vader |
   | WWE '13 (USA) (Wii) as an `.rvz` disc image | Jim Ross and Jerry Lawler's commentary |

   ROMs are recognised by their contents (SHA-1), not by their file names.
4. Start **Ultimate No Mercy.exe**. The **first start builds everything** from your ROMs -- the mod's game-data tables,
   the content packs and the wrestler folders -- with a status window (about a minute or two with every ROM; the WWE '13
   commentary is made in the background afterwards). Nothing is downloaded and nothing leaves your PC. Later starts only
   build again when you add a new ROM (or put an empty `content\reimport.txt` in to ask for it).
5. 5 / 6 insert a coin for player 1 / 2, 1 / 2 start; F2 is the operator menu (settings, controls, quit); F11 or
   Alt+Enter toggles fullscreen. `README.txt` in the download has the details.

Windows may warn that the program is unsigned (SmartScreen): "More info", then "Run anyway".

## Updating

New versions come out on the **Releases** page (click **Watch > Custom > Releases** above to be told about each one).
To update, download the new zip and unzip it over your old folder: your saves, settings, controls and wrestler folders
stay; the next start rebuilds what it needs. **Automatic updates are planned** for a future version.

## Reporting a problem

Open an issue on the **Issues** page and choose **Bug report** (or **Feature request** for ideas). Say which version you
play and name the ROMs in your `roms` folder -- **never upload ROMs or game files**. See `CONTRIBUTING.md`.

## Building from source

The source builds on Windows with the project's toolchain (Python 3.10, llvm-mingw, zig, CMake, Ninja; `toolchain\env.ps1`)
and PowerShell:

    .\build_mod.ps1                          # the mod  -> mod\build\mod_six\nomercy_arcade.nrm
    .\build_exe.ps1                          # the exe  -> exe\build\Ultimate No Mercy.exe
    .\tools\release\make_package.ps1         # the release folder and zip (docs\RELEASE.md)

**A fresh clone does not build on its own yet.** Besides the toolchain, the build reads files that are made from the ROMs
and so are never published: N64Recomp's translation of your ROM (`RecompiledFuncs`), the RSP audio code, the six-man
ROM, and the mod's game-data headers. `PUBLISHING.md` ("Building from a fresh clone") lists each one and what makes it;
making a clone build from your ROMs alone is open work. The developer guide is `docs/DEVELOPING.md`.

## Repository layout

| Folder | What |
|---|---|
| `exe/` | the PC program: `WWFNoMercyRecomp/` (the port's source with this project's changes, the converted C++, the first-start builder `src/main/arcade_firstrun.cpp`, the ROM importer `src/romimport/`) |
| `mod/` | the Ultimate No Mercy mod: `src/` (C, runs inside the game), `gen/` (generators), `stories/`, the folder templates |
| `six-engine/` | the six-man engine's work |
| `tools/` | the converter (`lift/`), the importers and their per-wrestler fixes (`handlers/`, `romimport/`), the first-start plan maker (`firstrun/`), the release tools (`release/`) |
| `qa/` | the in-game test scenarios and tools (`qa.ps1`) |
| `libs/` | the AkiMods and N64Recomp (their own licences) |
| `docs/` | the developer guide, importing, releasing, match slots, the theme script |
| `release/` | the READMEs that go into the release download |

## Licence

**GNU General Public License version 3** -- `LICENSE` (SPDX: `GPL-3.0-only`), like the port it is built on.
Third-party code keeps its own licence, in its folder: FFmpeg (LGPL 2.1+), SDL2 (zlib), libogg / libvorbis (BSD), zstd (BSD),
CORE-MATH and musl (MIT), vgmstream (ISC-style), Pillow's median cut (MIT-CMU), the fonts (SIL OFL 1.1), the controller
database, m2c (GPL-3.0) and the other libraries under `exe/WWFNoMercyRecomp/lib/`, `exe/WWFNoMercyRecomp/third_party/`,
`libs/` and `tools/lift/third_party/`. The release download carries these licence texts beside the program.

WWF No Mercy and the other games are the property of their rights holders; nothing from them is in this repository or
its releases.

## Credits

- [WWF No Mercy: Recompiled](https://github.com/jessetbh/WWFNoMercyRecomp) and the AKI recompilation family by jessetbh,
  which this port is built on, and the [AkiMods](libs/AkiMods/README.md). Their own credits:
  `exe/WWFNoMercyRecomp/README.md`.
- [N64Recomp](https://github.com/N64Recomp/N64Recomp), [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
  and [RT64](https://github.com/rt64/rt64) (Wiseguy, DarioSamo and contributors).
- The N64 SDK audio tools as reconstructed by the [Super Mario 64 decompilation](https://github.com/n64decomp/sm64) (CC0),
  which the ROM importer's VADPCM port follows: `CREDITS.md`.
- [m2c](https://github.com/matt-kempster/m2c), used by the converter's tools.
- Thanks to the makers of the mods the game can read: **No Mercy Plus V3** and **WCW/nWo Revenge Redux** by
  Retro Randy Price, and the **WWF WarZone - Badd Blood (Non Homebrew ID)** patch by Wallywood84. This project does
  **not** include anything from these mods -- no files, graphics, sound or data of theirs. Their content appears only
  when a player gets the mods from their authors and puts their own copies in `roms\`; the game reads them on the
  player's PC. Please support the original mods. More mod compatibility is planned: support for other mods and
  source games will be added in future releases.

Made with AI Assistance.
