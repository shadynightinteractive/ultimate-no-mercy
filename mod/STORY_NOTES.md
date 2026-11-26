# Story Mode engine (ovl_a + ovl_b) -- what's decoded (for the story runner, "B")

Disassembly: `python3 /home/devbox/work/six/mdis.py func_XXXXXXXX` (db.pkl: ovl_a / ovl_b / ovl_c / ovl_e).
RAM dump with a story page up: /tmp/devbox-0/ram_story.bin (0x80000000.., 8 MB); state /tmp/devbox-0/storypage.st.

## Runtime state (0x80097ED0..)
- 0x7ED0 u8  chapter counter (+1 per node advance), 0x7ED1 u8 championship (0 World .. 6 Women's; 7 = ?)
- 0x7ED2 u8  the match result (written by ovl_b func_8010DF98)
- 0x7ED3 u8  retries used on this node
- 0x7ED4 u16 current node index; 0x7EE4 u16 next node (from the branch); 0x7EE8 u16 scene to play
- 0x7ED6 u16 phase of the story state machine (0 start node, 3 scene, 4 match setup, 5 after match,
             9 next node, 0xB done) -- func_800EEFE0 / func_800EEE28 drive it
- 0x7ED8 s32 money; 0x7EDC money for this node; 0x7EEC.. champion ids per title, 0x7EFC.. costumes
- *0x80097F14 = pointer to the current node (func_800ED130(index): 0x800F8FA4 + 18 * index)

## Node (18 bytes at 0x800F8FA4 + 18 n)
- +0 u16 text id: the CHAPTER TITLE on the chapter card ("Chapter 1 / Royal Rumble Qualifying") (proved)
- +2 u16 scene before the match (0x7EE8)                               (func_800EEFE0)
- +4 u16 text id: the requirement text over the match card ("Normal Single Match. There are no set...") (proved)
- +6 u16 first match part (participants: 0x800FBE68, 12 bytes each; func_800EC554(part, i))
- +8 u16 branch list (0x800FA720 + 8 * n; func_800EE1F0 evaluates, returns the chosen 8-byte record)
- +A u8  lo: arena; bits 4-6 -> func_800EE3C4 (rules?); bit 7 flag (0x801064C4)
- +B u8  (func_800EC554 reads it)
- +C u8  lo: title on the line (belt + 1); 0x10 flag
- +D u8  match type (participant count 0x800F8D98[type]; rule bits via jump tables 0x801036E8/0x80103728)
- +E u8  -> func_800E2BC0 (music / entrance?)
- +F u8  lo: retries allowed; hi: money cut per retry (x10%)
- +10 u16 flags: & 0xF0 requirement (0x10 must win, 0x20/0x30/0x40: result 1/2/3), & 0x8, & 0x4
- Start node per championship: table 0x800F8E90, 28 bytes each (+0 start, +2 alt start (champion?)),
  chapter boundaries 0x800F8E94 (func_800ECC00).

## Branch record (8 bytes, 0x800FA720 + 8 n; a node's list = consecutive records, first byte 0 ends?)
- +0 condition index -> 0x800F8DC4 table of u32 condition masks (func_800EE1F0 checks bits per field)
- +1 effect (-> 0x800F8DAC table -> func_800D9A10)
- +2 u16 money (scaled by retries)
- +4 u16 next node (0 = retry / game over by +F)
- +6 u16 scene after the match

## Text
- func_800DD740(id) -> string. ids 0xF0CE..0xF1CA: pointers at 0x800B6F58 + 4*id; else 15 ranges at
  0x800F3938 {u16 first, u16 last, u16 bank file}; func_800DD6F4(file, id - first): the file (cached by
  func_800DF3BC) starts with u16 offsets, strings follow. Codes: %c01 %c02 (wrestler names), \n.
- PLAN: hook func_800DD740: a range of our own (e.g. 0xE000..) -> the runner's strings.

## Scenes
- Scene table 0x80102030, 6 bytes: u16 first part (cast, via func_800EC554), u16 script start, u8 cast count.
  func_800ED628(scene) loads the cast (models) and sets 0x7EEA = script start.
- ovl_b func_8010E648 plays it; func_8010D29C(pos) runs commands at 0x80125CF8 + 4*pos until one waits:
  byte0 hi nibble = op, lo nibble = actor / arg; byte1 = wait / count; u16 +2 = param.
  - 0x10 location / camera (lo: place; +2: transition 0..3)
  - 0x20 actor animation (lo actor, +3 anim (table 0x8012588C), +2 >> 8 ...)
  - 0x30 / 0x40 speech: speaker = lo, text id = +2 (0x80133C80)
  - 0x50 effects (sounds 0x15 / 0x4A / 0x49, ...)
  - 0x60 music? (+2 -> 0x80133C60)
  - 0x70 conditional (func_8010D1E0 checks the match result 0x7ED2) -> skip byte1 commands
  - 0x80 participants (func_800EBE0C)
  - 0x90 set a champion (0x7EEC / 0x7EFC from the cast)
  - 0xA0 money (+2 signed, 0x8000 = minus)

## Proved in the rig (story.c + /tmp/devbox-0/sttext.py, stmatch.py)
- Text: story_card_text (func_800DD740) and story_scene_text (func_8010CC38) serve ids 0xE000+ from our strings:
  chapter title (node +0), requirement text (node +4), narration lines (script 0x32 param) all showed ours;
  %c01 in our strings became the player's name.
- Match: node +D = 1 is "Tag Match"; node +6 -> 4 parts {0x8000 player, 0x0609 Taka, 0x0001 Rock, 0x0002 Austin}:
  card "JEFF JARRETT / TAKA MICHINOKU vs THE ROCK / STEVE AUSTIN", the match starts. Parts: u16 cand[5]
  (0x8000 player, 0x8001 partner?; else IDs, the first not excluded: champions 0x7EEC/0x7EEE/0x7EF0/0x7EF2),
  +0xA u16 -> func_800EC354 (extra), node +0xB too.
- Wrestler IDs: 0x0001 Rock, 0x0002 Austin, 0x0609 Taka.

## Results
- 0x7ED2 is NOT the match result: it's the player's CHOICE in a scene (op 0x40: a question, 0x6682 = options;
  D-pad up/down moves 0x801066D8, A confirms -> 0x7ED2). Branch conditions 0x10000/0x20000/0x30000/0x40000 =
  choice 0/1/2/3; node +10 & 0xF0 likewise.
- Match result facts: 0x800A10EC (u32 flags: 0x1, 0x2, 0x4, 0x10, 0x40, 0x4000, 0xA0000 ...), 0x800A0E68 (flags 4..0x40),
  0x800A0DF1 / 0x800A0DF2 (bytes), func_800ED860 / func_800ED91C (win / lose?), team stats 0x800A3DC2 / 0x3DD6.
  func_800ED958(cond) tests one condition bit (1 = true, -1 = false): the game's own vocabulary.
- Plan: also capture from the match itself (func_80118A20(winners, losers, how), hooked already) who won and how.

## Scenes, choices, match options (2026-10-31; data\storyscenes\SCENES.md has it all, with the evidence)
- Scripts: byte1 = wait (0 none, n frames, 0xFF till done / A). 0x1P place P (0 black, 1 hall, 2 locker room,
  3 boiler room, 4 pool hall, 5 parking lot -> sets 20, 24, 23, 21, 22; 6 the arena's stage, 7 its ring); clears
  the men on screen. 0x2A actor A: param mark << 8 | anim (0x8012588C; marks 0x801259AC x, y, z, facing).
  0x30 text: lo = box (0 speech bubble at the top, 2 narration on black, 3 / 4 bottom). 0x4N question, N options
  (the text's last N lines) -> 0x7ED2 = the answer from 0. 0x5E effects (7 scene music on, 8 off, 3 nothing).
  **0x6M is the camera, not music**: M = the actors framed (4 bits), param = a fixed camera path of the set.
  0x7F if (F & 8: the choice == param; F & 1 inverts) -> skip byte1 commands.
- At most 4 men on screen; cast entries past 4 are names only.
- Node +A bits 4-6 the match's set (5 ring, 6 stage, 0..4 backstage sets 20 + n; bit 7 with them: the game's
  backstage brawls 0x90 / 0xA0 / 0xB2). Node +E = the rule preset (0x800F5E78 + 12e; byte 0 the format): each
  match type needs its own (tag 0x1A, triple 0x23, handicap 0x25, four-way 0x28, rumble 0x29, cage / ladder 0x2B,
  ironman 0x2C; single 0x0F). Node +C = title on the line (belt + 1): the winner -> role 2, the old champion ->
  role 4 (func_800EBE0C).
- Proved in the rig (stock game, a pack's tables installed as story.c does, its strings by a func_8010CC38 stub):
  all six places, speech bubbles with %c names, a 3-option question and the answer in 0x7ED2, then / else on it,
  a walk-in, a single and a tag entrance on the stage, the camera shots (data\storyscenes\shots).
