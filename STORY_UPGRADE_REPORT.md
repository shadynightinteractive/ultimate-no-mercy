# Story Mode upgrade: running report

Task: `STORY_UPGRADE_TASK.md`. Agent "stories". Started 2026-11-01 evening (0.38.31 in the tree).
Work folder: `data\storyup\` (drafts, research, test stories). This file is kept current so another agent can pick up.

## Where things stand (newest first)

- 2026-11-01 23:55 (main session; the "stories" agent stopped at 23:11, its run went on alone): the tree is in place, mod + exe built, both
  compilers the same on every story. First real-game run (2026-11-01_231459_story_upgrade_a, older build): phases 1-4 and
  the script's time / end all seen working in the shots (Kane / Big Show stand-ins, voice tokens, lines by player and
  group, ask + store, LOGIC OK 1-4, signoff, his own taunt, ended at 1:00 by pinfall with the game's PIN FALL + replay,
  branch with parens and the stored answer) -- then the game stopped making frames in node 2's first bubble (freeze
  report qa\game\crash_reports\freeze_20261101_232259: thread 46436 in RtlAcquireSRWLockExclusive from the renderer's
  code). Not yet reproduced: to rerun. The rest of the QA waits for the user's own game to close (qa.ps1 won't run beside it).
  New since the agent: qa scenarios story_triggers (phase 9, 010_triggers.json), story_limits (011_limits.json: the 60
  character limit against what fits); gen/storyprompt.py (the prompt, from the compilers' tables; STATUS at its top: only what
  passed QA goes in); stories\screwjob.json (the features sample); the schema's teams / script / commentary; the tron
  fallback's warning in both compilers. AI_STORY_PROMPT.md is still the OLD prompt until the QA is done.
- 2026-11-01 22:40: read-only prep (the "attires" agent still owns the tree; the lead deploys its builds, so nothing
  of mine may go into mod\ or exe\ until the lead says "tree is yours").
  - Drafts in `data\storyup\work\`: `storyc.py`, `arcade_stories.cpp`, `story.c` (copies of the tree's, edited;
    `base\` holds the tree's versions as they were when copied, for a 3-way merge later), `roster_groups.json`,
    `_voices.json`, `tests\*.json`.
  - `data\storyup\work\cmp.sh <stories>`: the new storyc.py and the new arcade_stories.cpp (built alone as
    `story_test.exe`) on each story -- SAME / DIFFERENT -- and the old storyc.py (`base\`): OLD = byte-identical to
    what the old compiler made. `stories.lst` = every existing story (the game's 4, mod\stories, qa\stories_test,
    data\storyscenes tests). Result now: all OLD + SAME; `tests\t_new.json` (recast, or/not/( ), player is / in,
    role is, win by / lose by / ended by, voice tokens, per-player lines, store, scene if on variables, set) SAME.
  - Research agents (read-only, rig only) running: `data\storyup\result\` (how a match ended; mid-match triggers),
    `data\storyup\six\` (six-man story matches; the role limit; stand-in rules), `data\storyup\tron\` (Titantron
    scenes, JR & King cutaways, a Superstar's own taunt in a scene).

- 2026-11-01 23:30: drafts grown (still read-only): story.c (recast, the evaluator with programs / new terms, the
  latch of the last match's result for scene tests, per-player lines, voice-token expansion with a cache, the scene
  hook story_scene_cond for func_8010D1E0, SO_SET / SO_STORE / SO_TRON, the taunt table patch, story_qa for QA's
  state.csv), `_voices.json` (all 97 roster IDs + default; checked by storyc.py check_voices in --header),
  `roster_groups.json`; the tron step and the taunts in both compilers. story.c compiles with the mod's flags (into
  the scratchpad). QA story drafted: `data\storyup\qa\stories_test\005_upgrade_a.json`.
  - Tree changes still to make when it's mine: copy the three drafts in (3-way against `base\`), hooks_recomp.c
    (`RECOMP_PATCH s32 func_8010D1E0(u32 c) { return story_scene_cond(c); }`), arcade.h (story_scene_cond,
    story_tron_video, story_qa), media.c media_video_select (`v == 0x20` and story_tron_video() > 0 -> that video),
    logic.c (story_qa(QA_STATE + 0x220) while qa_story), build_mod.ps1 (_voices.json to build\stories; the both-
    compilers check), mod\stories\_voices.json, mod\gen\roster_groups.json, the exe rebuilt (arcade_stories.cpp).
- Research back: Titantron / JR & King / taunts (the agent couldn't write its own file; its facts are below under
  "Research").

## Research (summaries; details in the agents' folders)

- **Taunts in scenes: PROVED (rig).** The scene animation table (ovl_b 0x8012588C) takes 0..291 as the actor's own
  moveset entry; 227..230 are his four standing taunts, 235 his winner's taunt. Unused entries 18..21 / 24 pointed at
  them: The Rock / Steve Austin played their own taunts (data\storyup\tron\log_t1.json). -> "taunt", "taunt 1".."taunt
  4", "win taunt" (story.c writes the entries while a story runs).
- **Titantron: the game's own Titantron cut, PROVED (rig).** Scene effect 4 plays an entrance video full screen with
  the next speech bubble over it (data\storyup\tron\shots\t2_003/004/006.png); the video is chosen by func_8010C998,
  which is already media.c's media_video_select -> his video by func_8000BBD0(id, costume) (FROM CODE). Showing it on
  the arena's own 3D screen needs a story mode in tron.c (FROM CODE, not done); a live picture of another set is not
  possible. JR and King: drawn in story ring scenes already (PROVED in the real game: QA 2026-11-01_055855_story_scene
  shots 273.1s / 276.1s); no game camera shot frames the table; a look-at camera (announce.c qa_camera_mul) could.
  Corrections: place 7 is the match arena's set itself; place 8 = the ring without the announce table; ops
  0xB0..0xF0 end the script.

- **Six-man (data\storyup\six; the agent's files there: rr6way.py, rr6.log, shots_rr6, storyctx.py, story.log).**
  PROVED (rig): a six-man Rumble with exactly 6 entrants is a six-way elimination (6 HUD panels, 6 -> 5 -> 4; the end
  not seen). PROVED: Story Mode hands the engine a stock 4-man match (menu context 8). FROM CODE: the six engine
  lifts the 4 only through rw_rumble_fill (Rumble flag, >= 6 entrants, ring 6, menu context 1 or 7 -- never Story's
  8); func_800EC354 fills the match's slot arrays for slots 0..3 only (slti 4 at 0x800EC4B8); the story's win / lose
  / title code reads 4 slots. Verdicts: six-way = possible only with engine changes (count 0x807845AC = 6, slots
  4/5 arrays, entrants = 6) -- not done, untested; 3-on-3 tag / elimination, 2-on-3 / 1-on-3 handicap: NOT POSSIBLE
  (the engine's teams are two of two: partner by slot ^ 1, team bits 0x10 << slot, HUD pairs); gauntlet as one
  match: NOT POSSIBLE (no such entry rule). Roles: 8 (0x80097EEC u16[8]; costumes right after, then the player's
  record): NOT raisable in place; part codes 0x85xx..0x8Fxx unused. func_800EC554: role codes taken as they are;
  plain IDs: the first of cand[0..3] not the player / the champion (/ roles 1, 3 in the tag championship), else
  cand[4]; it never checks men already in the match -> story.c resolve_slots does.

- **How a match ends, mid-match triggers (data\storyup\result: draft_match_script.c has every address; exp_*.py the
  rig runs).** PROVED (rig): 0x800A10EC = how | team 0x100/0x200 | winners bits 0-3 | losers 4-7; 0x400 the match
  over, 0x1000 no winner. how: pinfall 0x4000, submission 0x2000, TKO 0x10000, DQ 0x20000, count-out 0x80000 (double
  count-out 0x101400), ladder 0x40000, cage escape 0x2000000, time-limit draw 0x1400 (code), ironman decision
  0x1000000 (code). It stays through the results page. Conditions: health TB+0xAA 0..255 (PROVED), the HUD clock
  0x8016B930 / 0x8016B934 (PROVED), outside TB+0x82 & 4 (PROVED), submission hold TB+0x14 == 73 (code), finisher: +0x30A
  changes with +0x8C bit 0 (code), referee down: NOT POSSIBLE (no referee in a match: the counts are HUD numbers).
  Actions: end with a set result (PROVED: the loser's TB+0x00 = the winner (DQ: +0x6C), his +0x94 |= 0x08 pin / 0x20
  submit / 0x40 TKO / 0x400 DQ / 0x10 count-out -> the game's own replay, title, line, results; calling
  func_80118A20 directly does NOT end the match); No DQ = clear 0x2000 in 0x800A9950 (code), no count-out =
  0x800A9462 = 0 (PROVED); pause 0x80151EDC each frame (PROVED, but nothing to show with it: a bubble would be a 2D
  overlay); a run-in at a chosen moment: the side's word 0x800AA078 + 4 side (six build 0x8051E280): 0x1FF holds,
  0x100 sends him (the trigger PROVED, his walk-in NOT: the rig hung -- its set-up, likely); referee bump / swap: NOT
  POSSIBLE; weapon into the ring: NOT RESOLVED; lights out: NOT FOUND. **Correction:** a run-in part's low byte is
  WHEN he comes (table 0x800F8BA0: 1 0:15, 2 0:30, 3 0:35, 4 0:40, 5 0:45, 6 1:00, 7 1:10, 8 1:30, 9 2:00, 10 3:00),
  the slot is whose side (slot 2 helps the player, slot 3 the opponents) -- so "helps": "player" with one run-in has
  always helped the opponents. Fixed in the compilers (the player's man to slot 2): the user's
  wwf_vs_wcw_invasion.json is the one existing story whose bytes change (its two single "player" run-ins).

## Design (pack format additions; old packs unchanged byte for byte)

- Header pad[1] (offset 0x3A) = the number of extension sections. They follow the arena list, before the strings:
  each `u16 kind, u16 count`, then the records. Kinds: 1 RECAST (u8 role, u8 0, u16 id: a role's candidates in order),
  2 GROUPDEF (u16 first member, u16 count), 3 GROUPMEM (u16 id, u16 0), 4 VARIANT (u16 text, u16 key: an id or
  0x8000|group, u16 string, u16 0), 5 VOICE (u16 id (0 default), u8 field, u8 n, u16 first string, u16 0),
  6 SCENEOP (u8 kind, u8 n, u16 first, u16 a, u16 0: 1 a condition in the conds, 2 sets, 3 store the answer in var a).
- Conditions: with "and" only, the old encoding (game mask + AND list). With or / not / ( ): a postfix program in the
  conds (branch flags & 1). New cond kinds: 5 C_GAME (b = game mask), 6 C_HOW (b = how), 7 C_ROLE_IN (a role, b group),
  0x10 AND, 0x11 OR, 0x12 NOT; 4 C_ROLE_IS (a role, b id) now has syntax. Stack 16 deep.
- Scene ops: op 0x7F with param 0xF000 | index (a cast test the engine never makes: cast 13), handled by a RECOMP_PATCH
  of func_8010D1E0 (story.c story_scene_cond) only while a pack runs; 0x71 then (skip when false), 0x74 else (the
  then's latched answer), 0x70 else-only / set / store (byte1 0: nothing skipped).
- Voice tokens `{who.field}` -> ESC, 0x80|field (|0x40 named), role or id bytes; story.c expands them where the text is
  served (ours()). Fields: nickname, catchphrase (one of the list, stable for the story), signoff, finisher, insult.
- Text limits (enforced by both compilers; every existing story passes): 60 characters a line (a {name} counts 10 --
  the engine prints the short name --, a voice token its field's maximum: nickname 20, catchphrase 40, signoff 2 x 40,
  finisher 24, insult 16), speech bubble 3 lines, narration page 7, requirement 3, question + options 5.

## Phases

Every status below comes from the real game (qa.ps1), with the shots looked at. Runs: `qa\runs\`.

| phase | status | QA run(s) |
|---|---|---|
| 1 recast | done | 2026-11-02_021748_story_upgrade_a (PASS) |
| 2 conditions | done | story_upgrade_a; "lose by countout" only since 2026-11-02_141204_story_triggers |
| 3 voices | done | story_upgrade_a (tokens, lines by player and group, signoff, his own taunt) |
| 4 scene logic | done | story_upgrade_a (if on any condition, store, set) |
| 5 six-man | done as far as the engine goes: 3v3, 2v3, 1v3, gauntlet not possible; six-way untried | research: data\storyup\six; 2026-11-02_061530_story_types |
| 6 match types | done | 2026-11-02_061530_story_types (7 types), 2026-11-02_082510_story_rules_places, story_runins_title (run-ins + title seen in the shots), 2026-11-02_054148_story_retry |
| 7 tron | done | 2026-11-02_134410_story_tron_arenas (video on the arena's own screen in every arena with one, shots looked at), story_upgrade_b |
| 8 commentary | done | 2026-11-02_134410_story_tron_arenas, story_upgrade_b (JR and King close at their table) |
| 9 triggers | done | 2026-11-02_141204_story_triggers (PASS, every ending and condition) |
| 10 tools | done | 2026-11-02_094735_story_broken_listed, 2026-11-02_095328_story_limits; gen/storycmp.py in the build: 27 stories, 0 different; mod\stories\AI_STORY_PROMPT.md generated by gen/storyprompt.py |

Found by the QA and fixed (each in mod\NOTES.md):
- A script's `"how": "countout"` came out a draw: now the game's func_80118A20 with the winner, the loser (story.c story_co_call).
- A Titantron shot played no video in the game's own arenas other than RAW: the screen search used the arcade's arena, not the node's (tron.c story_a).
- SURVIVOR SERIES' close-up framed its disc, which the user wants left as the arena's logo: no screen of its own yet, so the full-screen cut, as SUMMERSLAM.
- Retries, gameover, a branch with no pages, the list's rows, the 48 / 28 text limits: 2026-11-02 notes.

Known limits, in the prompt and the guide: a `fourway`'s "end" only eliminates `"loser"`, an `ironman`'s scores one fall; SmackDown's oval frames slightly large in the close-up; the "hold" trigger needs the CPU to take a hold (one run in three it didn't in 4:00: the time fallback ended it, as written).
One freeze in all these runs: 2026-11-01 23:22, in node 2's first bubble (qa\game\crash_reports\freeze_20261101_232259: thread in RtlAcquireSRWLockExclusive from the renderer). Not seen again, cause not found.

Deployed as 0.38.35 on 2026-11-02 at the user's word (the 13:45 build, the one these runs tested).
