# Two modes (Regular / Arcade) -- phase 1 notes

Working copy: `Development\mod_modes` (a copy of `mod\` as of 2026-11-07 15:30). Plan: `Development\scratch\modes_plan.md`
(sections 0, 2, 5, 6 W1-W4, 12), discovery: `scratch\modes_discovery.md`.

## What changed (phase 1: W1-W4 + QA groundwork)

### W1 -- the mode setting, latch, migration
- `recomp/mod.toml`: new option `game_mode` (Enum "Regular" / "Arcade", default "Regular"); `enabled` renamed "Arcade mode
  (old setting)", read only by the migration; hidden QA options `qa_modes`, `qa_reg`.
- `src/arcade.h` (logic.c section): `enum { MODE_REGULAR, MODE_ARCADE }`, `nm_mode()`, `nm_arcade()`.
- `src/logic.c` "the game's two modes" (around line 346-420):
  - `settings_migrate()` -- the old one-time setting changes (versions 2-4, moved out of `load_settings`) plus version 5:
    sv 0 (new install / never ran the arcade) leaves game_mode alone (Regular by default); sv 1..4 sets
    game_mode = enabled ? Arcade : Regular. Writes settings_version 5. `SETTINGS_VERSION` 4 -> 5.
  - `nm_mode()` -- the one guarded function: first call runs the migration, reads game_mode, latches it
    (`mode_running`, `mode_latched` set last). Every reader goes through it: the frame task, the controller read,
    the overlay hook, `roster_opts_load`, `load_settings`, the hooks in hooks_recomp.c. Two threads racing make the same
    writes from the same reading (harmless).
  - `load_settings()` calls `nm_mode()` first, then reads `mode_next` (F2's GAME MODE, the option as saved). EXIT WITHOUT
    SAVING re-runs `load_settings`, never re-reads the running mode. `save_settings` writes `game_mode = mode_next`.
    RESTORE FACTORY SETTINGS leaves GAME MODE alone (comment at `factory_settings`).
- F2 (`page_items`, `menu_draw`): GAME MODE row on top of GAME SETTINGS (REGULAR / ARCADE); the note "GAME MODE: TAKES
  EFFECT AT THE NEXT START-UP" (brighter, "CHANGED", when it differs from the running mode).

### W2 -- the 22 `enabled()` checks (recomp/hooks_recomp.c)
`enabled()` is gone. Every hook calls through; the decision is inside logic.c / gameplay.c:
| # | hook | now |
|---|---|---|
| 1 | func_800012B0 -> arcade_frame | always; split inside (W3) |
| 2-7 | draw / Titantron / HD / scene hooks | always (universal) |
| 8 | func_80134140 -> arcade_rules_preset_done | always called; returns at once unless Arcade (user's answer 2: per-mode toggle, off in Regular -- the toggle is W9) |
| 9 | func_800F03F0 -> arcade_rumble_ai | always (its F2 option decides) |
| 10 | arcade_roster_service(1) | universal (the roster options in both modes) |
| 11 | arcade_pad_hook | always; split inside (W3) |
| 12 | func_800EF478 -> arcade_demo_setup_done | always (any arena for a demo: Q11 as proposed; the QA bench) |
| 13 | func_800E8A14 -> arcade_story_page | always; returns 0 unless Arcade |
| 14 | func_80117A84 -> arcade_menu_sprites | always; does nothing unless Arcade (Regular's title keeps its PRESS START) |
| 15 | func_80114514 -> arcade_match_step | always: movex chains in both; the extra steps only in Arcade |
| 16 | func_80001580 -> arcade_camera | always; `arcade_entrance_cam()` = Arcade && (PLAY or QA) (answer 2) |
| 17-19, 21-22 | Rumble finalize / setup / frame / HUD | always; bodies already return unless the arcade's Rumble is set up (rr_mode) |
| 20 | func_8013FEF4 -> arcade_down_anim(w, nm_arcade()) | FAST GET UP only in Arcade (answer 2) |

### W3 -- arcade_frame / arcade_pad_hook split (src/logic.c)
- `arcade_frame`: the universal part unchanged (settings, commentary, story runner, announce table, arcctl_frame,
  arenas, qa_state), then `if (!nm_arcade()) { regular_frame(task, dt); return; }`, then the arcade exactly as before.
- `regular_frame` (new): unlock_everything, costume variants at the select, dbg_sel, screen bookkeeping, the QA bench
  (qa_on 1), the Regular QA driver (qa_reg), F2. Nothing of the coin-op.
- Factored out, same order in Arcade: `operator_menu_frame` (F2 key + menu + qa_pic_walk), `screen_watch` (scr_stable).
- `arcade_pad_hook`: universal head unchanged (settings, announcers, gtext, tron, QA pads, sounds, keys, F2's pause);
  then `if (!nm_arcade()) { regular_pads(raw); return; }`; then the arcade's filter as before.
- `regular_pads` (new): every controller as it is; F2 open zeroes them; ARCADE controls on ports 0-1 in a match.
- `pads_out` (factored from the end of the pad hook, both modes): QA / arcade injected presses, write-back, the
  entrance-video skip (FULL SCREEN TITANTRONS off).
- F2 pause (plan risk 6): `menu_open` remembers the pause flag; `menu_close` puts it back in Regular (Arcade still clears).
- F2 pages in Regular leave out: COIN / CREDIT SETTINGS, CONTINUE TIMER, FAST GET UP, ARCADE SPEED, ATTRACT DEMOS,
  QUIT MATCH, RESET TO TITLE (all arcade-routed or arcade-only for now; W8/W9 bring Regular versions).

### W4 -- the arcade's menu cosmetics gated to Arcade
`nm_arcade()` checks at the top of: tabs_without_belt, champ_rumble_slot, champ_survival_defs, champ_custom_defs,
champ_versus_defs, versus_links, exh_rows_fix, exh_ladder_defs, exh_ladder_slot, players_page, players_restore,
header_versus, menu_cam_fix, menu_cam_multiplay, demo_label_off, drive_title, arcade_menu_sprites, arcade_story_page.
`arcade_overlay_loaded` patches the arcade's rows only in Arcade (the latent bug: a disabled install got them).

### QA groundwork
- `qa_modes` (state.csv bs_tries / bs_grabs / bs_step, see qa_state): the running mode, F2's GAME MODE, the option as
  saved, the migration, settings_version found / now, F2 open, phase; the driver's step, F2 page, pause flag; the arcade's
  menu changes as found in the menus' data (list rows, BELT moved, Survival / Custom / Rumble rows, Ladder label).
- `qa_reg` (qa_regular_frame): Regular's menus driven by presses keyed to screen changes (bits 1 title->main menu,
  2 F2 a while, 4 an Exhibition match through the game's own pages, 8 quit).
- With the mod always on, Regular runs `qa_state`: state.csv exists for Regular runs (it didn't with "enabled" off).

## Tests (qa\scenarios\modes_*.json, -ModDir mod_modes -Copy game4)
Regular scenarios set `"settings_version": 5` in their options: qa_tool.py leaves settings_version as the game folder's
(4), and the migration would then turn them into Arcade ("enabled" is on by default).

| scenario | run | result | what the shots / state showed |
|---|---|---|---|
| modes_regular_boot | 2026-11-07_154730 | PASS 7/7 | legal page (014s), THQ / Asmik / AKI / WWF logos, intro movie, the game's title with no INSERT COIN / CREDITS (157s), the Multi Play main menu (180s), F2 GAME SETTINGS with GAME MODE REGULAR, the arcade-only rows gone, TITANTRON LIVE CAMERA OFF as the scenario set it (168s), the "takes effect at the next start-up" note; after EXIT WITHOUT SAVING still Regular. bs_step 0x800A all along: the list's own 10 rows, none of the arcade's changes in the menus' data |
| modes_migrate_new | 2026-11-07_155433 | PASS 2/2 | sv 0 -> Regular, no mode migration, sv 5 written (config after: sv 5, game_mode Regular); shots: logos and the game's own attract, no INSERT COIN |
| modes_migrate_arcade | 2026-11-07_155718 | PASS 2/2 | sv 4 + enabled on -> Arcade (migration 2), config after: game_mode Arcade, sv 5; shots: CREDITS 0 / INSERT COIN over the logos and the attract |
| modes_migrate_off | 2026-11-07_160010 | PASS 2/2 | sv 4 + enabled off (game_mode left Arcade) -> Regular (migration 1), config after: game_mode Regular, sv 5 |
| modes_regular_match (-Speed 6) | 2026-11-07_160233 | PASS 8/8 | the game's own MATCH page (no Ladder row), PLAYER page with all four line-ups (1P vs 2P, 1P vs CPU, CPU vs 1P, Watch), RULES, the select with the HD 2K15 Stone Cold (MORE page), his stage walk with the game's own camera, the match (HD Stone Cold vs The Rock, HUD), the Titantron's videos picked (4, 29), commentary lines played (log), JR and King listed; results Quit / Rematch (screen 29), then the Multi Play menu |

Arcade regression (the scenarios unchanged; qa_tool puts them in Arcade):

| scenario | run | result | shots |
|---|---|---|---|
| menu_from_attract | 2026-11-07_160655 | PASS (no checks) | INSERT COIN / CREDITS 0 over logos, title, a singles demo in a ported arena; coin + Start -> the championship list with Custom Story, Royal Rumble, Survival, Versus at full size (262s) |
| announcers_front | 2026-11-07_161141 | PASS 2/2 | JR and King from the front, the chairs behind them |
| tron_entrances | 2026-11-07_161303 | PASS 2/2 | RAW IS WAR: the low stage-walk camera, the stage wall, the Titantron's picture behind Austin, 2P PRESS START |
| hd_select (-Speed 6) | 2026-11-07_161601 | PASS 3/3 | the arcade's route (list, MATCH, PLAYER passed by itself, ARENA, RULES), the HD 2K15 Stone Cold on the select and in the match |
| versus_1p (-Speed 6) | 2026-11-07_162157 | PASS 7/7 | the Versus row lit on the 11-row list; Versus's MATCH page with its Ladder Match row |
| music_rumble_late (-Speed 6) | 2026-11-07_163059 | PASS 3/3 | the arcade Rumble: list, select, Bret's low-camera walk, eliminations, the results table, then 28 -> 2 -> 22 and the bench's next Rumble -- the same route as the real mod's run 2026-11-07_141815 |
| story_upgrade_b (-Speed 6) | 2026-11-07_163223 | PASS 3/3 | TRON over Vince's video on the big screen, TRON 4 / the table camera with JR and King, the match, END by pinfall |

## Findings
- The arcade's legal-page clearing (intro_service) is unreliable in today's build. The real mod's run by the other job,
  2026-11-07_154023_menu_from_attract, shows the legal page under INSERT COIN, and so do this copy's Arcade runs
  (155718, 160655). The 14:27 real-mod run had it cleared. This was already there before the split; not looked into.
- Regular's title shows no PRESS START, even though the arcade no longer hides sprite 15 there (157s, 155s). Discovery
  saw the same with the mod off. The spec wants the game's own PRESS START, so this is open.
- Regular's Exhibition singles results page is screen 29 (Quit / Rematch). A on Quit -> the Multi Play menu (screen 2).
  RULES' Start went straight to the Superstar Select (past BELT).
- Injecting at the controller read (pads_out / inject_press, into the game's controller 1) drives Regular's title and
  menus on this rig, where the physical pad holds controller 1. No SDL_GAMECONTROLLER_IGNORE_DEVICES was needed.
- QA copies get the game folder's settings_version (4) every run (robocopy, then qa_tool leaves it alone). A Regular
  scenario must set settings_version 5, or the migration makes it Arcade. W14 should make qa_tool write 5.
- Roster options in Regular: `arcade_roster_opt` takes the operator's values whenever `nobody_playing()`, which is always
  true in Regular. So F2 roster changes apply as soon as F2 closes, even in a select or a match. That is W7.

## Phase 2 (2026-11-07 evening) -- what changed (src/logic.c line numbers as of 18:00)

### W9 the per-mode toggles
- mod.toml: `fast_getup` (Arcade's) + `fast_getup_regular` (false), `arcade_speed` (Arcade's) + `regular_speed` (false),
  `rumble_continue_arcade`/`_regular` (true/false), `short_menus_arcade`/`_regular` (true/false), `splash_arcade`/
  `_regular` (true/true), `attract_arcade`/`_regular` (true/true), `rumble_rules_arcade`/`_regular` (true/false),
  `stage_cam_arcade`/`_regular` (true/false).
- logic.c: `mode_key()` (427) picks the running mode's key; `load_settings` (433) / `save_settings` / `factory_settings`
  (597, the running mode's factory values) read and write only the running mode's value.
- F2 GAME SETTINGS (both modes): GAME SPEED (was ARCADE SPEED), FAST GET UP, ABBREVIATED MENUS, LOW ENTRANCE CAMERA,
  SPLASH SCREENS, ATTRACT (+ ATTRACT DEMOS, MENU IDLE in Arcade; CONTINUE TIMER in Arcade or when the Rumble continue is
  on); ROYAL RUMBLE page: STARTS WITH, RUMBLE CONTINUE, OVER THE TOP ONLY (RULES). A note says whose values they are.
- Wired: GAME SPEED -> `arcade_match_step`; FAST GET UP -> hook 20 now passes 1, `arcade_fast_getup()` decides;
  RULES preset -> `arcade_rules_preset_done`; camera -> `arcade_entrance_cam` (3670; Regular: any real match, not a demo);
  SPLASH / ATTRACT -> `splash_attract_service` (4926: splash off leaves the intro the menus' own way before the first title;
  attract off holds NM_TITLE_IDLE at 0; Arcade's `drive_title` stands aside); ABBREVIATED MENUS -> Arcade's Story
  shortcuts block and `arcade_story_page` (3594); Regular ON -> `regular_short_menus` (4953: Story Mode's RULES at once, VS
  card after 5 s, story pages); RUMBLE CONTINUE -> Arcade's `watch_match` and `arcade_elim_check` hold; Regular's in
  `regular_rumble` (5003: held, CONTINUE? countdown, Start puts him back).
### W5 Custom Story in Regular
`champ_custom_defs_regular` (922): row 8 under Women's, links World up / Women's down to it, it up -> Women's, down ->
World, left/right itself; label 0x852F LENT only while the list is up (`custom_label_lend`, 936: no free picture label
exists -- 0x8518..0x851B are the boxes', 0x8531 on isn't the label table). `regular_pads` (2713) answers the row and the
stories' list (A/Start, B, L/R; taken buttons held back until let go). The same `list_open` / `custom_open` as Arcade.
### W6 the ARENA page past ten rows anywhere
`arena_page_frame` on any ARENA page (both modes), `arena_page_pad` from Arcade's pad branch (any screen 5) and
`regular_pads`.
### W7 roster changes between games
`between_games()` (630): Arcade nobody playing; Regular the title or a main page, F2 closed.
### W8 QUIT MATCH / RESET TO TITLE in Regular
`match_quittable` (3272) and `menu_action` Regular branches; `regular_route` (4965): the match's own exit (QUIT=1,
LEAVE=60), the entrances run to their end first; QUIT leaves the return screen to the game; RESET sets the title
(NM_RETURN_SCREEN 0, NM_MODE -1). F2 pause restore (`op_pause_was`) as in phase 1.
### W10 the F2 Rumble settings in the game's own Rumble (a and c; b not done)
- `arcade_menu_action_done` (1696, hook on func_8010F0A8's return): action 0x404 gives the match its copy (gm_*) of F2's
  SUPERSTARS IN IT / IN THE RING AT ONCE / TIME BETWEEN ENTRANTS / STARTS WITH.
- `arcade_number_setup` (1708, hooks on func_8012E934 and func_8010CE74): the NUMBER page and Start's skip-ahead start
  from the match's count (ORDER's confirm resets the remembered NUMBER, so the seed goes in just before either reads it).
- `arcade_number_confirm_done` (1716, func_8012E9F0's return): a changed page value is that match's; never written back.
- `arcade_rr_finalize_pre` (1743): the ring for this match (no leftover from the last Rumble), ORDER Random's count.
- `arcade_rr_finalize_post` (1826) + `gm_start` (1728): the field drawn again for Random (NO WOMEN / ONE UNDERTAKER, the
  four picks kept: `rr_draw_field_from`), the start emptied past 2 (or 4: STARTS WITH; humans always in), then the
  arcade's own clock (`arcade_rr_match_setup` 1888 / `arcade_rr_frame`) with the match's entry time (`rr_interval`).
- Regular's reset of the Rumble state waits past the match card / loading screens (`rr_on_way`); `arcade_rr_match_setup`
  also takes a game-menu Rumble whose slots were emptied (a crash in the HUD otherwise, run 172318).
- NOT DONE (W10b): the NUMBER page counting by one, and IN THE RING AT ONCE / ENTRY TIME rows on the game's screen.
### W11 menu idle -> attract (Arcade)
`menu_idle` (2924), F2 MENU IDLE (0..300 s, 90, 0 never), option `menu_idle_secs`; the PLAY case ends after it.
### W12 a story of ours' game over in Regular
The universal part of `arcade_frame`: phase 10 in Regular -> `reg_goal = 2` (the title the RESET way).
### QA
`qa_reg` bits 16 (custom story), 32 (F2 QUIT MATCH), 64 (F2 RESET), 128 (ARENA past ten rows), 256 (a Rumble from Multi
Play, `qa_regular_rumble` 4766), 512 (the "Retry Test" story to its end); `qa_reg_right`; state.csv: in a match bs_step =
the Rumble's entrants | ring << 8 | interval << 16 | gm_live << 24 | rr_mode << 28; bs_grabs >> 16 = F2's SUPERSTARS IN IT
as saved; on the title rr_next = PRESS START found | alpha << 8 | NM_TITLE_IDLE << 16; Arcade elsewhere rr_next = idle
frames | games ended by idle << 24; on screen 9 bs_step = the NUMBER widget's +0x4C/+0x4D/+0x4E | ways tried << 24.

## Phase 2 tests (qa\scenarios\modes_*.json, -ModDir mod_modes -Copy game4; shots looked at)
| scenario | run | result | what it showed |
|---|---|---|---|
| modes_reg_splash_attract_off | 165001, 182002 | PASS 4/4 | Regular, SPLASH and ATTRACT off: title at 4 s (one frame of the legal page as the intro becomes skippable), then 150 s on the title, never a demo |
| modes_arc_attract_off | 165257 | PASS 4/4 | Arcade, ATTRACT off: the title waits with INSERT COIN / CREDITS 0 at 200 s |
| modes_reg_custom_story (-Speed 6) | 172526 | PASS 7/7 | Regular's list: Custom Story in the Belt box's place, the game's own Complete box (3.0%) beside it, no Rumble / Survival / Versus rows; the stories' list; select; Chapter 1; the story's page, scene, RULES ("not selectable": A), VS card, match; F2 QUIT MATCH -> the chapter card (the game's own way back); F2 RESET TO TITLE -> the title |
| modes_reg_story_gameover (-Speed 6) | 172808 | PASS 3/3 | "Retry Test": three lost matches (2 retries), then the game over -> the title at once, the story let go, no loop |
| modes_reg_arena (-Speed 6) | 175648 | PASS 4/4 | the ARENA page scrolled past ten rows onto the WM 2000 arenas; the match in WCW Souled Out (venue 14); Regular's toggles' defaults: the game's own entrance camera |
| modes_arc_menu_idle | 175037 | PASS 3/3 | Arcade, MENU IDLE 30: coin + Start -> the list, 30 s idle -> the game ended quietly -> the attract (THQ with INSERT COIN) |
| modes_reg_rumble (-Speed 6) | 181122 | PASS 7/7 | Multi Play > Royal Rumble > Random > NUMBER opens at F2's 12 (shot) > ... the match: 12 entrants (the HUD's 012), ring 5 (the engine keeps 4 below six), every 10 s on the arcade's clock, two in the ring at the bell (Austin vs Scotty), Rikishi at 0:11, F2 still 12 |
| modes_reg_rumble_change | 174713 | FAIL 1/2 | the NUMBER page never took a RIGHT from the QA driver (5 ways: modes_reg_number_probe 181656), so the per-match change couldn't be made; F2 stayed 12 |
| Arcade: menu_from_attract, announcers_front, tron_entrances, hd_select, versus_1p, music_rumble_late, story_upgrade_b, rumble_ring10 | 182301..185127 | all PASS | as phase 1; rumble_ring10: ten in the ring |
| final build: modes_regular_match | 185624 | PASS 8/8 | as phase 1 (HD Stone Cold vs The Rock through the game's menus) |
| final build: modes_reg_custom_story | 190019 | FAIL 6/7 | after QUIT MATCH the story went on to a second match before the driver's RESET; that RESET fired from the game's OPTIONS page (screen 18, reached after the story's second match): the title came up, then the menus went on by themselves to the main menu's SmackDown Mall / Options page -- a W8 defect (below) |

## Phase 2 findings
- The NUMBER page (screen 9) opens with "Number of Superstars" and a big green number (index x 4). Its widget didn't
  move for any of five inputs from the QA driver (d-pad as the menus' press, d-pad on the controller, stick briefly,
  C-right, stick held a second): its +0x4C/+0x4D/+0x4E stayed 3. Not the mod's seed (the set-up hook ran twice, not each
  frame). The menus' loop only steps a type-5 row when its +0x52 is 0 and after func_80110504(row) allows it (masks
  0xF0CF..0xF0D3 by screen / match type): to be traced on screen for W10b.
- Until W10b, the page shows F2's count rounded down to its steps of four (15 shows 12), while the match uses F2's exact
  count when the page is left as it came.
- IN THE RING AT ONCE below 6 acts as 4: the engine's rw_rumble_fill keeps the stock four slots under six (six-engine,
  not touched).
- The title's PRESS START (sprite 15 at 240,200, requested by the menus' loop each frame NM_TITLE_IDLE >= 0:
  func_80111158 0x80111740) is in the sprite queue all along an untouched title but its alpha stays 0; it lit (255) only
  as the title took Start. The mod's own text with the same request flags shows, so the next place to look is the
  queue's fade for a picture sprite (by id, not a string pointer). The arcade no longer hides it in Regular. Not fixed.
- The arcade's automatic Next on the Rumble's results table sometimes doesn't fire: the real mod's run
  2026-11-07_154842_music_rumble_late (the other job's) stays on it with no try, as does 184326 here; 163059 moved on.
- RESET TO TITLE from a story's match goes by the chapter card for a moment (the game's own way back), then the title.
- DEFECT (W8, open): RESET TO TITLE in Regular with the game's OPTIONS page (screen 18) up: the title comes up, then the
  menus go on by themselves to the main menu's SmackDown Mall / Options page (run 190019, 192.6 s -> 197.8 s) --
  likely the game's own pending way back from Options surviving the restart; regular_route stops at the title's
  first frame. To try: keep NM_RETURN_SCREEN on the title a few seconds after reaching it, and test from page 18.
- In 190019 the story also carried on to a second match after QUIT MATCH (the game's own chapter card going on), where
  172526 had the driver's RESET land on the card within 5 s: the driver's timing, but whether QUIT MATCH in a story
  counts as a loss (a retry) is worth a look.

## Left (after phase 2)
- W10b: the NUMBER page counting by one over 2..40, and IN THE RING AT ONCE / ENTRY TIME (and the other F2 Rumble
  options) as native rows of the game's Rumble screen; the per-match change test after it.
- Regular's title PRESS START (above). The Arcade legal page (phase 1 finding).
- Abbreviated menus: Arcade OFF lands on the arcade's championship list still (not the game's main menu); Regular ON
  doesn't skip the chapter card; the Rumble's pages skipped with F2's values (Regular ON) not done.
- The Rumble continue in Regular is for one player (the first human slot); the ARCADE-controls arrow in Regular.
- W13 naming (strings in the mod: mod.toml display_name / descriptions; logic.c page_title "OPERATOR MENU", the INPUT
  TEST line "F2 OPERATOR MENU", CONTROL MODE's "REGULAR"/"ARCADE" names; README.txt; source head comments).

## Phase 3 (2026-11-07 night) -- what changed (src/logic.c line numbers as of 19:50)
### W10b the Rumble's NUMBER page, native
- Why the widget never moved for the driver: it is a type-5 row, which the menus' loop steps with UP / DOWN (the repeat
  word 0x80106640, 0x800 / 0x400; func_80111158 L_80111544), not LEFT / RIGHT. A controller's UP takes that path (the
  driver now presses UP on the controller with qa_pad, and a real pad's press goes the same way). Nothing in
  regular_pads or the F2 block eats it.
- Counting by one over 2..40 (ORDER Random): the big number (func_8011ED80) is +0x4E x mult + offset from the row's label
  (bits 4-7 / 0-3), so label 0x800 shows the value itself. The limits (+0x4A nibbles) can't hold 40, and the game has
  two "no limits" conventions that can't both hold: the loop takes 0xF000 (L_801115D4), while the rows' per-frame update
  (func_801079B4 L_801082B0) takes min == max and otherwise clamps (it put every value to 15). So the row keeps 0xF000
  for the loop, and a hook on func_801079B4 (arcade_row_update, 1885) makes this page's widgets read min == max while
  it runs. The pieces: `arcade_number_setup_done` (1845, func_8012E934's return); `number_page_frame` (1901: keeps the
  range 2..40, holds the limits from the controller read only); `arcade_number_confirm_done` (1923: the page's value is
  the match's).
- The page's own rows (`number_page_defs` 1797, at every overlay load, both modes): the page table now says 8 rows for
  screen 9, with its definitions moved into the mod's memory. The rows are NUMBER, IN THE RING AT ONCE (4..32), ENTRY
  TIME (10..95 s, in steps of 5, because the drawer has two digits) and STARTS WITH (2 / 4, stored as x2 + 2). Each is a
  widget like the game's under a caption, laid out 2 x 2. LEFT / RIGHT go round them, UP / DOWN change a value, and A
  on any is the page's confirm. The captions are pictures of their names in the menus' lettering (`label_text_render`
  1025, the stories' list's maker), lent as labels 0x8528..0x852A while the page is up. At the set-up the new widgets
  take the game's own row 0's control fields (+0x52 / +0x54 / +0x5A / +0x74); with the pool's leftovers they took no
  UP / DOWN and rolled. They are hidden on the other NUMBER pages and for non-Random orders.
### W8
- RESET TO TITLE in Regular: the title is watched for 5 s after it comes up (`regular_route` 5307, goal 4). If it is
  left with nobody pressing anything (a story's flow from its chapter card's OPTIONS running on), the title is started
  over again; a story of ours is let go at once. From the main menu's OPTIONS page it already stayed (run 193443).
- QUIT MATCH in a story's match (context 8, or a story of ours): the menus open on Single Play with the story let go
  (goal 3). Before, it went the game's way back through the results and the chapter card, where it counted as a loss and
  a retry.
### The partials
- ABBREVIATED MENUS off in Arcade: the arcade's routing stops on the game's main menu (Multi Play), with no B blocks and
  no re-route to Story Mode's list.
- ABBREVIATED MENUS on in Regular: the chapter card is skipped as Arcade's is (`story_card_skip` 3806, shared).
- Regular's Rumble continue, each player his own (`regular_rumble` 5359): a countdown per human slot; his own Start puts
  him back in his own slot (`rr_back_slot`).
### Others
- CONTROL MODE shows "N64" / "6-BUTTON" (3294). The option still stores "Regular" / "Arcade", because renaming an Enum's
  strings would drop every saved six-button set-up.
- W7's test: F2's roster changed in a select (BIG SHOW off) keeps the live roster until a main menu page (as built in
  phase 2; the test is new).
- QA: probes for the title's PRESS START (sprite 15's requests and draws, its blink counter, picture 0x458B in the
  sprite cache: `arcade_qa_sprite_draw` 3924, qa_press_start_second), the chapter card, the page's rows,
  the roster (W7) and the Rumble continue. New qa_reg bits: 1024 (RESET from OPTIONS), 2048 (the page's rows), 4096
  (F2 BIG SHOW off on the select), 8192 (the Rumble player kept on his controller, Start on CONTINUE?).

## Phase 3 tests
| scenario | run | result | what it showed |
|---|---|---|---|
| modes_reg_rumble_change (-Speed 6) | 193121 | PASS 2/2 | UP x5 on the controller: the page 12 -> 17 in the game's big digits (shot), the match had 17 entrants, F2 still 12 |
| modes_reg_reset_options | 193443 | PASS 4/4 | RESET TO TITLE from the main menu's OPTIONS page: on the title, 20 s on it (ATTRACT off) |
| modes_reg_custom_story (-Speed 6) | 194237 | PASS 7/7 | F2 QUIT MATCH in the story's match -> Single Play (3), the story left; RESET -> the title |
| modes_reg_roster (-Speed 6) | 194914 | PASS 3/3 | F2 BIG SHOW off on the select: F2 off, live roster on (Big Show still in his box, shot) through the select and the match; live roster off back on Multi Play |
| modes_reg_rumble_rows (-Speed 6) | 193858, 194526 | FAIL | the page with its four settings and captions laid out 2 x 2 (shot from 194526); LEFT / RIGHT moved the cursor; the new rows took no UP / DOWN and rolled. Fix: copy row 0's control fields (rerun below) |
| modes_reg_rumble_rows (-Speed 6) | 195251 | PASS 4/4 | each setting stepped on the page (probe: N 12->15, ring 6->8, time 6->4 = 20 s, start 0->1 = 4); shot: 15 / 8 / 20 / 4 under their captions (the font's 2 looks like a 5: compared with the game's own "12"); the match 15 entrants, ring 8, every 20 s, four in the ring at the bell (shot: 015, FIGHT!, Austin, Scotty, Rikishi, Sexay); F2 still 12 |
| modes_arc_short_off | 195648 | PASS 3/3 | Arcade, ABBREVIATED MENUS off: coin + Start in an attract demo -> the game's Multi Play menu, 80 s on it (shot), never the championship list |
| modes_reg_short_on (-Speed 6) | 202210, 203739 | PASS 2/2 | Regular, ABBREVIATED MENUS on: no chapter card -- straight into the story's scene (shot at 86 s), where with them off (194237) the Chapter 1 card (Continue / Save) is up 85-89 s first. The 9 s and 40 s left on screen 23 are the story's scenes and its choice ("Do you want MANKIND in your corner?"), as in Arcade (the skip's own comment: "on to the story scene") |
| modes_reg_title_press (speed 1) | 202737, 203341 | PASS 2/2 (the probe), not fixed | the title: sprite 15 requested and drawn 30 times a second, its blink counting 1..40 and back; no PRESS START on any shot (full-contrast crop of its band: nothing) -- see the findings |
| modes_reg_title_press_old (speed 1) | 204853 | (probe) | settings_version 4, enabled off: the same -- picture 0x458B in the sprite cache, loaded, drawn 30 times a second, nothing on screen |
| modes_reg_rumble_continue (-Speed 6) | 200733 .. 215151 | FAIL (the test's driver) | the player was never out: the CPU doesn't throw out a player who stands still (20 min of game); put on the floor (69 times in 49 s, 215151) he isn't out under the game's own rules -- the driver changed: the CPU plays him, his controller handed back once the game has him out |
| modes_reg_rumble_continue (-Speed 6) | 215927 | PASS (checks) / defect | the game eliminated him at 7:32 of match; his controller back -> PLAYER 1 CONTINUE? 10, PRESS START (shot 253.7 s); Start -> back in the Rumble (shot 15:16: Austin in the ring, full meter). DEFECT: while CONTINUE? is up, large black rectangles over the middle (x centred on 240: at y ~41-59, ~67-172, ~182-200 of 240) -- not there before or after; cause not traced (nothing in logic.c draws fills in a match; they don't match the per-player texts' places). Also the put-back counter reads 2 for one Start. Two players not tested (the driver has one) |

## Arcade regressions (final build, 2026-11-07 21:01-21:41; shots looked at)
| scenario | run | result | shot |
|---|---|---|---|
| menu_from_attract | 210100 | PASS | the arcade's championship list, 1P INSERT COIN |
| announcers_front | 210553 | PASS 2/2 | JR and King in front |
| tron_entrances | 210722 | PASS 2/2 | RAW IS WAR, the match, 2P PRESS START |
| hd_select (-Speed 6) | 211028 | PASS 3/3 | the select; the 2K15 Stone Cold vs The Rock |
| versus_1p (-Speed 6) | 211605 | PASS 7/7 | Austin (1) vs Rock (C); Versus's MATCH page after |
| music_rumble_late (-Speed 6) | 212225 | PASS 3/3 | the Rumble going (005 left), won |
| story_upgrade_b (-Speed 6) | 212346, 213517 | PASS 3/3, PASS 3/3 | 212346: another match (0x0101 vs 0x010D, a "RAW: HHH vs GOLDBERG" scene) than every run before -- unexplained, not in any story file; 213517 (rerun): Austin vs Rock, the script's pinfall at 0:45, as phase 2's 184501 and the other job's 200811 |
| rumble_ring10 (-Speed 6) | 212947 | PASS 4/4 | ten in the ring |

## Phase 3 findings
- The title's PRESS START (Regular; phase 2 said its alpha stays 0 -- that probe read the queue entry's fade, not what
  draws). Traced: the menus' loop requests sprite 15 each frame; the queue draws it (func_80117714) and id 15 is the
  special case func_80117630, the blink: its own counter (0x8014A5AC, 1..40 and back; flag 0x8014A5B0), alpha = count
  x 8, drawing picture file 0x458B through func_80114294 at (170,180)-(310,203). All of it works in the runs: the counter
  swings, the picture is in the menus' sprite cache (0x80104D30, 125 of 128 entries free), loaded (decoded from the RAM
  dump: the game's red "PRESS START", 128 x 32 CI4), the combiner gives its own colours (FC5196A3 112CFE7F: rgb =
  lerp(TEXEL0, ENV, PRIM rgb 0) = TEXEL0, alpha TEXEL0 x PRIM). It is drawn as a quad: G_VTX loads 4 vertices from
  0x800F3688 -- all at (0, 0, 0) -- and func_80114294 places them with G_MODIFYVTX (0x02, screen XY 0x18, ST 0x14, Z
  0x1C), then G_QUAD. The same with enabled off on old settings. The mod touches none of it. The lead: the renderer
  (RT64) not applying G_MODIFYVTX's screen XY here, which leaves a zero-size quad -- the port's, not the mod's; RT64's
  source isn't in this tree to confirm, and exe\ is outside this job.
- The NUMBER page's caption labels (0x8528..0x852A) lie inside the stories list's lent range (0x8525..0x852C). Both are
  lent only while their own screen is up (9 / 22) and given back on leaving, so they never meet; worth moving if either
  grows.
- Chapter card (Regular, ABBREVIATED MENUS on): the card's own overlay replaces the menus' code within a frame or two
  (title_loaded false on screen 23: 420 of 428 samples), so in_menus() is false there; the skip still lands in those
  frames, as Arcade's. Screen 23 then hosts the story's scenes (its state 3 at 0x80097ED6) -- they play, as in Arcade.

## Left (after phase 3)
- The title's PRESS START: the renderer's G_MODIFYVTX (above) -- exe\ / RT64, not this job.
- Regular's Rumble continue: the black rectangles while CONTINUE? is up; two players untested (the driver has one).
- IN THE RING below 6 acting as 4: the six-engine (the coordinator's).

## Left for phase 2 (as written after phase 1)
W5 Custom Story row in Regular (its own label id: 0x8517 is the GBC row's there; links without Survival); W6 ARENA page
past ten rows on any ARENA page; W7 roster changes between games in Regular; W8 Regular's QUIT MATCH / RESET TO TITLE
(hidden in Regular for now), F2 naming; W9 the toggles (GAME SPEED, FAST GET UP, RULES preset, stage-walk camera -- all
Arcade-only for now, i.e. off in Regular as the user's defaults say; splash, attract, abbreviated menus, Rumble
continue); W10 the F2 Rumble settings in the game's own Rumble; W11 menu idle -> attract; W12 a story of ours'
"gameover" in Regular; W13 naming; W14 qa_tool (settings_version 5, the mode column in state.csv); the ARCADE-controls
arrow in Regular matches; the QA Rumble bench (qa_on 2), qa_story and qa_versus are Arcade-only (they drive the arcade).
Regular's title PRESS START (above).
