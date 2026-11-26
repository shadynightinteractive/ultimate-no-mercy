# WWF No Mercy (N64) match engine: making it run 6 wrestlers instead of 4

The game's match engine supports at most 4 wrestlers. We are changing it to support 6 (slots 0..5).
Decompiled C (Ghidra, from a RAM image taken during a match) is in /home/devbox/work/ghidra/out/<func>.c,
one file per function. Raw MIPS for any function: `python3 /home/devbox/work/six/mdis.py func_XXXXXXXX`.
Addresses below are N64 virtual addresses. Ghidra writes some addresses as negative ints:
`-0x7fea6670` == 0x80159990 (add 0x100000000).

## Known facts
- Wrestler structs: 4 x 0x358 bytes at 0x80159990 (Ghidra: `&DAT_80159990 + i * 0xd6` as int*, or
  `(&DAT_80159a30)[i * 0x1ac]` as short*, or `iVar * 0x358 + -0x7fea....`). A wrestler's slot index is
  0..3. Struct +0x00,+0x04,+0x08,+0x0C (ints) are slot indexes of other wrestlers (opponent/partner links).
  +0xA0 (short) = -1 when the slot is empty. +0x9E (short) is the wrestler's MODEL index (see below).
- 0x80159080: the slot currently being processed (set by func_800E5890(slot)); 0x80159084..0x80159090 the
  linked slots; 0x801590ac.. pointers to those wrestlers' structs.
- Per-slot control input: 4 x 0x38 at 0x80159540. Per-slot CPU AI state: 4 x 0xb0 at 0x801596C0
  (0x8015A6F0 holds the pointer to the current one). 0x8016b9d0: int per slot = controller port of that slot.
- Controller pads (N64 ports, always 4): 0x8009eda4.. stride 0xC (held/pressed/released), 0x80063350 stride 6.
  Loops over 4 CONTROLLER PORTS are NOT wrestler loops and stay 4.
- Model instances (in the main segment, indexed by the model index from wrestler +0x9E): e.g. bones
  0x800A11D0 stride 0xB24, 0x800A4460 stride 0x5B0, 0x8008ABD4 stride 0x270, 0x80069F20 stride 0xC0.
  Whether there are exactly 4 model instances (one per wrestler) or more is still to be established.
- Match results written for the menus: 0x80097FB8, 8 bytes per slot.
- Bit masks with one bit per slot exist (e.g. 0xf = all four slots), and tables in data indexed by slot
  (e.g. 0x8014DB60, 4 u32 masks).

## What a "slot" loop looks like
`do { ... iVar += 0x358; i++; } while (i < 4);`, `for (i = 0; i < 4; i++) f(&DAT_80159990 + i*0xd6)`, loops
over the wrestler array, over the AI/control arrays, over slot bitmasks (`>> i & 1` with mask 0xf), loops
over model instances of wrestlers. Not slot loops: controller ports, 4 corners/turnbuckles of the ring,
4 vertices/sides, matrix 4x4 rows, sound channels, etc. When unsure, say so and why.

## Six-man build: where it stands (2026-10-20, later)
- build_rom.py: step_arrays (ovl_e arrays), step_main (main arrays, all sections), step_bss, step_heap, and
  SIX_LOOPS=gp -> step_loops_gp (173 loop bounds become `slt/sltu rt, rs, $gp` etc.; $gp = wrestler count),
  step_stubs (STUBS: two-instruction sites -> `jal rw_X` with the first in the delay slot; CALLS: a jal retargeted),
  step_masks (MASK_IMMS: the consumers of rewritten masks), step_rewrites (rw/r_XXXXXXXX.c: `j rw_X; nop` at the
  game function). The ROM's blob is saved next to it (ROM.blob.bin/.syms); oracle3/exp6 inject it at 0x80780000.
