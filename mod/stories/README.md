# Custom stories: authoring guide

A custom story is a JSON file in the game folder's `stories` folder (`<outside>`). The game
reads that folder each time you open the Custom Story list: no rebuild, just save the file and open the list
again. Files whose names start with `test_` or `_` are skipped (`_roster.json` there is the list of Superstar
names the stories may use: don't edit it; `_voices.json` the Superstars' voices). A story that doesn't compile stays
on the list as `ERROR: <file>`: pick it to read the reason. The reason is in `NoMercyRecompiled.log` too (a line starting
`[stories]`), and names the node, the step and what's wrong.

`story.schema.json` here is a JSON Schema of the format, made from the compiler's tables by `gen\storyschema.py`: point
an editor at it (`"$schema": "story.schema.json"`) for autocomplete and errors. `AI_STORY_PROMPT.md` is the prompt to give
an AI that writes stories, made by `gen\storyprompt.py` (run it again when the compiler, roster, voices or samples change).

The samples here (`mod\stories\`) are copied to the game's folder by `.\deploy.ps1` (and to the QA copy by
`.\qa.ps1`). The build still checks them with `mod\gen\storyc.py`, which makes the same data as the game's own
compiler, byte for byte.

The game's own Story Mode engine plays the story, with the presentation it uses for its own seven stories:
narration pages, 3D scenes backstage and in the arena (the cast on screen, what they do, speech bubbles, the
player's choices), the match card with its requirement text, the match itself, money, and branches after the
match.

## Playing a story

Coin + Start takes you to Story Mode's championship list. The **Custom Story** row sits in the left column,
under Women's, where the game's "Belt" box was. Pick it: the list shows the stories instead of the
championships, each by its `menu` name, in file-name order, eight at a time (L / R for the next / last eight).
Pick one, then your Superstar. B goes back to the championships.

The operator menu (F2) turns the row on or off: **GAME SETTINGS > CUSTOM STORIES**. With no stories in the
folder there's no row.

The story plays in the slot of the championship named by `"championship"`. While a custom story runs, it
replaces that championship's story. When you return to the list or the title, everything goes back:

- The game's own story for that championship is unchanged.
- A story in progress in your save is untouched. The custom story never goes into the save.
- When no custom story is running, the four Story Mode functions the mod patches run the game's own code
  exactly.

## The file

```json
{
  "title": "The Grudge",
  "menu": "Custom Story",
  "championship": "world",
  "cast": { "rival": "the_rock", "boss": "triple_h" },
  "vars": { "beat_rival": 0 },
  "start": "grudge",
  "nodes": {
    "grudge": {
      "chapter": "THE GRUDGE",
      "pages": ["{rival} has been running his mouth\nabout {player} all week long."],
      "match": { "type": "single", "slots": ["player", ["rival", "kane"]] },
      "requirement": "Singles match: {player} vs {rival}.\nWin, and a title shot is yours.",
      "branches": [
        { "if": "win", "set": { "beat_rival": 1 }, "money": 1000,
          "pages": ["{player} pinned {rival}!"], "next": "title" },
        { "pages": ["{rival} stands tall over {player}."], "next": "rematch" }
      ]
    },
    "rematch": { "...": "..." },
    "title":   { "...": "..." }
  }
}
```

`the_grudge.json` is the complete sample. It has three nodes, a win/lose branch after the first match, and an
ending either way. `backstage_sample.json` adds 3D scenes, a choice, a betrayal and a title match.

### Top level

| key | meaning |
|---|---|
| `title` | The story's name. It appears in the build log and the header comment. |
| `menu` | The story's name on the list. 24 characters at most (a long name is squeezed to fit). Default `"Custom Story"`. |
| `championship` | The championship slot the story runs as. One of: `world`, `tag`, `ic` / `intercontinental`, `european`, `hardcore`, `lightheavy`, `womens`. Default `world`. `tag` gives the player the game's tag partner (role `partner`). Use `world` unless you need that. |
| `cast` | Up to 4 roles of your own, each mapped to a Superstar: `{ "rival": "the_rock" }`. They are set when the story starts. A story with a `title` match has up to 3 (the engine keeps the former champion in the fourth; the engine has 8 role places in all, 4 of them its own). A role can take a list, up to 8: `{ "taker": ["undertaker", "kane", "big_show"] }`. The first one who isn't the player's Superstar or another role's man plays the role, everywhere: match slots, run-ins, scene casts, `who`, and `{taker}` in text. |
| `vars` | Story variables with their starting numbers: `{ "heat": 0 }`. Up to 32. |
| `start` | The first node. Default: the first node in the file. |
| `nodes` | The story's nodes, by name. Each node is one match, with the pages before it and the branches after it. |

### Names

Superstars are named by their **string ID**: `"the_rock"`, `"steve_austin"`, `"kane"` -- the built-in ones are in
`_roster.json` as `"@ID:..."` (made from `mod\gen\roster_ids.json`), a wrestler folder's is its folder's name (or the
`"id"` in its wrestler.json). Never the name on screen: that can change. A Superstar can also be named by his string ID
and his game number together, `{ "id": "aki_man", "game_number": "0x0379" }`: the number when it is his, else the string
ID (a wrestler renumbered by an ID clash is still found). Older stories that name Superstars as on the roster
(`"THE ROCK"`) still compile (startup 2026-11-19: the game's own stories use string IDs).

These roles are always defined:

- `player`: the Superstar picked at the select.
- `partner`: the tag partner, in a `tag` championship.
- `champion`: the champion of the story's championship, as the save has it when the story starts (and whoever
  wins it in a `title` match). `champion2`: the second tag champion (a `tag` or `womens` championship).

### Text

`pages`, `requirement` and `chapter` are plain text. `\n` starts a new line. Both compilers enforce the limits (a story
that breaks one doesn't compile): 48 characters a line, a `{name}` counting 10 and a voice token its longest (below);
3 lines a speech bubble, 7 a narration page, 3 a requirement, 5 a question with its options (an option, in bigger
letters, 28 characters). A line of all capital W's fills the screen at about 40: write in ordinary mixed text.

Text can depend on who the player picked. **Voice tokens**: `{player.nickname}`, `{rival.finisher}`, `{player.insult}`,
`{player.catchphrase}` (one of his, the same one all story), `{player.signoff}` (up to two lines), for any role or roster
string ID. They count 20, 24, 16, 40, and 2 lines of 40. The words are in `_voices.json`, one entry per Superstar plus a
`default`; a costume variant uses his base's.

**Lines by player**: anywhere a line goes, an object picks it by the player's Superstar, first match in file order:
`"say": { "the_rock": "...", "group:dx": "...", "default": "..." }`. `default` is required.

`{player}`, `{rival}`, `{kane}` and similar become that Superstar's name as the game prints it. In
`requirement`, only Superstars named in the node's `pages` or `scene` (or on screen in it) can be named.

### A node

| key | meaning |
|---|---|
| `chapter` | The chapter title (the engine's chapter card). The arcade skips that card, so it isn't seen. Nodes in a row with the same title form one chapter, up to 11 chapters. |
| `pages` | Narration pages before the match, one string per page. Each page moves on after about 5 seconds, or sooner when the player presses A. |
| `scene` | A 3D scene before the match, after the pages (see "Scenes" below). |
| `match` | The match (see below). |
| `requirement` | The text over the match card. |
| `branches` | Where the story goes after the match (see below). |
| `retries` | How many times a lost match can be retried when a branch says `"gameover"` (0 to 15; default 0). |
| `raw` | Expert use only: the engine's node bytes `a`, `b`, `c`, `e`, `f`, `flags` (STORY_NOTES.md). |

### `match`

| key | meaning |
|---|---|
| `type` | `single` (2 slots), `tag` (4: the first two a team), `triple` (3), `handicap` (3: the first two a team against the third -- the game puts the player third), `fourway` (4), `cage` (2), `ladder` (2), `ironman` (2). |
| `teams` | In place of `slots` and `type`: `[["player", "pal"], ["a", "b"]]`. 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1 or four men alone. Nothing bigger: the engine's teams are two men at most, so 3-on-3, 2-on-3, 1-on-3 and a gauntlet can't be made. |
| `slots` | Who is in each slot, in order. A slot is a role (`"player"`, `"rival"`), a Superstar (`"kane"`), or a list of them, `["rival", "kane"]`. With a list, the first one who can be in the match, so the second is a stand-in, for example when the player picked the rival's Superstar. No Superstar fills two slots of a match. |
| `arena` | Where the match is held, any capitals: RAW IS WAR, No Mercy, SmackDown, King of the Ring, SummerSlam, Survivor Series, Royal Rumble, WrestleMania, Backlash, Armageddon, Monday Nitro, Halloween Havoc, Bash at the Beach, RAW IS WAR WM 2000, Souled Out, SuperBrawl, Starrcade, Sunday Night Heat, Royal Rumble WM 2000, King of the Ring WM 2000, Survivor Series WM 2000, WrestleMania WM 2000, SummerSlam WM 2000. Without it, RAW IS WAR. |
| `runins` | `[{ "who": "kane", "helps": "opponents", "at": "0:45" }]`. A Superstar runs in during the match (`"helps": "player"` for the player's side). Up to two in a `single` match, one a side; one in a `handicap` match, for the opponents. `at`: 0:15, 0:30, 0:35, 0:40, 0:45, 1:00, 1:10, 1:30, 2:00 or 3:00 (default 0:30 for the opponents' man, 0:35 for the player's). Until 2026-11-02 a single `"helps": "player"` run-in in fact helped the opponents; the compilers put him on the player's side now. |
| `script` | Up to 16 triggers that act on the match as it plays. See "Mid-match triggers". |
| `rules` | The match's rules, by type. `single`: `normal` (the default), `no dq`, `hardcore`, `submission`, `anywhere` (falls count anywhere), `street fight`, `pinfall only`, `first blood`. `tag`: `normal`, `tornado` (no tags), `no dq`, `hardcore`. `triple`: `normal` (no DQ, as the game's), `hardcore`. `handicap`: `normal`, `hardcore`. `fourway`: `normal`, `no dq`. These are the game's own rule sets, named after its own match cards. |
| `title` | A championship on the line: `"world"`, `"tag"`, `"ic"`, `"european"`, `"hardcore"`, `"lightheavy"`, `"womens"`. The card says so, and the winner becomes the story's `champion` (the engine's story champion, func_800EBE0C). Whether the save's title history changes when a custom story ends with the belt won is not checked yet. |
| `location` | A match backstage instead of in the ring: `"pool hall"`, `"parking lot"` or `"boiler room"` (the game's own backstage brawls). |

### `branches`

After the match, the branches are tried in order. The first one whose `if` holds is taken. A branch with no
`if` always holds, so put it last as the "otherwise" case. If no branch holds, the story ends.

| key | meaning |
|---|---|
| `if` | Terms joined by `and`, `or`, `not` and parentheses: `win by submission and (heat >= 2 or choice 1)`. The match result is `win`, `lose` or `notlose` (the game's own result tests). How it ended is `win by X`, `lose by X` or `ended by X`, with X one of pinfall, submission, dq, countout, tko, ladder, escape, draw, decision. A variable test is `heat >= 2` (with `==`, `!=`, `<`, `<=`, `>`, `>=`). A random test is `chance 50%`. The player's answer to the last question in a scene (`ask`) is `choice 1` (the first option), `choice 2`, ... `player is steve_austin` and `rival is kane` test who a role is; `player in dx` tests a group (`mod\gen\roster_groups.json`: dx, nwo, ministry, corporation, hart foundation, apa, the outsiders, face, heel). |
| `set` | Changes applied when the branch is taken. `{ "heat": "+1" }` adds, and `{ "heat": 3 }` sets. A role can take another role's Superstar, as in a betrayal: `{ "rival": "partner" }`. A role can also take a named Superstar: `{ "rival": "kane" }`. |
| `money` | Money won. |
| `pages` | Pages shown after the match, as for a node. |
| `scene` | A 3D scene after the match (after its `pages`), as for a node. |
| `next` | The next node by name, or `"end"` (the story is over) or `"gameover"` (the match is retried if `retries` allows, otherwise the game is over). |

## Mid-match triggers

`"script"` on a match is a list of up to 16 triggers: `{ "when": ..., "do": ... }`. Each fires once.

| `when` | |
|---|---|
| `time 1:30` | the match clock has reached it |
| `health rival < 30%` | that role's health is under that share (1 to 99) |
| `rival outside` | he's out of the ring |
| `rival in a hold` | he's caught in a submission hold |
| `finisher by rival` | he has just hit his finisher |

| `do` | |
|---|---|
| `"end"` | `"winner"` (a role), `"how"` (pinfall by default, submission, tko, dq, countout, ladder, escape), `"loser"` with more than two men. The match ends as if the game had ended it: the bell, the replay, the results. This is the screwjob. In a `fourway` it only eliminates `"loser"` and in an `ironman` it scores one fall: those matches go on. |
| `"runin"` | The match's run-in comes now, whatever its `at` said (`"side"` when it has both). |
| `"no dq"` | No DQ from now on. |
| `"no countout"` | No more count-outs. |

Not possible (`STORY_UPGRADE_REPORT.md` has why): a referee bump or swap (the game has no referee in a match: the counts are HUD
numbers), a pause for a bubble (it would be a 2D overlay), lights out, a weapon thrown in.

## Scenes: backstage interviews and cinematics

A node can have a `"scene"`: a 3D scene played before its match, made of the game's own pieces (the places,
animations, camera shots and speech bubbles of its seven stories). A branch can have one too, played after the
match. `pages` still work: when a node has both, the pages come first (narration on black), then the scene.

`backstage_sample.json` is the complete sample: an interview in the locker room with a question, a plot in the
hall, a betrayal, a title match and a match in the parking lot.

```json
"scene": {
  "place": "locker room",
  "cast": ["player", "ally"],
  "steps": [
    { "say": "{player}! {champ} sent {heel} after you.", "who": "ally" },
    { "say": "Let him come.", "who": "player" },
    { "ask": "Do you want {ally} in your corner?", "options": ["Yes", "No"], "who": "player" },
    { "if": "choice 1", "then": [ { "say": "Good. I've got your back.", "who": "ally" } ],
                        "else": [ { "say": "Suit yourself.", "who": "ally" } ] },
    { "narrate": "Meanwhile, in the hall..." },
    { "place": "hall", "cast": [{ "who": "heel", "at": 1 }, { "who": "champ", "at": 2 }] },
    { "who": "heel", "do": "laugh" },
    { "say": "Tonight you finish {player}.", "who": "champ", "do": "point" }
  ]
}
```

A scene is a list of steps, run in order. `{ "place": ..., "cast": ..., "steps": [...] }` is the same as a list
whose first step is `{ "place": ..., "cast": ... }`.

### The steps

| step | what it does |
|---|---|
| `{ "place": "hall", "cast": [...] }` | Cuts to a place (table below) and puts the cast there. A cast entry is a role or Superstar (`"rival"`), or `{ "who": "rival", "at": 2, "do": "stand" }`. Without `at`, the first goes on spot 1, the second on spot 2, and so on. The camera frames them. `"camera": false` leaves the camera alone. |
| `{ "say": "text", "who": "rival" }` | `rival` speaks: the camera goes close on him, he talks (with a microphone in the ring and on the stage), and the line appears in a speech bubble at the top. `say` can be a list of lines, one bubble each. The one who spoke before goes back to standing. The player presses A for the next bubble. Options: `"do"` (what he does while talking, instead of talking), `"camera"` (`"close"`, `"two"`, `"wide"`, a shot number, or `false`). Without `who`, it is just the bubble. |
| `{ "ask": "question", "options": ["Yes", "No"], "who": "player", "store": "answer" }` | A question with 2 to 4 options: the player picks one with the D-pad and A. Branches and `if` steps test the answer with `choice 1` (the first option), `choice 2`, and so on. The answer is kept until the next question, so a branch after the match can use it. `"store"` also keeps it in a variable (1, 2, ...), so a later question doesn't overwrite it. |
| `{ "if": "answer == 1 or player in dx", "then": [...], "else": [...] }` | Steps played only if it holds: the whole condition language of branches (`win` and `win by X` there mean the last match). `else` is optional. |
| `{ "set": { "heat": "+1" } }` | Changes variables or roles, as a branch's `set`. |
| `{ "tron": "boss", "say": "..." }` | In the ring: the camera cuts to a close-up of the arena's own Titantron with his entrance video playing on it, the line's bubble over it, then back to the men. An arena with no screen of its own (SUMMERSLAM, SURVIVOR SERIES, ROYAL RUMBLE, ARMAGEDDON): the full-screen cut. `say` can be a list. He doesn't count toward the 4 on screen. Fallbacks, never a refusal: an arena with no screen of its own (TITANTRON ARENAS below) gets the game's full-screen Titantron cut; a man with no entrance video, or a backstage place (a warning in the log), gets the bubble alone. |
| `{ "commentary": "jr", "say": "..." }` | In the ring only: the camera close on JR and King at the announce table, the line in a bubble ("JR: ..." or "KING: ..."). `"jr"` or `"king"`. `say` can be a list. No voice clip plays. |
| `{ "narrate": "text" }` | A narration page on black (as `pages`). A list gives several pages. The next step that needs the 3D scene cuts back to the last place with its cast. |
| `{ "who": "rival", "do": "point" }` | `rival` does something (table below). `"at": 3` moves him to spot 3 first. A man who isn't on screen needs `at`. `"wait": 30` holds the scene 30 frames (60 a second) after it starts; `"wait": true` until it's done. |
| `{ "who": "rival", "do": "walk" }` | Walks in through the place's door (`"door": 2` for its second), the camera on the door. Only the hall and the parking lot have doors. `"run"` runs in. Door 1 brings him to spot 1 (keep it free: put the man he meets on spot 2); the parking lot's door 2 to spot 3, and its door 3 walks out. |
| `{ "who": "rival", "do": "entrance" }` | His entrance on the stage (`place` `"stage"`), music and all. `"tag entrance"` with `"with": "partner"` brings two men out; the partner must be the next man in the scene's cast (name them in that order). |
| `{ "camera": "wide" }` | `"close"`, `"two"` or `"wide"`, on the men named by `"on"` (a role or a list; default: everyone on screen). A number is one of the game's shots (`data\storyscenes\SCENES.md`). |
| `{ "music": "on" }` | The game's backstage scene music on, or `"off"`. |
| `{ "wait": 60 }` | Holds the scene 60 frames (1 to 254). |
| `{ "effect": 1 }`, `{ "raw": "20ff0002" }` | Expert use: the engine's effect command (0 to 10), or one 4-byte script command in hex (STORY_NOTES.md). |

A scene shows at most 4 different Superstars in all, across every place it cuts to (the player included); a
node's scene and each branch's scene count separately. It names at most 8 (the others named only in its text). Names in a scene's
text are as anywhere: `{player}`, `{rival}`, `{kane}`.

### Places

| `place` | where |
|---|---|
| `hall` (or `hallway`) | A backstage corridor. Spots 1 and 2 face each other. Doors: 2. |
| `locker room` (or `dressing room`) | A locker room with benches. |
| `boiler room` | The boiler room behind a mesh fence. |
| `pool hall` (or `sports lounge`, `bar`) | The sports lounge. |
| `parking lot` (or `garage`) | The parking garage. Doors: 3 (the third walks out). |
| `stage` (or `entrance`, `ramp`) | The stage of the match's arena: for entrances. |
| `ring` | The ring of the match's arena, with the crowd. |

Each place has its spots: `"at": 1` and `"at": 2` face each other (the game's own most used pair), 3 and up are
further spots (hall 6, locker room 8, boiler room 4, pool hall 6, parking lot 6, stage 5, ring 10).

### What they do (`do`)

| `do` | | `do` | |
|---|---|---|---|
| `stand` | standing (the default) | `laugh` | laughs |
| `talk` | talking, with gestures | `punch` | throws a punch |
| `mic` | talking into a microphone | `swing` | a big swing |
| `interview` | holding a microphone up to talk | `fist up` | raises a fist |
| `hold out mic` | holds the microphone out to someone | `arms up` | both arms up (celebrating) |
| `shout` | leans in, shouting | `bring it` | arms out: "come on" |
| `point` | points | `hurt` | doubled over |
| `shrug` | arms out: "what?" | `fall` | falls down |
| `rub face` | a hand to his face | `down` | lying down |
| `turn away` | turns his back | `get up` | gets up |
| `drink` | drinks | `walk`, `run` | in through a door |
| `entrance` | his entrance (stage) | `tag entrance` | two men's entrance (stage) |
| `taunt`, `taunt 1` .. `taunt 4` | his own taunts, from his moveset | `win taunt` | his winner's taunt |

A number (0 to 60) is one of the engine's scene animations (`data\storyscenes\SCENES.md` lists them).

## Building and checking

```
.\build_mod.ps1                                   # checks mod\stories\*.json, then the mod
python mod\gen\storyc.py mod\stories\x.json x.pack  # one story on its own (to check it)
.\qa.ps1 story_custom                             # the sample, played in the real game
```

`story_custom` uses the hidden QA options `qa_story` 3 (go to the row, take the first story on the list, and let
the CPU play the player's matches); `story_list_view` (`qa_story` 10) leaves the list up for screenshots.

Test stories for QA are in `qa\stories_test` (`003_scene_interview.json`, `003_scene_choice.json`,
`003_match_types.json`): a scenario's `"stories"` copies them into the run's stories folder.

## Not possible, and why

`STORY_UPGRADE_REPORT.md` has the evidence for each.

- 3-on-3, 2-on-3, 1-on-3, a gauntlet: the engine's teams are two men at most.
- More than 4 cast roles: the engine has 8 role places, u16 each, with the next record right after them.
- A referee bump or swap: no referee exists in a match; the counts are HUD numbers.
- A match where the player runs in to help someone (the game's "interfere" matches, type 15), the guest referee match and the
  tag ladder match: the engine has them, the compiler doesn't make them.
- JR and King's own voice clips on a cutaway line.
