ADDING AN ARENA

Each folder in here is one arena. It starts as a copy of one of the added arenas, and you replace what you like.

The quick way, from the source folder (the project's source, with its toolchain):

    python tools\arena_folder.py --list
    python tools\arena_folder.py "..\arenas\my_arena" --based-on 13 --name "MY ARENA"

That writes:
  arena.json       Its name and the arena it's based on.
  label.bin        Its row on the ARENA page, with its name on it.
  files\XXXX.bin   The base arena's stage: each part's model, palette and picture, named by game file ID.

Change or delete any of those files. Whatever is there replaces the game's file while this arena is in play, and whatever
isn't there stays the base arena's. A palette is 16 colours (RGBA5551, 32 bytes). The file after it is its 64 x 64 picture
at 4 bits a pixel: an 8-byte header, then 2048 bytes.

It appears at the end of the ARENA page, and stories can name it. If it doesn't appear, the game's log says why: look for
lines starting [arenas]. Folders starting with an underscore are skipped.

Limits today: it keeps the base arena's collision walls, its number of stage parts, and its preview pictures. So it can look
different but it can't be a different shape yet.
