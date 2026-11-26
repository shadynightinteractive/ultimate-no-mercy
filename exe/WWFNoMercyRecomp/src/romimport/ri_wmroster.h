// ri_wmroster.h (phase3 p3folders, 2026-11-19): WrestleMania 2000's roster records, read from the ROM -- the C++ twin of
// tools\handlers\handlers\baddblood\game.py BaddBlood.roster (which the WrestleMania 2000 handler, wm2000\game.py, uses for
// both games: Badd Blood is WrestleMania 2000 with its data changed, the same record layout at the same place).
//
// The records are static data of the main segment (RAM 0x80000400 = ROM 0x1000, up to ROM 0x144AA0): a table of 0x4C
// pointers at RAM 0x800411B0, each record: name pointer (the select screen's markup), then u16 game ID, ordinal, height,
// weight, moves file, abilities file, u8 music slot, u8 video, then four pointers to the costumes' looks (0x24 bytes each).
#pragma once
#include "ri_core.h"
#include <string>

namespace ri {

struct WmRecord {
    int k;                         // its roster index (0 .. 0x4B)
    std::string raw_name;          // the select screen's markup, latin-1 bytes as they are ("<AL> SNOW")
    int id, ord, height, weight, moves, abilities, music, video;
    Bytes looks[4];                // the four costumes' looks, 0x24 bytes each
};

// The main segment as the game loads it, in an 8 MB RAM image (wm2000\game.py WM2000.ram / BaddBlood.ram).
Bytes wm_main_ram(const Bytes& rom);
// All 0x4C records, from that image.
std::vector<WmRecord> wm_roster(const Bytes& main_ram);

// wm2000\game.py names(raw) -> (full name, short name): text in {} only in the short name, in <> in both, the rest only in
// the full; (the short name is the full one when it would be empty)
void wm_names(const std::string& raw, std::string& full, std::string& short_name);

}  // namespace ri
