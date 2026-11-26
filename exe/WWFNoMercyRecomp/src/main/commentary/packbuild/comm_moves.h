// [commentary2 2026-11-18] The commentary pack's moves table (move number -> the game's move name, lower case: what a line
// naming a move is matched against, director.cpp move_fits) made on the player's PC from their own No Mercy (USA) (Rev 1)
// ROM. The names are the game's text, so they never ship: the recipe holds only references (moves_refs.tsv: the move's
// number, the AKI file, the offset of its 0-ended name -- tools/commentary/make_tags.py from romimport's
// names_index.json). The same rows as the old moves.tsv, byte for byte (tools/commentary/test_moves: comm_moves_test).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "comm_pack.h"

namespace comm_build {

// The No Mercy (USA) (Rev 1) ROM in one of `folders` (.z64 / .v64 / .n64, any name), big-endian, checked by its SHA-1;
// empty if none.
std::vector<uint8_t> find_nomercy_rom(const std::vector<std::filesystem::path>& folders);

// One of the ROM's AKI files (1-based, unpacked: the game's own file table at 0x16C31D8, data from 0x1BD150, LZSS).
bool aki_file(const std::vector<uint8_t>& rom, uint32_t index, std::vector<uint8_t>& out);

// r.moves from r.move_refs and the ROM: "<number>\t<name in lower case>\n" per reference, in order.
bool moves_from_rom(Recipe& r, const std::vector<uint8_t>& rom, std::string& why);

}  // namespace comm_build
