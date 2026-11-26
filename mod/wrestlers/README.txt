ADDING A WRESTLER

Each folder with a wrestler.json in it is one wrestler. He can be right in here, or inside folders of your own, as deep
as you like: wrestlers\WCW\nWo\sting, wrestlers\Japan\80s\... Those other folders are only your own way of sorting
them: move and rename them whenever you like, it changes nothing. (A folder whose name starts with an underscore is
skipped, with everything in it.) To add one:

  1. Copy the _example folder and rename the copy. Copying any wrestler's folder works too, even to have two of him
     exactly alike: both are on the Superstar Select.
  2. Edit wrestler.json:
       "based_on"    The wrestler he starts as a copy of: looks, moves, music and size. His string ID ("steve_austin",
                     or another folder's), or his number ("0x0002"). The game's stories\_roster.json has them all
                     (the IDs are the lines starting @ID:). A name ("STEVE AUSTIN") still works in older folders, but
                     names on screen can change: use the ID.
       "id"          Optional. His ID is his folder's name in small letters ("Rock On Austin" -> rock_on_austin). Give
                     one here ("id": "stone_cold_99") to keep it whatever the folder is called. Folders made before
                     2026-11-12 have a number here, like "0x0340": he keeps it while no one else has it.
       "name"        His full name, up to 20 characters.
       "short_name"  His name in the match's bars, up to 10.
       "nick"        A nickname, up to 16. null ("nick": null) for none at all; left out, he keeps the copy's.
       "height"      Inches over 5'0".
       "weight"      Pounds, less 100.
       "music"       A music slot number.
       "video"       A Titantron video number (0: none).
     Only based_on is needed. Leave out anything you want kept from the copy.
     Two more that the importers (Development\tools\handlers) write, not usually typed by hand:
       "looks"       His four variants' looks as numbers: four lists of the 38 fields the game keeps a variant's
                     looks in (body, face, hair, attire, colours ... and the photo), in the game's order
                     (exe arcade_wrestlers.cpp lists them). Any two wrestlers may have the same.
       "moveset"     His moveset, packed as the game keeps it (216 bytes of hex).
  3. Start the game. He's on a MORE page of the Superstar Select, unless select_pages.json puts him somewhere else.
     Stories and select_pages.json name him by his ID ("rock_on_austin"), or by his name or short name in capitals.

THE GAME'S OWN SUPERSTARS

Every stock Superstar can be a folder too, and the game then loads him from it: wrestlers\No Mercy\the_rock and the rest,
64 of them, which the No Mercy handler writes from your own No Mercy ROM (it reads the ROM, and never copies it):
  python Development\tools\handlers\nm_import.py --out <the game's wrestlers folder>
Such a folder says "builtin" (which Superstar he is: his ID) where a new wrestler says "based_on", and has all of him:
his name, short name, nickname, height, weight, music and video; "per_variant" for a variant that is someone else (Taka's
second is Funaki, Jim Ross's Jerry Lawler ...; folders written before 2026-11-14 say "per_costume": still read); "looks",
his four variants as numbers (the game builds his model from them out of its own body parts); "moveset" (and moves.json,
the same moves slot by slot); photo.bin, and photo_c1.bin .. for a variant with a photo of its own. Change any of it and he changes, in every mode. Leave a field out and that field is
the game's own again: the game's tables are always there behind the folders.

Delete a stock Superstar's folder and he is REMOVED from the game (2026-11-13): no box on the Superstar Select, never
drawn at random or in a Royal Rumble, and wherever something still asks for him -- Story Mode, a custom story, an
exhibition preset, a demo -- the man after him on his select page stands in (Triple H for The Rock). A title he holds is
vacated at once: it has no champion until someone wins it, and the save is written so.
If he heads a family (below), the family stays where his box was, headed by the next of it (2026-11-14: Undertaker 1999
in The Undertaker's box); folders nested in his go with him, being inside it. Other men's run-ins and partners leave him
out while he is away. To take a wrestler off WITHOUT deleting anything, hide him instead (below). Nothing else of him is lost: your save keeps him in its presets and its history, and the game's
log says what it did with each (lines starting [removal]). To bring him back, run the handler again for him
(nm_import.py ... --only the_rock): his folder is written again and he is on the roster as he was -- but a title he lost
while away stays lost. This works because wrestlers\No Mercy\stock_roster.json lists the men the handler wrote: without
that file a missing folder just means the game's own Superstar, as before.

Two with the same ID (a copied folder, or two that set the same "id") are both on the roster: the second, in folder
order, gets ~2 after his ID (rock_on_austin~2), the next ~3, and the game's log says so in a line with ID CLASH. Set an
"id" in one of them to choose his ID yourself. The log's [wrestlers] lines show every wrestler's ID and number.

If he doesn't appear, the game's log says why: look for lines starting [wrestlers].

FAMILIES: ONE BOX, MANY VARIANTS (2026-11-14)

A box on the Superstar Select can hold a FAMILY: an ordered list of VARIANTS, each one wrestler's variant 1-4. The Type
arrows (C-left / C-right) step through them in order, and each plays as his own wrestler -- his model, name, moves, music
and Titantron. Any number of variants, in any order, mixed as you like. Two ways, and neither changes a wrestler's folder:

  1. families.json, here in the wrestlers folder (or in any collection folder of yours):
       {"families": [
         {"id": "chris_jericho", "variants": [
           {"wrestler": "chris_jericho", "variant": 1}, {"wrestler": "chris_jericho", "variant": 2},
           {"wrestler": "chris_jericho_wm2000", "variant": 1}, {"wrestler": "0x0371", "variant": 1}]}],
        "hidden": [{"wrestler": "kane"}, {"wrestler": "undertaker", "variant": 2}]}
     "wrestler" is his ID (or his number), "variant" which of his four. The box is the FIRST variant's wrestler's, where
     his box is; the others' own boxes are off the pages -- but a variant the family doesn't name is never lost: the
     first wrestler's others go at the family's end, another's keep a box of his own ("unlisted": "off" on a family
     leaves them out). A wrestler is in one family. With no families.json anywhere
     the game has the families it always had (The Undertaker + Undertaker 1999, Big Show + The Giant, the Revenge
     wrestlers' second looks); once a families.json has a "families" list, the files are all there is -- so start from
     the default one: python Development\tools\families_check.py --write-default wrestlers\families.json
  2. Nesting: move a wrestler's folder INSIDE another wrestler's folder (wrestlers\No Mercy\chris_jericho\
     revenge_jericho\). He joins that wrestler's family, after his four variants (nested folders in name order). A
     families.json that names him puts him where it says instead.

HIDDEN: "hidden" in a families.json takes a wrestler (or one variant of his) off the select and out of every random draw
(Random, the Royal Rumble's field), deleting nothing: whatever names him -- a title he holds, a story, a preset, your
save -- keeps him, and taking the line out brings him back exactly as he was. A hidden family head: the next variant
heads the box.

MERGE SAME WRESTLERS (F2 > GAME SETTINGS, off to start with; it counts from the next start-up): the game puts the same
wrestler from each game in one box by itself -- No Mercy's own four variants first, then WrestleMania 2000's, then
Revenge's, then the rest -- exactly as the roster editor's "Consolidate automatically" would, with the editor's own
known-identity list (data\aliases.json beside the game: the FULL name, or a known identity such as PRINCE ALBERT =
ALBERT; never a nickname, and never one person's different characters such as MANKIND / CACTUS JACK). Nothing is
written and no folder changes: a family families.json makes stays (the others added at its end), a hidden wrestler
stays hidden. The log's "[families] auto-merge" lines say who joined whom.

The game's log says what it made of it all: lines starting [families].

WHAT HE IS: "class" (2026-11-21)

"class" in a wrestler.json says what he is on the roster: "wrestler", "woman", "valet", "manager", "announcer",
"referee", "official" or "authority" (an owner who wrestles, as Mr. McMahon). The random pools -- the Royal Rumble's
draw with NO WOMEN / ANNOUNCERS on (F2), a Lumberjack match's lumberjacks -- take only a wrestler or an authority; whom
you pick yourself is never checked. The importers write it from each game's own data ("class_from" says which). A
wrestler.json without it: as before (No Mercy's women's pages, Earl Hebner and Jim Ross are left out by their numbers).
To change one: {"class": "wrestler"} in his wrestler.override.json.

RIVALS: rivals.json (2026-11-21)

Who is whose rival, besides No Mercy's own (each moveset's Rival 1-3): rivals.json here in the wrestlers folder (yours:
nothing ever writes it) and/or in any collection folder. The importers put one in each collection they make: Revenge's
men's real feuds of 1997-98, WrestleMania 2000's own Rival 1-3 (each game's men only). Never written over.
  {"rivals": [["goldberg", "hollywood_hogan"], {"pair": ["raven", "saturn"], "why": "Fall Brawl 1998"}],
   "not_rivals": [["the_rock", "triple_h"]]}
A wrestler is his ID (or his number); in a collection's file a name is first that collection's own folder. "not_rivals"
takes a pair out, the game's own too. A Lumberjack match's lumberjacks brawl only with their rivals. The log's lines
starting [rivals] say what was read.

YOUR CHANGES: wrestler.override.json (2026-11-17)

The importers write wrestler.json again every time they import him from your ROMs. To change a wrestler so that it lasts,
put your changes in a file of your own beside it, wrestler.override.json -- only what you change:
  {"name": "THE LADY", "nick": null, "height": 8, "body": {"nm_size": 70}}
The game lays it over his wrestler.json each time it reads him (a JSON Merge Patch, RFC 7386):
  O1  an object goes into the object at the same place, key by key: {"body": {"nm_size": 70}} changes only nm_size;
  O2  anything else -- text, a number, a list -- replaces what wrestler.json has (a list whole: give all four "looks");
  O3  null removes the key ("nick": null: no nickname of his own);
  O4  the override always wins over wrestler.json, and wrestler.json over the man he is based on;
  O5  his identity stays the folder's: "id", "string_id", "builtin" and "replaces" in it are left out;
  O6  a file that isn't a JSON object is not used (the log says so); with no file nothing changes;
  O7  only wrestler.json is overridden (not moves.json, his model, photo or theme).
No importer writes or deletes it, and a re-import leaves it where it is: copy a new import over the old folder, never
"mirror" it. The log says when one is used: [wrestlers] ...wrestler.override.json: laid over his wrestler.json.
Check them: python tools\wrestler_override.py <the wrestlers folder> (families_check.py lists them too).

Files you can add to the folder, each made by a tool in the Development\tools folder:
  photo.bin                        His select photo. wrestler_photo.py makes it from any picture.
  theme.bin, theme.meta            His entrance theme. wrestler_theme.py makes them from a music file.
  video.bin                        His Titantron video. wrestler_video.py makes it from a video file.
  hd.bin, hd.json, pictures.pak    A full-detail model, imported from another game. wrestler_folder.py packs one built
                                   with the tools in data\austin2k15. He also needs a photo.bin, and based_on must be a
                                   built-in WCW/nWo Revenge port (goldberg, sting, ...): the model is drawn over that
                                   port's bones. (The two built-in full-detail test slots, stone_cold_2k15 and
                                   the_rock_2k15, were removed on 2026-11-23.)

  model\model.json, model\c0.bin .. c3.bin, model\files\
                                   His own low-detail model, the N64 kind (the importers write it from the game he
                                   comes from). He also needs a photo.bin. Any number of wrestlers can have one: the
                                   game gives a model's files numbers only while it loads him.

Limits today: no count of wrestlers from folders -- every folder is on the roster (2026-11-13). The game itself knows a
wrestler by a one-byte number, and 112 of those are left for folders: they are handed out as the game needs them (the
select's pages on screen, the match, the Rumble's entrants, the champions) and taken back when nothing uses them, so up to
112 folder wrestlers are in play at once, out of any number. That is temporary: the fix is the game's records made native,
with wide IDs. With that many folders the select's pages are a window that moves with the page shown (L / R go on
through all of them). A champion from a folder is kept beside the save (saves\<save>.ours.json), because the save itself
can't hold him. Still counted: 14 of them with full-detail models (the first 14 folders that have one), about 1,700
folders in all (the game's 12-bit wrestler numbers), 4 variants each (a family: any number of variants). Themes and videos from folders have no count.
(Low-detail models of their own: no limit on how many wrestlers have one or how many files their folders hold; as
many on screen at once as the game's 32 places for wrestlers; one variant names at most as many files as the game's
model record has places for -- Stage 2's native records lift that.)
