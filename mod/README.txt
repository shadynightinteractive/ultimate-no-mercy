ULTIMATE NO MERCY
A mod for WWF No Mercy: Recompiled (the native PC port), as a program of its own.  Version 0.86.0

New in 0.38.2
-------------
  * Royal Rumble: the ported Superstars (WrestleMania 2000, No Mercy Plus, Revenge) now come out to
    their own themes as later entrants too, not only as the first two.
  * Mr. Perfect has his own entrance theme.

New in 0.38.1
-------------
  * Royal Rumble: a challenger now ends the Rumble properly -- HERE COMES A NEW CHALLENGER, then
    straight to Versus (the Rumble used to carry on behind the banner).
  * Versus's MATCH page no longer comes up empty (no match types listed) when player 2 joins after a
    Story match and a reset to the title.
  * Austin's entrance close-up in the Rumble arena no longer flickers white.
  * Scott Hall, Kevin Nash and Hollywood Hogan bring their own entrance music (from WCW nWo Revenge
    Redux). Bret Hart's is unchanged.
  * Custom stories: a match can name its arena ("arena": "WrestleMania", "King of the Ring", ...).
  * Fixed two rare crashes of the program itself: one on quitting, one when it was closed while still
    starting up.
  * Randy Savage and Curt Hennig join from WCW/nWo Revenge (their models, Revenge movesets, Redux themes).
  * The Superstar Select has a WCW page: Bret Hart, Jim Neidhart, Hollywood Hogan, Kevin Nash, Scott Hall,
    Randy Savage and Curt Hennig.
  * The Superstar Select's pages are yours to lay out: select_pages.json (next to the program) names each
    page and who's on it -- up to 32 pages, up to nine on a page (more go on over a page of their own).
    Superstars by name or ID; anyone it leaves out goes on MORE pages at the end ("unlisted": "hide" to
    leave them off). Mistakes are left out and written in the log; delete the file for the game's own
    pages. The file that comes with this version is the pages as they were.
  * The Revenge Superstars' themes play at the right speed (they ran twice too fast).
  * Bret Hart's Sharpshooter: his man is in it now (he was lying feet away).
  * The Revenge Superstars' moves have their sounds back: slams, thuds, hits (Nash's jackknife lands hard).
  * Moves the Revenge Superstars had borrowed from another Superstar (Hogan's punch and stomps were The
    Rock's) are their own Revenge moves where Revenge has a like move for the place, else an everyday
    one of the same kind (Hogan's strong strike the Big Boot, his stomps plain stomps). Randy Savage's
    strong punch no longer punches down at a man who isn't there; strikes, holds and throws no longer
    turn into another kind of move (a Snapmare that was a Spinebuster, a Sleeper Hold a Monkey Toss).
  * Kevin Nash's light strikes land again (and Savage's and Hall's of the same kind).
  * Two WCW arenas from WCW/nWo Revenge: MONDAY NITRO and HALLOWEEN HAVOC -- their stages, aisles, aprons
    and banners -- on Versus's ARENA page after the game's ten (the list scrolls past ten rows: more can
    follow). WrestleMania is still itself.

New in 0.38.0
-------------
  * Custom stories from a folder: put story files (JSON) in the game folder's "stories" folder. In Story
    Mode, the championship list's "Custom Story" row (now a picture in the list's own lettering, lit and
    glowing like the others) shows your stories in place of the championships -- in the list's own
    two columns, each by its name. A picks one, B goes back, L / R page through more than eight. The
    folder is read each time the list opens: no rebuilding. Two samples: The Grudge, Bad Blood.
    How to write one: mod\stories\README.md in the source.
  * F2 menu: QUIT MATCH (leaves the match or its entrances the way a challenger's match ends: back to
    the championship list, or to Versus) and RESET TO TITLE (back to the title and attract, credits kept).
  * JR and King no longer show through each other close up (the F6 camera): the game draws no depth at
    all, every model's pieces in order of distance, so side by side they crossed. The two of them now
    have a depth buffer of their own.
  * Versus: the Ladder Match row is the same size as the other match types (it was smaller, squashed),
    fades in and lights up with them, and left / right on the page go where they always did.
  * The Titantron's live camera sorts the barricades, the crowd's near rows and the table by its own
    side of the ring (it used the TV camera's).
  * The Play Test Build recording now logs each wrestler's move (for the struggling-slam report).

What it does
------------
Runs WWF No Mercy like a coin-op, with the game's own menus.

  * Power on: the company logos (THQ, JAKKS, Asmik Ace, AKI, WWF) and the full intro movie --
    no legal page. Then the attract loop: intro movie, title, CPU demo match (random
    Superstars), title, intro movie, ... INSERT COIN flashes throughout; with a credit it turns
    into PRESS START. All the arcade's words (INSERT COIN, PRESS START, CREDITS, CONTINUE?,
    GAME OVER, HERE COMES A NEW CHALLENGER!) are drawn in the game's own lettering by the
    game's own text code, white with a dark edge so they read on the white logo pages and on
    dark scenes alike. They're drawn as part of the game's own picture each frame (inside the
    game's display list, before the frame is finished), so they hold as steady as the game's
    own graphics. Start skips the movie once there's a credit.
  * A coin makes a sound: one of the game's own (a scatter of metallic pings, like coins
    dropping), in the attract loop, the menus and matches alike. Operator menu: COIN / CREDIT
    SETTINGS > COIN SOUND, OFF or 1-4 (each plays as you pick it).
  * Everything unlocked from the start: the hidden Superstars (the HIDDEN 1 and HIDDEN 2 pages:
    Vince, Shane, Shawn Michaels, Cactus Jack, ...), every SmackDown Mall item (attire, moves,
    props) and what the championships unlock. These are the game's own unlock flags, set as a
    game with everything bought has them, so the save stays the game's own (and keeps them).
    Operator menu: GAME SETTINGS > UNLOCK EVERYTHING.
  * Coin + Start, alone: Story Mode's championship list. Pick the title to go for (World
    Heavyweight, Intercontinental, Hardcore, Women's, Tag Team, European, Light Heavyweight),
    then your Superstar (B goes back to the list).
    No chapter card (championship / money / Save): the story goes straight on. The superstar
    select shows the wrestler's 3D model and the match card shows both wrestlers, as in the game.
    Its text pages and match cards move on by themselves (or press A).
  * ROYAL RUMBLE is on the same list, in the spot under Light Heavyweight: a one-player Royal
    Rumble, last one in the ring wins, in the Royal Rumble arena. No Rumble menu: pick the row,
    pick your Superstar (one box, yours), and the match starts -- no options pages, no CPU picks
    and no match card, so nobody knows who's coming. It's you against the CPU, with the operator's
    number of Superstars (you included; 15 to start with) and time between entrants (30 seconds
    to start with). Who's in it and the order they come out in are drawn fresh every time from
    everyone the game itself lets into a random Royal Rumble field, so anyone can be next. The
    women (Chyna excepted), Jim Ross and Earl Hebner are left out of it (operator menu: WRESTLER
    OPTIONS > NO WOMEN / ANNOUNCERS IN RUMBLE; off lets them in). (The game's own random order goes
    through the select's pages in turn -- a man from page 1, then page 2, ... -- the arcade
    shuffles the whole field instead.)
    As in the real Royal Rumble, the first two make their entrances before the bell: you, then
    the first entrant. At the bell the two of you are in corners diagonally across the ring, as
    in a one-on-one match: you top left, him in the lower right turnbuckle, facing each other,
    the camera on the two of you. A new entrant comes out every entry time. Ten seconds before he does, the
    game's own Royal Rumble countdown comes up (the red WWF box, :10 down to :00), the buzzer
    goes at :00 and he comes out with the game's own Rumble entrance (the stage shot, the walk
    down the aisle). If you're left in the ring alone, the next countdown starts at once. With
    four in the ring, the one called comes in at the next elimination. Every entrant fights:
    he's the CPU's from the moment he's in the ring. The clock at the bottom is the match clock.
    Out of the ring is out of the Rumble: whoever is thrown out (over the top rope or through
    the ropes) and lands on the floor is eliminated when he gets up -- the game's own Rumble rule
    (COUNT OUT: Lose). No pinfalls, no submissions, no rope breaks: over the top is the only way
    out (any Royal Rumble's RULES page starts that way too; it can still turn them on).
    The CPU fights for eliminations: each CPU Superstar goes after the man most worn down (the
    lowest spirit meter), and once a man's meter is in the blue (DANGER), he's the one to throw
    out -- at the ropes the CPU runs at him and hits him, on the apron it ties up with him through
    the ropes and throws him off. Only what the CPU wants changes: how anyone goes over the top is
    the game's own, as it always was. A hold is let go of after a moment (with submissions off it
    wins nothing). Operator menu: ROYAL RUMBLE > CPU GOES FOR ELIMINATIONS (off: the game's
    own CPU). An eliminated Superstar is gone from the arena (his panel and his place in
    the camera go with him). A ring spot that's empty, or whose entrant is still on the stage,
    has no panel on screen, so no name gives the next entrant away. The top counter is how many
    are still in the Rumble, in the ring or still to come.
    Last one in the ring after everyone has come out wins: the game's replay (A moves on), then
    it's back to the list.
  * Eliminated yourself: the game says so as usual and CONTINUE? 10..0 comes up over the Rumble,
    which carries on round you -- you stay where you are, and the Rumble can't end while your
    countdown runs. Coin + Start and you're back in it at full power: down in the ring, you get
    up; outside, you're taken back to the ring and roll in under the ropes, and the controls are
    yours again once you're on your feet. No continue: GAME OVER. In free play you're put back
    in by yourself after a moment.
  * Challengers, fighting-game style: when player 2 puts in a coin and presses Start during
    single player (Story Mode or the Royal Rumble), HERE COMES A NEW CHALLENGER! stops the
    game and it's straight to the Versus menu. Whoever wins that match takes over single
    player where it was, as the Superstar they won with; the loser is out (a coin and Start
    challenges again). Story Mode picks up its title run again; the Royal Rumble is set up again
    from where it stood, straight to the bell (no entrances: it's a match already on): the
    winner and the CPU Superstars still in the ring start it, and
    everyone who hadn't come out yet still comes out, in the same order, the next one when the
    entry time that had started would have run out. (The wrestlers' energy
    starts fresh: a match can't be frozen and picked up later.) If neither player is beaten
    (a team win, a time-out) the player who was challenged keeps their place.
  * SURVIVAL is on the list too, under Royal Rumble (where the Story Mode "Complete" box was):
    the game's own Survival. Pick your Superstar and it's you and three CPU Superstars in the
    ring; everyone put out is replaced by a new one, and the counter at the top is how many
    you've beaten. It goes on until you're out: CONTINUE? as after any lost match (a coin and
    Start puts you back on the list), or GAME OVER. The arena is the one the game has set, and
    the rules are Survival's own (no ARENA or RULES pages).
  * Two players in (both pressed Start): Versus -- the game's Exhibition menu (match type,
    players, arena, rules), then the superstar select. Its header reads "versus" (in the game's
    own red lettering), there's no title-belt tab and no "Exhibition!" announcer call.
    Under Cage Match there's LADDER MATCH: the game's own Ladder Match (Single, Tag, Triple
    Threat or Handicap, up the ladder for the belt), with its PLAYER, ARENA and RULES pages and
    the Superstar Select as usual. B on its first page goes back to the Versus match types.
    The PLAYER page only offers line-ups for exactly the two players, one of each (no Watch, no
    3P / 4P, none without 2P, no mirror or re-ordered copies): Tag Match 1P&2P vs CPU&CPU or
    1P&CPU vs 2P&CPU; Handicap 1P&2P vs CPU or 1P&CPU vs 2P. Where only one line-up is left
    (one-on-one matches: 1P vs 2P; Triple Threat: 1P vs 2P vs CPU) the PLAYER page is passed
    and the menu goes straight to ARENA.
  * No main menu, no navigating, no Rumble Pak message, no cover screens: the mod uses the game's
    own code to get there (the menus' own choices, the match's own exit, the demo's own fade).
  * Only players who paid have working controllers. The player who starts is controller 1.
    Player 2 can join any time with a coin + Start. In Story Mode that's at once, whatever is on:
    the bell rings (a deep hit outside a match), the match freezes where it is under
    "HERE COMES A NEW CHALLENGER!" and the game goes straight to the Versus menu.
    If a player drops out of a Versus game (doesn't continue), the other goes back to single
    player.
  * Lose a match: CONTINUE? 10..0 appears over the game. Coin + Start and you carry on right
    where you left off. If nobody continues: GAME OVER, the match fades out and it's back to
    the title.
  * Entrances: the wrestler walks out on the stage to their music (operator setting: Titantron
    videos on opens each with the full-screen video, as in the original game). The stage walk is
    filmed by a low camera looking up at the wrestler.
  * Fast get up: a wrestler knocked down always gets up the game's quick way, never the long,
    slow hurt get-ups -- unless someone with SPECIAL up has him as their target: then he's slow
    to get up, as in the game, and it's the moment for the finisher (operator setting).
  * Power up: Start (or 1 / 2) during your match, with a credit, spends it and puts you at full
    power, as on an arcade machine: the meter full (SPECIAL), stamina and every body part as
    fresh. (Free play: Start does nothing in a match.)
  * No pause: Start never opens the game's pause menu in arcade mode.
  * Arcade speed: matches run 15% faster than the original game (operator setting: off, or
    +5% to +30%). The game runs a match in steps, one per frame it draws (30 a second), and
    everything in a match is timed in them -- moves, reversal windows, the AI, the match clock,
    the Royal Rumble's countdown. With Arcade speed the match takes an extra step now and then
    before a frame is drawn (at +15%, about one frame in seven), so the whole match moves faster
    while the picture keeps the game's own frame rate. A button press still counts once. The
    music and crowd, the menus, the entrances before the bell and the replays keep their own
    speed. The match clock is the match's: at +15% a minute on it goes by in about 52 seconds,
    and so does the Rumble's entry time.
  * Big Show is back. He was cut from the game before release, but the ROM still has his face
    (one of the Create-a-Wrestler faces, left out of the menu), his BIG SHOW tank top (among
    the shirts, worn by no one) and empty slots in the music and Titantron lists where his
    were. He takes Chris Benoit's place: Benoit's box on the Superstar Select (Superstars 1,
    bottom right), in every mode -- Story Mode, Versus, the Royal Rumble, the attract demos.
    He's the Big Show No Mercy Plus V3 restores (taken from it and put into this game):
      - 7'4", 500 lbs. Four attires: the BIG SHOW logo tank top with black trunks; the tank
        top with jeans; a referee's shirt with jeans; and The Giant -- long hair, black trunks.
      - His photo on the Superstar Select and the other screens (a photo of its own: the game's
        photos are all left as they are).
      - His entrance theme, in his own music slot (put into the game's music bank and played
        by the game's own music engine).
      - His Titantron video: 72 frames (the crowd shots, his eyes, the BIG SHOW logo, his
        matches), in his own Titantron slot, full screen at the start of his entrance.
      - His moveset and abilities, in the menus and in the match alike. No Mercy Plus renumbered
        the game's moves (and added new ones), so each of his moves is the game's move with the
        same animations: 121 are exactly his. Where No Mercy Plus gave him a move it added with
        new animations, he does the game's nearest (Huge Chokeslam for its second Showstopper,
        Atomic Drop, the game's own running moves the new ones are built on, Choke Hold). His
        friends: Triple H, Shane McMahon and The Rock.
    Story Mode: Big Show plays every part the story gives Steven Richards, and Ken Shamrock
    plays Benoit's (including standing in when you're one of a story Royal Rumble's entrants).
    Your save file is never changed: whenever the game saves, it saves Benoit as he was.
  * Chris Benoit (operator menu: WRESTLER OPTIONS, off to start with): with Big Show on he's
    on SUPERSTARS 6 with the new Superstars (Big Show keeps his box) and plays his own Story
    Mode parts; with Big Show off he's in his own box. Both off: neither is on the roster (Ken
    Shamrock plays Benoit's Story Mode parts).
  * Four more Superstars, from No Mercy Plus V3: FUNAKI, GANGREL, MOSH and THRASHER. The
    roster itself is bigger: they're Superstars of their own (Benoit, or Big Show, and everyone
    else stay where they are), on a new page of the Superstar Select, SUPERSTARS 6 (after
    LADIES; R / L as for the other pages), and they're in every mode the select is: Versus,
    the Royal Rumble (the field and the first entrant are drawn from the select's pages) and
    Story Mode's title runs, and the select's Random picks them too.
    Each is No Mercy Plus's, taken from it and put into this game:
      - Four attires each, as No Mercy Plus has them. The parts No Mercy Plus made for them
        (Gangrel's and the Headbangers' trousers and kilts, the Headbangers' shirt, beards and
        masks, their faces) are new parts here, next to the game's own: nothing of the game's
        is changed. Their photos, heights and weights.
      - Their movesets and abilities (Funaki 131 moves exactly his, Gangrel 122, Mosh 124,
        Thrasher 126; for the moves No Mercy Plus added with new animations, the game's
        nearest). Funaki's friend, as No Mercy Plus has it: Dean Malenko.
      - Their entrances: with No Mercy Plus's themes and videos on, its themes (Funaki's own,
        in a music slot the game left empty) and Titantron videos; off, the game's own (Funaki
        with Taka Michinoku's, Kaientai's; Gangrel and the Headbangers with the music slots
        the game still has for them).
    Operator menu: WRESTLER OPTIONS, each of them on or off.
  * JEFF JARRETT, from WWF WrestleMania 2000, on SUPERSTARS 6 with the new Superstars, in every
    mode they are:
      - His looks: WrestleMania 2000's model of him, whole -- its body, head, face, hair, hands and
        ring attire, in his four costumes as it has them (red tights or blue, bare-chested or in
        his "Don't Piss Me Off!" tank top and sunglasses). Its meshes are its own files; its
        pictures are the ones WrestleMania 2000 draws for him (it draws a face, hair, tattoos and
        logos onto its pictures as it loads a model: they were taken from its memory as it showed
        him in a match). The parts are joined the way WrestleMania 2000 joins them (No Mercy cut
        the chest and upper arms in two and joins its models' parts differently: a WrestleMania
        2000 model joined No Mercy's way can't be drawn), and his hands take their poses (fists,
        open hands, ...) from WrestleMania 2000's own hand meshes and tables.
      - His moveset and abilities: WrestleMania 2000's, moved over move by move. No Mercy kept
        WrestleMania 2000's animations -- his own among them -- so 97 of his moves are the very
        same animations here (his Stroke is the game's "Front Russian Sweep", his taunts and
        poses its "Taunt 001"..."Taunt 162" ones), 26 more are this game's moves of the same name,
        and the 17 that are neither (a few counters and running attacks) its nearest.
      - His entrance: WrestleMania 2000's theme for him (a music slot the game left empty) and
        his WrestleMania 2000 Titantron video ("Don't P--- Me Off", Debra, his ring footage),
        its frames and timing as that game plays it.
      - His photo: a stand-in made from his WrestleMania 2000 Titantron footage.
    Operator menu: WRESTLER OPTIONS, JEFF JARRETT on or off. Like the others, nothing of him goes
    into your save.
  * DROZ and MIDEON, as No Mercy Plus V3 has them (on to start with), on SUPERSTARS 6:
      - Their looks: No Mercy Plus's models of them as they are -- Droz's long hair, beard,
        tattoos and plaid shorts, Mideon's -- four costumes each; their photos.
      - Their movesets and abilities, No Mercy Plus's (Droz 121 moves exactly his, Mideon 118).
        Where No Mercy Plus has a move this game hasn't, a move of the same kind by name, else
        their WrestleMania 2000 move for the slot when this game has it, else Albert's /
        Bradshaw's.
      - Their entrances: No Mercy Plus's themes for them and their Titantron videos, with No
        Mercy Plus's themes and videos on or off (the game has none for them).
  * THE BLUE MEANIE, from WWF WrestleMania 2000, on SUPERSTARS 6:
      - His looks: WrestleMania 2000's model of him, whole, as Jeff Jarrett's -- his face paint,
        goatee and blue hair, his "Blue Meanie" and "J.O.B. Squad" T-shirts, black shorts or blue
        jeans (four costumes), white knee pads and blue boots, his hands.
      - His moveset and abilities: WrestleMania 2000's, as Jeff Jarrett's (97 moves its very
        animations, 22 by name, the rest the nearest or Rikishi's).
      - His entrance: WrestleMania 2000's theme and Titantron video for him.
      - His photo: a stand-in made from WrestleMania 2000's Superstar Select.
    Operator menu: WRESTLER OPTIONS, DROZ / MIDEON / THE BLUE MEANIE each on or off. Nothing of
    them goes into your save.
  * THE MEAN STREET POSSE (PETE GAS, RODNEY, JOEY ABS), THE 1999 UNDERTAKER, RAVEN, WILLIAM REGAL
    and TIGER ALI SINGH, as No Mercy Plus V3 has them (on to start with), on SUPERSTARS 6 and 7 -- the
    1999 Undertaker in the Undertaker's box: on the Superstar Select the Type arrows go on past the
    Undertaker's Type 4 to the 1999 Undertaker's four (Type 1 to 4 again: his name, photo and model
    show it's him) and round; he's still his own Superstar after the select:
      - Their looks: No Mercy Plus's models of them as they are, four costumes each, with their
        entrance gear (the Undertaker's robe, Raven's jacket) on the way to the ring only; their
        photos, heights and weights.
      - Their movesets and abilities, No Mercy Plus's (the 1999 Undertaker's gaps filled from his
        WrestleMania 2000 moveset).
      - Their entrances: each his own theme and Titantron video, No Mercy Plus's. The game's
        entrance table is made bigger for them (five new music slots, each with its own theme and
        entrance effects): the Posse's theme (one for the three of them, as they had), the 1999
        Undertaker's, Raven's, Regal's and Tiger Ali Singh's (No Mercy Plus gives him the Lo Down
        theme he walked out to in 2000).
    Operator menu: WRESTLER OPTIONS, each on or off. Nothing of them goes into your save.
  * BRET HART and JIM NEIDHART, from WCW/nWo Revenge (on to start with), on SUPERSTARS 7, in the
    British Bulldog's group (the three of them each other's allies):
      - Their looks: Revenge's models of them, whole -- the same parts, meshes and pictures, its
        colours on them as it has them in a match; Bret in his pink and black, with and without his
        shades (Revenge's two masks of him; his four costumes go round them), Neidhart in his.
        Their hands are drawn with this game's WrestleMania 2000 hand shapes (fists, open hands),
        on their own Revenge skin.
      - Their movesets: Revenge's, in this game's moves. Revenge keeps a Superstar's animations in a
        list; the Superstars both games have (Malenko, Saturn, Guerrero, Jericho, the Bulldog) show
        which of this game's move slots each place in that list is; Bret's move for a slot is this
        game's move with his very animation there (his Sharpshooter is this game's Sharpshooter,
        the very animation), else the one nearest it pose for pose; where none is near enough,
        Dean Malenko's (for Bret) / Bradshaw's (for Neidhart). Their abilities are Malenko's /
        Bradshaw's too (Revenge rates its Superstars differently), their allies each other and
        the Bulldog.
      - Their entrance: Revenge's theme for them (the one it plays for both: its music is played
        from sequences, so it was recorded as Revenge plays it, as a sample like the other themes),
        each in a music slot of his own (Bret now enters to his own theme: see 0.35.4). No Titantron
        video: Revenge has none, and a Superstar brought in without a real one of his own gets none
        rather than a stand-in.
      - Their photos: from Revenge's Costume Change screen (its models of them).
    Operator menu: WRESTLER OPTIONS, BRET HART / JIM NEIDHART each on or off. Nothing of them goes
    into your save.
  * JIM ROSS and JERRY "THE KING" LAWLER at the announce table, in every match (the Royal Rumble
    too): No Mercy's own 3D models of them -- JR in his black hat, King in his jacket -- seated on
    chairs behind the table on the hard-camera side of the ring. No Superstar's place is taken:
    they're built apart from the match's slots, so six-man matches and the Rumble are as before.
      - They turn their heads to the action, and talk with their hands -- the one whose commentary
        line is playing (the commentary knows JR's lines from King's), else now and then.
      - A Superstar on the floor at the table, right by one of them: he flinches back, arms up (and
        again if the fight stays there). They aren't knocked down.
      - The table broken: both flinch, then get up and stand in front of their chairs for the rest of
        the match. No table (a cage match): they stand there from the start, no chairs.
      - Whole models, seen from any side: the game draws its props by distance (no depth test between
        them), so each piece of them is put before or after the table by where the camera is -- legs
        and chairs behind the table from the ring side, hands on it from above, all of them in front
        of it from behind. Most of a match's camera is above the table looking at the ring; the free
        camera (F6: mouse to look, W A S D / wheel / Space / Ctrl to move, Shift faster, F6 again to
        give it back) goes anywhere. Only with the camera inside a head is that one not drawn.
      - (0.34.1) They sit pulled in to the table (knees under it, as at a real desk), clear of the
        barricade and the crowd behind them. And from the match's own camera (on their side, behind
        them) they're seen: the game makes the table "see-through" (drawn after everything else)
        when it's between the camera and the ring, and it was drawn over them; now they're drawn
        with it, in front of it.
      - (0.34.2) No more white patch over their chins or dark panel on their chests: their neck piece
        isn't drawn, and the pieces joining head to neck and chest to waist follow their own bodies.
      - (0.34.3) The announce table is moved up toward the ring (about 8 inches), its walls with it
        (a Superstar stops at it, stands on it and goes through it where it now is), so they sit at it
        normally, clear of the barricade, with their forearms resting on top of it.
      - (0.35.0) Their chairs fit them: the seat sits under their thighs and the back is set behind
        them (no more backs and seats of the pants sunk into the chair). With the camera behind
        them they're drawn in front of everything there -- the ring, the table and the Superstars
        (a Superstar behind them at the table no longer shows through them) -- seated, flinching or
        standing; and standing, seen from the ring side, the table is in front of their legs.
    Operator menu: COMMENTARY -> JR & KING AT THE TABLE, on or off.
  * (0.33.1) Jeff Jarrett, the Blue Meanie, Bret Hart and Jim Neidhart (the WrestleMania 2000 and
    Revenge models) stay inside the ring and the barricade as everyone else does. Their skeletons
    lacked No Mercy's split-chest bones, and the match reads every body bone's place (the ropes'
    edges, the barricade, who's in or out): one point of them stayed at the ring's centre, so they
    could walk past the ropes and run over the barricade into the crowd.
  * (0.35.1) The same four have their necks on the Superstar select screen and the VS page before a
    match (as in the match itself): the menus show a model with No Mercy's own rule, which leaves
    out a piece its own models don't use -- the piece these models' necks are.
  * (0.35.2) ARCADE controls: STRIKE and GRAPPLE go to the game the moment they're pressed again, so
    the joystick + STRIKE / GRAPPLE in a tie-up gives the move you asked for (they had waited a moment
    to see if the other came too, and by then the joystick was often back in the middle: the tie-up's
    plain slam or suplex came out). Taunts and the finisher have a button of their own, SPECIAL
    (where FOCUS was); it taunts as the game's own stick does, so no more doing the other man's taunt.
    CONTROLLER SETUP asks for SPECIAL in FOCUS's place.
  * (0.35.3) FOCUS is DEFEND + ACTION together, and it's instant: nothing waits -- each button is
    the game's own until both are down. (Pressed a moment apart, the first one's start shows: a
    flinch of a block, as a tap of DEFEND does.)
  * (0.35.4) Bret Hart enters to his own theme (from the music folder, below), in a music slot of its
    own (65): nothing replaced -- the Revenge theme he had stays in its slot (63), Neidhart's too.
  * (0.35.5) Freeze reports (crash_reports\) say more: whether the graphics card was lost (and, from
    the driver, what it was doing), what the match was doing (who's in it, the last frames), and the
    graphics no longer wait forever on a lost card: it's noticed within a second and logged.
  * (0.35.6) Bret Hart and Jim Neidhart enter without a Titantron video (Revenge has none for them;
    the stand-ins made from their Revenge entrances are gone).
  * (0.35.7) Bret Hart's and Jim Neidhart's hands: their fingers were striped orange and red (Revenge's
    finger picture on the hand shapes the game uses for them, laid out for another picture), changing
    as the hands moved. Now the fingers are drawn in their own skin. Crash reports say more: when the
    game stops in the mod's code, which of its functions it was in, and the code addresses on the
    stack (the report used to end where the stack couldn't be followed).
  * (0.35.8) The Royal Rumble freezes and crashes: the announcers at the table (who look at the action)
    took a Superstar's place that wasn't a number yet (one coming in) and, on the PC, went round a loop
    that never ended. Their every number is checked now.
  * (0.35.9) Bret Hart's finisher is his own, as in WCW/nWo Revenge: with SPECIAL lit, from a front
    grapple, he slams his man down and goes straight into the Sharpshooter (the slam and the
    Sharpshooter are Revenge's very animations). His and Jim Neidhart's other moves were matched
    again to Revenge's: far more of them are now the exact moves they did there (Bret 88, Neidhart 86).
  * (0.35.10) The Royal Rumble slowing down and crashing (the graphics card ran out of memory): a part
    of a model placed at a position that isn't a number makes it a giant for the ray tracer. Every
    part's place is checked as the scene is built now, and a bad one put where its parent is. The log
    (NoMercyRecompiled.log) notes the memory every 15 seconds, and crash reports say how much there was.
  * (0.35.11) Bret Hart's and Jim Neidhart's moves that No Mercy has no copy of now play with their own
    WCW/nWo Revenge animations (Revenge's motion, both his and his opponent's) instead of the nearest
    No Mercy move. Jim Neidhart has his ground special, the Boston Crab, as in Revenge.
  * (0.35.12, a test) The WrestleMania 2000 and Revenge Superstars (Jeff Jarrett, The Blue Meanie, Bret
    Hart, Jim Neidhart) have No Mercy's own hands, in the No Mercy skin tone nearest theirs, drawn the
    way No Mercy draws its own; their bare arms are No Mercy's arms in that tone, and the parts that are
    partly skin keep their own picture with the skin brought to the tone. (Faces and hair are theirs.)
  * (0.35.13) Bret Hart's special: the Sharpshooter after the slam is a real submission now -- he holds
    it (the submission meter runs) until the man gives up, gets to the ropes or breaks out, and lets go
    with the release, as his own ground Sharpshooter does. (The move was made over without the game's
    mark for a special, so the hold let go at once.)
  * (0.35.14) Bret Hart's Sharpshooter ends as a submission does: the slam rolls straight into the
    Sharpshooter's turnover, he holds it (the hold's own loop, the submission meter running), and when
    it's broken, let go or tapped out of, he stands and his man stays down on the mat -- No Mercy's own
    Sharpshooter release. (Before, the turnover was played as the hold and the hold's loop as the
    release: both snapped up standing.)
  * (0.36.5) Bret Hart's Sharpshooter, fixed: the man's legs are crossed up in Bret's arms with his hips
    under Bret, as a Sharpshooter is. The hold's animation (No Mercy's, and Revenge's, the same file) had the
    man's hips a body's length past Bret's feet and his legs nowhere near Bret's hands; he's brought in under
    Bret as the turnover ends, held there, and stays there through the release. His slam-into-Sharpshooter
    special and his ground Sharpshooter both.
  * (0.37.0) The Royal Rumble freeze is fixed. It wasn't Bret Hart's back suplex: the game paints a
    wrestler's face and skin colours into his palette, and the ported (WrestleMania 2000 / Revenge) models
    keep smaller palettes with their pictures right after them -- the paint ran over, the frame's list lost
    its end, and the game slowed, froze or crashed (and King appeared in the ring). Their palettes have room
    now, and the paint only goes where there's room. A Rumble Superstar who's eliminated with no one left to
    come in stays out (the CPU men brawled on for good). ONE UNDERTAKER IN THE RUMBLE (on): only one of the
    two Undertakers draws in.
    RAW IS WAR's Titantron: in the match the big screen shows a hard camera -- the ring from its side, high,
    as on TV -- inside the screen's own steel truss frame; in the entrances it plays the entering
    Superstar's video from his music's start to its end (up close, to the stage's truss). The picture is
    steady (the wrestlers no longer come and go from it).
    JR and King sit at their table: it's moved up toward the ring (what a Superstar bumps into moved with it),
    they sit up to it with their chairs clear of the barricade, the chairs are thin, and they're no longer
    drawn through their chairs or through themselves.
    CUSTOM STORY (first test): the championship list has a Custom Story row, in the left column under
    Women's (where the Belt box was). It plays a story written as a file (the source's
    mod\stories, built into the mod) through the game's own Story Mode: narration pages that name your
    Superstar and the story's cast, the match card with its requirement, the match, and where the story
    goes after it, win or lose. One story comes with it, "The Grudge". The game's own seven stories, and
    the story in progress in your save, are as they were. Operator menu: GAME SETTINGS > CUSTOM STORY
    (OFF: no row, and the Belt box is back).
  * (0.36.4) Bret Hart's struggling slam is gone, checked move by move. A strong grapple plays its move's
    variants (the counter, the block, ...), and the Scoop Slam put in his strong grapple slot in 0.36.2
    has none: whenever the game picked one, both men shoved and let go -- the struggle. That slot is a
    proper strong grapple now (the Back Body Flip), and his strong-grapple Suplex is Bret's own from
    WCW/nWo Revenge, every variant of it (his Revenge animations, as the other ported moves are).
  * (0.36.3) RAW IS WAR's Titantron, fixed for real and checked: during the entrances the big screen in the
    middle plays the entering Superstar's own Titantron video (with FULL SCREEN TITANTRONS off the game
    skipped the step where it picks the video, so the screen never got one; and up close the big screen
    is a different model of it, which wasn't being found). In the match it shows the second camera. Only
    the big screen: the fist screens on either side keep their own picture. (A Superstar without a
    Titantron video -- Bret Hart, Jim Neidhart -- leaves the stock screen up for his entrance.)
    The announce table is solid from the barricade to its front: there's no gap behind it any more for
    a Superstar to walk into and stand inside JR and King, and up on the table he stays in front of them.
  * (0.36.2) RAW IS WAR's Titantron works: the big screen and the fist screens show the second camera's
    view in the match (they kept the stock logo before: the screen's panels are drawn from the arena's
    own lists, one level down, and weren't found; and the picture's rows were loaded on top of each
    other). Bret Hart's front grapples 25 and 50 were No Mercy's struggling slam (Revenge's body slam is
    that very animation): the plain body slam and his stand-in's move now.
  * (0.36.1) A wrestler whose position goes bad (a value that isn't a number -- seen in the Royal Rumble,
    Jerry Lawler taking Bret Hart's back suplex) is put back where he last was, and drawn there, instead of
    flat across the middle of the screen (and the slowdown that came with it).
  * (0.36.0, first test: the RAW IS WAR arena only) The Titantron. The arena's big screen (and the fist
    screen) plays the entering Superstar's Titantron video during the entrances, and during the match
    shows a second camera's view of the match -- the main camera doesn't go to it; you see it when the
    screen is in the shot. Options: TITANTRON VIDEOS (on), TITANTRON LIVE CAMERA (on); the old option is
    FULL SCREEN TITANTRONS now (off: the full-screen video before an entrance is cut).
  * (0.35.15) Bret Hart is a heavyweight now, for real: No Mercy keeps a wrestler's weight in three of
    his abilities, and 0.35.14 changed only one of them -- the other two still said cruiserweight (his
    stand-in's, Dean Malenko's), so his strong grapples still turned into a struggling slam.
  * (0.35.14) A Story Mode title match whose title intro was skipped could stall on a white screen. The
    frame's picture was too big for the game's list for it (the arena, the crowd and a ported Superstar's
    model, with the intro's last frame still on): it ran past the end into the task that sends it, and
    the picture was garbage. Every frame is now built in the six-man engine's bigger lists (more than
    twice the room), as its five- and six-man matches always were.
Music files (the source's mod\music)
  A file there named for a Superstar ("Bret Hart.mp3") is his entrance theme: it gets a music slot of
  its own (nothing replaced; his own slot is left as it is). The files are made into the mod's themes
  when the mod is built -- the game doesn't read the folder itself -- so a new file there needs a new
  build of the mod. A theme is made like No Mercy Plus's: one sample, mono, at the rate the game plays
  its themes, looping on a stretch of the song that comes round again (the seam blended), and as loud
  as the game's own themes (brought to their level, its peaks held by a limiter, not clipped).
    Your save file stays the original game's, readable by the game without the mod: nothing of the new
    Superstars goes into it. Where the game keeps a Superstar in it -- a title's champions, an
    exhibition preset, a story in progress -- the game has them as usual while you play, and
    what it saves leaves them out: a title a new Superstar holds is saved with the champions
    before him, a preset with one in it is saved as an unused preset, a story with one in it as
    no story in progress.
  * No Mercy Plus V3's themes and Titantron videos for everyone: every Superstar enters to its
    version of their theme (it re-did them all -- longer, better recordings), and every
    Titantron video is its 72 frames of real footage. Played by the game's own music engine and
    video player; the data (about 28 MB) is inside the mod. Operator menu: "No Mercy Plus
    videos" (off: the original game's).
  * Commentary: Jim Ross and Jerry Lawler call every match, from WWE '13's Attitude Era
    commentary (thousands of their lines). They follow the match as it happens -- the bell, the
    lock-ups, strikes and big moves (by the move's name when they have a line for it: "My God,
    what a suplex!"), counters, SPECIAL lighting up, finishers, the referee's count with them
    ("One!" "Two!"), kick-outs at one and at two, three-counts, rope breaks, holds, a man thrown
    to the floor and back in, top-rope moves, one man taking it to the other and the other
    fighting back, a long match wearing them down, eliminations, each Royal Rumble entrant (his
    number as his countdown starts, then him by name as he walks out), the winner -- and talk about each
    Superstar by name where they have lines for him (the Rattlesnake, the Great One, the
    Phenom...). A bigger moment cuts a smaller one off, never the other way; nothing is
    repeated soon after; quiet stretches get the odd line of their own. It's a two-man booth: after
    JR calls something, King now and then has his say on it, and when one of them puts something to
    the other ("Did you see that, JR?") the other usually answers. Where they have them, the
    arena gets its own hello and good night (a Royal Rumble in the Royal Rumble arena, WrestleMania,
    SummerSlam, Survivor Series, King of the Ring, Raw is War, SmackDown). The game's sound goes a
    little lower while they talk. The lines are commentary.pak (and commentary.pak.1, .2, ...)
    next to the game: without them there's no commentary and everything else is the same.
    LINES FROM picks whose lines: WWE 13 (WWE '13's only), LEGENDS (JR and King's from WWE
    Legends of WrestleMania only) or BOTH (mixed; the Legends ones evened out to sound like the
    WWE '13 ones; only lines about Superstars who are in No Mercy, nothing about matches or shows
    it hasn't got).
    NEW SPLICED COMMENTARY: now and then JR calls a moment with one of his own lead-ins joined to
    the Superstar's name, both cut from his real lines ("What a move by" ... "Kurt Angle", "Here
    comes" ... "the Big Red Machine", "Down goes" ... "the Phenom"). A WWE '13 lead-in only ever
    gets a WWE '13 name, a Legends one a Legends name, and only a take of the name about as worked
    up as he is in the lead-in; each way of saying a Superstar's name is as likely as the others.
    They're extra variety -- the lines they were cut from, and the ones that fit anyone, are all
    still there -- and only the live calls (the count, kick-outs, the finish) cut one off. In a
    Royal Rumble an elimination nobody's named for gets a line that fits anyone ("That's it, he's
    out!", or a line about him going over the top rope to the floor). Off: none of the spliced calls.
    Operator menu: COMMENTARY.
  * F2: operator menu.

Starting the game
-----------------
  Ultimate No Mercy.exe  the game, and nothing else: no launcher, no port menus, no "are you
                         sure you want to quit?" (closing the window quits, as switching a
                         cabinet off does), no mouse cursor. Esc does nothing.
                         (It used to be called "NoMercy Arcade.exe".)
  It finds the ROM itself: the copy it keeps (wwf.nomercy.us.z64), or any WWF No Mercy (USA)
  (Rev 1) .z64 / .n64 / .v64 put in the game's "roms" folder or the game's folder itself (it's
  checked and kept the first time). Without one it says where to put it and closes.
  It also needs its content files (the "content" folder), which are made from game data and so
  are not in the download: see README.txt.

Install
-------
Unzip the download anywhere you can write to, put your ROM in "roms" (README.txt, step by step).
Portable: everything is in the game's folder (saves, settings, the kept ROM), so the folder can be
copied or zipped and run anywhere. The mod lives in assets\mods and is copied into the mods folder
each time the game starts. The port's own settings (window, graphics, controls, sound) are the .json files in the
folder (graphics.json, controls.json, ...), kept from the menus you set them in before.

Cabinet keys
------------
  5 / 6        insert coin (player 1 / player 2)
  1 / 2        start (or Start on controller 1 / controller 2)
  F2           operator menu (arrow keys + Enter + Esc, or a controller's d-pad and A)

Controllers
-----------
  Xbox / PlayStation / other gamepads and arcade encoders work (plug in any time). The first
  controller is player 1, the second player 2 (operator menu: CONTROLS > SWAP CONTROLLERS 1/2).
  Pad: move on the d-pad or left stick, A = A, X = B, left stick click = Z, LT / RT = L / R,
  Y / B = C-up / C-down, LB / RB = C-left / C-right, START = start, BACK / SELECT = coin.
  About 2,000 known controllers are recognised by themselves (gamecontrollerdb.txt). Anything
  else (arcade encoders, odd pads, directions mixed up): operator menu > CONTROLS > P1 / P2
  CONTROLLER SETUP, then press each control as asked (UP, DOWN, LEFT, RIGHT, A, B, Z, L, R,
  C-UP, C-DOWN, C-LEFT, C-RIGHT, START, COIN; SPACE skips one, ESC cancels). It's remembered in
  controller_map.txt. INPUT TEST shows every control as it's pressed.

Buttons and the analog stick: every N64 input bindable (operator menu: CONTROLS > KEYBOARD (P1):
BUTTONS & STICK, ALL CONTROLLERS: BUTTONS & STICK, P1 .. P4 CONTROLLER: BUTTONS & STICK)
------------------------------------------------------------------------------------
  Each of the N64 pad's inputs can be put on a key (player 1's keyboard) or on a controller's
  button, stick or trigger: A, B, Z, L, R, START, C-UP, C-DOWN, C-LEFT, C-RIGHT, UP / DOWN /
  LEFT / RIGHT (the d-pad: moving), ANALOG STICK UP / DOWN / LEFT / RIGHT (the N64 analog stick:
  taunts and specials), and SPECIAL (STICK PUSH): while it's held,
  the analog stick is pushed all the way the d-pad is held (straight up with no direction), so a
  taunt or, in a tie-up with SPECIAL lit, the finisher is one button. SPECIAL is R3 (the right
  stick's click) on a controller and U on the keyboard to start with.
  The analog stick is the first row of each page (ANALOG STICK): on a controllers' page LEFT /
  RIGHT puts the N64 stick on the pad's left or right stick (all four ways at once), or ENTER and
  move the stick you want; on the keyboard's page ENTER asks for its four keys in turn (up, down,
  left, right; I K J L to start with). To start with it is the pad's RIGHT stick, the left stick
  moving the man with the d-pad; putting it on the left stick takes the left stick off the d-pad.
  A key pushes the stick all the way (no walk key, as in the other N64 ports). A page longer than
  the screen says MORE BELOW / MORE ABOVE.
  Controllers: ALL CONTROLLERS is the default for every player; P1 .. P4 CONTROLLER are each
  player's own, row by row: a row he hasn't set is the default's (grey on his
  page), one he has is his (yellow). His buttons follow him whatever controller is his (plugged
  in, SWAP CONTROLLERS, plugged in again), and setting a button on his page takes it from his own
  controller only. USE THE DEFAULT (ALL ROWS) gives him the default's back.
  Pick a row and press ENTER (or A), then the key, button or stick (ESC cancels); DELETE clears a
  row; RESET TO DEFAULTS puts the keyboard's or the default's factory buttons back. Taking a key
  for one row takes it off the others (a key does one thing). Saved as soon as it's set, in
  controls_n64.json beside the game (the keyboard's and the default's rows you changed, and each
  player's own; delete it for the factory buttons). A controller with no layout goes through
  CONTROLLER SETUP first. In 6-BUTTON mode the analog stick moves your man and SPECIAL is the
  cabinet's (C-RIGHT with the joystick); the buttons set here still feed it.

Arcade controls (operator menu: CONTROLS > CONTROL MODE: ARCADE)
---------------------------------------------------------------
  For a cabinet: an 8-way joystick and six buttons. CONTROLLER SETUP then asks for UP, DOWN,
  LEFT, RIGHT, STRIKE, GRAPPLE, RUN, DEFEND, ACTION, SPECIAL, START and COIN. (On a gamepad
  they're already there: X = STRIKE, A = GRAPPLE, B = RUN, RT = DEFEND, LB = ACTION, RB = SPECIAL.)
  Suggested panel:   top row     STRIKE   GRAPPLE   RUN
                     bottom row  DEFEND   ACTION    SPECIAL
  In a match:
    Joystick         move (a joystick that reports as a stick works too)
    STRIKE           punches and kicks (tap / hold, with the joystick: the game's B)
    GRAPPLE          tie-ups (tap / hold: weak / strong) (the game's A). In a tie-up, the joystick
                     + STRIKE or GRAPPLE picks the move, exactly as in the game
    RUN              run; climb the turnbuckle; slide into the ring (the game's C-down)
    DEFEND           block strikes and reverse grapples at once (the game's R and L together)
    ACTION           what fits where you are:
                       by a man down: tap = pin him, hold = pick him up
                       at the ropes, on the apron, or outside by the ring: through the ropes
                       anywhere else: pick up a weapon / turn your man around (the game's C-left)
    SPECIAL          a taunt (push the joystick with it for your other taunts) -- or, in a
                     tie-up with SPECIAL lit, your finisher (the game's analog stick, flicked)
    DEFEND + ACTION together = FOCUS: who you're after (matches with more than two), the moment
                     both are down (holding DEFEND, then pressing ACTION, works too): a tap goes
                     to the next one, nearest to furthest; hold both and push the joystick to
                     pick the one that way on the screen. As in the game, you still turn to
                     whoever attacks you. A small arrow in your colour (player 1 red, player 2
                     blue) marks him over his head, and fades about three seconds after you stop.
  The game's own button combinations work as they are (STRIKE + GRAPPLE in a strong tie-up with
  SPECIAL lit: steal his finisher).
  Menus work as always (STRIKE = B, GRAPPLE = A). CONTROL MODE: REGULAR is No Mercy's own controls.

Operator menu (F2)
------------------
A page of its own, as the other arcade ports have it (the game waits behind it). UP / DOWN
choose, LEFT / RIGHT (or Enter) change, Esc goes back. SAVE AND EXIT (or F2 again) keeps the
settings; EXIT WITHOUT SAVING puts them back as they were.
  GAME SETTINGS
    Continue timer       5-30 seconds
    Fast get up          no slow hurt get-ups (see above)
    Unlock everything    hidden Superstars, SmackDown Mall items (on to start with)
    Arcade speed         OFF, +5% .. +30% (+15% to start with)
    Attract demos        CPU demo matches in the attract loop
    Titantron videos     each entrance opens with the full-screen video (off to start with)
    No Mercy Plus themes/videos   everyone's themes and Titantron videos from No Mercy Plus V3
    Merge same wrestlers   the same wrestler from each game in one box on the Superstar Select, as
                         variants: No Mercy's own four first, then the others' (each keeps his own
                         moves, music, name and Titantron) -- the families the roster editor's
                         "Consolidate automatically" makes, by the full name and the known-identity
                         list (data\aliases.json); nothing is written. Off to start with; a change
                         counts from the next start-up (as GAME MODE)
  COMMENTARY
    Commentary (JR & King)   on to start with
    Volume               0-100 (80 to start with)
    Amount               LIGHT, NORMAL, HEAVY, NONSTOP (NORMAL to start with)
    Lines from           WWE 13, BOTH, LEGENDS (WWE 13 to start with)
    New spliced commentary   on / off (on to start with)
  COIN / CREDIT SETTINGS
    Free play            start without coins (and no continue countdown)
    Coin sound           OFF, 1-4: one of the game's own sounds (1 to start with)
  ROYAL RUMBLE
    Superstars in it     2-40, you included (15 to start with)
    Time between entrants   10-120 seconds (30 to start with)
    CPU goes for eliminations   on to start with
    No women / announcers   (as in WRESTLER OPTIONS)
  WRESTLER OPTIONS
    Big Show, Chris Benoit (off to start with), Funaki, Gangrel, Mosh, Thrasher, Jeff Jarrett,
    Droz, Mideon, The Blue Meanie, Pete Gas, Rodney, Joey Abs, Undertaker 1999, Raven,
    William Regal, Tiger Ali Singh --
    each on or off
    No women / announcers in Rumble   on to start with
  CONTROLS               CONTROL MODE (REGULAR / ARCADE), the players' controllers, CONTROLLER
                         SETUP, SWAP CONTROLLERS 1/2, KEYBOARD (P1) / ALL CONTROLLERS / P1-P4
                         CONTROLLER: BUTTONS & STICK (every N64 input on a key, button or
                         stick, per player; the analog stick first; SPECIAL; controls_n64.json)
  INPUT TEST             every control of both players (Esc, or both START buttons: back)
  RESTORE FACTORY SETTINGS, SAVE AND EXIT, EXIT WITHOUT SAVING, QUIT GAME
The settings are kept (mod_config\nomercy_arcade.json). The roster settings change between
games: a game in progress keeps its roster, and the next one has the new.

Known limits
------------
  * No taunt on the stage yet: the entrance wrestler's animations aren't something the mod can
    drive yet.
  * The arena's big screen behind the stage shows the arena's own picture during the walk
    (as in the original game); the video plays full screen before it.
  * Built and tested on an emulated N64 running the same code; the controller setup itself
    (the exe's part) can only be tried on the PC.

ADDING YOUR OWN CONTENT
  Wrestlers: make a folder inside the "wrestlers" folder, with a wrestler.json in it. Copy wrestlers\_example to start
    (folders starting with an underscore are skipped). He starts as a copy of "based_on", and you change what you like:
    name, short name, nickname, height, weight, music slot, Titantron video. He appears on a MORE page of the Superstar
    Select, or wherever select_pages.json puts him, and stories can name him. If a file has a mistake, he's skipped and
    the reason is in the log.
    Up to 112 can be added. A photo (photo.bin) and a full-detail model (hd.bin, hd.json, pictures.pak) can go in the
    folder too: tools\wrestler_folder.py in the source makes them.
    His own entrance theme can too (theme.bin, theme.meta): tools\wrestler_theme.py makes them from a music file. So can
    his Titantron video (video.bin): tools\wrestler_video.py makes it from a video file.
  Arenas: make a folder inside the "arenas" folder, with an arena.json in it. Copy arenas\_example to start. It starts as a
    copy of the arena it's "based_on" (tools\arena_folder.py --list shows the numbers) and comes after the rest on the
    ARENA page. tools\arena_folder.py exports the base arena's stage to edit, as files named by game file ID, which
    replace the game's while that arena is in play, and makes its label on the page. It keeps the base arena's walls and
    its number of stage parts for now.
  Texture packs: make a folder inside "texturepacks" with PNG files in it. To get the pictures to edit, make a folder
    called "_dump" in "texturepacks", and the game saves every picture it draws there, named by its contents. Edit them
    at any size and move them into your pack's folder. Delete "_dump" when you're done, as saving slows the game a
    little. Later packs (by folder name) win over earlier ones. Put an underscore at the start of a pack's name to turn it off.

