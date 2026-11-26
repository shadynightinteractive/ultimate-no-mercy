// [relclean 2026-11-19] The runtime's stand-in video mode, read from the player's ROM (see arcade_vi_mode.h).
#include "arcade_vi_mode.h"

#include <cstdio>

namespace {
// Where the mode is in each ROM the game knows (the same 80 bytes in both).
struct ModePlace { uint32_t offset; const char* rom; };
constexpr ModePlace MODE_PLACES[] = {
    { 0x544B0u, "No Mercy (USA) Rev 1" },
    { 0x54550u, "No Mercy (USA) v1.0" },
};
constexpr uint32_t MODE_BYTES = 0x50u;
// The check number: FNV-1a 64 of the 80 bytes, as the ROM holds them (a number, not the bytes).
constexpr uint64_t MODE_FNV = 0x4C6C685B3652A066ull;

uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
}  // namespace

bool arcade_vi_mode_from_rom(const uint8_t* rom, size_t size, OSViMode& out, std::string& why, uint32_t* found_at) {
    if (rom == nullptr || size == 0) { why = "no ROM is loaded"; return false; }
    for (const ModePlace& place : MODE_PLACES) {
        if (size_t(place.offset) + MODE_BYTES > size) continue;
        const uint8_t* b = rom + place.offset;
        if (fnv1a64(b, MODE_BYTES) != MODE_FNV) continue;
        // (the game's OSViMode, field by field: u8 type and 3 bytes of padding, then 32-bit registers)
        OSViMode m{};
        m.type = b[0];
        const uint8_t* r = b + 4;
        m.comRegs.ctrl = be32(r + 0);   m.comRegs.width = be32(r + 4);   m.comRegs.burst = be32(r + 8);
        m.comRegs.vSync = be32(r + 12); m.comRegs.hSync = be32(r + 16);  m.comRegs.leap = be32(r + 20);
        m.comRegs.hStart = be32(r + 24); m.comRegs.xScale = be32(r + 28); m.comRegs.vCurrent = be32(r + 32);
        for (int field = 0; field < 2; field++) {
            const uint8_t* f = r + 36 + 20 * field;
            m.fldRegs[field].origin = be32(f + 0); m.fldRegs[field].yScale = be32(f + 4); m.fldRegs[field].vStart = be32(f + 8);
            m.fldRegs[field].vBurst = be32(f + 12); m.fldRegs[field].vIntr = be32(f + 16);
        }
        out = m;
        if (found_at) *found_at = place.offset;
        return true;
    }
    char text[200];
    std::snprintf(text, sizeof text, "the ROM (%zu bytes) does not hold the video mode where No Mercy (USA) Rev 1 (0x%X) or v1.0 "
        "(0x%X) has it", size, MODE_PLACES[0].offset, MODE_PLACES[1].offset);
    why = text;
    return false;
}
