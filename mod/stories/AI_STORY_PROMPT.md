You are writing a custom story for "Ultimate No Mercy", a mod of the Nintendo 64 wrestling game WWF No Mercy. A story is
one JSON file. The game's own Story Mode plays it: narration pages, 3D backstage and in-ring scenes with speech bubbles
and choices, a match, then branches that pick what comes next from how the match ended.

Write the story described under MY STORY at the end.

====================================================================================================
RULES FIRST
====================================================================================================

A file that breaks ANY rule in this prompt is REJECTED: it does not play. It shows on the game's list as "ERROR" with the
reason, and the player gets nothing. There is no partial credit. Before you answer, re-check your file against the
CHECKLIST near the end, line by line, counting what it asks you to count.

====================================================================================================
OUTPUT RULES
====================================================================================================

- Output ONE JSON object and nothing else: no comments, no trailing commas, no markdown fences, no explanation.
- Use only the keys and values listed here. Anything not listed does not exist.
- Text is plain ASCII. \n inside a string starts a new line.
- TEXT LIMITS (counted exactly):
    a line               48 characters at most. A {name} counts as 10. A voice token counts as its longest:
                         nickname 20, catchphrase 40, finisher 24, insult 16; a signoff counts as TWO lines of 40.
    a speech bubble      3 lines
    a narration page     7 lines
    a requirement        3 lines
    a question + options 5 lines together; an option is one line of 28 characters at most
    "menu"               24 characters
- Superstars are named by their string ID, exactly as in ROSTER ("the_rock"; never the name on screen). Role names are matched exactly.
- Every "next" is the name of a node, or "end", or "gameover".

====================================================================================================
TOP LEVEL
====================================================================================================

{
  "title":        "The story's name (shown in logs).",
  "menu":         "Name on the Custom Story list.",
  "championship": "world",   // the Story Mode slot it runs as: world, tag, ic, european, hardcore, lightheavy, womens.
                             // Use "world" unless you want the game's tag partner (role "partner"): "tag".
  "cast": { "rival": "the_rock", "taker": ["undertaker", "kane", "big_show"] },
                             // your own roles: at most 4, or 3 if ANY match in the story has a "title".
                             // A list recasts: the first who isn't the player or another role's man plays the role, in
                             // every match, scene and line. Give a list (up to 8 IDs) to any role the player might pick.
  "vars":  { "heat": 0 },    // story variables with starting whole numbers: at most 32.
  "start": "first_node",     // optional; default: the first node in the file.
  "nodes": { "first_node": { ... }, "second_node": { ... } }
}

Roles that always exist (never list them in "cast"):
  player     the Superstar the player picked
  partner    the player's tag partner (only with "championship": "tag")
  champion   the champion of the story's championship (whoever wins a "title" match becomes it)
  champion2  the second tag / women's champion

====================================================================================================
A NODE (one match, with what comes before it and after it)
====================================================================================================

"node_name": {
  "chapter":     "CHAPTER TITLE",   // nodes in a row with the same chapter are one chapter: at most 11 chapters
  "pages":       ["Page 1", "Page 2"],   // optional narration on black, before the scene
  "scene":       { ... },           // optional 3D scene before the match (see SCENES)
  "match":       { ... },           // REQUIRED (see MATCH)
  "requirement": "Text over the match card.",
  "retries":     0,                 // 0 to 15: retries of a lost match when a branch says "gameover"
  "branches":    [ { ... }, { ... } ]   // see BRANCHES
}
Names in text: {player}, {rival} (any role) or {kane} (any roster string ID) print that Superstar's name.
A "requirement" containing any {name} needs "pages" or a "scene" in the same node, and every man it names must be
named or on screen there.

====================================================================================================
MATCH
====================================================================================================

"match": {
  "type":     "single",                  // see MATCH TYPES
  "slots":    ["player", ["rival", "kane"]],   // who is in it: exactly the number the type takes
  "teams":    [["player", "pal"], ["rival", "kane"]],  // INSTEAD of "slots" and "type": see TEAMS
  "rules":    "no dq",                   // optional, see RULES
  "arena":    "WRESTLEMANIA",            // optional, see ARENAS; default RAW IS WAR
  "title":    "world",                   // optional: this championship on the line (world, tag, ic, european, hardcore, lightheavy, womens)
  "runins":   [{ "who": "kane", "helps": "opponents", "at": "0:45" }],   // optional, see RUN-INS
  "location": "parking lot",             // optional: a backstage brawl: pool hall, parking lot, boiler room
  "script":   [ ... ]                    // optional: mid-match triggers, see SCRIPT
}

A slot is a role, a roster string ID, or a list of up to 5 of them (the first who can be in the match takes it). No Superstar
can fill two slots: the game moves down the list. The player goes in slot 1 unless the type says otherwise.

MATCH TYPES (slots):
  single    2
  tag       4   (slots 1-2 a team, 3-4 a team)
  triple    3
  handicap  3   (the first two a team against the third: put "player" third)
  fourway   4
  cage      2
  ladder    2
  ironman   2

TEAMS: 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1, or four men alone. Nothing bigger: the game's teams are two men at most.
Don't write 3-on-3, 2-on-3, 1-on-3, a gauntlet or a six-man: the file is rejected.

RULES (by type; the first is the default; cage, ladder and ironman have only "normal"):
  single:   normal, no dq, hardcore, submission, anywhere, street fight, pinfall only, first blood
  tag:      normal, tornado, no dq, hardcore
  triple:   normal, hardcore
  handicap: normal, hardcore
  fourway:  normal, no dq

