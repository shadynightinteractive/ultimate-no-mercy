Ultimate No Mercy -- WWF No Mercy as a native PC game, with the Ultimate No Mercy arcade mod
==========================================================================================

READ THIS FIRST: what this download is
--------------------------------------
This download contains NO game data: no ROM, no graphics, sound, models or other files from
any game. You must own WWF No Mercy and supply your own ROM of the USA Rev 1 release.

In it: the program (Ultimate No Mercy.exe and its DLLs), the Ultimate No Mercy mod without
its game-data tables (firstrun\: the first start fills them from your ROM), the AkiMods, the
program's own fonts and icons, sample custom
stories, the Superstar Select's page list (select_pages.json), the templates for your own
wrestlers and arenas (wrestlers\_example, arenas\_example) and these READMEs and licences.

THE FIRST START BUILDS EVERYTHING FROM YOUR ROMS
-----------------------------------------------
The arcade's game-data tables, its content files (the "content" folder) and the wrestler
folders of your other games are made from YOUR ROMs on the first start, on your PC: a status
window shows what is being made (about a minute or two with every ROM). Nothing is
downloaded and nothing leaves your PC. Later starts build again only when you add a new ROM.

Getting started
---------------
1. Unzip this folder anywhere you can write to (not Program Files): everything the game
   writes -- saves, settings, the log -- stays in this folder (portable.txt).
2. Put your WWF No Mercy (USA) (Rev 1) ROM in the "roms" folder (a .z64, .n64 or .v64 file;
   SHA-1 of the .z64: 91CEE3D096F4A76644D8B35B9AEAD6448909ABD1). The original USA release
   (Rev 0, "v1.0") is not accepted as the main ROM (it is only No Mercy Plus V3's base).
   See roms\README.txt.
3. Double-click "Ultimate No Mercy.exe". It goes straight into the game: no launcher, no
   menus of its own. The first time it checks your ROM, keeps a copy beside the program
   (wwf.nomercy.us.z64) and builds what it needs (above). Keep your other games' ROMs in
   roms: one taken out is left out at the next start (the wrestler folders made from it stay).
   - No ROM found: a message says where to put it, and the program closes.
   - The wrong version (Rev 0): a message says so, and the program closes.
   - Windows may warn that the program is unsigned (SmartScreen): "More info" > "Run anyway".
   - Updating an older folder: the program used to be called "NoMercy Arcade.exe". After
     unzipping the new version over the old folder, delete "NoMercy Arcade.exe" (only that
     file: your saves and settings stay). Use "Ultimate No Mercy.exe" from then on.
4. Coins and start: 5 / 6 insert a coin for player 1 / 2, 1 / 2 start. F2 is the operator menu
   (settings, controls, quit). F11 or Alt+Enter: fullscreen. Closing the window quits.
   "ARCADE README.txt" explains the arcade, the controls and every setting.

Optional: more Superstars and arenas from your other games
-----------------------------------------------------------
Put your copies of these in the "roms" folder too (roms\README.txt lists them):
WCW/nWo Revenge, WWF WrestleMania 2000, WWF No Mercy v1.0 with the No Mercy Plus V3 patch,
the WCW/nWo Revenge Redux patch, WWF WarZone - Badd Blood. The next start makes their
Superstars, moves, arenas and themes: each wrestler goes in its own folder inside "wrestlers"
(wrestlers\README.txt describes the folders), and content\import_report.txt says what was made.
Your own folders always come first: an import never writes over a folder that is there.
To make everything again, put an empty file named reimport.txt in the "content" folder and
start the game.

Optional: commentary
--------------------
Jim Ross and Jerry Lawler's commentary is made from your own copy of WWE '13 (Wii). Put its
disc image in the roms folder as an RVZ file (Dolphin: right-click the game, Convert File...,
RVZ) and start the game: it makes commentary\wwe13.pak by itself, once, in the background,
and says how it went in commentary\wwe13_import_report.txt. A plain .iso or .wbfs is
encrypted: it works if you also put your Wii's key file in roms (keys.bin as BootMii dumps it,
or common-key.bin); otherwise convert it to RVZ first. No key comes with the game. A commentary pack you already have
works too: put it in the commentary folder (or beside the program as commentary.pak) and the
game uses it as it is -- yours always comes first, and the game never writes over it.
Without a pack there is no commentary and everything else is the same.

