// [xplat 2026-11-12] A test for the Vulkan picture backend that needs no game: it opens a window, starts arcade_view_vulkan.cpp's thread and
// hands it made-up Frames (the same type arcade_view.cpp builds from the game's display lists) the way the reader would, 30 a second.
// What the frames hold is chosen to exercise each thing the backend does: vertex colours and textures through the combiner, alpha blend,
// the depth test and the later-layer bias, back-face culling (winding), the scissor, point and smooth sampling, wrap / mirror / clamp,
// MSAA edges, Smooth Motion (a square that moves between two updates), the Titantron's own target and the screen quad that shows it,
// a sign's shape, the CRT last step (pic_crt 1 and 2), and BMP shots.
//   build: tools/build_vk_harness.sh     run:  WCW_NATIVE_SHOTS=/tmp/shots xvfb-run ... ./vk_harness [seconds]
// It stands in for everything arcade_view.cpp shares with the backend (arcade_view_shared.h), so the backend's own code is the code tested.
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "arcade_view_shared.h"
#include "arcade_view_gpu.h"
#include "arcade_media.h"

SDL_Window* window = nullptr;
std::vector<std::filesystem::path> arcade_wrestlers_hd_pictures() { return {}; }
std::shared_ptr<const arcade_media::Picture> arcade_media_video_picture() { return nullptr; }

namespace arcade_view {
int mode_ = 3;
std::atomic<uint32_t> screen_hz{ 60 };
std::atomic<int> pic_window{ 0 }, pic_shape{ 0 }, pic_smooth{ 1 }, pic_limit{ 0 }, pic_crt{ 0 }, pic_hdr{ 0 };
const float SHAPES[6] = { 4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f, 3.0f / 4.0f, 9.0f / 16.0f };
std::mutex mtx_; Frame ready_; bool have_ready_;
void put_together(const Dec&, const Dec&, float, float* o) { for (int i = 0; i < 16; i++) o[i] = (i % 5 == 0); }
int world_depth_on() { return 1; }
std::atomic<uint64_t> depth_draws_[4];
std::atomic<uint64_t> cuts_, xf_world_, xf_direct_, xf_matched_, cam_lost_, read_ns_, read_n_;
std::atomic<float> worst_key_, worst_move_, worst_turn_;
}
using namespace arcade_view;

