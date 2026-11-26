# Importing wrestlers from your own games

Ultimate No Mercy ships no game data. Every wrestler folder in `game\wrestlers\` -- No Mercy's own 64, WCW/nWo
Revenge's men, WrestleMania 2000's, the two imported from mods -- is made on your PC, from your own ROMs, by the
importers in `tools\handlers\`. This page says what to run, in what order, and what each step needs.

Status (2026-11-17): the importers are Python scripts you run by hand. A first-launch import that does all of this from
the game is planned. On the project's own PC the steps below make the tested folders byte for byte (see "Checking").

## 0. What you need

- Your ROMs in `private\roms\` (the project's private folder; it is linked into the source tree as `roms\`):
  - `WWF No Mercy (USA) (Rev 1).z64` -- the game itself, and the stock 64 Superstars
  - `WCW-nWo_Revenge_USA.zip` (a .zip holding the ROM is fine) -- the Revenge men
  - `WWF_WrestleMania_2000.zip` -- the WrestleMania 2000 men
  - `WWF WarZone - Badd Blood (USA) (v1.0).z64` -- Vader (and some WrestleMania 2000 data)
  - `WWF No Mercy (USA) v1.0.z64` + `No Mercy Plus V3.xdelta` -- Goldust (No Mercy Plus V3's parts)
  - optional: WCW/nWo Revenge Redux (version 05/20/2026) -- its release `.zip` as downloaded (only the xdelta inside is
    read, in memory: nothing in it is extracted or run), or its `.xdelta`, or the patched ROM. Known by fingerprint
    (`tools\romimport\roms.py`). It gives every Revenge Superstar its THEME LIST has a song for (the ports, Jim
    Neidhart, 39 of the 52 never-ported men; one theme a folder) his Redux entrance theme -- the song's clip at its own
    speed, as loud as the game's other themes (`mod\gen\reduxmusic.py`); without it they have
    Revenge's own theme. Added later, `tools\romimport\reduxthemes.py` puts the themes into their folders -- only where
    the theme is still the importer's own, never over one the player changed (the user's rules, 2026-11-18).
  - optional: your own music files in `mod\music\` (`<his name>.mp3`): such a file is his theme, before any of the above.
  A missing ROM leaves its wrestlers out; the game still runs.
- The project's toolchain (`toolchain\`: Python 3.10 with numpy, scipy and Pillow; the emulator core in `rig\core\`).
- Run everything from the source folder in Windows PowerShell, after `. .\toolchain\env.ps1`.

## 1. The rig's captures (once per ROM)

Some of what a wrestler needs exists only while his game runs: his model as the game builds it in a match, his select
picture. The rig (the libretro N64 core in `rig\core\`, driven by `tools\handlers\rig\*.py`) runs your ROM headless and
saves those into `data\handlers_rig\<game>\cap\` (set `NM_HANDLERS_RIG` to that folder for each script):

| game | scripts (in order) | writes |
|---|---|---|
| Revenge (`data\handlers_rig\revenge`) | `capture_all.py` (models), `cc_all.py` (Costume Change pictures: photos) | `cap\auto.pkl`, `cap\cc_<code>.png` |
| WrestleMania 2000 (`...\wm2000`) | `wm_capture_all.py` (models), `wm_sel_all.py` (select renders) | `cap\all_index.json` + RAM images, `cap\sel_k<k>_c<c>_*.png`, `cap\sel1_ram.bin` |
| Badd Blood / No Mercy Plus (`...\imports2`) | `bb.py`, `capture.py`, `nm.py` (see `imports2.py`'s top) | `cap\*` |

Known gap: these scripts start from saved states of the game's menus (`cap\h11.st`, `cap\sel1.st`, `cap\cc_hennig.st`
...) that were made by hand in the rig; a new user can't make them yet without the menus' route. The first-launch import
will script them from boot. Until then the captures are the project's own (private, never shipped).

The No Mercy importer needs no captures (it runs the ROM's own code in `rig\harness`'s MIPS emulator).

## 2. The move library and the mods' remaps

- `tools\handlers\work\revenge_library\` -- the Revenge move library (`movelib_revenge.bin`, the game's
  `content\movelib_revenge.bin`), its index `rows_all.json` and `records_damage.json` (move records from Revenge's own data).
  `revlib.py <that folder>` makes them from your Revenge ROM and the mod's pack. Each never-ported man's library moves are
  also kept slot by slot in `tools\handlers\handlers\revenge\library_ids.json` (no game data: IDs only), so his moves come
  out the same whatever the index.
- `tools\handlers\work\nmplus\remap.json`, `work\baddblood\remap.json` -- how No Mercy Plus V3 and Badd Blood changed the
  stock tables: `modcheck.py tables` (see `tools\handlers\IMPORT_CHECKS.md`, step 1).

## 3. Run the importers

One command does it all, in the order the tested folders were made:

    .\tools\handlers\import_all.ps1 -Out <a new folder> [-Ports]

| step | script | makes |
|---|---|---|
| 1 | `revlib.py` (only if `work\revenge_library\rows_all.json` is missing) | the Revenge library index |
| 2 | `nm_import.py --out <out>` | `No Mercy\` (64 + `stock_roster.json`) |
| 3 | `aki_import.py revenge --out <out> --all --rest-only --legacy-ids 0x0340 --records <...>\records_damage.json`, then `--refresh` | `Revenge\` (the 52 never-ported men; numbers frozen in `handlers\revenge\legacy_ids.json`, e.g. Elizabeth 0x038C) |
| 4 | `aki_import.py revenge --out <out> --ports-universal` (with `-Ports`; game 0.84 and later) | `Revenge\` the 18 built-in ports as folders that replace the built-ins |
| 5 | `wm2k_photo.py` (only if `work\wm2000_photos` is empty), `wm2k_import.py --out <out>`, `wm2k_refresh.py <out>` | `WrestleMania 2000\` (56; photos: his first pose on the select) |
| 6 | `imports2.py --out <out>` | `Imported\` (vader, goldust) |

Each step's log: `<out>\logs\<step>.log`. About 25 minutes.

## 4. Put them in the game

Copy each collection folder from `<out>` into `game\wrestlers\` OVER the old one (copy, never mirror: a mirror would
delete your `wrestler.override.json` files). Try first without copying: set `WCW_WRESTLERS_DIR=<out>` for the game.

Your own changes to an imported wrestler go into `wrestler.override.json` beside his `wrestler.json` (see the wrestlers
folder's README.txt, "YOUR CHANGES"): a re-import never touches it.

## Checking

The project compares a fresh import with the tested folders file by file (the private notes' HANDOFF: "re-import
compared"). The checks per handler are in `tools\handlers\IMPORT_CHECKS.md`.
