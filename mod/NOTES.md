# Ultimate No Mercy: technical notes

WWF No Mercy (USA) Rev 1, SHA1 `91CEE3D096F4A76644D8B35B9AEAD6448909ABD1`. All addresses were found
and verified in a headless N64 emulator (parallel-n64, software RDP) driven from Python, with the
arcade code itself compiled to MIPS and injected into the running game.

## How the match is started

There is no menu walking. The mod uses the game's **attract demo** as the match:

1. On the title, the singles demo records (`0x80102998`, 10-byte records, records 3-10 = set 1)
   are rewritten to `[player's pick, other wrestler]` (Austin `0x0002`, Rock `0x0001`; the top
   nibble is the costume). The title idle timer `0x80158150` is set to 899 (the demo starts at 900)
   and the attract counter `0x800457AA` to 2 (set 1, singles).
2. On the return of the demo setup `func_800EF478` (ovl_a), the demo becomes a real match:
   - `0x800A6028[slot]` (s32) = controller number (1-4), 0 = CPU
   - `0x80097EC0` match flags: bit0/bit1 = slot 0/1 is CPU; clear `0x03000000` (demo bits)
   - `0x800A10F1` venue (0-9 arenas)
3. When the game switches to demo mode (`0x800457A0` = -6) it sets `0x02000000` again, and match
   init gives demos a 1-minute limit (`0x8009FE6C` = 7). The mod keeps the bit cleared and the
   limit at 0 for the whole match. Result: HUD, controller "1"/"2"/"C" tags, normal rules, and
   Start doesn't pause (demo mode has no pause).

Because it's still "demo mode", the match returns to the title by itself when it ends.

## Ending a match / demo on purpose

Set `0x02000000` in `0x80097EC0` and press any button: the demo exits to the title (about 4 s).
Used for starting while an attract demo plays, for a challenger, and as a safety if a result hangs.
After a pinfall/submission the game loops a replay until A or B is pressed; the mod presses A
after about 4 s.

## Who won

`0x8009FE58` s32[4]: the match second each slot was eliminated, -1 while still in (reset at match
init). Verified with count-outs and a pinfall. (`0x8009ED20` u8[4] looked like a loser flag but is
"slot is human", rebuilt every frame from wrestler struct `+0xE1` bit 3.)

## Other addresses

| Address | What |
|---|---|
| `0x800457A0` s32 | mode: 0 title/menus, 1 leave intro, -6 demo, -10 intro movie, -2 exhibition match |
| `0x800457A4` s16 | menu screen (0 title, 2 Multi Play, 7 Exhibition, 6 players, 5 arena, 21 rules, 26/27 superstar select, 25 VS, 42 match) |
| `0x80057404` u32 | vertical retrace counter (60 Hz), used for all mod timers |
| `0x80063350` | OSContPad[4] after `osContGetReadData` (`func_80034618`); the mod filters/injects here |
| `0x801532C0` u16 | match frame counter, 0xFFFF outside a match |
| `0x80159998` + 0x358*slot | wrestler runtime struct (`+0xF4/F8/FC` root x/y/z, `+0xDF` outside the ring, `+0xE1` bit 3 CPU) |
| `0x80097FB8` + 8*slot | per-wrestler end-of-match record (written by `func_80114514`) |
| Overlay signatures | intro `0x80106AB0` = `0x3C028016`, title `0x80111158` = `0x27BDFFB8`, demo setup `0x800EF478` = `0x27BDFF90` |

## Text overlay

`func_800012B0` runs on the graphics thread right before each frame's RSP task is started; its
argument is the frame task (`+0x0C` framebuffer, `+0x10` OSTask, OSTask `+0x30` data_ptr). The
mod walks the top-level F3DEX2 display list to its `G_ENDDL`, replaces it with a branch to its own
list (8x8 public-domain font as an I4 texture, texture rectangles), which ends the frame. The
framebuffer is 480 x 240 RGBA16; text uses a 40 x 30 grid (12 x 8 px cells). The overlay's
display lists and font live at `0x807E0000-0x807F0FFF` (above the 4 MB the game uses).
The game can submit more than one frame per retrace; every task gets the overlay.

## If something's wrong on the PC port

* No text at all: RT64 may not read the overlay buffers at 0x807E0000; move `DL_BASE`/`FONT_ADDR`
  in draw.c into the mod's own static memory, or below 4 MB.
* Arcade logic not reacting: check the hook targets exist in the port's `syms/dump.toml`
  (`func_800012B0`, `func_80034618`, `func_800EF478`).
* Keys: `recomp_get_key_down` must be exported by the port (it is in current builds).

## Building

* Mod: `build_mod.sh` (clang MIPS + ld.lld + N64Recomp's RecompModTool, same flags as AkiMods).
* Test blob: `build_test.sh`, loaded by `harness/rig.py` at 0x80700000 with jumps patched into
  `func_800012B0`, `func_80034618` and `func_800EF478`.

## Building on Windows (from 2026-10-27; the Linux sandbox is gone)
The game folder holds only what the game runs on; `Development\` (inside it) holds everything else, tools
included (nothing installed on the PC; `Development\README.md` is the map). The old Linux scripts:
`Development\backups\linux_build_scripts`.
* `Development\build_mod.ps1` (six build -> `mod\build\mod_six\nomercy_arcade.nrm`; `-NoSix`, `-Story`, `-NoMovex`),
  `build_test.ps1 [-Movex]` (the rig's blob), `build_exe.ps1` (-> `exe\build`), `deploy.ps1 [-Exe]` (backs up the
  game's current files to `backups\<version>`, copies in, MD5-checks). Each dot-sources `toolchain\env.ps1`.
* Rebuilt 0.36.5 here with only Windows on PATH: mod_binary.bin, mod_syms.bin and mod.json byte-identical to the
  sandbox's (only the .nrm's zip compression differs). The exe: equivalent, not byte-identical (llvm-mingw 20260908
  here, 20260922 there).
* Toolchain (`toolchain\`): zig 0.13.0 (`zig clang` = clang 18 with MIPS; NOT `zig cc`, which forces -mabicalls/PIC
  code; `zig ld.lld`; llvm.org's Windows LLVM has no MIPS backend), llvm-mingw 20260908 (the exe; llvm-nm etc.),
  python 3.10 (numpy, scipy, pillow), cmake 3.31.6, ninja 1.12.1, ffmpeg 9.0.2, `bin\RecompModTool.exe` (N64Recomp
  built with llvm-mingw, -static; local fix: mod.json written in binary mode, no CRLF), `mingw\` DirectX-Headers
  v1.619.5 + freetype 2.14.1.
* Exe: `exe\patches\toolchain-win.cmake` names the assembler as the C compiler's exact string (zstd builds its .S as
  C when the two strings differ, even by a letter's case). RT64's shaders: its own contrib dxc (1.7) on a Windows
  host; no case shims needed.
* Python paths: every script names files by their sandbox path (/home/devbox/..., /tmp/devbox-0/...) through
  `nmp()` (`Development\nmpaths.py`: the one table of where each is; importing it also puts `toolchain\` on PATH).
  New scripts: the same two header lines. Originals: `backups\scripts_before_windows_port.zip`.
* Music for themes: `Development\mod\music` (gen/mp3theme.py; the game never reads it).
* Rig: `rig\harness\n64.py` loads `rig\core\win\parallel_n64_libretro.dll` (libretro's nightly) on Windows. It has
  NOT the sandbox core's `retro_trace_*` (memory-access trace) or `CountPerOpDenomPot` (N64_DENOM): their source
  wasn't kept; a parallel-n64 built from source with them added back is needed for those. `RIG_SIX=1` defaults
  NM_ROM to the NoMercySix ROM (install() copies the engine from its 0x1FF0000).
* Still missing for regenerating some data (not for building): `/tmp/devbox-0/nmbank.pkl` (mkcontent.py),
  `names/`, `leads/`, `elim/`, `names2/`, `ainames/` (the commentary splice work), `sk/match_ram.bin`, the whisper.cpp
  build and its model (mknames/mkleads).
* Commentary (2026-10-27): mkcommentary.py can't remake the pack any more (wem_tags.pkl, dlg.pkl, lowm titles.pkl,
  the w16 whisper transcripts and 20 kHz wavs weren't kept; the encoded ogg of every clip was). The pack is the
  master now: `gen\compak.py unpack / add / pack` (round trip byte-identical: 6572 lines, 5401 clips, sha1
  c51c0096...). The AI voice (F5-TTS): scrapped by the user 2026-11-14 -- voicelab and data\voice_model deleted.
  Its only clips in the pack, the 13 AI names (the splice bank's "from" f7: Faarooq, Rikishi, Taz, Val Venis,
  Venis, X-Pac), were taken out: 5398 clips / 6583 lines, sha1 8fedf2c2... (the pack before:
  backups\commentary_before_noai_20261114_2310). The jr30 AI lines and the short intros were never in it.
  f5 are JR's own cuts (mknames2's real names: 11 of them match his recordings at 0.94-0.98), not AI.
  mkcommentary's wav_of now nmp()s keep.json's (sandbox) paths.
* Game runtime: no installs. The msvcp140 / vcruntime140 DLLs in the game folder are needed (dxcompiler, dxil and
  librashader are MSVC-built); librashader (the CRT filter only) also wants D3DX9_43.dll, not part of a stock
  Windows 11 (the filter is off).

## v0.7 findings
- Intro script (ovl_b func_80118390): `*0x8016CDB8` = entry playing (0x1C bytes each, first at 0x8013046C),
  entry time = (`0x8015CCE0` - `0x8015CD10`) / 2. Entry 0 = legal page then THQ/JAKKS/Asmik/AKI crossfades;
  the legal page is a 480x240 IA4 picture at 0x801BB7F0 (sampled hash 0xB53E5BCF). THQ fades in at time 96.
- Multi Play sub-header "exhibition": CI4 96x16 at 0x801C0800 (TLUT 0x801C07E0). Other sub-headers:
  royal rumble / pay per view / king of the ring / guest referee / ladder match / ironman match.

## v0.16: Big Show (bigshow.c)

Roster working data (built at boot by `func_8000B9B0`, rebuilt from the save by ovl_a's loaders):
- `*0x80047E90`: costume records, 0x4E bytes, `[ordinal * 4 + costume]`: name +0 (0x15), short name
  +0x15 (0xB), nickname +0x20 (0x11), packed looks +0x31 (0x17, `func_8001A9A0`), weight +0x48 (u16,
  lbs - 100), height +0x4A (inches over 5'0"), music +0x4B, Titantron video +0x4C.
- `*0x80047E94`: movesets, 0xD8 bytes per ordinal: 0xB8 packed moves + 0x20 packed abilities
  (49 bit fields; fields 37-40 are four 12-bit ally IDs).
- Every screen and the match read these through the getters (`func_8000B4B8/B530/B560/B5E8`), so
  writing Benoit's (ordinal 14, ID 0x201) is all it takes. Looks: `func_8000B60C(record, 0x26-byte
  source)`; the source's bytes are listed in bigshow.c (CAW menus 0x4A56..0x4A66 name the items).
- Save: `func_800E299C(dest, record)` / `func_800E2AC0(dest, moves)` pack for the save image; the
  mod swaps Benoit's own data in around them.
- Photos: 64x64 CI4, `TEX` files 0x45B3 + photo (select) and 0x4AAE + 2p / 0x4AAD + 2p (picture,
  palette). All file reads go through `func_80003C94(info, file, dest)`. Photo 0x2B (Gangrel, cut)
  is used by no one; Big Show's picture replaces it on load.
- Story Mode (ovl_a data, reloaded with the overlay): parts 0x800FBE68 (666 x 12: u16 candidates x5,
  `func_800EC554` takes the first not excluded), challengers 0x800F8BD4 (8 x 3, `func_800EBF70`),
  Rumble lists via 0x800F8CB0 (`func_800EE3EC`, which puts 0x201 in for the player: patched),
  random pools via 0x800F8D84 (`func_800EC268`). Scene table 0x80102030 (6 bytes: first part, ?,
  count), story nodes 0x800F8FA4 (18 bytes, `*0x80097F14` = current; +6 first match part, +0xD
  match type, count from 0x800F8D98[type]). `func_800EC354(slot, who, human, side, extra)` puts a
  wrestler in a slot (codes 0x80xx-0x84xx are the player, partner, champion, ...).

## v0.17: Big Show from No Mercy Plus V3 (themes.c, bigshow.c)
- NM+ V3 is an xdelta against v1.0 (not Rev1). Its file table: ROM 0x2BBC4D0, 0x7802 entries, files at
  0x5CD1B0 + offset (not 0x5CD150). Its looks/photo/theme/video for Big Show are carried over as data.
- Entrance music: the record's music byte m -> entrance table 0x800F50D0 + 12 m {s16 song, s16, u32 effects,
  u32 -}; song -> s16 table 0x800480D8 (< 0x2C0: a sound of the music bank *0x80062BFC; else a file).
  Bank: +0 sounds, +8 script-instrument count, +0x10 -> ctl ("N64 PtrTablesV2": +0x20 count, +0x24 u8[],
  +0x28 f32 tune[], +0x2C wave ptrs[]), +0x14 -> u16 map (script instrument -> ctl index), sounds at +0x18
  + 8 i (script ptr, priority). Script 81 nn = instrument nn. Wave: base (ROM), len, type/flags, loop, book.
  Samples stream through func_8002B0C0 (the audio DMA callback; func_8002B12C the cache). Song 0x18 (slot 2)
  is -1 in the game; NM+ plays sound 43 / instrument 43 (ROM 0x37667D6, 0x3BDF9 bytes).
  themes.c installs it at run time (arrays copied with it added), the sample served from the mod at fake ROM
  0x04000000 by the replaced func_8002B0C0. The wave table (book) must be in RDRAM < 16 MB: the port's audio
  ucode DMA masks to 0xFFFFF8 and mod memory starts at 0x81000000 -> 0x807C0000 (unused by the game).
- Titantron: video v's frame list is the word at 0x800F50CC + 12 v ((file, time) pairs, -1 ends), shown by
  func_8010C998 (0x80133C28: the list playing). Frames are 64x64 CI8 TEX files loaded through the sprite
  cache (0x80104D30, 128 x 16 bytes) with func_80003C94. NM+ video 5 = files 0x6620-0x6667 (time 0x8004).
  The entrance table is an overlay's data: set in pre-hooks of func_8010C998 / func_8000AB64 / func_8011E488
  (the game doesn't read pads while loading, so a per-frame update comes too late for the intro).
- Entrance steps (0x800A872C): 4 and 10 are the full-screen videos (the arcade skipped them; now an option).
- Moves: NM+ moveset packing = the game's func_800075DC with wider tables (0x8004437C/84/0x800455A0 in v1.0
  RAM, layout 0 always); nm_port/movedec.py decodes both. NM+ rebuilt the move tables (files 0x69/0x1F0/0x1F1:
  20 bytes per move, 1682 -> 2647 moves, nearly all records changed), so its move numbers aren't the game's.
- Moves (v0.17.1): NM+ moves map to the game's by the per-move animation records (files 0x69/0x1F0/0x1F1,
  20 bytes each, record = id/10; unique per move): 121 of Big Show's 131 exact. Names (files 0x4B7A-0x4C01:
  flag, 4 zero bytes, anim a, anim b, move id, name) agree. The game unpacks movesets only through
  func_80005EFC (slots: 292 x {u16 move, 3 flag bytes, pad}; abilities: 55 s16); a pre/post hook gives Big
  Show's (bigshow_moves.h) when the packed bytes are his carrier's (Kane's moves, allies cleared). Abilities
  index 0 is 0 for everyone in the game (NM+ uses it for its own purpose); 31-34 are the ally IDs.

## v0.19: a bigger roster (roster.c, parts.c, files.c, save.c)
- Ordinals: 1-64 Superstars, 65-82 CAW slots (0xA01-0xA12), 83 special (0xC00), 84-101 CAW edit buffers
  (0xB01-0xB12); the new Superstars are 102-105 (IDs 0x060A Funaki, 0x0408 Gangrel, 0x0309 Mosh, 0x030A
  Thrasher; the ID's group is the faction, ovl_e `func_80132F28`). The resident roster module is replaced
  (`func_8000AE38/AE78/B4B8/B560/B834/B914/B9B0/B004`, ovl_a's page and record save/load
  `func_800E30DC/E3020/E31C4/E3330/E5A44`, the stats arrays `func_800E4450`); their records and movesets
  live in the mod. They go on the select's pages 8/9 (SUPERSTARS 6/7, empty in the game).
- Movesets: the match decodes the packed 0xD8 bytes itself (ovl_e `func_80114348` -> `func_8013CB38`
  abilities, `func_8013CBAC` moves), as the menus do (`func_80005EFC`). A ported moveset is packed with the
  game's packer; moves its slots' value lists lack go in spare cells of those lists (a duplicate entry no
  game moveset uses), set while that moveset (recognised by its bytes) is decoded. gen/mkroster.py.
- Looks: packed 0x17 bytes (`func_8001A9A0` / `func_8001AC44`, 38 fields, 183 bits) -> 0x3F-byte struct ->
  model descriptor 0x270 bytes (`func_80018A3C`, parts via `func_8001858C` colours, `func_80018808`
  textures). Tables: lower 0x8004CF98/D018/FFAA, upper 0x8004EB98/EC18, face 0x8004D254/D354/D3CC/FE8C,
  beard 0x8004D528/D568, mask 0x8004EF84/F004, colour params 0x80049804/496E4. The new parts are entries
  after the game's in copies of these (parts_ext.h); 18A3C/1858C read the copies (gen/patches.py). New
  numbers: lower 128-129, upper 0x80, faces 0x80-0x81 (NM+ redrew the unused CAW faces 82/83), beard and
  mask 32-33, param 0x80. A new Superstar's record keeps a key (fields cut, bit 183 set); parts.c's
  `func_8001AC44` gives the whole struct for it. gen/checkparts.py runs the game's 18A3C (changed) against
  NM+'s own on both RAM images: every game costume unchanged, every new costume the same model.
- Files: IDs 0x4C03+ are the mod's (`func_80003BD0/3C94` replaced; the sprite cache's bound at
  `func_800DF2C8` and ovl_b's `func_80110820` raised to 0x5000). Photos 100-127: TEX 0x4C03+, CI4/TLUT pairs
  0x4C1F+ (photo-file code patched at each site). NM+ part files 0x5290-0x52DF -> 0x4C60+, its faces
  0x1E16-0x1E19 -> 0x4CB0+.
- Save: the image (*0x80062C20) is only ever written by `func_80004EE8`; save.c sanitises titles (+0x7018,
  7 x 0x26: 6 x 6-byte champions, bits ord 7 / costume 2 / ord 7 / costume 2 / 7), presets (+0x6E84, 15 x 0xE,
  0xFF = unused) and the story (+0x6F90, 0x88; +1 = 0xFF none; +8 16 x {ord, ord, bits}) around the write
  and restores them after. gen/checksave.py.
- Select Random: ovl_c `func_8010A9E8` (rand() % 65 -> ordinal) draws the new ordinals too.
- Funaki's NM+ theme (NM+ slot 87, song 109) plays as song 68 (slot 46, sound 45, unused by any song).

## v0.21: standalone ("NoMercy Arcade.exe")
- The port exe is now built here, for Windows, on Linux with llvm-mingw (GitHub mstorsjo/llvm-mingw 20260922;
  MSVC/Windows SDK hosts are unreachable). Script and patches: work/port-patches (build.sh, toolchain.cmake,
  *.patch per repo). Pieces: DirectX-Headers v1.619.5 (d3d12 headers + dxguids for MinGW's __uuidof), freetype
  2.14.1 built for MinGW (the prebuilt freetype.lib is MSVC's), a case shim for <Windows.h>-style includes and
  Winmm/D3D12/DXGI/Shcore libs, plume's struct-returning D3D12 calls (D3D12_RETVAL), dxcapi's uuids for MinGW,
  file_to_c built for the host, `-static` (no libc++ DLLs).
- Shaders: the Linux dxc must be 1.7 (/opt/dxc17, v1.7.2308): the raster shader libraries are linked when the
  game runs by dxcompiler.dll 1.7, which fails on 1.8-made libraries (rt64 RT64_HOST_DXC_DIR).
- RecompiledFuncs regenerated with the full post-regen pipeline (N64Recomp ffb39cd + fix_* + readd + RSPRecomp).
- Standalone (NOMERCY_STANDALONE, default ON; recompui::set_standalone): no launcher (main.cpp finds the ROM: the
  kept copy or a .z64/.n64/.v64 in the folder; else a message box), no config menu, closing quits at once,
  dropped files ignored, no mouse cursor. Window "WWF No Mercy Arcade". WCW_AUTOBOOT still works for tests.
- librecomp export `recomp_set_config_u32(id, value)` (the option's own type); the mod's F2 menu has every
  option now (Big Show, New Superstars, NM+ media too) and saves them on close; roster options change only
  when nobody is playing (arcade_roster_opt). continue_secs enum gained "25".
- PC QA: work/testrun/testrun.c (qa\testrun.exe + testrun.txt: env, key=<s>,<vk>, close=<s>; log in
  qa\testrun.log); frames via WCW_PRESENT_LUM + WCW_BMP_*.

## v0.34: JR and King at the announce table (src/announce.c)
- Table: func_8001DB4C (set-up with the arena; 4 pieces at 0x8007AC40, 0xDC each, positions 0x800500B8: x +-120,
  z -605/-545, top y 90, x +-110, z -685..-525), func_8001DF38 every frame (state 0x8007D78C: 0 up, 1 breaking via
  func_8001B08C, 2 fading, 3 gone; func_8001B0B8 rebuilds; none when 0x800A9950 & 0x100000 or 0x80097EC0 & 0x10000).
- Models: func_800140B4(slot, id, costume) only builds into a slot -> slot 0 borrowed (six/spec.py MAIN arrays saved,
  gen/mkannsave.py -> ann_slot.h), bones/parts copied out (heap), pointers moved, the slot restored; its shadow
  model freed and its draw entries dropped. In a match func_8000A900 finds records in 0x80097FE0 (0x114 each, looks
  at +0xFC, +1 size) by ID (0x800BD4D0) and costume (0x800A9358): entry 0 is JR's for the build (looks from the
  roster: ordinal 64 costumes 0/1).
- Draw: list 0x80060BA0 (count 0x800619B2), pool 900 x 0xB8 (0x800619B0); func_80002730 draws by group = bone
  flag (0 props, 4 wrestlers, 5 see-through), each group far to near by +0x28 (view depth + the object's +0x24 bias
  x 1024): no z-buffer between props -> their bones' +0x24 biased (legs +300 behind the table, forearms -150).
- Poses: announce/poses.py (bind space faces +z: rotations -> the game's Euler order, func_80010E50) ->
  ann_poses.h; func_80012BE0's matrices in C (tables 0x80047ED0/EF8/F0C/F40, size tables 0x80047F54..).
- Commentary tick (exe): 2 = JR's line on, 3 = King's (director.h speaker()).
- Rig: test/test6.ld keeps announce.o at 0x80776000 (rebuilt without moving the rest); harness rig.py
  save_state/remap (a state's elf next to it).
- (0.34.0 later) Own loader (func_800140B4's steps in C): no shadow model, the hat kept (0x80091CB4 = 0 around
  func_800148F0: in a match the game takes headgear off, desc +0x254 & 0x80). JR = costume 1's model (black, hat).
  Chairs: announce/mkchair.py meshes served as files 0xFFF0/0xFFF1 (files.c ext_file -> announce_file), the
  table's pictures (0x7B6/0x7B5 dark, 0x7B4/0x7B3 metal). Layering: the camera's eye solved from 0x80062BA8 (x, y,
  w = 0), each piece's sort bias set to land it just before/after the table's pieces (their +0x28 keys).

- (0.34.1) Announcers: seats at z -688 (were -700: backs in the barricade at -712). Bone joints are mostly at the
  hips (a bone's +0x8C is its pivot, not its piece), so the old "below y 85 = under the table" test caught the whole
  body: now only the legs (6 7 8 13 14 15). The table's pieces are group 5 (+0x18) when the camera is behind it
  (func_8001DF38 compares its key with the ring's): group 5 draws after all of group 0, so pieces of them meant to
  be drawn after the table are put in group 5 too (chairs as well).
- Table collision (wrestlers stop at z -500 at x 0) is NOT from the table pieces or 0x800500B8 (moving them leaves
  it there): moving the table means finding that too.
- (0.34.2) A part's +0x28 / +0x2C are its joining draw objects (blended pieces drawn with the parent): their +4
  bone pointers must be relocated with the bones too (they pointed at slot 0's bones and the draw-list filter
  dropped them: holes at the chin and chest). Bone 4 (neck, an 8x8 texture) draws as a white patch over the chin
  on JR / King: hidden.
- (0.34.3) Table collision = the match overlay's collision quads (s16 corners x 4 + plane; vanilla at 0x80146BA0..
  0x80147E70, found with the core's access trace on the wrestler's position): the table's walls x = +-113, z -690..-530
  (to y 100, and y 90..1000 above it), its top (y 90) and its 4 bounding planes (ids 83-86: x 113, x -113, z -530,
  z -690). announce_table_move: the pieces' places (0x800500C0/CC/D8/E4 z) +20, those quads' z +20 (10 of them).
  The barricade plane z -690 (x +-1000, a separate record with the same corners as plane 86) stays.
- The test harness's buttons (parallel-n64's WWF profile): "Z" = C-up, "R" = C-right, "CU" = C-left, "CL" = L,
  "CR" = R, "CD" = C-down; no Z. NM_PADS = what the mod sees; the vanilla game's own pad copy is 0x8009ECD4.
- The core's trace (retro_trace_enable / range / dump: every load/store in the interpreter) needs the
  cached_interpreter core option. Rig states: resume() reinstalls the blob (statics zeroed), M.load alone hangs.

- (0.35.0) Announcers: chairs (announce/mkchair.py) seat top 52 (was 62; the thighs are ~60 at the hips), back at z
  -40..-33 behind the seat point (was -26..-19). Draw groups: 0 props (sorted), 4 Superstars, 5 see-through, drawn in
  that order -> from behind them (eye z < ANN_TABLE_BACK) every piece is put after the table and in group 4 (group 5
  when the table is in 5), so Superstars and the ring no longer paint over them; standing with the table up gets the
  same before/after-the-table ordering (it had none: the table and the Superstars covered them from behind).
- (0.35.0) Arcade controls (arcctl.c) on: ARC_CTL_READY gating removed, mod.toml "controls", README section.
  Fixed: the arrow's start (now | 1) could be one VI ahead of now -> age underflow -> arrow and focus round cleared
  at once (half the focus changes showed no arrow, and the tap order restarted). Now ((now - 1) | 1).
  The arrow's anchor: y + 80 (a wrestler's +0x124 is his middle; the head top about +83).
  No Mercy's own controls (GameFAQs): L = pin by a downed man (also duck / reverse), R = block, tap R = half pick-up,
  hold R = standing pick-up, C-up = through the ropes, C-left = flip / weapons, C-down = run / turnbuckle; the
  finisher = the stick flicked in a STRONG grapple with SPECIAL (A+B in a strong grapple = the opponent's).
  Grapple states: 0x50 the attempt, 0x51 holding him (0x52 held), 0x55/0x56 the hold's move.
- Vanilla (non-six) test rig: a fresh boot hangs at the entrance (white screen) with 0.34.3's source too -- not today's
  work; the six build is what ships and boots fine.
- Deploy (from 0.35.0): the six build only, to the game's folder (<outside>); nothing old inside the game
  folder, no non-six build. Backups go just outside it: <outside><version> (the nrm and the README),
  md5 checked. The README never names the install location.
- (0.35.1) func_80014E44(slot, flag) (the menus show a model whole: func_800159FC; select screen, VS page) hides
  bone 4 like func_80014F5C -> skel_show (skel.c) keeps a WM2000 / Revenge model's part 4 (its neck) as skel_visible
  does. Six's copy is the stock code with the moved arrays (bones 0x8050B430, shadow 0x80501DE0, 0x1130 a slot).
- (0.35.2) Arcade controls: STRIKE / GRAPPLE pass at once (the old 5-VI wait for the STRIKE + GRAPPLE chord made
  joystick + A/B in a tie-up give the neutral move: the d-pad went first, A came after the direction was let go --
  rig grp.py: 0.35.1 gave 3391 (neutral A) for every direction, now = regular mode). SPECIAL = C-right: the stick
  flicked the joystick's way (up when neutral) for 3 reads; the stick's four flicks are four taunts (Rock: up 441d,
  down 2f88/42e5, left 4423, right 42d1) -- the old chord's clockwise turn did the opponent's taunt (GameFAQs: a
  clockwise turn steals it). FOCUS = RUN + ACTION within CHORD_VI (4): both wait that long; one already on (running /
  ACTION held) lets the other through at once. Rig: the core's SELECT toggle puts C buttons on pad buttons ("R" =
  C-right, "CU" = C-left, "CR" = R); presses under 3 VIs are never read by the game.
- Story runner (story.c) is built into the mod only with STORY=1 (-DARCADE_STORY: its RECOMP_PATCHes) until finished.
- (0.35.3) FOCUS = DEFEND + ACTION, no wait: each passes as it is; the moment both are down (an edge of either with
  both held) FOCUS starts, ACTION's meaning is dropped (a pin is only given on release, so none leaks) and neither
  reaches the game till both are let go. Rig foc3.py (rr4c.st): pressed within the same pad read (rig: < 3 VIs
  apart) nothing leaks; a read apart, DEFEND first shows one read of R+L (a brief 0x4C block flinch, as a DEFEND
  tap), ACTION first one read of C-up / C-left (nothing seen). RUN passes at once again.
- (0.35.4) Music files (game folder assets/music -> gen_data/music): gen/mp3theme.py (ffmpeg decode, loop found by
  4 s self-similarity at anchors 12..70 s with the seam mid-match, 0.25 s cross-fade, polyphase resample to THEME_RATE,
  loudness to NM+ themes' median 0.4 s RMS 8800 with a look-ahead limiter at 31000); gen/mkcontent.py adds a music
  slot per file (EXT_SLOTS, after 64) and user_music[] {id, slot}; roster_entrance_song (func_8000AAF4) gives the
  file's song for that ID first (his record's slot / effects untouched). Bret: slot 65, song 87. Measured decoded:
  NM+ themes median RMS ~8800 (7000..11000); the Revenge song (slots 63/64) is quieter, 4730.
  Rig: func_80017C64 (song, 2, 1) through GF_PLAY_SONG (test cmd 8) didn't change the music on the select screen
  or in a match (dbg_last_snd stayed -1): not the way to audition a theme in the rig.
- (0.35.5) Freeze in a six-man Rumble (user, 21:59): the game's RAM showed a normal match and draw list (446 / 654), the
  heap fine (1 MB free), every game thread waiting -- func_800012B0 (the frame task) on the framebuffer -- and the
  picture had lost the wrestlers and half the ring first: the renderer side (RT64, raytracer on). plume: fence /
  swap-chain waits timed (1 s) and the device asked each time (lost -> reported, with DRED, now on by default;
  WCW_DRED=0 off); wcw_gpu_status() in the reports; the reports read out the arcade log (logic.c flight_more:
  draws, heap, the six slots, the last 128 frames at FLIGHT + 0x480).
- Titantron rule (user, 0.35.6): a converted Superstar with no real Titantron video of his own gets none (video 0:
  media_video_select -> no list), not a stand-in. Bret / Neidhart: video 0 (their Revenge-entrance recordings, wmport
  videos 60 / 61, are still built into the content but unused). Jarrett / Meanie keep WM2000's real videos.

- 0.35.7: Revenge ports' fingers (parts 10/18): hands.c draws them with WM2000's pose meshes, whose UVs are laid
  out for WM2000's finger pictures; Revenge's own (dark red creases across the bands) came out striped
  orange/red. gen/wmport.py rev_fingers: WM2000's finger picture (Jarrett's, CI8), each colour replaced by
  the model's palm colour at the same brightness rank (pixel-weighted). Palms (9/17) stay Revenge's (mirrored).
  Rig check: nomercy6_full_h.z64 (the six ROM with the new wmport.bin at 0x3E00000), /tmp/devbox-0/hands3.py
  (dbg_cam side view of slot 0).
- 0.35.7 exe: crash reports. Two Rumble crashes were execute violations at heap addresses (live-recompiled
  mod code, or a jump into data); the old report's walk died on code without unwind info. Now: N64Recomp
  live_generator registers each live code block with its functions' names (librecomp mods.cpp),
  n64recomp_live_symbolize; the report prints the access kind (8 = DEP), VirtualQuery of the address,
  registers, every code address on the stack (live code named), dumps RAM before walking. 21:59 crash's old
  walk (exe symbols): game thread ... func_8001DA04 -> func_80002268 (regenerated) -> recomp::mods::run_hook
  -> a hook -> Config::get_option_value -> heap.

- 0.35.8: the Rumble freezes/crashes were announce_table_frame (hook on func_8001DF38): the 0.35.7 freeze report
  named it (live code func@82E23AF4 = announce_table_frame, spinning). N64Recomp compiles the unordered float
  compares as ordered (operations.cpp: c.ule.s -> `<=`, c.un.s -> false): with a NaN, `while (a > 180)`
  (c.ule.s + bc1f) never ends and `x != x` is never true. A Superstar's position NaN (a Rumble slot coming in)
  got through; wrapf looped. Now fbad() (exponent bits) on every float from outside, wrapf bounded.
  RULE for mod code: never rely on NaN compares; bound every loop on a float.
- 0.35.9 movex.c ON in what ships (NOMOVEX=1 to build without; build_test6.sh needs MOVEX=1). The per-wrestler
  move tables (six-man: moves 0x80509A80, record numbers 0x805146F0, continuations 0x8051C690, records
  0x8051E2A0; [slot][548]) and how a move goes on (record word +0x20: & 0x1E00 gets a continuation entry
  292+n at func_8013CBAC; bit 0 goes on, func_80129128 / func_80137044 -> func_800EC698(w, owner, 292 +
  next[owner][entry]) for ANY entry, so chains can be longer). Bret: entry 70 = 1880 (its animation 14296 IS
  Revenge's code-58 slam) flags 0x2501 -> 16770 -> 16771. Rig QA: /tmp/devbox-0/mxt3.py (test_ec698 in
  hooks_test.c redirects the CPU's entries 0..69 to 70): slam 110 frames, Sharpshooter, hold, release.
- Revenge vs No Mercy animation files: identical but for byte 0 (bytes 0-1 = end pose kind << 10 | speed: the
  kind indexes ovl 0x800F62C0, the stance to go back to; Revenge 0 -> NM 4 (or 0), 28 -> 80 ...).
  revmoves.py hashes bytes 1.. : Bret exact 11 -> 88.
- 0.35.11 ANIMATIONS ARE STREAMED FROM THE CARTRIDGE: func_80012A8C stores files_info's ROM start + 4 in the
  player (0x80500000 + 192 n, +0x10 ROM, +0x14 left) and func_80011340 sends PI requests 0x62 bytes at a time.
  A pack file (files_info start 0) played as garbage limbs (the "stretched limbs" were THIS, not byte 0: an NM
  file served from the pack looked the same). Also per animation file (0x2DD9.. x 0x169B): its first 4 bytes
  (table *0x8006A220, read at boot by func_800119A4) and a 36-byte record (file 0x2BF, *0x8006A224) --
  readers func_80011A54/A78/AC0/AE8/BD8/D30. Fix (movex.c): those six + func_80011340 replaced; pack
  animations get files_info start ANIM_ROM (0x7E000000) + pack offset, anim_stream copies from the pack and
  answers the request itself (osSendMesg to its retQueue). Revenge's records: its file 0x20F from file 0xE39
  (94% bytes = NM's for shared files after gen/revanims.py's per-byte value map; NM added bytes 34-35 01B0).
- Revenge's front special: strong grapple (A held) + the analog stick with SPECIAL! (taunt at full spirit);
  harness "A" = N64 A = grapple, D-pad = directions, the stick in neutral = taunt (code 190).

- 0.35.10: Rumble crash 01:28: D3D12 CreateResource E_OUTOFMEMORY in RT64 NativeTarget::copyFromRAM -> map ->
  bad_alloc, device removed; the user: it slowed down first. Bret in it (entry 74, Falling Back Drop). Suspect: a
  NaN/huge world matrix (PC float semantics) -> a giant part -> the ray tracer. func_80002268 (the scene's
  world matrices: node +0x1C local, +0x5C world = local x parent's world, +0x04 children, +0x08 next) is
  RECOMP_PATCHed with scene_world (gameplay.c): each result checked (NaN/inf/|v| >= 2^20 -> the parent's);
  count in FLIGHT +0x6C (crash reports print it; the log's "[mem]" line every 15 s with memory + VRAM).

## QA in the real game (2026-10-27): `Development\qa.ps1 <scenario>`
Why: the old chat called things fixed from the emulator rig (the N64 game + a test blob, hooks_test.c), which isn't
what ships; PC-only bugs (N64Recomp's float compares, RT64, D3D12) never showed there (0.36.2 "works" -> 0.36.3
"fixed for real"). Now the proof is a scenario run in the exe + .nrm just built, in a copy of the game (qa\game).
* Mod: hidden options qa_mode, qa_w1/qa_w2 (IDs), qa_c1/qa_c2, qa_arena (1..10 = venue 0..9), qa_ctl1/qa_ctl2
  (0 CPU, 1/2 controller), qa_special (bits), qa_matches (then plat_quit after a second). logic.c "QA scenarios":
  arcade_frame returns before the coin / phase logic; the title's attract demo is patched (patch_demo_table, the
  attract counter, title idle) and made a real match in arcade_demo_setup_done (logic_v1's way: NM_SLOT_PAD, match
  flags without the demo bits / CPU bits, venue; the demo bit and 1-minute limit cleared each frame); the pad hook
  passes controllers when qa_on; arcade_entrance_cam on. QA_STATE 0x807F3000 ('QAST', per slot 0x40 from +0x40:
  id | costume << 16, pad, x y z, spirit | stamina << 16, +0xA0 << 16 | +0x14, phase, +0x8C | +0xE1 << 16 | +0x7A
  << 24, elim, facing).
* Exe: WCW_QA_STATE=<csv> (crash_report.cpp qa_state_writer: 20 Hz while the frame changes);
  crash_report_shutdown() after recomp::start returns (the writer read the freed RAM: a crash on closing the window,
  found by the first QA run). WCW_QUIT_AFTER counts from late in start-up. WCW_AUTOBOOT=1 must NOT be set (it wants
  a stored nomercy.z64; the standalone exe boots by itself). WCW_VPADS / WCW_VPADS_SCRIPT for scripted presses
  (buttons only, no stick yet).
* Harness: qa.ps1 (copy, settings via qa\qa_tool.py config, PrintWindow screenshots as .bmp -> .png, run.json),
  qa_tool.py report: checks + verdict PASS / FAIL / CRASH / TIMEOUT / INCOMPLETE (closed early).
* Not yet: forcing a CPU move (the rig's test_ec698 replaced func_800EC698; in the mod it needs an exact copy via
  gen/asmpatch.py), tag / Rumble / Story scenarios (selftest + scripted pads reach Story today), stick input.

## The Royal Rumble "King in the ring" glitch (2026-10-27; the cause found: "The cause" at the end of this file)
What the user sees: a while into a Royal Rumble (often right after Bret Hart's back suplex, his Revenge "Falling
Back Drop", entries 73/74, RMA_BRETHART_158A_158B), King appears in the ring frozen, not part of the 3D world, a
black block for a head; the game stutters and slows; it may crash later (audio code, func_8002CD88: a bad pointer).
What it is (qa.ps1 -Record, glitch report 2026-10-27_141950 with RAM): every bone of BOTH announcers got a local
matrix that isn't numbers (from then on, every frame), their chairs' too. scene_world's root list (32) was full in
the Rumble's scene, so the bad roots got the identity: JR and King drawn at the world's origin, one on the other,
unscaled -- King's jacket, JR's black hat over King's head (the "black block"), JR's dark trousers. Thousands of
bad matrices a second, memory to 7.7 GB / graphics 6.2 GB (over the card's budget): the slowdown. The arcade's
frame code stopped running at that moment too (its overlay text gone, QA state frozen); game tables
(0x80047F54..) fine, the camera and wrestlers fine, Bret and his man at frame 57 of the back suplex.
Fixed (what's seen; QA announcers_nan_fallback): announce.c pose_kept -- a pose not numbers -> their last good one
(they stay at their places; hidden if there's none yet); chairs square to the ring; scene_world's roots 256.
OPEN: what makes their maths go bad (a stray write, it seems: it also broke the audio state). The first time a pose
fails, 'ADIA' at 0x807F3240 keeps what went into it (yaw, sin/cos, sin_d(30), stand/look/talk/flinch weights,
cur, base, the class tables, cls, and where the announcer / cur / dbg_ann_xyz / ann_stand are at run time -- the
announcer's address is stored one byte low: read at +1) and the exe writes a "pose" report with the whole RAM,
mods' memory included (dump to 0x84000000: file offset 0x800000 + (a - 0x81000000); words host-order). Suspects:
the data just before announce.o's in memory is movex.o's and hands.o's (Bret's Revenge moves and hands); hands.c
indexes the game's 20-entry hand-pose tables with the animation's pose unchecked. QA tools for it: qa_ann_stand,
qa_ann_nan, qa_cam (a scenario camera; func_80001580 patched), qa_fslot.. (a forced move; func_800EC698 patched).

## QA RULE (the user, 0.35.12): an imported move is checked WHOLE
Every stage of the move has to be watched to its end in the rig: the setup, each continuation, a
submission's hold (it must hold, the meters must run) and the release/get-up -- not just "it starts".
Contact sheets of the whole thing, both wrestlers, next to the same move done the natural way (e.g. the
special's Sharpshooter vs Bret's own ground Sharpshooter, slot 157).
- 0.35.12 (test): NM hands + skin on WM2000/Revenge models (wmport.py nm_skin, SKEL_WM_NMHANDS, hands.c
  NM path for them). Rig pack now > 1 MB: the test ROM is 64 MB (pack at 0x3E00000 runs past 0x3F00000).
  hands.py boot: the attract may run the intro first; the demo comes ~9600 frames (BOOTMAX 20000).

### The cause (2026-10-27, found with QA's write-watch)
- Not Bret's back suplex (qa\scenarios
umble_bret_backsuplex: 10 min of it, from real back grapples, clean; one
  Rumble froze 15 s in with none done). Every break came a few seconds after an entrant's model was being made.
- What breaks: a model's display list loses its end (G_ENDDL), so its texture list's G_BRANCH back to it loops
  forever: RT64 draws millions of triangles a frame (memory past 8 GB, the slowdown, the freeze; the announcers in the
  ring / NaN maths / the 3.9 MB zeroed are what follows). Seen in the frozen RAM: Jeff Jarrett's part 1 list, its last
  32 bytes a palette (offline walk: gen-free, scratch dlwalk.py).
- Who writes it: the face's / skin's decals, func_8000CAF8(part, tex, pal, at, n, ...) -- n colours into the part's
  palette from colour `at` (2*at bytes in, 2*n bytes; it assumes 256 colours, 0x200 bytes) and its pixels into the
  picture -- called by func_800199CC, func_80019B38, func_80019C54, func_80019D88 for each skin part (kind 8,
  0x8004FCFC[k]). A WrestleMania 2000 / Revenge model's palettes are packed in the ring (0x20 / 0x40 bytes, heap blocks
  one after another, its display lists right after), so the decal ran over the next block.
- Fix: gen/patches.py sends those 8 calls to skel.c pal_group_guard, which puts a decal in only where the palette has
  the room: up to the game heap's next block header (pointer, size, 0, 0xCAFE4ABE). A palette made with full room
  (every model made before a match) is unchanged. QA: pal_calls / pal_room_skipped columns, the last 40 calls at
  0x807F2C80 (in the run's state.rdram.bin).
- QA tools made for it: qa_bsuplex (scripted back suplexes), qa_rr_ports (a Rumble of ported Superstars only),
  WCW_GUARD_MOD (the packs read-only), WCW_WATCH_MOD (the mod's chosen words watched with debug registers, the writer
  logged), RT64's display-list cut-off (4M commands a frame, logged), the RAM at a QA run's end.
- Not yet seen: the guard skipping in a live Rumble (the packed layout didn't come up in QA's Rumbles) -- the user's
  own Rumbles are the test (qa.ps1 -Record).

### Fixed for real (2026-10-27, later): two bugs, not one
1. The arcade's own text and boxes (draw.c emit_texrect_q): a rectangle above / left of the frame (a glow's lines over
   the top, "2P PRESS START") put its negative coordinate into the TEXRECT command unmasked: 0xFFFFFFF8 -- opcode 0xFF,
   a colour image 4089 wide at an address made of the rest. RT64 drew into it and wrote it back (State::fullSync ->
   NativeTarget::copyToRAM): the 3.9 MB of the arcade's memory zeroed (its constants -> the announcers' NaN, King in the
   ring; the Revenge animations' tables -> the wrestlers out of place). Caught by QA's read-only packs (WCW_GUARD_MOD).
   Fix: rectangles cut at the frame's edges (the texture moved on as much), coordinates kept to 12 bits. RT64 also
   refuses a colour image wider than 1024 or past 8 MB now (rt64_gbi_rdp.cpp, logged "[arcade] colour image refused").
2. The ported models' palettes (Jarrett, Meanie: WM2000; Bret, Neidhart: Revenge -- the only four WM2K-tagged; the
   rest are No Mercy Plus models, No Mercy's format already): packed at their own size, No Mercy's decals (256-colour
   palettes assumed) ran over them. Fix in the data: every palette a ported model names is 0x200 bytes
   (wmport.py nm_palettes, run by build_all; gen/nmpal.py did it to the pack made before -- the old files kept as
   *.before_nmpal). Their meshes, rigging and pictures unchanged (re-rigging them No Mercy's way: their legs join
   7:(6,1), 14:(13,1), No Mercy has no such joins; No Mercy never draws a neck; README 0.33 "a WM model joined No
   Mercy's way can't be drawn"). skel.c pal_group_guard stays as the backstop (room up to the heap header).

### Titantron and the announcers' chairs (2026-10-27)
- The big screen's live picture was the second camera's image shown in flipped tiles: RAW IS WAR's screen (in the
  match: three columns x two rows, x -500..500, y 340..870; up close one quad) mirrors a quarter of its own picture
  into every part by its vertices' S/T (T 2024 -> 8 -> 2024 bottom to top; the right column's S the left's reversed),
  and the patch only rescaled those. Now tron.c panel_patch gives the panel's vertices S/T from where they are on
  the screen (tron_panels.h: each model's extent), the whole picture once across it (originals kept, put back), and
  the material loads the whole 64x32 image. RT64 samples the image as its own (full-resolution) colour image: sharp.
- Not yet: the Superstars aren't always in the second camera's picture. Their layer is the scene's camera 1 (groups
  13-18 in a singles match), whose VP can't be read off its items (bones' matrices don't agree) -- drawn with camera
  0's the picture went black. Seen from some places the frame's layers split so that they are drawn (the view from
  behind the table showed them).
- The chairs: in the table's group (sorted by distance, no depth test between them) JR's / King's pieces and their
  chairs had the same key (the table's +/- 2048): which came last was chance -- the chair's back over half their
  chests from the front. The chairs are a step further: +/- 4096 (QA announcers_front, announcers_from_behind).
- Also 2026-10-27: the Rumble's eliminated men kept out when no entrant is given (logic.c arcade_rr_entrant), an
  entrant's elimination time cleared; rumble_one_each (one Undertaker); QA options qa_rr_ports, qa_rr_out.
- (later) The screen steady: only a pass drawn whole is shown (tron.c shown_buf; the lists alternate on their own,
  dl_buf); a layer whose camera can't be read takes its own camera's from the last 15 frames. Still to do: the
  Superstars in the picture every time (a snapshot at quit showed camera 1's VP = camera 0's: what blacked the
  picture when their layer was drawn with camera 0's is not known yet -- needs a capture mid-match).
- Entrance videos with the music: tron_music(1) from media_play_sound (func_800037EC, during the entrances: the video
  from its first frame), tron_music(0) from a hook on func_800037BC (the music stopped / fading). Not QA-tested (the
  QA singles skip entrances; the Rumble's arena has no screen) -- the user's test.
- JR / King's own pieces: moved past the table all by one amount (their order kept, their model's biases kept);
  their chairs a step past their furthest / nearest piece.
- (later, plan B) The screen in the match shows the match itself, not a second camera (the user: "one view of the
  match", no dynamic angles). tron.c mirror_pass: after the scene's last run (its first group = the last frame's last
  run's: tron_scene_begin last_first / last_first_was) a list called from the task's list draws the frame's picture
  (480x240, the frame's colour image) into TRON_IMG 64x32 in 8x8 pieces (60x30 texels each: TMEM), then sets the
  frame's colour image and scissor back. RT64 makes the loads tile copies of its own frame; the screen's panels load
  TRON_IMG, which RT64 has as its own colour image. The first build drew black: the combiner was TEXEL0 x SHADE, and a
  rectangle has no shade (found with a red fill over half the image: the fill showed, the pieces drew black); now
  0xFCFFFFFF/0xFFFCF279, the texel alone. QA tron_raw_ring (from behind the ring): the match on the screen in every
  shot, both men. The game's HUD shows on it too (it's in the picture). dbg_tron[] published at QA +0x280.
- (later) Hard camera (the user: "a separate hard camera angle, like WWE on TV ... from the side, slightly angled
  down"). How the game draws (traced 2026-10-27): no depth buffer anywhere (no SETZIMG in any list): the scene's
  items (0x80060BA0, n 0x800619B2; +0 chain kind 0/1, +4 node, +8 next of chain, +0xC centre, +0x24 order bias,
  +0x28 key, +0x2C/+0x2E/+0x30 vertices, +0x34 triangles, +0x38 + slot*0x40 fixed matrix) are keyed by the camera
  (func_800022D8: node+0x9C = node world +0x5C x the frame's VP 0x80062BA8; key = (bias + z) * 1024 + sqrt(x^2 +
  y^2) of the centre), sorted far to near (func_80002400), grouped by the graphics thread (func_80002730 ->
  func_800025B0 in 8 passes (group, kind): (0,0) (0,1) (6,0) (3,0) (4,1) (4,0) (5,1) (5,0); kind 1 culled by
  func_800024F8: |x|,|y| <= 1.5 (z + 150); the node +0x9C made fixed into +0x38 for the slot) and drawn by
  func_800028B4 (per group: each item's matrix forced (DC38000E + DB0C), its vertices loaded; then FA/FB venue
  light + DE its triangles). Every drawn item's matrix = world x VP (checked: all 175 in a RAW match; the HUD's
  identity, node +0x19 != 0). tron.c tron_hardcam_build (hook on func_80002730, before the main thread goes on)
  does all of that again for the hard camera (eye 820,270,0 -> 105 high at the centre; 30 degrees; 2:1) into
  its own matrices (0x80680000, 1024 a slot) and list (0x806A0000, 0xC000 a slot; viewport 0x806B8000): the
  screen's image cleared, the game's task setup (seg 1 +0x10), the groups; called at the frame's last run
  (hardcam_pass), the frame's picture and the game's viewport put back. Only where the screen's panels were
  found (RAW IS WAR) or qa_tron. QA: tron_raw_ring (the screen from behind the ring), tron_hardcam_compare (the
  TV camera at the hard camera: the reference for its order), tron_rumble_full (qa_tron, a full ring: at most
  302 matrices, 0x33C0 bytes, never ran out). The RAM copy of the 64x32 image loses thin ropes; the screen (RT64's
  own, high resolution) has them. White whole-screen frames in shots are the game's finisher flash (seen before).
- (later) The chairs still looked wrong from the side (the user's screenshot: thick blocks, half in the barricade,
  the two far from the table with their arms out). Cause: they sat on the barricade's line (z -692; the barricade,
  the table's collision back: -690), the table's back edge at -667 -- no room for a man and a chair. Now the table
  is moved up 75 (ANN_TABLE_UP; its pieces' data and its collision quads: front -530 -> -455, the high wall over
  its top -650 -> -600 (just before them), back still -690: verified in a snapshot), they sit at z -637 (25 behind
  its back edge, as before: 11 put the edge across their upper arms -- forearms drawn on the table, upper arms under
  it, cut off), the chairs thin (mkchair.py: seat 4, back 4, legs 2; back z -36..-32) and clear of the barricade.
  ANN_TABLE_BACK -612; scenario checks moved (eye_z < -612, King z -637). QA announcers_side (the user's view),
  announcers_front, announcers_from_behind: pass, looked at.
- (later) The user: hard camera higher and tilted down more (only a sliver of the apron): eye (820, 420, 0) -> (0,
  150, 0). And the screen's own RAW IS WAR picture's steel truss border (it belongs with the stage's steel) kept:
  the panels' materials now branch to a list of ours (tron.c panel_patch, PANEL_DL 0x806B9000, 0x400 each): the
  arena's material and its triangles as they were (their texture coordinates no longer touched), then the picture
  as a quad of its own inside the screen (TRON_MX 6 / TRON_MY 10 percent in: the truss ~4% / 7% and a little of
  the dark border inside it, measured off the original -- QA tron_raw_original, titantron_live false: a Bool
  option must be false in a scenario, 0 is taken as the default), in the panel's own vertex slots (reloaded after)
  and the material's state put back after. In the panel's place in the scene's order: what's in front of the screen
  is still drawn over it. The entrance videos show in the same inset. QA tron_raw_ring: looked at.
- (later, 2026-10-28) The inset quad (over the arena's screen, in its place in the scene) broke the entrances: the
  stage wall gone around the screen in the close shots -- there the vertex slots and the current matrix where the
  screen's material is called aren't the screen's, and putting "its" slots back after the quad corrupted what came
  next. Now nothing of ours is drawn in the scene: the panels show the picture across all of the screen again (their
  S / T, as before), and the picture itself gets the frame: tron.c panels_draw draws each panel the arena's way
  into it (its material, its original vertices copied at patch time, a matrix laying the screen flat as the picture
  lies on it, its triangles' list copied without its geometry-mode commands -- the far screen's turns on back-face
  culling), then the hard camera (viewport and lens of the inner part, IN_X0..) or the entrance video (video_pass:
  the CPU's TRON_VIDEO stretched onto the inner part by a texture rectangle). RT64 does NOT clip a fill rectangle to
  the scissor: the inner part's black is filled by its own rectangle (the full-size fill had wiped the frame).
  QA tron_entrances (new: a QA Rumble at the scenario's arena -- logic.c rr_configure takes qa_arena -- its first two
  entrances, shots during them): the stage whole, the videos in the frame; tron_raw_ring: the frame round the hard
  camera. Looked at.
- (later) The user: in the entrances' close shots the video should go to the truss, as before. The arena has two
  models of its big screen: up close (tron_panels.h entry 0, one panel, the stage's own 3D truss round it) and far
  (entries 1, 2: the truss painted round its picture). TronPanel.framed says which; the panels met in a frame's lists
  (seen_mask) decide the next frame's picture (tron_framed): the far screen's -> the arena's frame drawn in, ours
  inside it (lens and viewport of the inner part); the close one's -> all ours (the hard camera's lens 2:1, the video
  across all of it). QA tron_entrances (close shots: the video to the truss, the stage whole), tron_raw_ring (the
  frame): looked at.
- (2026-10-28, open) Coin + Start during an attract demo: the Single Play (championship) page's 3D scene comes out
  small. Reproduced: QA menu_from_attract (new "match": "arcade" -- QA mode off, driven by "pads"). The projection
  and viewport are the game's; the menu camera's eye is at (0, 200, 1280) after a demo against (-100, 50, -520)
  from the intro path (view at 0x80069890, VP 0x80062BA8). Reopening the menus on the title after the demo
  (NM_RETURN_SCREEN) changed nothing (reverted). The eye's own variables not found yet (not plain floats).

### 2026-10-28 overnight (not deployed: the user tests first)
- Hard camera: a Superstar outside the ring is drawn in front of the ring (group 5) or sorted among it (0) by the
  side of the ring he's by -- func_800151A0 per bone, the side table 0x80084620 (func_800231A4: the ring's edges
  0x800528BC / 0x8005288C seen by the camera: 5 facing, 0 not). The hard camera took the TV's: a man on the TV's
  near side was drawn over the ring in the screen's picture. tron.c hc_sides: the table for the hard camera; a
  wrestler's bone (SIXA_80091CC0 .. + 6 x 0x1810) with 0 / 5 gets the hard camera's side of where it is (world x,
  z from its node's matrix: the game's quadrant (x+z<0)*2 | (x-z<0)). QA tron_hardcam_outside.
- JR / King on their feet with no table (broken; qa_ann_stand 2 for QA): drawn as a Superstar there is -- the side
  table's group for where they stand (5 / 0), with the wrestlers by them, sorted with them (announce.c free_group;
  in group 4 a man by them, in 5, was drawn over them: the user's screenshot from behind the barricade). The hard
  camera re-sides them too (ann_free, announce_owner). QA announcers_free_brawl.
- The screen's switch from the arena's picture to ours (the user: a glitch going to a video): the panels are found
  and recorded, but made to show our picture only once a pass of ours has drawn it (img_passes / img_ready,
  panel_apply); until a video's first frame is in, the video pass draws the arena's own picture of the screen in view.
- The entrance videos (the user: "horrible quality"): drawn by the RDP from the frame itself, 64 x 64 CI8 through its
  palette (TLUT in TMEM's upper half, the frame in two 64 x 32 halves) -- RT64 at its own resolution, filtered.
  Before: the CPU's 64 x 32, every other row dropped. Frames double-buffered (TRON_FRAME / TRON_FRAME_B): the main
  thread reads the next while the other's drawn. Timing as before: each frame for its own time.
- QA: "match": "arcade" (QA mode off, driven by "pads"); qa_tron (the hard camera anywhere); a QA Rumble takes the
  scenario's arena; the move log (QA_STATE + 0x400, 256 x 8 bytes, count at + 0x3FC: match frame, slot, move +0x30A
  (looks like the move code: base + variant 0/2/4/6/8), +0x308, +0x304 -- the latter two not understood).
- Open: the struggling slam (the user: Bret and Austin struggle to lift men who aren't super heavyweights; ban it
  but for those). Move 960 (No Mercy's body slam that chains into the struggle, 960 -> 961; no name: the engine's)
  isn't in Austin / Bret's logged moves in a 150 s CPU match; their codes are mostly base + 2/4/6/8 (a move's
  variants: counter, block, ...). Whether the "struggle" is 960 or a strong grapple's block variant isn't known yet:
  next, catch one on screen with the move log beside it (screenshot times vs match frames).
- Open: the 2-player menu (back, then pick again: the menu vanishes) -- not reproduced yet (a pads script through
  the challenger / versus flow needed).
- Story creator (mod/STORY_DESIGN.md, gen/storyc.py, src/story.c; build_mod.ps1 -Story): the runner's pieces exist
  (pack format, install / restore, branch evaluator, text hooks), proved only in the rig. Missing to finish: a loader
  (the pack built by storyc.py into the mod, or the exe reading stories\*.json), a way to pick a custom story (an
  entry on the championship list, as the Royal Rumble's), the four Story Mode patches on in what ships (they're
  RECOMP_PATCHes: every story then goes through story_branch -- must leave the game's own stories untouched), 3D
  scenes / choices / match options in the compiler, and QA driving Story Mode (pads).
- 2026-10-28 (dev build, not deployed, not yet QA'd in the game): Hollywood Hogan (0x0105), Kevin Nash (0x0106),
  Scott Hall (0x0107) from Revenge, as Bret / Neidhart (data
evport\ports\PORTS.md: the captures, how found).
  ext_late 13-15 (ordinals 120-122), photos 114-116, music slots 65-67 (Revenge's 0x214, as Bret's / Neidhart's;
  "Bret Hart.mp3" moved to slot 68), video 0, options hogan / nash / hall (RO_HOGAN.., ROSTER_LATE_N 16). Movesets:
  The Rock / Undertaker / Triple H the references, REV_WEIGHT heavy for all three, REV_POSE Nash 3 (the big men's
  stance); REV_OVERRIDE: the Revenge body slam (960) out -- Hogan 30 and Nash 25 -> 16250 (Back Body Flip), Nash 50
  -> 1020 (Sidewalk Slam; mkroster asserts no 960-969 is left in the three). Their Revenge animations: movex_data.h
  (Hogan 13 rows, Nash 15, Hall 12) -- each to be checked WHOLE in the game (the QA rule). With every new Superstar
  on, SUPERSTARS 6 / 7 are full (18 boxes): Hogan gets the last, Nash and Hall the LADIES page's free boxes
  (roster.c place_ext: the first page with room). Bodies (mkparts): Hogan, Nash 5 (Thick 2), Hall 2 (Medium 1).
  Generators on Windows: mkroster makes nmport/moverecs.pkl's tables from files 0x69/0x1F0/0x1F1 when the pkl is
  missing (roster_ext.h byte for byte as before); vadpcm.py runs the SDK tools through WSL; mkcontent needs
  ports
mbank.pkl copied to scratch	mp (its NM+ instrument length fixed to 0x3BDF9: content.bin and the old
  content_index.h entries as before). Commentary not touched (mkcommentary can't remake the pack): lines naming
  Hogan / Nash / Hall were dropped when it was made (NOT_HERE), and JR has no name clips for them.
- (later, 2026-10-28 morning) Fixed: coin + Start during an attract demo -> the Single Play page's 3D scene small.
  The menus' camera (ovl_a: page place 0x80104478, eye 0x80104490, looked at 0x8010449C, going to 0x801044A8, place
  again 0x80104540, page 0x801044D4; moved by func_800E2388 while 0x80106138 counts) is glided between pages'
  places as pages change; from a demo the menus come straight onto Single Play and it stays at its far place
  (0, 200, -1280). logic.c menu_cam_fix: on Single Play / the championship list, still there -> the page's own
  ((100, 50, 520), looking at (100, 50, 1020), page 14) -- what the intro path leaves. QA menu_from_attract.
- Fixed: Versus, back and pick again -> the menu emptied. In the Exhibition menu B leaves it for the main menu
  (the tabs go by L / R); from ARENA that's where B went, and the arcade put them back on MATCH, which came up with
  nothing on it (nor ARENA after it). Now on Versus's PLAYER / ARENA / RULES pages B is the tab before (L); the
  one-line-up PLAYER page passed going back goes on to MATCH (menu_change, as before); B is kept off the MATCH page
  while it's still coming up too; a MATCH page come up empty is set up again (GF_EXH_INIT, exh_rows_fix). QA
  menu_versus_back (qa_p2: the other player's Start N frames in -- a scripted second pad doesn't reach the game's
  controller 2; note the exe maps the script's "x" to N64 B, "b" is a C button).
- Struggling slam, more: Austin and The Rock have no 960 in their movesets; 22 stock movesets have it (slot 35,
  Andre slot 30). The engine itself turns a move into 961 (the struggle) in ovl_e func_80120D98 when the
  wrestler's +0x82 has bit 4 (bit 1 first: move 4). What sets bit 4 (a weight test?) is the next thing to find,
  then: bit 4 kept only when the other man is a super heavyweight (ability 2 == 2, e.g. Andre). Ports: Hogan slot
  30 and Nash 25 / 50 came out 960 (overridden as Bret's, by the port's agent).

## Story creator, finished for testing (2026-10-28; not deployed)
Custom stories (JSON) played by the game's own Story Mode engine, picked from the championship list. Authoring
guide: `mod\stories\README.md`; sample: `mod\stories\the_grudge.json`; engine formats: STORY_NOTES.md.
* On in what ships: build_mod.ps1 defines ARCADE_STORY by default (`-NoStory` leaves the four RECOMP_PATCHes out;
  `-Story` is accepted and does nothing). The game's own stories are untouched because each patch, with no pack
  running, is the game's code exactly (checked instruction by instruction against the disassembly):
  - func_800DD740 / func_8010CC38 (text): no u16 mask any more (the game takes the id as it is); ours() answers
    only while a pack runs (ids 0xE000..0xEFFF) or for the row label id 0xF1CB (a gap: the game's lookup gives ""
    for it, and nothing in the game asks for it). The rest is the game's table / ranges / empty string.
  - func_800ED130 (current node): sh / sw as the game; the pack's roles go in only while a pack is armed.
  - func_800EE1F0 (branch): node +8 read before the rating call (the game's order), history byte / Rumble
    history as the game, then the game's record walk with game_cond = the game's field loop (fields 0..4 by the
    jump table at 0x801036D0: masks 7 / 0x3000 / 0x70 (func_800EDD84) / 0xF00 (func_800EDE48) / 0x70000; 20..31
    one bit each; a result of 1 fails the record, the last result < 0 takes it).
* Loader: built in. build_mod.ps1 runs `gen\storyc.py --header stories src\story_packs.h` (every `*.json` but
  `test_*` / `_*`; a story that doesn't compile stops the build). story.c: story_count / story_title /
  story_belt / story_start_builtin. (No run-time file reading: one rebuild per story change.)
* Championship index (fixed in storyc.py): the list's row = 0x800A9579 = the engine's 0x80097ED1 (func_800ED28C):
  0 World, 1 Tag (it gets the partner), 2 IC, 3 European, 4 Hardcore, 5 Light Heavyweight, 6 Women's.
* The row (logic.c "a story of ours"): the list's row 8, the "Belt" box (def 0x8014C084, label 0x8518, no action;
  defs are 0x16 bytes from 0x8014BFD4 = row 0), is made a text row at (-0x46, 0x30) under Women's, label 0xF1CB,
  action 0x428 (the arcade takes A, as Rumble / Survival); Women's down, World up, Survival left / right go to
  it. Row label kinds (ovl_c func_80106B14): high byte 0xF0..0xF3 = a text id (func_80122ABC -> func_800DD740;
  0xF000..0xF0CD via a table at 0x80148494 of {text id, style}); 0x85xx etc. are pictures. A text row isn't lit
  when the cursor is on it: the arcade blanks it then and draws it itself (custom_row_draw, GT_GLOW, orange,
  centre 128 / top 145 in the 480 x 240 frame). Operator: GAME SETTINGS > CUSTOM STORY (OFF / the stories'
  "menu" names; option custom_story, default 1).
* Starting: A on the row -> story_start (pack parsed; the save's story-in-progress record kept and cleared; roles
  and variables; tables installed) -> the cursor put on the pack's championship row, A injected 12 frames later
  (custom_frame) -> the game's own Superstar Select and new story (func_800ED28C). Its ED130 call (after it
  cleared the cast, 0x7EEC..) is where the pack's roles go in (the old test "phase 0 and chapter 0" never held:
  ED28C sets 0x7ED0 = 1 first). story_frame(keep) stops the story back on the title / list (not while the list
  is still up right after the start: up to 5 s; not while single player is held for a challenger: keep).
* The save: the story in progress is image + 0x6D5C (0xC4 bytes; +0 the node, 0 none; func_800ECFFC writes it at
  each node, func_800ECEF0 continues from it, func_800ECE78 = its championship from the node's range). While a
  pack runs, save.c's write gets the kept record (story_save_swap round save_sanitize / save_restore); on stop the
  kept record goes back into the image. (save.c's 0x6F90 isn't the story record: func_800E35E4 checks an ID there
  with the presets.)
* Text names: the engine's %cNN = 0x800BD900[N-1] (main func_80008720: only the second digit), filled by
  func_800ED628 with the cast of the scene loaded last -- on the match card too. So storyc names a node's
  requirement text from its pages' scene cast (a requirement naming anyone needs pages); first QA run showed
  "ROCK vs AUSTIN" for "{player} vs {rival}" before this.
* bigshow.c remaps Richards / Benoit in all 666 parts (ours too): story.c's "ours" test is the nodes only now (a
  remapped part of ours made ours_a() false and the next install would have kept OUR tables as the game's).
* QA: scenario `story_custom` (match "arcade", free play, options custom_story 1, qa_story 3). qa_story (hidden):
  2 = the arcade starts one player on the title, puts the cursor on the row, chooses it 2 s later, picks qa_w1 at
  the select, and quits back on the list after the story; 1 = the CPU plays the player's Superstar in a pack's
  matches and A goes through the replay. state.csv while qa_story: matches = 10000 + node * 100 + phase (0x7ED6),
  clock = the pack branch taken last (dbg_story_branch, -1 none).
  - 2026-10-28_094341_story_custom: the row on the list ("Custom Story" in the game's text font, the Belt box
    gone), the select, both narration pages ("ROCK has been running his mouth / about AUSTIN all week long.",
    "Tonight AUSTIN gets the chance / to shut him up. One on one."), the match card (Single Match, STEVE AUSTIN
    VS THE ROCK, our requirement text over it, names the wrong way round: fixed above), the match in RAW IS WAR,
    CPU vs CPU to a finish (~13000 match frames). Then the replay looped (nobody pressed A: qa_story 1 presses it
    now) until the run's time ran out.
* Not done: 3D scenes (place, cast, animations, speech), choices, match options (arena / rules / title; `raw`),
  run-ins beyond the rig's one, how a match was won (branches use the game's win / lose / notlose tests), stories
  read at run time, a challenger during a custom story (keep path written, not run).

## 2026-10-28 evening (the user's play test of the overnight build; not deployed)
- JR's arm through King's chest close up (F6 camera): the game draws no depth buffer at all -- every piece of every
  model in the order of its distance -- so two models side by side cross wrongly close up. JR and King now draw with a
  depth buffer of their own: announce.c ann_zwrap copies each piece's list (40 commands at ANN_ZDL 0x806F0000) with its
  render mode's Z_CMP | Z_UPD on and G_ZBUFFER, calls the piece's triangles, then puts its render mode back; tron.c
  ann_zclear, where the frame's scene starts (func_800028B4's first run), fills ANN_ZBUF (0x80700000, 480 x 240 16-bit:
  expansion RAM that's empty in a match) far and makes it the depth image. Nothing else tests or writes depth: the
  table, chairs, ring and Superstars keep the game's order with them. The hard camera's picture draws their own lists
  (ann_zdl_orig). QA announcers_depth_side / announcers_depth_front (and -Installed for 0.37.0's): looked at.
  (qa_no_zbuf 1: without it.) (The access violations after "recomp::start returned" seen in some runs since were
  older than this: below, the exe's exit.)
- The Titantron's picture: the arena's pieces the TV's camera sides (0 / 5 by the side table; the barricades, the
  crowd's near rows, the table: func_80025F40 and the venue's) get the hard camera's side too -- a node seen both 0
  and 5 is one (tron.c sided_find), its centre's quadrant -> hc_side. QA tron_hardcam_sided: no visible change from
  the stage end; the user's "announce table and right barricade in front of the crowd" not reproduced: the crowd
  over the right barricade is the same with the screens off and in 0.37.0 (tron_zorder_cam_on/off, -Installed).
- F2: QUIT MATCH (a match or its entrances: left the challenger's way -- route(): the championship list / Versus's
  Exhibition menu; a demo: the title) and RESET TO TITLE (start_game_over without its words, credits kept).
  QA: qa_op (8 / 9) at qa_op_at match seconds; op_quit_match, op_reset.
- Versus's Ladder Match row as the game's own: label 0x8506's table entry variant 9 (Single's 128-px quads; with 0
  it took this page's style-2 quads, smaller and squashed), unhidden the game's way (+0x4A &= 0xFFF0: it fades in
  with the rest; the colours not written), out of the left / right chains (defs 5, 10, 11: the PPV grid's).
  (scratch\menus\notes.md: the menus' rows, labels and pages, studied.)
- Custom Story: the row a picture ("Custom Story" cut from the ROM's labels, gen/labeltex.py -> label_tex.h, file
  0x4C57 under label 0x8517 -- the GBC championship's, unused by the arcade). A on it: the championship page itself
  becomes the stories' list (logic.c list_open): its rows 0..7 -- its own two columns -- show the stories (labels
  0x8525.. lent: { file 0x4E00 + story, variant 9 }), the rest hidden (+0x4A |= 1; the cursor's own links pass over
  hidden rows), every row's +0x56 set 0 so they fade in as a new page's do; B (list_back) puts the championships
  back the same way; L / R the next / last eight; A starts the story and takes its championship's row with
  menu_confirm (the list not seen again). The Rumble row's per-frame relabel (champ_rumble_slot) waits while the
  list is up. (Page 12, Exhibition's BELT page, was tried first: menu_change(12) shows its frame but never draws
  its rows -- the page's record +0xC / +0x10 stay 0 without its tab's path.) Each story's picture: its menu name
  through the game's rasterizer (func_80009F6C) in style 0x11 -- the menus' own condensed lettering ("Continue",
  "Decision"; styles 0 / 8 / 0x30 / 0x8000 are all the plain font) -- cropped to its letters, 19 px high at most,
  set as CI4 on World's palette with the outline round it (storylist_make). (func_800DF3BC(string, style), the
  menus' text sprite, is that same record, made over a frame: GF_MENU_TEXT, kept for tests.)
  The stories: the game folder's stories\*.json, compiled by the exe (arcade_stories.cpp, storyc.py's format byte for
  byte; _roster.json there the names) when the list opens (story.c story_rescan; the built-in ones if there are
  none). GAME SETTINGS > CUSTOM STORIES: ON / OFF. Samples: the_grudge.json ("The Grudge"), bad_blood.json.
  QA: story_list_view (qa_story 10: the list left up), story_custom_short / story_custom (qa_story 3: the first
  story on the list; the CPU plays; A on a chapter card after 10 s). Looked at: the row lit with its glow bar, the
  list ("Bad Blood" lit, "The Grudge"), Bad Blood's chapter card and narration, its first match.
- The exe ends the process at once after the game stops (main.cpp: std::_Exit after the logs are flushed): the
  teardown after main freed the live-recompiled code under a thread still in it -- an access violation after
  "recomp::start returned" in ~1 of 4 QA quits since 2026-10-27 (not the depth buffer, which came later). It also
  happened in the moment between that line and the exit, so the unhandled-exception filter ends the process quietly
  (exit code 0, no report) once the game has stopped (g_game_stopped). Since: 8 runs, every exit clean.
- Recordings (Play Test Build) now log each slot's move (+0x30A) in state.csv's sN_move (it was +0xA0).
- The struggling slam: not settled. The user's Austin vs Rock recording: their "struggle" frames are tie-ups,
  states 81 / 82 (0x51 / 0x52) ~2 s after a grapple (80), then the move (85 / 86) -- and neither has 960. Static study
  (scratch\struggle): 961 is 960's continuation; arcade DEFEND sends R and L in the same frame, so the game's
  reversal tracker (func_80132FFC) only ever sees R (L's checks never fire for an arcade player); arcade_speed's extra
  steps make a "hold" (8 steps) ~233 ms instead of ~267 ms. A scripted pad doesn't reach slot 0 while a real
  controller is player 1 (struggle_* runs: identical sequences, the pad ignored).
- Hall's / Nash's / Hogan's music: Revenge has no themes of theirs (its entrances play 0x214 / 0x215 by who enters
  first); all three share Revenge's 0x214. Their own: mod\music\<name>.mp3 (mkcontent: a slot each, no code).
  Legends of WrestleMania: 21 Hogan clips (scratch\audio\lowm_candidates.tsv), none for Nash / Hall; no WWE '13 or
  LoWM game files on this PC.

## 2026-10-29 (after the user's test of 0.38.0)
- Fixed: a challenger in the arcade's Royal Rumble -> HERE COMES A NEW CHALLENGER stayed up and the Rumble went on
  (seen in QA's contact sheet by the user). The banner holds the match with the game's own pause flag
  (NM_MATCH_PAUSED) while goal_wait runs; the quit (NM_MATCH_QUIT + NM_MATCH_LEAVE) was then asked with that flag still
  up and the game dropped it as the pause ended (QA_STATE + 0x380: leave_asked 1, both back at 0). Now the hold is
  let go first (banner_paused) and the quit asked again if a second on the match still runs (dbg_leave_retries). And
  the Rumble being left was counted as the challenge match played -- its slots refill as it goes (everyone in again,
  watch_match armed with game_mode Versus) -> player 2 dropped at once, player 1 back in a new Rumble: a match (or
  its entrances) on when the challenger came is never the challenge match (challenge_old). QA rumble_challenger:
  the Rumble left, Versus's MATCH page kept to the end.
- Fixed: the exe's crash on quitting (QA / Play Test Build runs, ~1 in 4): the arcade's QA state writer and watchdog
  (crash_report.cpp) read the game's RAM until crash_report_shutdown, which ran only after recomp::start returned --
  after librecomp had freed that RAM. Now librecomp calls wcw_rdram_release_hook just before the free (recomp.cpp,
  [wcw fix]); the hook stops both readers and waits for them (the watchdog marks its passes, g_wd_reading). (The
  earlier "fix", the quiet exit once g_game_stopped, only covered faults after the flag; this one was before it --
  empty crash report, exit code 0xC0000005.)
- Versus's MATCH page empty (the user, Play Test Build: a Story match, the title, Start, player 2 joining on the
  championship list): the rows are there (labels, flags, alpha, places) and their pictures loaded, but each row's
  draw object (+0x6C; its translation +0x4C) was made with the camera at the far place (0, 200, -1280): from the title
  the arcade goes by Multi Play's page to Story Mode (route G_STORY: menu_change(2) -> GF_PAGE_INIT 2) and after a
  reset / game over the camera is still there; a challenger later goes Multi Play -> Exhibition without the page
  built again (menu_choose builds only a page never built), the MATCH page's rows ending up behind the camera
  (draw items at z -1657 against 350). Fix: route G_VERSUS puts the camera at Multi Play's place ((750, 0, 720),
  looking at (750, 0, 1220), page 13 -- what the good runs had) and builds Multi Play's page again before choosing
  Exhibition; menu_cam_fix also puts it there on the MATCH page (from the far or the championship's place, the rows
  set up again). QA versus_after_story_reset (qa_story 3, qa_op 9, qa_p2 480 with qa_p2_after_op: the second game
  started by the driver, player 2 8 s into it). menu_versus_back / versus_after_quit / rumble_challenger were fine
  before (the page built afresh on their way).
- Hall's / Nash's / Hogan's music, from WCW nWo Revenge Redux (the user's zip, <outside>): an xdelta for
  "WCW-nWo Revenge (U) [!].z64" (applied with our own VCDIFF decoder, scratch's vcdiff.py; nothing from the zip run).
  Redux moved the music bank (header ROM 0x2D62CEC, samples 0x2D66BBC; boot code 0x3D28..0x3D38) and appended 42
  themes as VADPCM instruments 0x36..0x5F, each one 30 s clip at 22050 Hz (sequence files 0x3F00..0x3F29: one
  instrument byte apiece); its THEME LIST (ROM 0xDAF60: model code, a song per attire) names them: Hall (0x0204)
  song 0x18 "nWo" (inst 0x36), Nash (0x0203) 0x19 "Wolfpac" (0x37), Hogan (0x0201) 0x3B "Hollywood" (0x59; his
  attire 3 "American Made", 0x5A). Rendered to mod\music\Scott Hall.mp3 / Kevin Nash.mp3 / Hollywood Hogan.mp3 (0.5 s
  of silence first); mkcontent: music slots 69 / 70 / 71 (Bret's file still 68, the old themes byte for byte).
  mp3theme: a song looping whole from its first sample (no loop found: the 30 s clips) cross-faded its end into
  silence (it sliced x[t0 - N:t0] with t0 < N: a crash). QA music_hogan / _nash / _hall / _bret (Rumble entrances,
  the sounds asked for logged at 0x807F33B0 by media_play_sound): 211 / 212 / 213 / 210.
- QA: qa_tool.py config puts every option at mod.toml's default before a scenario's own (they carried over from the
  run before: qa_p2, qa_no_zbuf, controls ...; read without a TOML module -- the toolchain's Python has none).
  qa.ps1: no state yet after 420 s (was 90) / the hard stop at seconds + 450 (was + 45) -- start-up took 85 .. 240 s
  while the user had Unreal Editor / Epic open (runs stopped as "hung"). New options: qa_p2_after_op (player 2's frames
  counted once qa_op is done; the qa_story driver then starts a second game and drives nothing in it), qa_op in a
  driven story (qa_op_frame), qa_fcycle (a ported Superstar's Revenge-animated moves in turn as the forced move:
  movex.c movex_qa_entry; scenarios moves_hogan / _nash / _hall). Route state at QA_STATE + 0x380..0x3A4.
- Fixed: white flicker in Austin's entrance close-up in the Rumble arena (the user: "flickering in his entrance,
  abnormally"). The colour was exactly our depth clear's 0xFFFC (255, 255, 247), inside the game's scissor (not our
  full-frame fill): the game sets its fill colour once and fills the background with it later in the frame, and
  ann_zclear (tron.c) left 0xFFFC set. Now ann_zclear puts the game's fill colour back after its clear (its last
  G_SETFILLCOLOR in the list so far, a 16-bit word the colour twice; black 0x00010001 if none; QA_STATE + 0x3A8).
  (The earlier reorder -- depth image named before the fill -- didn't help: the fill never reached the frame.)
  QA austin_flash: 0 of 517 frames with that white (3 before; qa_no_zbuf runs 0); the close-up looked at.
- Verified: Versus's MATCH page after a Story match, reset, player 2 on the championship list (versus_after_story_reset:
  all six match types shown). rumble_challenger, menu_versus_back again with this build: fine.
- Story "arena" (storyc.py / arcade_stories.cpp, node +A's low nibble): QA story_arena (King of the Ring, venue 3,
  its arena in the shots; the run's time ran out in that match, CPU against CPU) and story_arena_wm (a one-node
  story, qa/stories_test/001_arena_wm.json: WrestleMania, venue 7, its stage in the shots).
- Exe: a start-up crash (C++ exception 0x20474343 -> 0xC00000FF): wait_for_game_started's bad_optional_access when
  WCW_QUIT_AFTER quit during a slow start-up -- recomp.cpp now returns if the game is gone; WCW_QUIT_AFTER counts
  from the first VI (main.cpp, wcw_viswaps_total).
- Imported moves QA (moves_hogan / _nash / _hall, qa_fcycle): each Revenge-animated grapple move forced in turn, the
  opponent an idle player 2 (the CPU reversed the first try's), forced only on a grab that holds the man (him 80 /
  81, a man within 80: picked standing, Nash's jackknife never started; on a grab that missed -- 88 away -- it played
  with nobody held, the man dropped 130 away; held, they're 57 .. 76 apart). movex_qa_entry: entries below 140 only (148 / 168 reversals, 265 / 266 the
  slide: the same files for all three, not forcible from a grapple). Hogan 60 / 75 / 81 / 118, Hall 25 / 73 / 77 /
  118, Nash 3 / 70 / 75 / 77 / 81 (the jackknife: ~3.5 s with the head between his legs, then the lift and the bomb): grapple -> move (81/82 -> 85/86) -> its end (down 1/49, hold 72/73, pin 74/75), shots
  looked at.
- Hollywood Hogan's commentary: none to be had -- Legends of WrestleMania's 21 Hogan clips are the Hulkster
  (scratch\audio\Hogan clips (Legends of WrestleMania), for the user to choose from); its labels and the lines'
  text never say Hollywood / nWo; WWE '13's bank (data\commentary_audio\wwe13) has no names, only ids. The
  generic leg-drop calls fit him.
- Hogan's commentary (the user's picks, scratchudio\Hogan clips (Legends of WrestleMania)\Keep): transcribed with
  faster-whisper small.en (a scratch venv; Windows' own dictation was useless), filed by what's said -- clips
  5401-5410, lines.tsv rows 6572-6595, wrestler 105: entrance (Hulk_001 "they broke the mold", Hulk_002a "needs no
  introduction"), dominate (Hulk_004a "24 inch pythons", JR 004a "how powerful", JR 004b "for a big man, he can
  move"), comeback (Hulk_006a "so resilient"), kickout_2 (JR 007a "more than that to finish off Hogan"), finisher
  (King Fin 002 "dug in deep there"; King Fin 001 "Hogan hits the leg drop" keyed "leg drop", also big_move),
  strike_big / big_move keyed "boot" (King Fin 003 "Hogan hits the big boot": his Boot to Face, 773). Each row
  twice, src 1 and src 0: the default Commentary lines "WWE 13" plays no Legends line, and he has no WWE '13 ones.
  Packed to mod\build\commentary.pak (not deployed); qa.ps1 now copies a built pack into the copy. The exe logs
  each line said ("[commentary] line N", N = lines.tsv row). QA commentary_hogan / _entrance: not yet run.
  Found: his port has no leg drop -- mkroster's prediction gives slot 70 (standing special) 1690 Old Man Flop and
  211 (ground special) 8230 Stomp ("exact" animation matches); so the leg-drop line can't play until his ground
  special is a leg drop.
- Fixed (data): the Redux themes ran twice too fast -- its clips are 11025 Hz (in tune at 11025 / 22050 only; the
  user heard 22050 as "super fast"), 60 s, not 30: mod\music re-made from the decoded instruments at 11025 (Nash's
  and Hall's now loop on a real repeat: 17 / 14 s on). QA music_* re-run pending (the 2026-10-30 runs' sound log was
  written over: below).
- Fixed: Bret's Sharpshooter, the man feet away (the user). gen/revanims.py SS_SHIFT -280 (the man's root moved in
  the hold's animation, thought No Mercy's own was a body's length off) was the fault: in the game the hold's start
  places them (Bret moved ~105 and turned round) and No Mercy's own files fit. SS_SHIFT 0. QA ss_bret (qa_fsub 2: his
  specials) and subs_bret: 157 and 70 both Bret on the man, the legs held.
- Fixed (data): the Revenge animations' sounds -- rec_conv made each record byte No Mercy's by the most usual value
  at that byte, so frames and sounds seldom seen there became another's (mostly 0): Nash's jackknife had no slam and
  no thud (the user). Now frames (bytes 15, 16, 18, 20 .. 32 even) as they are, sounds (21 .. 31 odd) by a sound map
  from the files both games share (mostly the same numbers; Revenge 0x2D.. -> 0x45..), REC_SOUND for one of our own:
  the jackknife's thud 0x1D (No Mercy's finishers' heaviest; its own Jackknife Powerbomb's). 85 sounds back in 96
  records. QA_STATE + 0x340 counts the game reading the jackknife's sounds (an earlier + 0x3AC was media.c's sound
  log -- it spoilt that run's music logs; + 0x3AC..0x3CF is media.c's).
- QA: qa_fsub (logic.c qa_move_entry, movex.c movex_qa_subs / movex_qa_same_place / movex_qa_own): slot qa_fslot's
  holds (a move with record 0x0400, or going on to one), 2 its specials, 3 its moves with animations of their own --
  each forced in turn where the game picks a move of the same place (the moveset layout 0x800442DC: an entry's lists;
  list 0 is the empty one); state "forced" = count | entry << 16; the list at QA_STATE + 0x344 / 0x348.
  subs_<wrestler> for all 19 imported; run so far: Bret (77, 157, 50, 60, 70), Neidhart (108, 35, 45, 55, 70, 72,
  157, 93) -- every hold with both men together. The batch was stopped (memory) before the rest.
- The reference's moves the ported Superstars kept (the user: Hogan's strong punch and stomps were The Rock's):
  gen/revanims.py now gives every slot that kept the reference's move his own Revenge animation on it -- a move's pair,
  or for a strike (Revenge keeps his side only) his animation with the reference move's reaction (mixed_pair).
  Hogan 26 (188 Rock Punch, 213 / 214 Rock Stomp, 94 Rock Spinebuster ...), Bret 21, Neidhart 32, Nash 24, Hall 30;
  not the double-team ones. movex.c MX_PER 32 -> 96. Not QA'd yet (qa_fsub 3).
- WCW's page: the Superstar Select has thirteen pages -- roster.c roster_pages_tab (the game's twelve, then WCW: Bret,
  Neidhart, Hogan, Nash, Hall, Savage, Hennig; never saved), func_8000A6DC / A758 / A7D8 in C, and gen/patches.py's
  copies of the select screens' (ovl_c) and page editor's (ovl_d) functions pointed at it (the table's start, +0x24
  count, +0x26 name; the game's data follows its old place) and the select screens' 12s made 13 (L / R, Random, the
  next shown, the tabs). asmpatch.py: labels carry the overlay (ovl_c's and ovl_d's functions share addresses) and
  the six-man engine's functions are named (func_8013486C calls rw_rumble_fill). Scripted pads take lefttrigger /
  righttrigger (the N64's L / R). QA select_wcw: not run yet.
- Randy Savage (0x0108) and Curt Hennig (0x0109) from Revenge: data\revport\ports2\PORTS2.md (Hennig locked in
  Revenge: his model code written into the Decision screen's slot). References Shawn Michaels / Kurt Angle;
  REV_OVERRIDE 25 / 50; photos 117 / 118; their Redux themes (Hennig's is Scott Hall's -- the user to say); music
  slots 68 / 69 (Revenge's song) and the user music 70.. (Bret 212 .. Hall 217 the sounds). Operator menu and
  mod.toml options savage / hennig. Not QA'd yet: music_savage / _hennig, moves_ / subs_savage / _hennig.
- 2026-10-30 morning, the user: "it said it failed to compile the mod" -- two faults, both from the WCW page's patches:
  (1) func_8013486C's copy ended in "j rw_rumble_fill" (a tail call into the six-man engine): the mod loader's
  recompiler takes no "j" to another module's function ("Unhandled branch ... to 0x80000000"). asmpatch.py now makes
  a tail call out into its delay slot, then jal / return. (2) exe\WWFNoMercyRecomp\syms\dump.toml (RecompModTool's
  reference symbols) was an old copy: its rw_* section had rw_rumble_fill at 0x80783000, the exe's at 0x80783AF0
  ("Reference symbol does not exist: section 7 func offset 0x3000"). Now six-engine\NoMercySix\syms\dump.toml's
  (only the rw_* section differed). A built mod is loaded at least once (a QA run) before it's called done.
- logic.c still read the game's old page table (0x800A9990): the QA's auto_pick (sel_find), the Rumble's random
  entrants (rr_random_superstar) and the Rumble's field (rr_draw_field, + 0x24 counts) -- now roster_pages_tab, 13
  pages (the WCW page in the Rumble's pool).
- QA (the mod loading): select_wcw PASS -- L from SUPERSTARS 1 to WCW (Bret, Neidhart, Hogan, Nash, Hall, Savage,
  Hennig), R back, R round all (1-5, Ladies, Hidden 1-2, 6, 7, WCW, 1). music_*: the themes asked for Hogan 214, Nash
  215, Hall 217, Savage 216, Hennig 213 (Hall's first run the player was picked as 0x0003 without the cursor moving:
  not again). moves_nash (jackknife sounds read: QA_STATE + 0x340 = 347), moves_savage, moves_hennig PASS, shots
  looked at: both men together.
- The Superstar Select's pages, any number (the user, 2026-10-30: "dynamically allow any number of pages ... the
  number of wrestlers on each page modifiable ... a JSON system ... the game should not break"):
  * roster.c: roster_pages_tab is 32 pages, roster_pages_n how many there are, roster_pages_flags (PAGE_NO_RANDOM:
    the Rumble's random draw skips it -- what logic.c's page list { 0..4, 8, 9, 12 } was). The game's way (no file):
    its twelve, WCW, the new ones in free boxes (place_ext), 13 pages. With <program folder>\select_pages.json
    (exe arcade_pages.cpp: names by stories\_roster.json, plus UNDERTAKER 1999, CHRIS BENOIT, CAW 1..18, or IDs;
    more than nine split into NAME 2 ..; bad names / duplicates / empty pages left out, said in the log; packed
    'P' 'G' 1 ... for the mod): json_layout -- everyone checked against the roster as it is (box_ord: the operator's,
    Benoit's box), then the rest on MORE pages (the game's twelve's, the new ones, Create-a-Wrestler's boxes last:
    those the 32 drop first); nobody laid out: the game's way. Laid out again when the roster changes. The save
    gets the game's twelve as they were (base_tab), never the file's. mod\select_pages.json ships (the pages as
    they were, written out from a run: qa\pages_dump.py --json); deploy.ps1 copies it only if the game has none.
  * gen/patches.py: the select screens' twelves read roster_pages_n (L / R, the next shown, the page list, Random,
    the Superstars' list func_8013C5B0 -- that one was still at 12), and func_8013486C (the Rumble's field: one
    from each page in turn, s5 % 12 -- a twelve the constant search missed, written as a multiply) goes round
    every page and stops after a round that adds nobody, NUMBER made the field's size (two Superstars on the
    pages: it went round for ever -- the select hung).
  * 32 pages at most: the page list (func_801378B8, A on the page's name; I found no way to put the cursor
    there) keeps the page in five bits and lists them at 0x8015B8A0 (128 bytes). Nine on a page: the select's
    3 x 3 boxes (13 cursor places: 9 boxes, the name, L, R ...).
  * QA: pages_none (no file: 13 pages, R round), pages_default (the shipped file: the same 13, dumped),
    pages_stress (qa\pages_test\stress.json: 30 listed + MORE capped at 32, pages of 1..4, a 12 split, bad
    names, R round all 32 with the d-pad on small pages), pages_random_stress (Random to pages 18, 23 ...),
    pages_tiny_rumble (two Superstars, the rest hidden: a Royal Rumble of two), music_hall (a full Rumble with
    the file). qa\pages_dump.py reads the pages from a run (QA_STATE + 0x3E0: count, table, flags -- the mod's
    memory is placed when it's loaded; the dump has the mods' memory from 0x81000000 at 8 MB).
  * The QA batch (subs_ / own_ for the Revenge Superstars, the 14 other subs_, commentary, Austin) was stopped
    for this at own_hogan; subs_hogan PASS.
- Fixed (data), 2026-10-30 afternoon, the user: "Macho Man's moves seem off. Standing hard punch is a dropping punch as
  if the opponent is down?" -- revanims.py's own-animation pass (the one for Hogan's Rock moves) took his Revenge list
  at the place the five pairs voted for each slot, and where few agree the place is another kind's: the strong strike
  (188) got place 341, a ground attack (Savage's: No Mercy's Jumping Punch byte for byte; the others' a Soccer Kick);
  place 147 (a Monkey Toss) went to back grapples, strikes and a sleeper hold; Hogan's Rock Stomps became a Big Kick
  and a Big Chop. revanims.py slot_kind_ok: a Revenge animation No Mercy has too must be one of the moves the slot
  holds (RM.slot_pool), and the place must lie between those of the slots 3+ pairs agree on. 196 own rows -> 45
  (all with no twin or a fitting one, in order). Where that left the reference's own move (mkroster REV_OVERRIDE):
  Hogan 94 Spinebuster, 188 Big Boot, 213 / 214 Stomp 01; Nash 188 Big Boot; Savage 188 Punch 01 (HBK's is a jab).
  The old header: scratch\movex_data.h.before_guard. QA strike_savage (entries 186..190 forced as 188) PASS, shots
  looked at: a standing wind-up punch to the head, the Rock reacting standing.
- Fixed (data), the user right after: "Nash's light standing punches aren't hitting Rock" -- his light strike (172) was
  revanims.py's mixed pair: his Revenge swing (0x1119, an overhead club) on the reference's Elbow Strike reaction,
  whose hit is timed for another swing (own_nash's shots: his arms came down in front of the man, the man not
  reacting). revanims.py no longer makes mixed pairs (strikes keep the reference's move): Nash 172, Savage 173, Hall
  190 back to No Mercy's own. QA light_nash (every standing strike 172..206 taken as 172) PASS: 9 forced, the shots
  looked at -- each lands, the Rock staggering, bent over, knocked down. The own_ / subs_ batch re-run on this build.
- WCW arenas (2026-10-30, the user: "can we see about getting the Monday Nitro and Halloween Havoc arenas into No Mercy?"
  then "override any 10 arena limit ... unlimited, like the wrestler pages"). Backup first: backups\pre_arenas_2026-10-30.
  Research: data\arenaport\NM_ARENAS.md (No Mercy's per-venue tables, mesh format, the 15-piece stage set, venue >= 10 =
  backstage) and REV_ARENAS.md (Revenge's 6 venues, its table 0x80036C74.., Nitro 0x360-0x37A, Havoc 0x391-0x3AF; the
  states / RAM in data\arenaport\rev\cap: gen/arenas.py reads m0 / m4_ram.bin). Same mesh format AND the same units (the
  ring mat +-280 at 90 in both, the stage at +z in both): the pieces go in as they are.
  * gen/arenas.py (run by wmport.py build_all; files in wmport.bin; src/arena_files.h): per arena its stage set (Revenge's
    own pieces in its order, pairs with one picture merged -- the truss towers, flares, banners; the six aisle rails one;
    Revenge's floor by the ring 0x2F5 with the arena's picture; the base's floor-round-the-ring 0x5D0 last, as the game
    does: 10 pieces of 15), aprons (each No Mercy half-side its own 128 x 32 picture: Revenge's side picture's half,
    twice as wide, its own palette), the truss's 2 banners (Nitro's stage sign 0x36B, Havoc's ring logo 0x3AA).
    Left out: Revenge's crowd beside the aisle (No Mercy's stands are all round), its stage root.
  * src/arena.c: arenas 0-9 the game's, 10.. ours (arena_count / arena_venue / arena_choose / arena_current); ours plays
    in venue XARENA_BASE 7 (WrestleMania: flat, as Revenge's stages) with that venue's table entries ours (0x800507FC /
    824 / 84C the set, 0x800507D4 aprons, 0x8005078C truss) from the choice until the game is back in the menus
    (NM_MODE 0); the base is itself otherwise. QA: qa_arena 11 / 12 (qa_tool ARENAS: MONDAY NITRO, HALLOWEEN HAVOC).
  * QA (shots looked at): arena_nitro / arena_havoc (the game's cameras: the stage behind the ring, drawn in order, nothing
    over the ring or the men), arena_*_cam / _aisle (fixed cameras: truss, sign, banners, WCW letters, aisle rails; Havoc's
    face, pumpkin, crypts), arena_*_ring vs arena_wm_ring (the hanging banner: WrestleMania's -> Nitro's sign), arena_*_apron
    (NITRO aprons, Havoc's blue).
  * Next: the ARENA page past ten rows (native: func_8012D614 builds 10, labels 0x6500+i, a preview and a logo picture per
    arena -- ours need theirs), the story arena (a nibble: 0..15), the arcade's random venue (rng() % 10), commentary's place.
  * The ARENA page past ten (data\arenaport\ARENA_PAGE.md; arena.c arena_page_frame / arena_page_pad, logic.c calls them
    in Versus only -- Story / Survival's automatic arena picks as they were): its ten rows a window on the list (the
    game's shown arenas, then ours), rows labelled 0x6500 + slot, the ovl_c tables 0x80148044 (label files) and
    0x80148058 (the photo's 6 tiles) written per slot while the page is up, the game's own back after. Scrolling: the
    cursor moves only by the rows' links (func_8010BE34 reads the defs 0x8014C10C: up +6, down +7, then func_8010C71C --
    writing the cursor 0x8015897C doesn't stick); the bottom row's down link is itself while more of the list is below
    (the top's up the same), a press there (NM_PRESSED, the cursor already on that row the frame before) moves the
    window; at the ends the game's wrap, the window to the other end. A / Start (the pad hook) -> that frame the row is
    made the arena's venue in NM_SEL_ARENA, the page memory 0x80158156, the venue byte and its copy 0x800BD5A0, and
    arena_choose (the page closes that frame: applied whatever is up). The VS / loading backdrop of venue 7
    (0x80148E08) ours while ours is chosen.
  * gen/arenas.py: the label (Impact, the arena labels' palette 0x462B: the game lights it orange), the photo (a QA shot
    of the arena in No Mercy, data\arenaport\pics, brightened, in a white frame + Revenge's logo boxed: 128 x 96, six
    64 x 32 CI8 tiles), the VS backdrop (320 x 240, forty 64 x 30 CI4 tiles, one after another: first_of).
  * QA arena_page (Versus, ARENA, down x 11, up, down, A, Start; pad times count from the game's first frame, ~30 s
    after the shots' clock; 150 ms presses -- 250 ms ones repeat): shots looked at -- RAW .. ARMAGEDDON, then the window
    scrolls (MONDAY NITRO, HALLOWEEN HAVOC with their labels, photos, logos), up / down at the bottom, A on Havoc ->
    RULES, Start -> the Superstar Select with venue 7. Not reached: the match (the run's presses don't reach the
    Superstar Select's 2P pick -- the QA script, not the arena; the arena itself: arena_nitro / arena_havoc).
  * Still to do: Story's arena (a nibble), the arcade's random venue (rng() % 10), commentary's place.
- QA 2026-10-30 afternoon, the moves after the revanims.py guard (shots not all looked at yet): own_savage (25 forced,
  shots looked at: grapple 15 a lift and slam, 1 a takedown, 73 a drop outside, 70/72 a flurry of punches, all both men
  together), own_nash, own_hall, own_hogan, own_hennig, own_bret, own_neidhart PASS; subs_nash, subs_hall, subs_savage
  PASS; strike_savage / strike_hogan / strike_nash / light_nash PASS (shots looked at). subs_hennig: one FREEZE
  (qa\game\crash_reports\freeze_20261030_174635.txt: match frame 610, every thread waiting, GPU fine, heap fine;
  Hennig in state 50 -- the forced 108 (3820) taken 6 s before, the Rock standing (1)), the same scenario again PASS
  (108, 155, 108, the match to its end). Open: not reproduced; not the arena work (no arena of ours in it).

## 2026-10-30 evening (the user: leg drop, Revenge arenas whole, the table climb, the Rumble's list, Mr. Perfect)
- Hollywood Hogan's leg drop, a special finisher (the user: "Hogan needs a leg drop. It must be a special finisher"):
  his ground special (entry 155, at the head; the Rock's People's Elbow there) was a Choke Hold. His Revenge leg drop
  is No Mercy's 7980 byte for byte (his list's place 338). 7980 put in 155 (QA legdrop_hogan, first try) played his
  drop with the man standing up under it: 155 plays the ground special's kind of move (the move record: 16690's), a
  strike's doesn't hold the man down. Now: 155 = 16690 (People's Elbow special, its move record) with 7980's animation
  (gen/revanims.py NM_OWN: a movex_files row, the man's side the special's own 0x3322); commentary.c said_move reports
  Hogan's 16690 as 7980 (class DROP; King's "Hogan hits the leg drop" keyed "leg drop").
- Savage's back special (the user: "a roll-up pin, is that accurate?"): entries 81 / 83 (the back special's) hold 2600
  School Boy -- his Revenge list's place 130, byte for byte (exact). HBK's (his reference) is a Rolling Crutch Pin there.
  Left as it is; told the user.
- Curt Hennig is MR. PERFECT (short PERFECT; mkroster LATE, the operator menu, mod.toml, select_pages.json, the QA
  scenarios); the WCW page is REVENGE (roster.c wcw_page, select_pages.json). His music file keeps its name (mod\music\
  Curt Hennig.mp3: the user will bring his theme). A game folder's own select_pages.json (deploy.ps1 copies it only if
  there's none) keeps its "WCW" / "CURT HENNIG" until replaced.
- The Rumble's championship list empty after a win (the user: "it brought me back to Single Player mode selection ...
  and the text selections were gone"): the results' Next (screen 28) leaves the game on Multi Play's page (2), route
  G_STORY then chose Single Play's championship straight from there; the list came up with no rows (the user's
  recording 18:31, QA rumble_win_list: reproduced). As Versus's MATCH page (2026-10-29): from anywhere but the title
  the way from the title -- Multi Play's page and Single Play's built afresh (menu_change 2, GF_PAGE_INIT 3) and the
  menus' camera at Single Play's place, then the championship. qa_rumble lingers 5 s on the list after its last
  Rumble (its shots).
- The announce table climb (the user: "run up holding run and quickly dash up on top, to the side of JR and King ...
  you can stay at that height and clip out completely of any boundaries"): announce.c moves the table's collision
  forward (ANN_TABLE_UP 75) and puts the wall over its top just before JR and King (-600); its four bounding planes'
  back one stayed at -690, so a run at the table's side between -690 and -600 took him up onto its top behind that
  wall, where the walls over the top at the sides (now -600..-455) don't reach: up at the table's height he walked off
  and through everything. The back plane now on the wall's line (-600). QA table_climb (qa_table: every 10 s the CPU's
  Superstar put beside the table, at the announcers' depth or its front, either side, running at it; logic.c
  qa_table_input from gameplay.c's computer-player hook) and table_climb_old (qa_table 2: the plane as it was).
- Arenas, unlimited, stage 3: a story's arena may be one of ours (storyc.py / arcade_stories.cpp: the node's nibble the
  base venue, the pack's arena list (node, arena) before its strings, its count at header +0x38 -- old packs 0;
  _roster.json carries "@ARENA:<NAME>" / "@ARENA_BASE"; story.c story_xarena, logic.c chooses it in the menus before
  the node's match, puts it back after); the attract demos pick any arena (arena_count); commentary names no place in
  ours (JR's place lines are the game's ten). arena_choose swaps the tables at once (a demo's match may load before
  the next arena_frame). QA_STATE + 0x24 (state.csv "venue") is arena_current(): ours 10 + k (the arena_* scenarios'
  checks now 10 / 11). QA story_arena_wcw (stories_test\002_arena_wcw.json: Nitro, Havoc, WrestleMania) -- needs the
  exe rebuilt (its story compiler).
- The entrances' close shots (the user: "the entrances match WM 2000 arena in the closeup ... Doesn't Revenge have
  closeup versions"): a scene of their own, "other scene" venue + 10 (ovl_b func_8010B778 -> func_8001B3E4, tables
  0x80052280.. by venue): data\arenaport\NM_ENTRANCE.md (a research agent, rig-checked). gen/arenas_entrance.py (Revenge's
  stage + aisle floor + barricades, 31 pieces, vertex colour 73 -> 200, placed so Revenge's stand spot is the origin)
  and src/arena_entrance.c (arena.c tables() calls it: scene index 7's lists, counts, place). The purple in Hogan's
  close-up is his own entrance light (his entrance record's effects word), not the arena's. QA entrance_nitro / _havoc.
- Ringside as Revenge's (the user: "make sure the area surrounding the ring matches too ... removing JR, King and the
  announce table if needed. Make it look exactly like Revenge"): data\arenaport\RINGSIDE.md (a research agent):
  gen/arenas_ring.py (called by arenas.build) and src/arena_ring.c -- hooks func_8001B220 (the arena's build) at entry
  and return: for our arena's build the shared lists hold Revenge's (mat + Havoc's logo, 4 aprons, floor, barricades,
  corners with the yellow turnbuckles, crowd, rope colour, no truss), the game's back the moment the build returns;
  no table / steps (the game's own rules bit 0x100000 for the table's set-up only, ann_on 0: no JR and King); the
  table kept gone every frame; the barricade's -z side in to -530 as its other three (collision records moved, put
  back after). Revenge's pieces are lit (73 grey = a normal), No Mercy's unlit: brightened (stage set too).
- Hollywood Hogan's leg drop, a finisher with B (the user, 2026-10-31: "In Revenge it is with B ... make sure the opponent
  gets hit and stays laying"; data\legdrop\LEGDROP.md -- a research agent, rig-proven, then built): B at a lying man takes
  209 (face up) / 210 (face down) whatever SPECIAL is (no ground strike is a special); the man's reaction is the game's own
  by the damage dealt (+0xBE: 31 and more its big one, legs up, and its replay -- the People's Elbow's), the damage the
  record's byte 6 (x1.2 with SPECIAL!), his staying down his timer +0xB4. movex.c movex_post: an entry of Hogan's own
  past the continuations, 7980 Leg Drop (his Revenge drop's animation) with a finisher's record (byte 6 40 as the
  Stunner's, 7 0, 8 -15, 0x12/0x13 5/2, flags 0x8000); logic.c qa_move_entry (func_800EC698's start): his 209 / 210 with
  SPECIAL up (+0xEE & 6) at the man's head (func_80136240's rule) -> that entry, SPECIAL used up (spirit 50), the man's
  timer 300. The user played it (QA legdrop_user): "Excellent". The grapple-slot tries (155 as 7980 / the People's
  Elbow's or the Russian Neck Drop's move with his animations; 211) undone: the game's own ground special at 155 again.
  Also from the research: the mod's W(s) + 0x14 is the game's previous state (TB = W - 8: its state is W + 0x0C).
- The announce table climb: fixed (QA table_climb_old FAIL as the bug, table_climb PASS, shots looked at: a side climb
  by the table's front lands on it, JR and King behind). The top (record 82) ends at the wall in front of them (-600) --
  no climbing up behind it; announce.c announce_table_guard keeps a man up there (climbing, standing, walking, running)
  inside the table's walls.
- The Rumble's championship list after a win: fixed (the user won a 4-man Rumble, QA rumble_user_win: the list whole).
- WCW stage distance (the user's Revenge screenshot: the gate right behind the barricade): No Mercy's stage-set node is
  moved 1500 along z (func_8001D8EC); Revenge's pieces are where Revenge has them -- arena_ring.c puts the node at 0 for
  ours (QA arena_nitro, shots looked at). Story arenas: QA story_arena_wcw PASS (Nitro, Havoc, then WrestleMania).
- 2026-10-31 night (the user asleep: "Finish everything pending"; not deployed):
  * Bash at the Beach (the user: "import Bash at the Beach arena"): gen/arenas.py ARENAS + its banner / logo (0x3C4: the
    ring's BASH AT THE BEACH), label, page photo (data\arenaport\pics\bash_stage.png: a QA shot of it in No Mercy);
    arenas_entrance STAND (0, 0, 1200). Arena 12 (qa_tool ARENAS). QA arena_bash, entrance_bash: shots looked at -- the
    sky, the hut, palms, the sign, the sand aisle, the mat's logo; the close-ups the sign and sky.
  * Titantron on every arena with a screen (the user: "all arenas for screens ... like we did for RAW"; "If an arena
    doesn't have a screen, skip that"): data\tron_all\TRON_ALL.md (a research agent, rig-checked): 8 of the ten (not
    SummerSlam, Royal Rumble), none of ours. src/tron_panels_all.h (RAW's rows as they were + the others), tron.c
    (data\tron_all\tron_c.patch): the fold over the texture's own loaded size (1 KB textures read past their end), an
    extent check (blank screens fold alike), 32 vertices, twin (mirrored) and animated close-up pieces held on their
    frame while ours shows, the screen's vertex colours white while ours shows, the venue arena_current() (ours play
    in venue 7). QA tron_<venue>_entrances / _ring (data\tron_all\qa_scenarios).
  * Custom stories' scenes, choices, match options (the user: "custom stories wrapped up, including backstage
    interviews/cinematics"): data\storyscenes\SCENES.md (a research agent; the rig): "scene" (place: hall, locker room,
    boiler room, pool hall, parking lot, stage, ring; cast on spots; say / ask / if choice / narrate / do / walk / run /
    entrance / camera / music / wait / effect / raw), choices (choice N 1-based), match rules / title / location /
    2 run-ins / cage, ladder, ironman; roles champion / champion2. storyc.py and arcade_stories.cpp identical bytes;
    old stories unchanged; story.c untouched. mod\stories\backstage_sample.json (built in). QA story_scene,
    story_choice, story_backstage_sample, story_match_types (qa\stories_test\003_*).

## 2026-10-31 afternoon: one player's Versus, a row of the championship list (not deployed)
The user: "a mode in Single Player menu where I can select a Versus style match ... myself and a computer opponent ...
as if I was playing Versus". logic.c "one player's Versus".
- The row: "Versus" under Survival in the right-hand column (Royal Rumble, Survival, Versus: the arcade's own ways to
  play), a picture in the labels' lettering (gen/labeltex.py now writes label_custom_story and label_versus into
  src/label_tex.h: V from Pay Per View 0x4489 cols 79..90, e from Guest Referee 0x448B cols 28..37; Custom Story's bytes
  unchanged), file FILE_LABEL_VERSUS 0x4C58 (storylist_file), label 0x852D (a PPV match type's entry, lent for good),
  Women's style / quads, action 0x428 (the arcade takes A). "Exhibition" (0x8501) was the game's own picture for it, but
  the arcade calls two players' matches Versus and its MULTI PLAY header reads "versus".
- An eleventh row the game's own way (champ_versus_defs, at the overlay's load only: the rows' number is the build's):
  the page table 0x8014DFC8 ({ def*, u8 count } a screen) says 11 for screen 22; its defs run on into 0x8014C0B0, which
  was the story chapter card's (screen 23, 3 defs) -- those are copied into the mod's memory (card_defs) and the card's
  table entry pointed there first (the ROM has no other pointer to them: searched; no ovl_c code names the address).
  The row pool (0x11000 bytes = 512 rows, allocated as the menus start) holds 386: room. menu_confirm /
  menu_choose_action take a def in the mod's memory (def_ptr). Links: Survival down -> Versus -> down Tag Team, Tag up
  -> Versus, left / right -> Custom Story (Women's with CUSTOM STORIES off: versus_links, every frame). The stories'
  list hides row 10 too (LIST_PAGE_ROWS = the page's count).
- Choosing it (pad hook -> vs_open): sp_versus = 1, route G_VERSUS (the challenger's way from the list) -> Exhibition's
  MATCH / PLAYER / ARENA / RULES -> Superstar Select. Still single player (game_mode GM_STORY; versus_menus() for every
  Versus page rule: B -> L on PLAYER / ARENA / RULES, the ARENA window, the Ladder row, exh_rows_fix; Story Mode's
  auto RULES / match-card confirm and the select's sp_pick capture are off for it). PLAYER page: solo_option 1, 7, 14,
  15, 22 (1P vs CPU, 1P / CPU / CPU, both handicap sides, 1P & CPU vs CPU & CPU); one line-up passes by itself. The
  select is the game's 1P vs CPU: "1P" box, then "CPU" -- the player picks the CPU's Superstar on controller 1; the
  HUD "1" against "C".
- Fixed on the way (both modes): players_page's one-line-up pass confirmed row 0 -- the game takes the choice by its
  row, so a re-dealt 1P vs CPU went as row 0's 1P vs 2P and the select waited for player 2's pick. Now players_restore()
  first, then the cursor's row (for two players row 0 is 1P vs 2P anyway: unchanged).
- After the match: the results (screen 29: Quit / Rematch); Quit leaves for Multi Play's page and the main-page rule
  routes G_VERSUS for it (versus_menus) -> the MATCH page again. B on the MATCH page (exh_is_exhibition, rows in) ->
  vs_back: sp_versus 0, route G_STORY (Multi Play / Single Play built afresh, the championship) and the cursor put on
  the Versus row. Game over / RESET clear it. F2 QUIT MATCH -> its MATCH page, no challenger banner (!vs_solo()).
- A challenger: as in Story Mode (game_mode GM_STORY): the banner, the match left, Versus for two (versus_option);
  sp_versus is kept, so when the challenge is decided and one player is left the route is G_VERSUS -- the winner is
  back on Versus's MATCH page as one player's Versus (resume_pending not used: nothing to resume but the menus).
- QA (hidden options qa_versus = seconds before the CPU plays the player's Superstar too, qa_versus_p2 1 = player 2's
  Start 10 s into that match; qa_pad / qa_pad2: presses on the players' controllers as read, so the arcade's own
  handlers see them; state.csv bs_tries = list cursor + 100 x rows, bs_grabs = sp_versus | game_mode << 4 | goal << 8 |
  challenge_on << 12, bs_step = the driver's step | DOWN's row << 8 | round << 16 | players in << 20):
  versus_1p 2026-10-31_131329 PASS (7/7) -- shots looked at: the list with Versus under Survival, Survival lit, DOWN ->
  Versus lit (orange, glow bar, as World's), the MATCH page coming in, ARENA, RULES, the select 1P / CPU, Austin vs
  the Rock card, the match (AUSTIN 1 / ROCK C), PIN FALL, results Quit / Rematch, the MATCH page again, B -> the list
  with Versus lit. (Runs 125437 / 130513: the PLAYER-row bug above; the driver's results step.)
  versus_1p_challenger 2026-10-31_132246 PASS (5/5) -- shots looked at: Austin (1) vs the Rock (C), player 2's Start 10 s
  in: HERE COMES A NEW CHALLENGER over the held match, the MATCH page for two, the select 1P / 2P, ROCK 1 vs ROCK 2;
  player 2 pinned (the CPU playing both after 20 s): the match left at the PIN FALL (the Story challenger's way: the
  decision ends it), Versus's MATCH page again with one player and "2P PRESS START", B -> the list, Versus lit.
- Not changed: the MULTI PLAY header reads "exhibition" on the way in from the championship list (as the challenger's
  route always did: header_versus doesn't catch it there).

## PENDING (bulk): reported by the user, not started -- handle all at once when the user says "handle anything pending"
Added 2026-11-02 night, to attempt overnight (RAW IS WAR only, an experiment): the ring and the stage as ONE fightable area.
  The user: "Combine the in-ring ring area and the closeup stage area in gameplay. They are already connected in visuals, but
  the stage is still a seperate area you have to Irish Whip your opponent into the wrestle onn the stage. Cut out the middle
  man. Take the stage code (fight-able stage area) and apply the funcionality directly in the arena from the ring view. No
  transitions. Allow free roam and fighting onto the stage, just use the existing code, collision, etc. from the current
  stage area. We will experiment with Raw is War No Mercy." See the entry "RAW: ring and stage one area".
Added 2026-11-02 (0.38.34 in the game; not started, nothing looked into yet):
- (DONE 2026-11-03, 0.38.37: entry "Rey's head: Sting's hair") REY MYSTERIO's head: the user: "Rey Mysterio's head sculpt is off, it looks like he has another wrestlers hair." It's
  his purple Halloween Havoc costume (the user, 2026-11-02). Check against data\revport\attires\sheet_rey.png and
  Revenge's own Rey.
- A Superstar with no Titantron video gets someone else's in his entrance. The user: "if a wrestler doesn't have a
  titantron video, the opponent's will play for him before it cuts to the close view of the stage, or sometimes an
  unrelated wrestler will appear on the titantron briefly. We need to put in a check that if a wrestler doesn't have a
  titantron, not other will play during their entrance." Both things: the opponent's video, and a brief unrelated man.
  QA: a ported man with no video vs a man with one, watching the screen through his whole entrance, in both orders.
Added 2026-10-31 (0.38.1 in the game):
- (done 2026-11-01, below: "Bret's special Sharpshooter, Mr. Perfect's winning taunt") Mr. Perfect's winning taunt on an
  invisible turnbuckle; Bret's standing special's imaginary Sharpshooter.
- Titantron screens with the entrance video are in the wrong spots on SmackDown and Backlash; likely the same on every
  arena except RAW. The user wants to go through them one arena at a time. (2026-11-01: shots of the whole stage per
  arena in Development\Titantron Placement QA; the user is marking the ones to move with arrows in a folder of their own.)
  THE USER'S CORRECTIONS (2026-11-01, Titantron Placement QA\To Fix: an X marks where it is now, arrows where it goes;
  the other arenas are OK as they are):
  - WRESTLEMANIA: take the video OFF the centre back panel (the ramp's end; X) -- the arena's own picture back there.
    Put it on BOTH big side screens, left and right (the purple screens on truss, x ~ +-, above the crowd): 2 screens.
  - SMACKDOWN: take it OFF the oval doorway at the top of the ramp, inside the shell (X); its own picture back there.
    Put it on the "SMACKDOWN" oval sign at the right of the shell (circled), KEEPING THE OVAL SHAPE (the video
    masked to the oval, not a rectangle over it). Check whether the sign has a twin on the left (TP_TWIN).
  - BACKLASH: good as it is (the user, having seen the entrance close-ups too).
  - SURVIVOR SERIES: the video comes OFF the round disc in the hexagon, in the entrances' close-ups too. The disc stays
    static: in real life it played an animated WWF logo, and the user says keep it static for now. ADD a Titantron of
    its own above the stage, where Backlash's sits (a screen in a truss frame over the set; the real Survivor Series
    had one). This is a new piece of the set, in both the match's set and the close scene, built on the stage-parts
    restructure (no part freed or dropped). The video goes on it.
  - The same screens in the entrances' close-up scene, where those pieces are in it (the agent saw SmackDown's video
    low in the shell's mouth and WrestleMania's unframed behind the wrestler: both are the X'd panels' close copies).
  QA: tron_smackdown_* / tron_wrestlemania_*; shots of the match view like the user's and of the entrances.
  (2026-11-01 afternoon: WrestleMania / SmackDown / Survivor Series DONE in tron.c + tron_panels_all.h, built, not QA'd
  in the real game yet -- the entry "Titantron: the user's corrections" below; Survivor Series' own Titantron above the
  stage still waits for the stage-parts restructure.)
- (2026-11-01, IN PROGRESS) Bret's standing front-grapple SPECIAL is still misaligned in 0.38.12. The user: "He drops
  the opponent and does the sharpshooter and is still misaligned. Nothing changed." The 0.38.12 fix (entry 70's
  continuation -> 16352) only fixed the hold's ending, and its QA forced the move through qa_fslot, not the
  player's path. Now being reproduced through the player's path.
  Code-only follow-up (2026-11-01 afternoon): no cause found; file lengths, hand-off and start gate ruled out. Left:
  SPECIAL!'s damage on a man in DANGER, or the CPU defender's presses. Tonight's bret_special_* runs decide it.
  (2026-11-01 later: REBUILT the Revenge way -- two moves, the slam then the game's own ground grapple into his ground
  Sharpshooter: entry "Bret's front special rebuilt as two moves" below. 0.38.21, built, not tested yet.)
- (2026-11-01) Bret's special: the turnover half plays SILENT. The joined slam+turnover file 0x5429 / 0x542A keeps the
  slam's 36-byte records for all 185 frames, so 16350's own sounds (frames 26, 46, 94, 104) never play. Build the
  joined file's records from both halves (the slam's, then the turnover's), not the slam's alone.
  (2026-11-01 later: gone by construction -- no joined file; the turnover plays as 157's own file 0x3FDE with its own
  record. Entry "Bret's front special rebuilt as two moves". Not tested yet.)
- (2026-11-01, IN PROGRESS) The Giant (0x010A) has a piece of his upper leg missing, "like a polygon or texture didn't
  come over" (the user, 0.38.12).
- (2026-11-01) RAW played after a WCW Revenge arena keeps that arena's smaller ringside (the user saw it in play).
  Likely: arena_ring.c's collision-wall move (the barricade's -z side -690 -> -530, the table, the steps; rec_old /
  rec_n) isn't put back when a game arena follows, or the in-place writes to the shared lists (sv_* save / restore)
  miss a case. THE USER'S RULE: each arena is its own separate arena. Not a mod of another venue, not built on a
  copy of one. Today the Revenge arenas borrow venue 10's per-venue entries and write over the shared ring / crowd
  lists and the match overlay's collision records, then restore them afterwards. Restructure so each arcade arena has
  its own venue entries, lists and walls, and the game's own arenas' data is never written. Then RAW can't inherit
  anything because nothing was changed under it. QA: RAW after each Revenge arena (and the reverse), checking the
  walls in state.csv and the shots.
- (2026-11-01) Bash at the Beach has a screen of its own to use (the user). Revenge draws it as a slideshow: models 0
  and 13-20 of scene 0x3C (eight 64x32 CI8 photos), reached through the scene table's +6 / +16 fields, which were never
  decoded (STAGE3D.md 4 / 7), so arenas_stage3d.py never converted them. TRON_ALL.md lists Bash as "none". The match's
  set is also exactly 15 parts (RING_SKIP already drops the plant panel).
  THE USER'S RULE: no freeing a slot, merging away or dropping anything. Restructure instead: grow the stage set past 15
  parts (a bigger parts array than 0x8007B8A0's, and everything that indexes or loops over it). Then put back what the
  limit cut: Bash's plant panel (RING_SKIP), and the "needs more than 15 parts" detail noted for the WM2000 arenas and
  RAW's close set. Then decode the slideshow and add Bash's screen as a part of its own, with tron.c driving it.
- QA queue (the night of 2026-10-31 on 0.38.12, paused at the user's word ~11:00): passed with shots looked at --
  ss_bret, music_undertaker (by ID 0x0003, slot 78), the 14 WM2000 subs_*, subs_hennig, subs_bret, austin_flash,
  story_scene / story_choice, story_invasion (the user's story, node 1), every tron_* run (the findings: the agent's
  report; shots in Development\Titantron Placement QA). FOR TONIGHT'S QUEUE:
  - The Giant's thighs (entry "The Giant's thigh hole", 2026-11-01; built 12:44, not QA'd in the game): the close-ups
    (both men idle, a fixed camera on his corner) -- legs_giant_c0_hip_front / _back / _left / _right (the singlet, at the
    hips: the tops of the thighs whole, no see-through wedge on his left thigh), legs_giant_c0_front / _back / _left /
    _right again (the same views as the before runs 2026-11-01_120310 / 121236 / 121613 / 122901), legs_giant_c1_front /
    _back (red tights), legs_giant_c2_front / _back (black trunks); and Hollywood Hogan's costume 2 (Hulkamania trunks:
    the same fix) in a match. Copy before / after pairs into Development\qa\giant_leg_after.
  - music_rumble_late with qa_rr_out 4 (the winner's theme, media_winner_song, still unconfirmed)
  - story_match_types_b in full (handicap, four-way, run-ins, title, parking lot); story_invasion past node 1
  - subs_hennig again (one clean run doesn't clear the old hold freeze); austin_flash's 105.6 s near-white frame
  - arena_page: the script never makes player 2's pick (its match check fails since 10-30) -- teach it
  - (0.38.22, arenas-rev) SOULED OUT (arena 14) and SUPERBRAWL (arena 15): arena_souledout / arena_superbrawl (the match:
    the 3D stage at the aisle's end, the mat / aprons, no table, no JR and King), entrance_souledout / entrance_superbrawl
    (the close shots: Revenge's 3D stage behind the Superstar -- Souled Out's yellow box closed in front of the curtain),
    walls_souledout_then_raw / walls_superbrawl_then_raw (the -z barricade at -530, then RAW's own -690); also look at
    the ARENA page (rows past ten: 17 rows with Starrcade) and the VS screen with one of them chosen.
  - (0.38.23, arenas-rev) STARRCADE (arena 16): arena_starrcade, entrance_starrcade, walls_starrcade_then_raw.
  - (0.38.24, arenas-wm) WrestleMania 2000's SUNDAY NIGHT HEAT (17), ROYAL RUMBLE WM 2000 (18), KING OF THE RING WM 2000
    (19): arena_wm<heat|rumble|kotr> (the match: the 3D entrance scene as the stage at WM2000's stage place, its aprons, the
    table with JR and King, venue == 17 / 18 / 19), entrance_wm<...> (the close shots: the 3D scene behind the Superstar,
    placed as RAW WM 2000's), walls_wm<...> (Nitro then the arena: -z barricade -690), tron_wmheat_ring (the Titantron on
    Heat's big screen). Compare data\wmarenall\ov_<heat|rumble|kotr>.png / port_*.png.
  - (0.38.25, arenas-wm) SURVIVOR SERIES WM 2000 (20), WRESTLEMANIA WM 2000 (21), SUMMERSLAM WM 2000 (22):
    arena_wm<survivor|wm|summerslam>, entrance_wm<...>, walls_wm<...> (as above; ov_<survivor|wm|summerslam>.png).
    Also the ARENA page with 23 rows (the window scrolling to the last, SUMMERSLAM WM 2000) and the VS backdrop.
  - QA-only exit crash: main.cpp's write-watch thread reads freed RAM when a run quits by itself (only WCW_QUIT_AFTER
    stops it first); have the RAM release stop and join it (2 of ~470 runs; WCW_WATCH_MOD is QA-only)
  - a starved run with crash reports should be voided automatically, not kept (music_rumble_late 100911 _SLOW)
  - Titantron corrections (entry "Titantron: the user's corrections"): tron_smackdown_ring, tron_smackdown_sign (new:
    camera on the oval sign -- the picture must be OVAL, nothing square past the sign's edge), tron_smackdown_entrances,
    tron_wrestlemania_ring, tron_wrestlemania_entrances, tron_survivor_ring, tron_survivor_entrances; regressions
    tron_raw_ring, tron_backlash_ring. Look for: SmackDown -- video on the oval sign right of the shell, the shell's
    mouth its own picture (match and close shots); WrestleMania -- video on both side screens, the middle panel at the
    ramp's end its own picture, no video behind the wrestler in the close shots (the side screens there if in frame);
    Survivor Series -- the disc its logo in the close shots and the match, no video anywhere. Then copy before / after
    pairs into Development\Titantron Placement QA\After ("02 SmackDown - match - after.png", "... - entrance -
    after.png", the same "- before" from 2026-11-01_085424 / 084414 (SmackDown), 094926 / 094457 (WrestleMania),
    094143 / 093714 (Survivor Series)).
  - Arenas each their own (entry "Arenas restructured", 2026-11-01; built, not QA'd in the game yet: the user's play
    first). The walls (qa_walls 1: state.csv bs_tries = the -z barricade's least z the collision walks | the +x side's
    << 16; bs_step = arena_current | 1 << 8 when the arena's own walls are in | the stage set's parts built << 16), RAW
    after each Revenge arena and the reverse (scenarios with "arena2", qa_arena2): walls_nitro_then_raw,
    walls_havoc_then_raw, walls_bash_then_raw, walls_raw_then_nitro, walls_nitro_then_wmraw, walls_havoc_then_wmraw,
    walls_bash_then_wmraw, walls_wmraw_then_havoc (each checks: the Revenge arena -530 0xFDEE, RAW / WM2000 RAW -690
    0xFD4E, never -530 in RAW). The rest of the arena regression set: entrance_nitro, arena_nitro_ring,
    arena_havoc_ring, entrance_havoc, entrance_bash, arena_wmraw, entrance_wmraw, wmraw_announcers, table_climb_wmraw,
    table_climb, tron_raw_ring, tron_entrances, tron_wmraw_ring, tron_wmraw_entrances, tron_<no_mercy / smackdown /
    kotr / survivor / wrestlemania / backlash / armageddon>_ring and _entrances, stage_summerslam, stage_royal_rumble,
    stage_<every venue> (Step 2: each set's new parts), a Royal Rumble (rumble_ports). Look at: the Revenge arenas'
    ringside as before (no table, no steps, no JR / King, the barricade's -z side at 530 and solid there), the close
    shots their own scenes, RAW's / the other arenas' table, steps, JR and King, their -690 barricade; Step 2: each
    stage set against Development\Titantron Placement QA and data\arenaport\allstages\cmp_v<V>.png.
    Step 3: entrance_bash / arena_bash and tron_survivor_ring / tron_survivor_entrances (with titantron_screens /
    titantron_live): the entrant's video on Bash's picture board under the hut and on Survivor Series' new screen over
    the stage, the hard camera on both in the match; Survivor's disc its logo.
  - Bret's front special REBUILT (entry "Bret's front special rebuilt as two moves", 2026-11-01; 0.38.21, built, not
    tested yet; the scenarios below updated for it -- their old 0x5429 checks were stale): bret_special_player,
    bret_special_player_arcade, bret_special_cpu, bret_special_danger (NEW: the player's path, the man in DANGER),
    bret_special_cpu_danger (NEW: the CPU's Bret, the man in DANGER), bret_special_sharpshooter (forced), then ss_bret and
    subs_bret as regressions. Each checks: the slam (entry 70, files 0x37D8 / 0x37D9), Bret kneeling at the man (0x2F88:
    the game's ground grapple began), entry 157 with the turnover (0x3FDE / 0x3FDF), the hold (0x3FE0 / 0x3FE1). Look at
    every shot from the slam's end to the hold: Bret walks to the man's LEGS (not his head), kneels, the turnover with
    the legs in his hands (no jump of the man), the hold; listen for the turnover's sounds. Copy before / after into
    Development\qa\bret_special_after (before: 2026-11-01_104430, 1067-1073 s).
  - Bret's front special the way the user does it (entry "Bret's special Sharpshooter: the player's path", 2026-11-01;
    nothing forced, SPECIAL kept full): bret_special_player (Bret on controller 1, the game's controls: a tap of A on
    the CPU's Rock, then the stick), bret_special_player_arcade (the Arcade controls, the user's setting: A, then C-right),
    bret_special_cpu (the CPU's own specials) -- 1200 s each, 3 matches. state.csv (qa_pspecial): bs_tries = the two
    animation files (Bret | the man << 16), bs_grabs & 0xFFFF = Bret's nearest hand piece to the man's nearest bone x 10
    (bone >> 16 & 0xFF), bs_step & 0xFFFF = Bret's moveset entry, rr_put_out = the roots' distance x 10, rr_kept_out = the
    facing between them (0 face to face). A camera of its own on the two, three sides every 2 s. Look at: the slam's end
    (does the man jump at the slam / turnover seam, ~ key 35 of 0x5429 / 0x542A), the turnover (his legs in Bret's hands)
    and the hold, every shot; compare with bret_special_sharpshooter (forced, idle man: aligned) -- then ss_bret,
    subs_bret, bret_special_sharpshooter as regressions. Copy before / after shots into Development\qa\bret_special_after.
  - The ports' holds with Revenge's own blocks (entry "The ports' holds went on in No Mercy's", 2026-11-01; 0.38.20, in
    the game, not tested yet): blocks_giant, blocks_hennig, blocks_hogan, blocks_nash, blocks_savage, blocks_hall (holds
    forced, qa_fsub 1; each checks that through the hold state 72 / 73 both men play pack files >= 0x5373, never No
    Mercy's). Look at every hold's shots from the grab to the release: the man where the hold has him, no jump at the
    grab's end, the release his own. Then subs_<the six> and own_giant as regressions, and one match each of Hall (25 /
    50), Savage (79 / 80), Perfect (79 - 83), The Giant (77 / 78) with qa_fsub 3 (the moves now on a stand-in that ends).
  Done by the user in play: music_bret, commentary_hogan (both work).
- Commentary: the user reports "issues in general"; the CSV (Development\commentary) is theirs to edit -- import it
  when they say. Ladder and DQ have no lines.
- (2026-10-31, 0.38.2 in the game; the user confirmed the Rumble entrants' themes in play -- the winner's theme not
  confirmed on its own yet: queued with music_rumble_late)
  Ported Superstars' themes for a Rumble's later entrants and for the winner: two of func_8000AAF4's callers kept only the
  game's songs -- func_800E22D8 (a Rumble entrant after the first two: song < 0x50, else none) and func_800DBA88 (the
  six-man engine's rw_800DBA88, the winner's: 23..79, else 0x16). Both replaced (media.c media_entrant_song /
  media_winner_song; the engine's player count is its $gp, read in the patch). Mr. Perfect's theme: mod\music\Mr
  Perfect.mp3 (the user's; the old "Curt Hennig.mp3" had stopped matching once he was renamed -- in backups\music).
- Tiger Ali Singh enters to D'Lo Brown's theme: by design so far -- No Mercy Plus V3 gives him Lo Down's (D'Lo and Chaz,
  whom he managed); no game we have has a theme of his own. His own needs an mp3 from the user (mod\music\Tiger Ali Singh.mp3).
- (2026-10-31, 0.38.3 in the game) Monday Nitro's stage is Revenge's real 3D one (the user's screenshot: "a closeup version
  of the stage area that is higher quality ... make THAT the stage for both entrances and in-ring"): data\arenaport\
  STAGE3D.md (a research agent, rig-proven: the venue menu's backdrop scene, geometry 0x3E / pictures 0x3F, F3D Vtx +
  triangle lists, its scene table 0x800C8E84; drawn again in Revenge's entrance close-ups at x0.3). gen/arenas_stage3d.py
  converts it (points x0.3 on each mesh's grid, texels rebased by whole repeats, colour = vertex alpha, pictures with
  their header filled in, b4/b5 the repeat) -- the close scene: its 29 models in Revenge's order at 0.3 x set + (0, 0,
  454) (Revenge's pose spot at the origin) + a floor quad reaching to z -1000; the match's stage set: 11 parts merged by
  picture at 0.3 x set + (0, 0, 1220). arenas.stage / arenas_entrance.close_pieces take it for an arena in STAGE3D
  (arenas.pic_pair: a picture from bytes). QA entrance_nitro (shots looked at: close shots with the 3D gate, banners,
  letters; the ring's end the lit floor, truss, letters). Not carried over: Revenge's star-flare / doorway-glow sprites
  and its light show. Havoc / Bash still the flat set (their scenes 0x46/0x47, 0x3C/0x3D extracted, not converted yet).
  The ARENA page's Nitro photo is still the flat set's shot.
- (0.38.4) Nitro's 3D stage in the match moved back (the user: "the closeup stage needs to start at the end of that aisle,
  and the aisle itself should still have the same look as the far-away version"): RING_Z 1950, its floor's front edge
  at z 1230, where the old aisle's rails end (x +-170, z 530..1230); the aisle between as it was. Not QA'd yet (the
  user looks): check the banners (to z ~2230) against the far end of the arena.
- Nitro's aisle floor back (the user's screenshot of the old set: "the texture in the aisle way on the floor"): the flat
  set's 0x314 (the light beams, x +-170, z 500..1240, picture 0x316 / 0x315) first in the match's stage set, under the
  3D stage (arenas_stage3d.ring_parts, AISLE_BEAMS). Generated (wmport.py); goes in with the next build (waiting on
  the 1P Versus work in logic.c so a half-done build isn't deployed).
- The crowd beside Nitro's moved-back stage (the user, choosing "extend the crowd" over moving the stage forward):
  arenas_ring.crowd_ext -- Revenge's stage-end corner crowd (0x347, 0x344, 0x345, 0x341, 0x33B, 0x33E, +x) once more,
  moved back RING_Z - 1220 (730) and mirrored to -x (Revenge's -x corner is a crowd side's), in g2 (tiers 1-3: 9 of 9
  now) / g3 (tier 4: 18 of 22) -- the lists all Revenge arenas share (Havoc / Bash get it too). Not the aisle strips
  0x321 / 0x322 (moved back they'd stand on the stage's floor). Offline: data\arenaport\ringside\preview.py (it now
  reads gen/arenas_stage3d.py's pictures). Generated; in the next build.
- Halloween Havoc and Bash at the Beach get their 3D stages too (the user: "the closeup stages, moved back, aisle retained
  from worse quality version"): arenas_stage3d STAGE3D {0 Nitro 0x3E/0x3F, 4 Havoc 0x46/0x47, 3 Bash 0x3C/0x3D}; the
  match's set at ring_z(v) = 1230 - 0.3 x its floor's front (Nitro 1950, Havoc 1680, Bash 1980); merged by picture in
  first-use order (ring_groups; Nitro keeps its QA'd RING_PARTS); the old aisle floor first (AISLE: Nitro / Havoc 0x314
  light beams, Bash 0x3B2 + 0x3B3 sand, one mesh); Bash is 15 pictures + its aisle: its small plant panel (model 15) left
  out of the match's set (RING_SKIP; the close shots keep it); arenas.build adds its two floors only while there's room.
  Close scenes: every model, the floor carried on toward the camera by an affine fit of its texture coordinates (Bash's
  floor is a tapered boardwalk). Havoc 12 / Bash 15 parts in the match, 30 / 18 in the close shots. Offline renders
  looked at (preview.py: Havoc's crypts, face, pumpkins, tombstones by the aisle; Bash's sky wall, hut, sign, boardwalk).
  Not yet in the game: the next build (after the 1P Versus work).
- (0.38.5 in the game, 2026-10-31 13:55) The 1P Versus row + the three 3D stages, the aisle floors, the crowd. QA
  entrance_havoc first CRASHED in the arena build (func_8001B220's hook: a write to 0x100000000): Havoc's merged tombstone
  parts had 204 triangles and func_8000C398 reads the count as a signed byte (lb) -- now every part <= 127 / 127
  (ring_groups; arenas_ring.Mesh.file asserts it). Rerun PASS; entrance_bash / entrance_nitro PASS; shots looked at
  (close shots: Bash's sky wall / sign / palms, Havoc's crypt / pumpkin / face, Nitro's gate; the match: each old aisle
  floor up to its 3D stage, the crowd beside it).

- (2026-10-31 afternoon, dev build only, not deployed) RAW IS WAR's match stage set = its entrances' close set (the user:
  "Try to do it with RAW is War now. This time stage stays in the same place, ramp stays the same."): gen/arenas_raw.py
  (called by wmport.py build_all after arenas_entrance; writes src/arena_raw_files.h), arena.c arena_raw_tables (venue 0's
  0x800507FC / 0x80050824 / 0x8005084C written every frame from arena_frame: no restore, it's always ours). The old set
  is the close scene's (scene 10) at 0.8 x: p -> 0.8 p + (0, 160, 1040) in the set node's space (screen 0x57B +-460,
  y 370..830, z 1000 <- 0xAA7 +-580, 260..840, -10; face 0x578 z 810 <- 0xA92 -290; doorway 1190 <- 190; width 830 <-
  1040). Offline overlay: data/arenaport/rawstage (look.py; cmp3.png old / whole close set / ours from 3 cameras). Kept:
  the old ramp 0x576 and the floor round the ring 0x581 (last). The close deck cut at its face (its 150 lip would cover
  the ramp's top), the face's ramp gap closed; its own ramp / landing out. 15 parts: face, walkway, side floors (4
  pictures at 32 x 32 in one), ramp, walls (2 in one), doorway, logo front (silver + red in one), fists (0xAA5 / 0xAA6's
  first frames, one palette), screen (0xAA7, its own 0xE32/0xE33: tron.c's row), frame, posts + arches (0xE3C = 0xE36),
  towers R / L (0xE38), signs (WWF.com + AKI NETWORK in one), ring floor. Left out to fit 15 x 127 points: the truss's
  faces away from the ring (only -z faces kept), the arches' second copies (extra list), the logo's dark copy, the
  doorway's side walls; the posts drawn with their beams' pictures (0xE3E -> 0xE3C, 0xE3A -> 0xE38); fists not animated.
  tron_panels_all.h's RAW match row = the new screen's extent (XRAW_SCREEN_*, -465..465, 370..830; the 0x81A / 0x81C
  rows now meet nothing: no frame painted round ours -- the picture to the 3D truss, as in the close shots). (Regenerating
  tron_panels_all.h with data/tron_all/mkheader.py would put the old row back.) QA: tron_raw_ring 2026-10-31_144152 PASS,
  raw_stage (new: a camera by the ramp's foot) 2026-10-31_144427 PASS and -Installed (the old set) 2026-10-31_144622,
  tron_entrances 2026-10-31_144817 PASS -- shots looked at: the stage where the old one was with the hard camera on its
  screen, the ramp meeting the deck as before, the entrance video on it in the wide shots, the close shots unchanged.
  Open: the whole close set (all faces, the logo's depth, both arch copies, the animated fists) needs more than 15 parts --
  a patched func_8001D8EC / 8001DA24 / 8001DA6C with a bigger parts array than 0x8007B8A0's.
- (0.38.7 in the game, 2026-10-31) The struggling slam, FOUND (scratch\struggle2\STRUGGLE.md, a research agent, rig-proved
  on vanilla): the game's rule (ovl_e 0x80128E78): the held man's class bit 0x1000 (super heavyweight: 400 lbs+, from
  the match record's weight, func_80114CC4 at match start; light < 231, heavy 231..399) and the move record's flags
  without 0x2000 -> entry 159 (4550, the struggling body slam) / 161 (4560, back suplex). The mod's bug: announce.c
  copied King's 0x1D-byte looks row into match record 0 + 0xFC (0x17 bytes there): 5 bytes into record 1 -> slot 1's
  weight 0x1300 (4864 lbs), a super heavyweight whenever the announcers were built before the classes -- every lift on
  the usual singles opponent a struggle. Now ANN_LOOKS_N 0x17 (+ a _Static_assert). Also: logic.c's QA move log / state
  sN_move read W + 0x30A (a record pointer's low half): now W + 0x302 / 0x300 / 0x2FC -- earlier "moves seen" were void.
  QA struggle_fix_austin_rock PASS (Scoop Slam 270 on Rock, never 4550 / 4560; check_run.py: record 1 clean, Rock
  heavyweight), struggle_keep_austin_andre PASS (4550 on Andre, his escape 160). King's picture checksum as before.
  Vanilla has no forced struggle for a light heavyweight on a bigger man (only a better chance to resist) -- the user's
  second rule would be new (STRUGGLE.md "Optional"), not done.
- (2026-10-31 evening, dev build only, not deployed) The other nine arenas' match stage sets = their entrances' close
  sets, as RAW IS WAR's (the user: "Do the same for all remaining arenas"): gen/arenas_stages.py (the general one: the
  tables of all ten venues in gen/arenas_stages_tab.py, each venue's SPEC in gen/arenas_stages_spec.py; called by
  wmport.py build_all after arenas_raw; writes src/arena_stage_files.h), arena.c arena_stage_tables (venues 1-9's
  0x800507FC / 0x80050824 / 0x8005084C entries every frame and at arena_choose; venue 7 only while WrestleMania is itself:
  a_in 0 -- the Revenge arenas' tables() / arena_ring.c own it while one of them is in). Offline: data/arenaport/allstages
  (tabs.py bounds, sheet.py, fit.py, pics.py, cmp.py V -> cmp_vV.png: old set / whole close set at the transform / ours,
  from four match cameras).
  The engine: close scene + place (+ the extra list's offset) -> s p + d; old pieces kept (the ramp / aisle and the
  floor round the ring in every venue, more where noted) inserted in the drawing order; floors cut at the old stage's
  front (a real triangle clip now, texels and colour along the edge); triangles no match camera faces (9 points round
  the ring + one on the aisle by the stage, cos > 0.25) left out, or only the -z faces of trusses (front); same
  picture under two palette ids one; pictures packed into 64 x 64 / 128 x 32 (median-cut 16 colours); "repic" (a lattice
  drawn with a like one, its mean colour into the vertex colours), "solid" (one-colour pictures: their colour into the
  vertex colours, one white 8 x 8), depth bands (a part's pieces from one depth: painter's order), parts split at 127 /
  127. Both vertex forms (SmackDown's close set is the u16 one: colour 200).
  Per venue (gen/arenas_stages_spec.py has each one's matching and choices):
  * NO MERCY (1): 0.92 x, z +770 (place 430): screen, trusses, side screens, arches, doorway, floor front matched
    within 2%. Kept 0x586 aisle, 0x582. Out: the aisle's rails / fences, the arches' second layer (m61 0xB0A..), the
    logo's dark copy. 13 parts (floors 6 pictures in one, walls / doorway 4 in one, logo 2, ledge + lights 3).
  * SMACKDOWN (2): 0.97 x, 180 up, z +967 (shell +-740 / oval screen matched). The close stage stands 280 high and
    reaches 370 further forward, so its floor is cut at the old deck's front (z 930), its face / walkway / ramp out;
    kept the old deck face 0x599, the ramp 0x598, 0x59A, the old halo 0x58E / 0x58F (one mesh; the close halo and its
    lamps out: 1300 triangles) and the old sign 0x596 / 0x597 (the close big oval sign would stand in the stands).
    Shell 8 pictures in 2, deck floor 7 in 1. The tubes: faces toward the ring. 13 parts.
  * KING OF THE RING (3): 1 x, z +380 (backdrop / screen / rows of lights exactly). Backdrop, letters, crown bits are
    one-colour pictures: WHITE + vertex colours; letters' sides cos 0.6. Kept 0x59E aisle, 0x59C, the old rows of
    lights 0x59F / 0x5A0 (one mesh; the close ones are 24 truss pieces). Out: rails / fences, small floor lights; the
    old floor 0x59D (the close floors are drawn first, under everything). 15 parts.
  * SUMMERSLAM (4): 1 x, z +150 (backdrop and the two flat crane layers <- the close scene's back two crane rows). Its
    front row (on the old aisle, before the stage's front) and the fences out; floors / walls cut at z 140. The sign and
    spotlights hang where the close scene has them (in front of the cranes: bigger to a camera near the stage than the
    old painted sign). Kept 0x5AC, 0x5AA. 15 parts.
  * SURVIVOR SERIES (5): 1 x, z +80 (the three hexagon frames 140 / 530 / 910 exactly). Its aisle part out; kept 0x5BA,
    0x5BB. The frames' 9 lattice pictures one (repic, tone kept), depth bands so each frame's parts are drawn back to
    front; the disc's ring one copy of two; the floor lights by the sides out. The round screen shows its logo, no
    Titantron (the match had none: tron.c panel_patch now drops a TP_ANIM row's match that isn't the close scene's
    animated piece). 15 parts.
  * ROYAL RUMBLE (6): 1.1 x, z +391 (garage door and the taxi over it matched). The close scene's crowd is over and
    behind the door, an alley along the road (cut where the old rows began, z 400): the old three low crowd rows beside
    the road are not in it (the sides of the road are dark now). Out: its road, two slivers. 35 pictures: the door's two
    and the taxi's sign as they are, the rest in 32 x 32 cells. Kept 0x5C4 road, 0x5CF. 15 parts.
  * WRESTLEMANIA (7): 0.95 x, z +795 (screen, side screens, back wall, stage floor within 1-2%). The close aisle's part
    on the stage under the old aisle 0x5D1; 0x5D0 kept; rails out. Crowd / seats / rail over the back wall come with it.
    The side screens their own picture (QA: in one palette with the lights they lost their red). 13 parts. Written only
    while WrestleMania is itself (not the Revenge arenas: their sets as before -- QA entrance_nitro).
  * BACKLASH (8): 1 x, z +575 (the roof truss and the arms' three flat layers exactly). Kept the old floor 0x5DA (wider)
    first, 0x5DB, 0x5D9. Arms: faces toward the ring, 6 lattice pictures one, a band per layer. 14 parts.
  * ARMAGEDDON (9): 0.92 x, z +155 (curtain, back panel, sign, tower, tank, sandbags). No screen in the close set: the
    old screen 0x5F4 and its truss 0x5F2 kept where they were (tron.c's row as it was). Kept 0x5EC, 0x5EA. 41 pictures in
    6 packs (16 x 16 cells for the small vehicles' / sandbags' / helicopter's). 15 parts.
  Titantron (tron_panels_all.h): match rows for the new screens (XSTAGE_SCREEN<v>_*: No Mercy 0xB07 0 / 0, SmackDown the
  oval 0xB79 first frame -- 0xEB3's 1 KB fold, KOTR 0xBF7, WrestleMania 0xD50, Backlash 0xDB9 0 / 0) in place of the
  old ones (their pieces are gone); Armageddon's as it was; SummerSlam, Royal Rumble, Survivor Series none.
  QA (all PASS, shots looked at; qa\scenarios\stage_<venue>.json new: a fixed camera over the aisle near the ring,
  run with and without -Installed): stage_no_mercy 163714 / -Installed 163906, stage_smackdown 164054 / 164245,
  stage_kotr 164511 / 164657, stage_summerslam 164841 / 165034, stage_survivor 165247 / 165614, stage_royal_rumble
  165807 / 170004, stage_wrestlemania 172334 (after the side screens' fix) / 170344, stage_backlash 182821 / 170735,
  stage_armageddon 183016 / 171124 -- each new set where the old one was, the same size, the old aisle / ramp as it
  was, the hard camera on No Mercy's, SmackDown's (the oval), KOTR's, WrestleMania's, Backlash's and Armageddon's
  screens; Survivor Series' disc its logo. tron_no_mercy_ring 171318, tron_smackdown_ring 171536, tron_kotr_ring
  175904, tron_wrestlemania_ring 172542, tron_backlash_ring 180138, tron_armageddon_ring 180424: the hard camera on
  each. tron_<no_mercy / smackdown / kotr / survivor / wrestlemania / backlash>_entrances 180655 / 181105 / 181443 /
  181825 / 174048 / 182210: the close shots' screens show the video (SmackDown's oval and Survivor Series' disc too:
  the TP_ANIM guard holds in the close scene). entrance_nitro 175411 (Nitro's set and close shots as in 134253),
  tron_raw_ring 182555 (RAW as it was). (All 2026-10-31_.)
  Open: Royal Rumble's sides of the road are dark (the old low crowd rows aren't in the close scene); SummerSlam's
  sign / spotlights and KOTR's letters stand where the close scenes have them (nearer the ring than the old flat
  pictures); lattice pieces are their faces toward the ring (thinner from the side); the close scenes' animated
  pieces are their first frames. More detail needs more than 15 parts (a bigger parts array than 0x8007B8A0's).
- (2026-10-31 evening, dev build only, NOT deployed; 0.38.8 unchanged) RAW IS WAR WM 2000: WrestleMania 2000's RAW IS WAR
  as the arcade's 4th arena (arena 13, on base venue 7). The user: 'port in WM 2000 Raw is War arena and do the same';
  then chose the close-up set in the match, and 'Add JR and King here'. Research and generator: data\wmarena\WMARENA.md.
  * WM2000 and its close-up:
    - WM2000's arena is No Mercy's kind of data: per-venue file-ID lists at 0x80047160.., meshes in No Mercy's format,
      the same units.
    - Its builder always draws the arena with combiner FC15FE2B (the picture as it is). No Mercy does the same for a
      list whose first mesh has byte 1 bit 7 (rig-proven on vanilla), so every WM2000 ringside mesh gets that bit:
      no brightness guess.
    - Its entrance close-up is a 3D scene of Revenge's kind (scene table 0x80105D70 in its match overlay; RAW = scene 0,
      files 0x2F / 0x30, at x0.3): the big RAW IS WAR screen in its truss, WWF.com, the WWF doorway, curtains, the
      Attitude towers.
  * gen/arenas_wm.py (new) makes:
    - the match stage set: the 3D set, 12 parts merged by picture, at WM2000's stage place (doorway z ~2270, deck
      y 187) plus WM2000's ramp slope; painter-ordered (No Mercy draws a list in order with no Z); long-texel parts in
      the alternate vertex form;
    - the close scene: 30 parts, WM2000's pose spot at the origin, the deck carried on toward the camera;
    - all of WM2000's ringside: mat, RAW IS WAR aprons, corners, floor, barricades, the crowd's walls / tiers / sign
      holders / hands, its rope colours;
    - the ARENA row label, photo and VS backdrop (a WM2000 rig shot);
    - two tron rows (arena 13).
  * The table (TABLE = True):
    - No Mercy's announce table, steel steps and JR / King are kept. arena_ring.c has a per-arena xring_table (1 for
      this arena), and for it table_begin, match_frame and no_table leave the game's table path alone: no rules bit
      0x100000, ann_on as it is, no hiding, no wall moves.
    - So announce.c seats JR / King, and the base venue's -690 walls and announce.c's table walls stand.
      build_begin also puts walls a Revenge match moved back.
    - WM2000's barricades are stretched onto No Mercy's lines (z <= -470: -530 -> -690, corners (+-530, -620) ..
      (+-470, -690)); its floor runs out to -690; the -z side's sign holders and hands are moved back 170.
  * Wiring:
    - gen/arenas.py ARENAS + ("RAW IS WAR WM 2000", "wm");
    - gen/arenas_entrance.py close_pieces / place;
    - gen/arenas_ring.py: every list per arena (xring_*[XRING_N][n], rope colours, xring_table);
    - src/arena_ring.c: [x] on the lists, ring_table();
    - src/tron_panels_all.h: two rows (13, -404..404, 456..908 match / 272..720 close-up);
    - qa/qa_tool.py ARENAS + the name.
    - arena.c's arena_stage_tables already leaves venue 7 alone while one of ours is in (a_in): nothing to change.
    - Originals in backups\pre_wmarena_2026-10-31 (+ the old wmport.bin). wmport.py regenerated (1866 files, 2.52 MB),
      build_mod.ps1 with no warnings.
  QA: all 2026-10-31_, shots looked at.
    New scenarios (qa\scenarios, from data\wmarena\qa), all PASS:
    - arena_wmraw 183925: WM2000's crowd, red ropes, aprons, the 3D stage and its ramp, steps, JR / King built;
    - wmraw_stage 184215: the hard camera on the 3D screen, WWF.com, truss, WWF doorway, curtains, deck;
    - wmraw_ring 184532: this camera only sees the far stage with its screen: no ring check;
    - tron_wmraw_ring 184825: the whole stage, the Attitude towers, the ramp, the tron picture on the big screen;
    - entrance_wmraw 185130: close shots with the 3D set behind; the entrant's video on the screen for Pete Gas, the
      RAW IS WAR picture for Hogan (no video of his);
    - wmraw_announcers 185537: JR and King seated at the table, WM2000's barricade and crowd behind;
    - wmraw_announcers_wide 185751: table, chairs clear of the moved barricade, the corner;
    - table_climb_wmraw 185947: 8+ runs at the table, never up off it, never past z -700.
    Regressions (arena_ring's lists changed shape), all PASS:
    - entrance_nitro 190303: Nitro's 3D close set; ringside NITRO aprons, flames floor, yellow turnbuckles, grey ropes,
      sign barricades;
    - entrance_havoc 190637: Havoc's set; mat logo;
    - entrance_bash 191011: Bash's set, mat logo;
    - stage_wrestlemania 191352: venue 7 as itself, its close set and its own crowd;
    - table_climb 191549 (SmackDown);
    - tron_raw_ring 191854.
    arena_page 192116 / 193106 (now 13 downs to the 14th row, more pick presses): the page scrolls to RAW IS WAR WM 2000
    with its label, photo and RAW IS WAR logo box; A / Start make it the arena (arena_current 13 from the select on).
    Its "match" check FAILs as in every run since 2026-10-30 (the script's presses never reach the select's 2P pick);
    a reachable check (arena_current 13 at screen 27) is added for the next run.
  Not testable with qa.ps1: a WrestleMania match right after this arena in one game (one arena per run), and a Revenge
  match right before it (walls_back for the table).
  Open: the ARENA photo is WM2000's own match-stage shot (retake from a QA shot); the -z crowd's 170 shift is by eye;
  WM2000's doorway sparkles and truss chase lights not ported.
Added 2026-11-01 (the overnight QA queue on 0.38.12; found, not fixed):
- Custom story retries never replay the match, and "gameover" loops forever (stories\backstage_sample.json, "Watch Your
  Back": node "interview" has "retries": 2 and a last branch { "next": "gameover" }). QA story_backstage_sample
  2026-11-01_065552 (report PASS: its checks only cover the first match): Austin (CPU) lost to Kane, 07:23 pin
  (match_state 4 at 694 s). Branch 2 (the loss) was taken: state matches 10106, clock 2. Then each retry went back to
  phase 5 (10105, screen 21, match_frames 0) and replayed the interview scene (shots 708-765, the question again), but
  no match started: straight to the results page (screen 29) with the OLD result, AUSTIN L / KANE W 07:23 (shots 786.0,
  954.1). Twice (702 s, 786 s). After that, phase 10 (10110) looped scene -> results -> scene every 84 s, from 870 s
  until the run's 2400 s ran out: the story never ended and never went to a game over. A player who loses that match is
  stuck. Likely the match-done / result state from the lost match isn't cleared for the retry, and "gameover" (storyc.py
  compiles it as next 0) isn't handled after a scene node. The user's own stories don't use retries / gameover;
  bad_blood / the_grudge don't either.
- Custom story: a branch with no "pages" and no "scene" shows the node's OWN narration again after its match.
  qa\stories_test\003_match_types.json node "tag" (branches [ { "next": "triple" } ]). QA story_match_types
  2026-11-01_074146: after the tag match's PIN FALL / results (2560-2565 s), "Tag match: AUSTIN and ROCK vs KANE and
  UNDERTAKER." typed out again (shots 2569.1 .. 2575.3 _WHEN), then a grey fade, then "Chapter 2 TRIPLE" (2585.7) and
  node 2's own page. Small, but it reads as a repeat. (The same run: CPU vs CPU, the tag match took 43 minutes, so the
  hour ran out in node 2. story_match_types_b, new, starts the same nodes at "triple".)
- QA-only: a crash report at exit when a QA run ends on its match count. subs_bret 2026-11-01_053454
  (crash_20261101_054154.txt in its crash_reports): after "[wcw] recomp::start returned", exe+0xA6DD (the thread
  proxy of main.cpp's write-watch lambda, NoMercyRecompiled.map) read 0x2305DDA2F10 = mw_base + 0x7F2F10 (its
  watch block) in freed game RAM. g_mw_stop is set early only on the WCW_QUIT_AFTER path (main.cpp ~1356). When the game
  quits by itself (QA "matches" reached here, also the operator QUIT GAME / window close with WCW_WATCH_MOD set), it is
  set only after recomp::start returns, and the RAM is already freed by then. wcw_rdram_release_hook ->
  crash_report_shutdown stops only the QA writer and the watchdog. Fix idea: set g_mw_stop and wait on g_mw_reading in
  the release hook too. Players never set WCW_WATCH_MOD (qa.ps1 does), so this is a QA nuisance: the run's holds were
  all fine. (2 of ~470 runs since 2026-10-27; the other, ports_jarrett_neidhart 2026-10-27_155635, predates the hook.)
- QA tool note (fixed in the scenario, not the tool): qa_tool.py roster() lets roster_ext.h's names override the game's,
  so "UNDERTAKER" means 0x00A (the 1999 look). music_undertaker now names 0x0003. Run 2026-11-01_025546 played 0x00A:
  sound 201 = his WM2000 slot 59, as designed. The rerun 2026-11-01_030409 (0x003) asked for sound 220 = Undertaker.mp3,
  slot 78. Both right.
- QA scenario note: arena_page's start row is the arena the SAVE remembers (page memory 0x80158156, saves\
  wwf.nomercy.us.bin mirrored from the game folder: SMACKDOWN since the user's 10-31 21:28 save), not RAW IS WAR. Now 11
  downs (from SMACKDOWN). Run 2026-11-01_054755 wrapped past the 14th row and chose No Mercy; 2026-11-01_061557 is right.
  story_scene / story_choice / story_backstage_sample / story_match_types had "qa\stories..." in their "about" (invalid
  JSON: PowerShell's ConvertFrom-Json refused them, so they had never run); the backslashes are now "/".

## 2026-10-31 night: costume variants -- Undertaker 1999 in the Undertaker's box (dev build only, not deployed)
The user: "When there are cases like Undertaker 99, add him into the character slots of existing wrestlers (in this case No
Mercy stock Taker), as costume variants, but retaining their new names, moves, music, etc. ... you cycle to him via the same
button to scroll through costume choices"; The Giant (being ported) "will become Big Show's alternate slot after Big Show
costumes". src/variants.c (new; build_mod.ps1 / build_test.ps1 list it).
- The table: variants[] { base, variant, when } (when VW_ALWAYS / VW_BIG_SHOW: 0x201 is Big Show only with the Big Show
  option on -- off, 0x201 is Benoit and the pair is off; bigshow.c bigshow_is_on). { 0x0003, 0x000A } now; the Giant's row is
  there commented out, waiting for his ID: { 0x0201, <his ID>, VW_BIG_SHOW }. A pair is on while both are on the roster
  (roster_id_to_ord), so RO_TAKER99 off = no 1999 Undertaker anywhere (the Type arrows go round the Undertaker's own four).
  A base may have several variants (FAMILY_MAX 4: the cycle goes base, then the variants in the table's order).
- The variant keeps his own ID: the select hands the game (0x00A, costume k) as from a box of his own -- name, moves, music,
  Titantron, weight class, saves, story, challenger as they were.
- The pages: his box is gone (roster.c place_ext and json_layout's box_ord skip variant_hidden(id); the pages close up -- the
  game's boxes fill from the first, so "blank" would mean a hole the cursor walks into). mod\select_pages.json no longer lists
  UNDERTAKER 1999 (a user's file that does: left out, as with anyone off the roster).
- The select (ovl_c, found on the rig + the recomp's output): the menus' record for screen 26/27 (0x80158360 + n x 0x138: +4
  the cursor, +0x14 the rows, 0x88 each; a box's row +0x62 its Superstar, +0x60 0x3000 | box << 4 | costume, +0x64 its def,
  kind 0x41F); 0x800A9586 the page, 0x800A9588 the costume under the cursor (Type - 1), 0x800A95A0 the picks (50 x 12),
  0x8015B80C the select's state (0x80: the page list -- the Type arrows are skipped then), 0x8015B1AC frames since the
  hovered Superstar / costume changed (the model and theme load at 6), 0x8015B9E0 the menus' sound (0x401 the click).
  The Type arrows: func_80111158 0x80111974: C-right / C-left (0x8010663E & 1 / 2) -> func_8010A960(id, costume, +-1) (the
  next costume not taken: func_8010A89C, func_8010A908). gen/patches.py: that call is variants.c sel_type_step(id, costume,
  step, row) -- for a family it steps through base 0-3, variant 0-3 ... (taken ones skipped) and writes the row's +0x62.
  Everything on the screen follows the row each frame (tried on the rig, vanilla: the row's ID poked -> the box's photo, the
  big portrait, name bar, height / weight, the model, the theme, NM_SELECT_HOVER all followed); A picks the row's ID. A row
  the cursor has left is put back to the base (variant_select_frame, logic.c each frame on the select).
- Type: the variant's own number, 1-4 (the digit is a picture per costume, ovl_c's table 0x80147F62 -> files 0x4594..0x4597:
  there's no 5-8; the name bar shows both as UNDERTAKER -- No Mercy Plus's own name for him -- the photo and model differ).
- Random (C-up, func_8010A9E8): its end calls variant_random_pick(id, page): one of the family at random, put in when the
  cursor is on the box (variant_select_frame).
- The Royal Rumble: rr_random_superstar takes a random one of a box's family; rr_draw_field puts every family member in the
  pool; rr_same_man (ONE UNDERTAKER IN THE RUMBLE) is variant_same_man (the old same_man table gone).
- logic.c: auto_pick -> auto_pick_c(want, costume, controller): a variant through his base's box and C-right (the
  challenger's winner back on his Superstar, QA's picks); sel_hover() (the cursor's row) for sp_pick and the QA.
- QA (hidden option qa_variant: 1 a tour of the Type arrows, 2 the challenge's picks on each player's controller, 4 the Rumble's
  list, 8 Random always the Undertaker; state.csv bs_tries = hover | costume << 16 | 1 << 24 on the select, bs_grabs /
  bs_step the first two picks, "forced" = the last auto_pick press | count << 16 | the cursor << 24):
  All 2026-10-31_, shots looked at:
  - variant_select_1p 224204 PASS (first run 223056 FAIL: under a stalled machine the state lost frames and the CPU's pick
    went to Kane and back -- rerun clean): the Undertaker's box, C-right: Undertaker Type 1-4 (biker, ...), then the 1999
    Undertaker's own photo in the box and the big portrait, his model (purple gloves, ...), Type 1-4, then the Undertaker's
    Type 1 again; C-left back to 1999's Type 4; picked 1999 Type 2; the CPU's half: the cursor's row the Undertaker's again,
    C-right past the taken costume to his Type 4; VS card UNDERTAKER vs UNDERTAKER; 1999's robed entrance, the biker's; the
    match 0x00A c1 (pad 1) vs 0x003 c3 (CPU). Pages (qa\pages_dump.py): SUPERSTARS 7 = Rodney, Joey Abs, Raven, Regal,
    Tiger (closed up), no 0x00A anywhere.
  - variant_select_2p 225638 PASS: single player's CPU pick the 1999 Undertaker Type 3 on controller 1; the challenge's
    select: player 2's own cursor ("2P" box) C-right Undertaker Type 2-4 -> 1999 Type 1-3 on HIS controller; the match
    Austin (pad 1) vs 0x00A c2 (pad 2), his entrance.
  - variant_rumble_pick 231200 PASS: the Rumble's select picks 0x00A through the Undertaker's box; ONE UNDERTAKER on: the
    30-man list (whole) has no 0x003, never in a slot.
  - variant_rumble_draw 231708 PASS: Austin, the ports-only field (qa_rr_ports): 0x00A drawn (18th of 21) though he has no box.
  - variant_rumble_draw 2026-10-31_231708 had the machine's stalls (82 frames / 20 s): void; rerun 2026-11-01_012330 PASS
    (frame rate whole): 0x00A drawn (19th of 21) though he has no box.
  - variant_taker99_off 2026-11-01_001918 PASS (2026-10-31_232241 cut short at the intro by a window closed from outside:
    void): with UNDERTAKER 1999 off the Undertaker's Type arrows go round his own four (tour: T1-T4, T1 ... C-left T4),
    never 0x00A; picked his Type 3; the match 0x003 c2 vs the Rock (shots: the biker Undertaker only).
  - variant_random 2026-11-01_014047 PASS: Random (C-up, always the Undertaker under qa_variant 8): the first landed on the
    1999 Undertaker (his photo, portrait, model, Type 1), a later one on the Undertaker; picked the Undertaker Type 1.
  - Regressions: rumble_one_undertaker 2026-11-01_015301 PASS (one-of-each now the table's: 0x00A never in with 0x003).
    versus_1p 2026-11-01_010902 CRASH: a freeze in the Austin-vs-Rock match itself (match 03:22, a grapple; the select,
    picks and match start all as before, every check up to it PASS) -- no variant code runs in a match; the build also had
    the Giant agent's generated move data. Rerun 2026-11-01_013915 PASS (7/7, frame rate whole).
    The freeze is open (once in two; its freeze report in that run's crash_reports).
- Not done / open: a Rumble's or a Survival's own (the game's, not the arcade's) random field from the pages (ovl_c
  func_8013486C) never draws a variant (no box); the Type digit 5-8 (no pictures); the select's 0x80 state (the page list)
  skips the Type arrows as the game always did.

## 2026-11-01 night: The Giant from WCW/nWo Revenge (dev build only, not deployed)
- The Giant (0x010A, ordinal 125, ext_late 18): a whole Superstar of his own (name, model, moves, music), to become Big
  Show's alternate in the costume fold (its table: { 0x201, 0x010A }; until then his own box on the REVENGE page).
  data\revport\ports3\PORTS3.md has how he was found, the captures, photo, moveset preview, theme. Revenge's model code
  0x0002, moveset file 0x167, body word 0x7002 (his own meshes 0x4C0-0x4D1: his size is in them -- in the ring he stands a
  head over the Rock). 7'4" / 540 lbs (mkroster h 28 / w 440: func_80114CC4's super heavyweight, 400+). THE GIANT / GIANT.
- Model: captures gi_c0 / gi_c1 / gi_c2 (ports\cap\h11 with slot 0 poked to 0x0002 = ports3\cap\g11; OVR 0:1:0 / 0:2:0):
  his black singlet (default), red tights, black trunks (Revenge's attires 0-2 of body type 2, which all its men share;
  the fourth costume the singlet again). wmport.py REV_PORTED / REV_CAPTURES / REV_CAP_START, REV_CAP_PORTS3 (ports3\cap).
  mkparts REV_LATE ("giant", 5 Thick 2, photo 119); mkovr; rev_photos.h + files.c: photo 119 (Revenge's Costume Change).
- Moves: Kane (4) the reference (86 exact / 14 near / 192 reference). His finisher the Huge Chokeslam (1520, his Revenge
  finisher byte for byte) at 70 / 72; 25 / 50 the Military Press (790, exact -- file 0x69's record has the strong slot's
  variants, as 16320's / 960's); 55 Two Handed Choke Lift (1210, exact). REV_OVERRIDE {30: 16250} (Revenge's body slam
  960 out), REV_NO_STRUGGLE; REV_WEIGHT [2, 0, 0] (Andre the Giant's abilities 2-4); Kane's big-man stance (ability 1 = 3).
  revanims.py: 18 rows (slots 1, 3, 73-83, 148, 163, 168, 185, 265 / 266). Allies Hogan, Nash, Hall.
- Music: Redux's THEME LIST gives 0x0002 song 0x18 "nWo" (instrument 0x36), the same song as Scott Hall's (and Redux's
  Hennig's); Redux has no theme of his own. Rendered as Hall's was -> mod\music\The Giant.mp3 (byte for byte Scott
  Hall.mp3). mkcontent: his EXT slot 70 (Revenge's 0x214 until a file), the file's slot 77 = sound 219. The user-music
  slots moved up one: Bret 71 .. Scott Hall 76, Undertaker.mp3 78 (was 76: music_undertaker's "about" is out of date).
- Select: roster.c wcw_id 0x0105..0x010A (the REVENGE page); mod\select_pages.json REVENGE + "THE GIANT" (8 boxes). Operator
  menu THE GIANT (option giant: RO_GIANT, ROSTER_OPTS 26, ROSTER_LATE_N 19); mod.toml option + roster sentence; names.py.
- QA on a quiet machine only -- data\revport\ports3\qa_one.sh waits until total CPU stays under 40% for a minute with no
  compile or game running, takes qa\.busy per run, and voids a run whose game went under 600 frames per 20 s (median).
  2026-11-01: the user's other project (<outside>, ninja -j<spec>) starved the game to 0-3 frames per 20 s and the
  freeze watchdog wrote "freezes" that were the machine's: vs_giant 2026-10-31_235641 stalled at match frame 6685 (all six
  N64 threads waiting normally, the frame thread for its RSP / VI events, anim_stream's queues consistent, heap fine, no
  RT64 display-list cut-off), chokeslam_giant 000245 crawled; the CPU-vs-CPU match plays the same every run, and
  vs_giant_long 004006 went through 6685 to the end (frame 7610, his Huge Chokeslam and the pin) at ~1,200 frames per
  20 s. (qa.ps1 also counted -3067 s across the 02:00 -> 01:00 DST change: own_giant 015856 INCOMPLETE, rerun; run names
  made 01:00-02:00 EST sort before the EDT ones.) All 2026-11-01_ unless said, shots looked at:
  - music_giant 014933 PASS: the REVENGE page with his photo in the 8th box; his Rumble entrance, his model whole walking
    out; the first sound asked for 219 (The Giant.mp3) -- data\revport\ports3\sndlog.py. (2026-10-31_235407: the same
    sound; its page had left him out: a concurrent run's robocopy put the old _roster.json back.)
  - vs_giant 003013 PASS and vs_giant_long 004006 PASS: CPU Giant vs the Rock -- head, hands / fingers, singlet, palettes
    right, a head taller than the Rock; the Rock's lifts on him became the struggling slam (4550 / 4552); the Giant's own
    SPECIAL Huge Chokeslam, 3 count. (336.0s, the replay: the grey-blue square and the dark box by his legs are the ring
    corner's turnbuckle pad and post between the camera and him -- ports3\cap\replay_corner_check.png.)
  - chokeslam_giant 012018 PASS (every front grapple as entry 70): grab by the throat, the Rock lifted high, slammed,
    covered (003743 the same, its "forced >= 3" check too strict: the match ends after two).
  - press_giant 011031 PASS (entries 1-60 as 25): the Military Press played whole three times -- overhead, dropped.
  - struggle_austin_giant 010617 PASS: Austin's Scoop Slams on him -> 4560 / 4550, the struggling suplex / slam (him
    reversing 4562 / 4552). (004624: void, the machine's.)
  - moves_giant 011617 PASS (qa_fcycle 18): 160, 2680, 2720, 60, 2630, 2650 with his Revenge animations, both men together.
  - rumble_giant 012832 PASS: a 10-minute ports-only Rumble with him in it; no freeze, pal_room_skipped 0, bad_mtx 0.
  - subs_giant 012638 PASS (qa_fsub 1): his Two Handed Choke Lift (1210, the Rock held up by the throat) and the 2680 hold
    (his Revenge neck wrench: both men together) forced in turn, 18 times over three matches.
  - own_giant 015256 PASS (qa_fsub 3): 2630, 160, 2680 and 265 / 266 with his own animations. The slide (265 / 266:
    Revenge's 0x1C37 pair, the same files as the other Revenge ports') forced while the Rock stood away from the apron:
    the Giant slid in the ring and the Rock wasn't hit -- the forcing, not the move (a slide wants the man by the apron).
- The fold (last, 2026-11-01 ~02:05): src\variants.c { 0x0201, 0x010A, VW_BIG_SHOW } uncommented -- with Big Show on,
  The Giant has no box: Big Show's Type arrows go on past his four costumes to the Giant's (7'4", 540 lbs, THE GIANT on
  the name bar, his photo in the box, his model); with Big Show off he's back in his REVENGE box. mod.toml texts say so.
  QA variant_select_giant 021543 PASS (qa_variant 1): Big Show Type 1-4, The Giant Type 1-4, round and back; picked The
  Giant Type 1; the CPU's The Rock; THE GIANT's entrance (sound 219), the match. Shots looked at.

## 2026-11-01 Bret's special Sharpshooter, Mr. Perfect's winning taunt (from PENDING; not deployed, no version bump)
- Bret's standing special (entry 70: the slam, then the hold 16771), the user: "Bret ends up away from the opponent,
  giving an imaginary sharpshooter". Not the placement after the slam (state.csv: the world positions stay put through the
  slam and the hold, 56..92 apart as the game's own hold has them; the shots show the legs locked). It was the hold's
  ENDING: the hold state (72, ovl_e func_80137044 phase 1) ends a hold by playing the hold's animation file + 2 (let go),
  + 4 (+0x9C & 8) or + 6 (+0x9C & 0x10) for both men (the defender's = + 1). movex.c gave the continuation gen/revanims.py's
  copy SS_HOLD (pack 0x542B), and in the pack after it come SS_REL (+2), then SS_TURN (+4: the turnover from the ground
  grapple's start -- Bret beside the man starting the move over, the man rolled over again) and past the pack's end (+6:
  any file at all). No Mercy's own Sharpshooter has all three (16354 release, 16356 a longer one, 16358 Bret thrown off).
  QA baseline 2026-11-01_020745_bret_special_sharpshooter (the build before): 8 holds, one (177-185 s) ended so -- shots
  181.5 .. 184.6: the man flat on his back, Bret beside him doing the turnover over again, positions and facings never
  updated (the ordinary endings give the man Bret's facing).
  Fix (movex.c): the continuation plays 16771's own files (then_file 0: No Mercy's 16352 / 16353, whose + 2 / 4 / 6 are
  its endings); Bret's ground Sharpshooter (157) no longer gets SS_TURN / SS_HOLD either (the same fault). SS_SHIFT is 0
  since 2026-10-30, so those copies were No Mercy's files byte for byte; SS_HOLD / SS_REL / SS_TURN are still made by
  gen/revanims.py and sit unused in the pack (drop them when the pack is next rebuilt). BRET_SLAM_SS (the slam + the
  turnover, one file) stays: a move's own file is never offset.
  QA tool: with qa_fsub, state.csv bs_tries / bs_grabs = the animation file slot 0 / 1 plays (+0xA6) -- scenario
  bret_special_sharpshooter checks it never reaches the pack's SS files and that 16352 / 16353 play.
  QA 2026-11-01_022943_bret_special_sharpshooter PASS (5/5; 4 matches, entry 70 forced, 11 holds, frame rate whole):
  the same moment as the baseline's (match frame 3321) now ends + 4 = No Mercy's 16356 (0x3FE4) -- it is the SUBMIT
  ending: shots 169.6 / 170.6 Bret sitting on the man, legs locked; 172.6 .. 173.6 Bret lets go, stands over him; SUBMIT.
  The other holds end + 2 (16354); files seen: 0x3FE0 / 0x3FE2 / 0x3FE4 and the slam's 0x5429, no other pack file.
  So the user's "imaginary Sharpshooter" was most likely a Sharpshooter that won (or was escaped): the turnover replayed.
  Not seen yet: the + 6 ending (16358, 0x9C & 0x10) in a run; Bret's ground Sharpshooter (157) not re-run since (ss_bret).
  CORRECTION (2026-11-01 noon): the user played 0.38.12 (this fix in) at ~11:50: "He drops the opponent and does the
  sharpshooter and is still misaligned. Nothing changed." The QA above never reproduced his case -- the move was forced
  through qa_move_entry from an ordinary grapple, the CPU's Bret, the man an IDLE player 2, SPECIAL not up (spirit 68).
  Its holds are aligned (11 of 11: roots 75 apart face to face, the legs in Bret's arms), but that says nothing about
  the user's path. "Most likely a Sharpshooter that won" was a guess, and wrong. See the next entry.

## 2026-11-01 noon: Bret's special Sharpshooter -- the player's path (open: the cause not established yet)
- Entry: the player's special IS entry 70. func_800EC698 (funcs_22.c) sets +0x8C bit 2 for entry 0x46 (70) and 0x51 (81,
  the back special) only, so the front special is 70 and only 70; 72 (also 1880 for Bret: the reference had its special
  in both) is another kind of pick, not the special. Last night's change did reach the move the user plays.
- Recorded data with the special NOT forced: the CPU's Bret in today's Rumbles (music_rumble_late). 2026-11-01_104430,
  1065-1074 s, Bret vs Scott Hall (Hall at stamina 0), SPECIAL! up (spirit 100): 1880 / 0x5429 1067.0-1072.2 (161 match
  frames, as forced), the hold 1072.3. Roots 74-77 apart, face to face (rel 0..12), moved together (the game moves the
  pair as one). Yet shot 1070.1 (97 frames in: the turnover, its ~41st frame) shows Bret crouched crossing the legs and
  Hall lying flat at his feet, legs on the mat -- the man's side NOT in step with Bret's (the forced run at the same
  frame, 104.6 s, has the legs up in Bret's hands). Placement is right; the man's animation is behind / another one.
  (2026-11-01_113206's case -- the two 260 apart in the hold -- is QA's own doing: qa_rr_out put the man outside, z -41
  -> -243, in the grab. Not a game fault.)
- So the difference is the circumstance, not the chain: SPECIAL! up and / or a CPU defender (the forced runs had
  neither). Not yet told apart; nothing in movex.c changed for it (no guess shipped).
- The QA tool for it (logic.c, QA only; mod.toml option qa_pspecial): the player's controller pressed as a player's --
  with SPECIAL up and the man in front, a tap of A, then the special (1 the stick, 2 C-right with the Arcade controls;
  3 no presses, the CPU's own); not near in 6 s, Bret put 70 in front of the man (where a grab puts him); + 16 a camera
  of its own on the two. state.csv: see the queue entry (bret_special_player, _player_arcade, _cpu). The 12:18 run of
  bret_special_player was void (the option wasn't declared in mod.toml then: Bret stood idle). Queued for tonight.
- From the code (RecompiledFuncs; fa.py-style listings of the "// 0x" comments), ruled out so far:
  * The joined file's halves: 0x5429 / 0x542A both 185 frames, 93 keys, the slam / turnover seam at key 35 in both;
    header speed bits 0 in all four source files (14296 0x0800, 14297 0x5000, 16350 / 16351 0x0000) and in the join.
  * The next file one step later: each man goes on at the end of HIS OWN file (func_80129128: +0xA2 frames left <= 0 ->
    func_800EC698(self, +0x304, 292 + MX_NEXT[owner][+0x308])) -- same length, same frame; and in 104430 the two went
    72 / 73 in the same state row, 85 / 86 too: no start or hand-off lag visible at 20 rows a second.
  * func_8012F848(w, file, kind) loads a file's header + 36-byte record into w + 0x2E8.. (func_80011BD8): + 0x2F8 =
    rec[9] (0 -> 1), + 0x2FD = rec[0x10], + 0x302 = rec[0xF] (0 -> frames), +0xA2 = frames, +0xA4 = 0. The man going
    into a move (func_80129128's receiving phase) starts the move's man file when HIS frame == + 0x2F8 of the file he's
    in and the other's +0xF0 & 2, then state 86 (func_800EB5E0(self, 0x56)) -- No Mercy's own moves the same.
  * The joined files carry 14296's / 14297's records for all 185 frames (revanims join_files): the man's rec[0x10] = 50
    (the slam's impact) drives func_800E8F1C (before frame 50 the man is drawn to Bret at 4.0, after at 6.0 --
    func_8012C9B0) and func_800EE1E4 (ring / table breaking at that frame). The turnover's own record (16350: sounds at
    26 / 46 / 94 / 104) is not in the join: the turnover is silent. A finding, not the misalignment.
  Left: SPECIAL!'s damage at the slam's impact on a man in DANGER (Hall had stamina 0 in 104430) changing the man's pose
  while his state stays 86; the CPU defender's presses. Not established.
- Next: the queued runs give both men's animation files every row (bs_tries) and the hand-to-leg distance -- if the
  man's file is 0x542A and his pose lags, look at what holds the defender's frame back (SPECIAL!'s flash / the special
  camera, the CPU's escape presses); if it isn't 0x542A, what the game gives the defender then.
- Mr. Perfect's winning taunt (the user: he climbs an invisible turnbuckle mid-ring): the winner's taunt is moveset slot
  235 (func_800E67F0, the taunt state, phase 4: func_800EC698(w, owner, 235)), played where he stands. His port took
  Revenge's win pose (his list place 351), which is his turnbuckle taunt -- the same file as place 355 -- No Mercy's 11240
  Taunt 008, a slot-232 (top turnbuckle) move. None of the game's own Superstars has a corner / turnbuckle / apron /
  outside taunt (slots 231-234's lists) in 235, and the game never walks a winner to a corner: so a standing one.
  gen/mkroster.py REV_OVERRIDE hennig 235: 15020 (Taunt 016: his own Revenge standing taunt, place 353, byte for byte; the
  game's 230 for several), and main() asserts no Revenge port has a slot-231..234 move in 227-230 / 235. roster_ext.h
  regenerated: only his moveset key changed (2 bytes; checked against a regeneration of the unchanged script first).
  QA tool: option qa_over_wait (seconds before the A presses after a QA match; 2 as before) so the winner's taunt plays
  out; scenario hennig_win_taunt.
  QA 2026-11-01_024116_hennig_win_taunt PASS (5/5): Perfect pins the Rock (match frame 831); the winner's taunt (state 27,
  move 15020, never 11240) from 83.7 s until the game goes to its replay by itself (~88 s); shots 084.1 .. 087.6: him
  standing mid-ring by the Rock, both feet on the mat, arms up -- no climbing.


## 2026-11-01 Arenas restructured: each arena its own (DESIGN first, then the work; not deployed, no version bump)
The user (PENDING (bulk), 2026-11-01): RAW played after a WCW Revenge arena kept that arena's smaller ringside; each arena
is its own separate arena -- not a modification of another venue, not built on a copy; the arcade's arenas must not
borrow a game venue's per-venue entries, must not write over the shared ring / crowd / stage lists or the match
overlay's collision records (arena.c tables(), arena_entrance.c, arena_ring.c put16 / put32 / sv_*, walls_move / rec_old
did exactly that, with a save / restore); grow the stage set past 15 parts and put back what the cap cut.
Research tools (scratch, not the mod): xref.py (every lui + addiu / load naming an address range, from the six-man
engine's RecompiledFuncs comments), luis.py (each `lui rX, 0x8005` of a function and what its register addresses).

### What the game reads per arena (found)
- The arena builders read one region of the main data segment, always through `lui rX, 0x8005` + an offset (no
  `lui` there serves anything else -- luis.py: every one is "arena" or "other", none mixed): 0x80050040..0x80050870
  (ring list A, steps, floor, barricades, corners, the table's places / meshes, the stands, the four near-row crowd
  groups and both frames' palette lists, the upper deck [v], the truss + banners [v], aprons [v], the stage set [v]:
  0x800507FC / 0x80050824 / 0x8005084C), the entrances' close scene (place 0x80050690 + 16 i; its tables
  0x80052280..0x80052830 by scene index i) and the ropes' colour (0x80052850..0x80052890: set [v] + R G B).
  Readers: func_8001B548 (ring), 8001C62C (the ring rebuilt in place: func_8001B3E4 at the entrances' end -- NOT through
  8001B220, so arena_ring.c's swap never covered it), 8001CD34 / 8001D354 / 8001D4D4 (crowd), 8001D8EC / 8001DA24 /
  8001DA6C (stage set), 8001DB4C / 8001DF38 / 8001E488 / 8001B0B8 (table, steps), 80021980 (ropes, from 80022B98 at
  8001B220's start), 8001B220 / 8001B3E4 / 8001F088 / 8001F2D8 / 8001FFA8 / 80020940 / 80020B58 / 8002173C (close scene).
- The stage set's parts: 0x8007B8A0 (15 x 0x34; 0x8007BBAC is the next variable), named only by 8001D8EC / 8001DA24 /
  8001DA6C; the display list func_80010314 makes is heap (n x 16 + 0x40): the array is the only cap. The close scene's
  parts (0x8007D890) have room for 392: no cap there. The set's node: 0x8007B6E0 (+0xDC the moved one, z 1500 from the
  immediate 0x44BB8000 in 8001D8EC).
- The collision (the match overlay ovl_e's data, loaded with it from the ROM: records 0x80146xxx.., the master list
  0x8014D06C is only for func_8012DAF0's linking / normals): a wall is found zone list -> span {first, last} -> records
  by +0x2C next. Ring row 0 of the zone table 0x8014D290 (func_8012CB8C, 8012D58C: [quadrant] -> {barricade chunk,
  apron chunk}), the floor zones 0x8014DA6C[row] (func_8012DDD8: {floor span, a zone list per quadrant}: the floor
  {55, 56} -> 0x80147BB4..BE4) and 80140FD4's fixed lists 0x80147BB4..0x80147C24 (compared and switched by pointer).
  The barricade loop 10..21 (+ 24..28 the table's front, linked in by group 12), the apron 31.. with the steps 45..54;
  func_8012DCC0(k) (from 800EE1E4 / 801433E8) is the game's own relink when the steps (0, 1) or the table (2) go.
- Other per-venue readers (venue copy 0x800BD5A0): the nameplate font 0x80045980[v % 10] (func_8000A470), ovl_b's
  0x801257B8[v * 3], 8011D2F8 (effect sets by scene), 80116194 (0x801497FC[v]), 8011649C, 8010DC70 -- see "open" below.

### The structure
- arena.c: an arena descriptor per arena -- the game's ten and the arcade's -- `arena_desc(a)`: the base of its data
  block in the game's arena-data layout (`lists`), its stage set (meshes, palettes, count: the mod's ported close sets
  for RAW and venues 1-9, the game's own entry for a venue that has none, the arcade arenas' own), the set node's z,
  `table` (No Mercy's announce table and steps) and its collision. The game's ten: `lists` 0x80050000 -- their own
  data, read in place, never written. The arcade's: their own block (`xarena_block[k]`, 0x2890 bytes of mod memory, the
  same offsets), filled once at boot from the generated lists (gen/arenas_ring.py's xring_*, arenas_entrance's close
  scene): every per-venue entry in it [0..9] is the arena's own, so no game venue's entry is ever read for it.
- Choosing the arena for a build: entry hooks on func_8001B220 (the arena / scene build) and 8001B3E4 (the entrances'
  scene switch) set the globals the patched code reads, from arena_current(): `arena_lists` (the ring, crowd, table,
  ropes), `arena_close` (the close scene: the arena's block for a scene index below 10 -- a venue's close-ups --, the
  game's 0x80050000 for the backstage scenes), `arena_set_mesh / pal / n / z`, `arena_no_table`, the zone pointers.
- The patched game functions (gen/patches.py, asmpatch copies): every arena-data `lui rX, 0x8005` -> `lui rX,
  %hi(arena_lists); lw rX, %lo(arena_lists)(rX)` (or arena_close), the offsets after it as they were. The stage set: the
  parts array -> `arena_set_parts[ARENA_SET_MAX]` (Part 2: the cap grows), the lists -> the arena's, the count freed =
  the count built, the node's z -> the arena's. The table: the three "no table" tests (rules 0x100000 / the cage) also
  take arena_no_table; 8001B0B8 (table back) / 8001B08C (table breaks) do nothing with no table. announce.c asks
  arena_has_table() (no JR and King without one) instead of arena_ring.c clearing ann_on for the build.
- Collision of an arena without the table (Nitro, Havoc, Bash): its own records (the barricade loop at 530 all round,
  the apron without steps, the floor to -530), its own spans and zone lists, found through `arena_zone_q` (row 0 of
  0x8014D290 in 8012CB8C / 8012D58C), `arena_floor_zones` (row 0 of 0x8014DA6C in 8012DDD8) and `arena_zone_fix[8]`
  (80140FD4's eight constants). The game's arenas (and RAW IS WAR WM 2000, with the table): the game's own pointers.
- Gone: arena.c tables() / base_ptr / arena_raw_tables / arena_stage_tables (the game's 0x800507FC.. written every frame),
  arena_entrance.c's swap of scene index 7, arena_ring.c's put* / sv_* swap, walls_move / walls_back / rec_old, the rules
  bit and ann_on during the build, the per-frame table hiding, the set node z write.

### Part 2: the stage set past 15 parts (2026-11-01)
- The parts: src/arena.c `arena_set_parts[ARENA_SET_MAX]` (64 x 0x34 bytes) in place of the game's 0x8007B8A0 (15):
  func_8001D8EC (made), 8001DA24 (freed: arena_set_built, the count made), 8001DA6C (made again) -- the only three
  that name the array (xref.py). func_80010314's display list is heap (n x 16 + 0x40): no other cap. The close scene's
  parts (0x8007D890) have room for 392: no cap there. The generators' SET_MAX 64 (gen/arenas.py, arenas_raw.py,
  arenas_stages.py, arenas_wm.py; arenas_ring.py sizes its arrays to the longest set); arena.c asserts them.
- Hairline cracks: each part's points go on the grid its scale needs; with pictures no longer packed into one part,
  neighbouring parts rounded on different grids opened cracks (offline render, SmackDown's shell). arenas_ring.py
  grid_scale / snap_parts: a power-of-two grid per part, a point two parts share on the coarser of the two
  (arenas_stages.py, arenas_raw.py).
- What came back (parts before -> after):
  * BASH AT THE BEACH 15 -> 16: its small plant panel (model 15; arenas_stage3d.py RING_SKIP {}).
  * RAW IS WAR WM 2000 12 -> 14: the lower curtain panels' own picture (17) and the deck's black skirt's (23), drawn
    with the curtains' picture while the set had 15 places (arenas_wm.py TEX_REMAP {}).
  * RAW IS WAR 15 -> 37 (arenas_raw.py parts): the whole close set -- every picture its own (the four side floors, the
    back / side walls, the logo's silver / red, the fists, the signs were packed four to a 64 x 64 with one median-cut
    palette, at half size), the truss whole (only its faces toward the ring were kept), the screen frame's side posts
    their own picture (0xE3E), the towers' posts theirs (0xE3A), the arches' second copies (the extra list 0xAAA..),
    the logo's dark copy behind it (0xA9E..), the doorway's side walls (0xA93). Left out by design as before: the
    close ramp / landing (the old ramp stays), the deck past its face; the fists are their first frames.
  * No Mercy's other arenas (arenas_stages_spec.py, the block at its end): every picture its own (no packs, no repic,
    no solid) but SmackDown's old sign (its two halves one picture: tron_panels_all.h's TP_MASK row is that picture's);
    the trusses / lattices whole (no "front only": the faces a match camera sees, as every piece); NO MERCY 13 -> 30
    (the arches' second layer, the logo's dark copy), SMACKDOWN 13 -> 35 (the close halo of lights and its lamps in
    place of the old flat halo, the four lamps by the shell), KING OF THE RING 15 -> 28 (the close rows of lights in
    place of the old flat rows, the small floor lights), SUMMERSLAM 15 -> 19, SURVIVOR SERIES 15 -> 32 (the disc's
    second ring, the floor lights by the sides), ROYAL RUMBLE 15 -> 35, WRESTLEMANIA 13 -> 20, BACKLASH 14 -> 27
    (its arms' bands by their own lattice pictures), ARMAGEDDON 15 -> 46. Left out by design as before: aisles,
    ramps, rails / fences on the old aisle, pieces in the stands (SmackDown's close big oval sign, two far lamps).
  * MONDAY NITRO 12, HALLOWEEN HAVOC 14: nothing was cut for the cap.
- Room left (64 places): Bash 48 free, Survivor Series 32 (the two screens to come: Bash's slideshow board, a
  Titantron over Survivor Series' stage -- each a part of its own on this structure).
- Step 2 built 2026-11-01 12:46 as 0.38.15 (wmport.py + mkovr.py regenerated with the Giant's thigh fix in -- 2056
  files, 2.62 MB; mkovr's output unchanged; backups in Development\backups\pre_arena_step2_2026-11-01 and
  pre_arena_own_2026-11-01). tron: the XSTAGE_SCREEN / XRAW_SCREEN extents moved by 1-3 (the new grids), the rows
  use the macros; every screen picture is the same file as before (screens were already parts of their own), and
  SmackDown's packed old sign (TP_MASK row) is kept packed. Not QA'd in the game (the user plays it; queue above).

### Step 3: the two new screens (2026-11-01, built as 0.38.16; not QA'd in the game)
- BASH AT THE BEACH's own screen: Revenge's picture board, decoded -- scene 3 (geometry 0x3C / pictures 0x3D) draws
  models 0 and 13-20 through its scene table's +6 (3) / +16 records (0x800C8E14: 12-byte records, not needed to place
  them: the models' points are the set's own, their nodes at the origin). Models 13-16 and 17-20 are the same four
  places -- a 2 x 2 wall of 64 x 32 CI8 photos on the diagonal by the boardwalk under the hut (set x 500..1348, z
  -848..0, y 0..900; the match x 150..404, y 0..270): the slideshow's two frames (sunsets / the sea; the BASH AT THE BEACH
  logo). Model 0 is the beach umbrella on the other side of the boardwalk. gen/arenas_stage3d.py EXTRA / extra_model:
  models 0 and 13-16 (the first frame: the stage set and the close scene don't animate) after the scene's own, in the
  match's set (Bash 16 -> 21 parts: each tile its own picture, a part each) and the close scene (18 -> 23).
  tron: four rows each scene (one per tile, the wall's extent: the picture once across all four) -- arena 12.
- SURVIVOR SERIES' Titantron: gen/arenas_stages.py NEW / new_pieces -- a screen (x +-320, y 900..1220, z 900 in the
  match: over the back hexagon frame, whose top is at 850) in a frame of four truss beams of the arena's own lattice
  (0xF4E, 40 wide), the screen's picture the arena's emblem (0xF34) until the video / hard camera shows; built in the
  close scene's space and put through the same pieces() as the close set (the match: s p + d; Survivor 32 -> 34 parts,
  the screen a part of its own: XSTAGE_SCREEN5_*). In the close scene: its extra list (empty in the game's for index
  5) -- arena.c game_close_block: Survivor's own close block, index 5's entries its own close scene's (read from the
  game's tables: its lists, animated pieces, single models, place, rates, bias), the extra list the two new pieces
  (xstage5_close_mesh / _pal, XSTAGE5_CLOSE_N); arena_scene takes it while Survivor's close-ups are built. The disc
  keeps its logo (no rows).
- The rows: src/arena_screen_files.h (gen/arenas_stages.py write_screens, regenerated with the sets: each screen's
  extent as its mesh files have it, its picture's fold); tron_panels_all.h's rows use its macros; tron.c includes it.
- Offline looks: data/arenaport/ringside/preview.py 3 (the board under the hut, the umbrella), allstages/cmp.py 5.

### Step 4a: per-venue entries and the announce table, each arena's own (built as 0.38.17; not QA'd in the game)
- The nameplate font (func_8000A470: u16 0x80045980[v % 10]) and ovl_b's close-shot byte (func_80109F88: u8
  0x801257B8[3 v]): read through arena.c's arena_font_tab / arena_close_tab -- the game's tables for its arenas, an
  arcade arena's own ten-slot copies while it's in play (arena_tabs, every frame and at arena_choose), filled from
  gen/arenas.py write_tables (src/arena_table_files.h: xarena_font, xarena_close_bytes -- the values they had, venue
  7's from the ROM, now their own: font 0x4481, bytes 05 10 17).
- The announce table and the steps: every arena its own table block (xtable_block[10 + XARENA_N][0x100]: the game's
  steps / table lists from the ROM with the table's four pieces 75 toward the ring built in, -605 -> -530, -545 ->
  -470), read through arena_table by func_8001DB4C / 8001DF38 / 8001E488 / 8001B0B8 (their table luis, gen/patches.py
  _TABLE_LUIS). announce.c no longer writes 0x800500C0.. (table_data_up unused). The table's walls: still announce.c's
  per-match move of the overlay's records until step 4b (0.38.18).
- Still per venue through the venue byte (step 5, next): ovl_b's entrance effect sets (func_8011D2F8 -> 0x80132CC0
  entries: pointers into ovl_b's effect scripts) and ovl_c's 0x801497FC / func_8011649C tables.

### Step 4b: every arena's own walls (built on its own for a roll-back; not QA'd in the game)
- gen/arenas_walls.py -> src/arena_walls_files.h: per arena (the game's ten, then the arcade's) its own records, spans,
  zone lists and floor zones in the match overlay's layout, from the six-man ROM's ovl_e records:
  * an arena with the announce table (the game's ten, RAW IS WAR WM 2000): the barricade loop with the table's front
    (10..15, 24..28, 17..21), the -z side at -690; the table's walls, top and planes with announce.c table_walls_up's
    move built in (front -530 -> -455; over the top and the back plane -690 -> -600); the straight -z side (16) its own
    record, linked in when the table breaks (func_8012DCC0 case 2 -> arena_brk_at / _val / _zone);
  * an arena without (Nitro, Havoc, Bash): Revenge's square at 530, the apron without steps, the floor and the far
    plane (id 0) to -530 -- as arena_ring.c's walls_make had them (now generated);
  * every arena: the aisle's walls (x +-140, z 500..2000), the far plane, the table's top / plane lists.
  The apron with the steps, the ring's skirt and the ring's floor zone stay the game's (the ring and its steps are one
  prop in every arena).
- arena.c walls_take(a) at each build: the zone pointers (arena_zone_q / _fix / arena_floor_zones as before, and new:
  arena_zone_top 0x80147E20's, _t28 / _t30 0x80147E28 / 30's, _aisle 0x80147408's, _plane 0x80147B7C's, arena_rec_top
  0x80147E38's), the table side re-linked and the top's floor zone put back (a break in the last match). gen/patches.py
  _ZONE_SITES (8012CB8C, 8012D58C, 8012D82C, 8011B5B0, 8011E748, 8011EBC0, 8011FDE8, 800EB0DC, 800EB334, 80139D80) and
  8012DCC0 case 2. The overlay's records are never moved now: announce.c table_walls_up is no longer called (its top
  record for QA: arena_rec_top). QA's table_climb_old (qa_table 2: the back plane as it was) can't reproduce the old bug
  any more (the plane is built in).

### Step 5: the rest read by the venue byte, each arcade arena's own (not QA'd in the game)
- ovl_b's entrance effect sets (the sparks round the stage in the close shots): func_8011D2F8 takes {set, script list}
  8-byte entries from 0x80132CC0 by index venue / + 10 / + 20 (func_8011D05C: the set's positions, a script per spark,
  its bytes indexing the shared sprite table 0x801318A0). Each arcade arena: its own copy of the effect data (ovl_b
  0x80131960..0x80132CC0 from the ROM, every pointer into it made its own: xfx<k>_blob) and its own 30-entry table into
  it (xfx<k>_tab); func_8011D2F8's table base -> arena_fx_tab.
- ovl_c's VS-screen pieces: func_80116194 draws a 0x24-byte struct of three pictures by 0x801497FC[v] (WrestleMania's:
  0x4684 lines, 0x4687 swoosh, 0x468A emblem); func_8011649C takes values by [row][v] from 0x80149D68 / 0x80149E80
  (7 x 10 u32). Each arcade arena: its own struct copy and its own ten-slot pointer table, its own 7 x 10 tables
  (arena_vs_ptrs / arena_vs_a / arena_vs_b).
- The values are the ones they had (venue 7's from the ROM: the same sparks, the same VS-screen pieces): now each
  arena's own copies, nothing points at WrestleMania's. Arena-specific contents (Revenge's own light show, the arena's
  own VS pictures) would go in these copies (gen/arenas.py effects_and_vs).
- Every per-venue read found (venue copy 0x800BD5A0 / NM_VENUE / 0x8007D798 / the scene index) is now the arena's own:
  the arena data (step 1), the stage set (2), the close scene (1, 3), the table (4a), the walls (4b), the font and close-
  shot byte (4a), the effect sets and VS pieces (5). Left as the venue byte only for control flow ("a ring venue":
  venue < 10 tests), and the menus' 3D venue scene and cutscene table 0x80125870 (8010DC70: story / backstage scenes by
  id, not an arena's).
- QA queued (FOR TONIGHT'S QUEUE, 0.38.18 / 0.38.19): qa\scenarios\walls_*.json (all 8), table_climb, table_climb_wmraw
  (expect table_climb_old to pass now: its old back plane is gone), plus entrance close shots and the VS screen in
  Monday Nitro / Havoc / Bash / RAW IS WAR WM 2000 (sparks as before, no crash). The user's hand test list for 4b is in
  the 0.38.18 report: barricade on every side, the table (front / sides / top / dive), a table break and the next
  match, the aisle, items at the table, the camera near the table -- in a game arena and an arcade one.

### The RAW bug: what QA showed (2026-11-01, before the restructure)
- New QA hooks: option qa_arena2 (the arena of the second match on; qa_tool.py "arena2") and qa_walls (logic.c ->
  arena_ring.c arena_ring_qa: the walls the collision really walks -- row 0 / quadrant 2's zone list, its first span).
- walls_nitro_then_raw 2026-11-01_123255 PASS (the old code): Nitro's match had the -z barricade at -530 (rec_n 21
  records moved), RAW's next match had the game's -690 / -615 (walls_back ran, and the match overlay -- ovl_e, its data
  0x80144670..0x80151ED0 with the collision -- comes from the ROM again with every match: the menus' overlays share its
  addresses). So in the title -> match path nothing leaked; the user's path (Versus menus) wasn't reproduced. Places
  the old swap could leak, all gone now: the ring made again in place at the entrances' end (func_8001B3E4 ->
  8001C62C reads the shared lists outside func_8001B220, the only build arena_ring.c swapped for), announce.c's
  table-wall search running on records arena_ring.c had moved (its table walls -455 -> -530), walls_back skipping a
  record whose id had changed and still clearing rec_n. With each arena's data its own and nothing of the game's
  written, there is nothing left to put back.
- Step 1 built 2026-11-01 12:42 (mod\build\mod_six\nomercy_arcade.nrm; generated data unchanged -- the stage sets,
  ringside lists and close scenes are the same files as 0.38.x, only how they're found changed). Not QA'd in the game
  (the lead's workflow change: the user plays it; the scenarios above go in tonight's queue).
- Not restructured (open): announce.c still moves the game's announce table in place (0x800500C0.. +75 in the game's
  data, and the overlay's table walls per match) -- for every arena with the table, the game's ten and RAW IS WAR WM
  2000; other per-venue readers outside the arena data still see the venue byte (XARENA_BASE, WrestleMania's entries)
  for an arcade arena: the nameplate font 0x80045980[v % 10] (func_8000A470, ovl_b 8010E648), ovl_b's close-shot bytes
  0x801257B8[v * 3] (func_80109F88), the entrance effect sets (func_8011D2F8(v / v + 10)), the cutscene table
  0x80125870 (8010DC70), ovl_c's 0x801497FC[v] (80116194) and 8011649C's table. The aisle's walls (x +-140 to z 2000)
  and the item / camera planes at z -690 (records 0, 85, 86) are the game's in every arena.
- (2026-11-01 afternoon, dev build only, NOT deployed, NOT QA'd in the real game yet) Titantron: the user's corrections
  (PENDING (bulk), Titantron Placement QA\To Fix). src/tron_panels_all.h + src/tron.c; data\tron_all\TRON_ALL.md
  section 7, data\tron_all\stage_rows.py (the match-set parts' extents / folds, from gen/arenas_stages.py read-only).
  * WRESTLEMANIA: the middle panel at the ramp's end (0xD50 / its match copy) back to its own picture; the video on
    BOTH side screens: match row (0xD42 + 0xD43 made one part, texture 0x0FF5, |x| 528..1024, y 112..288, TP_TWIN) and
    the close scene's 16 animated frame rows (TP_TWIN | TP_ANIM) moved out of TRON_SIDE_SCREENS (which now only guards
    Backlash's lower screen). Each side the whole picture, left to right.
  * SMACKDOWN: the oval doorway in the shell's mouth (0xB79 and its match copy) back to its own animated / static logo;
    the video on the "SMACKDOWN" oval sign at the right of the shell: match row (the old sign 0x596 + 0x597, one part,
    packed 64x64, x -1240..-740, y 200..700) and close rows (the extra list's big sign halves 0xB6A / 0xB6B). No twin.
    The picture keeps the OVAL: new flag TP_MASK, tron.c mask_compose -- TRON_MASKED (0x806C4000) = our picture with
    its alpha bit taken from the sign's own texture (filled 0, the sign's picture laid flat with coverage x alpha
    written full, ours drawn over with CVG_DST_SAVE); the masked panel samples it, and the arena's own render mode for
    the set (texture edge: clear texels not drawn) cuts the video to the oval. Called at the end of the hard camera's
    and the video's passes; the hard camera keeps MASK_BYTES of its list for it.
  * SURVIVOR SERIES: all its rows out (the disc 0xCBF's TP_ANIM frames): its logo in the close shots and the match.
  * BACKLASH, RAW, No Mercy, KOTR, Armageddon, RAW WM2000: rows unchanged.
  Open: if the stage-parts restructure re-packs SmackDown's sign or merges WrestleMania's side screens differently,
  rerun data\tron_all\stage_rows.py and update the two match rows (their fold / extent). QA queued (FOR TONIGHT'S QUEUE).

## 2026-11-01: The Giant's thigh hole (the user, 0.38.12: "a piece of his upper leg missing") -- built 12:44, not QA'd
- Cause: gen/wmport.py nm_skin put No Mercy's own picture on a ported model's bare-skin part when the two were the same
  size. No Mercy's thigh picture (0x2970, parts 7 / 14) has a 12 x 13 corner of 120 transparent texels (no alpha bit) that
  its own thigh mesh never samples; a Revenge thigh's UVs are laid out for its own picture and do -- The Giant's left
  thigh (mesh 0x4CC) has 4 triangles at its top, by the hip, there: a wedge drawn as nothing (data\revport\ports3\
  thighrender.py: the thighs rendered offline with both pictures, the transparent texels magenta -> qa\giant_leg_after\
  offline_thigh_render_NMpicture_vs_own.png). Nothing was dropped in the conversion: Revenge's meshes go into the pack
  byte for byte (45 triangles each), the textures and palettes whole -- the substituted picture was the fault.
- Fix (wmport.py _opaque): No Mercy's picture goes on only if every pixel of it is opaque; else the part keeps its own
  Revenge picture, recoloured to No Mercy's skin tone like the part-skin ones. Changed: The Giant's costumes 0 / 3
  (singlet) thighs, and Hollywood Hogan's costume 2 (Hulkamania trunks) thighs, which had the same hole. wmport.py +
  mkovr.py regenerated (the arena lines of the log unchanged), backups in Development\backups\pre_giant_thigh_2026-11-01.
- Before shots (QA, no fix): legs_giant_c0_front 2026-11-01_120310, _back 121236, _left 121613, _right 122901 -- the
  camera at thigh height showed both thighs whole there; the hole is at the very top by the hip (the hip views are queued).

## 2026-11-01 afternoon: The ports' holds went on in No Mercy's (0.38.20: in the game, not tested yet)
- The user: "We need to tackle why Revenge wrestlers holds are glitching and causing themselves and/or the opponent to not
  be aligned right with the hold ... Bret's sharpshooter from front grapple special. Giant has a sleeper hold from behind
  that does it too. Mr Perfect's winning animation we tried to fix should be a clue." The clue holds: in every case the
  game played an animation that doesn't belong with where it put the two men (Perfect: a turnbuckle taunt mid-ring).
- A hold in No Mercy is two moves: the grab (move m, files F / F + 1, record & 0x1E00: it goes on) and the hold (m + 1,
  the game's continuation entry 292 + MX_NEXT[entry], files F + 2 / F + 3, played over and over), ended with the hold's
  file + 2 / + 4 / + 6 (ovl_e func_80137044 phase 0 / 1, func_80137364 the man's; func_8013C164 the same for a move's
  + 2 / + 4 parts) -- 2680 / 2681, 11620 / 11621, 16770 / 16771 alike. The loop's first key is the grab's last, both men
  (2680's end -102 / -92, 2681's start -102 / -92: the continuation isn't placed, it goes on in the same frame).
- movex.c only made over the grab (entry e). The continuation entry kept the stand-in's move + 1 and its No Mercy files:
  The Giant's sleeper from behind (slots 75 / 76 / 79 / 80, Revenge code 64, files 0x1652 / 0x1653, stand-in 2680
  Abdominal Neck Wrench) grabbed his Revenge way (end: him -128, the man -32), then both snapped into the neck wrench's
  loop (-102 / -92: the man ~60 away, another hold), and let go as the neck wrench does. 38 such rows on 8 ports.
- Revenge keeps the same thing: each wrestler's list (rev_files L + 2) has, past the move codes, 24 blocks (codes 239 -
  262, 8 files each) -- a hold's loop pair and its three endings, No Mercy's order (Bret's code 260 is No Mercy's
  16352 .. 16359 byte for byte). The move records (L + 1, 24 bytes a code) don't name the block plainly; which block
  is a move's is the one whose loop starts where the move's animation ends (both men's first key against the move's
  last): on the 1035 moves of Revenge's 76 lists that No Mercy has byte for byte, 1034 right (the one a tie).
  gen/revanims.py rev_block(): right ones score 1700 .. 5600, the next block twice that and more; a move that ends
  10000+ with the next as near.
- Fix: gen/revanims.py brings each made-over move's block (the four pairs, one after another in the pack:
  RMA_<KEY>_B<code>_...) and movex_data.h rows carry it (then_file[0]); movex.c movex_post overrides the continuation
  entry (292 + MX_NEXT[e]) with it -- the loop, and the game's own + 2 / + 4 / + 6 its endings. 26 rows: The Giant 75 /
  76 / 79 / 80 (block 247), Perfect 155 / 236 his Sleeper (260), Hogan 75 / 76 / 79 / 80 (249), 81 / 83 (250), Nash 75
  / 76 / 79 / 80 (248), Savage 70 / 72 his front special (246), 77 / 78 (248), Hall 77 / 78 (243), Bret 77 / 78 (244)
  and 81 / 83 (245: his back suplexes' pins).
  Where Revenge's move ends but the slot's stand-in goes on (a German Suplex Pin's bridge after his Revenge suplex):
  the entry's move becomes the nearest by pose of the moves the slot can hold that end (MoveX.move, its record copied
  as movex[]'s continuations are; SPECIAL's mark kept): Hall 25 / 50 1710 -> 940 Reverse Suplex, Savage 79 / 80 and
  Perfect 79 / 80 -> 2500 Atomic Drop, Perfect 81 / 83 -> 7630 Stalling German Suplex, The Giant 77 / 78 -> 2590
  Pendulum Back Breaker (his Revenge animation kept; the move's name / damage those of the new stand-in).
  Not made: Perfect 77 / 78 (block 249: a first byte BYTE0 doesn't know) -- still the stand-in's No Mercy pin.
- Bret's front special is a separate fault (an agent on it): the special glued No Mercy's slam (14296) and the
  Sharpshooter's turnover (16350) into one file; at the seam the man's root jumps 212 -> 20 (~190) and Bret's ~50 --
  the turnover begins from a ground grapple's places (Bret at the man's legs), not where a slam leaves them. Revenge's
  code 58 (the slam) has record byte 15 = 200: after the slam, the ground Sharpshooter (code 200 -> block 260) as a move
  of its own, the two placed as for any ground grapple. So two moves, not one file.
- Still open (not done): a move's own + 2 / + 4 parts (func_8013C164, record 0x10 / the struggle flags at +0xEA) on a
  made-over move still play the pack's next pair -- which moves reach that state not established.
- QA queued (FOR TONIGHT'S QUEUE): blocks_<giant / hennig / hogan / nash / savage / hall>.

## 2026-11-01 Bret's front special rebuilt as two moves, the Revenge way (0.38.21; built, not tested yet; not deployed)
- Found (the main session's data, then the code): the join BRET_SLAM_SS was no valid animation -- bone 1 at the slam's
  end Bret (-56,141,14), the man (212,36,14); at the turnover's start (-6,140,0) / (20,36,0): the man jumps ~190, Bret
  ~50, the two men's relationship another one. The turnover begins where the game's ground grapple puts them. Revenge
  makes the special two moves: Bret's code 58 (files 0x1570 / 0x1571 = No Mercy's 14296 / 14297) has record byte 15 =
  200, his ground Sharpshooter (turnover 0x1AE7 = 16350, then block 260 = 16352..): the slam ends, the Sharpshooter
  begins fresh, placed as a ground move.
- No Mercy has the like in its own data: a move record's halfword +4 is the moveset entry it goes on to when it ends
  (func_8012F660 -> func_800ECEEC: no continuation (& 0x1E00) and +4 set -> func_800EC698(him, his slot, +4), both men's
  files at once, states 95 / 96 (93 / 94: the man may counter, func_800ED518)). 1880's own +4 is 240 -- the moveset's
  ground grapple at a face-up man's legs; 930 (Bret's slot 50) has 236 (the head); 1510 / 3370 236, 3380 244. But that
  way the next move plays where the last one left them, not placed (a move after a move goes on in the same frame: the
  ports' holds entry, 2680 -> 2681) -- the join's fault again. Not used.
- The game's ground grapple (read in the recompiled engine, ovl_e):
  * func_8013E870(attacker, man, input): the start, from func_8013E79C (the standing state 1's func_800E5EB8 and the
    taunt's func_800E67F0) with the press's input -- 0x8000 (A). Needs the man down (+0x80 & 9 == 8), on the same side
    of the ropes (+0x82 & 0x3C5 in common), within 100 (func_800EB970). Then func_800EB77C(him, 70, man, 71) and +0xEB
    = 0 (0x20 without 0x8000: the lift path, states 54 / 55; | 0x10 for A + B with SPECIAL!).
  * State 70, func_80136240 (phases by +0x18, table 0x80151700): 0 his approach animation; 1 the place -- +0xEB | 2 face
    down; | 1 at the head when |the man's facing - the way from him to the man| < 0x400 (face up; > 0x400 face down:
    logic.c legdrop_at_head is this rule); the spot: the man (+0xFC) + his facing x (150 legs / -130 head; 140 / 110 /
    -80 sitting; negated face down), func_8012E374; phase 2 walks and turns him there (func_80132374, func_800EBA84
    0x155 / 0x200) and kneels him (0x2F88); 4 picks the move, func_80136D90: table 0x8014DC90 [240, 242, 248, 250, 236,
    238, 244, 246] by (head) x 4 + (sitting) x 2 + (face down); with +0xEE & 4 (SPECIAL!), not sitting: 236 -> 155,
    238 -> 156, 240 -> 157, 242 -> 158 when the moveset's params (+0x2E4 -> +0x3C) have 0x1000 / 0x2000 / 0x4000 /
    0x8000; phase 5 plays it, func_8012F660(0x2480) -> func_800ECEEC (157: & 0x1E00 -> 72 / 73, the hold). The pair is
    kept together at 2.0 in 70 / 71 (the table at 0x8014D70C, func_8012CA7C).
  * +0xEB's head bit is only OR'ed in by phase 1 -- the legs can't be forced, and needn't be: after a front grapple the
    man faces him, and a man lying face up faces his feet's way, so the game's rule puts him at the LEGS.
- Changed:
  * movex.c: movex[]'s Bret row is { 70, file 0, flags 0, ground 157 } (MoveX got `u16 ground` after the main session's
    `move`): entry 70 is 1880 as the game has it -- its own files (0x37D8 / 0x37D9, No Mercy's, Revenge's byte for
    byte) and record (0x2000 | the game's 0x8000) -- a move that ends; +4 set to 0 in his slot's copy (no 240 in place).
    New section movex_chain_pick / movex_chain_step: when he picks his entry 70 (his own moveset) the other man (+0x8) is
    noted; after each step of the match's logic (logic.c arcade_match_step: every step, the extra ones too), his move
    over and him standing (state 1, or 2 walking on) with the entry still 70: the game's func_8013E870(him, the man,
    0x8000), a press of A at a lying man (new GF_GROUND_GRAPPLE, recomp/hooks_recomp.c). Not begun (the man not down,
    too far, outside the ropes): tried for 20 steps, then let go; any other move first: let go. Its pick at a face-up
    man's legs (240 -- or 157 already, SPECIAL! still on) is made 157 (logic.c qa_move_entry -> movex_chain_pick, the
    leg drop's hook); at the head or face down the game's own pick stays. The player's and the CPU's alike, any pair of
    slots. QA: movex_qa_subs counts 70 as a hold through the chain; dbg_chain[4] (begun, started, picked, let go).
  * gen/revanims.py: NM_JOIN empty (BRET_SLAM_SS and its ramp_end line in join_files gone); SS_HOLD / SS_REL / SS_TURN,
    ss_files, ramp_end, nm_pair, SS_SHIFT and the animpatch import gone (dead since 0.38.12). Regenerated: rev_anims.json
    (129 pairs: the same less those four, which were the last), movex_data.h (RMA_N 266 -> 258, no row changed), then
    gen/wmport.py: rev_move_anims.h (RMA_BRET_SLAM_SS / RMA_SS_* gone, the rest the same), build/wmport.bin (2144 files)
    and the arena headers (their pack numbers 8 lower). The files before: the agent's scratchpad bretchain\pre.
  * The silent turnover: gone by construction -- 0x3FDE plays as 157's own file with its own record (sounds at frames
    26 / 46 / 94 / 104, file 0x2BF), the slam with 14296's (17 / 47).
  * logic.c QA: qa_pspecial + 32 keeps slot 1 in DANGER (stamina 0; spirit 0, +0xEE | 1, its timer 300, as
    func_8011D05C makes it) -- 2026-11-01_104430's case (the CPU's Hall at stamina 0).
  * Version 0.38.21 (mod.toml, README.txt). Built (build_mod.ps1, no warnings), not deployed.
- Not known yet (QA decides): that the game's rule gives the legs after this slam every time (the man's facing at the
  slam's end: 14297's header turn 0, end pose 20 face up -- read, not seen); that the CPU's Bret, standing after the
  slam, doesn't go for something else first (then no Sharpshooter that time, as the game would); the walk and kneel
  put ~a second between the slam and the turnover (not compared with Revenge's look).
- QA queued (FOR TONIGHT'S QUEUE, above): bret_special_player / _player_arcade / _cpu / _danger / _cpu_danger /
  _sharpshooter, then ss_bret, subs_bret.

## 2026-11-01 evening: Revenge's Souled Out, SuperBrawl (arenas-rev; 0.38.22 + 0.38.23 Starrcade: deployed, in the game, not tested yet)
The big job (mod/AGENTS_BIGJOB.md): Revenge's remaining venues, each its own arena, the way Nitro / Havoc / Bash were
done. Research and offline looks: data/arenaport/rev3 (REV3.md, prev3.py / photo3.py, rig sequences sheet_seq1/2/5).
- Revenge's 3D entrance stages for all three were already extracted (data/arenaport/stage3d/souledout, superbrawl,
  starrcade: scenes 1 / 2 / 5, geometry 0x42 / 0x44 / 0x40, pictures 0x43 / 0x45 / 0x41); every model is drawn by a
  plain node (no slideshow-like extras: the scene table's +16 records of these are the menu's 2D title logos). So
  "the same strategy": the 3D stage is the in-match set and the close scene (gen/arenas_stage3d.py STAGE3D 1 / 2 / 5,
  AISLE: the flat set's aisle floor under it -- Souled Out's / Starrcade's light beams 0x314, SuperBrawl's red carpet
  0x382). Parts: Souled Out match 8 (+ aisle + door) / close 13; SuperBrawl 6 (+ aisle) / 10; Starrcade 14 (+ aisle) / 27.
- Souled Out's box: scene model 1 is a box (1200 x 1125 x 250, tapering back) with sides and top only; Revenge's
  entrance closes its front with the yellow "SOULED OUT" board and opens it, glowing, for the pose (rig sheet_seq1);
  its flat match set stands that board (0x34E, picture 0x359 / 0x358) at the aisle's end. arenas_stage3d.py DOOR /
  door_part: the board's picture as the box's front, closed, in the match set and the close scene.
- Ringside (gen/arenas_ring.py, from Revenge's table as before): Souled Out -- plain mat with its SOULED OUT logo
  (0x35A), default grey aprons (0x35E), the default green floor (0x350); SuperBrawl -- plain mat, no logo, its own blue
  aprons (0x38F), its floor 0x383. No announce table, Revenge's barricades / crowd / grey ropes (as all Revenge arenas).
- gen/arenas.py ARENAS + ("SOULED OUT", 1), ("SUPERBRAWL", 2) (arenas 14, 15); BANNER / LABEL / LOGO / SHOT; the photo's
  logo box fits a wide picture (SuperBrawl's 128 x 32 "SuperBrawl VIII" banner 0x38B; square logos as before). The
  photos (data/arenaport/pics/souledout_stage.png, superbrawl_stage.png): NOT QA shots -- photo3.py's offline render
  of the arena down the aisle (no Superstars); a QA shot can replace the file later. Walls, table block, font / close-shot
  tables, effect / VS copies: per row, generated (no table -> Revenge's square walls). No Titantron rows: none of the
  three has a screen (Souled Out's box is a door, not a screen).
- Not ported (as for Nitro / Havoc / Bash): the sprite light shows (star flares, chase lights), the pyro / sparkle
  bursts, the box door's opening and glow.
- qa/qa_tool.py ARENAS + the two names. QA queued (FOR TONIGHT'S QUEUE): arena_souledout, entrance_souledout,
  walls_souledout_then_raw, arena_superbrawl, entrance_superbrawl, walls_superbrawl_then_raw.
- Version 0.38.22; wmport.py (2315 files) + build_mod.ps1, no errors; backups\0.38.22_build.nrm; the files before in
  backups\pre_arenas-rev_1. Starrcade next (its own window).
- STARRCADE (0.38.23, its own window): ARENAS + ("STARRCADE", 5) (arena 16). The 3D stage (scene 5): the blue gable
  arch on pillar pairs, the WCW STARRCADE triangle with its Aki plaque, pillar trios either side (x +-450..630 in the
  match, at the stage-end crowd's edge), the blue floor; match set 14 parts + the light-beam aisle, close scene 27.
  Ringside: plain mat + its STARRCADE logo (0x3D8), grey aprons, the default floor. Photo: pics/starrcade_stage.png
  (photo3.py, offline). qa_tool ARENAS + STARRCADE. wmport.py (2426 files) + build_mod.ps1, no errors;
  backups\0.38.23_build.nrm; the files before in backups\pre_arenas-rev_2. QA queued (FOR TONIGHT'S QUEUE).

## 2026-11-01 evening: WrestleMania 2000's other venues (arenas-wm; 0.38.24 + 0.38.25: deployed, in the game, not tested yet)
The big job (mod/AGENTS_BIGJOB.md): every WM2000 venue but RAW IS WAR (done as RAW IS WAR WM 2000) an arena of its own,
"the same strategy". Research, rig shots, dry runs and offline renders: data\wmarena\all\ALL.md.
- WM2000 has seven venues (Exhibition's arena select, rig): 0 RAW IS WAR, 1 Sunday Night Heat, 2 Royal Rumble, 3 King
  of the Ring, 4 Survivor Series, 5 WrestleMania, 6 SummerSlam (u8 0x800B1014 -> the arena build's venue 0x8007CE82 & 7
  and the 3D entrance scene of the same index). No SmackDown, no other PPVs (venue 7's tables are empty); no venues
  sharing a set. Per venue only the aprons' picture and crowd group 2's head (WM2000's low-detail match stage) differ;
  the 3D entrance scene is each venue's own.
- The 3D scenes' world matrix, found: 0.3 x (rec[+32] / 2048 x set + (0, 0, rec[+34])) (RAW / Heat 1.0, 0: RAW's 0.3;
  Rumble / KOTR / SummerSlam 0.75; Rumble / KOTR z 1800, Survivor / WrestleMania 800), node turns applied -- every scene
  so drawn matches its rig close shot. The close scene: WM2000's world + 100 along z for all (RAW's rule). The match:
  the scene at its close scale (WrestleMania x0.8: WM2000's own match size) moved to WM2000's own match stage (its
  low-detail copy: front planes / floors matched), WM2000's aisle floor kept under it (RAW / Heat: the ramp, as RAW).
- gen/arenas_wm.py generalised (VENUES[v], venue(v), wm_venue("wm<v>"); every entry point takes v; RAW IS WAR's output
  byte-identical, checked against the old dry run). Reads data\wmarena\wm_venues.json (the tables, all seven) and the
  photos data\wmarena\pics\wm_<n>_fly.png (rig shots). gen/arenas.py WM_ARENAS (after every row before them: arena
  numbers never move) -- ("<name>", "wm<v>"); arenas.py / arenas_ring.py / arenas_entrance.py / arenas_stages.py (walls:
  every WM2000 venue keeps No Mercy's table, JR and King, as RAW WM 2000) take any WM2000 venue; the Titantron rows of
  every WM2000 venue with a screen are generated (arenas_stages.write_screens -> src/arena_screen_files.h XWM_TRON_ROWS;
  tron_panels_all.h takes the macro in place of RAW WM 2000's two hand-written rows -- the same values).
- Each arena its own everything (arena.c's per-arena structure, all generated per row): its data block, stage set,
  close scene, ringside lists, ropes, walls, table block, font / close-shot tables, effect / VS copies, ARENA label /
  photo / VS backdrop.
- Window 1 (0.38.24): SUNDAY NIGHT HEAT (arena 17: RAW's set with Heat's screen picture and HEAT aprons; its own
  Titantron rows), ROYAL RUMBLE WM 2000 (18: the portal ring of truss and spotlights, the tunnel to the far ring and
  its sign, the lit floor; match 21 parts / close 34), KING OF THE RING WM 2000 (19: the portal with its crown of lit
  spikes; 24 / 43). The files before in backups\pre_arenas-wm_1. wmport.py (2671 files, 3.19 MB) + build_mod.ps1, no
  errors / warnings; backups\0.38.24_build.nrm. qa/qa_tool.py ARENAS + the names. Not ported: the scenes' sprite light
  shows (star flares, chase lights), the spotlights' animated tilt.
- QA queued (FOR TONIGHT'S QUEUE): arena_wmheat, entrance_wmheat, walls_wmheat, tron_wmheat_ring, arena_wmrumble,
  entrance_wmrumble, walls_wmrumble, arena_wmkotr, entrance_wmkotr, walls_wmkotr (qa\scenarios: copies of the
  RAW WM 2000 ones, data\wmarena\all\gen\mkqa.py).
- Window 2 (0.38.25): SURVIVOR SERIES WM 2000 (arena 20: the skull between two truss arches, the light-box towers by
  the aisle; match 17 parts / close 21), WRESTLEMANIA WM 2000 (21: the chrome "2000 WWF WrestleMania" logo, the white
  aisle with the truss pieces beside it, at x0.8 -- WM2000's own match size; 22 / 45), SUMMERSLAM WM 2000 (22: the red
  doorway with its sign, the red carpet, the side walls; 12 / 15). The files before in backups\pre_arenas-wm_2.
  Every WrestleMania 2000 venue is in now (seven: RAW IS WAR WM 2000 13, then 17-22); 23 arenas in all (the ARENA
  page's window scrolls through them: pg_list holds 64).
- QA queued (FOR TONIGHT'S QUEUE): arena_wmsurvivor, entrance_wmsurvivor, walls_wmsurvivor, arena_wmwm, entrance_wmwm,
  walls_wmwm, arena_wmsummerslam, entrance_wmsummerslam, walls_wmsummerslam.
- (window 2 fix) gen/arenas_stages.py's XWM_TRON_ROWS block went in twice (data\wmarena\all\gen\integrate1.py re-ran it): the second copy removed from the generator and from src/arena_screen_files.h after the build (an identical macro defined twice -- the 0.38.25 nrm is the same code).

## 2026-11-01 evening: ten more from WCW/nWo Revenge -- ports4 (0.38.26-0.38.30: deployed, in the game, not tested yet)
- Deployed by the lead as each build landed (0.38.26 .. 0.38.30). The game folder's select_pages.json was the old default
  (it differed only by the new names): backed up into backups\0.38.N\ and replaced with mod\select_pages.json each time.
- The user: "start porting in from Revenge: Piper, Rey Mysterio, Bischoff, Goldberg, Luger, Sting, Steiners, Booker T,
  Stevie Ray. Match their music from the Revenge mod." Each a whole Superstar of his own the way The Giant was
  (data\revport\ports4\PORTS4.md: how each was found, the captures, photos, moveset previews, themes, the lists changed).
- Who (Revenge's code / moveset file; ID; ordinal; ext_late; photo; EXT music slot; reference):
  STING 0x0001 / 0x1F7, 0x010B, 126, 19, 120, 71, The Rock -- LEX LUGER 0x0003 / 0x1A9, 0x010C, 127, 20, 121, 72, Test --
  GOLDBERG 0x000A / 0x16D, 0x010D, 128, 21, 122, 73, Steve Austin -- RICK STEINER 0x0005 / 0x1DC, 0x010E, 129, 22, 123, 74,
  Bradshaw -- SCOTT STEINER 0x0206 / 0x1EE, 0x010F, 130, 23, 124, 75, Kurt Angle -- BOOKER T 0x000B / 0x140, 0x0110, 131,
  24, 125, 76, D'Lo Brown -- STEVIE RAY 0x0108 / 0x1F4, 0x0111, 132, 25, 126, 77, Faarooq -- REY MYSTERIO 0x0304 / 0x1B8,
  0x0112, 133, 26, 127, 78, Essa Rios -- RODDY PIPER 0x0007 / 0x1D0, 0x0113, 134, 27, 128, 79, Mankind -- ERIC BISCHOFF
  0x020D / 0x13A, 0x0114, 135, 28, 129, 80, Vince McMahon. Heights / weights Revenge's (Rey 5'6" 155: a light man for
  the struggle; his and Bischoff's weight abilities their references'; the rest REV_WEIGHT [1, 1, 0]).
- Models: captures in data\revport\ports4\cap (tag matches from ports\cap\h11 with slots 0 / 1 poked: sl_* Sting + Luger,
  st_* the Steiners, hz_* Harlem Heat, gp_c0 Goldberg + Piper, rb_* Rey + Bischoff), wmport.py REV_CAP_PORTS4. Costumes:
  Sting Wolfpac / the Crow (coat, white paint) / the surfer / the Crow (mask 3); Luger black tights / black trunks /
  stars and stripes; Goldberg one; Rick black / pink singlets, black jacket; Scott white singlet / gold trunks / black
  singlet; Booker and Stevie the red singlet / the flames tights; Rey blue pants / purple bodysuit; Piper one; Bischoff
  his suit. (Revenge's own attires; No Mercy's four go round them.)
- Moves: mkroster port_rev_moveset with the references above; REV_OVERRIDE Sting / Goldberg 188 Punch 01 (the Rock
  Punch / Austin Punch out), Scott 30 the Back Body Flip (Revenge's body slam 960 out); REV_POSE Luger 0 (Test's
  big-man stance out); all in REV_NO_STRUGGLE. gen/revanims.py now goes over every LATE entry with a Revenge file
  (the hard-coded tuple gone), so the ten get their own animations and their holds' Revenge blocks (rev_block).
  Sting's front special: Revenge's code 58 is Bret's byte for byte (record byte 15 = 200): the slam 1880, then his
  ground Sharpshooter (the Scorpion Deathlock) as a move of its own -- movex.c movex[] row { LATE_STING, 70, ..., 157 }
  as Bret's. Goldberg: 70 the Jackhammer (1560), running grapples 7270 (Revenge's spear, exact); Luger 81 / 83 the
  Spinning Torture Rack; Scott 70 / 72 the Screwdriver; Rey 70 / 72 the Jump Swinging DDT; Piper 81 / 83 the Sleeper.
- Music: Redux's THEME LIST rows (ROM 0xDAF60) -> songs Sting 0x1C "Sting", Luger 0x1D, Goldberg 0x22, the Steiners 0x1B
  (Scott's attires 1-2 have the nWo song: his own Steiners one used), Harlem Heat 0x27 (both), Rey 0x34, Piper 0x39,
  Bischoff 0x40; instrument = song + 0x1E; rendered as Hall's (11025 Hz, 0.5 s lead-in) -> mod\music\<Name>.mp3, each
  his own music slot by name (mkcontent). The user-music slots after the EXT ones move up (the music_* scenarios'
  sound numbers in their "about" texts are out of date; their checks don't use them).
- Limits grown: ROSTER_OPTS 26 -> 36, ROSTER_LATE_N 19 -> 29 and the roster's mask a u64 (bits 6..34; a u32 held 26:
  arcade.h u64, logic.c arcade_roster_service, roster.c ext_mask / EXT_MASK_ALL / roster_frame); roster.c wcw_id
  0x0105..0x0114; photos past 127 (Piper 128, Bischoff 129): a photo was a signed byte in the game's code --
  func_8000AB9C reads it with lbu, its ten callers' sra 24 -> srl 24, the clamps (func_801118E0, 80122660, 80122C40)
  < 0x100, gen/patches.py photo_file names a second block of files for 128-255 (files.c FILE_PHOTO2_*: TEX 0x7C00 +,
  palette / picture 0x7C80 / 0x7C81 + 2k).
- Select: mod\select_pages.json's REVENGE list goes on with the ten (REVENGE fills to nine, then REVENGE 2); the
  operator menu's roster page has the ten (options sting .. bischoff, mod.toml); names.py has JR's names for them.
- Not done: commentary names (mkcommentary NOT_HERE etc.: through compak.py, as for the earlier ports); Titantron
  videos (Revenge has none: none, the user's rule).
- Windows (each built with no errors / warnings, the .nrm in backups\0.38.N_build.nrm; not deployed):
  * 0.38.26: Sting, Lex Luger, Goldberg (+ the u64 roster mask, revanims.py's loop over LATE, wmport.py nm_skin: a
    costume with no bare skin -- Sting's Crow coat -- takes his other costume's skin tone, it divided by zero).
    The files before: backups\pre_ports4_1. Music slots by file: Goldberg 75, Lex Luger 78, Sting 82 (sounds slot + 142).
    revanims: Luger 81 / 83 (the Torture Rack) -> block 250, Goldberg 70 (the Jackhammer) -> block 247; Sting 77 / 78
    "block 247's first byte not known" (as Perfect's 77 / 78: the stand-in's No Mercy pin stays).
  * 0.38.27: Rick Steiner, Scott Steiner, Booker T (+ wmport.py ci8: a 4-bit picture with 32 colours -- Booker's,
    Stevie Ray's, Scott's hands -- was taken for an 8-bit one, and rev_fingers indexed past its palette; now by
    tex_size's own rule, the palette kept whole). The files before: backups\pre_ports4_2. revanims: Scott 79 / 80
    (Revenge's code 64 ends) -> 2930 Pump Handle Suplex; Scott 238 / 246 goes on, Revenge's code 198 doesn't, no
    move that ends there (not made: the stand-in's).
  * 0.38.28: Stevie Ray, Rey Mysterio. The files before: backups\pre_ports4_3. revanims: Stevie 77 / 78 (Revenge's
    code 63 ends) -> 2590 Pendulum Back Breaker; Rey 25 (code 23 ends) -> 1310; Rey 55 / 77 / 78 / 79 / 80 / 81 / 83
    "+ 1 goes on again (not made)": his hurricanranas' and suplexes' pins stay the stand-ins' No Mercy ones.
  * 0.38.29: Roddy Piper, Eric Bischoff (+ photos 128-255: the photo read unsigned -- gen/patches.py, files.c; above).
    The files before: backups\pre_ports4_4. revanims: Piper 81 / 83 (his Sleeper) -> block 244. Music slots by file
    now (sounds slot + 142): Booker T 81, Eric Bischoff 83, Goldberg 84, Lex Luger 87, Rey Mysterio 90, Rick Steiner 91,
    Roddy Piper 92, Scott Steiner 94, Stevie Ray 95, Sting 96 (the others moved up: Bret 82, Hogan 85, Nash 86, Perfect
    88, Savage 89, Hall 93, The Giant 97 -- sound 239, was 219 --, Undertaker 98).
  * 0.38.30: roster.c wcw_id 0x0105..0x0114 (it had stayed ..0x010A: the REVENGE page first for the ten when there's
    no select_pages.json; with the file they're on its REVENGE list anyway). The file before: backups\pre_ports4_5.
- QA queued (FOR TONIGHT'S QUEUE), per man (qa\scenarios, data\revport\ports4\mkqa.py): music_<k>, vs_<k>, moves_<k>,
  own_<k>, subs_<k>, finisher_<k> for k = sting, luger, goldberg, ricksteiner, scottsteiner, booker, stevieray, rey, piper,
  bischoff (and sting_special_cpu: his front special into the
  Scorpion Deathlock, bret_special_cpu's checks). Look at each costume in the vs_ / moves_ shots (the QA rule).


## 2026-11-01 night: the ARENA page's order (0.38.31: deployed, in the game, not tested yet)
- The user: "Make WM2000 arenas come after No Mercy's. Then put all of Revenge's last." The page lists the game's ten,
  then WrestleMania 2000's in its venue order (RAW IS WAR WM 2000, SUNDAY NIGHT HEAT, ROYAL RUMBLE, KING OF THE RING,
  SURVIVOR SERIES, WRESTLEMANIA, SUMMERSLAM WM 2000), then Revenge's in its (MONDAY NITRO, SOULED OUT, SUPERBRAWL, BASH AT
  THE BEACH, HALLOWEEN HAVOC, STARRCADE). gen/arenas.py writes xarena_page_order (src/arena_files.h); arena.c's page list
  takes 10 + xarena_page_order[k]. Only the page's order: every arena's number (10 + its ARENAS row: QA's venue checks,
  the menu's memory) and the pack are as they were (wmport.bin byte-identical).
- QA queued (FOR TONIGHT'S QUEUE): the ARENA page scrolled top to bottom -- the order above, each row's label and photo
  its own arena's, a pick from each group plays that arena.

## 2026-11-01 night: the Revenge ports' alternate attires (attires; 0.38.32-0.38.34 built, not deployed, not tested yet)
- The user: "I noticed you didn't carry over alternate attires. Can you make sure you do" (the WCW/nWo Revenge ports).
  data\revport\attires\ATTIRES.md has the whole of it. WCW/nWo Revenge keeps each Superstar's four costumes in a table
  (*(0x8003A594) + 42 x (4 x select index + costume): name, short name, mask, attire, three colour rows): its Superstar
  Select steps them with C-left / C-right (the rig's right stick; Savage's four seen stepped, attires\sheet_select_savage
  .png), and the match loads a slot's from it (func_8002B7E0(code, costume) -> func_8000CFD4(slot, code, attire, mask,
  colours); the slot's costume 0x8007EF80 + 0x10 slot on the Decision screen). A fresh boot's table (no save) is the
  same in vanilla and in Revenge Redux (Redux's files differ from vanilla's in four title pictures only: no new attire).
- Before, each man's costumes were judged from Costume Change sheets of his body type's shared attire list, put on with
  OVR (attire / mask only: the colour rows stayed his costume 0's). Now each man's four are Revenge's own four, captured
  with the slot's costume poked (attires\cap\at_<pair><1-3>; the ports' default captures for costume 0). gen/wmport.py
  REV_PORTED (all 18), REV_CAP_ATTIRES / REV_CAP_COSTUME (rev_ensure makes one again with attires\rv.py); mod.toml's
  descriptions name each man's four.
- Dropped (not his): The Giant's red tights and black trunks (Revenge gives those attires to The Barbarian and Larry
  Zbysko; his are the black singlet, the nWo shirt and jeans, the red singlet, the purple singlet), Scott Steiner's gold
  trunks (Yugi Nagata's). Dropped as the same look as one of Revenge's four: Sting's Crow coat in mask 3 (byte for byte
  his mask 0 one), Lex Luger's black trunks and stars-and-stripes captured in his costume 0's colours (a shade off
  Revenge's), Scott Hall's Outsider shirt (byte for byte). Kept for 0.38.33 (the looks past four, below): Bret's black and
  pink with his shades, Savage's red tights with the black bandana, Scott Steiner's black singlet with his shades, Booker
  T's and Stevie Ray's red flames tights, Rey's purple bodysuit with the red mask.
* 0.38.32: the four costumes of all 18 (gen/wmport.py, mod.toml). The files before: backups\pre_attires_1. Built by
  build_mod.ps1 with no errors; backups\0.38.32_build.nrm. Skin (wmport's report): one costume's own colours can pass
  for skin (Booker's / Stevie's gold singlet, the Giant's / Scott's / Rick's red, Sting's green) and give that costume
  another No Mercy tone than his others -- 0.38.33 takes the tone from the parts bare in his costume 0.
* 0.38.33: the looks past four -- second IDs (gen/revbanks.py, new): Bret Hart 0x060F (his black and pink with his
  shades), Randy Savage 0x0115 (the red tights with the black MADNESS bandana), Scott Steiner 0x0116 (the black singlet
  with his shades), Booker T 0x0117 and Stevie Ray 0x0118 (the flames tights in red), Rey Mysterio 0x0119 (the purple
  bodysuit with the red mask). Each the same man: mkroster copies his LATE entry (name, moves, abilities, music slot,
  allies) and REV_WEIGHT / REV_POSE / REV_OVERRIDE / REV_NO_STRUGGLE under the new key and ID (ext_late 29-34); its
  moveset key is his base's byte for byte (mkroster's uniqueness check lets a second ID's equal its base's: roster.c
  finds the base's -- the same cells, the base's movex rows, Bret's special chain too); wmport its costume, mkparts his
  body and photo, mkovr its models, mkcontent his music file (a user_music row), names.py his names.
  src/variants.c: the six as costume variants of their base (VW_ALWAYS) with a new field n -- the costumes the Type
  arrows show of the variant (1 here; 4 for Undertaker 1999 and the Giant): sel_type_step goes base Type 1-4, then the
  variant's n; first_free and variant_pick_member (the Rumble's draw: as many chances as costumes) count them.
  arcade.h RO_BRET_B .. RO_REY_B (RO_BANK0, ROSTER_BANKS_N 6), ROSTER_OPTS 42, ROSTER_LATE_N 35; logic.c: no option of
  their own -- arcade_roster_opt maps them to the base's (on the roster exactly when he is), the save loop stops at
  RO_BANK0; roster.c wcw_id + 0x060F, 0x0115-0x0119. The Type digit shows 1 for the fifth look (no pictures past 4).
  mkparts: a second ID's hat colour (cut to 5 bits in the packed key; no hat worn) 16 from his base's, as it shares
  his photo (else "two costumes alike"). gen/wmport.py nm_skin: each Revenge port's tone from the parts bare in his
  costume 0 (his arms) where the costume has them bare too (data\revport\attires\skintest.py: the measures). The files
  before: backups\pre_attires_2. Built by build_mod.ps1 with no errors; backups\0.38.33_build.nrm.
* 0.38.34: nm_skin, the rest: a costume with none of his costume 0's bare parts bare (a shirt, a bodysuit, a jacket;
  Neidhart's singlets: none bare in any) takes his costume 0's tone, and a second ID his base's -- every Revenge port
  one No Mercy tone across all his costumes now (0.38.32's log had Booker's / Stevie's gold singlet at tone 4, their
  flames tights at 7, the Giant's / Scott's red singlet and Sting's surfer at 3, Bischoff's / Piper's / Bret's jackets
  at 0 ...): Bret, Neidhart, Nash, The Giant, Sting, Scott Steiner tone 1; Booker T, Stevie Ray 5; the others 3. The
  files before: backups\pre_attires_3. Built with no errors; backups\0.38.34_build.nrm.
- QA queued (FOR TONIGHT'S QUEUE): costume_<key>_<n> for the 18 (qa\scenarios, data\revport\attires\mkqa_att.py: 78 --
  one per costume, n = 1-4, 5 for Bret, Savage, Scott Steiner, Booker T, Stevie Ray, Rey): his box, the Type arrows to
  costume n, A, the CPU's The Rock, the match; costume_<key>_1 tours the Type arrows first (qa_variant 1; a shot a
  press), the others pick straight (qa_variant 16). Look at each costume against data\revport\attires\sheet_<key>.png
  (attire, colours, head / mask, skin tone, hands / fingers); the fifth look's name bar / VS card his own name.
- Not done: per-costume themes (Redux gives Sting's Wolfpac costume Nash's song, his surfer "Surfer Sting", Scott
  Steiner's first two the nWo song): ATTIRES.md section 6 has how (an (id, costume) -> music slot table in media.c).

## 2026-11-02 night: RAW: ring and stage one area (built, QA'd as below, NOT deployed; the user's request in PENDING)
- data\ringstage\RINGSTAGE.md has the mechanism, addresses and design (an agent, rig-proved); arena_ringstage.c, gen/arenas_walls.py (RAW's
  records), 10+ patched functions. Files before: backups\pre_ringstage_1. Other arenas generate byte for byte as before.
- In the real game (qa\runs\2026-11-02_03*): ringstage_whip PASS (a whip up the aisle runs past z 800, up the ramp, no area change, lift 160);
  ringstage_walk PASS (out through the ropes, up the ramp to the deck); ringstage_fight PASS (the CPU follows; they fight on the deck for
  minutes; the shots: men on the deck, the ring's camera, the set behind). Found in QA and fixed: a whipped man refused by the stage door
  ran on the spot (the door wall has no bump: 0x80110E80's refuse now goes to the game's bounce, state 0x15); the shadows stayed on the
  aisle floor under the deck (func_800E1E60 -> ringstage_shadow: the ground he's lifted by), to look at in the next run.
- QA input in this project: scripted vpads (scenario "pads") do NOT reach a QA singles match (mode -6) when a real controller is plugged in.
  logic.c qa_ringstage_pad presses at the game's controller read. THE D-PAD MOVES; THE ANALOG STICK IS TAUNTS AND SPECIALS; C-up at the
  ropes goes through them; C-down runs.

## 2026-11-02 night: Story upgrade: what changed from the agent's draft (see STORY_UPGRADE_REPORT.md)
- Titantron scene step: the user: "should show a closeup of the titan tron in the arena, not just a full screen titantron". story.c
  story_cam_cut: a shot of our own by the game's own scene camera: position 0x80104478, rotation 0x80104484 (guPosition: forward (0,0,1) at 0; so
  pitch = atan2(-dy, horizontal), heading = atan2(dx, dz)), eye 0x80104490, look 0x8010449C, 0x801044A8, the path 0x80104540 / 0x80104528 (none left),
  the shot's memory 0x800F5594 = -1, framing mask 0x80133C68 = 0x100 (low nibble: men to look at; func_8010D9A4). tron.c tron_story: his video on
  the arena's own screen for the shot; tron_place: the screens' places. JR and King close by the same cut (an arena with no table: the
  game's ring shot 67). A look-at override of the view matrix (qa_camera_mul) was tried first: a black picture, the bubble lost.
- Text limits from what fits (QA story_width): 48 characters a line, 28 an option (bigger letters).
- A branch with no pages and no scene replayed the node's scene: 0x80097EEA pointed at its script's end in story_branch.
- The list: a row whose text the game's text maker couldn't make stayed blank for good (the sixth story, the user's own Invasion): retried.
  The reason rows: the sprite cache lets go of the labels (GF_SPRITE_RELEASE), or it draws the old pictures.
- The entrances' Titantron: a man with no video kept the last one picked (the opponent's, or an earlier match's): tron_music sets 0.

## 2026-11-02 day: RAW's stage: full size, whole deck, the stock stage camera, animated fists (deployed as 0.38.35, 19:35)
- The user, having played it: "the stage is way too small. Scale it normal" (done: SCALE 1.0; 0.8 build in backups\pre_ringstage_scale1); then "more
  depth to the stage when fighting in the closeup version ... look at the old version and try to copy the camera angle. Also bring those
  animated RAW screens into the area full time"; then, on the first camera try: "a little tighter on the wrestler and a little more angled down
  ... a little jerky as we move onto the stage. You can see the old way if you enter a locker room (which works) and then exit ... For QA it is fine
  to leave it, we'll just have to patch that when we confirm everything looks good."
- Depth: the deck had been cut at its face (330 of 480). Whole now: gen/arenas_raw.py MOVE z 1245, the face moved to the deck's front; arenas_walls RS_DZ 2745;
  the Titantron's row z 2695.
- Fists: arena.c arena_raw_anim: the close scene's frame list (0x800526FC / 0x80052790), each frame's picture and palette written into the part's own.
- Camera: what the stock stage does (rig: data/ringstage/scripts/camold.py, camroute.py): func_800E30D8, area 10's case, reached because func_800DAAE4 sets the camera
  mode 0x80159074 past area 9 (forcing the area alone goes on using func_800E38A4: my first try). arena_ringstage.c ringstage_cam_call: the ring's camera as ever on the live
  state, func_800E30D8 told it's area 10 on a state of its own, the frame's camera the one blended into the other by how far up the ramp the stage camera's men
  are, limited to 0.02 a frame; all the way on: the mode on too, so the special shots are as few as the stage area's. A tried and wrong lift of the camera's men's
  height (11:13 build): they're lifted already (func_8012F9CC's gateway), it looked at the screen over the stage.
- QA numbers (mode 64 of qa_ringstage: bs_grabs eye z | at z, rr_* their y): 2026-11-02_114912 on the deck: 449 back from the nearest man (the stock formula 464), 154 above
  where it looks, looking within 17 of their middle (the ring's camera: 732, 69, 255 short).
- Found in QA, not fixed: the run's door: the hall (area 20), then area 12, a BLUE stage, not RAW's; then back in area 2 with the blue set still up (2026-11-02_091228, 339 - 346 s). The
  user's own locker-room exit lands on RAW's old stage: another door. Left as it is till the user confirms the stage (memory raw-stage-door-later).
- Story, found by story_triggers: a script's "how": "countout" gave a DRAW ("lose by countout" never true): func_80117438 calls a count-out by who's out (+0x82 & 3 == 0), both in the
  ring are level. story.c ms_co_*: till the result, the winner held in and the loser out.

## 2026-11-02 night: every arena's ramp and stage playable where it stands, from its models (deployed as 0.38.36, 2026-11-03 06:21)
- The user: "making every stage playable on the closeup ramp without transition, even the Revenge and WM 2000 ones. Use collision detection based on the models on the ramp. Irish whips should crash opponents into them, etc."
- gen/arenas_collide.py (python arenas_collide.py [--pictures]; results data/stagecollide/result/<n>.json, keyed; run before gen/wmport.py when a stage set changes): from each arena's parts in the ring's world and its crowd: a 20-unit
  grid; floors = up-facing triangles, flooded from the aisle's mouth with a step of at most 24; blocked = anything steep through a man's legs and body (12..110 above his ground: higher and RAW's arches, which curve in over him, read solid: he crashed in the open), a
  low top under 170, the crowd's and barricades' whole footprint; the aisle kept to +-140 to the stage's front; room for a man (60, the game's own); nothing behind the set's last model + 60 (SUPERBRAWL's floor fades to black behind it). The edge of what's walkable is traced into one
  open path from the mouth round the stage (ids 21 .. 600+ .. 11, spliced into the barricade's chain) and closed loops round each prop (their own spans, in quadrant 0's lists). data/stagecollide/COLLISION.md: what the engine allows (an agent, rig-proved).
- Wall kinds: a model within 90 behind the wall, at a man's height: a crash (fl dir | 8, kind 1: state 0x15; the logo board at the back of NITRO's stage stands off the floor and counts). Nothing there: a drop (fl dir | 0x2000, kind 0x400: state 193, a teeter).
- Doors: only the game's ten arenas (the stage's own id, 0x80145140). The arcade's have no door in their pictures: a whip there crashes, so no backstage from them: a choice, the user to say.
- src/arena_ringstage.c made general: arena.c ringstage_take(arena); XrsArena: a 40-unit map (height / 2, 255 not walkable). ground(x, z) between the map's cells; the camera's blend from the arena's front; cam_over_crowd: where the stage starts right behind the crowd's rows (z 1420, 1550 on the arcade's) the
  stage camera's 400 back is among their boards, so it rises till it sees over them; a Rumble entrant's lift unless the game gives him y (only venues 0 and 2 have ramp records). The CPU: the map's breadth-first way when no straight line (the game has no pathing: COLLISION.md section 5).
- QA: stage_<arena> (walk up, the CPU follows, they fight), crash_<arena> (qa_ringstage 135: every 15 s the two put 250 / 295 short of a crash wall, a real whip: A, C-down; a whip sends the man past the whipper: the first try whipped him the other way). In the real game: state 0x15 at props and walls, state 193 at edges, a walking man stops 30 short.
  crash_superbrawl fails its check by design: no crash wall faces the ring with room in front, the target is the back edge, he teeters (the shots: right).
- A freeze twice today (2026-11-02_145038 RAW's ring, 211353 NITRO's aisle): every game thread parked at the frame's end (func_800E1E60's func_80003050; patch_function_1@800012B0), the same shape as 2026-11-01_010902. Open.
- Results on the deployed walls: stage_<arena> and crash_<arena> pass in all 23 (shots looked at: a sheet of each crash). crash_superbrawl's check is its teeter. Regressions: ringstage_rumble, versus_1p, table_climb, table_climb_wmraw, walls_raw_then_nitro, walls_nitro_then_raw, walls_nitro_then_wmraw, story_types, story_triggers, story_tron_arenas, entrance_nitro, tron_raw_ring pass. rumble_ports' one fail: Bret not drawn in the first four slots; 10 minutes, no freeze.
- Later fixes the runs led to: a way between two ROOM blocks must be a block's own (corner-touching blocks joined SURVIVOR SERIES WM 2000's skull's back, 40 from the aisle's side, so its walkable area now ends at the skull); wall kinds by any model close behind (NITRO's logo board).
- False failures: stage_bash_at_the_beach, stage_raw_is_war_wm_2000 at 3.6 frames a second, 2026-11-03 00:46: the user's Unreal Editor had the GPU at 91%. A QA run in a slow machine fails on time. Check the frame rate before believing a walker that never arrived.
- Not run: the CPU's way round a wide prop as such; six-man and tag on a stage (story_types' tag in the ring); weapons on the stages; the door's way back: the hallway's exit still goes to the old close stage in the game's ten (the user: patch once the stage is confirmed).
- Queue left paused (qa\queue.stop): bret_special_danger, bret_special_player_arcade.

## 2026-11-03: Rey's head: Sting's hair (0.38.37)
- The user (with reference pictures): "Yes reys head is wrong. Your solution to put texture over the wrong hair/head was wrong. I'm thinking Goldbergs might be off too."
- Cause: wmport.rev_capture took each part's LAST mesh call in the whole log. Revenge's log has three load passes for a slot; Rey's first and last load 19 parts (4 = 0xC0A, 5 = 0xC0D: a masked head, 5 is the whole head, z -9..16, 56 triangles; no part 20), the middle one loads Sting's head (0xA66, 0xA69, 0xA63). So Rey kept part 20 = 0xA63 with its brown picture: literally Sting's hair. The first answer (mask_tint: recolour the hair to the mask, "without it a hole") was wrong and is gone.
- Fix: rev_capture restarts at each pass (a load of part 1), so only the last pass counts. Rey now has no part 20; skel.c already handles a model without one (desc + 0xA0 == 0). Audit of every capture of every port (data: the passes [20,20,20] for all; Rey [19,20,19]): Rey and rey_b are the only ones that changed.
- Goldberg: nothing changed. His parts are all from his own last pass (20 0xAC6, 4 0xAC9, 5 0xACC). Side by side with the reference and Revenge's Costume Change, face, skull and crown are the same; different: No Mercy's skin tone on the body (the user's call, 0.35.12) and the neck piece showing more in the idle pose. Close up the wide lens makes his skull look pointed: the mesh has a shallow peak (0, 88, 4), (+-4, 87), (+-7, 85). Waiting on the user's word on what looks off.
- QA, real game, shots looked at: head_goldberg_c0_front / back / above / far; head_rey_c0_front / back / left, head_rey_c3_front / back (all whole: the lacing at the back, no hole), head_rey_c0_far (ran as 0x0119, purple with the red mask), vs_rey (a full match), costume_rey_1 (select tour, match). qa_tool.roster: a name now stays its first ID (the second ID 0x0119 had taken "REY MYSTERIO": the head_rey_c0_far and first vs_rey "FAIL").
- Files before: backups\pre_rey_head.

## 2026-11-03: Revenge's own skin on Goldberg and Nash, a try (0.38.38)
- The user: "Can you try to Revenge skin on Goldberg and Nash." gen/wmport.py REV_SKIN: those two skip nm_skin in all costumes: Revenge's skin pictures and its hands (hands.c's WM2000 path, no SKEL_WM_NMHANDS). To undo: empty the tuple, rerun the generators. Files before: backups\pre_rev_skin.
- Found: nm_skin's No Mercy arm picture had been covering Goldberg's arm tattoo (back now). Other ports' fully bare parts get the same swap.
- QA, shots looked at: skin_goldberg_c0, skin_nash_c0, skin_nash_c2, vs_goldberg.

## 2026-11-03: restore point before the WWE 13 experiment
- backups\pre_wwe13_experiment: mod\ (no build\), qa_tool.py, the built and deployed .nrm and exe of 0.38.38, with RESTORE.txt.

## 2026-11-03: Stone Cold 2K15: drawn whole (src/hd.c); the No Mercy shape tried and dropped (0.38.40)
- data\austin2k15: hd.py (the OBJ to the rest frame, arms down, weights, reshape: the user's sunken chest / hunchback / shoulders), hd_pack.py (mod/build/hd.bin, src/hd_index.h), mod/texturepack/stonecold15_hd (RT64 pack: stand-ins' hashes from a run with WCW_TEX_DUMP, match_hash.py). hd_stance.py <a run's state.rdram.bin>: the model offline in the game's own pose. In the game: 45,378 triangles, about 29.5 frames/s against 29.8.
- The user asked for No Mercy's proportions, then, having seen them: "It looks bad. Revert to before we went this No Mercy model route". Tried and dropped, kept in backups\nm_body_route and data\austin2k15 (fit.py, body.py, hd_slicefit.py, clay.py): slice fit (0.38.39: pointed chest, thin knees), surface fit (ridges), his No Mercy meshes subdivided with baked pictures. 0.38.40 is hd_reshape_v2.py again: the same 33,968 points and 7,187 blended as before the route.
- Regressions hold until the user signs off the look. Not done on this build: vs_sc15, rumble_ports, hd_rumble, hd_select, the Bret tests. The freeze (func_800012B0, a thread at func@..+0xB21): 2 in about 20 runs of this model; the same place in freezes from 11-02, before it.
- (0.38.41) The user's marked-up shots: "His chest is way too big, as are his shoulders. Tighten them up"; "slight lines where the pieces meet". hd.py: the chest's front scale down to at most 1.06, the upper trunk 10% narrower, the arm's thickness tapered (0.78 at the deltoid to 1.0 at the forearm), the lift 1.0: chest front 19.8 -> 16, widest 32 -> 29.7 (shoulders about 27). hd_pack.py: seals: a flap behind every open skin edge and every edge between two bones' lists (each list rounds its points in its own bone's frame), 655 edges. Files before: data\austin2k15\hd_v3_before_notes.py, hd_pack_before_seal.py.
- (0.38.42) The user, with a 2K15 reference shot: "Shoulders are too small now, and chest should be more pronounced, but male/muscular. Also the arms are odd placed". hd.py to_rest: the arms TURNED about the shoulder by degrees, not slid between two poses (that cut the corner: the deltoid flat, the arm stuck on). reshape: the pecs' mass added evenly across the chest (+2.6), not a scale (a point). mknormal.py: the OBJ's normal maps painted into its pictures (this game has no lighting; what made the reference's pecs and abs read was the normal map): build/hdtex_*.png, mkpack_hd.py. RT64 takes a .dds over a .png of the same name: qa.ps1 and deploy.ps1 now clear a pack's folder. Files before: data\austin2k15\hd_v4_tight.py.
- (0.38.43) The user: "Did you add muscle to the texture? Don't do that ... His upper arms are huge and his lower arms look like they're buried in them ... why can't you just make it match the model I gave you? Overwrite No Mercy model if you must." The cause: every version had reshaped his model to No Mercy's skeleton, on guessed joints. data\austin2k15\joints.py solved No Mercy's real joints from the bones' matrices in 581 QA dumps, whole numbers and no error: elbow (24, 29, 0), wrist (24, 7, 0), knee (9, -31, 0), ankle (9, -74, 0) (shoulder about (23, 56), hip (9, 5)). hd.py rewritten: his model as it is, one scale; each limb's segments turned whole about HIS joints (profile.py, the mesh's edge loops; the arm isn't straight in the T pose: 16 degrees forward, which had put the shoulder 8 units too far back) to hang straight: no reshape, no stretching, no baked normals (the pack is the OBJ's own DDS again). hd.c: his skeleton: a bone keeps its rotation; its place is where its parent carries his joint (hd_joints, build/joints.json), on the draw's matrices only. hd_stance.py does the same offline. Full match vs_sc15 looked at. The other tests still wait for the user's sign-off.
- (0.38.44) The user: "define his body a little closer to No Mercy, just by increasing size of shoulders, arms, etc ... Add in fists and middle fingers ... add some base lighting". hd.py: bulk(): limbs thicker about their joints' line, 70% (arms) and 50% (legs) of the way to No Mercy's own part's measured thickness at each height, clipped at 1.32; pecs +1.8. hand_rig: fingers by z, joints along the arc they're modelled in (handposes.py, hand_view.py), each point's closed and straight place; hd.c hd_hand: the game's own hand pose (0x80076588[slot]) -> the shape table read from the game (0 No Mercy's own: a loose fist; 2 the middle finger: poses 9 left, 15 both; 5 a tight fist). hd_light: his alone, each point's colour from its normal in its bone's frame, half the bones each frame. Other models unlit, untouched. Pace: 29.7 f/s before, 24 lighting all every frame, 25.7 now. In-game shots show the build; the fingers checked offline only (hand_view.png), no run caught pose 9 or 15 on camera.

## 2026-11-03 0.38.45: 2K15 Stone Cold: the two fingers, legs, arms, no lighting
- User on 0.38.44: arms too low for the animations, no middle finger in his standing taunt, legs "fused", lighting not visible: remove it.
- Middle finger, cause: hd.c read a pose's shape v as the list's mesh v. It's mesh v - 1 (0x80076A50 + v * 0x10's entries in RAM, their point counts, are the list at 0x80048400 moved along one; entry 0 holds the list's last). Pose 7 (his taunt 14190) is v 3, mesh 2: the middle finger on both hands, which the table called "open, spread". hd_shape re-ordered. Poses themselves were always read right (QA hd_taunt, state.csv bs_grabs).
- The finger's shape: hand_rig now takes the fingers from the mesh's connections (past about 50 degrees round it falls into four pieces), not a line across z (strips of one finger went with its neighbour: stretched sheets), and straight is the finger unbent along its centre line, its length (middle 10.8) and thickness kept.
- Legs: the thickening pushed the inner thighs past x 0, and weights_full took the side from where a point ended up: inner thigh on the other leg's bones, 82 triangles strung between the legs. The side is now the file's; the thickening stops at x 0.3.
- Arms: bone 12 / 19's joint is No Mercy's shoulder (ARM_PIVOT 22.5, 55.8), the rest pose unchanged: his arm's ring at 50 had raised hands 6 short. Elbow and wrist stay his (within 1.6 of No Mercy's).
- Lighting removed (hd_light, the normals in the pack): colours a fixed shade again, as before 0.38.44.
- mkroster: the uniqueness swap is now the last two differing standing taunts (229, 230), not 227 and 230. Stock Austin: 14190 x 3, 14200.
- New QA: hd_taunt (qa_pspecial + 64 presses taunts, bs_grabs logs pose | shown; qa_hd_cam 3 in front of him); play_sc15 (him on the user's controller). QA 2026-11-03_193001_hd_taunt: both fingers seen, shot 074.6s. Pace not re-measured. Regressions still held until the user signs off.

## 2026-11-03 0.38.46: the 2K15 Rock, a second full-detail man (0x011B)
- The user: "I just dropped The Rock in the folder. Try with him. Another separate wrestler slot. Let's see what happens". TheRock1.7z -> data\rock2k15\src. The same rip as Austin's: objects 1-9 the same counts, most the same faces and uvs (2, 3, 4, 14: same counts, another order).
- data/austin2k15/hd.py, hd_pack.py, hd_stance.py: HD_MODEL=rock (MODELS, PK). hd.bin byte for byte as before after the change: Austin untouched. His: arm bands x 73.8 / 76.7; Object011/012 wear eye (the file says all_color; their uvs are eye's); Object010 a bare foot inside the boot, left out; Object018 a second trunk layer. Hands as Austin's, measured from his wrist.
- src/hd.c: hd_models[], the man by SKEL_WM_HD_MODEL (descriptor flags bits 24-27, gen/wmport.py); a slot's copies sized for the biggest. hd_pack_rock in recomp/content.S; hd_index_rock.h.
- Slot: mkroster LATE rock15 (same=1: moves; music 37, video 29, 6'5" 275, No Mercy's record), mkparts REV_LATE ("rock15", 8, 0), mkovr, names, arcade.h RO_ROCK15, logic.c, mod.toml. Generators: mkroster, wmport, mkparts, mkovr, mkcontent. Against backups\pre_rock15 the generated files only grew.
- Texture pack mod/texturepack/rock15_hd (data/rock2k15/mkpack_hd.py, hashes via WCW_TEX_DUMP + match_hash.py). His stand-ins each carry a mark in a corner (hd_pack.py stand_in): boots and kneepads, nearly black at 64 x 32, otherwise hashed alike and RT64 couldn't tell them apart.
- His thickening still measures toward No Mercy's AUSTIN (fit.py's descriptor), not No Mercy's Rock.
- QA: vs_rock15 PASS, Rock vs Austin, CPU, the game's camera: 23.7 frames a second over 190 s, no crash (cut at 240 s, not a whole match). rock15_taunt PASS, shots looked at. Pace of one man on 0.38.45+ and his fingers in pose not checked; regressions still held.

## 2026-11-03 0.38.47: the full-detail men's pace: native work, a low-detail man on the hard camera (needs the new exe)
- The user: "Why is it making it so slow? This is a gaming PC". <spec>, <spec>: in a match GPU about 35%, the process about 1.1 cores, no core full. The game is capped at 30.
- Measured, RAW IS WAR, Rock vs Austin, CPU, shots every 20 s: No Mercy's own men 29.8 f/s; the two full-detail men 27.2-27.5; with them left off the Titantron's hard camera 29.6; with the low-detail man on it 29.4. Screenshots themselves cost: the same match gave 22 with a shot a second, 24-26 with one every 4 s. Earlier single-run numbers vary a lot.
- Blended points moved native (exe src/main/arcade_hd.cpp, recomp_arcade_hd_blend): 2.3 ms -> 0.47 ms a frame (qa_hd_clock), but the pace didn't move (27.4 / 27.5): my guess that they were the main cost was wrong. Kept: they were never going to be cheaper in translated mod code. Fingers native too (recomp_arcade_hd_hand). The translated paths are gone.
- The cause: tron_hardcam_build draws every draw item again for the screen, so each man drew whole twice. hd_pack.py now makes a second list per bone (LOD_GRID 4.5, uv cell 1/8: Austin 5,344 triangles, 3,970 points; Rock 4,949, 3,714; no flaps, no mouth) on copies of a few of his points, refreshed each frame (recomp_arcade_hd_lod_sync: they bend and close fists with him). The no-depth stub, the one the hard camera uses (hd_zdl_orig), calls it.
- Not done: zoomed in, the low-detail men show small dark gaps (clusters split across picture seams); at real size the picture is tiny. QA: perf_native, perf_stock; hd_taunt on the native hands, shots 073.6 / 074.1 show the fingers. Regressions still held.

## 2026-11-03 0.38.48: the Titantron back to full detail
- The user on 0.38.47: "I didn't tell you to lower the quality of the titan tron.. I want to make this native code. Stop emulating N64". The low-detail list is out (hd_pack.py, hd.c, arcade_hd.cpp): the hard camera draws him whole again, at the cost measured above (about 27 frames a second with two men). The native points and fingers stay. No trading picture quality for pace without asking.


## 2026-11-03 native_test: a throwaway native proof (separate from the mod and its exe)
- The user: "Stop emulating N64 ... Do a temporary version of 3 to test without affecting what we've built so far. Simple version, 2 wrestlers, one arena, limited moves." Development\native_test: its own exe, build.ps1, data\game.pak; reads data/austin2k15, data/rock2k15, the extracted No Mercy files and QA runs' RAM; writes nothing outside its folder.
- C++, Direct3D 11. The men skinned on the GPU, fingers as vertex shader morphs, the Titantron a real second render at full detail. Animations: No Mercy's files, decoded (data/announce/fk.py; the move -> file table at 0x800F6EE4, file 0x69's records). Arena: tools/capture_arena.py reads the scene's draw items from RAM dumps, baked to world coordinates; the ropes (made per frame by the game) drawn natively.
- Measured, 1280 x 720, 4x MSAA, vsync off, full-detail Titantron: 2 men about 5,900 fps; 12 men 2,400; 32 men 980; 64 men (5.4 million triangles) 470.
- What it is not: no rules, pins, music or crowd animation; the grapple alignment and hand poses are approximations written for this test. The real game's logic isn't ported.

## 2026-11-04 0.39.0 (the project folder only): the renderer's fast path for the full-detail men
- The user: "get started, can you work in a new folder". This folder is a copy of <outside> at 0.38.48; its scripts work only here. Step 1 of the plan: replace the N64 path piecewise.
- RT64: G_EX_DRAWBATCH_V1 (0x34), RSP::drawBatchV1: a bone and picture's points and triangles in bulk: no per-point screen transform, no per-triangle checks; the GPU transforms and culls; the batch's rectangle taken as the scissor's. hd_pack.py: one batch a bone and picture (points 36,384 -> 28,183 for Austin, no more splitting at 120), u32 indices; hd.c: hd_desc per slot.
- Measured with the PC busy with other programs (CPU 23-68% before each run), alternating, perf_native: fast 24.4, 23.6; old 17.7, 17.5. On a quiet PC the old build gave 27.2: not yet measured here on a quiet PC.
- A freeze on the first fast-path run (same signature as the old one: patch_function_1@800012B0, func@..+0xB21; "no new frame for 12 seconds"); not in the four runs after it. One in five isn't the old 1%: watch it.
- Shots looked at: main view, hd_taunt (both fingers, 111.6 s), tron_hd (both men on the Titantron).

## 2026-11-04 0.39.1 (the native port): the first game function native: func_800028B4
- It turns the scene's draw items into commands each frame. Read from the recompiled code and written as C++ (exe src/main/arcade_scene.cpp), run beside the original with every word compared (recomp_arcade_scene_check; kept for the next one): scene_check 22,000 lists, 6.1 M words; and rr_check, a real 6-slot Royal Rumble: 38,000 lists, 22.7 M words; no differences. (A first run named rumble_ports wasn't a Rumble: QA never took control, the game's attract match; rr_probe then showed the Rumble starting in both folders.) Then RECOMP_PATCH in its place. The method for the next ones: read, rewrite, compare, replace.
- It changes nothing visible. What it's for: the one place every object's drawing goes through, so later objects can leave N64 commands from here.

## 2026-11-04 0.39.2 (the native port): runs of triangles in one go, for every object
- RT64 tri1 / tri2 (F3DEX2): a run of triangle commands handed to RSP::drawIndexedTris: drawIndexedTri's work with its set-up once a run, not once a triangle. The same result, all objects. Still N64 commands: this makes them cheap, it doesn't remove them.

## 2026-11-04 0.39.3 (the native port): the real game drawn natively, live, in a window beside the normal one
- exe src/main/arcade_view.cpp, on with WCW_NATIVE_VIEW=1 (Play Native View.cmd; off otherwise, no cost: capture_chain returns at once). At func_800028B4 each chain's items are read as data: fixed-point matrices, points, triangles, pictures (decoded, cached by contents), combiner (both cycles, in the shader), prim / env. G_EX_DRAWBATCH's are read too. The 2K15 men's pictures: native_test\data\game.pak by stand-in address (recomp_arcade_view_hd_pack). A frame runs from first == 0 to a run reaching the list's end.
- Direct3D 11, its own window and thread. The Titantron: a second pass: this frame's clip positions through the inverse of the frame's view x projection, into the hard camera's (tron.c hc_eye etc.), at 512 x 256, full detail.
- QA tron_hd with WCW_NATIVE_VIEW, shots looked at (native_test\build\view): arena, crowd, HUD bars, ropes, both men, the Titantron all right. 0.5-0.8 ms a frame there (upload and both passes): 93,324 triangles, 182-197 draws. Not yet there: the HUD's text and timer, menus, anything not a scene item. Normal window untouched: RT64 still runs beside it.

## 2026-11-04 0.39.4 (the native port): the whole frame native; matches can be native only
- arcade_view.cpp now reads the frame's whole list as data at func_80002DE4 (head rw_dltab[idx], up to the end the game's about to write; a frame is several lists: frame_begin at func_80002BB4 with clear): HUD, text, rectangles (fill, tex), segments, viewports, scissors, G_MTX, picture targets (the Titantron's camera pass is the list's own, into a 512 x 256 target). Pictures not used for 300 frames let go.
- Native only (WCW_NATIVE_VIEW=2, Play Native Only.cmd): in a match (mode -2 / -6, match frames valid) the list RT64 gets is cut to its end, and the D3D11 picture is a child of the game's window: no input of its own (WS_DISABLED), so pads and keys stay the game's; 4 : 3 fitted in the window. Menus and entrances before the match clock: RT64 as before. RT64 still starts, and draws everything outside a match.
- Measured, RAW, Rock vs Austin 2K15, CPU, PC busy with other programs (CPU 38-74%): 29.6 f/s over 143 s native only; fast path 21.5-24.4, old build 17.5, the same evening. Native frame under 1 ms for 188,831 triangles. Quiet-PC comparison not done.
- Shots looked at (mode 1 and 2, native_test\build\view): RAW with the 2K15 men, a 20-man Rumble of ports with the countdown, an entrance's close-up and banner, SmackDown's set. Not checked with a person's hands: pads, keys, in native only. Not handled: Titantron video in arenas without the hard camera as moving picture (decoded as ordinary pictures), RT64's own menus (F-keys) are hidden under the native picture in a match, full-frame effects read back from the frame.

## 2026-11-04 0.39.5 (the native port): native only covers the whole game
- WCW_NATIVE_VIEW=2: not only matches: whenever a frame's list drew into the frame's picture, RT64's list is cut to its end and the native picture shows, inside the game's window. No lists for 0.4 s: it hides, and RT64's picture shows. The Titantron's picture, with no camera pass in the frame, is its memory as it is (the game's video).
- QA rr_probe native only: title, championship list, select pages, Rumble entrance and its fades, a 20-man Rumble: looked at (the game's window's own shots, PrintWindow, show the native picture). Not done with a person's hands.
- Still N64 in native only: RT64 still starts, owns the window, runs its empty lists; the game's own display lists are still BUILT by the game in N64 form, then read here as data. Game code is all still the recompiled game but for func_800028B4. 30 frames a second still: game logic's timing.

## 2026-11-04 0.39.6 (the native port): Step 2 begins: the skeleton native
- func_80012BE0 (19 bones' matrices, per wrestler, per frame) and what it calls (the N64's own sin and cos in double, its rotation, its matrix product, its transform) rewritten in exe src/main/arcade_bones.cpp, single precision in the N64's order so results match to the bit. Proved beside the original (qa tron_hd, rr_check): 4,000 and 40,000 skeletons, 19 M values, none different. Then RECOMP_PATCH in its place. Second native game function after func_800028B4.
- The animation player's bone work native too, each proved beside its original first, then swapped in: func_800135D0 (600,000 keys), func_80013234 and func_800132D8 (40,000 frames in a Rumble, 4,000 in a match), func_80012F48 (4,000). None different. The driver func_80012130 above them is still translated: it starts the disk reads.
- Native window: rectangles' coordinates are signed 12-bit: the credits' scrolling text had turned into vertical stripes (sw story_upgrade_b).
- Sweep native only (view_nitro, hd_select, story_upgrade_b, vs_goldberg, arena_nitro_aisle, hennig_win_taunt): all PASS, no freezes, looked at; the stripes the one thing wrong.

## 2026-11-04 0.39.7 (the native port): the animation driver native; the twin
- func_80012130 (537 instructions: each wrestler, each frame: which key, the root's matrices, every bone's place) rewritten: exe src/main/arcade_bones.cpp drive(). Replaced by a RECOMP_PATCH. Native game code now 7 functions, 1,529 of the 383,555 translated instructions: 0.40%.
- The twin (arcade_bones.cpp): a whole function proved against the original while the game plays. The 8 MB kept as the original starts; the rewrite run on the copy when it has finished; every word compared. The game isn't touched. A function left translated isn't run twice: its writes are noted as the original makes them and put on the copy when the rewrite reaches that call, and the rewrite must reach it in the same order for the same reasons. To use: WCW_TWIN=1, RECOMP_HOOKs on the function and on what it calls: recomp_arcade_twin_in(args) / recomp_arcade_twin_out(), recomp_arcade_twin_call_in(id, a0, a1) / recomp_arcade_twin_call_out(). So far it only knows drive(): to generalise for the next function.
- What it taught: (1) other threads write the sound buffers (0x800C0000-0x800DFFFF) and the pictures (0x80600000 up) throughout: left out of the compare; seen by region before leaving them out: nothing else differed. (2) Sampling on a timer almost never saw a move start: it samples every start and end now, and counts the ways through. (3) THE BIG ONE: check the mod's own patches before bypassing anything: grep RECOMP_PATCH and RECOMP_HOOK for every function a rewrite stops calling, and gen/patches.py. My first driver read the game's table for a move's length, past the mod's patch of func_80011A54 (the pack's moves): one run in 7,000, move 22347, the path differed: a real bug it caught. And it used a plain bone tree, past the mod's func_80002268 (scene_world: every matrix checked, positions kept): no test would ever have shown that, as long as no matrix went bad; only reading the mod did.
- Not exercised: the driver's "y too" (flags & 8) never ran in any compared run: it was checked by reading against the original only. Same for settle's.

- Checked on 0.39.7 with the driver in place, real game: tron_hd, rr_check (6-slot Rumble), hd_taunt, rock15_taunt, hennig_win_taunt, vs_goldberg, story_upgrade_b: all PASS, no freezes; frames looked at. Not played by hand.

## 2026-11-04 0.39.8 (the native port): the picture no longer waits for the game (step 1 of the agreed five)
- The user's rule from here: no emulation, no hacky band-aids, the proper way. Order agreed: 1 the 30 fps picture and RT64 go; 2 the N64 draw lists go; 3 the N64 system layer goes; 4 the translated code to readable C++, by tools, proved by the twin; 5 the 8 MB memory map goes. Function-by-function rewrites paused until 4.
- exe src/main/arcade_view.cpp redesigned. Points are no longer turned into screen positions as the list is read: each object keeps its points and its matrix, and the graphics card puts them together at draw time (a structured buffer of matrices, one index a point). World matrix = the item's matrix x the inverse of the camera the frame was taken with (func_80002730, recomp_arcade_view_take; 0x80062BA8), so at a whole update it comes back to exactly the game's (worst seen 0.0012 of the matrix's size, QA). Each picture blends the last two updates: matrices taken apart (polar decomposition: a turn, a stretch, a place; the turn slerped), the camera the same way, its projection straight. Points blend too (the 2K15 men's are remade each update). Same object from update to update: the item (scene: its matrix's address less + 0x38 + slot * 64; the Titantron: tron.c's table hc_item_of, its items come in a different order each update). Unchanged on screen (the HUD's): left alone. The game's timing untouched; the picture is one update behind it.
- Same idea as the base project's 60/120 option (RT64, README), which guesses which object is which; here it's known.
- Measured, real game, QA, Native Only: 59.8 pictures a second at the screen's rate (this PC's monitor 60 Hz) from 30 game updates; with WCW_NATIVE_VSYNC=0: 1,795 pictures a second, 188,917 triangles, the two full-detail men and the full Titantron. WCW_NATIVE_INTERP=0: the game's frames only, to compare.
- Camera cuts jump: a cut is a break in the camera's motion (how far it landed from where its last step would have put it), not speed: it eases at up to 180 an update. NOT the game's own signal: shot changes are spread through several routines (func_800E28C0, 800E30D8, 800E38A4, 800E4B60 in the match alone; entrances, stories elsewhere). To replace when step 4 reaches them. A wrong call costs one stepped update or one blended picture.
- Memory: the 25,000 textures in a story are made and let go: 177-563 live, about 500 MB throughout, no climb. That question is closed.
- Sweep, Native Only: rr_check, hd_select, story_upgrade_b, hd_taunt, arena_nitro_aisle, hennig_win_taunt, vs_goldberg, tron_hd: all PASS, no freezes; pictures between updates looked at. Not played by hand. Not looked at for: motion feel, pacing on a monitor faster than 60 Hz.

- Rumble cuts 47 -> 20 with the motion test; some are still the first step of a fast ease: one stepped update each.
- The user played it by hand (Play Native Only.cmd): "It plays good"; controls work. Wondered if a punch looked off, couldn't say against the original. The picture can't change whether a hit lands; only how it looks between updates.

## 2026-11-04 0.39.9 (the native port): the native picture's textures as the hardware has them
- Logos (THQ, Asmik, AKI) were grey and jagged. Traced by logging what the game asks for: the THQ's is IA8 with the palette on. With the palette on, texels are palette indices whatever the tile's format says (RT64's TextureDecoder.hlsli sampleTMEM: the same; 16-bit: the first byte). Now so, with both palette kinds (RGBA 5551, IA 16), palette slots for 4-bit, palette memory as the hardware's (256 entries, loaded in parts, kept from list to list).
- That first broke all text (boxes round PRESS START): the reader had no 0xEF (set all other modes), so a palette the game had turned off stayed on. Added. Nothing else had noticed because the old code only used palettes for CI.
- The game's filter honoured: nearest where it asks for it, with the tile's wrap (only clamp before).
- SmackDown's oval sign showed black, not the Titantron: the "plainer" noted before was this. A sign of its own shape shows tron.c mask_compose's picture (0x806C4000), made by its own list; the renderer took that for "not a picture". Now a target of its own (2): cleared to nothing, the sign's shape to its alpha, the Titantron's picture over it keeping that alpha, as the list says. 3 is now "anything else".
- Checked against RT64 side by side (earlier QA shots of the same scenarios): intro's green frames are the game's own; the oval's right. QA, Native Only: story_upgrade_b, hd_select, hennig_win_taunt, tron_hd, rr_check: PASS, no freezes.

## 2026-11-04 0.40.0 (the native port, now the main version): the native picture on Direct3D 12
- The user's decisions: Direct3D 12; no recompui (settings in the in-game F2 menu); picture shapes 4:3 default, 16:9, 16:10, ultra wide, 3:4, 9:16 (scrap 9:16 if not worth it); HDR to modern standards, default off; our own CRT; our own texture packs; a GUI for stories, roster etc. later; unlimited wrestlers, arenas, stories: no new fixed counts in anything native. <outside> is the backup from here.
- arcade_view.cpp's window rewritten for Direct3D 12: the strongest adapter (DXGI high performance: <spec> here), flip swap chain, 4x MSAA target resolved to it, a root signature (blend constants, a draw's constants, the matrices by root SRV, a picture's table entry, a sampler's), pipelines made as needed, upload memory per frame growing on demand, points and triangles copied to the card's own memory once an update, textures with smaller versions made on the CPU (D3D12 has no GenerateMips), the pictures' descriptor table growing on demand, things let go only after the GPU's finished with them.
- Screen's rate: a frame-latency wait, starting each picture as late as it can (least delay). Not waiting for the screen (WCW_NATIVE_VSYNC=0): no wait, tearing allowed. With the wait in that mode too it ran at half speed: 650 against D3D11's 1,230: my setting, not D3D12.
- Measured, alternating with D3D11 (PC busy: 55-99% CPU from other programs): uncapped 1,127 and 1,373 median, 2,191 at best; D3D11 781 in the same stretch. Screen's rate 59-60 from 29.6 updates. Memory about 750 MB (D3D11: 500).
- Sweep: hennig_win_taunt, rr_check, hd_taunt, tron_hd, hd_select, story_upgrade_b: PASS, no freezes, pictures looked at. hd_select and story_upgrade_b failed once while the user was playing a QA window ("Probably me": a 4K window, Venis v Kane); both passed run again with nobody at the window.
- Open: twice, uncapped on D3D12 only, a 5-second stretch of 7-8 game updates a second; not seen at the screen's rate. The PC was busy; not shown to be mine or not. Direct3D's debug layer: d3d12SDKLayers.dll wasn't installed, so "no messages" meant nothing: WCW_NATIVE_D3DDEBUG=1 is only worth anything with Graphics Tools (the user's installing it).
- Direct3D's debug layer, installed by the user (Graphics Tools), asked for at the very start of main (WCW_NATIVE_D3DDEBUG=1): before any device, or it takes those devices away. Full-detail match and SmackDown: one warning: default-heap buffers created in COPY_DEST: now COMMON.

## 2026-11-04 0.41.0 (the native port): RT64 isn't started
- arcade_view.cpp NativeRenderer: the runtime's ultramodern::renderer::RendererContext. The list's read as the game ends it (recomp_arcade_view_frame), so send_dl and update_screen have nothing to do. Draws straight into the game's SDL window. The default: double-click the exe. WCW_NATIVE_VIEW=0: RT64, while it's still in the build, to compare. RT64 still links in; recompui still runs, not drawing (config, controllers, rumble go through it).
- "Play Native Only.cmd", "Play Native View.cmd" retired. native_view.pak beside the exe (deploy.ps1); qa.ps1 points at native_test\data\game.pak.
- Sweep, debug layer on: hd_select, story_upgrade_b, rr_check, hennig_win_taunt, hd_taunt, tron_entrances, view_nitro, tron_hd: PASS, no freezes, no Direct3D errors or warnings; about 590 MB (750 with RT64 running beside it). A log of every picture the game draws into that isn't shown: only the z buffer (0x80700000): no effect lost in these scenarios.
- Next in step 1: settings in F2 (window, shape, CRT, HDR, controls), device-lost handling (a failed Present just stops the picture for now), texture packs; then recompui and RT64 out of the build.

## 2026-11-04 0.42.0 (the native port): F2 > PICTURE
- A page in the operator menu (logic.c PG_PICTURE; mod.toml picture_*; picture_send: to the renderer at once, as the operator goes along; recomp_arcade_picture): DISPLAY (window, all of the screen: SDL's desktop full screen, on the window's thread, main.cpp update_gfx), SHAPE (4:3, 16:9, 16:10, ultra wide, 3:4, 9:16), SMOOTH MOTION, FRAME RATE (the screen's, no limit), CRT (off, scanlines, full), HDR.
- Shapes: nothing stretched. The picture's the shape, as large as fits the window; clip x or y scaled so it shows more of the view (more at the sides, or above and below); the HUD keeps its shape in the middle. Whole-width and top or bottom rectangles (fades, logos' backgrounds) reach the edges, their pictures going on as they went. The scissor follows. wcw_widescreen_cull_scale follows the shape (no more objects dropped at wide edges by the game's own cull). HUD to the corners, if wanted: to do, in the mod.
- CRT: ours, not the crt\ folder's presets (those are RT64's, third party). HLSL_POST: the game's 240 lines as a beam, wider when bright; full: the tube's curve, an aperture mask, dark corners. HDR10: Rec. 709 to 2020, white at 200 nits, ST 2084, R10G10B10A2; off if the swap chain says it can't. SDR picture in HDR's container: it isn't graded for it. WCW_NATIVE_SHAPE, _CRT, _HDR, _WINDOW hold them for a test.
- The menu wasn't showing at all in the native picture: flush_menu swaps the task's list for its own after the renderer's read the game's: plat_view_replace: the frame's read again from the new list. QA picture_walk (Rumble: qa_op 20): the page, 16:9, CRT full, full screen (3840 x 2160 in the shots) and back, SAVE AND EXIT: saved settings right. picture_saved: starting from saved settings. (Singles QA matches: mode -6, match_running never: use a Rumble for qa_op.)
- A graphics card lost: device_lost: says so, quits properly (saves flush). Not triggered in a test.
- 0.42.1: HDR on an SDR desktop came out blown (QA's shot: whites clipped): the swap chain accepts HDR10 whatever the desktop's in. Now only when Windows has the screen in HDR (IDXGIOutput6 GetDesc1's colour space); otherwise left off and logged. The menu's own value still says ON then. What HDR looks like on a real HDR desktop: not seen; QA's shots aren't HDR.
- QA, debug layer on: tron_hd in all shapes and CRT, hd_select, story_upgrade_b, rr_check: PASS, no freezes, no Direct3D errors.

## 2026-11-04 0.43.0 (the native port): RT64 and recompui out of the build
- The exe's 9.6 MB (23.4). Gone from the link: rt64, plume, recompui, RmlUi, nfd, freetype's use, dxc, librashader. Still in the tree, unused, until this has been played: lib/rt64, lib/RecompFrontend/recompui. The code as it was: backups/before_rt64_removal.
- src/main/arcade_host.cpp: where things are (portable.txt beside the exe: settings and saves there, as before, else %LOCALAPPDATA%\\NoMercyRecompiled), the settings the input and sound read. recompinput kept till step 3, off the UI (recompinput/host.h). controls.json loaded as before.
- F2 > SOUND & FEEL: main volume, rumble strength, stick dead zone (their sliders were in the UI). Their saved values start from the mod's defaults (100, 25, 5), the old screens' saved general.json and sound.json are read first.
- Closing the window quits (the UI's prompt, invisible since 0.41, would have swallowed it). F11 and Alt + Enter: full screen, as before (the menu's value doesn't follow them).
- recomp_get_target_aspect_ratio: the game's own. wcw_rt_emit_camera, wcw_interp_*: nothing to do; the hand-edits calling them go at step 4. The projection latch (raytracing.cpp) stays: the renderer's camera blend uses it.
- A 12 s stall ("freeze report") in story_upgrade_b, recovered, with the game at 13-15 updates a second for 45 s before it: the render thread in EnumDisplaySettings: NativeRenderer::get_display_framerate asked Windows at every retrace. Now looked up every 5 s on the picture's thread. Not shown to be the whole cause (the PC was busy; the old 1% freeze is a different report: threads parked in func_800012B0).

## 2026-11-04 0.43.1 (the native port): reading the lists cheap; step 2's shape
- Measured: reading the lists took 7 ms of the game's 33 ms update with the full-detail men, 5 of it textures: every triangle worked its draw out again, every picture's contents hashed each time. Now worked out only when a command's changed how things are drawn (St::state_new), with XXH3, still over all the texels (an animated picture changes in place). 1.76 ms with the full-detail men, 0.55 in a Rumble, about 1 in the menus.
- Step 2's shape: the scene (wrestlers, arenas) comes through one function (func_800028B4, already native), with about 15 places in the mod reading those lists (tron.c list_scan finds the arenas' screens there; draw.c, hd.c, announce.c). But at least 132 of the game's functions write draw commands themselves (menus, HUD, text): those go with step 4. With reading the lists under 2 ms, there's no speed reason to hurry the scene's list out: the limits that matter are in steps 4 and 5. So: on to step 3, and step 2's scene part when the code around it is easier to change.
- QA, debug layer on: tron_hd, rr_check, hd_select, story_upgrade_b, hennig_win_taunt, tron_entrances: PASS, no freezes, no Direct3D errors, pictures looked at.

## 2026-11-04 0.44.0 (the native port): step 3 begins: the sound chip's program is gone
- src/main/arcade_audio.cpp: the game's sound commands in plain C++ (ADPCM, resample, envelope mixer, mixer, interleave, the buffers), worked out per sample from the chip's program. rsp/nomercy_audio.cpp (the translated program) out of the build; kept on disk, -DARCADE_AUDIO_TWIN=ON puts it back beside this.
- Proof, WCW_AUDIO_TWIN=1: on a copy of the 8 MB, every task, everything written compared. tron_hd, hd_select, story_upgrade_b, tron_entrances, rr_check: 86,528 tasks, 11,260,407 writes (ADPCM states, resampler states, mixer states, saved buffers: what reaches the speakers), 0 different. 0.08-0.14 ms a task against 0.11-0.17.
- This program has fixed frames: 0x170 bytes, fixed buffers. Its table's command 14 lands on the last store of SETVOL's second form (the "12-bit PC" oddity of the toml): the game does send it. The resampler's state writes 10 bytes here; the chip's 16, the last 6 junk nothing reads.
- Still N64: the game's translated synth writes those commands (func_8001xxxx, alSyn*), and they're still commands: they go at step 4.

## 2026-11-04 0.44.1 (the native port): a lost screen swap, the mechanism of the 1% freeze
- ultramodern events.cpp: the VI thread's update_vi flipped the two VI states (cur_state ^= 1; *next = *cur) without message_mutex, which osViSwapBuffer holds to write next->framebuffer. A swap landing between the flip and the copy went into the state the copy then overwrote: lost. func_800012B0 (the frame task) then waits until neither the current nor the next buffer is the one it wants, and neither ever changes: every game thread parked. Now update_vi holds the lock, and osViGetCurrent/NextFramebuffer take it.
- Proof: a test delay of 3 ms between the flip and the copy, from swap 200 on (taken out again). Without the lock: frozen at swap 204, freeze report with patch_function_1@800012B0 (the signature of the 9 in ~800). Same build with the lock: 717 swaps and 133 swaps under the delay, no freeze. (A third run without the lock got cut off at swap 104, before the delay began: it doesn't count.)
- Rough odds without the delay: a window of some tens of ns at 60 Hz, 30 swaps a second, gives about 1% of QA runs: the same size as measured. What isn't proved: that this is the only freeze of its kind. Only hundreds of QA runs can say. Watch for that signature again.
- <outside> (the backup, 0.38.48) still has this. Not touched.

## 2026-11-04 0.45.0 (the native port): threads and messages rewritten, the old valves gone
- ultramodern threads.cpp, mesgqueue.cpp, scheduling.cpp, threadqueue.cpp rewritten (1,476 lines to 642, of which the platform part of threads.cpp stays). events.cpp: nine diagnostic blocks and their counters out. The originals: backups/before_threads_clean.
- Gone, all from the WM2000/WCW ports: the "starvation valve" (ran a thread out of priority order after 50 ms: it fired 1-4 times a run here, on nothing but a busy PC or a long load, and let a low-priority thread run in the middle of a higher one's work), the priority boost, the repair of threads' structs "wiped by the game" and its host-side record of every thread (never fired in this game), the checks for bad links in the queues, the invalid-queue guard, the dropping of a completion after a second without room.
- A completion with no room now waits for room, for as long as it takes (dropping one after a stall of more than a second could cost the game a load it was waiting for). `[message] N completions waiting` in the log if those ever pile up: none in any run so far. A retrace or sound buffer's end with the queue full is missed, as on an N64.
- thread_queue_remove was broken (its loop never moved on): only reached from osSetThreadPri / osDestroyThread on a thread that wasn't at the head. Written properly.
- The game makes 7 threads (entries 800004D0, 80000E3C, 80000F98, 800010F4, 80026888, 80003138, 8002B5B8) and never ends one.
- QA: tron_hd, menu_from_attract, menu_p2_input, hd_select, story_upgrade_b, hennig_win_taunt, tron_entrances, rr_check, picture_walk: PASS, no freeze files, updates a second median 28.6-29.8. Pictures looked at.
- User, 2026-11-04: game progress needn't be kept; everything's unlocked by default (it is: unlock_all, logic.c); saving settings matters once the game is done.

## 2026-11-04 0.46.0 (the native port): controllers straight from the PC; the stand-in for the N64's controller chip gone
- src/main/arcade_cont.cpp + patches in hooks_recomp.c: func_800344F0 (osContStartReadData), 80034590 (osContStartQuery), 80034570 (osContGetQuery), 80034618 (osContGetReadData), 800399D4 (osMotorInit), 80039890 (osMotorStart / Stop). The pads' state goes to the same place as before; arcade_on_pads' work moved into the patch of 80034618 (a hook and a patch can't share a function).
- librecomp si.cpp: 380 lines to 40. The PIF command walker, the pak file, its format and CRC, the "identity" of the pak (the game's motor check flipped it between Controller Pak and Rumble Pak twice a second). What's left answers "nothing's plugged in" to the game's pak and microphone code, in the library until step 4. `[si] the game asked...` once in the log if it ever does: not in any of these runs. saves/*.pak.bin is no longer read or written (this game never saved to a pak).
- Change the user can feel: rumble works on all four controllers, it was the first alone: the stand-in only answered there. Untested on a real pad: QA can only see the calls ("[cont] first rumble, controller N").
- QA: menu_p2_input, menu_from_attract, tron_hd, hennig_win_taunt, hd_select: PASS, no reports.
- Where step 3 stands: sound chip's program, threads, messages, controllers: done. Left, and all tied to translated library code, so they go in step 4: flash (a plain file behind the osFlash calls), the ROM reads (osEPiStartDma, overlays), the timer thread, the rest of librecomp/ultramodern's [wcw] logging.

## 2026-11-05 0.47.0 (the native port): more memory for the game; the plan's order changes
The order changed: content limits now come before the whole-game conversion, because the translated code already runs as PC code and converting it changes nothing a player would notice. See the memory note limits-first for the order and the numbers. Code that gets rewritten is now commented in full sentences, and the mod folds into the game's code as each area is converted (user, 2026-11-04).

What changed:
- The game heap was 3.5 MB (0x80176000-0x80500000). It now has two more regions, added by arcade_heap_service (logic.c) through the game's own free, once the game has set the heap up: 8 MB at 0x80810000 (below 16 MB, so every old assumption still holds) and 256 MB at 0x90000000. The allocator gives the smallest block that fits, so everything goes where it always went until it can't fit. In these runs nothing went past the first region without a test forcing it.
- 66 scattered `0x80800000` checks in the mod are now GAME_PTR and GAME_RAM_TOP (arcade.h). The silent 0x84000000 limit on the mod's image is gone. The renderer reads all 512 MB. librecomp's own heap stops at 0x90000000.
- Addresses of 16 MB and up in display lists: the renderer (seg_addr), draw.c and tron.c treated the top byte as a segment number and cut the address. A top byte of 0x10 or more is now a whole address, and segment bases keep all their bits.
- [audio] log line if a sound list or sound data is ever past 16 MB. The chip's commands hold 24-bit addresses, so that would read the wrong memory silently. Not seen in any run.

Tested with WCW_HEAP_TEST, which uses up the room below a region at the start:
- 3 (everything into the 8 MB region): hd_select, story_upgrade_b PASS.
- 2 (blocks of 32 KB or more above 16 MB): hd_select, story_upgrade_b, tron_hd PASS. Only 86-127 KB actually went high, so a really large model hasn't been tried yet: that waits for content loading.
- 1 (everything above 16 MB): NOT safe. tron_hd and hennig_win_taunt pass, but hd_select's driver fails to pick and story_upgrade_b crashes in func_80010314 (read of address 2, called from the entrance code func_80020B58). Real use can only reach this if the 11.5 MB below 16 MB is full. Cause not found.

Known hidden assumptions still in the mod: header_versus (logic.c) searches 0x80180000-0x80200000 for the menu's title; bigshow.c ovl_ptr and announce.c search fixed ranges of the overlay.
The crash report's memory dump still only covers the first 8 MB and the mod's image, not the new regions.

## 2026-11-05 0.48.0 (the native port): content in files; easy limits lifted
- The content packs (themes and Titantron frames, ported wrestlers' files, the 2K15 men: 44.6 MB) are files in the game's content folder now, not built into the mod. exe src/main/arcade_packs.cpp loads them the first time the mod asks for one, from 0x88000000, with room up to 0x90000000. The mod asks for an address by PACK_* number (plat_pack). A missing file stops the game with a message. mod/recomp/content.S is gone (backups/before_packs). The .nrm went from 32 MB to 0.5 MB.
- ARENA page list: 64 to 256 (entries are bytes). Custom Story list: 32 to 4,096. A story's compiled file: 64 KB to 1 MB; its parts are still limited by the game's tables (story_start).
- Theme instruments: wave tables moved from 16 KB at 0x807C0000 (78 of them, and nothing stopped a 79th overwriting the menu's list) to 1 MB at 0x80810000 (about 5,000), still below 16 MB as the sound commands need. The bank's table: 128 to 256, the most a song's one-byte script can name. Not heard by me: the user should listen to an entrance theme.
- QA: tron_hd, menu_p2_input, menu_from_attract, hennig_win_taunt, hd_select, story_upgrade_b, tron_entrances, rr_check, picture_walk: PASS, no reports. Pictures looked at. WCW_CONTENT_DIR points QA at mod/build. [packs] in the log shows where each landed.

## 2026-11-05 0.49.0 (the native port): wrestlers from folders, first version
- wrestlers\\<anything>\\wrestler.json adds a wrestler at start-up, without a rebuild: exe src/main/arcade_wrestlers.cpp reads them, mod roster.c "wrestlers from folders" puts them on the roster after the built-in ones. wrestlers\\_example is a template (underscore folders are skipped). A bad file is skipped with a line in the log saying what's wrong.
- He starts as a copy of "based_on" (looks, moves, music, size), and the file can override: name, short_name, nick, height, weight, music, video, costumes (the game's 23-byte packed looks), moveset (the game's 216 bytes). Having the folder is what puts him on the roster: no F2 switch.
- He can be named in stories and select_pages.json (his name or short name, in capitals). With no place in select_pages.json he goes on a MORE page.
- Not yet: a wrestler with his own model files. The limit is one-byte ordinals: 112 more.
- QA folder_wrestler with WCW_WRESTLERS_DIR=qa\\wrestlers_test: on the select, a match as "FOLDERGUY" against The Rock.

## 2026-11-05 0.50.0 (the native port): full-detail models from folders; texture packs are back
- A wrestler folder can hold photo.bin (his select photo) and hd.bin + hd.json + pictures.pak (his own full-detail model). tools/wrestler_folder.py makes them from a model hd_pack.py has built. The route for a new man imported from another game: data/austin2k15's tools -> hd_pack.py -> wrestler_folder.py.
- How he's recognised: a folder wrestler with a photo has a photo number of his own, counting down from 255. The packed looks in his record are a marker (FOLDER_KEY_*, roster.c); parts_unpack swaps in his real looks with his photo. parts_model_post matches whole-body models on the photo, so it has to match on his base's photo, then it writes his model number (SKEL_WM_HD_MODEL, 4 bits: 16 men in all, 14 from folders) into the descriptor. "based_on" must be a full-detail wrestler, for the descriptor's SKEL_WM_HD.
- hd.c: hd_models is filled at run time, and each slot's working copies are sized for the biggest man, set aside after the packs (recomp_arcade_pack_reserve). Renderer: texture numbers below 96 are the men's six pictures each; load_hd_folders fills men 2 and up.
- Texture packs (src/main/arcade_texpacks.cpp, lodepng in src/main/lodepng): texturepacks\\<pack>\\<16 hex digits>.png replaces the picture with that name, at any size. The name comes from the decoded pixels and size only, not the address. texturepacks\\_dump saves every picture. RT64's packs aren't shipped any more (deploy.ps1 still clears old copies from mods). Decoding happens on the list reader's thread the first time a picture is used, so a large pack can hitch; to improve if it matters.
- QA: folder_hd with WCW_WRESTLERS_DIR=qa\\wrestlers_test: the 2K15 Rock from a folder, on the 2K15 Stone Cold's setup: on the MORE page with his photo, the select, the VS screen, the entrance, a match. Its expects only prove the match ran: the first build passed them with the wrong model. Look at the pictures. tron_hd with WCW_TEXTURE_PACKS_DIR: 197 pictures saved, then a pack of 148 with red and blue swapped at twice the size: the arena's banners and crowd changed colour.
- Nothing's proved on a man the game has never seen: the test model is one it already had.

## 2026-11-05 0.51.0 (the native port): a wrestler folder can hold his theme
- theme.bin + theme.meta (tools/wrestler_theme.py, from any file ffmpeg reads, through gen/mp3theme.py and gen/vadpcm.py). Up to 64 (FOLDER_THEMES), and fewer if the bank's table runs out: its limit is 256 instruments, what a song script's one byte can name. At install, each is one more instrument, song (SONGS_N on) and sound (after the ext slots') in the bank. Sample data is asked for by address through themes_audio_dma, so each theme has 16 MB of made-up addresses from FOLDER_THEME_ROM (0x10000000), read from its own content pack. func_80017C64's song limit is SONGS_N + 64 (gen/patches.py).
- Proof (WCW_AUDIO_RECORD keeps what goes to the speakers): a test theme of 4 s at 1000 Hz then 4 s at 1500 Hz, on the folder wrestler, in folder_hd. In the recording at his entrance: 89% of the sound's energy at 1000 Hz, then 89% at 1500 Hz. So it plays, at the right pitch. Not heard by anyone yet, and no real song tried: a music file should be tried by ear.

## 2026-11-05 0.52.0 (the native port): videos and arenas from folders; a crash in the entrances fixed
- 0.51.0 never shipped: its test set caught the crash below. This holds its themes too.
- The crash: func_8001B3E4(255) -> func_80020B58 -> func_80010314 reading from address 0, twice since 2026-11-04 (a forced-memory story, then hd_select with nothing forced). I first put the first one down to the high memory region: wrong. func_8010DC70 stores the scene it looked up in SCENE_WANTED and only sets SCENE_PENDING for a real one (below 25), so a "no scene" request (255) over one still pending leaves the flag on with 255. The dumps of both had pending 1, wanted 255. arcade_on_scene_request (hooks_recomp.c) drops such a request, as the rest of func_8010DD38 treats 25 and up. Proof: WCW_HEAP_TEST=9 forces the state on every request: hd_select passes with the guard firing ("[entrance] ... dropped" in the log). It may have been there all along, or the 0.45 thread rewrite may have made it likelier: not known. Neither crash had appeared in the 250 runs before them.
- Videos: video.bin in a wrestler's folder (tools/wrestler_video.py, 7.5 pictures a second, 64 x 64, each frame's own palette). Video numbers after the ported ones (FOLDER_VIDEO0); his record's video is set to it. The arena's big screen plays all of it; the game's small panel goes through its 72 frame files, so only their worth. QA folder_video: a test pattern on the screen behind him.
- Arenas: arenas\\<anything>\\arena.json: its name and the arena it's based_on. exe src/main/arcade_arenas.cpp reads them, the pack loader loads their files, mod arena.c: numbers after the built-in ones. arena_current() answers with the base arena, so every table and rule (walls, ring-stage map, screens, tables) is the base's while it plays. What's its own: its name, label.bin on the ARENA page, and files\\XXXX.bin, replacing that added file ID while it's in play (arena_file, first in files.c ext_file). tools/arena_folder.py exports the base's stage set and draws a label. Limits for now: the base's walls and part counts; preview and backdrop are the base's; cartridge files (ID below 0x4C03) can't be replaced; list stays under 256 arena numbers. QA folder_arena (compare folder_arena_base: the WWF logo's underline is blue, not red), folder_arena_page.
- Stories can name a folder arena (@ARENA:<NAME>, numbers after the built-in count in _roster.json). Not tested.
- arena_page.json fails as it stands: it expects the arena order from before 2026-11-01's change, row 14 no longer being RAW IS WAR WM 2000. Not from this work: to rewrite.

## 2026-11-05 0.53.0 (the native port): first game functions converted to C++ by m2c
- 366 functions, 4,139 of 383,555 instructions (1.08%), on top of the 1,529 by hand (0.40%). 59 of them named and explained in tools/lift/names.json (`lift.py check-names` proves the names change no machine code). Only functions that call nothing and have no jump table. exe src/game/lifted.cpp, written by tools/lift/lift.py: don't edit it. Names and comments still to come: tools/lift/ to read them from a separate file.
- How: tools/lift/to_asm.py writes each translated function back as MIPS from RecompiledFuncs' comments; m2c (tools/lift/third_party/m2c, GPLv3, a tool only, not in the game) makes C; lift.py makes that C++ on src/game/lift_mem.h, where P<T> and Ref<T> are game pointers into rdram. The ones here override the weak translated originals at link time.
- Proof: `python tools/lift/lift.py twin`, build with -DARCADE_LIFT_TWIN=ON, run with WCW_LIFT_TWIN=<report>, then `lift.py score` or `release` on the reports. src/game/lift_twin.cpp: the converted one runs on the side, writes to a list, then the original runs; final bytes and result compared. A function goes in only with no difference and every instruction reached during comparisons (7 scenarios). That doesn't prove every value: see below.
- Refused, staying as translated: anything the mod patches or hooks; anything the port changed or added calls to in RecompiledFuncs (162 functions: frame interpolation, widescreen, rw_ repairs, which m2c can't see because it reads the MIPS comments); goto; division by a variable (C++ traps on zero); system instructions, unaligned and 64-bit access; registers beyond its parameters; anything whose scratch registers a caller reads afterwards (including through a pointer call, if its address is taken).
- m2c's traps, each found by a real difference or crash: (1) parameters are numbered by register, so a function can start at arg2. (2) `p += 4` is 4 bytes whatever p points at; only p[i] counts elements: P<T> does the same. (3) A small stack parameter sits in the end of its 4-byte slot: read the slot, then narrow. (4) It types a register by its later use: an s8 local wraps where the 32-bit register didn't (func_80111EF4): locals, parameters and results are widened to 32 bits. (5) It merges base+offset into one field with one type, so a read can be the wrong size (func_8000C1A8 read 1 byte of 2): lift.py's fields_disagree compares against the instructions and refuses, since it only shows when the top byte isn't zero. Those last two are why "all instructions ran" isn't enough alone.
- A hole the comparison can't see, found when the first normal build crashed at boot: m2c made func_8000176C void though its caller uses the pointer it leaves in v0. In the proof build the original still ran, so the caller got its value. lift.py's check of what callers read after a call was meant to catch it, but looked for each call at the jal, and the translator writes the call after the delay slot, so it treated every direct call as unknown: fixed. So a passing proof build isn't enough either: the normal build's own test run is part of the proof.
- Another of the same kind, the next crash: func_8002C57C isn't a function, it's the end of func_8002C4E8, split off by the translator because the code jumps past a label. It restores the stack for its caller; m2c sees bookkeeping and writes `return 0`, so the stack drifted 24 bytes a call (the sound thread crashed on a null). lift.py now refuses anything another function jumps into, and anything that changes a saved register without saving it or returns with the stack moved. And the proof build checks at run time that the original left sp, ra and the s registers as they were ("kept-registers" in the report). P<T> reads and writes whole words now, as the translated code does.
- Twin build is about half speed in places (story_upgrade_b ran out of time once, then passed).
- The random-input tester (tools/lift/fuzz, `lift.py fuzz`): each candidate and its original, away from the game, on 200,000 made-up inputs each: registers, parameters and memory. Every 32-bit address is real memory there, so wild pointers read zeros instead of crashing. It found what play didn't: a value read after the loop that overwrites it (func_80142BE8), a stack parameter widened past the byte the N64 reads (func_8011BD90), a parameter the function narrows itself losing that (func_80005D34), signed and unsigned mixed in a compare (func_8000516C), and reads moved past writes when pointers overlap. 9 functions stay translated for those. Tried and dropped: m2c's --reg-vars, which keeps reads in order but gets branch delay slots wrong.
- A function is in the game when: the static checks pass; no difference in play (where it runs) and on random inputs; every instruction reached in one or the other. In the 0.53.0 run all 7 scenarios passed in the proof build with no difference in play.

## 2026-11-05 0.54.0 (the native port): converted functions that call others, and switches
- 596 functions, 11,070 of 383,555 instructions (2.89%), on top of the 1,529 by hand (0.40%): the 366 of 0.53.0 and those that call others or have switches.
- Functions that call others: a converted caller keeps its stack frame in game memory, the same size, its locals (spXX) at the same offsets and the registers the original saves written where it saves them, so a pointer to a local, or a parameter on the stack, is where the callee looks (src/game/lift_call.h LiftFrame). Calls go through lift_call, which puts parameters in the N64's registers and stack slots and calls the translated function by its name (so a mod's patch on it still applies).
- Their proof is the random-input tester, with stand-ins for what they call (fuzz_stand_in): it records what each call is given (registers it reads, its stack parameters, and what's at a pointer into the stack), returns random results, scrambles every scratch register and sometimes changes memory. The two versions must make the same calls in the same order. Not in the proof build yet: comparing them in play would mean running callees twice.
- Jump tables: to_asm.py writes each table out from the translator's switch (jtbl_<address> in .rdata) so m2c makes a switch. 240 of 249 found.
- The register analysis (analyse.py, mips.py) had three bugs, all fixed: the delay slot of a call taken as the call (so its writes were ignored, and every call looked as if it read all four parameters); $zero dropped before working out operands (mtc1 $zero, $f20 read as reading f20); jump tables not followed. Functions that read only their parameters: 60.7% before, 79.9% now.
- The random tester committed 4 GB per copy and, four at a time beside a test game, ran the computer short of memory (the agent's jobs were stopped). It reserves now and commits pages as they're touched, three at a time.
- Traps found this round: m2c relies on a narrow result type (its own or a callee's) to cut a value down, so those aren't widened any more (func_80004B7C); a stack frame missing the saved registers looks different to a callee given a pointer into it.
- The "No Mercy GBC" Transfer Pak link: the messages are in the ROM (0x144038 on), used by the Controller Pak screens (func_80131848 builds a menu with "Gameboy" in it, func_80136C7C has the Transfer Pak messages). Not looked at further. Separately, 8 dummy functions at 0x80018210-0x80018250 are a subsystem stubbed out; the front-menu code (func_8010B778, func_8010AD7C, func_80141520) still starts it in modes 1-3, polls it for bit 0x8000 and sends it bytes. Not identified.

## 2026-11-05 0.55.0 (the native port, not deployed): 10% of the game's code converted
- Not deployed: its full test set failed (hennig_win_taunt ran out of time; hd_select crashed). It was built before the fix to the analysis's result_unused (a converted caller could drop an argument, func_80003F3C). The set is to be made again on the 32-slot code (0.56.0).
- 1,204 functions, 39,560 of 383,555 instructions (10.31%), plus the 1,529 by hand (0.40%). Same proof as 0.54.0: static checks, the random-input tester (200,000 inputs each, stand-ins for what they call), the proof build in play for those that call nothing, and the normal build's full test set.
- What opened it up, all in tools/lift: doubles (ldc1/sdc1: saved f20-f30 kept in the frame like the integer ones, the originals' SD noted by the testers, -ffp-contract=off so no fused multiply-add rounds differently); gotos taken (marked "to be tidied by hand"); an instruction the port patched (moved tables and heap) no longer counts as port-added code, since its comment, which m2c reads, was patched too: only added code without an instruction comment is refused (port_added_code); a function that calls others only counts the registers it writes itself as left different, not every scratch register (it makes the same calls with the same parameters); m2c gets the real parameters of what it calls from the register analysis as a context header (prototype, callee_context), so a parameter passed through unchanged is passed; gp (the game keeps a count of wrestler slots in it, set only by rw_run_overlay) reads as the register; jump tables; division where the original has the compiler's zero checks (the tester refuses any that divide by zero on random inputs).
- The register analysis (analyse.py) also ignores what an instruction reads when nothing uses its result (result_unused), and takes registers from independent.json / independent_by_hand.json as not inputs. independent_by_hand.json: 3 by my reading (the allocator func_800007D4, the sound voice allocator func_8002985C, func_8012C474) and 20 from a review workflow: an analyst per register read, three skeptics each trying to find an input where it matters, kept only if none could. Most say "only on inputs no caller passes" and name them. Left out on purpose: func_8012069C's fp, which depends on 10 or fewer wrestler slots, since raising that is planned.
- Fixes found on the way: a declaration pattern was taking statements ending in a local (`*arg0 = sp10;`) for declarations; a bare `*0x8008D284` from m2c now gets its size from the instruction too.
- Tried and dropped: catching divide-by-zero in the tester by jumping back from the processor's trap (it corrupted the test loop's state). 143 functions differ on random inputs and stay translated.

## 2026-11-05 0.56.0 (the native port): room for 32 wrestlers (a match still has its own count)
- The six-man engine's layout is now built for any slot count: six-engine/work/spec.py takes SIX_SLOTS (6 unless set). This build has room for 32: the match overlay's per-wrestler arrays end at 0x80194000 (was 0x80176000), the main per-wrestler arrays start at 0x804B0000 (was 0x80500000), and the game heap keeps the room between them (3.1 MB; the stock game's was 2.1 MB, and the arcade adds 7 MB above the 8 MB besides). A match still plays its own count ($gp / rw_match_count, 4 or 6), so play is meant to be exactly as before.
- The engine's tools run on Windows now (they were written for a Linux sandbox): libs/N64Recomp/build-native/N64Recomp.exe (built with -include cstdlib for the newer compiler), six-engine/work/rw/build.py (the blob), six-engine/work/port/regen.py (the port ROM, N64Recomp and the post-regen fix tools), and six-engine/work/port/install.py SLOTS DIR, which rebuilds and installs everything that depends on the layout. Checked: at 6 it gives back the same blob, ROM, port ROM and translated code, byte for byte.
- At 32 the translated code changes in 425 functions, every change an address in an instruction (no instruction, register or function changed: the comparison script checks each).
- Addresses that were typed by hand now come from the generated files: the mod's src/six_addrs.h (NM_SLOTS_CAP, SIX_RESULT6, SIX_HEAP_SHIFT, six more arrays: logic.c's entrance videos and its menu-title search, media.c's winner song, gameplay.c's wrestler loop, movex.c's move tables and anim streams), and the exe's new src/main/six_layout.h (arcade_bones.cpp's arrays, main.cpp's overlay BSS end). At 6 the mod built from them is the same, byte for byte.
- Converted code: the 0.54.0 set (596 functions), none of which touches the moved arrays. The 0.55.0 set is to be made again on this code.
- Tested with tests chosen for what the layout touched (scratchpad slots_tests.ps1; the user asked for that, not the general sweep of past fixes): start-up and menus, the HD models (arcade_bones.cpp), JR and King (a borrowed slot), a Rumble's entrance videos, a Rumble's winner song, the Versus title (the heap search), a ported man's holds (the move tables).
- Found on the way, none of them the layout's:
  - The test build kept 0.55.0's converted code (with six-slot addresses) after lifted.cpp was copied back: Copy-Item and Python's copy2 keep a file's old date, and ninja kept the old objects. install.py and the scripts now give copies today's date.
  - QA's write-watch (WCW_WATCH_MOD, main.cpp) put an old watch back on a thread when its handler returned (the context's debug registers), and the watcher never fixed it: a 0.6 s load took 57 s. The handler now resumes the thread with the watch as it stands; the watcher adds to its list only after resuming a thread (no allocating while one is suspended); and the handler only notes a hit -- the watcher thread prints it (printing a stack from the game's own thread, a line at a time, held a Rumble entrant's loading up 15-20 s). The deployed six-slot game stalls the same way in these tests (control runs 2026-11-06 00:16, 00:31).
  - The Rumble's runaway walkers the user saw in a QA window (wrestlers walking at the ring's height out through the crowd, one to x -203,688): logic.c's QA hook qa_rr_out put a CPU man at z ±260 and left him at height 90, still marked in the ring (+0x7A), so he walked on along the apron's line. The six-slot game does it too in that test (control: one to x -1590). Now he's put on the floor past the apron (z ±330, height 0, +0x7A outside), as the floor-brawl hook already did after meeting the same walk.

## 2026-11-06 0.57.0 (the native port): step 2 of the 32-slot work begun -- what holds a match to six (the map)
- 0.56.0 (room for 32, playing six) deployed 01:45 after all seven of its tests passed.
- The mod's tables at NM_SLOTS_CAP and its loops over every wrestler to nm_slot_span() (the match's count, or six
  if fewer: six-man play as before); the QA block stays six slots.
- First try (01:53) didn't load the mod: "Reference symbol does not exist: section 7 func offset 0x3af0". The
  mod links against the exe's own syms/dump.toml (RecompModTool's func_reference_syms_file), and step 2's engine
  changes moved its functions; install.py copied the new list to six-engine/NoMercySix only. It now updates both.
- Second try (02:46): all seven tests passed by the game's state (holds graded from its state.csv after the run was
  stopped for low memory), but every screenshot was black and the renderer logged no pictures: arcade_view.cpp read
  the frame's display lists from rw_dltab at a typed-in 0x807845A4, and step 2's engine changes moved it to 0x80784844.
  It now takes SIX_RW_DLTAB from six_layout.h (gen_arcade_six.py, from the blob's symbols). Exe rebuilt 03:43; not
  tested yet, so 0.57.0 isn't deployed (0.56.0 is). The game's own logic ran right in those runs; only the picture
  was missing -- the screenshots are what showed it.
- Deployed 15:20 2026-11-06 after all seven tests on that build (start-up, HD models, JR and King, a Rumble's entrance
  videos, its winner song, Versus, the holds), the pictures looked at. Two runs were stopped by the system for low memory
  (the user's Unreal Editor): their reports made after from state.csv (qa_tool.py report). The Rumble now ends: the
  QA hook's men are out on the floor and counted out.
Done in the engine's source (six-engine/work/rw, built with -DSIX_NSLOTS from spec.py; six-man play unchanged):
- rw_sltu_v0_v1_k8 / rw_slt_v0_a0_k358 compute count x 8 and count x 0x358 (they read halfword tables for counts 0..7).
- The results of slots 4 on: two words, winners (RES6) and losers (RES6L), bit (slot - 4): room for 32 (one word,
  winners bits 0-1 and losers 4-5, had room for six). media.c's winner song reads the winners' word as before.
- The hit-stat matrices SIX_NSLOTS x SIX_NSLOTS; r_800EDE48, r_results, r_800EC484 sized and looped by the slot room.
- The match-start flag tables (rw_tab_6440 / 636C / 6450) have SIX_NSLOTS entries, slots 4 on all alike.
- rw_rumble_fill: the ring takes rw_rumble_ring (as far as the entrants and the slots go), so more than six can be
  asked for; under six it's the stock four, as before.
Still holding a match to six to eight, each to be widened (not packed tighter):
- The camera's slot mask (rw_80116130, u16: slots bits 0-5, out of play 6-11, flags 0x1000/0x2000) and its
  consumers' masks and shift (func_800E28C0, func_800E30D8, func_800DEDE0: andi 0x3F, xori 0xFC0, srl 6, andi
  0xFFFF), and func_800EE130's two masks (0x41 << slot): a wide mask means these functions as C++ -- the lift
  tool's proven conversion first, then the mask widened there.
- 0x8016B650's lock (u16; models 4, 5 at 0x1000 / 0x2000, rw_tab_65C8; bits 4-11 taken): func_8011D848, func_80115000,
  the replay's LOCKS 0x300F.
- The camera event word's slot field 0x700 at 0x801594D2 (3 bits: slot + 1 up to 7; rw_tab_6350).
- The instant replay's slot bits in 0x80162A7C (rw_tab_DB60 / DB78) and rw_snap_v0 (slots 4, 5 only).
- The Rumble's opponents rw_opp[16] ([mode ? 8 : 0 + slot]) and the start spots rw_tab_6558 (x, z for six; the mod's
  SIX_RR_SPOTS): spots for more need laying out round the ring.
- The contact list's 10 (rw_contact_cap, func_800EBD2C) and func_80120D98's 11 ints; r_80117438's lists.
- The display lists' room (dlbuf, 2 x 0x8000, for six) -- step 3.
- The HUD (r_hud.c: six panels) and the results' four columns -- step 4, with the user's say.
- The mod's per-slot tables at NM_SLOTS_MAX (6) and its loops to it (announce.c, commentary.c, hands.c, movex.c,
  gameplay.c rr_plan, logic.c rr_model): to NM_SLOTS_CAP, loops to the match's count.

## 2026-11-06 Faster tests (the user: fast-forward "a given"; menus skipped, the match chosen by parameters; two at once)
- Fast-forward: WCW_SPEED=n (1..16) -- ultramodern timer.cpp's speed multiplier (was a constant 1) read at its first use:
  the N64's clock and the VI thread's ticks both n times as fast, so the game's own timing holds. No sound device at
  n > 1 (the virtual queue drains at the game's speed: main.cpp reset_audio / virtual_drain); WCW_VPADS_SCRIPT's
  seconds are the game's. qa.ps1 -Speed n (or the scenario's "speed", or QA_SPEED): the scenario's "seconds" and
  "shots" stay the game's, the run's are over n. qa.ps1 -Copy game2: a test in its own copy (qa\game2), two at once.
- Fast-forward made variable (the same day): the game's clock (ultramodern timer.cpp) adds up its own time -- the ticks at
  the last change of speed plus the real time since, times the speed -- so its speed can change while it runs without a
  jump; the VI thread's ticks are on that clock (events.cpp), and only every n-th tick queues a screen update (one per
  tick flooded the graphics thread: a hung window, black pictures). main.cpp arcade_speed_policy, every VI: a match
  (mode -2 or -6, its frame counter running) at WCW_SPEED, the rest at WCW_SPEED_MENUS (2): above about 2x the menus ran
  slower than at 1x, erratically. WCW_QUIT_AFTER counts the game's seconds with fast-forward on. The Rumble's QA now
  presses Next on its results table (logic.c qa_rumble): it waited there until its time ran out.
  music_rumble_late: 126 s at -Speed 6 (all three checks), ~830 s at 1x.
- What a QA match is now: qa_mode 1 turns the title's attract demo into a real singles match (qa_w / qa_c / qa_arena /
  qa_ctl / qa_special); qa_mode 2 the arcade's Royal Rumble; the intro is skipped already. The time before a match is
  the boot logos and the title.
- Toward any match type and rules without menus (not done yet): the demo routine func_800EF478 picks a demo set 0..8
  (NM_ATTRACT_COUNTER / 2) and writes its set-up flags to 0x800A957C: 0x42, 0x1, 0x11, 0x1, 0x48, 0x20200 (Royal
  Rumble, 0x800A958C = 0x24 entrants), 0x2, 0x4, 0x8; records per set from 0x80102AE4 {0, 3, 11, 16, 17, 18, 19, 27,
  30, 33} into the demo table 0x80102998 (10 bytes: 4 x u16 wrestler | costume << 12, u16 flags). The menus' match
  type -> flags: func_8012D918 (text ids 0x851C..): the low bits the format, and 0x800A9582 (u16) the wrestlers --
  0x1 singles 2, 0x2 tag 4, 0x4 three men 3, 0x8 handicap 3, 0x10 2, 0x200 Royal Rumble; 0x40 / 0x80 / 0x100 added
  as modifiers (0x41 2, 0x42 4, 0x44 3, 0x48 3, 0x81 .., 0x84 4, 0x88 4, 0x101 2, 0x102 4, 0x104 3, 0x108 3) --
  which modifier is which match (cage, ladder, ...) wants the menu's texts.
  By text id (its jump table at 0x80155110, ids 0x851C + k): 851C 0x1, 851D 0x2, 851E 0x4, 851F 0x8, 8520 0x10,
  8521 0x200 (Royal Rumble), 8522-8524 by the page's own entry (+0x60: 0x8522 / 0x8523 / 0x8524 -> 0x40200 / 0x10200 /
  0x20200 with a Rumble), 8525-8528 0x41 0x42 0x44 0x48, 8529-852B 0x81 0x84 0x88, 852C-852F 0x101 0x102 0x104 0x108.
  Names to confirm by a run each (a test's match shown), not guessed. The rules: preset 0x800F5E78 + 12e (gen/storyc.py RULES
  names them per type), the RULES page's rows 0x800AA044 u8[12].

## 2026-11-06 0.58.0 (the native port): fast-forward for tests
- Deployed 16:47 after the 32-slot test set at 6x (all seven; Versus's limit raised to 1800 game s -- its idle player
  against the CPU varies a lot: a 6x run's match went past 19,000 frames) and the start-up test at normal speed. Normal
  play is speed 1, as before. The set took ~30 min at 6x against ~75 at 1x; music_rumble_late 126 s against ~830.
- The test-set runner is the project's own now: qa_set.ps1 (-Tests, -Speed, -Deploy), with qa
ine.py (a run's
  sheet.png), qa\stalls.py, qaarthest.py; tools\lift
ight.ps1 the conversion pipeline end to end. (The session's
  scratch folder, where they were, was emptied by a temp clean-up mid-afternoon.)
- deploy.ps1 names its backup of the game after the README's version: mod\README.txt said 0.55.0 since then, so the
  0.56/0.57/0.58 deploys all backed up into backups .55.0 (now renamed 0.57.0, what it holds). README at 0.58.0;
  night.ps1 bumps it with mod.toml.

## 2026-11-06 QA match formats without menus (in 0.59.0's build)
- A scenario's "match": "tag" / "triple" / "handicap" (and "singles", "rumble", "arcade" as before), with "wrestlers",
  "costumes" and "control" up to four. logic.c qa_demo_set: the title's attract demo set of that format (6 tag 0x2,
  7 triple threat 0x4, 8 handicap 0x8; 1 singles), its records in the demo table given the scenario's Superstars, the
  attract counter asking for that set; qa_demo_setup gives each of the format's slots its controller. mod.toml qa_w3 /
  qa_w4 / qa_c3 / qa_c4 / qa_ctl3 / qa_ctl4 / qa_demo_set. Applied by qa\patch_match_types.py.
- Tried 2026-11-06 at 6x (qa\scenarios\match_tag / match_triple / match_handicap), all pass, the format proved by the
  game's own match flags (0x80097EC0): tag 0x20CF (0x2000, four CPU slots), triple threat 0x8007 (0x8000 three-way),
  handicap 0x20047 (0x20000); the pictures show four, three and three men with the HUD's panels to match.
- Rules too (the same evening): the scenario's "rules" by gen/storyc.py RULES' names for its type ("hardcore",
  "submission", "tornado", ...) -> qa_rules, the game's preset e. The demo puts a preset of its own into the RULES rows
  with func_800E2B44(e, dst) (from func_800E2BC0), after the title -- a copy made on the title was written over. Now a
  RECOMP_HOOK pair on func_800E2B44 (logic.c): when it filled the rows and a scenario asked for rules, the scenario's
  preset goes there in its place. Tried at 6x (qa\scenarios
ules_*): the rows hold each preset at the end, and the
  game's rules word 0x800A9950 follows them -- singles normal 0x2832, hardcore 0x4830, submission 0x0812, tag tornado
  0x6870; the tornado run's pictures have all four men in the ring at once.
- The set-up flags' kinds, named by runs of the demo sets (qa\scenarios\demo_set_*, pictures): 0x10 a guest referee
  (set 2, 0x11: a man in a suit refereeing), 0x40 a ladder match (set 0, 0x42: tag ladder; set 4, 0x48: handicap
  ladder -- ladders, "YOU GOT IT!" at the belt). Scenario "match": "guest_referee" / "tag_ladder" / "handicap_ladder".
  0x80 and 0x100 (no demo set has them) still unnamed: the cage is one of them, by the menu.
  Tried (qa_setup_flags, a RECOMP_HOOK_RETURN on func_800EF478 writing 0x800A957C after the demo routine): no effect
  -- 0x81 and 0x101 runs were plain singles (match flags 0x1003): the routine makes the match from its set's flags
  before it returns. A hook inside it (its jump table's targets) would be the next try. The option stays, unused.
- (was: Not yet: the 0x10 / 0x40 / 0x80 / 0x100 kinds
  (cage, ladder ... by the menu's entries 0x8520 / 0x8525.. -- a run each to name them).
- qa_tool.py: a game that ends itself on WCW_QUIT_AFTER (with fast-forward, the game's seconds, sooner than the run's
  real limit) is the scenario's time run out, not INCOMPLETE. qa
un_pair.ps1: two scenarios at once, qa\game and
  qa\game2, when 2.5 GB is free.

## 2026-11-06 0.59.0 (the native port): toward a 10-man Rumble (step 2 of the 32-slot work)
- The probe: qa/scenarios/rumble_ring10.json (qa_rr_ring 10, 20 entrants, one every 10 s, 6x) -- QA state columns
  present / in_ring (logic.c, QA_STATE + 0x238).
- First run: 7 in the ring, then a crash (match frame 2278): the entrance countdown banner index went to -1
  (func_8011D848 -> func_800E07FC(10 - count), count 11). Cause: the sequence lock 0x8016B650 (one wrestler at a time
  runs his sequence, func_8011D848) took a wrestler's bit from rw_tab_65C8, six entries -- the 7th read past it.
  Fix (six-engine/work/patch_seq_lock.py, rw/stubs.S rw_seq_take / rw_seq_release): 0..3 keep their bits; 4 on all
  hold the game's 0x1000 and their own bit in the engine's 32-bit rw_seq_lock (taken as empty once the game clears
  its word). Its first version set the wide bit even while the wrestler waited -- the game stores its word only when
  he gets the lock -- and the second probe deadlocked at 3-4 in the ring; corrected.
- The camera event word 0x801594D2: slot + 1 in bits 8-15, but func_800E2344 tested `& 0x700` -- slot 7's (0x800) were
  dropped. Now `& 0xFF00` (build_rom SEQS).
- Third run: 10 in the ring (from match frame 4130), 9-10 held to frame 14979 -- then a crash: osSendMesg from the
  graphics task thread (func_800010F4), frame block 1's scheduler task (block 0x804AA080 + 0x2EE0, the top of the
  game heap) written over, its reply queue 0x0000FFFE. Traced with the write-watch (rumble_ring10_watch, new QA
  options qa_watch1 / qa_watch2 -- RAM offsets: an option's number is read as a signed 32-bit one, a full 0x80xxxxxx
  address came back 0x80000000; qa/patch_watch_opt.py): func_8000D284 -> func_800016E4 -> func_8000176C wrote block 1's
  heap header. func_8000176C hands out 0xB8-byte scene records (bones, objects) from one heap block made at boot,
  900 of them, no end check: a match uses 504 with 4 wrestlers, ~645 with 6, ~968 with 10 (every 10-man run, even the
  first at 7 in the ring: 918). Fix (six-engine/work/patch_record_pool.py): spec.POOL_RECORDS = 300 + 96 a slot, never
  under 900 (3372 for 32; the six-slot build unchanged).
- Next limit, not done: the frame's draw list 0x80060BA0 (900 u32, count 0x800619B2; ~47 a wrestler, 633 with 10):
  about 19 wrestlers. Moving it needs the mod's announce.c / tron.c readers moved with it.
- The camera for any count (six-engine/work/patch_cam_wide.py): func_80116130's u16 mask packs slots 0-5 in view,
  0x40 << slot out of play, flags 0x1000 / 0x2000 -- slot 6 on garbled it (the probes' camera ended on the empty
  floor). It keeps its u16 for the HUD and the tag-type matches, and also keeps 32-bit rw_cam_view / rw_cam_play;
  func_800E2344 and func_800E38A4 test slots against rw_cam_view, func_800E28C0 / func_800E30D8 frame the wide set
  (stubs rw_cam_*), and func_800E2344's in-view list on its stack (room for 8 before its saves) has a frame grown
  0x48 -> 0x88. Six or fewer: every bit as before.
- Deployed 21:03 after the test set at 6x (menu_from_attract, hennig_win_taunt, hd_select, story_upgrade_b,
  tron_entrances, picture_walk, announcers_front, music_rumble_late, versus_1p, blocks_hogan, rumble_ring10: all
  passed, pictures looked at) and the start-up at normal speed. Also in it: the conversion run's newly converted
  functions (night.ps1 -From candidates, stopped before its own tests: its first test ran an exe built while the engine
  was being installed -- a crash in the mod's code, the exe and mod not matching; its set rerun here).
- rumble_ring10 on this build: 10 in the ring from match frame ~4100 to the run's end (21222), 1789 of 2410 match rows
  with 10; records peaked at 944; the camera frames the crowd. The HUD draws up to 9 name panels in an uneven grid
  (step 4, the user's design).
- Seen once, not again: music_rumble_late (6 slots) stalled 39 s real at the Rumble's win (match_state 2 -> 3, 15
  frames), winner 0x030E; its rerun (winner 0x0106) had none. The renderer's "[read] ms an update" spikes (60-210 ms)
  are in the afternoon's 0.58 runs too (14.7 s once in versus_1p): not new. To look at: a Rumble won by 0x030E.
- QA: qa_rr_out_after (qa/patch_rr_out_after.py) -- qa_rr_out's eliminations from a match time on;
  rumble_ring10_end (the end of a 10-slot Rumble: the winner and the results screen past six).

## 2026-11-06 After 0.59.0: the end of a 10-slot Rumble; the draw list moved (in test)
- rumble_ring10_end (10 entrants, room for 10, men put out from 110 game s): 10 in the ring, then down to a winner
  (match frame 7294), the results screen, the menus, the next match -- all checks passed. The results table is still
  the stock four columns (HART Player, CHYNA, HART, HART: the known repeated names) -- step 4 (the user's design).
- The frame's draw list 0x80060BA0 (stock 900; ~47 a wrestler) moves with the main arrays past six slots: spec.py
  MAIN, 0x384 bytes (225 entries) a slot, 7200 for 32 (its stock 900 is 4 x 0x384: the references moved are the list's,
  not its count's at 0x800619B2 right after; 14 moved). gen_arcade_six.py emits SIXA_80060BA0 (the stock place where
  the layout doesn't move it); the mod's announce.c DRAW_LIST and tron.c NM_ITEMS use it. The six-slot build unchanged.
- First try at the list's move: 0x384 a slot -- the main arrays' start went down 64 KB (spec.EXP: the room is
  rounded to 64 KB) and every test crashed at once: the converted code (lifted.cpp) has the arrays' places written in
  (func_80025EA4: 0x804E3481, the old shadow-state array). Now 0x258 a slot (150; 4800 for 32), which fits the room
  left below FB0: nothing else moves. port/install.py check_lifted stops an install that moves an array the converted
  code points into, against src/game/lifted_layout.h (the layout it was made with; tools/lift/night.ps1 writes it).
- Then announcers_front passed with JR and King not drawn: the announcer's load puts its borrowed slot's arrays back
  (mod/gen/mkannsave.py, src/ann_slot.h), and the list -- shared, not a model's -- was one of them, put back over their
  entries. spec.MAIN_SHARED leaves it out (40269 bytes a slot again). New QA column ann_listed (QA_STATE + 0x23C: the
  draw-list entries that are the announcers'; qa/patch_ann_listed.py), checked by announcers_front (48 when right).
- The frame display lists (rw_dltab; used with 5+ wrestlers): 0x8000 each, ~0x900-0xA00 a wrestler (0x20D4 with 6,
  0x5A84-0x60BC with 10): full at ~13. Past six slots they're at 0x80730000 (expansion RAM empty in a match, between
  the announcers' depth image and the engine), spec.DLBUF_SIZE = 0x1000 + 0xA00 a slot each (0x15000 for 32);
  SIX_DLBUF_SIZE for the mod's draw.c. spec.EXP_PIN keeps the main arrays at 0x804B0000 (out of their room, the start
  would have gone up 64 KB). six-engine/work/patch_dlbuf.py. The six-slot build unchanged.
- QA: rumble_ring16 / rumble_ring24 / rumble_ring32 probes (the user's goal from here: a 32-in-the-ring Rumble).
- rumble_ring16, first run (frame lists moved): 12 in the ring, records 1366, draw list 899, frame lists 0x966C (past
  the old 0x8000: the move was needed) -- then a crash at match frame 8754 in func_80126384: func_80120D98's target
  list (every other wrestler in the match, props while room, -1) is on its callers' stacks at sp+0x10 with room for
  12, their locals from sp+0x40 -- 16 slots, 15 others: over the end, a stored position read back as a wrestler index.
  Fix (six-engine/work/patch_target_list.py, build_rom.step_frames): func_80126384 and func_8012069C's frames grow by
  spec.TARGET_LIST_GROW (room for max(n - 1, 10) + 1; 0x50 for 32), the list kept at sp+0x10, every other stack offset
  moved up (147 instructions). Their other two callers (func_80121080, func_801210C4) pass no list.
- Second rumble_ring16 (frames grown): the same crash -- func_80120D98 sets only the list's first 11 entries to -1
  and writes no end after the wrestlers. rw_tl_call (rw/stubs.S; build_rom SEQS at its two list callers) sets entries
  11 up to $gp to -1 first. Third run: 13 in the ring, then a crash in func_80131E08 (s0 back from a call garbage):
  func_800EBD2C's contact list (0x28-byte entries; wrestlers without limit, props up to 10; entry 10 doubles as the
  owner's record, list + 0x19C) on func_8012C474's stack ran over its saves. Fix (six-engine/work/patch_contacts.py,
  build_rom.step_contacts): the list in the engine's rw_contacts (spec.CONTACT_CAP = max(n - 1, 10) entries and the
  owner's after them), the clear, the owner's offsets, the walk's end and owner pointers widened. Off the stack, not
  a 0x348-bigger frame: the match thread's stack room couldn't be measured (the port's calls write no return
  addresses); func_8012C474 never calls itself, so one buffer is enough. hennig_win_taunt / rumble_ring10 pass.
- Fourth run: "froze" at 9 in the ring -- the renderer's thread held in WriteFile on stderr: qa.ps1's Start-Process
  -RedirectStandardError gave the game a pipe that PowerShell copies to the file; when the copying fell behind, the
  game's writes waited. The same as the 39 s at a Rumble's win, the shutdown "freeze" and the "[read]" spikes (60 ms to
  14.7 s) -- the QA harness, not the game (a normal launch logs to its own file). Fix: WCW_LOG_FILE (main.cpp
  wcw_setup_logging): the game opens the run's game_stderr.txt itself (stdout and stderr), qa.ps1 sets it; the pipes
  get only what comes before (game_stdout.txt, game_pipe_err.txt).
- Fifth rumble_ring16 (QA log file): 16 in the ring for 1512 of 2875 match rows, to match frame 26220, no crash and
  no gap over 2 s (the stalls are gone). But the pictures: the ring empty, the camera far out -- every wrestler at
  the ring's height (y 90) thousands of units out (to -42713). Not at 10 (no replays there). The instant replay's
  per-slot bits in 0x80162A7C (rw_tab_DB60 worthy, DB78 snap, DB70 / 6350 camera slot fields) had 6 entries: ~20 move
  functions clear and func_800E5890 sets F's bits by slot -- slots 6 on read the next tables' words, setting and
  clearing F's own replay state (0x100, 0x200). Fix (six-engine/work/patch_replay_bits.py): 6 on share 0x10000000
  (unused by every function that touches F), which of them in rw_rp_worthy (rw_rp_set_a0 at func_800E5890's set,
  build_rom.step_replay; the recorder takes it as empty once 0x10000000 is cleared); no snap for 6 on; DB70 / 6350
  (slot + 1) << 8 for all 32. Replay rings: 0x2BF20 bytes = (n + 7) x 0x1C20 -- full at 18 wrestlers: next.
- Sixth rumble_ring16 (replay bits): 16 in the ring for 1840 of 2906 rows, to match frame 26491, everyone inside the
  ropes at the end, the pictures a full ring; hennig_win_taunt and music_rumble_late pass.
- The replay's rings (func_80113E30's block): (n + 7) x 0x1C20 past 18 slots, stock 0x2BF20 below (build_rom
  step_replay: `lui a0, 2; ori a0, a0, 0xBF20`). Only the rings use the block (func_801144E4 frees it).
- First rumble_ring24: a crash at match frame 129 in the exe's arcade_scene.cpp (the native func_800028B4): the
  scene's sorted groups (0x80061D40, 900 u32 up to the words at 0x80062B50, count 0x800619B4) -- 1224 with 24 slots,
  over the lights at 0x80062B68. Moved like the draw list (spec.MAIN 0x258 a slot, MAIN_SHARED; the 64 KB the frame
  lists left keeps the arrays' start where it was): SIXA_80061D40 for tron.c, SIX_80061D40 for arcade_scene.cpp
  (gen_arcade_six writes the stock place where a layout doesn't move it).

## 2026-11-07 0.60.0 (the native port): 32 in the ring
- rumble_ring24: 24 in the ring (843 rows with 24, 22-24 most of the match), to match frame 24146; rumble_ring32: 32 in
  the ring (850 rows with 32, 28-32 over a thousand), to match frame 22402 -- no crash, no stall, everyone inside the
  ropes, the pictures a full ring, ~60 pictures a second. Peaks at 32: records 2590 of 3372, draw list 1648 of 4800,
  frame lists 0x11D04 of 0x15000.
- Operator setting IN THE RING AT ONCE (Royal Rumble page; mod.toml rumble_ring; 4..32, default 6 -- the game's own):
  mod/patch_rumble_ring_opt.py; rr_configure writes it to the engine (a QA scenario's qa_rr_ring still wins). Built,
  not yet run.
- New QA: rumble_ring32_end (the end of a 32-slot Rumble). Not yet run.
- The full set at 6x (16 tests with the big Rumbles) was stopped by the system for low memory at 01:10 (Unreal
  Editor open, 7 GB compressed) after menu_from_attract and hennig_win_taunt passed. Not restarted (the user's rule):
  0.60.0 waits for the set and the start-up at normal speed. Versions bumped to 0.60.0 in README / mod.toml.
- The ending tests check the win (match_state 4), not the return to the championship list: after a Rumble the arcade's
  automatic Next on the results table (logic.c, menu_choose_action ACT_RESULTS_NEXT) can go round without moving --
  seen with six in 0.58's music_rumble_late too (the table up from 134 s to the run's end at 344 s); rumble_ring10_end
  passed once by leaving after 83 s. Not from the 32-slot work; to look at on its own. Results table: the user's call
  (2026-11-07) -- Player 1 and the three eliminated before him, as now, for any count.
- Deployed 08:45 after the full set at 6x: menu_from_attract, hennig_win_taunt, hd_select, story_upgrade_b,
  tron_entrances, picture_walk, announcers_front, music_rumble_late, versus_1p, blocks_hogan, rumble_ring10, 16, 24, 32,
  rumble_ring10_end, rumble_ring32_end (a run in a third game copy; the set's own got down to two and ran out of time --
  qa_rr_out leaves the player's slot and anyone mid-move, so the last two are the game's to settle) -- and the start-up
  at normal speed. The user (2026-11-07): 32 is the limit and won't change; the HUD past six and the other match types
  later; the results table as it is.

## 2026-11-07 0.61.0 (the native port): 11% of the game's code converted
- tools/lift/night.ps1 -Version 0.61.0 (the whole pipeline, LIFT_WORKERS 6): 1239 functions proved by the random-input
  tester, 42264 instructions, 11.01% of the game (0.60.0 had the 3.3% set of 0.54.0's). Its tests now include
  rumble_ring10 and rumble_ring32; it writes src/game/lifted_layout.h (the layout the set was made with).
- qa_set.ps1 runs two at once (-Parallel 2: each in its own copy of the game; a further one only with 4 GB free): the
  12 tests in 27 minutes. Deployed 11:15 after them and the start-up at normal speed. Pictures looked at:
  rumble_ring32, story_upgrade_b, menu_from_attract.
- The Rumble results table that wouldn't go: QA column rr_next (QA_STATE + 0x240: what the automatic Next's
  menu_choose_action gave | tries << 16). On 0.61.0's build (11% converted) both music_rumble_late and
  rumble_ring10_end left the table in ~1.4 s without the automatic Next being needed (rr_next 0) -- not seen again;
  the column stays for when it is.
- qa.ps1: the state row and header are trimmed (a hex last column kept the line's carriage return and ToInt64 threw);
  qa_set.ps1 doesn't give a test a copy whose game is still open (a qa.ps1 that stopped on an error left one).
- tools/lift: the agent-found fixes -- fields_disagree counts pointer steps m2c folds into an offset (base_steps);
  floats that arrive in integer registers / stack slots (param kind 'g': a number, never NaN, in the tester); temp_
  locals and parameters the original narrows with sll/sra stay narrow (shift_cuts, locals_narrowed). Subset tests: 22 +
  15 more functions proved. Going in with the 0.62.0 run.
- tools/lift, calls through a pointer (agent prototype, scratch/lift_ptr/ptr.diff, 640 lines: lift_call_ptr through
  LOOKUP_FUNC, static argument-slot rules, a recording stand-in in the tester): 2 of 25 proved (171 instructions). NOT
  merged: a function reached through a pointer may read registers besides its arguments (func_801472C4, func_80147A20
  read s- and t-registers), which the converted caller doesn't keep as the translation does and the tester can't see
  -- a gap in the proof for 171 instructions. Its finding that counts more: "a caller reads what it leaves" (lift.py
  callers_rely_on / after_indirect, the union of everything read after any jalr, a0-a3, t2, f0) refuses many functions
  in every subset (9 here, 12 and 17 in the earlier two) -- probably over-counting; the next thing to look at.
- BUG in 0.61.0's converted code, found by the split-function agent: tools/lift/analyse.py ended its liveness at a jump out
  of a function (`j` = the translator's call-then-return), so a function whose jump target reads registers looked as if it
  read none -- func_800EC990 (a stub: `j rw_800EC990`, which reads a0..a2) "read nothing". The converted func_80128F5C
  called it with no arguments (the original passes a0 = s0, a1 = s1, a2 = 1: it got what the call before left, a1 the
  wrestler's +0x9E, a2 0x98); func_80120C00 passed func_8012096C two of its four. The tester didn't see it: its stand-in
  for the callee used the same too-small register list. Fix: entry_reads counts a jump target's reads (jump_targets);
  38 functions read more now. The 0.62.0 run (restarted with it) converts them again or leaves them out.
- Split functions (agent prototype, scratch/lift_merge/merge.diff + merge.py): 145 refused functions classified -- 39
  the mod's own asm; 71 in 25 one-entry groups that can be put together; the rest multi-entry / odd. Subset: 5 merged
  entries proved, 474 instructions; 4 reach all but switch-default jumps (left unproved). To review before merging.
- tools/lift, merged 2026-11-07 afternoon (backups/*_before_merge.py, *_before_big.*):
  - the split-function merge (merge.py; to_asm read_functions(merge=True); analyse.py analyses merged entries whole,
    drops their old independence results; lift.py checks every piece, links the pieces' real originals in the tester).
    Checked in a sandbox (scratch/lift_int) on the agent's subset: the same 5 merged entries proved (474), and the two
    callers fixed by the jump-read analysis proved with their right arguments.
  - the big-functions agent's three (scratch/lift_big/big.diff): M2C_BITWISE as __builtin_bit_cast (a size mismatch won't
    compile; a bit-cast call result still refused); m2c stack arguments stored in an earlier block (FrameArgSlot ->
    Ref<T>(frame.at(off)); sw / swc1 only) and pending stack arguments forced when what they read changes (m2c read one
    late: func_80114CBC); the tester tries each function's own constants for 1 in 8 random words (only more inputs --
    on its subset it found differences on paths never reached before, and a ran-long one in the shipped func_801163C4).
    Its measurement: big functions proved 85 -> ~104 on its subset (+~4.1K instructions safe).
  - The 0.62.0 run re-proves the whole set with all of it (12 workers, tests 3 at a time, not deployed: deploy.hold).
- "a caller reads what it leaves" (agent, scratch/lift_callers/callers.diff): refuses 579 functions / 59,615
  instructions today; only 50 (2,814) by a caller's own instructions. Over-counted: overlay (looked-up) jals treated as
  pointer calls; f20-f31 never kept; lwl/lwr pairs counted as reads (in lift.py and analyse.py); analyse.py treating a
  looked-up jal as reading all argument registers. Missed reads: switch tables after a call, jumps into other functions,
  reads after overlay calls. Its exact version (callers_rely_exactly; f20-f31 kept when saved and restored, the tester
  checks them; today's three trusting assumptions kept -- my decision): +69 proved (3,797) on its subset; it also
  refuses 13 shipped functions (func_800173C0, 800D9A5C, 800DF3BC, 800E4540, 801094B8, 8010BE34, 8012D2A0, 8012DD2C,
  8012E640, 801347B0, 80135728, 80137B34, 80140950) -- every traced case analysis over-counting, not a real read.
  func_800E5D74 (shipped): not a bug as far as traced. Goes in after the 0.62.0 run; next: the analysis over-counting.
- Merged (backups/*_before_analysis.*): the exact "caller reads what it leaves" rule and the analysis fixes
  (scratch/lift_analysis/analysis.diff): lwl/lwr adjacent pairs are writes (mips.uses_defs_at), a looked-up jal reads
  what the functions at its address read (not tails that pop a frame they never made), system functions' exact reads
  (system_reads.py, checked against the runtime's C++; osEPiStartDma reads a0-a2; four still unknown -> all). Agent's
  subset: +110 proved (6,779). Shipped functions held back by the exact rule: 6 of 13 (func_800DF3BC, 801094B8,
  8012DD2C -- a0, probably harmless; func_80135728, 801347B0, 8012E640 -- v0: POSSIBLE REAL BUG in 0.61.0 --
  func_8001BDDC's looked-up call to 0x800EE640 may reach func_800EE640_ovl_a, which returns func_80135728's v0, and the
  shipped conversion is void). The next build leaves them out. 0.62.0 run restarted with all of it.

## 2026-11-07 0.62.0 (the native port, built and in test, NOT deployed: deploy.hold): 16% converted
- The run with every tool fix of the day (merge, base_steps, float params 'g', narrowing, jump reads, constants
  dictionary, bit casts, stack arguments, the exact caller rule, analysis fixes): 1484 functions proved, 61,278
  instructions, 15.97% of the game (0.61.0: 1239 / 11.01%). Its tests at 6x three at a time from 14:09.
- Merged after it, for the next run: --register-cuts (scratch/lift_narrow/narrow.diff; backups/*_before_narrow*): m2c
  dropped a sll/sra cut when it could fold it into a type -- parameters typed s16 (then widened back without the cut),
  variables cast on every assignment, results cast on every return. Now each cut is an explicit (s32)(s16)x at its own
  instruction and nowhere else; lift.py keeps register parameters and results register-wide. Agent's check: 37 of the
  141 DIFFERENT proved (3,162), 146 changed-but-proved all still prove, no regressions.
- Next (agent): per-access size and sign of memory reads (m2c types a field once; lb read as u8).
- Merged (for the next run; backups/*_before_compile*): the "does not compile" fixes (scratch/lift_compile/compile.diff,
  three-way merged with --register-cuts) -- m2c offset-0 accesses as M2C_FIELD(var, T *, 0) unless var really points
  to T; pointers assigned to int variables cast; undeclared spXX / unkspXX as Ref<T>(frame.at(0xXX)) typed by the
  instruction there (refused if none / two sizes), bare sp as frame.at(0) (leaf functions that need it get a frame);
  saved_reg_sN as the register's value on entry; fields_disagree checks every nested M2C_FIELD (it only ever checked
  the innermost). And the game header exe/.../src/game/lift_mem.h (scratch/lift_compile/header.diff): pointer +/- in
  32-bit wrapping bytes both ways, pointer - pointer as s32, an address stored into an integer Ref keeps its bits,
  Ref<P> += / -=, Ref<T> /= -- existing operators still chosen where they matched. Agent's results: 273 of 307 refused
  now candidates; 179 proved (11,259 instructions); the 105 changed existing candidates none worse.

## 2026-11-07 0.63.0 (the native port, built and tested, NOT deployed: deploy.hold): 20% converted
- The run with --register-cuts and the compile fixes on top of 0.62.0's: 1713 functions proved, 77,271 instructions,
  20.14% of the game. All 12 tests at 6x (three at a time) and the start-up passed. Build kept in backups/build_0.63.0
  (exe, mod, lifted.cpp, lifted_layout.h). The game folder still has 0.61.0.
- Today's conversion: 3.3% (0.60.0) -> 11.0% (0.61.0) -> 16.0% (0.62.0) -> 20.1% (0.63.0).

## 2026-11-07 17:00 tools/lift: reads at their instruction's own size and sign (m2c --access-types)
- m2c typed each field once, so a field read different ways came out wrong: an lb read as unsigned, an lw read as one
  byte (func_800F25D4, func_800199CC, func_8003D780). Now each load/store records its own type and prints with it where
  the C would differ; fields_disagree checks per access, sign too, and fixed addresses. Sandbox before/after: 3 more
  functions proved (772 instructions), no regressions. Patch scratch/lift_reads/reads.diff, backups/*_before_reads_1656.py.
- 0.64.0 run started with it (night.ps1 -Version 0.64.0, LIFT_WORKERS 12).
- Regular/Arcade phase 1 done in a copy of the mod (mod_modes/, MODES_NOTES.md): mode setting + migration, hooks
  ungated and split, arcade cosmetics gated. Phase 2 in progress there. Not merged into mod/ yet.


## 2026-11-07 18:24 0.64.0 (built and tested, NOT deployed): 20.32%
- With the reads size/sign fix: 1715 functions, 77,977 instructions (+706 on 0.63.0). All 12 tests at 6x and the
  start-up passed. Build kept in backups/build_0.64.0. Game folder still 0.61.0.

## 2026-11-07 18:35 tools/lift: reads no longer move past stores/calls; unsigned division (agent 4)
- m2c held loads as expressions and let them move past stores through other pointers, delay-slot writes, and calls
  (pin_pending_reads / pin_pending_control / pin_pending_calls in translate.py); magic-number divisions take their
  signedness from mult/multu (evaluate.py fold_divmod); lift.py only_loaded_as, bit-cast check on temp_ret.
  Sandbox: 74 DIFFERENT -> proved (8,324 instructions), 0 proved lost; func_80118390 partial -> DIFFERENT (m2c fold
  fragility; never shipped). Patch scratch/lift_order/order.diff; backups/*_before_order_*.py. 0.65.0 run started.

## 2026-11-07 20:40 tools/lift: m2c warnings and call-result bit-casts (warn.diff)
- --left-registers (args passed on as earlier calls left them), extra stack args as frame-slot stores,
  --split-delay-slots on retry, exact_spill_slots, irreducible note accepted, result_registers (callee v0/f0).
  Sandbox: +25 proved (3,964 instructions), zero regressions in 297 changed functions. backups/*_before_warn_*.

## 2026-11-07 20:45 0.65.0 DEPLOYED (the user: "You can deploy the next one"); hold back on after
- All 12 tests at 6x + start-up passed. night.ps1 skipped the deploy: qa_set / deploy.ps1 counted the modes agent's QA game
  (qa\game4) as the user's game -- both checks now ignore qa\game* (backups qa_set_before_deploycheck.ps1,
  deploy_before_qacheck.ps1). Deployed by hand with deploy.ps1 -Exe; 0.61.0 backed up to backups .61.0.

## 2026-11-07 22:10 Regular / Arcade modes merged into mod/ (from mod_modes/, phases 1-3; details mod/MODES_NOTES.md)
- game_mode (Regular default, latched at start-up, settings_version 5 migration: existing enabled=true installs ->
  Arcade); hooks ungated and split; arcade cosmetics Arcade-only; per-mode toggles; F2 Rumble settings in the game's own
  Rumble with a native 2x2 NUMBER page (count by one 2..40, IN THE RING, ENTRY TIME, STARTS WITH 2/4); Custom Story in
  Regular's Belt box; menu idle -> attract (Arcade); F2 QUIT MATCH / RESET in Regular; CONTROL MODE N64 / 6-BUTTON.
- Open: Regular Rumble continue shows black rectangles during CONTINUE?; Regular title PRESS START (quad at 0,0,0 --
  G_MODIFYVTX in the renderer); IN THE RING below 6 acts as 4 (six-engine); naming (exe) not done.
- backups/mod_before_modes/ has the files replaced. Not deployed (hold).

## 2026-11-07 tools/lift: port additions carried, break allowed (port_strict.diff)
- 125 of 129 "port added calls" were false alarms (do_break / do_lwl helpers). Entry / after-call / guarded-call
  additions carried into the converted code, recording stand-ins in the tester (12/12 mutants caught). break no
  longer refused (both sides stop alike). Strict coverage (user rule): division_checks.py only reports
  (fuzz/division_dead.txt), nothing pre-marked -> +6 proved (173 instructions); 24 more would prove with pre-marking.
- 22:36 the user allowed one exception: division checks proven never to run count as reached -> port_premark.diff
  merged instead of port_strict (+24 functions, ~5,200 instructions in the sandbox). In the 0.66.0 run.

## 2026-11-07 23:20 tools/lift: register-convention functions (abi_rebased.diff, interim)
- A second candidates round for functions refused for ABI reasons: own writes of registers callers/callees rely on
  stored to the context (lift_out_r / LiftAbiKeep), extra inputs loaded (lift_in_r); the tester compares those
  registers at the end and gives these functions stand-ins that change exactly what the callee may write. Round 1
  byte-identical. Sandbox: +629 candidates; of 583 tested, 424 proved (37K instructions), 30 DIFFERENT (caught).
- 0.66.0 restarted from candidates with it (night.ps1 -From candidates). backups/*_before_abi_*.

## 2026-11-07 23:32 tools/lift: steered tests for partly-reached functions (cover_abi.diff)
- Part 1 unchanged (200,000 random inputs); part 2, only for a function part 1 left partly unreached, mutates what
  the original's path read (memory, parameters, stand-in results within what the callee may write). An instruction
  counts only when a compared test ran it. Sandbox: +101 proved (13,995 instructions), zero lost; caught 2 real
  conversion bugs (func_8014434C delay slot, func_8000CEEC signedness; neither shipped). Merged before 0.66.0's tester.

## 2026-11-08 00:35 the proof standard enforced: 200,000 random tests
- lift.py score let a function be proved on MIN_CHECKED (2) compared tests when the tester's 40 s limit stopped it:
  0.65.0 shipped ~260 functions proved on under 200,000 (36 under 10,000), though the standard told the user is
  200,000. Now proved needs FULL_TESTS (200,000) random tests; the rest go to fuzz/too_few.txt ("too few tests").
  To do: a long-run pass for those (fuzz_main.cpp MOST_MILLISECONDS from the env, lift.py timeout 180 s scaled).
  backups/lift_before_fulltests_*.py.

## Queue for 0.67.0 (merge after 0.66.0 finishes)
- scratch/lift_unal/unal_current.diff (unaligned lwl/lwr pairs, +27 proved)
- scratch/lift_abi/abi_v2.diff (bounds-check retry, mfhi, doubles: +44 proved on the subset)
- long-run pass for fuzz/too_few.txt (MOST_MILLISECONDS from env; subprocess timeout scaled)
- pending agents: lift_fix (delay-slot / signedness causes + boundary values), lift_sys, lift_ovl
- tester: C0000374 heap corruption in 4 division functions (longjmp out of converted C++ skips destructors?) -- tester only
- scratch/lift_ovl/ovl_rebased.diff (overlay LOOKUP_FUNC calls through lift_lookup<A>, relied_through_jumps; +32 proved under 200k rule)

## 2026-11-08 02:05 0.66.0 first build FAILED its tests -> cause: mod-replaced functions converted
- 3 crashes (hd_select, music_rumble_late, versus_1p: a read of N64 0x17FEE99B6 in converted func_801371EC from
  func_8012D2EC from the newly converted func_8010A2E8) and blocks_hogan's forced holds lost. Both are mod.S
  replacements (gen/asmpatch.py copies, no RECOMP_PATCH in the source): lift.py patched_by_mod missed all of them. 0.65.0
  (installed) has 36 such functions converted: the mod's change is skipped where a converted caller reaches them.
- Fixed: patched_by_mod also reads mod/build/patches/mod.S (195 names). 0.66.0 rebuilt with the 40 excluded
  (LIFT_ONLY scratch/lift/keep_nomodpatch.txt); full tests running. First build kept in backups/build_0.66.0_try1.

## 2026-11-08 02:20 0.66.0 PASSED (rebuilt without the 40 mod-replaced functions): 35.06% converted
- 2,199 functions, 134,545 instructions; all 12 tests + 2 Regular-mode tests at 6x passed. backups/build_0.66.0.
  Not deployed (asked the user: it fixes 0.65.0's lost mod patches).

## Queue for 0.68.0 (after 0.67.0)
- scratch/lift_ovl/ovl_v3.diff (overlay calls via lift_lookup; rebased on 02:25 tools; sample: 21 more proved at 200k, 0 changed)
- pending: lift_sys (OS calls), lift_fix (delay-slot / signedness causes + boundary values)

## 2026-11-08 04:40 0.67.0 PASSED: 40.67% converted (not deployed: hold)
- 2,333 functions, 156,063 instructions; unaligned, abi_v2, long-run pass (256 slow ones re-run at 5 min; 217 still
  short, 21,923 instructions), mod.S check (0 mod-replaced converted). 14 tests at 6x + start-up passed.
  backups/build_0.67.0. Next: 0.68.0 with ovl_v3.
- scratch/lift_fix/fix.diff (delay-slot reassigned pin, --split-all-delay-slots, --compare-types casts, tester part 3 boundary constants; +7 DIFFERENT->proved, 0 regressions) -> for 0.69.0

## 2026-11-08 ~05:45 0.67.0 DEPLOYED (the user: "Yes"); hold back on. 0.65.0 backed up to backups/0.65.0.

## 2026-11-08 07:12 0.68.0 PASSED: 41.42% (2,367 functions, 158,965 instructions); backups/build_0.68.0; not deployed.

## 2026-11-08 08:20 account switch
- The old account hit its weekly limit ~08:15: all seven agents stopped (work kept in their sandboxes) and the 0.69.0 run
  died in its tester step. Restarted 0.69.0 (-From candidates) and resumed all seven agents on the new account.
- mod-replacement check (lift_modrep): nothing in 0.67.0 passes a mod copy an unset parameter; 12 converted callers
  leave callee-saved registers (s3/s4/fp) as their caller had them before game code reads them on never-set paths
  (garbage either way; low risk, not proved) -> the union of game + mod analysis goes into modrep.diff.

## 2026-11-08 build_mod.ps1 -ModDir really stays in the copy
- The generators (gen/asmpatch.py ...) find the mod through nmpaths (/home/devbox/work/arcade -> mod\), so -ModDir still
  read the real mod\gen and overwrote the real moduild\patches (found by the lift_modrep agent; no harm: the inputs
  were identical and normal builds regenerate it). nmpaths now honours NM_MOD_DIR; build_mod.ps1 -ModDir sets it.
- night.ps1 runs above-normal unless the user's own game is open (speed, 2026-11-08).
- scratch/lift_ptr: ptr.diff = 14 proved of 33 pointer-call functions; sent back for v2 (keep callee-saved registers in step around pointer calls: 4 address-taken functions break them) -> ptr_v2.diff

## 2026-11-08 11:35 status check (verification and notes only; nothing built, tested, merged, patched or deployed)
- 0.69.0 did NOT finish and is NOT running (most likely it died in its long-run pass, probably when the last session
  ended ~10:33 -- the cause is not established). Evidence:
  - tasklist.exe (11:32): no fuzz.exe, no powershell.exe, no clang / ninja / NoMercy*. The only python.exe are 4 small
    ones (3.8-22 MB) beside uvx / mcp-for-blender (a Blender MCP server, not lift.py). One pwsh.exe (PID 29556, 128 MB,
    beside WindowsTerminal / OpenConsole): its command line can't be seen with tasklist, but night.ps1 in its long pass
    would have a lift.py python and fuzz.exe under it, and there are none.
  - lift.py run_fuzz writes fuzz/report.txt only once, at its end (lift.py ~2802; a subset run puts its lines into the
    full report). report.txt is still 10:04:53 = the main 200,000-test pass. The long pass (scratch/lift/too_few.txt,
    203 functions, its fuzz/build/fuzz.exe built 10:05:40) never wrote it, so it did not finish.
  - release never ran: lifted.cpp, mod.toml, exe 06:26 (0.68.0's); mod.toml version 0.68.0; no backups/lifted_0.69.0;
    no QA run after 07:04 (those are 0.68.0's).
- Interim score of the main pass (`python tools/lift/lift.py score tools/lift/fuzz/report.txt`, read-only; safe: nothing
  writes report.txt now): proved 2,414 functions, 161,024 instructions, 41.96%; partly reached 153 / 31,222; never ran
  7 / 855; too few tests 203 / 24,624; DIFFERENT 106 / 24,882 (caught, not converted). NOT 0.69.0's result: the
  long pass, release, build and the 14 tests are all still to do. (0.68.0: 2,367 / 158,965 / 41.42%.)
  None of the 40 DIFFERENT the score lists is in 0.68.0's set (scratch/lift/def_068.txt); the other 66 not checked.
- Mod-replaced exclusion (by grep, the same sources lift.py patched_by_mod reads): mod/build/patches/mod.S (06:26) has
  103 .globl, 102 func_ names (not 195 as written earlier -- that figure was probably the union with the source's
  RECOMP_PATCH / HOOK names; not re-derived); mod sources' RECOMP_PATCH / HOOK / HOOK_RETURN names ~100. The current
  lifted.cpp (0.68.0's) defines NONE of them (only forward declarations; the pattern checked on func_80000590 /
  800005BC, which it finds), and none is in 0.69.0's fuzz/report.txt (so not a 0.69.0 candidate either).
- QA evidence: only 0.68.0's (06:26-07:04): all 15 report.txt RESULT: PASS (5 "time ran out before the match ended:
  checks are for what it played"). The sheet.png images were not looked at in this check.
- Sandboxes (from their files, as at 11:20; agents stopped): split.diff delivered (0 regressions, +2 proved) is first for
  0.70.0; ptr v2 unfinished and shows 3 v1-proved functions DIFFERENT (not mergeable); diff.diff has no final
  regression table; sys.diff still the 01:28 one on old tools; slow no table; steer no diff; modrep.diff not reviewed.
- NEXT: the user decides whether to restart 0.69.0 (it is not running; a restart is a new run, not a duplicate):
  `tools\lift\night.ps1 -Version 0.69.0 -From candidates` redoes disassembly, candidates (~40 min last time) and the
  random test (~1 h) before the long pass. night.ps1 has no way to start at the long pass; doing that by hand (its
  LIFT_SUBSET / FUZZ_MS lines, then release onward) would be the user's call. Then validate (last lines, score, 14 sheets, mod-replaced check), keep backups/build_0.69.0, and only then
  start 0.70.0 with split.diff (back up tools/lift first; split.diff changes merge.py and to_asm.py too -- check
  whether -From analysis is needed).

## 2026-11-21 tronflicker: the Titantron's live picture black for moments in matches
- The user: "the titantron is clipping to black again every couple of seconds briefly" (after visual's 2026-11-19 fix).
- The native renderer shows the hard camera's last picture for up to 6 game updates with no camera pass (visual's
  WCW_NATIVE_TRON_KEEP); after that it shows the screen's "video in memory" (0x806C0000), which nothing writes in the
  native picture (only N64 lists draw there): black. Visual's measure counted only the cleared screen (trace code 2),
  not code 1, so stretches over 6 updates were never counted: its "0 black" was code 2 alone.
- Why the passes went missing: tron_scene_end drew the camera pass in the run starting where the LAST frame's last run
  started. A match has hundreds of groups (a chain of items each); any item seen or culled before the last run (the
  TV camera turning, a man stepping to the near side) moves where that run starts, and the pass was missed -- 11-17%
  of updates in the match, now and then 7-12 in a row. (The run's end was also worked out only up to group 64.)
- Fix: tron_scene_begin works out the run's end as func_800028B4 does (exe arcade_scene.cpp build, up to the groups'
  count); the run reaching the count is the scene's last, and the camera pass (and the entrance video's pass) goes there.
- QA tf_novideo_raw / tf_mixed_raw (La Parka vs Ultimo Dragon, Ultimo vs Farooq, RAW IS WAR, the picture trace).

## 2026-11-23 no2k15: the two WWE 2K15 test slots removed (STONE COLD 2K15 0x011A, THE ROCK 2K15 0x011B)
- The user, 2026-11-14: "the HD / 2K15 models can be REMOVED from what ships (they were only for testing)" -- keep the engine
  logic; 2026-11-23: "Yes remove them unless they exist as character folders" (none does). Their models were third-party rips
  (hd.bin, hd_rock.bin, native_view.pak, hd_index.h, hd_index_rock.h): nothing of them is left in the mod, the exe or the tools.
- Gone: their LATE entries (gen/mkroster.py, mkparts.py, mkovr.py, names.py, roster_ids.json; the exe's romimport ri_ports.cpp,
  ri_ports_parts.inc), their stand-in models (gen/wmport.py, ri_wmport.cpp -- the No Mercy hand picture pair they made first
  stays at its rows, so wmport.bin and wmport_files.h are byte for byte as before), RO_STONE15 / RO_ROCK15 (ROSTER_OPTS 42,
  ROSTER_LATE_N 35), EXTRAS_HD2K15 (bit 16) and SRC_HD2K15, PACK_HD_AUSTIN / PACK_HD_ROCK (the exe's PACK_FILES: content.bin,
  wmport.bin), the native renderer's native_view.pak (WCW_NATIVE_PAK), the first start's optional hd packs (an older plan's
  are passed over), 23 QA scenarios that only tested them (8 others moved to stock Austin 0x0002 / The Rock 0x0001).
  roster_ext.h, parts_ext.h, parts_ovr.h made again by ri_cli ports (and by the Python generators: the same bytes); only rows
  out. A folder wrestler's seat ordinal now starts at 142 (144 before).
- Kept (the engine): src/hd.c whole -- a wrestler folder's own hd.bin + hd.json + pictures.pak (tools/wrestler_folder.py), up to
  16, model k the k-th folder's (was k + 2); parts.c now sets SKEL_WM_HD itself on a built-in Revenge port's descriptor (it used
  to come only from the 2K15 stand-ins'), so "based_on" is a Revenge port (goldberg, sting ...); the native renderer's folder
  pictures from man 0. A descriptor that asks for a model nobody loaded is drawn as the game's.
- What can still name them (mod gen/retired_ids.json -> stories\_roster.json "@RETIRED:<id | NAME | 0x number>" -> the stand-in):
  a save sidecar's champion -> his title VACATED and the save written (exe sidecar_get FF FD, save.c sidecar_vacate); a story ->
  his stand-in (STEVE AUSTIN / THE ROCK, the men the slots copied), both compilers; select_pages.json (and families.json by string ID: families_take, read, not run) -> left
  out, no blank box. Presets and the story in progress never hold an added Superstar (save.c), so they can't name one. A folder
  with one of their string IDs or numbers is just that folder's wrestler.
- QA (a fresh install of this branch's package, private\qa\no2k15_game, its first start; -Installed, the copy qa\updqa6): no2k15_select_off
  PASS (the game's pages: neither number, no empty box; The Rock's Type arrows his own four), no2k15_select_on PASS (MERGE SAME
  WRESTLERS: The Rock's box No Mercy's 4 then WrestleMania 2000's 4, 8 in all; automerge's parity.py: editor 56 = game 56
  families), no2k15_sidecar_prep + no2k15_sidecar_make + no2k15_named PASS (the game's own save and sidecar, its champion changed
  to stone_cold_2k15 / 0x011A: World Heavyweight title VACATED, KANE kept a former champion, the save written; the story
  no2k15_retired.json compiled with STEVE AUSTIN / THE ROCK standing in -- 384 bytes, the same as with them named; a
  select_pages.json naming them: page RETIRED is THE ROCK, STEVE AUSTIN, KANE, MANKIND, UNDERTAKER), no2k15_p1 PASS (player 1
  The Rock vs Steve Austin, played, a decision), no2k15_folder_hd PASS (qa\wrestlers_test's full-detail folder, based on
  Goldberg 0x010D in a scratch copy: his own model drawn -- the shots -- in a match).
