"""The customs handler (2026-11-12): DESIGNED, NOT WRITTEN YET. Mods of No Mercy (No Mercy Plus V3 is the one in hand) and
of the other AKI games, texture packs among them.

A mod of No Mercy is No Mercy's own formats, so it needs no conversion, only a diff against the user's clean ROM:
  - apply the mod's patch (xdelta, IPS) to a copy of the user's ROM, in memory (data/nmplus: NM+ V3 is an xdelta against
    v1.0; mkovr/mkparts/mkroster read it as nmfile());
  - its file table: the mod may move it (NM+ V3: ROM 0x2BBC4D0, 0x7802 entries) -- aki.romtable with the mod's numbers,
    found by scanning for the table's shape (an increasing run of offsets with bit 0 flags) rather than hard-coded;
  - per wrestler whose record / looks / moveset / files differ from the clean ROM: a folder. Looks that fit the game's
    packed fields stay a costume key; looks that use the mod's new parts become a model of his own (model/: the mod's own
    func_80018A3C run on its RAM image, as gen/mkovr.py does for Droz, Mideon, Raven, Regal, the Posse, Undertaker 99,
    Tiger Ali Singh); moves map by animation record (gen/mkroster.py port_moveset: NM+ renumbered its moves);
    themes / videos / photos as files.
  - a texture pack: pictures by content hash (the exe's texture packs already work that way) -- copied into the folder of
    the wrestler whose model uses them, or left a global pack.
A mod of another AKI game goes through that game's handler with the mod applied to its ROM first (the same patch step).
"""
