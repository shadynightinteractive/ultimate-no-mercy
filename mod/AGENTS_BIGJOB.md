# The big job (2026-11-01 evening; the user away): every WM 2000 arena, the rest of Revenge's arenas, ten Revenge ports

The user: "Start working on porting all remaining WM 2000 arenas and Revenge arenas. Same strategy, the 'big' high quality
stage is the in-match stage, etc. Each gets its own slot. We need all of them ported. Also start porting in from Revenge:
Piper, Rey Mysterio, Bischoff, Goldberg, Luger, Sting, Steiners, Booker T, Stevie Ray. Match their music from the Revenge mod."

Three agents work at once:
- arenas-rev: Revenge's remaining venues -- Souled Out (1), SuperBrawl (2), Starrcade (5) (Nitro 0, Bash 3, Havoc 4 are done).
- arenas-wm: every WrestleMania 2000 venue but RAW IS WAR (done as RAW IS WAR WM 2000).
- ports4: Roddy Piper, Rey Mysterio, Eric Bischoff, Goldberg, Lex Luger, Sting, Rick Steiner, Scott Steiner, Booker T, Stevie Ray.

## The house rules (all of them -- read the memory rules in mod/NOTES.md's head too)
- Reshape, don't replace: grow the engine's limits; never drop, swap, "free a slot", band-aid or hack. Each arena is its own
  separate arena (its own data block, its own per-venue entries, walls, table block, effects / VS copies -- arena.c's
  structure, NOTES "2026-11-01 Arenas restructured"), never a modification or copy of another venue's.
- Ported wrestlers bring their music (Revenge Redux's THEME LIST, as ports3 did). Menus are native, never overlays.
- Do NOT touch the user's other work: anything under <outside>, SignalRGB, any process not yours. Never kill a
  process you didn't start.
- The machine has ~<spec> GB RAM, often only ~5 GB free. At most ONE emulator / game per agent, and before starting one check
  free physical RAM >= 2.5 GB (PowerShell: (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory). Keep 4.5 GB free on C:.
- No real-game QA runs now (deploy-then-queue rule): write qa\scenarios\*.json for each thing and list them in
  mod/NOTES.md "FOR TONIGHT'S QUEUE". The libretro rig on the original ROMs is fine for research.
- Never call anything "fixed" or "working": "built, in the game, not tested yet".

## Shared files: the integration lock
Research, captures, dry runs: in your own folder (data\...), any time, no lock.
Anything under mod\ (gen\, src\, recomp\, music\, select_pages.json, NOTES.md) and any generation / build: ONLY while you hold
`Development\.integrating` -- create it exclusively (bash: `set -o noclobber; echo <agent> > .integrating` fails if it
exists), wait and retry (every 2-5 minutes) if it's someone else's. Hold it for the whole edit -> generate -> build window,
and also take qa\.busy (exclusive create) around the build itself as the existing scripts do. Leave the tree building
cleanly when you let it go. Before you start, snapshot the files you'll change to Development\backups\pre_<agent>_<n>\.
Other agents' work lands between your turns: re-read a shared file before you edit it; never revert what's not yours.
Version: in your build window bump mod\recomp\mod.toml line 3 and mod\README.txt line 2 to the next 0.38.N (read the
current one first), build with Development\build_mod.ps1 (and wmport.py / mkovr / mkcontent etc. as the pipeline needs:
see data\revport\ports3\gen_all.sh, NOTES "Building on Windows"), check it builds with no errors, copy the nrm to
Development\backups\0.38.N_build.nrm, release both locks, then report to the lead (who deploys with deploy.ps1 -Exe).
Integrate in pieces (an arena or two, a wrestler or two per window) so the others get turns and each piece can be deployed.

## Writing up
Your folder's own .md (how found, what made), and a dated entry in mod/NOTES.md (in your window). Final report to the lead:
what's in which build, what's left, the QA scenarios queued, open doubts.
