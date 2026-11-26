// [audiotable 2026-11-18] The sound resampler's filter: 64 fractions x 4 samples, read from the player's own ROM at start-up.
//
// The filter is data of the N64 sound program the ROM carries, so the source holds no copy of it (it used to:
// arcade_audio_table.h): only where it is in each No Mercy ROM and a check number of its 512 bytes. It is not made by a
// formula (no polynomial of the fraction gives it exactly), so it is read, not computed.
//   No Mercy (USA) Rev 1: ROM 0x559D0, 0x200 bytes (big-endian 16-bit numbers); No Mercy (USA) v1.0: ROM 0x55A70. Those
//   512 bytes are at exactly one place in each ROM.
// A ROM that does not hold them where expected stops the game with a message (main.cpp): never a filter of zeros.
// Test: src/main/test/arcade_audio_filter_test.cpp (the table read from both ROMs == the old header's numbers).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// The filter as the resampler uses it (arcade_audio.cpp): filled by arcade_audio_filter_from_rom before any sound runs.
extern int16_t arcade_audio_filter[256];

// True once arcade_audio_filter_from_rom has filled the filter from a ROM whose bytes checked out.
bool arcade_audio_filter_ready();

// Fills arcade_audio_filter from `rom` (`size` bytes, big-endian .z64 order: as the runtime holds it). Tries the places the
// filter has in No Mercy Rev 1 and v1.0 and takes the one whose 512 bytes have the expected check number (FNV-1a 64).
// False, with the reason in `why` and the filter left as it was, when neither does. `found_at` gets the ROM offset used.
bool arcade_audio_filter_from_rom(const uint8_t* rom, size_t size, std::string& why, uint32_t* found_at = nullptr);