- $gp is the MAIN THREAD's: rw_run_overlay (func_800007AC's `jal 0x800D9960`, every overlay's entry) sets 4, or
  rw_match_count for ovl_e (ROM 0x144BE0); no other thread runs a slot loop (static call graph from each thread
  entry: none reaches a gp loop). Context restore is stock again (no self-modifying code: the PC port can't have it).
- Masks: camera mask func_80116130 has the six layout for any count (slots 0x3F, out-of-play 0xFC0 with the slots
  past the count set, flags 0x1000/0x2000); func_800EE130's two masks at bits 6-11 / 0-5.
- Engine-owned blocks: spec.EXTRA_OVLE (X_<name> in rw sources), e.g. namebuf (HUD per-slot name textures).
- Oracle: oracle3.py. Compares only where both main threads wait for a frame (osRecvMesg on 0x80060B20 or
  0x80063368) the same number of times (orc/tramp.S counts them; O3_SYNC=1 default) -- else loading shows as
  differences. Differences must persist O3_PERSIST (3) compared frames. PTRW: a word that keeps bytes of a moved
  pointer after a partial overwrite is explained. O3_KEEP=1 carries on after differences.
- Debug: dbg/threads.py ROM STATE [frames] -- thread states (state 1 = stopped: a crash, with cause/badvaddr and
  REGS=1 for the registers); HOT=1 hottest pcs; WHO=lo,hi [WHO_PC, WHO_COND=reg=hexval] who accesses a range, the
  registers at WHO_PC (only when reg holds the value: core retro_trace_cond). Needs CORE_B=1 O2_CORE=cached_interpreter.
- Results: oracle layout with all arrays moved = IDENTICAL for a 16:24 Rumble; full layout (SIX_HEAPGROW=0) =
  IDENTICAL through the menus and (synced) 400 match frames from the pair state.
- The arcade mod's refs into moved arrays: kb/mod_port_todo.txt (32).
- Next: the result mask 0x800A10EC (bits 0-3 winners, 4-7 losers, 0x100.. flags; func_80118A20/func_80119C28/
  func_800DBA88 and the 0x8014637C/0x80146384 tables), HUD positions (func_800E0A0C), func_80117438 stack lists,
  func_80120D98, func_800E2344, func_800F2BBC, func_80018258, func_800E0304, func_800EC940.

## 2026-10-21: slots 4/5 made real (six_full76 / six_gps12)
- Menus: rw/r_menu.c rw_rumble_fill at func_8013486C's end (rw_rumble_epi): with rw_rumble_ring 6 and >= 6 entrants,
  entrants 4/5 -> slots 4/5 (id, costume, pad 0, level) and rw_match_count = 6; rw_run_overlay reads it ONCE
  (resets to 4). oracle3 sets rw_rumble_ring = 4 (O3_RING).
- Tables (rw/r_tables.c, build_rom.SLOT_TABLES via Moves): 0x80146440/636C/6450 (+E9 flags), 0x80146558 (start
  positions 0xC000: 4/5 at mid sides), 0x80146350 (camera slot field), 0x8014DB60/70/78 (replay bits: slots 4/5 =
  0x200000/0x400000 eligible, 0x800000/0x1000000 snap), 0x801465C8 (0x8016B650 lock: models 4/5 = 0x1000/0x2000;
  xori 0x300F at 0x8011D8B8, andi 0xCFF0 at 0x80115350). rw_opp (func_80115000 first target, SEQ).
- spec: pads 0x8009EDA0 (6 ports; 4/5 none), CPU recent actions 0x8014521C (ovl_e .data -> BSS).
- Rewrites: r_801308FC (replay rings n), r_8013092C (replay recorder, n slots, snap bits by n), stub rw_snap_v0.
- Contact ids: `< 4` -> `< 100` (WRESTLER_ID_TESTS); rw_contact_cap (func_800EBD2C list <= 10).
- SEQs: 0x800DDB70/0x80115B88 (4 -> gp), levels count-down pointer (func_80113E30), func_8011723C threshold,
  camera focus bound 0x800E4B88.
- Known open: rumble entrant into slot 4 stalled (was the 0x801465C8 lock table read past its end -> fixed, to
  verify); func_80119328 per-opponent stats count only opponents 0..3; 3v3 tag not designed.
- dbg/rumble6.py ROM STATE frames: per-slot entrance fields when they change.
- Oracle gps12 (4-man Rumble from boot, every change through r_8013092C): IDENTICAL for 33296 match frames, then one
  word 0x80048508 (func_80017C64's music fade counter) one step apart: it is reset to 0xF while func_8002782C (audio
  thread: stream still playing) says so -> set by the async audio state, a timing artifact, not match logic.
## 2026-10-21: the PC build with the six-man engine
- port/mkport.py: six ROM + blob at ROM 0x1FF0000, [[section]] "rwblob" (vram 0x80780000) for syms/dump.toml.
- /home/devbox/src/NoMercySix: recompile tree (N64Recomp + the fixups); WWFNoMercyRecomp branch six-engine has its
  RecompiledFuncs (stock kept in RecompiledFuncs.stock), main.cpp six_engine_init (on_init_callback: blob words into
  RDRAM, ovl_e BSS end 0x80052E2C -> 0x80176000), six_blob.h. build-mingw: `ninja NoMercyRecompiled` -> "NoMercy Arcade.exe".
- PC-port constraints on the blob: N64Recomp keeps the FP condition per function (no c.cond across a stub: the
  contact cap sets f0 = f2 instead) and `jr` only through ra (the Rumble fill is a tail call `j rw_rumble_fill`).
- Arcade mod: src/six_addrs.h (port/gen_arcade_six.py), NM_SIX build `SIX=1 sh build_mod.sh` -> build/mod_six;
  the arcade Rumble loops use nm_slots(); slots 4/5 start empty like 2/3. asmpatch copies come from the six ROM
  (func_80114FBC, func_8011A250 differ).
- Deployed for testing: <outside> (a copy of <outside> with the six exe + mod).
## 2026-10-21 (later): PC crash at match start / entrance camera (six_full79)
- Cause: the runtime live-recompiles mods' hooked functions from its ROM and DMAs overlays from it -> stock code/data
  mixed with the six-man engine. main.cpp six_engine_init now patches the runtime ROM (six_rompatch.h, 7661 words)
  and the boot image already in RDRAM (ROM 0x1000..0x101000 -> 0x80000400). Checked: stock + patch == port ROM.
- port/regen.sh SIX_ROM: port ROM, dump.toml rwblob, N64Recomp + post-regen pipeline, headers, exe (one command).
  audit_coresidency's 89 "violations" are all rwblob calls into ovl_e (only run with ovl_e loaded): expected.
- Fixes: ovl_a func_800EE69C biased base 0x800AA070 (= team conds - 8) -> SEQ hi_arr/lo_arr with bias;
  ovl_c func_8010A19C reads ovl_e's 0x8016B920 after the match -> moved place; slots 4/5 results in RES6
  (X_result6, ovl_e BSS) -- 0x1000000/0x2000000 in 0x800A10EC are ovl_c func_8013843C's flags; rw_rumble_fill only
  with MENU_FLAGS & 0x200 and context 1 (Multi Play) / 7 (arcade one pick); human_bit for slots 4/5 = 0.
- exp6 EXP_OLD=path: access trace of every moved array's OLD place during the match. Rumble 9000 frames: only
  func_800F0D04/func_800F6154 (3-byte timers 0x80145210 + 3*slot, slots 4/5 run into the moved 0x8014521C's old
  place) and func_8010EFB0/func_8010FF78 (per-slot words 0x801590D0 + 4*slot, slots 4/5 into 0x801590E0's old
  place). Both work (old places free) but TODO: move both arrays properly (spec) + oracle.
- TODO: slot 4/5 result for ovl_c readers (title winner func_8010B05C, records) -> RES6 in EXTRA_MAIN; results
  screen 4 columns; func_80135EE8 CPU bits slots 0-3.
- Deployed six_full79 build to <outside> (other mods there: aki_camera/cheats/quickmatch; quickmatch hooks
  func_80113E30 = match init, which explains "crash when a match starts" with stock-ROM live recompiles).
## 2026-10-21 night: six_full82 / six_gps15
- spec OVLE += 0x80145210 (CPU timers x3), 0x801590D0 (per-slot word), 0x801452C4 / 0x801452CC (CPU per-model
  last action / same-action frames; slots 4/5 overran into each other and the level table 0x801452D4).
- rw/r_800EDE48.c: ovl_a's challenge check read the stock hit-stat matrices (0x8009FDF8, no longer written) -> STAT
  (X_statmat stays in ovl_e's grown BSS after the match: nothing else uses 0x8016E000..0x80176000 with the heap moved).
- Tools: dbg/slotscan.py (whole-RAM access trace: pc -> addresses, per current slot) + dbg/slotscan_cmp.py (4-man vs
  6-man of the same ROM, exp6 EXP_RING=4/6 EXP_SAVE): equal-step accesses 4 long -> 6 long that land outside the
  engine's per-slot places. KNOWN = triaged fine. dbg/restrace.py: who reads what on the results table.
- Results table (scr 28, ovl_c): column objects type 0x73xx (func_8011FD1C, idx & 3) from 0x800A3DC0 records;
  objects not built from ovl_c code constants (likely a layout asset) -> 6 columns is UI asset work: TODO.
- Multi Play Rumble has no title at stake (0x800AA084 moved = -1), so func_8010B05C's slot 4/5 gap doesn't arise.
## 2026-10-21 morning: six_full83 -- the late-Rumble freeze
- Symptom: after all entrants are in, eliminated wrestlers stood idle (stock: they brawl at ringside), then the last
  two idled too (clock running). Cause: state 1's handler func_800E5EB8 retargets (func_800EC484) whenever the
  computer presses 1 (func_800F559C); func_800F0EF0 presses it until +0x08 is the brawl partner it wants, and
  "nearest but the current" only swaps between the two nearest -- with 5 others the wanted one can be 3rd+.
  rw/r_800EC484.c: for n > 4 it goes round all of them in distance order (n <= 4 unchanged).
- Verified: a six-man Rumble (8 entrants) from the start to the results table and back to the menus (ROM 83,
  dynarec, presses every 240 frames): match over at 99300 frames.
- Open: results table (scr 28, 4 columns from ovl_c page records 0x8014D3F8.. type 0x7300..0x7303, x -42/6/54/102,
  stride 0x16) shows slots 0..3 only -- a slot 4/5 winner isn't on it.
- Debug helpers this session: /tmp/devbox-0/odis.sh (ovl_e disassembly from a ROM), who82c/who08 (slog the store
  that changes a field), dbg/rumble6.py R6_SAVE=%d (numbered states).
## 2026-10-21: six_full85 -- Rumble results table
- rw/r_results.c: rw_colrec (4 x 0x1C, word-aligned: +0x18 is read with lw) = the records the table shows; after a
  six-man Rumble (rw_last_count, set in rw_run_overlay at ovl_e entry; MENU_FLAGS & 0x200): the winner, then the
  latest elimination times (0x8009FE58) among slots with a wrestler; else records 0..3. Filled at the end of
  rw_80118A20 (each result) and again in func_80138D10 (STUBS2 rw_colrec_fill_s). Readers redirected (SEQS):
  func_8011FD1C, func_80138D10, func_801401BC; func_8010AED8 calls -> rw_col_result (slot the column shows, RES6).
- Verified in the emulator from r6_84_75000 (new blob code injected): Rock (winner), Tazz, Austin, Al; Next -> menus.
- PC: the exe re-installs assets/mods/*.nrm over mods/ at every launch -> the six mod must be in BOTH (<outside> too; it had the stock arcade mod). main.cpp now load_overlays(0x1FF0000, 0x80780000, blob) so live-recompiled hooks find blob functions (crash: Failed to find function at 0x80783AF4 = rw_slt_v0_a0_k358).
## 2026-10-21: six_full86 + arcade mod -- HUD, get-ups, Rumble continue, Start refill
- HUD (rw/r_hud.c): corners 4/5 = top / bottom centre (left panels moved +144 in the 480x240 HUD). The entrant count
  (rw/r_800E0D20.c: 3 digit objects A_80157F60, x 243-14k, y 18) and clock (rw/r_800DFED0.c: 5 objects 0x801532E0,
  x table 0x80144B54, y 208) go to the strip above the top row when nslots() > 4 (y 0, count units at 203, clock +29).
- Arcade (mod, src/gameplay.c): RECOMP_PATCH func_8013FEF4 (state 0x4f's lying anim: 0x2f79 + level by +0xBE / +0xAA;
  levels 2-3 are the slow get-ups): FAST GET UP keeps level 0 unless an opponent with +0xEE & 6 (SPECIAL) targets him.
- Rumble continue (logic.c): RECOMP_PATCH func_8011723C (eliminate slot -> match over?) counts the held player slot
  as in; the player stays (no rr_leave); continue -> rr_back_in: +0x94 &= ~0x33, elim time -1, +0xE9 |= 0x10, full
  power; count-out rule 0x400 (0x800A9950; state 1 handler sets +0x94 |= 0x10 for anyone standing outside) off till
  he's standing in the ring; outside the AI drives him (CPU flag) -> state 0x9b (roll in under the ropes).
- Start with a credit (not free play) in a match: full_power (spirit 100 + SPECIAL, +0xAA/+0xAC 255, body parts
  +0x2C4.. 50.0, +0xBE 0); the controller's Start is held back (no pause menu).
- Tests (stock rig): /tmp/devbox-0/t/{mkrr2,cont_test,gu_test,start_test}.py. States carry their blob: remake
  rr_start (mkrr2.py) after every test-blob build.
- No pause in arcade mode: the pad hook holds back Start on the active players' ports whenever a match runs (match_running); Start stays the arcade's (refill / continue / join). Mod e1bd4799.
## 2026-10-21 afternoon: operator menu, controllers, wrestler options (mod 0.22.0, exe b87)
- Operator menu (logic.c menu_*): Terrordrome's layout (pages, 640x480 units, Press Start 2P = src/arcade_font.h).
  draw_menu_frame(): flush_menu replaces the task's DL (OSTask data_ptr/size) with the menu alone -> no flicker on
  the title (which makes more than one picture a frame). Menu DLs at 0x807C4000 (2 x 0xD000).
- Exe src/main/arcade_pads.cpp: gamecontrollerdb.txt + controller_map.txt layouts; exports recomp_arcade_pad_name /
  _buttons (bit0 BACK = coin) / _learn_start / _learn_read / _learn_apply (15 x 16 bytes: dpup dpdown dpleft dpright
  a x leftstick lefttrigger righttrigger y b leftshoulder rightshoulder start back) / _pad_swap / _quit.
  recompinput players::swap_first_two (submodule fork).
- Roster: ROSTER_OPTS 7 (big_show, nmplus_media, funaki..thrasher, benoit); ext 4 = Benoit (ID 0x205, ordinal 106)
  from bigshow.c's kept copies; ROSTER_HIDE_BENOIT removes his box (kept for the save); story remap by mode with a
  checksum of the tables (fresh load vs ours). Rumble field: no women (0x702.., 0x8xx), 0x903, 0x904.
- Start with a credit in a match = full power; Start never reaches the game in a match (no pause).
- settings_version 2 migration: titantron off, ext toggles from new_superstars, benoit off.

## Attract text flicker (paused 2026-10-21, not yet implemented)
- Cause: draw_flush appends our text AFTER the game's G_RDPFULLSYNC (E9, written by func_80002DE4). RT64 State::fullSync
  closes/presents the workload there; our commands land in the next workload -> alternate frames. Emulator shows no flicker.
- Plan: RECOMP_HOOK func_80002BB4 (a0=picture, fb=RD32(a0)) + RECOMP_HOOK/HOOK_RETURN func_800028B4 (a0 = &gfx ptr on
  FA4's stack): at return write DE00 call to our list at *a0, *a0 += 8 (before DE4's E9/DF). Don't hook FA4 (rt camera hand edit).
  draw_flush (non-menu) just publishes items; emission moves to the scene-end hook; always SETCIMG fb (same-address no-op in RT64).
- Rig: patch jal 0x80002FC8 (0x0C000AED) and jal 0x80002FF4 (0x0C000A2D) to C wrappers calling the originals.
- DONE b88 (mod 0.22.1, sha 49ebbdcf): implemented as planned (draw_frame_begin / draw_scene_end; draw_flush publishes to shown[]).
  Rig: our DE call sits right before E9/DF in both frame lists; text in every shown frame. Deployed to both mod folders.
  Note: device_commit_files with a reused stagedPath delivered a STALE file once -- use a fresh staged filename and verify sha.
- DONE b88 (mod 0.22.1, sha 49ebbdcf): implemented as planned (draw_frame_begin / draw_scene_end; draw_flush publishes to shown[]).
  Rig: our DE call sits right before E9/DF in both frame lists; text in every shown frame. Deployed to both mod folders.
  Note: device_commit_files with a reused stagedPath delivered a STALE file once -- use a fresh staged filename and verify sha.
- b89 (mod 0.23.0, sha efa2b95d): coin sound (sound #149 default, operator COIN SOUND 0-4 = {-,149,132,156,142};
  func_800173C0 ids < 0x401 are raw indices, one global set: same in menus and matches); Rumble rules preset hook
  (func_80134140 return: rows 2 COUNT OUT=Lose, 3 PIN, 4 SUB, 6 ROPE BREAK = No; row bits PIN 0x20 SUB 0x10 TKO 1
  ROPEBRK 2 DQ 0x2000 BLOOD 0x800; arcade RR rules 0x40C00); unlock_all (0x800BD910 25 bytes + save image +0x6B67);
  Rumble AI (gameplay.c arcade_rumble_ai, hook func_800F03F0 return; AI rec *0x8015A6F0 +0xC held +0xE pressed):
  worn-down (spirit <= 45 + 2/s since last out, cap 90) victim at the ropes in a lock-up / reeling 0x35,0x2d /
  whipped 0x0b,0x0c -> facing squared (+0x10C s32, atan2(dx,dz) 4096) + func_801202F4 test + func_800EB6D4(v,0x70);
  apron landers re-0x70'd on getting up. Rig all-CPU 8-man Rumble: 8 elims in ~3 min (stock AI: 1 in 5 min).
  Rig tools: /tmp/devbox-0/t/rr_metric.py (cpu0), mkrr2.py must be re-run after every test-blob build.
- b90 (0.23.1, sha fbba9d79): user reported Rumble crashes + "whipped and phased outside onto the announce table".
  Cause (most likely): b89's whip (C-down injection) + forcing 0x70 on a man in the whip's paired states 0x0b/0x0c
  pulled both men out. Removed both; assist now only state 0x35 (alone); rr_over checks +0x348 body ptr; save-image
  unlock write only in menus; operator ROYAL RUMBLE > CPU GOES FOR ELIMINATIONS (rumble_ai). Six-man rig now exists:
  /tmp/devbox-0/t/rig6.py (six_full86 + its rw blob + test blob built by /tmp/devbox-0/t/build_test6.sh -DNM_SIX),
  mkrr6.py -> states/rr6_start.st, rr6_run.py N [human]. Two 6-man Rumbles ran clean (all-CPU finished, 7 elims).
- b91 (mod 0.23.2 sha cd6410ed, exe sha 0bb1aa35, exe+map kept in /home/devbox/work/builds/exe_b91 for symbolizing):
  "no Continue, game ended and froze": in a 5-6 man match the six engine builds frames in rw_dltab (0x8000 each), so
  draw_scene_end's room check (stock 0x8005D8E0 + 0x2EE0) skipped our drawing -> CONTINUE?/GAME OVER invisible. Fixed
  (SIX_DLTAB / SIX_CUR_COUNT in six_addrs.h via gen_arcade_six.py). Verified in rig6 dbl_elim.py: CONTINUE? shows.
  Crash/freeze reports: exe writes <outside>{crash|freeze}_<date>_<time>.txt (+ _rdram.bin:
  8 MB game RAM then 4 MB mod memory from 0x81000000); NoMercyRecompiled.prev.log keeps the last session's log.
  Mod flight recorder at 0x807F2000 ('ARCL', state u32s, event count at +0x7C, 64 x 16-byte events from +0x80;
  codes FL_* in arcade.h). Fetch reports via device_stage_files and symbolize offsets against the kept .map.
- b92 (mod 0.23.3, sha ac40f6e4): user: AI too aggressive; apron men knocked out by kicks from afar. Removed every
  forced elimination (rr_over / apron re-0x70 / lock-up toss / stagger assist / victim facing). AI is input-only now:
  focus target (+0x08) = lowest spirit; a man in DANGER (+0xEE bit 0: set by func_8011D05C when spirit hits 0, 300
  frames) -> at the ropes run+B, on the apron tie-up through the ropes + A; holds released (subs off). 6-man rig
  all-CPU 6 min: 1 elimination (the game's own), DANGER reached once.
- b93 (mod 0.24.0, sha1 2128e7a3): JEFF JARRETT (ext 5 = EXT_N + 1, ordinal 107, ID 0x30B, option "jarrett" / RO_JARRETT,
  mask ROSTER_EXT_JARRETT 0x40). Moveset: WM2000's (gen/wm2k.py runs WM2000's own code on gen_data/wm2k_ram.bin:
  0x80004D0C unpack, 0x800178FC model builder; move names: rec file 0x94 [m/10] u16 -> name tables 0x95-0x97; NM:
  0x69 -> 0x6A-0x6E), mkroster.port_wm_moveset (slots same except WM 86/90/94/98 -> NM 86/88/90/92; abilities WM 50
  -> NM 55: 0-30 same, 35-49 -> 39,40,42-47,49-54), Billy Gunn (ord 12) reference/taunts. Looks: No Mercy Plus V3's
  Jarrett parts (face 132 -> our 0x82, lower 143/151 -> our 130/131, files 0x5128-0x5133 -> 0x4CB4-0x4CBF), on
  Billy Gunn's build/hair (mkparts.JARRETT_APP; costumes 16-19). Theme: WM2000 music bank (*0x800571EC) sound 23,
  sample ROM 0x17A2970, in content pack as song 36 (slot 14) only while he's on (media.c CT_SONG_JARRETT). Photo
  101 (jarrett_photo.h, from WM tron frame 0x33F; user will source a better one). Rig: states/jj_*.st,
  /tmp/devbox-0/jj/full.py (select S6 box 5 -> match), theme.py (audio + dbg_theme_reads). WM2000 meshes/textures
  straight into NM's descriptor hang the game (not pursued: NM+ parts used instead).
- b94 (mod 0.24.0, sha1 1773d201): Jarrett's moves by ANIMATION (gen/wmanim.py): move m's anims = s16 [m+k] of
  0x1F0 (NM, anim file = v + 0x3322; anims are files 0x2DD9-0x4473, headers table *0x8006A220, attrs file 0x2BF
  0x24 each) / 0x3AE (WM, v + 0x19DB; files 0x1609-0x2847, attrs file 0x435). NM kept WM's anim files byte for
  byte (Stroke = NM 1480 "Front Russian Sweep", his taunts = NM Taunt 001..162): 97 anim, 26 name, 17 ref/stand-in.
  Hair: NM "Short 1-A" (hair 2, back 0); hair values >= 0x1D blank the model (NM+ only).
- b95 (mod 0.24.0, sha1 371a8274, deployed both mod folders): Jarrett's HEAD from WM2000 ("exact" hair). WM2000 makes each head piece's picture as a 256-colour
  CI8 + 512-byte TLUT at run time (palette groups: 0 skin/face, 2 hair colour, 4-5 moustache); his FACE picture is
  composited from features (file 0x114F is a generic base face!). Select screen keeps 4 models; part structs 0x34
  each from entry 0x08 (pic+hdr at +0xC, TLUT at +0x10); copies at 0x8009C054/BB40/B62C/B118 -- his is the one with
  hair colours in the face TLUT group 2 (0x8009B118 in ram_c0). NOTE: WM slot 0x20 is UNDER THE CHIN, 0x28 back of
  head, 0xA0 face wraps the head (hair drawn on it). gen/wmcapture.py -> head.pkl {slot: (pic, tlut)};
  gen/mkwmhead.py -> parts_wm.h: 9 files (3 meshes, 3 pics, 3 TLUTs; empty TLUT groups filled with group 0 so the
  game's skin-group copy keeps his colours); parts.c parts_model_post swaps them in and zeroes 0x218/0x228/0x24C.
  Probe trick: paint a candidate TLUT green in the live WM rig to see which piece it colours.
- b96 (mod 0.25.0, sha1 ffd17913, deployed both mod folders): DROZ (ext 6, 0x509, NM+ ord 75), MIDEON (ext 7, 0x50A,
  NM+ 79), THE BLUE MEANIE (ext 8, 0x60B, WM ord 41). roster_ext.h ext_late[] (Jarrett..Meanie), mask bits
  ROSTER_EXT_JARRETT << k, RO_DROZ/MIDEON/MEANIE, options droz/mideon/meanie. Costumes 16.. 4 each (mkparts
  JARRETT_APP / OVR_APP / MEANIE_APP via app(**fields): VPW Studio's appearance field names). gen/mkovr.py ->
  parts_ovr.h: Droz/Mideon whole NM+ models (NM+ files + neighbours -1..+3 served from 0x4D00), Jarrett/Meanie
  WM2000 head pieces 0x28/0xA0 only (game neck 0x20 kept; pic+pal served as pic,pal pairs; pal groups filled with
  group 0 where empty/unused). parts_model_post picks costume by photo + >=34 fields. Body types cross-ref: WM
  body size (looks byte) == NM body type 0..7 (Jarrett 3 Medium 2, Meanie 7 Fat 2). Moves: Droz/Mideon NM+ with
  WM2000 fill (port_moveset wm=); Meanie port_wm_moveset ref Rikishi. Themes: songs 36/29/65/51 (slots 14/7/43/29)
  CT_SONG_LATE0.. ; videos 45/47 forced on for Droz/Mideon. Photos 102-104.
  LESSONS: WM2000 bodies on NM skeletons look wrong (NM rebuilt body meshes; mesh header byte 3 = skin-joins to
  neighbour bones differ; 128-wide pics and CI4 body pics hang/blank in NM). Use NM body + WM head instead.
  NM colours: 0 item's own (boots: silver), 1 black, 2 white, 3 red, 4 blue. VPW Studio repo (AKI-Club/VPWStudio)
  FileTableDB names every file; NM CAW item lists files 0x4A56-0x4A66. NM+ V3-B5 ROM = V3 + move-data changes only.
- b97 (mod 0.26.0, sha1 d47e5bc7, deployed both): + NM+ Pete Gas/Rodney/Joey Abs (80-82), Undertaker 1999 (83), Raven
  (84), William Regal (86), Tiger Ali Singh (89) as ext_late 4..10 (whole NM+ models via mkovr OVR2, photos 105-111
  from NM+ files 0x4C21+p: gen/mkphotos.py -> photos_nm.h). Jarrett & Meanie default OFF (SETTINGS_VERSION 3 turns
  them off on existing machines). DYNAMIC THEMES: func_8000AAF4(id,c) (entrance song lookup; callers ovl_e/b/c)
  replaced by roster.c roster_entrance_song: ext_late_theme[k]>=0 -> media_entrance_theme(k) sets dyn_cur, swaps
  song 53 (slot 31, sound 30) script to CT_SONG_DYN0+k, returns 53. Only free sound-based slot left was 31.
  Entrance attire: game clears looks field 0x09 for select/match; ovr models built twice (with entr / 0), ovr_entr[]
  selects. Videos >= 44 always wanted (NM+-only). NM+ ordinal table: 0x80401010 + 0x50*o; +0x10 name index into
  file 0002 strings. NM+ V3 01042026 zip patch == our nmplus.z64 byte for byte.

## b98 (0.27.0)
- Extended music slots 58-62 (posse, taker99, raven, regal, tiger) with own songs 80-84 / sounds 200-204 (media.c media_play_sound builds ext_bank). Verified normal rig (Raven 202, Tiger 204) and six rig (Regal 203, fullw6.py).
- Six rig: test blob bss moved to 0x807A0000 (test/test6.ld); rig6.py clears it.
- checkparts part 3 "fails" for costumes 16-59: all overlay-replaced (ovr_costume), so stale by design; parts 1-2 pass.
- Deployed sha1 70a5410e1dec1dd06bd6355d18ae05a53fcf529a (both mods folders).

## b99 (mod 0.28.0, exe f74f2964): JR & King commentary
- Source: WWE '13 Wii (<outside> 13\*.rvz). /tmp/devbox-0/wwe13: tools/rvz.py (RVZ reader, ctypes libzstd),
  akpk.py (Wwise pck), dlg.py (DialogueEvent trees: tree = section's LAST uTreeDataSize bytes; nodes >IIHH), lang.py
  (sound->parent chain -> dialogue leaf) -> wem_tags.pkl. wwiser patched (v65 ranseq extra u16). BRAND AEM = JR&King;
  '*' brand = shared. whisper.cpp small.en q5_1 (model in extracted\), tools/tx.py (per-clip -ac by length).
- gen/mkcommentary.py -> build/commentary.pak (NMCP v2, parts .pak.N <28MB). EVMAP: WWE13 event -> director event.
- Exe: src/main/commentary/{director,arcade_commentary}.cpp + stb_vorbis; mixes in queue_samples (ducks game 0.72).
  Exports recomp_arcade_comm_{event,tick,config,reset,available}. Mod MUST ship with this exe (imports).
- Mod: src/commentary.c (polls: +0x14 state, +0xAA hp drop = hit, +0xD6 move id (/10 = names.pkl move), +0xEE 4 SPECIAL
  1 DANGER, +0x95 &0x10 outside &1 eliminated, 0x80159204 pinned slot, 0x80153A84 s16 count shown, 0x801594D0 u16
  result up, 0x800A10EC winners, 0x80158E90 HUD msg (0x801448BC rope break?)); gen/mkcommmoves.py -> comm_moves.h.
- Rig: /tmp/devbox-0/cm/live.py (boot->arcade->match, both CPU, reads dbg_comm_log), sim (director on an event log).
- b100/b101 (exe 628ef17e, mod d2740a03, pack 2 parts <=20MB: device_commit 28MB failed HTTP 400): current move is
  +0x30A (the last move started; damage lands as it ends; trust it < 300 frames old), NOT +0xD6 (strike AI pick).
  Match-type words filtered (TYPE_WORDS) except where allowed (TYPE_OK); Rumble entrant number passed (arg) and
  matched to "#n" lines; lines naming a move only for that move (MOVEKEYS + QUALIFIERS: "german suplex");
  victim-named move lines ("v<id>") for b. Rig rumble: /tmp/devbox-0/cm/rrlive.py.
- Full pack (all 9718 AEM+shared clips transcribed): 4933 lines / 3939 clips, 3 parts (sha a169d8f1/65a936df/ca221519)
  deployed with mod 5da8c880 (b103: taunt on move change, TOP_ROPE/AERIAL_MISS, finisher set-up) + exe 628ef17e.
  Filters added: PPV/show names, sign-offs "from", WWE13-only finisher names (NOT_HERE), proper_noun() for generic
  move lines, finisher/special generic whitelist, match_start <=16 words & no match-type talk.

## b104 (0.29.0): Legends of WrestleMania lines + run-of-match events
- LoWM (PS3) SOUND/COMMEN_A.YAF -> /tmp/devbox-0/lowm (mp3/NNNN.mp3 + ID3 titles in titles.pkl, w16 transcripts).
  Titles give category + speaker + legend: LOW09_<JR|King>_<Cat>_<Name>_NNN; 09_/08_ = SvR leftovers (09_LOW_* = SvR09 JR&King
  banter, 09_JR_FIN_<Name> etc. = modern roster). mkcommentary.py lowm_kind() maps them; TMAP = title names in No Mercy.
- King's per-legend categories (ChainRev/ChainStrt/Momentum/Winner/Eliminated, JR Winner/Out/Enter) kept only when R-named
  or name-less ("He reversed it!"); capitalised words outside VOCAB (lowercase words seen >=3x in all transcripts) = garbled
  legend names -> line dropped. CAS (create-a-superstar) lines dropped except Finisher_CAS w/o nicknames.
- LoWM TopRope/TopRpeRslt = all misses (aerial_miss). JR_Out = eliminated. Enter/WrstEnter = tag-ins. Bio = in-match colour
  (dominate / danger / comeback by text). ChainStart/ChainStrt = going for it (special).
- Encoded with af.txt EQ, -ar 20000; level -17.4 vs WWE '13 -17.2 dB.
- Pack v3: index columns + src (0/1) + speaker (J/K/-) before text. Director src mask: exe talk arg bit 0x10 = include LoWM.
  Mod: comm_lines (config "commentary_lines"), menu COMMENTARY LINES WWE 13 / WWE 13+LEGENDS.
- "+key" / "+" move keys = repeat lines ("another suplex"): director repeat_ = same move by same man within 30 s.
- Mod now fires EV_DOMINATE / EV_COMEBACK (dealt/took per slot, fading 15/16 per second by VI; dominate dealt>=30 took<=4,
  every 15 s at most; comeback = 3 hits within 20 s of being beaten (took>=30 dealt<=4)) after the hit's own line, and
  EV_TIRED (6 min in, every 3 min, when quiet). Hit events' arg low byte = damage (logs only).
- Mod frames_in runs ~140/s in the rig (not 60): use NM_VI_COUNT for real time.
- Unfired events still: lockup, weapon, weapon_hit, blood, tag, table, cage, ladder, escape, save, run_in, dq.

## b105: Rumble entrant numbers / walk-outs, arena lines
- Rumble entrant: as his countdown starts the slot's id (TB+0xA0) changes to a stand-in (rr_model: 1 for a never-used
  slot), TB+0xF8 (W+0xF0) = his list index, TB+0xD8 (W+0xD0) = the entrant's id; the real id loads ~15 s later (walk-out).
  Number = list index + 1 + rr_num_offset (logic.c: after a challenge's rebuild, next_at_capture - (1 + rr_kept)).
  Announced once per list index (rr_said); EV_ENTRANCE (named) when the id becomes TB+0xD8.
  (Old counting at frames_in == 400 over-counted: 3rd called 5th.)
- MATCH_START / SIGN_OFF arg = NM_VENUE + 1 | 0x80 Rumble. Director place_ + "@place" keys (PLACE_LINES in
  mkcommentary.py: hand-picked clips): @rrmatch, @rumble (Rumble in the RR arena = venue 6), @raw 0, @smackdown 2,
  @kotr 3, @summerslam 4, @survivor 5, @wrestlemania 7. No lines for No Mercy (1) or Heat.

## b106: the booth (JR / King pairing)
- WWE '13 speakers by voice: /tmp/devbox-0/spk (feat.py: MFCC mean/std + delta std + long-term mel shape; logistic
  regression trained on Legends' LOW09_JR_/LOW09_King_ clips, 99% held-out by category). WWE '13 BRAND '*' (shared)
  clips = King (99%), AEM = mostly JR (~95%). pking_plain.pkl: P(King) per wem id; pack speaker K >0.8, J <0.2.
- Speaker column lower case = said to the other man (vocative JR/Ross / King, or a "did you / can you ... ?" question).
- Director follow_: after a J/K line for an answerable event, tick() may play the other man's line for the same
  moment (same a/b/move) right after it ends (<1.5 s): J->K 30% x talk scale; addressed lines 75% either way (fallback:
  his filler); depth 1 (2 if the answer addresses back). Pending events cancel it.
- Legends numbered clips (1xxxxxxxa JR / 2xxxxxxxa King; digits 2-5 = moment) partly used: NUMKIND.
- EV_TIRED: once, 5:00-5:30 into the match when quiet; only "five minutes have elapsed" (WWE '13) + Legends fatigue lines.

## b107: clips were cut at their first pause (fixed)
- mkcommentary TRIM used silenceremove stop_periods=1 -> ffmpeg stops at the FIRST 0.3 s silence: ~20% of clips truncated
  (Big Show 9.5 s line -> 1.9 s). Now: leading silence off, areverse, leading silence off, areverse. Pack 72.8 MB, 4 parts.
- Voice cloning test: <outside> (RUN VOICE TEST.bat: uv -> py3.11 venv, torch 2.5.1 cu124, f5-tts;
  gen.py reads lines.json, refs = JR WWE '13 clips 24 kHz; out\<id>_<ref>_<take>.wav, log.txt). Container/VM can't run it
  (no pip in container, HF blocked, no GPU); the user runs the .bat.
- device_commit_files can write a STALE copy of a staged file that was edited after it was first written under
  /mnt/user-data/outputs (seen with voicelab gen.py/.bat): commit a freshly-named copy and verify md5 on the device.
- JR fine-tune (F5-TTS, user's <spec>): 30-min set = 420 WWE '13 JR clips (metadata30.csv, events entrance/finisher/
  match_start/win/kickout/big moves; pk<0.02, 2.5-10 s). prep30.py needs ArrowWriter.finalize() (else raw.arrow empty).
  Batch 1600 frames, accum 1, lr 2e-5, 30 epochs = 3810 updates at ~4.3 updates/s (~20 min incl. 3.4 GB checkpoint saves).
  The 5.3 h set with 2000-frame batches x2 accum ran 3-8 s/update (VRAM spill). Ckpts .venv\Lib\ckpts\jr30\.
  TEST TRAINED VOICE.bat -> gen_ft30.py -> out_ft\.

## b108 (0.30.0): COMMENTARY operator page
- Operator menu: new page COMMENTARY (PG_COMM, 2nd on the main page): COMMENTARY (JR & KING), VOLUME, AMOUNT,
  LINES FROM (comm_lines 0 WWE 13 / 1 BOTH / 2 LEGENDS; config "commentary_lines"), AI VOICE LINES (comm_ai,
  "commentary_ai", default on). Rows left GAME SETTINGS.
- talk arg bits: &3 amount, 0x10 Legends, 0x20 not WWE '13, 0x40 AI. Exe -> director src mask bit0 WWE13 / bit1 Legends /
  bit2 AI (pack src 2 = AI-made lines, none yet).
- F5 fine-tune on Windows: Trainer.train num_workers=16 -> a worker died (RAM); ft_launch.py patches it to 4.
- Name splicing in progress: /tmp/devbox-0/names (whisper word timestamps -ml 1 -sow -ojf on the JR clips naming
  someone or with by/from/of).
- Legends-only JR fine-tune (jrlowm): 1266 clips / 72 min (metadata_lowm.csv), 16 epochs = 4656 updates ~25 min,
  ckpts .venv\Lib\ckpts\jrlowm; TRAIN LEGENDS JR VOICE.bat = prep + train + gen_lowm (lines_lowm.json, refs ref3 =
  Legends JR "here comes" clips 4461/4495/4557) -> out_lowm.
- Director splices (b109, not deployed yet): EV_NAME clips (pack event "name", wrestler = id) per wrestler; lead-in
  clips with wrestler "*" (a) / "v*" (victim) = -2: picked in the named pass only if that wrestler has a name clip;
  chain() = the name to append; exe start() appends it after CHAIN_GAP_MS (60 ms); started() adds its length.

## b110 (0.31.0): spliced calls from the user's QA'd Keep folder
- REVIEW\Keep (PC voicelab) -> /tmp/devbox-0/keep/Keep: 125 JR names (35 wrestlers), 64 lead-ins, 13 Rumble lines. gen/mkkeep.py
  -> keep.json (kind name/lead/elim, game 0 WWE13 / 1 Legends from the source clip, heat = mean z of pitch, loudness,
  brightness within each game; leads 0.4 whole + 0.6 last second, db). Pack v4: 11th column "heat:db" (lead/name).
- Director: splice path in pick() (SPLICE_CHANCE 0.25, not for King's answers): lead-ins only there; name_for(w, lead):
  same src, |heat diff| <= 0.5, each distinct text equally likely, nearest take; chain_gain() = lead db - name db (+-6).
  Chained lines run at prio >= 7 (only live calls cut in). CHAIN_GAP_MS 40, CHAIN_FADE_MS 20 (exe fades the lead's end).
- Exe config talk bit 0x80 = no splices; mod comm_splice / "commentary_splice" (COMMENTARY page "NEW SPLICED COMMENTARY").
- Rumble generic eliminations: "And the champ is out!" / Legends "Bam, bam" removed; ELIM_LINES (11 whole lines) + 2 Legends
  King openings ("He's been eliminated", "I don't believe it!").
- AI voice: tabled (user: "not hitting"; AI name cuts were too short). jr120 = 2 h WWE '13 F5 fine-tune (15 epochs).
- whisper.cpp: rebuilt with GGML_NATIVE=OFF + AVX2 (a container move gave "Illegal instruction"); DTW times need -nfa.
- Sim: splices ~1 in 25 lines; off bit -> 0.
