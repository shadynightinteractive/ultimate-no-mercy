# Match slots: more than four wrestlers in any match type

(any32, 2026-11-18. Code: `mod/src/slots.c`; QA scenarios `qa/scenarios/any32_*.json`.)

The user's requirement: no limits. Every match type -- one on one, tag, triple threat, handicap, cage, ladder, special
and story matches -- must be able to have more than four wrestlers present, up to 32, as the Royal Rumble does, and
new match types (the Lumberjack match, Survivor Series elimination, 3-on-3 tag ...) are built on that, NOT on the
Royal Rumble's machinery. Rumble pieces may be reused where they help (said below which and how); each match type
keeps its own rules, flow, HUD and ending.

## 1. The map: every per-slot limit a match hits past four

A *slot* is one wrestler place in a match (the record `SIXA_80159990 + 0x358 x slot`, 0x8017FEF0 in the 32-slot
layout). The stock game has 4. Our engine (historically "No Mercy Six", `six-engine/work`, built with `SIX_SLOTS=32`;
the mod's `six_addrs.h` `NM_SLOTS_CAP 32`) moved or rewrote every 4-slot structure it found. What it covers, and the
three things it did not (marked **open before any32**):

| area | where (stock address / function, converted name) | stock size | 32-slot engine / Rumble |
|---|---|---|---|
| **the match's count** | `$gp` = `rw_match_count` (blob, `SIX_MATCH_COUNT`), read once when ovl_e starts (`rw/stubs.S rw_run_overlay`), then 4 again; every widened loop runs to it | 4 | room for 32 -- but **only the Royal Rumble's set-up raised it** (`rw_rumble_fill`, the mod's `gm_fill_ring`). **open before any32** |
| the game's own count by type | `hud_player_count` 0x80151ED7 (`run_match`, func_800D9990): 2 singles, 3 for 0x28000, +1 run-ins, `$gp` for tag / Rumble (0x6000); the panels' count 0x80158E98 (`set_up_match_hud`, func_800DDB10) | by type | not the set-up's count: `set_up_match_memory` (func_80113E30) sets up slots 0..`$gp`-1. It is the HUD's and the pads' count -- a type's competitors |
| **match records (movesets, names, looks)** | 0x80097FE0 + 0x114 x n (moves +4, packed moves +0xBC, name +0xDC, short name +0xF1, looks +0xFC); built by `build_match_records` (func_80136CC8); read by `unpack_match_movesets` (func_80114348) for slot n = slot (Rumble: entrant), `find_superstar_record` (func_8000A690, names/looks by the ID list 0x800BD4D0) | room for 40 (zeroed 0x2B20); built: 4 (singles 2) | **not built past slot 3 outside the Rumble** -- the Rumble builds one per entrant (0x800BD4D0, count 0x800457AC). A one-on-one's slot 4 unpacked an empty moveset: the Lumberjack probe's crash in `walk_step_length`. **open before any32** (room was never the problem) |
| unpacked movesets | 0x8051C360 + 0x40 x slot (abilities) | 4 | 32 (`spec.py` main arrays) |
| the slots' menus data | slot Superstar `SIXA_800A4038`, look `SIXA_800A8958`, controller `SIXA_800A6028`, level `SIXA_8009ED10`, human `SIXA_8009ED20` | 4 | 32 (moved); filled past 3 only by `rw_rumble_fill` |
| **slot roles in the set-up** | `func_80115000` (the mod's copy, `modrep_func_80115000`): +0xE9 bits from the match flags and `rw_tab_6440/636C/6450` (slots 4 on: the computer's), singles: +0xE9 0x10 (a side), target slot ^ 1, side slot / 2; places `func_80115E54` (by type: 4-entry tables; modes 0xC000 `rw_tab_6558`, 32 spots) | 4 | tables widened for 0xC000 (Rumble / three-way); in a one-on-one or tag every filled slot becomes a side of the match. **open before any32** |
| the decision (who won) | func_80117438 = `rw/r_80117438.c` (rewritten: lists of n + 1), results `rw/r_80118A20.c`, result bits: slots 0..3 in 0x800A10EC, 4 on in `RES6` / `RES6L` | 4 | 32 -- but a singles fall is decided as slot 0 vs slot 1 (A = 0, B = 1): an extra's fall would end the match. The type decides only by +0xE9 0x10 ("still a side") |
| the referee's pin count | `run_pin_count` (func_801163C4): the first slot with +0x8A 0x400; clock 0x80189750 + 12 x slot; 3 -> +0x94 |= 8 | 4 | loops to `$gp`, clocks moved (32) -- counts anybody pinned |
| wrestlers' state | 0x80159990 x 0x358 and ~50 ovl_e per-slot arrays (`spec.py OVLE`: HUD sprites, input history, run/rope, head-look, CPU move lists, AI per slot, hit radius, per-slot state, controller port, the step records found by the 32-man crash) | 4 | 32 |
| animation / models | `spec.py MAIN`: model anim stream, buffers, loaded ids, attachments, face parts, joints, blend, DL objects, shadows, anim tables A/B, render struct, bones, parts, descriptors, AI per model | 4 | 32 |
| AI | `func_800F03F0` (a slot's buttons), `func_800FB038` move lists, CPU recent actions / timers / last action (ovl_e .data), level `SIXA_8009ED10` | 4 | 32; target lists `func_80120D98` (`patch_target_list.py`, max(n-1,10)+1) |
| collision | contacts `func_800EBD2C` (`rw_contacts`, `patch_contacts.py`), hit radius 0x8016B728 | 4 | 32 |
| camera | `func_80116130` in-view mask (`patch_cam_wide.py`), event word slot field `rw_tab_6350`, replay bits `rw_tab_DB60/70/78` | 4 | 32 |
| HUD | panels: corner tables `rw/r_hud.c` (6 corners), name textures `namebuf` (32), stat matrices `statmat` (32 x 32) | 4 | 6 panel places; a Rumble shows the ones in the ring |
| sound | per-wrestler calls by slot (`play_wrestler_sound` func_80017780: the voice by slot), the sequence lock 0x8016B650 (`patch_seq_lock.py`, `rw_seq_lock`) | 4 | 32 |
| results / pin logic | above; the results table `rw/r_results.c` (rw_last_count) | 4 | 32 |
| tag / partners | sides: match flags 0x10..0x80 by slot (`teamA_bit` in the decision: 4 slots), waiting outside +0xE9 4 (`rw_tab_6450`, bits 8-11), the swap bits in 0x8016B650 (bits 4-11), tag-in / save (`try_tag_partner_save`, `non_legal_man_free_to_act`) | 4 | slots 4 on share the lock bit 0x1000; **no side bit or waiting bit past slot 3** (see 4.) |
| memory pools | scene-record pool (`patch_record_pool.py`: 300 + 96 a slot), frame display lists (`patch_dlbuf.py`), draw lists 150 a slot (`spec.py MAIN_SHARED`), the heap between ovl_e's BSS and the main arrays (3 MB with 32) | 900 / 2 x 0x2F58 / 900 | 32 |

## 2. The per-slot model

A match has `slots` places (its count, `$gp`, up to 32). Each slot has:

| field | meaning | where now |
|---|---|---|
| role | EMPTY, COMPETITOR (one of the match type's own sides), EXTRA (present, not a side: a lumberjack, a run-in, a second / manager, a crowd brawler) | `slots.c ms.role[]` |
| side | which team he is on (competitors: the type's own, record +0x0C; extras: their own, 8 + slot) | `ms.side[]`, record +0x0C |
| state | LEGAL (may be pinned / counted), WAITING (on the apron / in his corner), ELIMINATED (out: walks to the back) | `ms.state[]`, record +0xE9 0x10 / 4 |
| rule | what a pin of an extra does: RINGSIDE (the referee never counts it) or ELIMINATION (three puts him out, the match goes on) | `ms.rule` |

The match type decides only **who is legal** (+0xE9 0x10 and the `state` above) and **how the match ends** (its
decision). Everything else -- loading, records, models, animation, AI, collision, camera, sound -- is per slot, for any
count.

The three engine steps any32 added (slots.c), for every match type:
1. `match_slots_add(n, ids, looks, rule, wait)` -- after the type's records are built, before the match: the extra
   slots after the type's own (singles 2.., triple 3.., tag / four-way 4..) get their Superstar, look, the computer's
   controller and level; **their match records are built** (`func_8000BAD8` into record n, as the Rumble builds an
   entrant's; the ID list 0x800BD4D0 so names and looks are found); the match count `rw_match_count` = the slots.
2. `match_slots_setup()` -- after the type's own set-up (func_80115000): the extras are the computer's, NOT a side of
   the match (+0xE9 0x10 cleared: the type's decision never counts them), each on a side of his own, a target, placed
   round the ring (a square 360 out, facing in).
   The request is kept and done again each time the match records are built (`build_match_records`, func_80136CC8:
   the menus build them once more as they leave for the match, which empties the records and a singles match's slots
   2 and 3) -- a return hook on that function.
2a. `match_slots_on_memory` -- after `set_up_match_memory` (func_80113E30, the match starting): a one-on-one makes
   every slot past 1 a run-in man held back (+0xE9 = 2, +0xF1 = 2 -- `func_80115000` then hides him, +0xA0 = -1) or a
   man placed outside (+0xE9 = 1); a handicap match its slot 3. For an extra slot those bits are cleared (a return hook).
3. `match_slots_step()` / `match_slots_cpu()` -- each step: the referee's count on an extra follows the rule (stopped
   for RINGSIDE; for ELIMINATION his three is taken before the game's own, so it never reaches the type's fall
   decision); targets renewed; an extra waiting (QA `qa_extras_wait`) or eliminated has his buttons held.

4. `match_slots_after()` -- when the menus' overlay comes back after the match: the extra slots are emptied as the
   type leaves an unused slot (Superstar -1, controller -1), their IDs out of the record list, the computer bits of
   slots 2 and 3 the type's own -- the menus see the type's own slots again.
5. `match_slots_hud()` -- the HUD stays the type's: an extra slot's panel is hidden (a tag match's panel count is the
   engine's count; a singles match's is 2 anyway).

Pins on extras, as found: in every QA run (up to 30 extras, minutes of fighting) no computer player ever covered an
extra -- the game's AI covers only a man who is a side of the match (+0xE9 0x10), which an extra is not. So under the
RINGSIDE rule an extra is never pinned by the computer, and the referee's guard (`match_slots_step`: a count on an
extra stopped) is there for a player's cover. The ELIMINATION rule (the referee's three on an extra taken just before
the game's own, the extra out, the match going on) is written but NOT yet exercised in a run: it needs a match type
that makes its extras pinnable (a side, +0xE9 0x10) and then decides its own ending over sides -- the Survivor Series
design below. A QA probe that set the pinned bit (+0x8A 0x400) by hand did not work (the wrestler states rewrite that
word every step) and was taken out.

A stale-memory bug found on the way (tron.c, not the slots' own): after a match the Titantron put its saved panel
vertices back into the arena's memory, which the menus had already re-used for their page rows -- the 12-man runs
crashed in the menus (`give_page_row_objects`), found with the exe's write-watch on the row word. Fixed twice over:
`unpatch_all` puts a vertex back only while it is still the white it wrote, and after a match with extra slots the
patches are forgotten (`tron_forget_patches`, from `match_slots_after`). The stock game's matches are left as they were
(the second fix runs only after a match with extras); the first is a guard that changes nothing while the memory is
still the arena's.

With no extra slots asked for, none of this runs: `ms_live` stays 0, nothing is written.

### What was reused from the Royal Rumble, and how
- **Record build by entrant**: the Rumble's branch of `build_match_records` builds a record per entrant with
  `func_8000BAD8(id, look, 0x80097FE0 + 0x114 x n)`; `match_slots_add` calls the same function for each extra slot n
  (the record index is the slot, as a non-Rumble match reads it).
- **Slot fill past 3**: as `rw_rumble_fill` does for a Rumble's slots 4 on (Superstar, look, controller 0, level).
- **The engine's 32-slot tables** (all of section 1) and the count `rw_match_count`.
- NOT reused: the Rumble's flag (0x4000), its entrant order, entrances, eliminations, HUD, results table or AI. A
  one-on-one with extras is still a one-on-one: match flags 0x1000, its own rules and decision.

## 3. Proof that stock is unchanged

Lockstep replay hashes (qa.ps1 -Lockstep, NM_REPLAY_HASH: a hash per structure of the engine group -- random,
step_context, controls, ai, match, match_objects, ring_arena, moves_animation, wrestler -- and of the N64's 8 MB, every
retrace; NM_REPLAY_MOD_SPAN 0 leaves the mods' own memory out), the same exe, the stock QA scenarios run with the main
branch's mod (A) and with this one (B), both translated as loaded (NM_AOT=0). tools/replay/replay_diff.py and a
member-level comparison over the retraces both runs have:

| scenario | retraces | result |
|---|---|---|
| control: match_tag, A vs A again | 6,064 | THE SAME, every structure and the 8 MB: the runs are deterministic |
| smoke_singles (one on one) | 23,712 | identical except one word of menu data (below) |
| match_tag | 6,064 | identical except that word and Wrestler[1]'s two wall pointers (below) |
| match_triple | 14,444 | identical except the menu word |
| match_handicap | 14,442 | identical except the menu word and the wrestler record's pointers |
| Royal Rumble (any32_proofrumble = rumble_ring10, 10 in the ring, with fewer screenshots) | 36,030 | identical except the menu word |

What differs, read from the bytes (NM_REPLAY_DUMP at retraces 161 and 1967): every differing word is a pointer into
the mod's own memory, moved by exactly 0x18A0 -- the bytes `slots.c` adds ahead of the mod's data: Wrestler[1]
nearest_wall / second_wall 0x8112E3C0 -> 0x8112FC60 (the mod's collision walls), and the menus' word at 0x8014E010
0x811618A8 -> 0x81163148. The same wall, the same table, at another address. Nothing that follows from them differs:
the AI, the random numbers, the controls, every position and the match state are the same frame for frame. The "ram"
hash differs from the first retrace for the same reason (mod addresses held in the N64's memory and stack) and because
the mod reads three more options (the host's answers). The A-vs-A control shows that without a mod change the 8 MB
are identical too.

## 4. For the match types to come

### Lumberjack (lumberjack + lumberjack2, 2026-11-21: built -- `mod/src/lumberjack.c`)

The match: the competitors fight in the ring; a ring of lumberjacks stands round it on the floor; a man who leaves the
ring or is thrown out is set on by them and put back in. The lumberjacks are never in the match: a pin or a submission
in the ring decides it, by the match type's own rules.

**The real rules, and what is followed** (round 2; sources read 2026-11-21: Wikipedia, "Professional wrestling match
types" -- Lumberjack, Canadian Lumberjack and Extreme Lumberjack matches, Tornado tag team match -- through its en-academic
mirror, and thesmackdownhotel.com's "Lumberjack Match"; where they are silent, long-standing WWF / WCW / WWE practice,
marked so):
- A lumberjack match is a standard match with the ring surrounded by wrestlers who are not in it. -> The match type's own
  rules stand: the RULES page (falls, submissions, count-out, DQ) as the player set it; the decision is the type's.
- The lumberjacks are there to keep the competitors in the ring: a man who leaves it is attacked and thrown back in. ->
  the swarm (below) and the roll back in. They are there "ostensibly to prevent a count-out": no count-out is taken away
  (the RULES page's stands), but he is back in long before the referee's count (beaten 5 s, or out 10 s in all).
- Lumberjacks may not interfere inside the ring (it may cost a DQ). -> A lumberjack never goes in nor onto the apron:
  one who ends up there goes straight out (a QA check: under 5 s in all, all lumberjacks together).
- A competitor may get back in by himself at any time (the user's rule, as in the real matches). -> the lumberjacks never
  stop him: once he is climbing or sliding in, on the apron, or at the apron facing in, they stand off.
- Lumberjacks are often split into faces and heels, each side attacking the other's man; a brawl among the lumberjacks
  is common. -> brawls, between rivals only (below). Faces / heels: the face and heel lists exist only as the stories'
  compiled roster (mod/gen/roster_groups.json -> stories\_roster.json, read by the exe for story conditions), not in the
  match: not used (the user: rivals only).
- Lumberjill matches (women) and Canadian (straps) / Extreme (no DQ) variants exist; not built (the user: no).
- Tornado tag: all four in the ring at once, no tags needed. -> The game's own tag rule "tornado" (the RULES page's tag
  row 10 = 5: rules 0x40).

**Slots and roles** (the model of section 2; nothing of the Royal Rumble's match machinery):

| slots | role | side | state | rule |
|---|---|---|---|---|
| 0 .. c-1 (the type's own: c = 2 one on one, 3 triple threat, 4 tornado tag) | COMPETITOR | the type's | LEGAL | the type's own decision |
| c .. c-1+N (N 2..30, F2 default 12) | EXTRA, job LUMBERJACK | his own (8 + slot) | LEGAL, never a side (+0xE9 0x10 clear) | RINGSIDE: never counted |

`match_slots_add_at(base, n, ids, looks, rule, wait, job)` takes the base explicitly (c) and a per-slot job. Up to 32
present (the engine's cap): a tornado tag with 12 lumberjacks is 16 slots, a one on one with 30 is 32. Every lumberjack
is a real match slot: his own match record, moveset, model, AI, collision. Triple threat (match flags 0x8000) and the tag
match (0x2000, with rules 0x40) are the game's own types; no Rumble flag or machinery.

**What happens, in order:**
1. THE COMPETITORS' ENTRANCES: the game's own, as for the type (the front end's entrance player, ovl_b, before the match
   overlay loads). Only the competitors get them: the front end plays entrants 0..3 of the type only (a one on one's 2
   and 3 skipped), and a lumberjack is never put in slot k + 2 when he is competitor k's "Accompanied by" (the front end
   walks that man out with k as a valet: func_80109A34) -- swapped with a later lumberjack.
2. THE LUMBERJACKS' INTRO (lumberjack5; the user, after playing build 33: "The lumberjack entrances is too long. Can we
   show a side angle of the ramp (not the stage entrance) where we seem them walking down the ramp in one shot? One after
   the other."): the lumberjacks are match slots, so they do not exist during the front end's entrances -- their intro is
   in the match, after the competitors' entrances and before the bell (the user, 2026-11-21: "It's ok that it comes after,
   before the match"). At the match's set-up each is hidden (show_model off) and parked out of sight; the lead-in
   (func_80114514 state 1, its count 0x801591E0) is held at 27 while ONE camera, at the side of the ramp (330 to the side,
   300 above it, a little toward the ring), looks at the stretch of the ramp from about 120 to 720 up from its foot by the
   ring (z 530: every arena's ramp runs up the aisle from there; its height the arena's map, ringstage_ground_at). The
   lumberjacks walk down it one after another by their own sticks (x 0, toward the ring): each sets off just above the
   stretch (out of the frame) when the one before is 90 down, and a man who closes to 70 behind the one ahead (men walk at
   their own pace: the moveset's walk) stands a step, so nobody walks into anybody; past the bottom of the stretch he is
   hidden again. When the last one is half-way down the shot, the camera cuts back to the game's, every lumberjack is at his
   post round the ring, and the bell rings (12 lumberjacks: about 8 s; at most 14 s). The camera's place was tried on RAW,
   SmackDown, WrestleMania and Nitro (b2 - b4: lower or farther out, one of the crowd's signs by the aisle fills the shot on
   SmackDown and RAW; this place clears them). The camera goes in where the game
   makes the frame's view (announce.c qa_camera_mul, the QA camera's hook; lumberjack.c lumberjack_cam). (lumberjack4 showed
   them one at a time at the stage on a close shot, 17.5 s for twelve: "too long"; rounds 2 and 3 walked them down the
   aisle and round a walkway on the Rumble's walk-in shot: "a Royal Rumble attempt entrance". The arena's walk areas were
   never changed.) QA: lj_a's high half, the closest two lumberjacks walking down the ramp.
3. POSTS: evenly round the ring on the floor (the square 400 out from the middle, clear of the ring steps; the apron ends
   about 310), facing in. Each lumberjack starts the match as a fresh man: stamina 255, every body part whole (50), not
   hurt, and FULL spirit (the user) -- set at the set-up and again as the intro ends, so it is 100 at the bell.
   (Why they looked hurt before -- the user's question: an extra slot's start comes from the slot's 8 bytes at 0x8051BFC0
   + 8 x slot (set_up_wrestler_slot, func_80114DBC), which the menus fill only for slots 0..3; slots 4 on started from
   whatever those bytes held: stamina anywhere from 5 to 240 and EVERY body part at 0 -- the game's hurt walk and stance --
   and spirit 50 drifting low. slots.c now writes the game's own reset values there for every extra slot: any match with
   extras gets them.)
3a. NO DAMAGE BETWEEN LUMBERJACKS (the user, 2026-11-21): a lumberjack's move on another lumberjack -- a brawl, or two of
   the swarm round a man -- plays in full (the move, the bump, the sell), but does him no damage. After each step a
   lumberjack whose damage went up gets back what he had (stamina and its cap, the five body parts, the hurt walk's part,
   his time down, his spirit) when the man who did it was a lumberjack; a competitor's hit stands. Who did it (lumberjack5):
   a strike that landed this step names its striker in the man's record +0x00 (apply_strike_to_target); otherwise a move's
   other man, his target (set_pair_states; apply_move_damage takes the attacker from it); otherwise the nearest man.
   (lumberjack3 / 4 took the nearest man alone; in a crowd of lumberjacks round the player one of them was often nearer and
   the player's own hit would have been put back.) (A hook on apply_move_damage itself never ran: the converted move
   states call it directly and it has no compiled hook place.)
3b. STILL AT THE POST (the user: "shaking like they're out in the freezing cold"; after build 33: "We need to solve the issue
   with the lumberjacks twitching in place. It looks odd and unpresentable"): a lumberjack at his post is truly idle -- no
   stick, no buttons, no position or facing writes -- until a push takes him 80 off it (he arrives within 28); the ring turn
   is made once, on arrival. The man he watches -- his target (+0x08) -- is the lumberjack whose post is across the ring
   (lumberjack4): the game turns a standing man to his target every step, and the man across stands still. What the user
   saw (lumberjack5, b1's trace of every step at the posts): the lumberjacks TAUNTING -- the computer player taunts by its
   analog stick (the AI record's +0x12 / +0x13, handed on as the slot's stick: a flick past 0x28 is the standing state's
   taunt input, read_slot_controls), which the job never held: state 27's gestures every few seconds, the computer's D-pad
   let through while "busy" drifting them a unit a step, a run after it (b1: 1279 moves over a unit in 1335
   lumberjack-seconds). Now a lumberjack has the stick only in a fight of his own (the swarm, a brawl), and a taunt is
   never walked or run about in. QA lj_l: steps at a post in any state but standing | taunts begun there << 16; lj_m:
   lumberjack-seconds | moves over a unit << 16 (lumberjack3's reversed-turn count saw 0.003 a second and missed it).
3c. STILL ANYWHERE OUTSIDE THE RING (ljtwitch, 2026-11-23; the user after the 12:18 build: "There's still some twitching",
   "Its not just when they're at posts", "Check when they're idle outside the ring, thats where I notice most"). A per-step
   trace of every man (QA qa_lj_trace 4, the game's log's "ljf" lines; scratch jit.py) found what the posts-only measure
   never looked at -- and every cause was this code's walk fighting itself or the game's computer player:
   - THE STEP IN TO THE POST: a post is reached from the walkway's point (470 out) by a 70-long step in, but a man counted
     as on it only within 30 of either end; a step in, he was sent back out to the walkway, then in again -- his D-pad
     turned round every step, the walk files switching each step. Lumberjacks stood twitching by their posts for seconds to
     the rest of the match: 236 of 1287 lumberjack-seconds in the user's line-up's first match, 1725 of 4300 in its third.
     Now the whole stretch (on_post_leg) is the step in.
   - THE WALK: the D-pad keeps last step's way while it is within 39 degrees of the way wanted (walk_dpad: no zigzag
     between two of the eight ways); a walk that makes no headway for 12 steps stands 1.5 s (unblocked) -- the walkway
     blocked, he goes straight in after; blocked three times on his way home, he stays where he is (a man thrown out past
     the barricade walked on the spot every 2 s for good). The ring steps are at the ring's corners only (crosses_ring: the
     whole square out to 395 counted as steps, and a walk to a man by the apron turned between two corners every step); the
     corner just reached is not walked back to.
     (ljtwitch2) The ring's box (325) reached past the apron's edge (310), so a man on the floor hugging the apron
     stood inside it and every walk from there "crossed the ring": to a corner, a step out of the box straight again, a
     step in to the corner again -- a roller after his man lying by the apron turned two ways every step (the merged
     build's lumberjack_twitch, match 2: 15 events a second). The box is now drawn no further out than the walker and
     his goal, less 8, never inside 300.
     The ring steps' zone starts 290 out (250 took in the floor by the apron near each corner), and a walk under 160 is
     never sent round for them. A roller by his man lying waits once within 95 and walks up again only past 125 (he never
     reached the 50 he walked for -- the body keeps a man 60-100 off -- and pressed on round him for half a minute a
     match); while he waits he faces the man's place, turned at most 0x100 a step (the game aims a standing man at his
     target's body point, which swings tens of units a step as a lying man rolls and rises).
     (ljtwitch3) A walk's memory (straight in, the corner just reached, the blocks) is kept while the job is the same:
     ljtwitch cleared it every step for every job but the post's, so a roller by a corner chose it, reached it, chose the
     far corner, and chose it again a step off it (the packaged 55ebdb1's lumberjack_twitch, match 3: 17 events a second).
     THE THROW'S SIDE (the packaged 55ebdb1's lumberjack_p1: player 1, held by the apron at (33, -341) by a roller OUTSIDE
     him, thrown OUT to (30, 90, -588) and left at ring height off the ring two minutes). The game's throw-in throws the
     man over the thrower: in every throw that landed in the ring in the round-6 traces (17 of 17) the roller stood at the
     apron's edge (308-335 out), the man as far out or farther (317-390), the roller facing out or across. Plan 103 walks
     the pair toward the ring; with the roller outside it is the man who reaches the apron, and the throw goes out. Now
     its C-up goes through only with the roller no farther out than his man (+10), and then with the D-pad -- the throw's
     way, pick_whip_target -- set into the ring from the man's side; otherwise the C-up waits, and after 2 s he lets go
     (R alone, the break-off) and the next attempt takes the man again.
     (ljrole, round 7: the walk memory and the C-up hold-back are gone with the steering; the same rule -- the roller no
     farther out than his man (+10), and facing out or across (60 degrees or more off the ring's middle) -- is now the
     role's check at plan 103's C-up step: else a grapple move (plan 26) instead of the throw, and the next attempt. Item 11.)
   - THE MAN WATCHED must stand still: the game turns a standing man to his target every step, and when the man across left
     his post the watchers followed his every step (3432 reversed turns, all but 33 of them so). Now a watcher whose man is
     off his post takes another lumberjack who is at his, the nearest to the point across, and keeps him. No facing set on
     arrival (it jumped up to half a turn in a step); at the intro's end each faces his man already, held 3 steps (the
     game's angle to him was one step stale: 30 degrees one way and back at the bell).
   - THE COMPUTER PLAYER'S "SECOND" WALK: the game takes a man at ringside (+0xE9) for a second, and on the floor gives him
     plan 71 / 72, the walk to a second's place -- into the apron, on the spot, for seconds (the swarm, the roller closing
     in). In a lumberjack's own fight that plan is the game's own approach to his man (plan 48), and by the apron his D-pad
     never points into it. The roller: near his man from 150, far again past 190; round to the man's front from past the
     arc, back once well inside it; a front spot on the apron is not walked to (the grab from where he is, then the turn to
     the front); at the spot he stands. A computer-played man sent back in keeps his side and its middle stretch (40 either
     way). A computer-played competitor's man among the lumberjacks on him stays the same while that one is on him. The
     intro's walker who stood behind the man ahead walks on only 30 further off (one threshold: a stand and a walk every 8
     steps).
   QA (lumberjack.c qa_jitter, every man every step, by what he is doing): turns reversed within 1/3 s (both turns 3 of 4096
   or more), walk / stand files switched back within 2/3 s or restarted, states switched back, steps reversing the last one's
   way. lj_p idle outside the ring (at the post, anywhere on the floor in no job, walking back to the post) | lumberjack-
   seconds << 16; lj_q the intro's walk; lj_r on a man / brawling / leaving / busy / whipped; lj_s the competitors out
   (report-only: mostly the game's own). Scenario lumberjack_twitch (the user's twelve, qa_lj_set 1; three matches) and
   lumberjack_p1: at every match's end idle at most 1 a 5 lumberjack-seconds, the intro 1 a second, on a man 4 a second.
   937b34d: idle 6-11 a lumberjack-second.
4. SWARM: a competitor on the floor (+0x82 & 4), not on his way back in, has the nearest lumberjacks go for him -- 2, or 3
   with ten or more: their target is he, and the game's own computer player fights him (its own moves). On the floor a
   lumberjack's C-up is held off (he never climbs in) and his C-right (he keeps his man).
5. BACK IN BY HIMSELF: at any time, player or computer. A competitor climbing in (111), sliding in (155), walking in
   (126), on the apron, or at the apron facing in is left alone: the swarm stands off.
6. THE ROLL BACK IN (lumberjack4; the user, 2026-11-21: "There is a way, CPU grapples and rolls the player in" and "We
   shouldn't have to set where a lumberjack can roll the player (or CPU) in.. The CPU typically will grapple the player,
   walk (while in grapple) towards the ring, and then roll them in"; and "they should know to throw them in more often
   (but after getting some attacks in)"). WHEN is ours: a short beating -- LJ_BEAT_HITS (3) of their hits landed on him
   outside, or LJ_BEAT_TIME (4 s) out, whichever comes first; then the nearest lumberjack (LJ_ROLLERS 1 -- with two the game made
   it its own double-team move) goes for the grab, and stays on it from attempt to attempt (pick_rollers); a failed attempt (no grip, the hold broken,
   the grapple gone into another move) is followed by the next one at once (LJ_RETRY, a third of a second), without
   another beating; a roller farther than 150 off walks over by our route, near him with him down he waits right by him
   (no moves of his own on a downed man), with him up his own computer player closes in and within 70 is given the
   game's grapple plan (plan 1); the same for the player and the computer's men (a computer-played
   man still out at 13 s climbs in by his own buttons, the last resort). Then the others step back and one lumberjack,
   the roller, gets hold of him -- the game's front grapple (A, while he is on
   his feet; any lumberjack who has hold of him then is the roller). Then the roller is given the game's OWN
   computer-player plan for it, on his own record (set as ai_set_plan sets a plan): ai_plan_103_whip_with_stick -- in a
   grapple outside, turned to the ring's middle, the man walked in the grapple, lined up, C-up with the side's stick --
   and his buttons are that plan's. The game decides where and when, as for its own computer players: no distance of
   ours. Its throw (pick_whip_target with the apron's wall ahead: the pair 15 / 16) lands the man in the ring (b16: The
   Rock from (394, 0, -88) to lying at (137, 90, -86); b18: the player's man thrown in, then attacking at once). All of it
   is the roller's own: for the PLAYER's competitor (a controller's: NM_SLOT_HUMAN) nothing of his -- state, target,
   buttons, plan -- is ever written; the game's own move carries him in, and he is free the moment it ends.
   If the hold ends on the floor all the same (he breaks it, the tie-up runs out), they try again (above). Rounds 2-3 forced the game's walk to the ring (126) and the slide under the
   bottom rope (155) on him: the slide took its way in from the wrong wall record mid-climb and carried Triple H along
   the apron at ring height (the user's "my opponent went off the ring while staying at ring level"), and the forced walk
   took the player's controls ("I can't attack anymore"). A man's target is only ever written while he is free
   (standing, walking, running): in a hold or a move it is the other man of the pair, and b13, writing every
   lumberjack's each step, broke pairs -- two competitors held for good, one carried off through the walls. Any other
   lumberjack holding a man 4 s ends it with his own grapple move.
   FROM BEHIND (lumberjack5; the user, after build 33: "Lumberjacks, when they grapple from behind, try to roll the
   opponent into the ring from the back grapple. This actually makes them try to put the opponent on their shoulders (for
   like a Doomsday Device top-rope move). They should know that if they're in a back grapple, they need to flip the
   opponent to a front grapple first."): a grab of a man facing away is a rear grapple (the grabber's +0x78 | 0x80,
   pick_grab_outcome), and there C-up is the shoulder lift for a top-rope team move (129 / 130, the CPU's plan 112), not
   the throw; the game's own computer player never takes plan 103 from behind (ai_try_grapple_c_up_plan skips +0x78 &
   0x780). The game's way to turn a rear grapple to the front is R, the pick-up button (the user: "I believe it's the
   same button to pick them up from the ground"; the tie-up reads R alone, count_down_grapple_tie_up: its break-off plays
   the man round to FACE the grabber and lets him go -- b15: the two 2048 apart, face to face). So a roller who has his
   man from behind presses R, then grabs him again at once from the front, and the roll goes on (plan 103, the throw). The grab plan is given only from in front of the man (within
   67 degrees of the way he faces; else the roller goes round to his front, by his side first), and a computer-played
   man's target is his roller meanwhile, so he turns to him. QA lj_o bits 16-19: steps a roller holding from behind had
   C-up (never), 20-23: holds from behind; lj_l bits 20-23: of those, turned to face him.
7. BRAWLS: lumberjacks brawl among themselves -- only rivals, by the game's own data: each moveset's "Rival 1", "Rival 2",
   "Rival 3" (the Create-a-Wrestler ALLY / ENEMY page, ally_enemy_page_run func_801119AC: abilities 31-33, 12-bit IDs at
   bits 149 / 161 / 173 of the packed abilities, read from each slot's match record +0xBC); two are rivals when either
   names the other (the stock data names 99 pairs among 64 men, half of them one way only). Each 10 s window with nobody
   out of the ring has a 10% chance of one brawl (one rival pair at their posts, for that window; a man coming out ends it:
   the job first). With no rival pair among the lumberjacks there are no brawls. Imported folder wrestlers carry no rivals
   (their abilities 31-34 are 0), and the built-in Revenge ports' four are their allies (mkroster.py): since lumberjack3 the
   rivals are roster-tags' shared rule (roster.c `roster_rivals`: wrestlers\rivals.json -- Revenge's real feuds, WrestleMania
   2000's own Rival 1-3 -- for the imports, else No Mercy's own Rival 1-3).
8. NEVER IN: a lumberjack who ends up in the ring or on the apron goes out: to the nearest ropes and out through them (C-up
   at the ropes, state 110); off the apron outwards.
9. The competitors: in the ring a computer-played competitor's target is an opponent (a lumberjack picked from inside is
   put back to the nearest competitor of the other side); on the floor, alone, he may fight the lumberjacks back; with his
   opponent out there too he goes on fighting him. The player's man: his own, always (lumberjack4).
10. STRIKING A LUMBERJACK (lumberjack5; the user, after build 33: "I can't seem to land striking attacks on lumberjacks"):
   the cause was the lumberjacks' body-part radii (0x80188950 + 0x70 a slot), which the strike and grab tests read -- a
   blow lands when a striking point comes within the two men's radii. The game computes them in state 0 and the Rumble's
   walk-in (compute_body_part_radii, func_80120560) from the model's bounds; for a lumberjack that ran before his model
   was built, and they stayed 0 (b13's trace: the player 13.5, every lumberjack 0), so no strike or grab ever reached one.
   lumberjack_step now runs the game's own function for a lumberjack whose radii are still 0, once his model is there
   (b16: player 1's hits on lumberjacks 28 / 47 / 32 in three matches, from 0). A hurt on a lumberjack is put back only
   when a lumberjack did it (3a). As ever in No Mercy, a man faces his target (his focus): the game focuses the player on
   a lumberjack who hits him, and C-right moves his focus (retarget_nearest_wrestler: every man in the match, lumberjacks
   too). This code never writes the player's target (lumberjack4). The lumberjacks defend as any computer player does.
   QA lj_l's top byte: player 1's hits on lumberjacks kept.
11. THE ROLE (ljrole, ROUND 7, 2026-11-23; the user: "What if you integrate it into the CPU AI, their role.. but only in
   Lumberjack matches so it doesn't leak out elsewhere"). Rounds 2-6 steered each lumberjack every step -- his D-pad,
   facing, target, plan -- on top of his computer player, which went on choosing (mostly plan 33, back to the ring for the
   count); every boundary of that steering flipped step to step, and ljtwitch / ljtwitch2 patched them one by one. Now a
   lumberjack is a ROLE of the game's own computer player, as its other men outside the match are: the run-in man after his
   run-in (+0xE9 bit 1) and the manager or valet placed outside (+0xE9 bit 0) get ai_choose_for_second_man (func_800FD828)
   -- the walk down the aisle to ringside (plan 71), to a corner side (72), or no plan at all: standing, turned to his man
   (+0x08 = +0x0C), watching; the manager fights back once punished enough, while his man is near. (The guest referee has
   no computer role: no plan counts a pin.) The computer player's step (run_cpu_step, func_800F03F0) runs the slot's plan
   (the plan table 0x80145A10); with none, ai_plan_0_choose -> ai_choose_by_place chooses. The role is decided at that
   point, after each step (the step's return, slots.c match_slots_cpu -> lumberjack_cpu): when his plan has ended or his
   job changed, the role gives his computer player the next of the GAME'S OWN plans, which then walks, turns, faces and
   collides him as it does any computer player's man -- watch at the post plan 55 (no buttons; his man the still
   lumberjack across the ring); walk home plan 60's walk (its step 1 holds +0x10 for +0x18 steps: the way set once a leg,
   round the ring by the walkway's corners); to a man outside a walk leg toward him (plan 48, the game's walk up, holds its
   eight-way D-pad at him every step: a zigzag); beat him: the game's own attack choice, ai_choose_by_target_situation
   (func_800F1EA4), with its plans into the ring, onto the apron, off the ropes or the turnbuckle, the weapon hunt, the
   second's walks, the wait-and-watch and the walk up put aside (a strike or a leg instead); the roller: straight to his
   man, plan 19 (pull up) when he lies, plan 1 (grab), then plan 103 (the throw), which walks the pair to the apron and
   lines them up itself; its C-up step only from the throw's own geometry -- the roller no farther out than his man (+10)
   and facing out or across (60 degrees or more off the ring's middle): the game's throw goes OVER the thrower, and from the
   other side it lands the man outside at ring height (the floating player of 2026-11-23 16:22; ljtwitch3's 17 good
   throws), so else a grapple move (26) instead and the next attempt; a non-roller's grapple never throws (102 / 103 ->
   26); held from behind, R (item 6); out of the ring plan 36; the intro's walk plan 71 (the run-in's walk) and 55 to stand
   a step -- his man the intro's ANCHOR, one lumberjack still to come or already through, hidden, standing at the ramp's
   foot (z 540): a stick walk faces the man, so they face down the ramp, and the fight-on-stage steering (ringstage_ai,
   run inside the computer player's step) leaves a man on the ramp alone unless his man is by the ring (then it walks him
   to the aisle's mouth and runs him once lined up). The hide point is 610, 20 higher than round 5's. WHAT and WHEN stay ours (the beating, the throw, the retries -- items 4-7); no lumberjack's D-pad,
   stick, facing or target is written step by step: his man is set when a job is decided, his place and facing once at the
   intro's end (each put at his post out of sight), and R pressed only while a roller holds his man from behind. Competitors
   and the player: the stock computer player -- the one write kept, a computer-played competitor in the ring whose man the
   game made a lumberjack gets his opponent back (else plan 36 takes him out to that lumberjack for good), and the 13-s
   last-resort climb-in (item 6). THE GATE: lumberjack_cpu runs only from match_slots_cpu with a Lumberjack match's slots
   live (ms.job MS_JOB_LUMBERJACK) and set up (lj_live), and the role only for its lumberjacks (match_slots_is_extra); the
   log's "lumberjack: the role" line at each match's end counts its decisions, and any call outside a Lumberjack match.

**Picking the lumberjacks** (F2 > LUMBERJACK MATCH): LUMBERJACKS 2..30 (default 12); PICKED: RANDOM or SELECT; MATCH: ONE ON
ONE, TRIPLE THREAT or TORNADO TAG (the match the Lumberjack Match row plays). F2's are the defaults; since ljround8
(Regular) the menus set them for the match too -- the Lumberjack MATCH page below.
- RANDOM: drawn from the Superstar Select's pages (the pool the Royal Rumble's random field is drawn from: the Superstars
  the game lets into a random field, one version of a man), never a competitor (nor another version of him), never a
  woman, valet, manager, announcer, referee or official -- imports too, by their class (roster.c `roster_not_drawn`) --
  whatever the Rumble's NO WOMEN / ANNOUNCERS says, each in a look the select shows.
- SELECT: the game's own Superstar Select takes the competitors' picks and then N more -- the player picks each
  lumberjack there, as for the type's own (the select's picks wanted, 0x800A9582, raised to c + N while it is up; the
  type's set-up takes picks 0..c-1, the lumberjacks are picks c..c+N-1 from its records 0x800A95A0). Anyone the player
  picks may be a lumberjack, women and announcers too.

**Menu flow** -- (ljround8, 2026-11-23; the user: "How do we select triple threat? Tornado Tag? How do I select
lumberjacks? ... its ok if Lumberjack match goes on the previous screen (with Ladder match in regular mode). In arcade it
would just stay in Versus.") Regular: **Multi Play**, the row **Lumberjack Match** under Ironman Match, as Ladder Match is
there (the page's definitions copied into the mod's memory with one more row: logic.c `multiplay_lj_defs`; DOWN from
Ironman Match reaches it, and from it round to Exhibition). A on it brings up the **Lumberjack MATCH page**: the game's MATCH
page (screen 7) built by the game's own page set-up (init_exhibition_page) from a row list of its own, as the game builds
Exhibition's, Ladder Match's or Pay Per View's MATCH page from theirs (ljmenu10: while the match is asked for, Exhibition's
list, 0x8014FE08, names the page's rows -- set at the press, before the page is built, and given back as the request ends:
logic.c `lj_page_data`; nothing is drawn over Exhibition's rows, so no frame ever shows Handicap or Cage): Single Match (a
one on one), Tag Match (a tornado tag) and Triple Threat Match, then the match's two settings, **Lumberjacks: 12** (LEFT /
RIGHT or A: 2..30) and **Picked: Random / Select**, starting from F2's (`lj_regular_pad`). A on a type is that type's own row, with the Lumberjack match asked for at those
settings (lumberjack.c `lumberjack_menu_set`); B goes back to Multi Play. Arcade: Versus's MATCH page, the seventh row,
right after Ladder Match (DOWN from Ladder Match reaches it by the page's own links); its press plays F2's MATCH type, as
before. The row's picture: "Lumberjack Match" in the labels' own lettering -- letters cut from the ROM's labels
(gen/labeltex.py `make_squeezed`: L a r "Match" from Ladder Match, u m b e from Royal Rumble, c k from Smackdown Mall, the j
the i of Exhibition with a tail), squeezed to 114 columns as the game's long rows are drawn narrower. Being made from
the ROM, it is shipped zeroed and made again by the first start from the player's ROM (exe arcade_firstrun_labels.cpp,
mkplan's labels port, as Custom Story's) -- until that port had it the installed game showed the row's glow with no letters. (Rounds 1-7 drew it with the
game's text maker, condensed and smaller -- the user: "text is too small", "looks odd".) The settings rows' pictures
(ljmenu10, the user: the text "needs to match the text of the other menu items exactly") are made the same way, in the
labels' own lettering: P from Pay Per View, i from Exhibition, d from Ladder, s from Guest Referee, the colon and its space
from "Ladder: Single", "Random" and "Select" the ROM's own labels whole; the labels have no figures, so the count's are the
game's NUMBER page's (file 0x4588) at the labels' capital height; on Single Match's palette (gen/labeltex.py
`label_lj_count` / `label_lj_pick`, made again by the first start: arcade_firstrun_labels.cpp). (Round 8 had the text
maker's, taller and bold, drawn on Handicap's and Cage's rows each frame.) The label 0x852E is lent while its page is up;
the settings rows are the kinds whose labels are 0x8525 / 0x8526 (Ladder Match's "Ladder: Single" / "Ladder: Tag", never up with this page),
their pictures ours while the request lasts. Its press goes to the row of the match F2 names -- Single Match, Triple Threat or Tag Match
-- and everything is that type's: PLAYER, ARENA, RULES, the Superstar Select, the card, the match. A tornado tag has the
RULES page's tag row "tornado" only while the match flags are made (func_80136784), then put back as the player left it.
The match card's caption is **"Lumberjack Match"** / **"Lumberjack Triple Threat"** / **"Lumberjack Tornado Tag"** (the
card's text ids 0xF0F0 / 0xF0F2 / 0xF0F1, through the text lookup the mod already replaces: story.c story_card_text). The
results screens (28, 29) show no match type in the stock game: nothing to change there. A rematch keeps the Lumberjack
match and its lumberjacks; choosing another row of MATCH, or leaving the page, ends it.

**Round 8** (ljround8, 2026-11-23; the user after playing the role build: "Lumberjacks are working much better, but ...
They don't attack enough. They attack the player more than the CPU. They should try to throw them into the ring 60% of the
time, but about 40% of the time should try to do a grapple move to cause damage", the escape loop, and the intro's pauses):
- AGGRESSION: the swarm (2, or 3 with ten or more lumberjacks, as before -- r3's 3 / 4 walked into each other) stays on a
  man while his roller goes for the grab (it stood off as soon as the roll began). The game's own attack choice is asked
  as before, but on a competitor its passive answers -- guard, back off, sidestep, taunt, wait, walk about, run
  (`passive_plan`) -- are replaced by the role's attack (`attack_now`): from 85 or nearer a strike, 40% a grab when in
  front of him, the strike at a man down (22) when he lies; farther, a walk leg up to him.
- THE HOLD: any lumberjack with a competitor held from the front -- the swarm's as well as the roller's -- decides once a
  hold: the throw in (60%, LJ_THROW_PCT: he becomes the man's roller and plan 103 throws him, its C-up checked as in round
  7) or a grapple move (40%: plan 26, the game's own move, damage); after a move the next attempt takes him again. A
  grapple move only on the floor by the ring (no farther out than 450, not in the aisle's mouth): by the barricade or in
  the aisle the game's moves left the man lying across the barricade's top or at the ramp's height for a second (the
  tornado tag's float check, r9 / r13) -- there the hold is the throw in, or let go, and the split is kept to 60 / 40
  over the match, not drawn at random (throw, throw, move, throw, move ...; a move the zone refused is made up at the next
  hold that may). And no grab with either man's area
  still the ring's or the apron's while he is out on the floor (thrown over the top), a hold so let go with R.
- THE ESCAPE LOOP: a grab only from in front of the man (within LJ_FRONT_ARC of the way he faces): from behind the game's
  grab is a rear grapple, and a man on his way in elbowed out of it, again and again. Behind him, a strike instead (the
  game turns a man to whoever hits him, or he goes down to be pulled up face to face); a grab plan under way whose man
  has meanwhile turned his back is ended the same way. A hold from behind that happens all the same -- any lumberjack's,
  not only the roller's -- is turned at once with R (the user's rear grapple -> R turn -> front; pressed the hold's first
  step). And the real rule as the boundary: a roller still going for the grab lets the man go once he is on his way in by
  himself (`escaping`: climbing, on the apron, at it facing in), as the swarm already did. QA lj_v: holds from behind a
  man heading back in got out of (not turned to face), and spells with two of them -- the loop.
- LEAVING THE RING by the place too: a lumberjack thrown over the top lay and rose on the floor 300-420 out with the game's
  area still the ring's, and the leave-the-ring plan walked him about the floor (r5's trace, 21 jitter events a second):
  the EXIT job only within the apron's footprint (330).
- THE INTRO: men walk at their own pace (round 8's log: 5.2, 5.9, 6.7 a step -- not the model's size: Big Show 5.2), so a
  faster man set off 90 behind a slower one closed on him and stood, and the men behind him in turn -- the user's pauses.
  They walk down now in TWO FILES (36 either side of the aisle's middle line), setting off into them in turn, still one
  after another 90 apart; a man sets off only once the man ahead in his own file is far enough down that not even the
  fastest walk (7.0) could close on him before he leaves the shot -- his pace measured as he walks (5 a step for his
  first 4). A faster man passes a slower one in the other file, 72 to the side; nobody stands. The stand-a-step stays as
  the guard (in his own file). QA lj_w: stops after walking (0 wanted).
- QA lj_t / lj_u: the lumberjacks' hits on competitor 0 / 1 outside, his seconds out; the log's "aggression" lines add the
  lumberjack-seconds on each man and the rates.

**Round 9** (ljround9, 2026-11-24; the user rejected round 8's ways round the float: "all I wanted was just regular back
grapple functionality ... if they're fighting a player on the outside, they might want to back grapple. That's just regular
game behavior that needs to stay"; "If that doesn't happen in a regular match when they're outside the ring, it shouldn't be
happening here. It's just regular fighting outside the ring. CPUs can do whatever moves they want"):
- REMOVED (round 8 and before): grabs only from in front of the man, a strike instead of a grab from behind, a grab under
  way ended when the man turned his back; the grapple-move zone (370-450 out, not the aisle's side) and the throws it
  forced; the let-go with R of a hold out of the zone, of a hold with a man's place bits "stale", of a hold on no job; the
  roller letting the man go once he climbs in. A lumberjack grabs from the front or from behind and does the game's own
  moves anywhere, as any computer player.
- KEPT: the role (plans at decision points), the aggression, the 60 / 40 at the hold -- now every hold of a competitor,
  front or behind: the throw in about 60% (from behind: R turns him to the front first, and the throw stays decided for
  that roll through the turn and the grab again), the game's own grapple move about 40% (from behind: its rear grapple
  move); the throw's C-up line-up rule (the game throws over the thrower); the menus, labels, intro; the hold following
  the man held.
- THE ROOT CAUSE of "a man left lying at ring height off the ring" -- found with a place-bit trace (logic.c
  qa_area_trace_step: every man's +0x82 as it changes, and each grapple move's files with their end level and mark frame)
  in a one-on-one, a tornado tag and a Lumberjack match: the game sets a man's place bits from his animation's end level
  (the record's byte 0: 1 / 7 the mat, 3 the apron, 2 the floor, 0 "where he is") once the file reaches its mark frame
  (read_slot_controls -> set_place_from_end_level), and his height from it (settle_height_after_animation). Every No
  Mercy grapple move begun on the floor ends "where he is" (the traces: 55 moves outside, every attacker and man file 0;
  the game's own files that end on the mat are its moves from the apron), so a move outside ends outside. The moves brought over from other games -- the Revenge and WrestleMania 2000 libraries, a folder's own
  (the exe's pool numbers) -- can end on the mat: 32 of Revenge's 783 library moves, 5 of WrestleMania 2000's 256; Lex
  Luger's front grapple revenge:126c_1270 (the slam that goes on into the Sharpshooter) has its man's file end at 1 with
  mark frame 103. Round 8b's event A was that move by Luger as a lumberjack: at frame 104 of the man's file the game made
  his place the ring's 500 out by the ramp's foot, then handled him by the ring's rules (height 90, the ring's walls: put at
  (-19, 90, 352)) until the void guard dropped him. Not the slots past four, not the walkway, not the ring stage's lift,
  not a lumberjack as the mover: the same move does the same in a one-on-one and a tornado tag. The Lumberjack match only
  made it common: its lumberjacks have SPECIAL from the bell, are drawn from the whole roster (the imported Superstars
  too) and do the game's grapple moves outside all the time.
- THE FIX, at that root (movex.c keep_place, in the mod's reader of an animation's details, anim_details / func_80011BD8):
  an animation whose record the exe gave (a library move's or a folder's own) started for a man on the floor outside ends
  where he is, its end level read as 0 -- as No Mercy's own moves do. The game's own files are never changed; in the ring or
  on the apron an imported record is as it was. QA qa_place_keep_off 1 turns it off (the before).
- QA: lj_x (state.csv): the lumberjacks' grapple moves on a man on the floor | by the apron's edge (under 370 out) << 8 | by
  the barricade or in the aisle (430 or more) << 16 | from behind << 24; checks in lumberjack_p1 / lumberjack_twitch5 that
  they do them, and grab from behind (lj_w >> 24). The trace scenarios area9_* / area9f_* (qa_outfight: the same two put
  on the floor at six spots every 10 s; area9f: Luger's grapple moves forced to his entry 15) -- for the proof, not checks.
- QA measures put right on the way (lumberjack_p1_triple failed both; r6's trace): "at their posts the lumberjacks stand
  still" counted the intro's end putting each man at his post (12 "moves" a match, 400-670 in a step) and a man standing
  beside him pushing him a step at a time (61-84 away: the game keeping two men apart) -- both now counted apart (the
  log's "pushed by a man beside him"), his own moves checked; and "never C-up from behind" now means with the throw in
  decided (the role turns him with R first, over whatever the game's plan pressed -- r6: a swarm lumberjack's hold from
  behind, the throw decided, and the game's own plan 112, the shoulder lift, pressing C-up that step); in a hold left to
  the game's own move the shoulder lift is the game's choice, counted apart in the log. And "the CPU as much as the
  player" is judged over the run (lj_y), not one match's handful of hits (r8's lumberjack_twitch5 match 5: the CPU 4 hits
  in 57 lumberjack-seconds, the player 21 in 135; the run's whole: 7 in 87 against 42 in 484).

**Round 10** (ljround10, 2026-11-24; the installed real-time lumberjack_p1 had 5.1 / 5.3 hits a minute on player 1 out
against 6, and no grapple move by the barricade or aisle; then the user: "Lumberjacks shouldn't be idle when a wrestler is out
of the ring in striking distance ever", and on the ramp or stage "they should rather try to irish whip them back to the ring
area"):
- WHY THE RATE WAS LOW (QA "on a man" breakdown in the log, every match): not real time against lockstep -- the match's logic
  steps over the game's frames is 1.14-1.15 in both, and real time is only another, non-deterministic replay (one build's real
  time matches ranged 3.0 to 11.9 a minute). The lumberjacks on a man spent 31-44% of their steps down or hit themselves,
  15-20% on walk legs: they came at him from one side in a file, and three quarters of their strikes were begun on a man
  reeling from a blow (45) or in a move -- 1 in 10 landed, and 30-119 a match landed on each other.
- THE ROLE NOW (choices at decision points, no restriction on any move): the lumberjacks on a man come at him from sides of
  their own (his place among them: straight out of the ring, a quarter-turn and more either way, the ring's side where there
  is floor); an attack is put off while a fellow lumberjack stands in its way (to his own side first); no blow is begun on a
  man reeling, coming up or in a move -- the next once he can take it, and meanwhile a blow at another competitor in reach; a
  walk leg that brings him in reach ends in the attack; a stray blow that made a mate his target puts him back on his man at
  once. With that, over half their strikes land and lumberjack-on-lumberjack hits fell to a handful a match.
- NEVER IDLE IN STRIKING DISTANCE: any lumberjack within 100 of a competitor out there (floor, ramp, stage) is put on him,
  whatever he was doing (his post, the way home, another man out of his reach); the swarm keeps a man in reach though he
  escapes or walks in (only the roll's own hold and throw, a move in progress, are left alone); the roller strikes a man he
  can't grab, and another man in reach meanwhile. QA lj_z: spells of 2 s a lumberjack stood within 85 of a man out there who
  could be hit, not on him -- checked 0 at every match's end (half a second at first; the user: "Half a second is ok.. I
  just meant extended idle", b143c55). A running lumberjack with no plan gets the blow too (90f9823).
- THE RAMP AND THE STAGE: the roll-in there is the game's whip toward the ring -- in the hold the stick toward the ring's
  middle and C-up, what the game's own whip plan (103) presses once lined up, without its walk to the apron; the man runs off
  down the aisle and the floor's roll-in takes over. Up the aisle past its mouth counts as the ramp (RAW's aisle is floor-high
  all the way to its stage). The 13-s last-resort walk-in is not started there. QA: lumberjack_stage_raw / smackdown /
  nitro / wrestlemania, a man put on the ramp's middle or the stage's front every 15 s (qa_of_stage).
- BARRICADE / AISLE MOVES: the men are out there (50-260 s a match at 430+ in the traces) and moves are 40% of the holds
  wherever they are; a 3-match lumberjack_p1 has 2-5 holds there, so none by chance is possible. Not the role's: the check
  is now in the stage scenarios (several holds up the aisle every match), report-only in lumberjack_p1; the QA player walks
  out to the barricade's foot on every other trip.

- LATER IN ROUND 10: a blow at another competitor in reach while one's own man can't be hit (strike_other), left to the game
  only while it is under way (20 steps standing with it: decided again); a roller whose walk to his man on the stage makes no
  headway strikes him in reach; a role-level backstop (standing with the wait or no plan, a hittable man out there in reach:
  the blow). State 57 -- the stay down after lying, 49 -> 57 -> 50, about 3 s -- is protected: blows begun on it never land
  (r18: 3 of 51), so the lumberjacks wait by a man in it. Open after round 10: the aggression floor per match is not met in
  every match (lockstep lumberjack_p1 6.1 / 5.2 / 7.6: in match 2 player 1 is down, staying down or held ~60% of his time
  out); twitch5 match 3 had 3 idle spells the backstop did not catch (most likely a mate in the strike's line -- not yet
  traced); lumberjack_p1_tornado showed the CPU partner running at ring height beyond the ropes for 15-19 steps (put back by
  the game) -- not yet traced.

**Reused from the Royal Rumble**, and only these: the record build per entrant and the slot fill (section 2, through
`match_slots_add_at`), and the random field's pool of the select's pages (`arcade_rr_offer`, non-wrestlers kept out).
NOT its flag, entrances, eliminations, HUD, results or AI.

**Engine change for this** (arena_ringstage.c): the ramp / stage height lift (`h_now`, RS_SLOTS) was per slot for slots 0..5
only (the six-man engine's), so a man in slot 6+ on a raised stage or ramp walked at y 0 inside it -- now every slot the
engine has (32; the stage door's test still looks at the first six, as before).

**Decided by the user (2026-11-21), the round-1 questions:** 1 back in by himself any time + rolled in by them (the
game's own move if any: none -- built from its pieces, above); 2 RANDOM or SELECT; 3 the real rules (above); 4 brawls 10%,
rivals only, from existing data; 5 no women / announcers unless picked; 6 Arcade: after Ladder Match; 7 default 12, in
F2; 8 triple threat and tornado tag too, past four natively; 9 the card's caption; 10 one group walk-out, only the
competitors' own entrances.

**Decided by the user (2026-11-21), round 2:** faces / heels: no -- rivals only; Lumberjill / Canadian / Extreme: no;
the walk-out after the competitors' entrances, before the bell, stays (no rework); rivals for imports: wrestlers\rivals.json
and a shared rivals API, with a class for every import (woman, valet, manager, announcer, referee, official) that every
random pool keeps out -- roster-tags' work, in use here since lumberjack3 (`roster_rivals`, `roster_not_drawn`); no damage between lumberjacks (their moves still play); full spirit at the start.

### Survivor Series elimination / 3-on-3 .. 8-on-8 tag (design only)
How it fits the Lumberjack's model: the same slots and per-slot fields; the partners are COMPETITORS (a side each,
`ms.side`), not extras -- one LEGAL a team, the rest WAITING in their corner (a corner place, as a lumberjack's post is a
place round the ring: `ms_place`); ELIMINATED men walk to the back (as a lumberjack is kept out of the ring, an
eliminated man's buttons are his job's: the ring's exit, state 150, then the Rumble's exit walk). A per-slot job
(`MS_JOB_*`) is where a waiting partner's corner behaviour goes; the type's own decision over sides is the one new piece:
- Sides: `ms.side[slot]` (2 teams of 3..8 = 6..16 competitors). The stock tag sides are match-flag bits 0x10..0x80, one a
  slot (4 slots only) and the decision's `teamA_bit[]`: past slot 3 the side must come from the per-slot side, not
  the flags (the decision's m_22000 branch reads `MF & teamA_bit[i]`: a per-slot side table in its place).
- Legal / waiting: one LEGAL man a team, the rest WAITING -- the stock tag's +0xE9 4 ("waiting outside", the start
  place on the apron: `func_80115000`'s `E9 & 4` branch puts a waiting man at x 100 z -100, facing -0x200) and the
  tag-in (`non_legal_man_free_to_act`, `try_tag_partner_save`, the swap bits of 0x8016B650). Per partner: a corner
  place -- the apron of his team's corner for the first two (as the stock tag), then the floor beside it, in a line
  along the apron (x +-(180 + 40 k)), facing the ring.
- Eliminated: a pin, submission, DQ or count-out on a LEGAL man sets ELIMINATED (as the ELIMINATION rule does for an
  extra now: taken before the type's fall decision), he walks to the back (the Rumble's eliminated man's exit is the
  piece to reuse), his team's next WAITING man becomes LEGAL.
- The end: the last side with anyone not ELIMINATED wins -- a decision over sides (the rewritten decision already has
  lists of n + 1; it needs the side table instead of `teamA_bit`).
- Stock rules to reuse: the tag match's legal / waiting / tag-in (above); the Royal Rumble's elimination (a slot's
  `func_8011723C`, the mod's `arcade_elim_check`) and its last-one-standing count (`kind 0x800000`: still-in men,
  +0xE9 0x10 and not +0x94 0x200). The stock game has NO team elimination match and no tag rotation past two a side.

## 5. Results

QA runs of the final build (qa.ps1, 6x, every wrestler the computer's; extras stand at their places 10 s -- tag 2 s --
then join in; `bs_*` columns of state.csv: slots.c `match_slots_qa`). Every run PASS: no crash, no bad position, the
match ended by its own decision and the game went on to the menus and quit by itself.

| scenario | match | present (engine count) | competitors | extras: acted / took hits / grappled | match frames |
|---|---|---|---|---|---|
| any32_singles6 | one on one | 6 | 2 | 4 / 4 / 4 | 9,185 |
| any32_singles12 | one on one | 12 | 2 | 10 / 10 / 10 | 8,859 |
| any32_singles32 | one on one | **32** | 2 | 30 / 30 / 30 | 9,862 |
| any32_triple10 | triple threat | 10 | 3 | 7 / 7 / 7 | 10,570 |
| any32_tag8 | tag | 8 | 4 | 4 / 4 / 4 | 12,619 |

"Acted": the computer player pressed buttons for him (its own move choice; the mod holds only a waiting extra still).
"Took hits": his stamina went down. "Grappled": he was in a grapple (states 80 / 81). The HUD showed the type's own
panels only (singles: the two; tag: the four). Pins: no computer player covered an extra in any run (section 2).

## 6. What changed (for the conversion pipeline's re-test)

- Game functions newly hooked by the mod (lift.py marks them "Changed for new features"; until the next night re-converts
  them with their hook places, the loader runs their hooked translations): `build_match_records` (func_80136CC8,
  converted -- return hook `match_slots_on_records`) and `set_up_match_memory` (func_80113E30, translated -- return hook
  `match_slots_on_memory`).
- Mod functions changed: logic.c `load_settings`, `qa_demo_setup`, `qa_state`, `arcade_match_step`, `arcade_rr_hud`,
  `arcade_rr_match_setup`, `arcade_overlay_loaded`; hooks_recomp.c `arcade_on_cpu_done`; tron.c `unpatch_all` and the
  new `tron_forget_patches`; every function of the new slots.c. Any change to logic.c moves the mod's later code, so
  the night's converted copies of the mod's functions (modrep) are re-made from this build, as for any mod change.
- QA only: options `qa_extras`, `qa_extras_rule`, `qa_extras_wait` (mod.toml), scenarios `qa/scenarios/any32_*.json`.
- Lumberjack (lumberjack2 / lumberjack3, 2026-11-21): no game function newly hooked. Mod: every function of lumberjack.c;
  slots.c `ms_apply` (each extra slot's start bytes 0x8051BFC0 + 8 x slot written; an extra whose record can't be built
  left out), `match_slots_on_records` and the new `ms_fix_competitors` (a competitor whose record can't be built gets a
  stand-in) and `match_slots_step`; logic.c (F2 rows, settings, the select's picks, the Arcade row, finalize hooks, QA); story.c
  `story_card_text`; arena_ringstage.c `RS_SLOTS`. exe: crash_report.cpp's state.csv columns lj_a .. lj_m (QA only).
- Lumberjack round 4 (lumberjack4, 2026-11-21/22): no game function newly hooked. Mod: lumberjack.c throughout; announce.c
  `qa_camera_mul` (the intro's camera, `lumberjack_cam`, when no QA camera is on -- stock unchanged: it is 0 outside the intro);
  arena_ringstage.c the new `ringstage_ground_at`; logic.c the QA option `qa_lj_p1` and its pad-hook call (QA only). exe:
  crash_report.cpp's state.csv columns lj_n, lj_o (QA only).
- rumblecrash (2026-11-22): no game function newly hooked. Mod: slots.c `match_slots_on_records` and the new
  `ms_fix_rumble_entrants` -- the Royal Rumble gets the empty-record guard too (its records are its entrant list's, which the
  slot-based guard never looked at): an entrant on no roster (a test's list, a story, a list kept from before) gets a
  stand-in on the list and in his slot instead of an empty record that crashed walk_step_length when he walked
  (music_rumble_late on an install without No Mercy Plus). logic.c (QA only): the port draw asks the roster; options
  `qa_rr_first` (mod.toml) and `qa_rr_out` with a played player 1. Scenarios rumble_absent_entrant, rumble_p1_play.
- Lumberjack round 5 (lumberjack5, 2026-11-22): no game function newly hooked; lumberjack.c now CALLS the game's own
  compute_body_part_radii (ovl_e func_80120560) for lumberjacks. Mod: lumberjack.c throughout (the ramp intro, the AI
  stick held at the posts, the turn from a rear grapple, damage attribution, QA traces); logic.c reads the QA options
  `qa_lj_trace`, `qa_lj_behind`, `qa_lj_camx` / `camy` / `camin` / `gap` (QA only; `qa_lj_p1` now up to 15). No exe change;
  the state.csv words lj_a / lj_l / lj_o carry new meanings (section 4). New scenarios: lumberjack_intro_raw /
  _smackdown / _wrestlemania / _nitro, lumberjack_behind, lumberjack_trace.
- Lumberjack round 7 (ljrole, 2026-11-23): no game function newly hooked; lumberjack.c now also CALLS the game's own
  ai_choose_by_target_situation (ovl_e func_800F1EA4) for a lumberjack's beating, and gives plans on his own AI record
  (as ai_set_plan does) from the existing return hook of run_cpu_step. Mod: lumberjack.c only (the steering replaced by
  the role, item 11 of section 4). No exe change. QA: scenario lumberjack_twitch5 (five matches).