// ---- a frame's pieces ----
static void quad(Frame& f, float x0, float y0, float x1, float y1, float z, u8 r, u8 g, u8 b, u8 a, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1, float dx = 0, float dy = 0) {
    // (y up, as clip space is; counter-clockwise as seen: a front face. dx dy: where the quad was an update ago, for the blend)
    const u32 base = u32(f.v.size());
    auto vtx = [&](float x, float y, float s, float t, u8 rr, u8 gg, u8 bb) { V v{}; v.x = x; v.y = y; v.z = z; v.px = x - dx; v.py = y - dy; v.pz = z; v.s = s; v.t = t; v.r = rr; v.g = gg; v.b = bb; v.a = a; v.xf = 0; f.v.push_back(v); };
    vtx(x0, y0, s0, t1, r, g, b); vtx(x1, y0, s1, t1, g, b, r); vtx(x1, y1, s1, t0, b, r, g); vtx(x0, y1, s0, t0, r, g, b);
    for (u32 i : { 0u, 1u, 2u, 0u, 2u, 3u }) f.idx.push_back(base + i);
}
static void tri(Frame& f, float x0, float y0, float x1, float y1, float x2, float y2, float z, u8 r, u8 g, u8 b, u8 a) {
    const u32 base = u32(f.v.size());
    auto vtx = [&](float x, float y, u8 rr, u8 gg, u8 bb) { V v{}; v.x = x; v.y = y; v.z = z; v.px = x; v.py = y; v.pz = z; v.r = rr; v.g = gg; v.b = bb; v.a = a; f.v.push_back(v); };
    vtx(x0, y0, r, g, b); vtx(x1, y1, g, b, r); vtx(x2, y2, b, r, g);
    for (u32 i = 0; i < 3; i++) f.idx.push_back(base + i);
}
enum Mode { SHADE, TEX_SHADE, PRIM, TEX_PRIM };
static void draw(Frame& f, u32 start, int tex, Mode m, u8 zmode = 0, bool cull = false, bool point = false, u8 cms = 0, u8 cmt = 0, u8 target = 0, const float* prim = nullptr, float alpha_min = 0) {
    Draw d{}; d.tex = tex; d.start = start; d.count = u32(f.idx.size()) - start; d.zmode = zmode; d.cull = cull; d.point = point; d.cms = cms; d.cmt = cmt; d.target = target;
    d.sc[0] = 0; d.sc[1] = 0; d.sc[2] = 1; d.sc[3] = 1;
    DrawCB& c = d.cb; c.uv[0] = 1; c.uv[1] = 1; c.uv[2] = 0; c.uv[3] = 0; c.vp[0] = 1; c.vp[1] = 1; c.vp[2] = 0; c.vp[3] = 0;
    const float p[4] = { 1, 0.5f, 0.1f, 0.6f }; std::memcpy(c.prim, prim ? prim : p, 16);
    const float e[4] = { 0.2f, 0.3f, 1, 1 }; std::memcpy(c.env, e, 16);
    // cycle 1: colour a b c d, alpha a b c d. cycle 2 passes cycle 1's result on:  (0 - 0) * 0 + combined
    int c1[8];
    switch (m) {
    case SHADE:     { int v[8] = { 4, 7, 7, 4, 4, 7, 7, 4 }; std::memcpy(c1, v, 32); break; }      // colour = (shade - 0) * 0 + shade ; alpha likewise
    case TEX_SHADE: { int v[8] = { 1, 7, 4, 7, 1, 7, 4, 7 }; std::memcpy(c1, v, 32); break; }      // (tex - 0) * shade + 0
    case PRIM:      { int v[8] = { 3, 7, 7, 3, 3, 7, 7, 3 }; std::memcpy(c1, v, 32); break; }      // prim
    default:        { int v[8] = { 1, 7, 3, 7, 1, 7, 3, 7 }; std::memcpy(c1, v, 32); break; }      // (tex - 0) * prim + 0
    }
    std::memcpy(c.comb, c1, 32);
    const int pass[8] = { 7, 7, 7, 0, 7, 7, 7, 0 }; std::memcpy(c.comb + 8, pass, 32);
    c.zx[0] = alpha_min;
    f.draws.push_back(d);
}
static std::vector<u8> checker(int w, int h, int cell, u32 c0, u32 c1) {
    std::vector<u8> p(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { const u32 c = (((x / cell) + (y / cell)) & 1) ? c1 : c0; std::memcpy(&p[(size_t(y) * w + x) * 4], &c, 4); }
    return p;
}

// The frame for "tick" (a game update). scene: what's in it.
static Frame make_frame(int tick, bool first) {
    Frame f; f.snap = false;
    f.xf.clear();                                    // (no matrices: the backend's own identity stands in: vertices are in clip space)
    f.cam[0].valid = f.cam[1].valid = false;
    if (first) {
        // textures 20: a checker with see-through cells (rgba), 21: a gradient strip, wrap / clamp / mirror test
        { NewTex t; t.id = 20; t.w = 64; t.h = 64; t.rgba = checker(64, 64, 8, 0xFF2020F0u, 0xFF30D0F0u); for (size_t i = 3; i < t.rgba.size(); i += 4) { const size_t px = i / 4; if (((px % 64) / 8 + (px / 64) / 8) % 5 == 0) t.rgba[i] = 0; } f.fresh.push_back(std::move(t)); }
        { NewTex t; t.id = 21; t.w = 8; t.h = 8; t.rgba = checker(8, 8, 1, 0xFFFFFFFFu, 0xFF000000u); f.fresh.push_back(std::move(t)); }
        { NewTex t; t.id = 22; t.w = 4; t.h = 4; t.rgba.resize(64); for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { u8* q = &t.rgba[(y * 4 + x) * 4]; q[0] = u8(x * 85); q[1] = u8(y * 85); q[2] = 128; q[3] = 255; } f.fresh.push_back(std::move(t)); }
    }
    const float mv = 0.25f * std::sin(tick * 0.1f);        // (the moving square's x this update)
    // ---- the Titantron's own picture (target 1): a gradient quad and a triangle, 512 x 256 ----
    { const u32 s = u32(f.idx.size()); quad(f, -1, -1, 1, 1, 0.5f, 255, 80, 20, 255); draw(f, s, 0, SHADE, 0, false, false, 0, 0, 1); }
    { const u32 s = u32(f.idx.size()); tri(f, -0.8f, -0.8f, 0.8f, -0.8f, 0, 0.8f, 0.3f, 20, 255, 40, 255); draw(f, s, 0, SHADE, 0, false, false, 0, 0, 1); }
    // ---- the frame (target 0) ----
    // 1. the backdrop: a gradient (vertex colours), no depth
    { const u32 s = u32(f.idx.size()); quad(f, -1, -1, 1, 1, 0.9f, 30, 60, 160, 255); draw(f, s, 0, SHADE); }
    // 2. a checker with see-through cells, linear and point-sampled, left half
    { const u32 s = u32(f.idx.size()); quad(f, -0.9f, 0.1f, -0.5f, 0.7f, 0.5f, 255, 255, 255, 255); draw(f, s, 20, TEX_SHADE); }
    { const u32 s = u32(f.idx.size()); quad(f, -0.45f, 0.1f, -0.05f, 0.7f, 0.5f, 255, 255, 255, 255); draw(f, s, 20, TEX_SHADE, 0, false, true); }
    // 3. 8 x 8 checker magnified, point (blocky) and linear, with wrap (s t to 2: it repeats), mirror and clamp
    { const u32 s = u32(f.idx.size()); quad(f, 0.05f, 0.1f, 0.35f, 0.7f, 0.5f, 255, 255, 255, 255, 0, 0, 2, 2); draw(f, s, 21, TEX_SHADE, 0, false, true, 0, 0); }
    { const u32 s = u32(f.idx.size()); quad(f, 0.4f, 0.1f, 0.7f, 0.7f, 0.5f, 255, 255, 255, 255, -0.5f, -0.5f, 1.5f, 1.5f); draw(f, s, 22, TEX_SHADE, 0, false, true, 1, 1); }      // mirror
    { const u32 s = u32(f.idx.size()); quad(f, 0.72f, 0.1f, 0.98f, 0.7f, 0.5f, 255, 255, 255, 255, -0.5f, -0.5f, 1.5f, 1.5f); draw(f, s, 22, TEX_SHADE, 0, false, true, 2, 2); }       // clamp
    // 4. depth: two triangles that cross (a solid one with the depth test), drawn in the "wrong" order for painter's
    { const u32 s = u32(f.idx.size()); tri(f, -0.9f, -0.9f, -0.1f, -0.9f, -0.5f, -0.2f, 0.7f, 240, 40, 40, 255);
      f.v[f.v.size() - 1].z = 0.2f; f.v[f.v.size() - 1].pz = 0.2f; draw(f, s, 0, SHADE, 1); }
    { const u32 s = u32(f.idx.size()); tri(f, -0.7f, -0.3f, -0.7f + 0.8f, -0.3f, -0.3f, -0.95f, 0.45f, 40, 240, 40, 255);
      f.v[f.v.size() - 1].z = 0.9f; f.v[f.v.size() - 1].pz = 0.9f; draw(f, s, 0, SHADE, 1); }
    // 5. winding: the one counter-clockwise (shown with culling), the clockwise one (gone with culling); both with cull on
    { const u32 s = u32(f.idx.size()); tri(f, 0.05f, -0.9f, 0.35f, -0.9f, 0.2f, -0.55f, 0.3f, 250, 250, 20, 255); draw(f, s, 0, SHADE, 0, true); }
    { const u32 s = u32(f.idx.size()); tri(f, 0.4f, -0.9f, 0.55f, -0.55f, 0.7f, -0.9f, 0.3f, 250, 20, 250, 255); draw(f, s, 0, SHADE, 0, true); }       // (clockwise: must not show)
    // 6. alpha blend: a half-see-through prim-coloured square over the backdrop
    { const u32 s = u32(f.idx.size()); quad(f, 0.0f, -0.85f, 0.45f, -0.4f, 0.2f, 255, 255, 255, 255); static const float p[4] = { 1, 1, 1, 0.5f }; draw(f, s, 0, PRIM, 0, false, false, 0, 0, 0, p); }
    // 7. a thin diagonal sliver (MSAA smooths its edge), and a square moving between two updates (Smooth Motion)
    { const u32 s = u32(f.idx.size()); tri(f, 0.55f, -0.95f, 0.99f, -0.35f, 0.98f, -0.4f, 0.3f, 255, 255, 255, 255); draw(f, s, 0, SHADE); }
    { const u32 s = u32(f.idx.size()); quad(f, -0.1f + mv, 0.75f, 0.1f + mv, 0.95f, 0.3f, 250, 250, 250, 255, 0, 0, 1, 1, 0.25f * (std::sin(tick * 0.1f) - std::sin((tick - 1) * 0.1f))); draw(f, s, 0, SHADE); }
    // 8. the scissor: a big quad cut to a window of the picture
    { const u32 s = u32(f.idx.size()); quad(f, -1, -1, 1, 1, 0.1f, 250, 120, 250, 255); draw(f, s, 0, SHADE); f.draws.back().sc[0] = 0.05f; f.draws.back().sc[1] = 0.88f; f.draws.back().sc[2] = 0.2f; f.draws.back().sc[3] = 0.97f; }
    // 9. the Titantron's screen: the target's picture on a quad (tex -1), and a sign's shape (target 2) over a second quad
    { const u32 s = u32(f.idx.size()); quad(f, 0.28f, 0.76f, 0.88f, 0.98f, 0.3f, 255, 255, 255, 255); draw(f, s, -1, TEX_SHADE); }
    { const u32 s = u32(f.idx.size()); tri(f, -0.8f, -0.8f, 0.8f, -0.8f, 0, 0.8f, 0.3f, 0, 0, 0, 255); draw(f, s, 0, SHADE, 0, false, false, 0, 0, 2); }      // (target 2: a sign's shape: a triangle)
    { const u32 s = u32(f.idx.size()); quad(f, 0.78f, -0.98f, 0.98f, -0.78f, 0.1f, 255, 255, 255, 255); draw(f, s, -3, TEX_PRIM); }     // (the shape, shown: black where the triangle was, gone elsewhere)
    if (getenv("HARNESS_CHURN")) {
        // pictures made and let go of every update, and now and then a frame with thousands of draws (the buffers grow): what is let go of must not be freed while the card uses it
        { NewTex t; t.id = 30 + tick % 8; t.w = 64; t.h = 64; t.rgba = checker(64, 64, 4 + tick % 5, 0xFF000000u | u32(tick * 977), 0xFFFFFFFFu); f.fresh.push_back(std::move(t)); }
        if (tick > 8) f.dead.push_back(30 + (tick + 4) % 8);
        const u32 s0 = u32(f.idx.size()); quad(f, -0.2f, -0.2f, 0.2f, 0.2f, 0.05f, 255, 255, 255, 255); draw(f, s0, 30 + (tick + 7) % 8, TEX_SHADE);
        if (tick % 40 == 39) for (int i = 0; i < 4000; i++) { const u32 s = u32(f.idx.size()); const float x = -0.95f + 1.9f * (i % 80) / 80.0f, y = -0.95f + 1.9f * (i / 80) / 50.0f; quad(f, x, y, x + 0.01f, y + 0.01f, 0.04f, 255, u8(i), 0, 255); draw(f, s, 0, SHADE); }
    }
    return f;
}

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? atof(argv[1]) : 12.0;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    window = SDL_CreateWindow("vk_harness", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, getenv("HARNESS_W") ? atoi(getenv("HARNESS_W")) : 1280, getenv("HARNESS_H") ? atoi(getenv("HARNESS_H")) : 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    std::thread picture(backend_run);
    picture.detach();
    const auto t0 = std::chrono::steady_clock::now();
    int tick = 0;
    const int crt_from = getenv("HARNESS_CRT") ? atoi(getenv("HARNESS_CRT")) : 0;
    pic_crt = crt_from;
    if (getenv("HARNESS_SHAPE")) pic_shape = atoi(getenv("HARNESS_SHAPE"));
    for (;;) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (t > seconds) break;
        SDL_PumpEvents();
        if (getenv("HARNESS_RESIZE")) {      // (the window made smaller, then larger, then the CRT toggled: the targets are remade, nothing should be left behind)
            static int stage; if (stage == 0 && t > seconds * 0.3) { SDL_SetWindowSize(window, 900, 700); stage = 1; }
            if (stage == 1 && t > seconds * 0.55) { SDL_SetWindowSize(window, 1500, 1000); stage = 2; }
            if (stage == 2 && t > seconds * 0.8) { pic_crt = 1; stage = 3; }
        }
        { Frame f = make_frame(tick, tick == 0); std::lock_guard<std::mutex> l(mtx_);
          if (have_ready_) for (NewTex& n : ready_.fresh) f.fresh.push_back(std::move(n));      // (an update the picture never took: its new pictures are not lost, as the reader keeps them too)
          ready_ = std::move(f); have_ready_ = true; }
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    char st[256]; backend_status(st, sizeof st); fprintf(stderr, "[harness] %d updates; backend: %s\n", tick, st);
    _Exit(0);
}
