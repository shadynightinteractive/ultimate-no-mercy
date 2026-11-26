// Whether this copy of the mod holds the built-in extras (firstrun, 2026-11-17).
//
// The built-in extras are what the No Mercy ROM alone can't give: the ported Superstars (WCW/nWo Revenge's, WrestleMania
// 2000's, No Mercy Plus V3's), No Mercy Plus's themes and Titantron videos, the arcade's own arenas (Revenge's and WrestleMania
// 2000's). Their tables are compiled into the mod, and their files are the content packs (content\content.bin, wmport.bin).
// (no2k15, 2026-11-23: the two WWE 2K15 test slots, and their packs hd.bin / hd_rock.bin, are removed.)
//
// A release carries no game data, so the mod it ships has every table made from a game left empty (zeros), this flag among
// them; the game's first start fills them in from the player's ROM (exe src/main/arcade_firstrun.cpp). What the No Mercy ROM
// gives is always filled; the extras' tables, and this flag with them, only when their content files are there. Without
// them the flag stays 0 and the mod leaves the extras out (arcade.h arcade_extras): the game's own Superstars, themes and
// arenas, everything else of the arcade as it is.
//
// A build of the mod (build_mod.ps1) has every table, so this is EXTRAS_ALL there, and the game plays as it always has.
//
// (phase3 2026-11-19) Each extra comes from one or more source games, and a player may have some of them only (say WCW/nWo
// Revenge and WrestleMania 2000, not No Mercy Plus). So the flag is a set of bits, one per source the first start built the
// extras' tables from (arcade.h EXTRAS_*); their tables keep their places whatever the set (an absent source's parts zero),
// and an extra is offered only when every source it needs is in -- the two tables below (ours). With every source: as always.
#include "arcade.h"

u32 arcade_extras_in = EXTRAS_ALL;

// the sources a roster option's Superstar (or feature) is made from (gen/mkroster.py: EXT / LATE's nm / wm / rev keys)
// (nmplus 2026-11-19, the user's rules: No Mercy Plus is a source of its own, and no item may need two source games)
//  - Big Show: No Mercy Plus. He was cut from the stock game (only his face and his BIG SHOW shirt are left in it: bigshow.c);
//    No Mercy Plus gives him back his moves, photo, theme and video (roster.c roster_bigshow_moveset, media.c). (content2,
//    the user 2026-11-19: Big Show only with No Mercy Plus -- without it he is nowhere: not on the select, not in a Rumble's
//    draw, not a stand-in, and Story Mode keeps Steven Richards in his own parts.)
//  - No Mercy Plus's media, its four, Droz, Mideon, the 1999 Undertaker and the rest of its men: No Mercy Plus alone (Droz,
//    Mideon and the Undertaker no longer take moves from WrestleMania 2000's: its own Droz, Mideon and Undertaker are its
//    imported wrestlers, wrestlers\WrestleMania 2000).
//  - The Revenge ports (Bret Hart .. Eric Bischoff, and their second IDs, which follow their base's option): Revenge alone
//    (c2hands 2026-11-19, the user: "Revenge uses its own hands when WM2000 is missing"). The importer builds their models,
//    wmport.bin and their tables from Revenge and No Mercy when WrestleMania 2000 isn't there (WrestleMania 2000's rows zero,
//    in the build's shape); with it, as always (their hands and fingers WrestleMania 2000's where they aren't No Mercy's).
//  - Jarrett and the Meanie: WrestleMania 2000 alone. (arenasplit 2026-11-24, the user: "a source game present means its
//    content is present" -- they needed Revenge AND WrestleMania 2000 before, only because the importer made wmport.bin, their
//    models' pack, from Revenge; it is made from either game now, each one's own part.)
u32 arcade_ro_needs(int i) {
    if (i == RO_BENOIT) return 0;                                                   // (the game's own)
    if (i == RO_BIG_SHOW || i == RO_MEDIA || (i >= RO_EXT0 && i < RO_EXT0 + 4)) return EXTRAS_NMPLUS;
    if (i == RO_JARRETT || i == RO_MEANIE) return EXTRAS_WM2000;
    if (i == RO_DROZ || i == RO_MIDEON || i == RO_TAKER99) return EXTRAS_NMPLUS;
    if (i >= RO_PETEGAS && i <= RO_TIGER) return EXTRAS_NMPLUS;
    if (i >= RO_BRET && i <= RO_BISCHOFF) return EXTRAS_REVENGE;
    return EXTRAS_ALL;
}

// the arcade's own arenas (gen/arenas.py ARENAS, arenas_wm.py: Revenge's and WrestleMania 2000's venues): (arenasplit
// 2026-11-24) each is its own game's -- its stage set, ringside, crowd, close scene, photo and backdrop from that game's files
// and No Mercy's, nothing of the other's -- so each needs its own game alone (it needed both before: the importer made them
// all or none). Their rows in ARENAS' order: MONDAY NITRO, HALLOWEEN HAVOC, BASH AT THE BEACH (Revenge's), RAW IS WAR WM 2000
// (WrestleMania 2000's), SOULED OUT, SUPERBRAWL, STARRCADE (Revenge's), then SUNDAY NIGHT HEAT .. SUMMERSLAM WM 2000
// (WrestleMania 2000's).
u32 arcade_arena_needs(int k) { return (k == 3 || k >= 7) ? EXTRAS_WM2000 : EXTRAS_REVENGE; }
