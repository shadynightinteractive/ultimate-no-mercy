// [relclean 2026-11-19] The video mode the runtime shows before the game starts, read from the player's own ROM at start-up.
//
// Before the game's code sets its own video mode, the runtime (ultramodern events.cpp set_dummy_vi) shows a blank picture
// in a stand-in mode: libultra's NTSC LAN1 mode (OS_VI_NTSC_LAN1: 320 wide, 16-bit, anti-aliased). Those 80 bytes are the
// N64 SDK's data, which every AKI ROM carries; the program carried a copy (built from the numbers in events.cpp), and the
// release audit found it equal to the ROMs. So the program holds only where the mode is in each No Mercy ROM and a check
// number of its 80 bytes (the way the sound filter is read: arcade_audio_filter.h):
//   No Mercy (USA) Rev 1: ROM 0x544B0, 0x50 bytes (big-endian, as the game's OSViMode: u8 type, 3 zero bytes, 9 common
//   registers, 2 x 5 field registers); No Mercy (USA) v1.0: ROM 0x54550. (Each ROM holds the mode once more, 0x1350 bytes
//   later: the first place is taken.)
// A ROM that does not hold it where expected stops the game with a message (main.cpp): never a mode of zeros.
// Test: src/main/test/arcade_vi_mode_test.cpp (the mode read from both ROMs == the numbers events.cpp had).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "ultramodern/ultra64.h"

// Fills `out` from `rom` (`size` bytes, big-endian .z64 order: as the runtime holds it). Tries the places the mode has in No
// Mercy Rev 1 and v1.0 and takes the one whose 80 bytes have the expected check number (FNV-1a 64). False, with the reason
// in `why` and `out` left as it was, when neither does. `found_at` gets the ROM offset used.
bool arcade_vi_mode_from_rom(const uint8_t* rom, size_t size, OSViMode& out, std::string& why, uint32_t* found_at = nullptr);