What it is
----------
A native PC port of WWF No Mercy (N64), built on a partial, behaviour-tested (non-matching)
decompilation: the game's own code converted to C++ and tested against the original, the rest
still running as N64Recomp's translation of the original (static recompilation). The Ultimate
No Mercy mod changes and adds to the game on purpose (an arcade: coins, attract mode, Royal
Rumble, more Superstars, custom stories, ...).

Files
-----
  Ultimate No Mercy.exe       the program
  *.dll                       SDL2 and FFmpeg (FFmpeg-*.txt: its licence and build)
  mods\, assets\mods\         the AkiMods; the Ultimate No Mercy mod once the first start made it
  firstrun\                   what the first start makes the mod and the content from (ours)
  roms\                       your ROM goes here
  wrestlers\, arenas\         your own Superstars and arenas (README.txt in each, _example)
  stories\                    custom stories (JSON); samples
  texturepacks\               your own texture packs (README.txt)
  select_pages.json           the Superstar Select's pages: yours to edit
  portable.txt                keeps saves and settings in this folder

Updating
--------
New versions come out on the project's GitHub Releases page:
https://github.com/shadynightinteractive/ultimate-no-mercy/releases
Download the new zip and unzip it over this folder: your saves, settings, controls and
wrestler folders stay, and the next start rebuilds what it needs. Automatic updates are
planned for a future version.

Reporting a problem
-------------------
Open an issue on https://github.com/shadynightinteractive/ultimate-no-mercy/issues and choose
"Bug report". Say which version you play and name the ROMs in your roms folder -- never
upload ROMs or game files. NoMercyRecompiled.log in this folder helps too.

Credits and thanks
------------------
A big thank-you to the makers of the mods this game can read:
  - No Mercy Plus V3 -- by Retro Randy Price
  - WCW/nWo Revenge Redux -- by Retro Randy Price
  - WWF WarZone - Badd Blood (Non Homebrew ID) patch -- by Wallywood84
Ultimate No Mercy does NOT include anything from these mods: no files, graphics, sound or
data of theirs are in this download. Their content appears only if you get the mods
themselves from their authors and put your own copies in the roms folder (roms\README.txt);
the game reads them on your PC. Please support the original mods.
More mod compatibility is planned: support for other mods and source games will be added in
future releases.

Also built on: WWF No Mercy: Recompiled and the AkiMods by jessetbh; N64Recomp,
N64ModernRuntime and RT64 by Wiseguy, DarioSamo and contributors; frame interpolation by
Gadge64.

Licences
--------
GNU GPL version 3 (COPYING; GPL-3.0-only): the port (based on WWF No Mercy: Recompiled,
https://github.com/jessetbh/WWFNoMercyRecomp) and the mod. FFmpeg: LGPL (FFmpeg-LICENSE.txt,
FFmpeg-dav1d-LICENSE.txt, FFmpeg-BUILD.txt). SDL2: zlib (SDL2_LICENSE.txt). Controller
database: gamecontrollerdb_LICENSE.txt. Fonts (SIL OFL 1.1): assets\Inter_OFL.txt,
assets\NotoEmoji_OFL.txt, assets\promptfont\LICENSE.txt. The other libraries built into the
program: libogg, libvorbis, zstd, CORE-MATH, musl, vgmstream, Pillow (their *_LICENSE.txt).
This project is not affiliated with or endorsed by Nintendo, THQ, WWE or AKI.
