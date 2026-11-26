// [arcade] The full-detail models' per-frame work (the mod's src/hd.c), native: mod code is translated from MIPS as it loads, with no optimiser, and
// these are tens of thousands of points a frame. The mod calls these through RECOMP_IMPORT (recomp_arcade_hd_*), on its own thread, with the game's bones
// already worked out; each runs start to finish before the call returns, so nothing about when the renderer sees the points changes.
// The game's memory holds words in this machine's order: a u32 or float at a (word-aligned) address reads as it is; a 16-bit value at A is at A ^ 2.
#include <cstdint>
#include <cstdio>

#include "arcade_platform.h"

#include "ultramodern/ultramodern.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"

namespace {

constexpr int BONE_N = 28;
constexpr float SUB = 16.0f;       // (a point's place x this in its s16: hd.c's HD_SUB)

struct Mem {
    uint8_t* rdram;
    uint8_t* at(uint32_t a) const { return rdram + (a - 0x80000000u); }
    uint32_t u32(uint32_t a) const { return *reinterpret_cast<uint32_t*>(at(a)); }
    float f32(uint32_t a) const { return *reinterpret_cast<float*>(at(a)); }
    int16_t s16(uint32_t a) const { return *reinterpret_cast<int16_t*>(at(a ^ 2u)); }
    void w16(uint32_t a, int16_t v) const { *reinterpret_cast<int16_t*>(at(a ^ 2u)) = v; }
    uint8_t u8(uint32_t a) const { return *at(a ^ 3u); }
};

inline void inv3(const float* m, float* o) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (det == 0.0f) det = 1.0f;
    const float q = 1.0f / det;
    o[0] = A * q; o[1] = (c * h - b * i) * q; o[2] = (b * f - c * e) * q;
    o[3] = B * q; o[4] = (a * i - c * g) * q; o[5] = (c * d - a * f) * q;
    o[6] = C * q; o[7] = (b * g - a * h) * q; o[8] = (a * e - b * d) * q;
}

inline int16_t fix16(float v) {
    v *= SUB;
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return static_cast<int16_t>(static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f));
}

// recomp_arcade_hd_blend(bones, table, vtx, mats): the points shared between bones, each in its bone's own frame.
//   bones: HdBone[28] (u32 dl, dln, vtx, vtxn, bl, bln); table: the pack's blended points (28 bytes each: u32 point, float x y z, 4 bones, float w0 w1); vtx: the slot's copy of the points; mats: float[28][16], this frame's
void hd_blend(uint8_t* rdram, recomp_context* ctx) {
    const Mem m{ rdram };
    const uint32_t bones = _arg<0, uint32_t>(rdram, ctx), table = _arg<1, uint32_t>(rdram, ctx), vtx = _arg<2, uint32_t>(rdram, ctx), mats = _arg<3, uint32_t>(rdram, ctx);
    const float* M = reinterpret_cast<const float*>(m.at(mats));
    uint32_t total = 0;
    for (int k = 1; k < 20; k++) {
        const uint32_t row = bones + static_cast<uint32_t>(k) * 24u;
        if (!m.u32(row + 4u)) continue;       // (dln: no list)
        const uint32_t n = m.u32(row + 20u);
        if (!n) continue;
        const float* Mk = M + k * 16;
        float I[9];
        inv3(Mk, I);
        const uint32_t* e = reinterpret_cast<const uint32_t*>(m.at(table + m.u32(row + 16u)));
        const uint32_t vb = vtx + m.u32(row + 8u);
        for (uint32_t i = 0; i < n; i++, e += 7) {
            const float px = reinterpret_cast<const float*>(e)[1], py = reinterpret_cast<const float*>(e)[2], pz = reinterpret_cast<const float*>(e)[3];
            const uint32_t bb = e[4];
            const float w0 = reinterpret_cast<const float*>(e)[5], w1 = reinterpret_cast<const float*>(e)[6];
            const float wt[3] = { w0, w1, 1.0f - w0 - w1 };
            float wx = 0.0f, wy = 0.0f, wz = 0.0f;
            for (int j = 0; j < 3; j++) {
                const float w = wt[j];
                if (w < 0.001f) continue;
                int b = static_cast<int>((bb >> (24 - 8 * j)) & 0xFFu);
                if (b < 1 || b >= 20) b = k;
                const float* q = M + b * 16;
                wx += w * (px * q[0] + py * q[4] + pz * q[8] + q[12]);
                wy += w * (px * q[1] + py * q[5] + pz * q[9] + q[13]);
                wz += w * (px * q[2] + py * q[6] + pz * q[10] + q[14]);
            }
            wx -= Mk[12]; wy -= Mk[13]; wz -= Mk[14];
            const uint32_t v = vb + e[0] * 16u;
            m.w16(v, fix16(wx * I[0] + wy * I[3] + wz * I[6]));
            m.w16(v + 2u, fix16(wx * I[1] + wy * I[4] + wz * I[7]));
            m.w16(v + 4u, fix16(wx * I[2] + wy * I[5] + wz * I[8]));
        }
        total += n;
    }
    ctx->r2 = static_cast<int32_t>(total);
}

