# Import checks: what to run for each import

These steps come from the modcheck agent's hand-back of 2026-11-13. final082 moved its tools here, so the paths now point at
`tools\handlers` and the outputs go to `tools\handlers\work`. Run everything from the `Development` folder with Python 3,
numpy and PIL installed.

## 1. Compare the mod's tables with the stock game's (once per mod, before its handler runs)

The handler refuses to run without this step.

    python tools\handlers\modcheck.py tables --family nomercy --base "roms\WWF No Mercy (USA) v1.0.z64"
           --patch "roms\No Mercy Plus V3.xdelta" --base-ram data\gen_data\van_sel_ram.bin
           --mod-ram data\gen_data\nm_pick_ram.bin --out tools\handlers\work\nmplus          (about 3 min)
    python tools\handlers\modcheck.py tables --family wm2000 --base roms\WWF_WrestleMania_2000.zip
           --patch "roms\WarZone Badd Blood (Non Homebrew ID).xdelta" --out tools\handlers\work\baddblood

- `--mod <patched ROM>` can replace `--patch`, or be given beside it.
- `"<archive>.zip::<member>"` reads a patch out of an archive.
- The RAM images can be taken at any moment after the game has booted, because the parts tables are in memory from then
  on. The rig's saved states work.

This step writes `work\<mod>\remap.json`, which the handlers load (`aki\modtables.py`, `Remap`), and `tables.txt`, the
same information for reading.

The handlers look for the remap at `tools\handlers\work\nmplus\remap.json` and `tools\handlers\work\baddblood\remap.json`.
To use another file, set `NMPLUS_REMAP` or `BADDBLOOD_REMAP`.

## 2. Run the import

Run the handler as before (`tools\handlers\imports2.py --out ...`). It loads the remap. It stops with `ModTableChanged` if
it is about to use a value with its stock meaning that the mod changed. The message names the table, the value, both
meanings, and the mod's number for the stock meaning.

## 3. Check every folder the import wrote, by sight

a. Take the source game's pictures in the rig:
   - No Mercy mods: `nm_sel.py <name> <4 appearances>`
   - Revenge: `rv_shots.py [<string id>]`
   - Badd Blood / WM2000 select: `tools\handlers\rig\bb.py <state> ...`

   `nm_sel.py` and `rv_shots.py` are still in modcheck's own rig folder: they drive imports2's saved states and write
   next to themselves. The rig scripts in `tools\handlers\rig` write to the rig folder instead: `NM_HANDLERS_RIG`, or
   `data\handlers_rig` if that is not set.

b. Compare the folders with those pictures:

       python tools\handlers\check_import.py --dir <wrestlers folder> --who <folder>=<rig prefix>
              [--family cross --view 0.62] --qa

   This writes the QA scenario, runs it, and compares the result with the source. Its QA runner is modcheck's
   `run_mine.ps1`. Set `NM_IMPORT_CHECK_DIR` to the folder that holds that runner and its `scenarios\` folder. If it is
   not set, `tools\handlers\work` is used, which has no runner, so `--qa` only works with the variable set. The
   pictures are compared from the project's `qa\runs`.

c. For a handler that builds looks from claims (the customs handlers, e.g. No Mercy Plus), also run the intent check:
   `visual.check_intent(name, handler_prefix, intent_prefix, out)` in `tools\handlers\visual.py`. It compares No Mercy
   Plus's render of the looks the handler built against its render of what the handler meant (its stock claims read
   again through the remap), from the same machine state, frame for frame. Take both sets of pictures with `nm_sel.py`.
   This check catches a wrong choice of looks that step b cannot see: step b only shows that the transfer was faithful.
   On Goldust's first import it flagged the wrong shirt in the chest band. No script calls it yet; modcheck's own
   `scripts\visual_all.py` shows how.

## 4. Check a texture pack (one shipped with a mod, or any)

a. Do a QA run with a `_dump` folder in `WCW_TEXTURE_PACKS_DIR`: the game saves every picture it draws, named by its
   hash. Play the screens the pack is for.
b. Check which of the pack's pictures were seen:

       python tools\handlers\texcheck.py check --packs <texturepacks folder> --seen <that _dump> --out x.txt

c. Run the same scenario without the packs and with them, then compare the two runs' pictures:

       python tools\handlers\texcheck.py renders --before <run> --after <run> --out <dir>

## Where the guards are

- `customs\nmplus.py`, `goldust_apps`: every "nothing else on" value is a stock claim that gets checked.
- `customs\nmplus.py`, `tame()`: every value kept from No Mercy Plus is translated to the stock meaning.
- `baddblood\game.py`: every animation file and move-name file that Badd Blood changed is read from Badd Blood.
- `revenge\game.py`: the ROM must be the clean USA one, and the RAM image must be its own. A modded Revenge stops the
  import.

A table the remap has not checked, because there was no RAM image for it, also stops the import: unchecked does not
mean unchanged.
