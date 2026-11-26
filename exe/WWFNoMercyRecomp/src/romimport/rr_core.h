// rr_core.h (phase3, 2026-11-19): the deterministic software renderer's core (rr_core.cpp, the C++ twin of
// tools\romrender\rr.py, whole-number arithmetic): the wrestlers' photos and the arenas' pictures are drawn with it from the
// ROM-built models. The scene building (rr.py's Scene / look_at, rr_model.py, rr_arena.py) is ported by the steps that
// need it; this header names the core's entry points.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace rr {
typedef int64_t i64;
struct Texture { i64 w, h, ws, wt; std::vector<uint16_t> texels; };
struct Camera { i64 eye[3], right[3], up[3], fwd[3], focal16, cx16, cy16, near8; };
typedef std::array<i64, 8> Vtx;           // x y z s t shade r g b (as rr_core.cpp)
struct Tri { i64 tex, flags; Vtx v[3]; };
typedef std::array<i64, 3> C3;
// RENDER: the scene drawn at S x S samples a pixel, then box-downsampled -> W*H*4 bytes RGBA (bg: the background RGBA)
std::vector<uint8_t> render(const std::vector<Texture>& texs, const std::vector<Tri>& tris, const Camera& cam,
                            const i64 bg[4], i64 W, i64 H, i64 S);
// QUANT: n colours by median cut in RGB555 (pal: the palette, index: each colour's entry)
void quantise(const std::vector<C3>& colours, i64 n, std::vector<C3>& pal, std::vector<i64>& index);
// RESAMPLE: RGBA sw x sh -> dw x dh (box filter)
std::vector<uint8_t> resample(const std::vector<uint8_t>& px, i64 sw, i64 sh, i64 dw, i64 dh);
i64 isin(i64 a);
i64 isqrt(i64 n);
}  // namespace rr
