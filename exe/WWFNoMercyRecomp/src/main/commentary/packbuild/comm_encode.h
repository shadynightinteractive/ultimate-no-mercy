// [commentary2 2026-11-18] The commentary pack's clips, encoded on the player's PC exactly as the developer's tools made
// them (tools\commentary\rebuild.py: FFmpeg 6.1's silenceremove + libvorbis q0 + its Ogg muxer, then
// oggpk.replace_comment). Same input samples -> the same bytes; tools\commentary\test_encode.py checks it against FFmpeg.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace comm_build {

// FFmpeg's "silenceremove=start_periods=1:start_threshold=-50dB,areverse,<the same>,areverse": silence off both ends.
// x: the clip as FFmpeg's filter sees it (float, s16 / 32768). Returns [first, last) of what is kept (first == last: none).
void trim_silence(const std::vector<float>& x, int rate, size_t& first, size_t& last);

// One clip: mono float samples at `rate` -> an Ogg Vorbis file, libvorbis quality 0 the way FFmpeg's libvorbis encoder
// feeds it, paged the way FFmpeg's Ogg muxer pages it; the header pages carry `comment` (a whole Vorbis comment
// packet) and every page `serial`, laid out as tools\commentary\oggpk.py replace_comment() does.
// Returns an empty vector on failure.
std::vector<uint8_t> encode_clip(const float* x, size_t n, int rate, const std::vector<uint8_t>& comment, uint32_t serial);

// [commdisc 2026-11-18] A clip back to its samples (libvorbis's decode, float, as many as its last granule says), so that
// a clip is judged on what it sounds like, the way rebuild.py (tools, commentary) judges a decoded clip. false: not a mono
// Ogg Vorbis file this can read.
bool decode_clip(const uint8_t* p, size_t n, std::vector<float>& out);

// The Ogg CRC (polynomial 0x04C11DB7, not reflected), as every Ogg page carries.
uint32_t ogg_crc(const uint8_t* p, size_t n, uint32_t crc = 0);

}  // namespace comm_build
