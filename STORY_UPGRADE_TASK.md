# Task: Story Mode upgrade for No Mercy Arcade

You are working in the No Mercy Arcade project (`<outside>`, development tree in `Development\`). Custom
stories are JSON files that are compiled into the original Story Mode engine's own data format (nodes, parts,
scenes, scripts), which the recompiled game then plays. This task extends what stories can do. When it's done,
it produces a new authoring prompt (AI_STORY_PROMPT.md) that the user will give to another AI to write stories.

## Read first

- `Development\README.md`: building, QA, the folder layout
- `Development\mod\NOTES.md`: the technical notes, especially "Story creator" and the 2026-10-31 sections
- `Development\mod\STORY_DESIGN.md`, `Development\mod\STORY_NOTES.md`: the runner's design and the decoded engine
- `Development\data\storyscenes\SCENES.md`: the scene script ops, with evidence
- `Development\mod\stories\README.md`: the current authoring guide
- Code: `mod\src\story.c`, `mod\gen\storyc.py`, `exe\WWFNoMercyRecomp\src\main\arcade_stories.cpp`,
  `mod\src\tron.c`, `mod\src\announce.c`, `six-engine\` (the six-man engine)
- The stories in the game's `stories\` folder and in `mod\stories\` (the regression set)

## Hard rules

1. **No new assets.** Don't add 3D objects, sets, models, animations, textures or Superstars that aren't already
   in the game. Use only the existing roster, arenas, the 7 scene sets, the announce table that's already in the
   arena, the existing animation and taunt list, the game's own weapons, referees, entrance videos and photos.
   People who aren't on the roster may appear only in narration text.
2. **No 2D overlays for now.** Name plates, logos and other on-screen graphics are on hold until the user decides.
   Don't build them.
3. **The game's own 7 stories must behave exactly as before.** With no pack running, every patched function runs
   the game's code, as NOTES.md describes now.
4. **Both compilers stay in lockstep.** `storyc.py` and `arcade_stories.cpp` must produce byte-identical packs for
   every story. Add a check that compiles every story with both and compares the bytes.
5. **Backward compatible.** Every existing story (the 4 in `stories\` and the samples in `mod\stories\`) must
   still compile and play as it does now. Old syntax keeps working; new syntax is added alongside it.
6. **The QA rule in `README.md` holds.** Nothing is called done until a QA scenario that exercises it passes in
   the real game, and you have looked at the report and the screenshots. Write a scenario for each feature.
7. **Build and QA, but don't deploy.** The user play-tests first. Bump the version and the changelog in
   `mod\README.txt` as the project does, and record what you did in NOTES.md.
8. **If you're unsure what the engine can do, investigate it** (rig, disassembly, RAM captures) before writing
   code. Expose only what you've proved. Leave anything unproved out of the authoring prompt, or mark it
   EXPERIMENTAL there.

## Work, in order

Finish each phase, including its QA, before starting the next. At the end of each phase, write its status
(done, partly done, or not possible, and why) into a running report, `Development\STORY_UPGRADE_REPORT.md`.

### Phase 1: recast when the player picks a cast member
- A `cast` role may take a list: `"taker": ["UNDERTAKER", "KANE", "BIG SHOW"]`.
- When the roles are installed after Superstar Select, if a role's first choice is the player's Superstar (or
  another role has already taken that Superstar), use the next one on the list.
- The swap applies everywhere: match slots, run-ins, scene casts, "who" in scene steps, and `{role}` names in
  text. A plain string role works as before.
- QA: a story played as a cast member's Superstar shows the stand-in in scenes, text and the match.

### Phase 2: branch conditions
- Add `or`, `not` and parentheses, so conditions read like `"win and (heat >= 2 or choice 1)"`.
- Add how the match ended. Use what the hooked `func_80118A20(winners, losers, how)` and the result flags really
  report: pinfall, submission, DQ, count-out, and anything else proven. Expose them as terms such as `win by
  submission` and `lose by dq`.
- Add who the player is:
  - `player is STEVE AUSTIN`
  - `player in <group>`, with groups defined in `_roster.json`. Seed it with dx, nwo, ministry, corporation,
    hart foundation, apa, the outsiders, and face / heel as 1999-era defaults.
  - `role is <Superstar>` for any role.
- Branch evaluation is already the mod's own code (`func_800EE1F0`), so this phase is mostly the compilers and the
  evaluator.

### Phase 3: voice packs (player-specific dialogue)
- Add a voice table for every roster Superstar, in `_roster.json` or a `voices.json` next to it. Fields:
  - `nickname`
  - `catchphrases` (a list)
  - `signoff` (for example "And that's the bottom line, 'cause Stone Cold said so!")
  - `finisher` (its name)
  - `insult` (for example "candy ass", "brother", "chico")
  - which in-game taunt animations are his
- Write era-accurate text for the main eventers and sensible defaults for the rest. Keep every field within the
  text limits (about 36 characters a line).
- Text tokens: `{player.nickname}`, `{rival.finisher}`, `{player.signoff}`, `{player.catchphrase}` (a random
  one), and so on, for any role or roster name. Fill them in at run time, where the story text is served
  (`ours()` in story.c), since the player is only known after the select. Line wrapping must still fit.
- Per-line variants: `"say": { "THE ROCK": "...", "group:dx": "...", "default": "..." }`.
- `"do": "taunt"` plays that Superstar's own in-game taunt.

### Phase 4: scene logic
- Scene `if` steps take the full condition language from phase 2, including variables and `player is` tests, not
  only `choice N`. That needs a new script op, or a wider 0x7F op, in the script runner hook.
- `"store": "varname"` on an `ask` saves the answer, so a later question doesn't overwrite it.
- A `set` step inside a scene changes variables.

### Phase 5: six-man story matches
- Find out what the six-man engine can run as a story match: 3-on-3 tag, elimination, 2-on-3 / 1-on-3 handicap,
  six-way, gauntlet. Expose only what plays correctly.
- Add a `teams` syntax: `"teams": [["player", "partner", "ally"], ["a", "b", "c"]]`. The old `slots` stays.
- Stand-ins are resolved across the whole match, so no Superstar fills two slots.
- Raise the cast role limit if the engine's cast variables allow it. Document the real limit.

### Phase 6: verify the existing match types
- The authoring guide says only `single` has been played in the game. Write QA scenarios for tag, triple,
  handicap, four-way, cage, ladder, ironman, run-ins (both sides, two in a single), title matches, every rule set,
  and every backstage location.
- Fix what's broken. If something can't be fixed, mark it so it can be documented as unsupported.
- Bring the arena list up to date. Stories already compile with Monday Nitro and Halloween Havoc, but the README
  lists only 10 arenas.

### Phase 7: Titantron scenes
- Add a scene step that shows a Superstar on the arena's Titantron while a Superstar in the ring talks to him:
  `{ "tron": "boss", "say": "..." }`. It uses existing pictures only: the Superstar's photo, his entrance video,
  or tron.c's live picture of an existing backstage set.
- The man on the tron doesn't count toward the 4 on screen.
- Build a capability table: which arenas have a working screen. Account for the open placement fixes in
  `Titantron Placement QA\To Fix` and Bash at the Beach having none.
- In an arena without a usable screen, the compiler falls back. It cuts to the speaker in the hall, or turns the
  lines into narration. It warns; it never rejects the story.

### Phase 8: commentary cutaways
- Add a scene place, or a step, for JR and King at the announce table, which is already in the arena
  (`announce.c`). They say speech-bubble lines. If it's feasible with clips already in `commentary.pak`, a line
  may also play a matching existing clip.

### Phase 9: mid-match triggers (largest; investigate first)
- Add a `script` list on a match. Conditions:
  - a Superstar's health below a share
  - match time
  - a finisher hit
  - a submission applied
  - the referee down
  - a Superstar out of the ring
- Actions:
  - a run-in (beyond the engine's two, if the six-man engine allows)
  - a referee bump
  - a referee swap to another existing referee
  - a speech bubble with play paused
  - end the match with a set result (the screwjob: "ring the bell" on a submission)
  - a change of rules (for example, it becomes No DQ)
  - one of the game's own weapons into the ring
  - lights out, only if the game already has a lighting state it can switch to
- Prove each trigger and action in the rig and then in QA. Ship the ones that work and list the rest in the
  report.

### Phase 10: authoring tools
- When a story fails to compile, show it on the Custom Story list with the reason (it must not just vanish), and
  write the reason to the log.
- Make the compiler messages specific: node, step, what's wrong.
- Add a JSON Schema for the story format (`mod\stories\story.schema.json`), so editors give autocomplete and
  errors.
- Update `mod\stories\README.md` to match everything above.

## Final deliverable: a new AI_STORY_PROMPT.md

Write `Development\mod\stories\AI_STORY_PROMPT.md`. It's the prompt the user gives to an AI to write a story.
If it's practical, generate it from the schema and the roster data, and keep a script that regenerates it.

- **Keep the current prompt's shape:**
  - an opening paragraph
  - OUTPUT RULES (one JSON object, nothing else)
  - TOP LEVEL
  - A NODE
  - MATCH (types, slots / teams, rules by type, arenas, locations, run-ins, script triggers)
  - BRANCHES (the full condition language)
  - SCENES (places, steps, actions, tron, commentary, scene logic, limits)
  - VOICE TOKENS
  - ROSTER (exact names, groups, and for each Superstar his voice fields, so the writer can match his cadence)
  - SAMPLES
  - CHECKLIST
  - MY STORY (an empty section at the end for the user's story idea)
- **Document only what passed QA.** Mark anything partial as EXPERIMENTAL with its limits. Leave out what failed.
- **State every limit exactly as the compiler enforces it:**
  - characters per line and lines per bubble
  - Superstars on screen and named per scene
  - roles
  - chapters
  - variables
  - run-ins by match type
  - which arenas have a tron
- **Rules first.** Its first section says plainly that a file breaking any rule is rejected, and that the writer
  must re-check the checklist before answering.
- **Samples that compile.** Include at least two, compiled by both compilers in the test: one small, and one
  that shows the new features (a recast role, voice tokens and variants, a stored choice, a scene `if` on a
  variable, a six-man match, a tron scene with its fallback, a mid-match trigger, a `win by submission` branch).
- **Write it for an AI that has never seen the code:** plain, exact, no engine addresses.

## When you finish

Reply with:
1. The phase-by-phase summary from `STORY_UPGRADE_REPORT.md`: what works, what's EXPERIMENTAL, what isn't
   possible.
2. The QA runs that prove each feature.
3. The path of the new `AI_STORY_PROMPT.md`.

The user will bring the new prompt back to the AI that writes the stories.
