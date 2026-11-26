// ri_vadpcm.h (phase3, 2026-11-19): N64 VADPCM -- the audio library's ADPCM, the form the game's instruments (and so the
// themes the importer renders) are kept in. The C++ twin of mod\gen\vadpcm.py's encode() and decode(), which run the N64
// SDK's own tools (tabledesign -s 2, vadpcm_enc -c, vadpcm_dec); same bytes (tools\romimport\native\vadpcm_check.py).
// Ported from those tools as the Super Mario 64 decompilation reconstructs them (n64decomp/sm64, tools/sdk-tools; CC0 1.0
// Universal) -- except the per-frame linear solve (lu_decompose / lu_solve), which is our own code, written from the
// mathematics (relclean 2026-11-19): see ri_vadpcm.cpp's header and CREDITS.md.
//
// A book is the wave table's ALADPCMBook as the game keeps it: s32 order, s32 npredictors, then s16 coefficients
// [npredictors][order][8], all big-endian (order 2, 4 predictors: 0x88 bytes). The ADPCM data is 9-byte frames of 16
// samples (a header byte: scale << 4 | predictor; then 16 four-bit residuals), padded with one zero byte to an even length.
#pragma once
#include "ri_core.h"

namespace ri {

struct VadpcmOut {
    Bytes book;          // the codebook tabledesign designs for this sound (0x88 bytes)
    Bytes data;          // the ADPCM frames vadpcm_enc writes (9 per 16 samples, the last one zero-padded; even length)
    uint32_t samples;    // the sound's length in samples (= n)
};

// = vadpcm.encode(pcm, rate): no loop, no given book. 'rate' only names the AIFF's sample rate in the Python; the tools'
// results do not depend on it (kept so the call reads like the Python's).
VadpcmOut vadpcm_encode(const int16_t* pcm, size_t n, int rate);

// = vadpcm.decode(data, book, n): n samples of 16-bit PCM. 'data' must hold the n samples' frames (ceil(n / 16) of them).
std::vector<int16_t> vadpcm_decode(const Bytes& data, const Bytes& book, size_t n);

}  // namespace ri
