// [commentary2 2026-11-18] A WWE '13 line (a Wwise .wem, Vorbis) -> its samples exactly as vgmstream-cli (git f7f7641,
// the build that decoded the developer's copy) writes them to a 16-bit .wav. The Wwise-specific part is vgmstream's
// own code (vgmstream\coding\vorbis_custom_utils_wwise.c, ISC licence); this file is its meta\wwise.c (the Vorbis
// case) and the decode loop, cut down to what Wwise Vorbis needs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace comm_build {

// data/size: the whole .wem (RIFF or RIFX). out: mono samples (the number the header gives); rate: its sample rate.
// false + why: not a Wwise Vorbis file, more than one channel, a looping file, or a decode error.
bool wem_decode(const uint8_t* data, size_t size, std::vector<int16_t>& out, int& rate, std::string& why);

}  // namespace comm_build