ARENAS (any capitals):
  RAW IS WAR, NO MERCY, SMACKDOWN, KING OF THE RING, SUMMERSLAM, SURVIVOR SERIES, ROYAL RUMBLE, WRESTLEMANIA, BACKLASH, ARMAGEDDON, MONDAY NITRO, HALLOWEEN HAVOC, BASH AT THE BEACH, RAW IS WAR WM 2000, SOULED OUT, SUPERBRAWL, STARRCADE, SUNDAY NIGHT HEAT, ROYAL RUMBLE WM 2000, KING OF THE RING WM 2000, SURVIVOR SERIES WM 2000, WRESTLEMANIA WM 2000, SUMMERSLAM WM 2000

RUN-INS: a man runs in during the match. "helps": "opponents" (the default) or "player". At most 2 in a single match (one
each side), 1 in a handicap match (for the opponents), none in other types. "at" is when he comes, and must be exactly
one of: 0:15, 0:30, 0:35, 0:40, 0:45, 1:00, 1:10, 1:30, 2:00, 3:00. Without "at": 0:30 for the opponents' man, 0:35 for the player's.

SCRIPT: a list of up to 16 triggers, each fires once while the match plays: { "when": <condition>, "do": <action>, ... }
Conditions (<role> is player or a role who is in this match):
  "time 1:30"                the match clock has reached it
  "health rival < 30%"       his health is under that share (1 to 99)
  "rival outside"            he is out of the ring
  "rival in a hold"          he is caught in a submission hold
  "finisher by rival"        he has just hit his finisher
Actions:
  "do": "end", "winner": <role>, "how": <how>     ends the match with that result, through the game's own bell, replay
       and results. how: pinfall (default), submission, tko, dq, countout (in cage: escape; in ladder: ladder).
       With more than two men in the match add "loser": <role>.
       In a fourway it only eliminates "loser": the match goes on with the others. In an ironman it scores one fall: the match goes on.
  "do": "runin"             the match's run-in comes now ("side": player or opponents if it has both). It needs "runins".
  "do": "no dq"             the match is No DQ from now on
  "do": "no countout"       no more count-outs
Not possible, don't write them: a referee bump or swap, a weapon thrown in, lights out, a pause for a bubble in a match.

====================================================================================================
BRANCHES (after the match; tried in order, the first whose if holds is taken)
====================================================================================================

{ "if": "win by submission and (heat >= 2 or choice 1)", "set": { "heat": "+1", "rival": "partner" }, "money": 1000,
  "pages": ["After-match page"], "scene": { ... }, "next": "node_name" }

"if" is terms joined by and, or, not, and parentheses. (Nesting up to 16 deep; 255 pieces at most.) Terms:
  win | lose | notlose                the match result
  win by X | lose by X | ended by X   how: pinfall, submission, dq, countout, tko, ladder, escape, draw, decision
  heat >= 2                           a variable: == != < <= > >= and a whole number
  chance 50%                          random
  choice 1 | choice 2 ...             the answer to the most recent "ask"
  player is STEVE AUSTIN              a role is that Superstar (any role)
  player in dx                        a role is in that group (see GROUPS)

- The LAST branch of every node has no "if": it always holds.
- "set": { "var": 3 } sets, { "var": "+1" } or "-1" adds. A role can take another role's Superstar ({ "rival": "partner" }: a
  betrayal) or a roster string ID ({ "rival": "kane" }).
- "money": a whole number. "pages" / "scene": shown after the match.
- "next": a node, "end" (the story is over), or "gameover" (retry if "retries" allows, otherwise game over).

