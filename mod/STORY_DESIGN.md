# Story runner -- design (user chose "B": our runner; the game's 7 stories stay as they are)

## Shape
- A story is JSON (stories\*.json in the game folder, or a built-in template). The exe reads + checks it
  (template gaps filled from the roster, at random) and compiles a STORY PACK: the game's own Story Mode data
  (STORY_NOTES.md formats) + our extras. The mod plays the pack through the game's Story Mode engine.
- Game presentation for free: chapter cards, narration pages, 3D scenes (cast, places, animations, speech,
  player choices), match card + requirement text, the match, retries, money.

## Pack -> the engine
- text: ids 0xE000+ (story.c: story_card_text / story_scene_text hooks)               [proved]
- nodes (18 B), parts (12 B), scenes (6 B), scripts (4 B commands): written over the overlay's own tables
  while the runner is active (ovl_a: 0x800F8FA4 nodes, 0x800FBE68 parts, 0x80102030 scenes; ovl_b: 0x80125CF8
  scripts), re-applied on each overlay load, the originals put back when the runner ends.   [match set-up proved]
- branches: func_800EE1F0 (branch choice) hooked: our evaluator -> a game-format 8-byte record (next node,
  money, scene after). Conditions: the game's own (func_800ED958(bit): win/lose, pin-only, time, weapons,
  choices, titles ...) + our variables + chance.
- roles: the engine's cast variables 0x7EEC[k] (k = 0..7; part code 0x8000 + k): player, partner, rival ...
  our "vars" that hold Superstars map to these; a betrayal = a branch that moves the partner into the rival role.
- run-ins: the game's own: a part's extra low byte (0x0002, 0x0003, 0x0006 the player runs in) with match
  types 14 / 15 / 18 / 19.                                                                [testing]
- titles: story-local (our variables); the card's "title on the line" (node +C) with the engine's champion
  copies 0x7EEC.. set from ours; the save's real champions untouched (save.c restores them).

## JSON (authoring) -- first cut
{ "title", "championship" (which belt's slot it runs in), "vars" {name: "pool:..."/id/number},
  "start", "scenes": { id: { "chapter", "pages"[], "scene"{place, cast, lines[{who, text}], choice{q, options[]}},
  "match"{type, sides, runins[{who, side}], arena, title, rules, requirement}, "branches"[{if, set, next}] } } }

## Done (2026-10-31)
- 3D scenes ("scene" on a node / a branch: places, cast on screen, animations, speech bubbles, questions and
  "if choice N", camera, music), choices in branches ("choice N", from 1), match rules / title / backstage
  location / two run-ins / handicap run-in, the champion roles. Both compilers (storyc.py, arcade_stories.cpp),
  same bytes; no story.c change was needed (its hooks and install already cover scripts, scene text and the
  choice byte). mod\stories\README.md for authors, data\storyscenes\SCENES.md for the engine.
