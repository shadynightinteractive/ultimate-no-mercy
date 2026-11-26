// [audiotable 2026-11-18] The sound resampler's filter, read from the player's ROM (see arcade_audio_filter.h).
#include "arcade_audio_filter.h"

#include <cstdio>

int16_t arcade_audio_filter[256] = {};

namespace {
bool filter_ready = false;

// Where the filter is in each ROM the game knows (the same 512 bytes in both, at one place in each).
struct FilterPlace { uint32_t offset; const char* rom; };
constexpr FilterPlace FILTER_PLACES[] = {
    { 0x559D0u, "No Mercy (USA) Rev 1" },
    { 0x55A70u, "No Mercy (USA) v1.0" },
};
constexpr uint32_t FILTER_BYTES = 0x200u;
// The check number: FNV-1a 64 of the 512 bytes, as the ROM holds them (a number, not the bytes).
constexpr uint64_t FILTER_FNV = 0xCE96567506BB5BB1ull;

uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}
}  // namespace

bool arcade_audio_filter_ready() { return filter_ready; }

bool arcade_audio_filter_from_rom(const uint8_t* rom, size_t size, std::string& why, uint32_t* found_at) {
    if (rom == nullptr || size == 0) { why = "no ROM is loaded"; return false; }
    for (const FilterPlace& place : FILTER_PLACES) {
        if (size_t(place.offset) + FILTER_BYTES > size) continue;
        const uint8_t* b = rom + place.offset;
        if (fnv1a64(b, FILTER_BYTES) != FILTER_FNV) continue;
        for (uint32_t i = 0; i < 256; i++) arcade_audio_filter[i] = int16_t(uint16_t(b[2 * i] << 8 | b[2 * i + 1]));
        filter_ready = true;
        if (found_at) *found_at = place.offset;
        return true;
    }
    char text[200];
    std::snprintf(text, sizeof text, "the ROM (%zu bytes) does not hold the sound resampler's filter where No Mercy (USA) Rev 1 "
        "(0x%X) or v1.0 (0x%X) has it", size, FILTER_PLACES[0].offset, FILTER_PLACES[1].offset);
    why = text;
    return false;
}