// recomp_arcade_hd_hand(args): one hand's fingers to a shape. args: u32[5]: e (the pack's entries, 20 bytes each: u32 point, s16[3] toward closed, s16[3] toward straight, u8 finger, ...), n, vb (the slot's points), tb (the pack's), cl (s16[5], the shape)
void hd_hand(uint8_t* rdram, recomp_context* ctx) {
    const Mem m{ rdram };
    const uint32_t a = _arg<0, uint32_t>(rdram, ctx);
    uint32_t e = m.u32(a);
    const uint32_t n = m.u32(a + 4u), vb = m.u32(a + 8u), tb = m.u32(a + 12u), cl = m.u32(a + 16u);
    int shape[5];
    for (uint32_t f = 0; f < 5u; f++) shape[f] = m.s16(cl + f * 2u);
    for (uint32_t i = 0; i < n; i++, e += 20u) {
        const uint32_t idx = m.u32(e);
        const uint32_t f = m.u8(e + 16u);
        int amt = shape[f < 5u ? f : 0u];
        const uint32_t d = amt >= 0 ? e + 4u : e + 10u;      // (the closed shape's moves, or the straight's)
        if (amt < 0) amt = -amt;
        const uint32_t v = vb + idx * 16u, t = tb + idx * 16u;
        for (uint32_t c = 0; c < 3u; c++)
            m.w16(v + c * 2u, static_cast<int16_t>(m.s16(t + c * 2u) + ((m.s16(d + c * 2u) * amt) >> 8)));
    }
}

// recomp_arcade_hd_time(what): a stopwatch on the mod's own stretches, for QA (game_stderr.txt, every 240 frames). 0 / 1 the whole of hd_frame, 2 / 3 the blended points
int64_t tm_start[2], tm_sum[2], tm_max[2], tm_freq;
int tm_frames;
void hd_time(uint8_t* rdram, recomp_context* ctx) {
    const int what = _arg<0, int>(rdram, ctx);
    const int64_t now = int64_t(arcade::platform::ticks());
    if (!tm_freq) tm_freq = int64_t(arcade::platform::ticks_per_second());
    const int w = (what >> 1) & 1;
    if (!(what & 1)) { tm_start[w] = now; return; }
    const int64_t d = now - tm_start[w];
    tm_sum[w] += d;
    if (d > tm_max[w]) tm_max[w] = d;
    if (w == 0 && ++tm_frames >= 240) {
        const double us = 1e6 / static_cast<double>(tm_freq);
        fprintf(stderr, "[hd] of %d frames: hd_frame average %.0f us, worst %.0f; its blended points average %.0f us, worst %.0f\n", tm_frames,
                static_cast<double>(tm_sum[0]) * us / tm_frames, static_cast<double>(tm_max[0]) * us, static_cast<double>(tm_sum[1]) * us / tm_frames, static_cast<double>(tm_max[1]) * us);
        fflush(stderr);
        tm_frames = 0; tm_sum[0] = tm_sum[1] = tm_max[0] = tm_max[1] = 0;
    }
}

} // namespace

void arcade_hd_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_hd_blend", hd_blend);
    recomp::overlays::register_base_export("recomp_arcade_hd_hand", hd_hand);
    recomp::overlays::register_base_export("recomp_arcade_hd_time", hd_time);
}