====================================================================================================
SCENES (3D scenes with the game's own sets, animations and speech bubbles)
====================================================================================================

"scene": { "place": "locker room", "cast": ["player", "ally"], "steps": [ step, step, ... ] }
(or a plain list of steps whose first is { "place": ..., "cast": [...] }). A scene starts with a place.

LIMITS: at most 4 DIFFERENT Superstars ON SCREEN in one scene IN TOTAL, across every place it cuts to (every "cast" entry
and every "who" counts, and so does "player"). At most 8 named in one scene, text and tron included. A node's scene and each
branch's scene are separate scenes, with 4 each. Only the hall and the parking lot have doors.

PLACES ("at" uses these spots; spots 1 and 2 face each other: put two people talking on 1 and 2):
  hall (or hallway)                   6 spots, 2 doors
  locker room (or dressing room)      8 spots
  boiler room                         4 spots
  pool hall (or sports lounge, bar)   6 spots
  parking lot (or garage)             6 spots, 3 doors
  stage (or entrance, ramp)           5 spots
  ring                               10 spots
"stage" and "ring" are in the match's arena; put the node's "arena" on its match.

STEPS (each is one object):
  { "place": "hall", "cast": ["heel", { "who": "champ", "at": 2, "do": "stand" }] }
        cut to a place. Without "at": spots 1, 2, 3 ...
  { "say": "Line one\nline two", "who": "rival" }
        he speaks in a bubble. "say" can be a list (a bubble each), and a line can differ by player (see LINES BY PLAYER).
        Optional: "do", "camera" ("close", "two", "wide", false). Without "who": just the bubble.
  { "ask": "Question?", "options": ["Yes", "No"], "who": "player", "store": "answer" }
        2 to 4 options. "choice 1", "choice 2" test the latest answer. "store" keeps this one in a variable (1, 2 ...), so a
        later question doesn't overwrite it. The variable must be in "vars".
  { "if": "answer == 1 or (heat >= 2 and not player in nwo)", "then": [steps], "else": [steps] }
        steps played only if it holds: the whole language of BRANCHES; "win" and "win by" mean the last match. "else" optional.
  { "set": { "heat": "+1" } }                  changes variables or roles, as a branch's "set"
  { "narrate": "Meanwhile..." }                a page on black; may be a list
  { "who": "rival", "do": "point" }            an action; add "at": 3 if he isn't on screen yet. "wait": 30 (frames) or true
  { "who": "rival", "do": "walk", "door": 1 }  walks in through a door ("run" runs). Door 1 brings him to spot 1: keep it free
  { "who": "rival", "do": "entrance" }         his ring entrance with music: place "stage" only
  { "who": "a", "do": "tag entrance", "with": "b" }   stage only; b must be the next man in the scene's cast
  { "camera": "wide", "on": ["rival", "player"] }     close, two or wide
  { "music": "on" } or { "music": "off" }      the backstage scene music
  { "wait": 60 }                               1 to 254 frames; 60 is one second
  { "tron": "boss", "say": "You'll see me when I'm ready." }
        the camera cuts to a close-up of the arena's Titantron with that Superstar's entrance video playing on it, the bubble over
        it, then back to the men: for a man who isn't in the building. Use it in "ring". He doesn't count toward the 4 on
        screen, but toward the 8 named. Arenas with a screen of their own: RAW IS WAR, NO MERCY, SMACKDOWN, KING OF THE RING, WRESTLEMANIA, BACKLASH, BASH AT THE BEACH, RAW IS WAR WM 2000, SUNDAY NIGHT HEAT. Fallbacks, never an error:
        another arena: the same video full screen. A man with no entrance video, or a backstage place: the bubble alone.
  { "commentary": "jr", "say": "Business is about to pick up!" }     "jr" or "king"; "say" can be a list
        the camera goes close on JR and King at the announce table, the line in a bubble ("JR: ..."; those 4 characters count in the line, "KING: " 6).
        Only in "ring". JR and King don't count toward the 4. No voice clip plays.

"do" ACTIONS:
  stand, mic, talk, entrance, tag entrance, walk, run, shout, point, interview, shrug, rub face, turn away, drink, laugh, punch, fist up, arms up, hurt, hold out mic, swing, fall, down, get up, bring it
  taunt (his first own taunt), taunt 1, taunt 2, taunt 3, taunt 4, win taunt: that Superstar's own moves from the game

====================================================================================================
VOICE TOKENS
====================================================================================================

Words that depend on who the player picked: {role.field} or {ROSTER NAME.field}, in any "say", "ask", "narrate", "pages" text:
  {player.nickname}     his nickname                  (counts 20)
  {player.catchphrase}  one of his catchphrases, the same one all story    (counts 40)
  {player.signoff}      his closing line, up to 2 lines (counts 2 lines of 40: nothing else on those lines)
  {player.finisher}     his finisher, with "the"       (counts 24)
  {player.insult}       what he calls people           (counts 16)
Every Superstar has them (see ROSTER; anyone missing uses the defaults: "the competitor", "the finisher", "pal").

LINES BY PLAYER: anywhere a bubble's or page's line goes, an object picks the line by the player's Superstar:
  "say": { "the_rock": "Line for him.", "group:dx": "Line for DX.", "default": "Anyone else." }
The first match in the file's order wins; "default" is REQUIRED.

====================================================================================================
ROSTER (string IDs, with each man's voice, so your lines match his cadence)
====================================================================================================

the_rock | nickname: The Great One | finisher: the Rock Bottom | insult: jabroni | catchphrases: Know your role! / Just bring it! / It doesn't matter what you think! / Finally, The Rock has come back! | signoff: If ya smell what The Rock is cookin'!
steve_austin | nickname: Stone Cold | finisher: the Stone Cold Stunner | insult: son of a gun | catchphrases: Austin 3:16 says I whipped your ass! / Gimme a hell yeah! / What? What? What? / Don't trust anybody! | signoff: And that's the bottom line, 'cause Stone Cold said so!
undertaker | nickname: The Phenom | finisher: the Tombstone | insult: mortal | catchphrases: Rest... in... peace. / Your soul belongs to me! / The darkness is coming. / I will make you suffer. | signoff: Rest in peace.
undertaker_1999 | nickname: The Lord of Darkness | finisher: the Tombstone | insult: sinner | catchphrases: Where to, now? / The Ministry will rise! / There is no escape. / You will serve me. | signoff: The Ministry is waiting.
kane | nickname: The Big Red Machine | finisher: the Tombstone | insult: fool | catchphrases: Burn in hell! / I am a monster. / Fire... and brimstone! / You can't hurt what's dead. | signoff: Burn.
steven_richards | nickname: Stevie Richards | finisher: the Stevie Kick | insult: chump | catchphrases: Hey, I'm talking here! / You'll respect me! | signoff: Stevie Richards has spoken!
mankind | nickname: Mankind | finisher: the Mandible Claw | insult: buddy | catchphrases: Have a nice day! / Mr. Socko says hello! / I'm just a big kid at heart. / Bang bang! | signoff: Have a nice day!
cactus_jack | nickname: Cactus Jack | finisher: the double arm DDT | insult: Junior | catchphrases: Bang bang! / I'll take the pain! / Hardcore is a way of life! | signoff: Bang bang!
mick_foley | nickname: Mrs. Foley's Boy | finisher: the Mandible Claw | insult: pal | catchphrases: Have a nice day! / I've got nothing left to prove. / I'm the hardcore legend! | signoff: Have a nice day!
shawn_michaels | nickname: The Heartbreak Kid | finisher: Sweet Chin Music | insult: sweetheart | catchphrases: I'm the showstopper! / Suck it! / I'm the main event! / Boy toy coming through! | signoff: Two words for ya: suck it!
triple_h | nickname: The Game | finisher: the Pedigree | insult: jackass | catchphrases: I am the game! / Time to play the game! / I'm that damn good! / I am the Cerebral Assassin. | signoff: I am the game, and I am that damn good.
x_pac | nickname: The 1-2-3 Kid | finisher: the X-Factor | insult: punk | catchphrases: Suck it! / X-Pac is in the house! / Bring it, punk! | signoff: And you know what? Suck it!
mr_ass | nickname: Mr. Ass | finisher: the Fame-Asser | insult: chump | catchphrases: Kiss my ass! / Nobody's got more ass! / I'm the bad ass! | signoff: Kiss my ass!
road_dogg | nickname: The Road Dogg | finisher: the Pumphandle Drop | insult: chump | catchphrases: Oh, you didn't know? / Ladies and gentlemen, boys and girls... / Your ass better call somebody! | signoff: Oh, you didn't know? Your ass better call somebody!
big_show | nickname: The Big Show | finisher: the Chokeslam | insult: little man | catchphrases: I'm the biggest there is! / Nobody's bigger than me! / I'm gonna crush you! | signoff: Big Show, big man, big win.
dean_malenko | nickname: The Iceman | finisher: the Cloverleaf | insult: amateur | catchphrases: A man of a thousand holds. / I'll take you apart. / You'll tap. | signoff: Technically, you're beaten.
perry_saturn | nickname: Perry Saturn | finisher: the Rings of Saturn | insult: mope | catchphrases: You don't scare me! / I'm the Radical one! / This is where it ends! | signoff: Moppy says it's over.
eddie_guerrero | nickname: Latino Heat | finisher: the Frog Splash | insult: ese | catchphrases: Viva la raza! / I lie, I cheat, I steal! / Orale! / Latino Heat, baby! | signoff: Viva la raza, ese!
chris_jericho | nickname: Y2J | finisher: the Walls of Jericho | insult: assclown | catchphrases: Welcome to Raw is Jericho! / Never, ever interrupt me! / Shut up! Shut up! / I'm the Ayatollah of Rock'n'Rolla! | signoff: Raw is Jericho!
kurt_angle | nickname: The Olympic Hero | finisher: the Olympic Slam | insult: hooligan | catchphrases: Oh, it's true! It's damn true! / I won a gold medal! / Intensity, integrity, intelligence! | signoff: And that's the truth!
tazz | nickname: The Suplex Machine | finisher: the Tazmission | insult: punk | catchphrases: Beat me if you can! / Survive if I let you! / You're goin' to sleep! | signoff: Beat me if you can. Survive if I let you.
test | nickname: Test | finisher: the Pumphandle Slam | insult: punk | catchphrases: You just failed the Test! / I'm the next big thing! | signoff: You failed the Test.
godfather | nickname: The Godfather | finisher: the Pimp Drop | insult: playa | catchphrases: Hey, pimpin' ain't easy! / Hop on the Ho Train! / It's time to party! | signoff: Pimpin' ain't easy!
dlo_brown | nickname: D'Lo Brown | finisher: the Lo Down | insult: homey | catchphrases: Hey, hey, hey! / Don't make me shake my head! / Down low, baby! | signoff: D'Lo is gone, baby!
val_venis | nickname: The Big Valbowski | finisher: the Money Shot | insult: chief | catchphrases: Hello, ladies! / Ladies, take a good look. / Val Venis is in the house! | signoff: Goodnight, ladies.
ken_shamrock | nickname: Most Dangerous Man | finisher: the Ankle Lock | insult: punk | catchphrases: I'll snap your ankle! / Nobody gets out of my hold! / You made me snap! | signoff: Tap, or I'll break it.
rikishi | nickname: Rikishi | finisher: the Rikishi Driver | insult: brother | catchphrases: Rikishi is gonna dance! / Time for the Stink Face! / Everybody dance! | signoff: Too cool for school!
grand_master_sexay | nickname: Grand Master Sexay | finisher: the Hip Hop Drop | insult: homie | catchphrases: Too cool, baby! / We're gonna get funky! / Sexay in the house! | signoff: Too Cool, see ya!
scotty_too_hotty | nickname: Scotty 2 Hotty | finisher: the Worm | insult: homie | catchphrases: Here comes the worm! / W-O-R-M, baby! / Too Cool! | signoff: The Worm says goodnight!
edge | nickname: Edge | finisher: the Edgecution | insult: dork | catchphrases: Reeking of awesomeness! / Totally reeking! / Five seconds of pose! | signoff: For the benefit of those with flash photography!
christian | nickname: Captain Charisma | finisher: the Impaler DDT | insult: dork | catchphrases: Reeking of awesomeness! / Five seconds of pose! / It's on, brother! | signoff: Totally reeking of awesomeness!
matt_hardy | nickname: Matt Hardy | finisher: the Twist of Fate | insult: dude | catchphrases: We're the Hardy Boyz! / It's high flying time! / Team Xtreme! | signoff: Team Xtreme, all the way!
jeff_hardy | nickname: The Enigma | finisher: the Swanton Bomb | insult: dude | catchphrases: I'm going higher! / Team Xtreme! / Watch me fly! | signoff: Team Xtreme!
faarooq | nickname: Faarooq | finisher: the Dominator | insult: punk | catchphrases: Damn! / Time to get paid! / The APA is in business! | signoff: The APA. Damn!
bradshaw | nickname: Bradshaw | finisher: the Clothesline | insult: boy | catchphrases: Clothesline from Hell! / We drink, we fight! / Time for some beer! | signoff: The APA. Let's go drink.
d_von_dudley | nickname: D-Von Dudley | finisher: the 3D | insult: punk | catchphrases: D-Von, get the tables! / Testify! / Wassup! | signoff: Testify!
buh_buh_ray_dudley | nickname: Bubba Ray | finisher: the Bubba Bomb | insult: punk | catchphrases: D-Von, get the tables! / You're going through a table! / Wassup! | signoff: Through a table you go!
hardcore_holly | nickname: Hardcore Holly | finisher: the Alabama Slam | insult: punk | catchphrases: I'm the toughest man here! / Hardcore, all the way! / I'll bust you open! | signoff: That's hardcore.
crash | nickname: Crash Holly | finisher: the Crash Landing | insult: pal | catchphrases: I'm a super heavyweight! / Weigh me, I'm ready! / 24/7, baby! | signoff: Crash Holly, champ 24/7!
big_boss_man | nickname: The Big Boss Man | finisher: the Boss Man Slam | insult: punk | catchphrases: You have the right to remain silent! / Law and order! / You're under arrest! | signoff: Case closed.
albert | nickname: Albert | finisher: the Baldo Bomb | insult: runt | catchphrases: I'm gonna break you! / Nobody pushes me around! | signoff: Albert has spoken.
al_snow | nickname: Al Snow | finisher: the Snow Plow | insult: fella | catchphrases: What does everybody want? / Head! Head! Head! / Head wants you! | signoff: Head says goodnight!
steve_blackman | nickname: The Lethal Weapon | finisher: the Lethal Kick | insult: pal | catchphrases: You'll learn respect. / Silence is lethal. / I'll finish this. | signoff: It's over.
british_bulldog | nickname: The British Bulldog | finisher: the Running Powerslam | insult: mate | catchphrases: Rule Britannia! / Mind your manners! / For Queen and country! | signoff: Cheerio!
mark_henry | nickname: Sexual Chocolate | finisher: the Bear Hug | insult: boy | catchphrases: I'm the world's strongest man! / Sexual Chocolate! / Respect the strength! | signoff: Sexual Chocolate, baby!
viscera | nickname: Viscera | finisher: the Big Splash | insult: fool | catchphrases: I'll squash you! / Feel the weight! / The Ministry calls! | signoff: Viscera has spoken.
andre_the_giant | nickname: The Eighth Wonder | finisher: the Giant Splash | insult: boss | catchphrases: You are too small, boss. / Nobody slams the Giant! / I am Andre. | signoff: Andre the Giant has spoken.
bull_buchanan | nickname: Bull Buchanan | finisher: the Scissors Kick | insult: punk | catchphrases: You have the right to remain silent! / Justice is coming! / Law and order! | signoff: Justice is served.
essa_rios | nickname: Essa Rios | finisher: the Moonsault | insult: amigo | catchphrases: Ole! / I fly higher than you! / Feel the fire! | signoff: Ole!
taka_michinoku | nickname: Taka Michinoku | finisher: the Michinoku Driver | insult: baka | catchphrases: Indeed! / Kaientai is here! / I am the champion! | signoff: Indeed!
chyna | nickname: The Ninth Wonder | finisher: the Pedigree | insult: little boy | catchphrases: I'm no lady! / Don't mess with Chyna! / I can beat any man here! | signoff: Chyna doesn't lose.
stephanie_mcmahon | nickname: The Princess | finisher: the slap | insult: loser | catchphrases: Do you know who I am? / Daddy will hear about this! / I'm a McMahon! | signoff: Don't forget who I am.
tori | nickname: Tori | finisher: the DDT | insult: honey | catchphrases: You'll regret this! / I'm right here! | signoff: That's what I think.
terri | nickname: Terri | finisher: the roll-up | insult: sweetie | catchphrases: I'm ready for this! / Bring it on! | signoff: Back to you, King!
trish_stratus | nickname: Trish | finisher: the Stratusfaction | insult: honey | catchphrases: Stratusfaction guaranteed! / Don't touch me! / I'm all that! | signoff: Stratusfaction guaranteed.
lita | nickname: Lita | finisher: the Moonsault | insult: chick | catchphrases: Team Xtreme! / Watch me fly! / I'll take you all on! | signoff: Team Xtreme!
ivory | nickname: Ivory | finisher: the Poison Ivory | insult: tramp | catchphrases: Shame on you! / You're disgusting! / Right to Censor! | signoff: Shame on all of you.
linda_mcmahon | nickname: Linda McMahon | finisher: the slap | insult: young man | catchphrases: This company is mine too. / Enough is enough! | signoff: This is my company.
the_kat | nickname: The Kat | finisher: the Kat Splash | insult: honey | catchphrases: Meow! / Kitty's got claws! | signoff: Meow!
mae_young | nickname: Mae Young | finisher: the Bronco Buster | insult: sonny | catchphrases: I've still got it! / Bring it on, sonny! | signoff: Not bad for my age!
ho | nickname: the Ho | finisher: the slap | insult: baby | catchphrases: Hop on the Ho Train! | signoff: Pimpin' ain't easy!
vince_mcmahon | nickname: Mr. McMahon | finisher: the Stone Cold Stunner | insult: son | catchphrases: You're fired! / No chance in hell! / I am the chairman! / It's my company! | signoff: No chance in hell!
shane_mcmahon | nickname: Shane-O-Mac | finisher: the elbow drop | insult: pal | catchphrases: Here comes the money! / Shane-O-Mac! / I'm a McMahon, baby! | signoff: Here comes the money!
earl_hebner | nickname: Earl Hebner | finisher: the fast count | insult: buddy | catchphrases: Ring the bell! / I call it down the middle! | signoff: That's the call.
jim_ross | nickname: Good Ol' JR | finisher: the cowboy hat | insult: son | catchphrases: Business is about to pick up! / By God! / Bah gawd! / As God as my witness! | signoff: Business is about to pick up!
funaki | nickname: Funaki | finisher: the Funaki Driver | insult: baka | catchphrases: Indeed! / Kaientai is here! / Funaki, number one! | signoff: Indeed!
gangrel | nickname: Gangrel | finisher: the Impaler DDT | insult: mortal | catchphrases: Welcome to the Brood. / The blood will flow. / Join us... | signoff: The Brood awaits.
mosh | nickname: Mosh | finisher: the Stage Dive | insult: dude | catchphrases: Headbang! / Let's mosh! | signoff: Headbangers rule!
thrasher | nickname: Thrasher | finisher: the Stage Dive | insult: dude | catchphrases: Headbang! / Let's thrash! | signoff: Headbangers rule!
jeff_jarrett | nickname: Double J | finisher: the Stroke | insult: slapnut | catchphrases: Ain't I great? / Choose or lose! / I'm the chosen one! | signoff: Ain't I great?
droz | nickname: Puke | finisher: the Droz Driver | insult: dude | catchphrases: I'm one of a kind! / Watch me go! | signoff: Droz is out.
mideon | nickname: Mideon | finisher: the Eye Opener | insult: sinner | catchphrases: The Ministry rises! / I serve the darkness. | signoff: The darkness wins.
the_blue_meanie | nickname: The Blue Meanie | finisher: the Meaniesault | insult: dude | catchphrases: Blue World Order! / Let's dance! | signoff: Meanie out!
pete_gas | nickname: Pete Gas | finisher: the Gas Attack | insult: pal | catchphrases: The Mean Street Posse is here! / Greenwich rules! | signoff: Posse out.
rodney | nickname: Rodney | finisher: the Mean Street Slam | insult: pal | catchphrases: The Mean Street Posse is here! / Don't mess with the Posse! | signoff: Posse out.
joey_abs | nickname: Joey Abs | finisher: the Abs Buster | insult: pal | catchphrases: The Mean Street Posse is here! / Check out these abs! | signoff: Posse out.
raven | nickname: Raven | finisher: the Evenflow DDT | insult: lemming | catchphrases: Quoth the Raven: nevermore. / Life is pain. / Welcome to Raven's world. | signoff: Quoth the Raven, nevermore.
william_regal | nickname: Lord Regal | finisher: the Regal Stretch | insult: you scoundrel | catchphrases: You filthy animal! / Mind your manners! / Good day to you! | signoff: Good day!
tiger_ali_singh | nickname: Tiger Ali Singh | finisher: the Tiger Bomb | insult: peasant | catchphrases: You Americans disgust me! / I am the Tiger! | signoff: The Tiger has spoken.
bret_hart | nickname: The Hitman | finisher: the Sharpshooter | insult: punk | catchphrases: I'm the best there is! / The best there was! / The best there ever will be! / You screwed Bret! | signoff: The best there is, the best there was, the best there ever will be.
jim_neidhart | nickname: The Anvil | finisher: the Anvil Flattener | insult: pal | catchphrases: Hart Foundation forever! / Yeah, baby! | signoff: The Anvil has spoken!
hollywood_hogan | nickname: Hollywood | finisher: the Leg Drop | insult: brother | catchphrases: Whatcha gonna do, brother? / nWo for life, brother! / Too sweet, brother! / 4 life, dude! | signoff: Whatcha gonna do when Hollywood runs wild on you?
kevin_nash | nickname: Big Sexy | finisher: the Jackknife | insult: chico | catchphrases: Oh, you didn't know? / Too sweet! / Big Sexy is in the house! / The Wolfpac's here! | signoff: nWo 4 life.
scott_hall | nickname: The Bad Guy | finisher: the Outsider's Edge | insult: chico | catchphrases: Hey, yo! / Say hello to the bad guy! / Chico, you know who I am! / Too sweet! | signoff: Say hello to the bad guy.
randy_savage | nickname: Macho Man | finisher: the Flying Elbow | insult: brother | catchphrases: Oooh yeah! / Snap into it! / The cream rises to the top! / Dig it! | signoff: Oooh yeah, dig it!
mr_perfect | nickname: Mr. Perfect | finisher: the Perfectplex | insult: loser | catchphrases: Absolutely perfect! / I'm perfect! / It's a perfect day! | signoff: Absolutely perfect.
the_giant | nickname: The Giant | finisher: the Chokeslam | insult: little man | catchphrases: I'm the biggest there is! / Nobody's bigger! / You can't stop the Giant! | signoff: The Giant has spoken.
sting | nickname: The Stinger | finisher: the Scorpion Death Drop | insult: pal | catchphrases: It's showtime! / The Stinger is here! / Wooo! | signoff: It's showtime, folks!
lex_luger | nickname: The Total Package | finisher: the Torture Rack | insult: pal | catchphrases: I'm the Total Package! / Feel the rack! / Wolfpac's here! | signoff: Total Package, baby!
goldberg | nickname: Da Man | finisher: the Jackhammer | insult: punk | catchphrases: Who's next? / You're next! / Goldberg! | signoff: Who's next?
rick_steiner | nickname: The Dog-Faced One | finisher: the Steiner Bulldog | insult: pal | catchphrases: Steiner Brothers! / I'll take you down! | signoff: Steiner out!
scott_steiner | nickname: Big Poppa Pump | finisher: the Steiner Recliner | insult: freak | catchphrases: Freaks, holler if ya hear me! / I'm Big Poppa Pump! / Your hookup! | signoff: Big Poppa Pump is your hookup! Holla!
booker_t | nickname: The Bookerman | finisher: the Harlem Hangover | insult: sucka | catchphrases: Can you dig it, sucka? / Don't hate the player! / Spinaroonie! | signoff: Can you dig it, sucka?
stevie_ray | nickname: Stevie Ray | finisher: the Slapjack | insult: sucka | catchphrases: Harlem Heat's in the house! / You can't beat Stevie! | signoff: Harlem Heat, baby!
rey_mysterio | nickname: Rey Mysterio | finisher: the West Coast Pop | insult: ese | catchphrases: Booyaka! / Size doesn't matter! / 619! | signoff: Booyaka 619!
roddy_piper | nickname: Rowdy Roddy Piper | finisher: the Sleeper Hold | insult: laddie | catchphrases: I came to kick butt and chew gum. / I came here to kick butt! / Hot Rod's here! | signoff: Just when they think they got the answers, I change the questions!
eric_bischoff | nickname: Easy E | finisher: the karate kick | insult: pal | catchphrases: It's controversy that creates cash! / I'm in charge here! / nWo, baby! | signoff: Controversy creates cash.

GROUPS (for "player in dx" and "group:dx"; 1999-era defaults):
  dx               triple_h, shawn_michaels, x_pac, road_dogg, mr_ass, chyna
  nwo              hollywood_hogan, kevin_nash, scott_hall, randy_savage, scott_steiner, the_giant, sting, lex_luger, stevie_ray, eric_bischoff, x_pac
  ministry         undertaker, faarooq, bradshaw, mideon, viscera, edge, christian, gangrel, big_boss_man
  corporation      vince_mcmahon, shane_mcmahon, the_rock, big_boss_man, ken_shamrock, test, kane, triple_h, chyna, big_show
  hart foundation  bret_hart, jim_neidhart, british_bulldog
  apa              faarooq, bradshaw
  the outsiders    kevin_nash, scott_hall
  face             the_rock, steve_austin, kane, mankind, cactus_jack, mick_foley, big_show, chris_jericho, tazz, godfather, dlo_brown, ken_shamrock, rikishi, grand_master_sexay, scotty_too_hotty, matt_hardy, jeff_hardy, faarooq, bradshaw, al_snow, steve_blackman, mark_henry, andre_the_giant, essa_rios, chyna, terri, lita, linda_mcmahon, the_kat, mae_young, earl_hebner, jim_ross, mosh, thrasher, droz, the_blue_meanie, bret_hart, jim_neidhart, sting, goldberg, rick_steiner, booker_t, rey_mysterio, roddy_piper
  heel             undertaker, shawn_michaels, triple_h, x_pac, road_dogg, mr_ass, dean_malenko, perry_saturn, eddie_guerrero, kurt_angle, test, val_venis, edge, christian, d_von_dudley, buh_buh_ray_dudley, hardcore_holly, crash, big_boss_man, albert, british_bulldog, viscera, bull_buchanan, taka_michinoku, stephanie_mcmahon, tori, trish_stratus, ivory, vince_mcmahon, shane_mcmahon, steven_richards, funaki, gangrel, jeff_jarrett, mideon, pete_gas, rodney, joey_abs, raven, william_regal, tiger_ali_singh, hollywood_hogan, kevin_nash, scott_hall, randy_savage, mr_perfect, the_giant, lex_luger, scott_steiner, stevie_ray, eric_bischoff

====================================================================================================
SAMPLE 1: small (narration, win / lose, variables, a stand-in)
====================================================================================================

{
  "title": "The Grudge",
  "menu": "The Grudge",
  "championship": "world",
  "cast": { "rival": "the_rock", "boss": "triple_h" },
  "vars": { "beat_rival": 0 },
  "start": "grudge",
  "nodes": {
    "grudge": {
      "chapter": "THE GRUDGE",
      "pages": [
        "{rival} has been running his mouth\nabout {player} all week long.",
        "Tonight {player} gets the chance\nto shut him up. One on one."
      ],
      "match": { "type": "single", "slots": ["player", ["rival", "kane"]] },
      "requirement": "Singles match: {player} vs {rival}.\nWin, and a title shot is yours.",
      "branches": [
        { "if": "win", "set": { "beat_rival": 1 }, "money": 1000,
          "pages": ["{player} pinned {rival}!\nThe whole arena is on its feet.",
                    "{boss} was watching from the back.\nHe's next."],
          "next": "title" },
        { "pages": ["{rival} stands tall over {player}.\nThis isn't over.",
                    "The boss grants a rematch.\nOne more chance."],
          "next": "rematch" }
      ]
    },
    "rematch": {
      "chapter": "SECOND CHANCE",
      "pages": ["{player} wants {rival} again.\nNo excuses this time."],
      "match": { "type": "single", "slots": ["player", ["rival", "kane"]] },
      "requirement": "Singles match: {player} vs {rival}.\nLose, and the story is over.",
      "branches": [
        { "if": "win", "money": 500,
          "pages": ["Revenge! {player} beat {rival}.\nNow for {boss}."],
          "next": "title" },
        { "pages": ["{rival} wins again.\n{player} walks to the back alone.",
                    "THE END"],
          "next": "end" }
      ]
    },
    "title": {
      "chapter": "THE MAIN EVENT",
      "pages": ["{boss} says {player} got lucky.\nTime to prove him wrong."],
      "match": { "type": "single", "slots": ["player", ["boss", "steve_austin"]] },
      "requirement": "Main event: {player} vs {boss}.\nWin it all.",
      "branches": [
        { "if": "win", "money": 5000,
          "pages": ["{player} has beaten {boss}!\nWhat a night.",
                    "From grudge to glory.\nTHE END"],
          "next": "end" },
        { "pages": ["{boss} survives.\n{player} will be back.",
                    "THE END"],
          "next": "end" }
      ]
    }
  }
}

====================================================================================================
SAMPLE 2: the features (recast, voice tokens and lines by player, a stored choice, a scene if on a variable, a Titantron cut, JR and King, a tag match by teams, mid-match triggers, a win by submission branch)
====================================================================================================

{
  "title": "The Screwjob",
  "menu": "The Screwjob",
  "championship": "world",
  "cast": {
    "boss": ["vince_mcmahon", "shane_mcmahon"],
    "rival": ["the_rock", "steve_austin", "kane"],
    "pal": ["mankind", "kane", "big_show"]
  },
  "vars": { "deal": 0, "heat": 0 },
  "start": "offer",
  "nodes": {
    "offer": {
      "chapter": "THE OFFER",
      "pages": ["Monday night. {player} is told to see\n{boss} before the show."],
      "scene": {
        "place": "locker room",
        "cast": ["player", "boss"],
        "steps": [
          { "say": "So you're {player.nickname}.\nThe people love you. I don't.", "who": "boss" },
          { "say": { "group:nwo": "Too sweet, old man.\nSay your piece and get out.",
                     "the_rock": "{player.insult}, you're wasting\nThe Great One's time.",
                     "default": "{player.catchphrase}" }, "who": "player" },
          { "say": "You face {rival} tonight.\nLose, and there's money in it.", "who": "boss" },
          { "ask": "Take the deal from {boss}?", "options": ["Take it", "Tell him off"], "who": "player", "store": "deal" },
          { "if": "deal == 2", "then": [
              { "set": { "heat": "+1" } },
              { "say": "{player.signoff}", "who": "player" },
              { "who": "player", "do": "taunt" },
              { "who": "player", "do": "stand" },
              { "say": "You'll regret that. Ask {rival}.", "who": "boss", "do": "point" }
            ], "else": [
              { "say": "Smart. Stay down when it counts.", "who": "boss" }
            ] }
        ]
      },
      "match": {
        "type": "single",
        "slots": ["player", "rival"],
        "script": [
          { "when": "rival in a hold", "do": "end", "winner": "rival", "how": "submission" },
          { "when": "health rival < 20%", "do": "no dq" },
          { "when": "time 5:00", "do": "end", "winner": "player", "how": "pinfall" }
        ]
      },
      "requirement": "{player} vs {rival}.\nWatch the referee.",
      "retries": 1,
      "branches": [
        { "if": "lose by submission and deal == 2", "set": { "heat": "+2" }, "next": "fallout",
          "pages": ["The bell rang on a hold that was never\napplied. {boss} is laughing."] },
        { "if": "win by submission or win by pinfall", "money": 2000, "next": "fallout",
          "pages": ["{player} beat {rival}, clean.\n{boss} isn't laughing."] },
        { "next": "fallout", "pages": ["{rival} takes it. Nobody says why."] }
      ]
    },
    "fallout": {
      "chapter": "THE FALLOUT",
      "scene": [
        { "place": "ring", "cast": [{ "who": "player", "at": 1 }] },
        { "say": "{boss}! Get out here and\nsay it to my face!", "who": "player" },
        { "tron": "boss", "say": ["You'll see me when I'm ready, {player}.", "Tonight it's {pal} and you\nagainst {rival} and his friend."] },
        { "commentary": "jr", "say": "Business is about to pick up, King!" },
        { "commentary": "king", "say": "{player} looks mad, JR!" },
        { "if": "heat >= 2 and not player in corporation", "then": [
            { "say": "No more deals, {boss}.", "who": "player" }
          ], "else": [
            { "say": "We'll settle this in the ring.", "who": "player" }
          ] }
      ],
      "match": {
        "teams": [["player", "pal"], ["rival", ["undertaker", "kurt_angle"]]],
        "rules": "no dq",
        "arena": "WrestleMania"
      },
      "requirement": "Tag match: {player} and {pal}\nvs {rival}.",
      "branches": [
        { "if": "win", "money": 5000, "next": "end", "pages": ["{player} has the last word.\n{boss} leaves the building.", "THE END"] },
        { "next": "end", "pages": ["{boss} still runs the show.\nFor now.", "THE END"] }
      ]
    }
  }
}

====================================================================================================
CHECKLIST (verify every line before answering; a file that fails one is rejected)
====================================================================================================

[ ] One valid JSON object, no comments, no trailing commas, nothing before or after it.
[ ] "menu" 24 characters or fewer.
[ ] "cast" at most 4 roles (3 if any match has "title"); built-in roles not in it; lists of 8 IDs at most.
[ ] Every name is a role or an exact ROSTER name; every {name} and {role.field} too.
[ ] COUNT every text line: 48 characters at most, {name} = 10, tokens as under TEXT LIMITS; an option 28. Bubble 3 lines, page 7, requirement 3. Write in mixed case, not all capitals.
[ ] Every node has a "match"; "slots" has the type's exact number, or "teams" has an allowed shape; never both.
[ ] "rules", "arena", "title", "location" from their lists; "rules" fit the type.
[ ] Run-ins only in single (2, one a side) or handicap (1, for the opponents); "at" from its list.
[ ] "script" at most 16 triggers; its roles are in this match; "runin" only with "runins"; "end" with 3+ men has "loser".
[ ] A "requirement" that names anyone has "pages" or a "scene" in the same node.
[ ] Every node's last branch has no "if". Every "next" exists, or is "end" / "gameover".
[ ] At most 11 chapters. At most 32 vars, each one declared in "vars" before it's tested, set or stored.
[ ] Each scene: starts with a place; COUNT the different Superstars on screen across all its places, player included: 4 at
    most (tron and commentary excepted); 8 named; "at" within the place's spots; walk / run only in hall or parking lot;
    entrance / tag entrance only on stage; commentary only in ring; "ask" has 2 to 4 options; "wait" 1 to 254.
[ ] Every "say" object has a "default".
[ ] No file ever mentions something not listed here: no six-man, no referee bump, no new sets, objects or people. Someone not on
    the ROSTER may be mentioned only in narration text.

====================================================================================================
MY STORY
====================================================================================================

(Describe it here: who's in it, the feud, the matches and where they happen, any twists, choices, title matches, how it
should end. Whatever isn't specified, make good wrestling-storyline choices yourself.)
