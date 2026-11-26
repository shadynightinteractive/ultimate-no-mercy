// ri_data.h (phase3, 2026-11-19): the generators' small shared tables, made from the ROMs -- the C++ twin of the rest of
// tools\romimport\make_data.py: the move names (names.pkl, from names_index.json: each key's text found in the game's own
// files), Revenge's moveset files by (group, row) (rev_movesets.pkl), a file's fingerprint (hashes.pkl's (md5, length,
// first 4 bytes), made when asked).
#pragma once
#include "ri_roms.h"
#include <map>

namespace ri {

// move number -> name, in names_index.json's key order (the pickle's): No Mercy's and No Mercy Plus V3's
struct MoveNames { std::vector<std::pair<int, std::string>> nomercy, nmplus; };
MoveNames move_names(Roms& R, const fs::path& names_index_json);

// Revenge's moveset files: rows (group, row, file) in the game's table order, and each row's list (file f + 2: 763 u16)
struct RevMovesets {
    struct Row { int group, row, file; };
    std::vector<Row> rows;
    std::map<std::pair<int, int>, std::vector<uint16_t>> lists;
};
RevMovesets rev_movesets(Roms& R);

}  // namespace ri
