// ri_vcdiff.h (phase3, 2026-11-19): the VCDIFF (xdelta3) decoder of vcdiff.cpp, for the ROM importer (a player's patch,
// applied in memory to his clean ROM).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace vcdiff {
typedef std::vector<uint8_t> Bytes;
bool apply(const Bytes& source, const Bytes& patch, Bytes& out, std::string& err);
}
