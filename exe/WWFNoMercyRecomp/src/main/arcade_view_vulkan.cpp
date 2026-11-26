// [xplat 2026-11-12] The picture backend for Linux and the Steam Deck: Vulkan, through plume (RT64's render interface, MIT).
// It does what arcade_view_d3d12.cpp's run() does, in the same order and with the same names where it can, so the two can be read side by
// side: take each finished Frame off the reader, blend it between the last two game updates (k), draw the Titantron's picture (512 x 256),
// a sign's shape, then the frame at 4x MSAA with a depth buffer, resolve it, run the CRT last step if it's on, show it, keep BMP shots.
// The shaders are the same HLSL as Direct3D 12's, compiled to SPIR-V at build time (src/main/shaders/*.vk.hlsl, CMake runs dxc).
//
// Where Vulkan (or plume) made it differ from Direct3D 12:
//  * No root constants / root descriptors. A draw's constants (DrawCB) for the whole frame go in one storage buffer, indexed by a push
//    constant holding the draw's number; the matrices in a second one. Both are set 0, one set for each of the 3 frames in flight.
//  * A picture and how to read it are descriptor sets of their own (set 1 each picture, set 2 each of 18 samplers), made once.
//  * The points and triangles are in host-visible buffers (a ring of 4, one per game update still being drawn) instead of being copied
//    up to the card's own memory by a command: the same on the Steam Deck (one memory), a little less ideal on a card with its own.
//  * plume's swap chain images can't be copied from or resolved into, so the frame is always resolved into a picture of its own
//    (scene_tex) and the last step -- the CRT, or a plain copy (ps_blit) -- draws it into the swap chain. A QA shot draws that last step
//    once more, into a picture of its own (shot_tex), and copies that out.
//  * plume has no texture -> buffer copy; the shot's copy uses the Vulkan handles inside plume's objects (plume_vulkan.h is public).
//  * HDR10 isn't done yet (NOTES.md): asked for, it's left off and said so.
// Not Windows-only: nothing here but SDL's window (the game's own, main.cpp's `window`), plume, and the standard library.
// [vkwin 2026-11-12] On Windows too, beside Direct3D 12 (CMake ARCADE_VIEW_BACKEND=vulkan, then WCW_NATIVE_GPU=vulkan at run time), to compare
// the two backends picture by picture on one machine. There plume makes the Win32 surface from the game window's HWND (SDL hands it over),
// so the window needs no SDL_WINDOW_VULKAN; the doorway's two calls get their own names (arcade_view_gpu.h); the file's own types sit
// in an unnamed namespace so they can't be mixed up at link time with arcade_view_d3d12.cpp's of the same names (both have a Tex).
#include <SDL.h>
#include "arcade_host.h"   // (arcade::shown: paths for the log, privacy 2026-11-13)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif
#include "plume_vulkan.h"
#include "arcade_view_shared.h"
#include "arcade_view_gpu.h"
#include "arcade_media.h"
#include "arcade_platform.h"
#include "scene_vs.spv.h"
#include "scene_ps.spv.h"
#include "scene_screen.spv.h"
#include "scene_shape.spv.h"
#include "post_vs.spv.h"
#include "post_ps.spv.h"
#include "post_blit.spv.h"
#include "post_resolve.spv.h"      // [vkwin 2026-11-12] (arcade_view_resolve.vk.hlsl)

extern SDL_Window* window;                                              // (main.cpp: the game's window; made with SDL_WINDOW_VULKAN when this backend is built)
std::vector<std::filesystem::path> arcade_wrestlers_hd_pictures();     // (arcade_wrestlers.cpp)
#ifdef _WIN32
namespace plume { extern std::unique_ptr<RenderInterface> CreateVulkanInterface(); }     // [vkwin 2026-11-12] (plume without SDL: Windows has its HWND)
#else
namespace plume { extern std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow sdlWindow); }
#endif

namespace arcade_view {
#ifdef ARCADE_VIEW_VULKAN_TOO
bool both_chosen(); extern Frame ready2_; extern bool have_ready2_;      // [vkwin 2026-11-12] (WCW_NATIVE_GPU=both, a test: arcade_view.cpp)
#endif
namespace pl = plume;
namespace {     // [vkwin 2026-11-12] (this file's own: see the top)

static const unsigned FRAMES = 3, RING = 4;      // (pictures in flight; sets of points and triangles)
static const int TW = 512, TH = 256;
static const pl::RenderFormat PIC = pl::RenderFormat::R8G8B8A8_UNORM, BACK = pl::RenderFormat::B8G8R8A8_UNORM, DEPTH = pl::RenderFormat::D32_FLOAT;
static_assert(sizeof(V) == 40, "the vertex layout below");
static_assert(sizeof(DrawCB) == 144, "arcade_view_scene.vk.hlsl's DrawCBData");

// ---- what the GPU has -------------------------------------------------------------------------------------------------
struct Gpu {                       // a picture (or a target) with its place in the shaders' set 1
    std::unique_ptr<pl::RenderTexture> tex; std::unique_ptr<pl::RenderDescriptorSet> set;
    pl::RenderTextureLayout layout = pl::RenderTextureLayout::UNKNOWN;
};
typedef std::shared_ptr<Gpu> Tex;       // (shared so that a picture can be let go of "later": see release_later)
struct Slot {                      // one picture in flight
    std::unique_ptr<pl::RenderCommandList> cl; std::unique_ptr<pl::RenderCommandFence> fence; std::unique_ptr<pl::RenderCommandSemaphore> acquired;
    std::unique_ptr<pl::RenderBuffer> up, draws, xf; uint8_t* up_p = nullptr; uint64_t up_cap = 0, up_off = 0, draws_cap = 0, xf_cap = 0;     // (up: pictures on their way up. draws, xf: set 0's two buffers)
    std::unique_ptr<pl::RenderDescriptorSet> set0;
};
struct Geo { std::unique_ptr<pl::RenderBuffer> vb, ib; uint64_t vb_cap = 0, ib_cap = 0; uint64_t last_use = 0; };     // (the points and triangles of one game update)

static std::unique_ptr<pl::RenderInterface> rif; static std::unique_ptr<pl::RenderDevice> dev; static std::unique_ptr<pl::RenderCommandQueue> queue; static std::unique_ptr<pl::RenderSwapChain> sc;
static Slot slots[FRAMES]; static Slot* fr; static Geo ring[RING]; static int cur_geo = -1; static size_t geo_v, geo_i;
static uint64_t sub_n, done_n;      // frames submitted / known finished (frame n uses slots[n % FRAMES]; they finish in order)
static std::vector<std::unique_ptr<pl::RenderCommandSemaphore>> released;      // (one for each swap chain image)
static std::unique_ptr<pl::RenderPipelineLayout> layout;
static std::unique_ptr<pl::RenderShader> sh_vs, sh_ps, sh_screen, sh_shape, sh_post_vs, sh_post_ps, sh_blit, sh_resolve;
static std::unordered_map<int, std::unique_ptr<pl::RenderPipeline>> pipes;
static std::unique_ptr<pl::RenderSampler> samplers[18]; static std::unique_ptr<pl::RenderDescriptorSet> sampler_sets[18];
static pl::RenderDescriptorRange rng0[2], rng1[1], rng2[1]; static pl::RenderDescriptorSetDesc set_descs[3];
static std::vector<Tex> srvs; static Tex white, video_tex, tron_tex, shape_tex, scene_tex, native_tex, main_rt, main_ds, tron_ds, shot_tex;
static u32 native_w, native_h; static uint64_t native_serial; static bool native_has;
static std::unique_ptr<pl::RenderFramebuffer> fb_main, fb_tron, fb_shape, fb_shot, fb_scene; static std::vector<std::unique_ptr<pl::RenderFramebuffer>> fb_back;
static int W_ = 1280, H_ = 960;
static float cur_xs_ = 1.0f, cur_ys_ = 1.0f;
static bool use_video_; static uint64_t tron_switches_;
static bool frame_cam_, tron_has_;      // [visual 2026-11-19] (as Direct3D 12's: the frame in hand brought the Titantron's camera pass; tron_tex holds a camera picture)
static double jitter_sum_; static uint64_t jitter_n_, late_updates_, held_pictures_, pictures_seen_;
static std::vector<std::pair<uint64_t, std::shared_ptr<void>>> later_;
static pl::RenderInputSlot in_slot(0, sizeof(V));
static const pl::RenderColor BLACK(0, 0, 0, 1), NOTHING(0, 0, 0, 0);
static std::string device_name;

// what the GPU may still be reading: let go once the picture being built has finished
static void release_later(std::shared_ptr<void> r) { if (r) later_.push_back({ sub_n + 1, std::move(r) }); }
static void collect_later() {
    size_t n = 0;
    for (size_t i = 0; i < later_.size(); i++) if (later_[i].first > done_n) later_[n++] = std::move(later_[i]);
    later_.resize(n);
}
static void wait_upto(uint64_t n) {
    while (done_n < n && done_n < sub_n) { done_n++; queue->waitForCommandFence(slots[done_n % FRAMES].fence.get()); }
}
static void wait_idle() { wait_upto(sub_n); }

static void to(Gpu& g, pl::RenderTextureLayout l, pl::RenderBarrierStages st = pl::RenderBarrierStage::GRAPHICS) {
    if (g.layout == l) return;
    fr->cl->barriers(st, pl::RenderTextureBarrier(g.tex.get(), l)); g.layout = l;
}
// room for n bytes of things on their way up in this picture's staging buffer. (Grown by a bigger one when it must: the old one let go of later)
static uint8_t* upload(uint64_t n, uint64_t align, uint64_t* off, const pl::RenderBuffer** buf) {
    uint64_t at = (fr->up_off + align - 1) & ~(align - 1);
    if (!fr->up || at + n > fr->up_cap) {
        if (fr->up) { fr->up->unmap(); release_later(std::shared_ptr<pl::RenderBuffer>(std::move(fr->up))); }
        fr->up_cap = std::max<uint64_t>(fr->up_cap * 2, std::max<uint64_t>(n * 2, uint64_t(32) << 20));
        fr->up = dev->createBuffer(pl::RenderBufferDesc::UploadBuffer(fr->up_cap)); fr->up_p = static_cast<uint8_t*>(fr->up->map());
        at = 0;
    }
    fr->up_off = at + n; *off = at; *buf = fr->up.get();
    return fr->up_p + at;
}
// a buffer of the picture's own, made bigger when it has to be (contents don't survive: they're written afresh each picture). True if it was remade.
static bool grow(std::unique_ptr<pl::RenderBuffer>& b, uint64_t& cap, uint64_t need, pl::RenderBufferFlags flags) {
    if (b && need <= cap) return false;
    if (b) release_later(std::shared_ptr<pl::RenderBuffer>(std::move(b)));
    cap = std::max<uint64_t>(need * 2, uint64_t(1) << 20);
    pl::RenderBufferDesc d = pl::RenderBufferDesc::UploadBuffer(cap, flags);
    b = dev->createBuffer(d);
    return true;
}
static void set_of(Gpu& g) { g.set = dev->createDescriptorSet(set_descs[1]); g.set->setTexture(0, g.tex.get(), pl::RenderTextureLayout::SHADER_READ); }
static Tex make_target(int w, int h, int samples, pl::RenderFormat fmt, bool sampled_set) {
    auto g = std::make_shared<Gpu>();
    const pl::RenderMultisampling ms(samples == 4 ? pl::RenderSampleCount::COUNT_4 : pl::RenderSampleCount::COUNT_1);
    g->tex = dev->createTexture(fmt == DEPTH ? pl::RenderTextureDesc::DepthTarget(w, h, fmt, ms) : pl::RenderTextureDesc::ColorTarget(w, h, fmt, ms));
    if (sampled_set) set_of(*g);
    return g;
}
// a picture, with its smaller versions made (each the last averaged: no call for that in Vulkan or Direct3D 12), copied up in the picture's list
static Tex make_tex(u32 w, u32 h, const void* rgba, bool mipmapped = true) {
    auto g = std::make_shared<Gpu>();
    uint32_t mips = 1; if (mipmapped) for (u32 m = std::max(w, h); m > 1; m >>= 1) mips++;
    g->tex = dev->createTexture(pl::RenderTextureDesc::Texture2D(w, h, mips, PIC));
    if (!g->tex) return nullptr;
    to(*g, pl::RenderTextureLayout::COPY_DEST, pl::RenderBarrierStage::COPY);
    std::vector<u8> a(static_cast<const u8*>(rgba), static_cast<const u8*>(rgba) + size_t(w) * h * 4), b;
    u32 mw = w, mh = h;
    for (uint32_t m = 0; m < mips; m++) {
        const pl::RenderBuffer* up; uint64_t off;
        u8* to_ = upload(size_t(mw) * mh * 4, 16, &off, &up);
        std::memcpy(to_, a.data(), size_t(mw) * mh * 4);
        pl::RenderTextureCopyLocation dst; dst.type = pl::RenderTextureCopyType::SUBRESOURCE; dst.texture = g->tex.get(); dst.subresource.mipLevel = m; dst.subresource.arrayIndex = 0;
        fr->cl->copyTextureRegion(dst, pl::RenderTextureCopyLocation::PlacedFootprint(up, PIC, mw, mh, 1, mw, off));
        if (m + 1 < mips) {
            const u32 nw = std::max<u32>(mw >> 1, 1), nh = std::max<u32>(mh >> 1, 1);
            b.resize(size_t(nw) * nh * 4);
            for (u32 y = 0; y < nh; y++) for (u32 x = 0; x < nw; x++) {
                const u32 x0 = std::min(x * 2, mw - 1), x1 = std::min(x * 2 + 1, mw - 1), y0 = std::min(y * 2, mh - 1), y1 = std::min(y * 2 + 1, mh - 1);
                for (int c = 0; c < 4; c++) b[(size_t(y) * nw + x) * 4 + c] = u8((a[(size_t(y0) * mw + x0) * 4 + c] + a[(size_t(y0) * mw + x1) * 4 + c] + a[(size_t(y1) * mw + x0) * 4 + c] + a[(size_t(y1) * mw + x1) * 4 + c] + 2) / 4);
            }
            a.swap(b); mw = nw; mh = nh;
        }
    }
    to(*g, pl::RenderTextureLayout::SHADER_READ);
    set_of(*g);
    return g;
}
// (no2k15, 2026-11-23) The two built-in full-detail men -- the WWE 2K15 test slots, whose pictures were native_view.pak
// (WCW_NATIVE_PAK) -- are removed: the full-detail men are the wrestler folders' own now, man 0 the first folder's.
// The full-detail men from wrestler folders: their pictures.pak files (six pictures each), man 0 the first.
static void load_hd_folders() {
    srvs.assign(HD_MEN * 6, Tex{});
    int man = 0;
    for (const auto& path : arcade_wrestlers_hd_pictures()) {
        FILE* f = fopen(path.string().c_str(), "rb");      // [vkwin 2026-11-12] (string(): on Windows a path's c_str() is wide)
        if (f) {
            char m[4]; u32 n = 0; if (fread(m, 1, 4, f) == 4 && fread(&n, 4, 1, f) == 1) {
                std::vector<u8> b;
                for (u32 i = 0; i < n && i < 6; i++) {
                    u32 w, h, len; if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1) break;
                    b.resize(len); if (fread(b.data(), 1, len, f) != len) break;
                    srvs[size_t(man) * 6 + i] = make_tex(w, h, b.data());
                }
            }
            fclose(f);
        }
        else fprintf(stderr, "[view] %s can't be read: his pictures are missing\n", arcade::shown(path).c_str());
        man++;
    }
}

// target: 0 the frame, 1 the Titantron, 2 a sign's shape. shader: 0 the combiner, 1 the screen's picture, 2 the shape. blend: 0 over, 1 written, 2 colour only
static pl::RenderPipeline* pipe(int target, int shader, int zmode, int cull, int blend) {     // (cull: 0 none, 1 back, 2 front)
    const int key = target | shader << 2 | zmode << 4 | cull << 8 | blend << 10;
    auto it = pipes.find(key);
    if (it != pipes.end()) return it->second.get();
    static const pl::RenderInputElement ied[] = {
        { "POSITION", 0, 0, pl::RenderFormat::R32G32B32_FLOAT, 0, 0 }, { "TEXCOORD", 1, 1, pl::RenderFormat::R32G32B32_FLOAT, 0, 12 },
        { "TEXCOORD", 0, 2, pl::RenderFormat::R32G32_FLOAT, 0, 24 }, { "COLOR", 0, 3, pl::RenderFormat::R8G8B8A8_UNORM, 0, 32 },
        { "BLENDINDICES", 0, 4, pl::RenderFormat::R32_UINT, 0, 36 } };
    pl::RenderGraphicsPipelineDesc d;
    d.pipelineLayout = layout.get(); d.vertexShader = sh_vs.get(); d.pixelShader = (shader == 0 ? sh_ps : shader == 1 ? sh_screen : sh_shape).get();
    d.inputSlots = &in_slot; d.inputSlotsCount = 1; d.inputElements = ied; d.inputElementsCount = 5;
    d.primitiveTopology = pl::RenderPrimitiveTopology::TRIANGLE_LIST; d.renderTargetCount = 1; d.renderTargetFormat[0] = PIC;
    d.multisampling.sampleCount = target == 0 ? pl::RenderSampleCount::COUNT_4 : pl::RenderSampleCount::COUNT_1;
    d.cullMode = cull == 1 ? pl::RenderCullMode::BACK : cull == 2 ? pl::RenderCullMode::FRONT : pl::RenderCullMode::NONE; d.frontFace = pl::RenderFrontFace::COUNTER_CLOCKWISE; d.depthClipEnabled = true;
    auto& t = d.renderTargetBlend[0];
    t.renderTargetWriteMask = blend == 2 ? uint8_t(uint8_t(pl::RenderColorWriteEnable::RED) | uint8_t(pl::RenderColorWriteEnable::GREEN) | uint8_t(pl::RenderColorWriteEnable::BLUE)) : uint8_t(pl::RenderColorWriteEnable::ALL);
    t.blendEnabled = blend == 0; t.srcBlend = pl::RenderBlend::SRC_ALPHA; t.dstBlend = pl::RenderBlend::INV_SRC_ALPHA; t.blendOp = pl::RenderBlendOperation::ADD;
    t.srcBlendAlpha = pl::RenderBlend::ONE; t.dstBlendAlpha = pl::RenderBlend::ZERO; t.blendOpAlpha = pl::RenderBlendOperation::ADD;
    d.depthTargetFormat = target == 2 ? pl::RenderFormat::UNKNOWN : DEPTH;
    if (target != 2 && (zmode & 3)) {
        d.depthEnabled = true; d.depthFunction = pl::RenderComparisonFunction::LESS_EQUAL; d.depthWriteEnabled = (zmode & 3) != 3;
        // (a later layer, toward the camera: 16 of the depth value's least steps at its depth per level -- a float depth's step grows
        //  with the value, so this stays a hair at every distance -- and as much again per unit of slope, for a floor seen flat)
        const int lv = zmode >> 2;
        if (lv) { d.depthBias = -16 * lv; d.slopeScaledDepthBias = -1.0f * float(lv); }
    }
    auto p = dev->createGraphicsPipeline(d);
    if (!p) { fprintf(stderr, "[view] a pipeline failed (target %d shader %d)\n", target, shader); fflush(stderr); }
    return (pipes[key] = std::move(p)).get();
}
static pl::RenderPipeline* post_pipe(bool blit) {
    const int key = 0x10000 | int(blit);
    auto it = pipes.find(key);
    if (it != pipes.end()) return it->second.get();
    pl::RenderGraphicsPipelineDesc d;
    d.pipelineLayout = layout.get(); d.vertexShader = sh_post_vs.get(); d.pixelShader = (blit ? sh_blit : sh_post_ps).get();
    d.primitiveTopology = pl::RenderPrimitiveTopology::TRIANGLE_LIST; d.renderTargetCount = 1; d.renderTargetFormat[0] = BACK; d.renderTargetBlend[0] = pl::RenderBlendDesc::Copy();
    d.cullMode = pl::RenderCullMode::NONE; d.depthClipEnabled = true;
    auto p = dev->createGraphicsPipeline(d);
    if (!p) { fprintf(stderr, "[view] the last step's pipeline failed\n"); fflush(stderr); }
    return (pipes[key] = std::move(p)).get();
}

// [vkwin 2026-11-12] the MSAA resolve as Direct3D 12 does it (arcade_view_resolve.vk.hlsl says why): main_rt's four samples into scene_tex
static pl::RenderPipeline* resolve_pipe() {
    const int key = 0x20000;
    auto it = pipes.find(key);
    if (it != pipes.end()) return it->second.get();
    pl::RenderGraphicsPipelineDesc d;
    d.pipelineLayout = layout.get(); d.vertexShader = sh_post_vs.get(); d.pixelShader = sh_resolve.get();
    d.primitiveTopology = pl::RenderPrimitiveTopology::TRIANGLE_LIST; d.renderTargetCount = 1; d.renderTargetFormat[0] = PIC; d.renderTargetBlend[0] = pl::RenderBlendDesc::Copy();
    d.cullMode = pl::RenderCullMode::NONE; d.depthClipEnabled = true;
    auto p = dev->createGraphicsPipeline(d);
    if (!p) { fprintf(stderr, "[view] the resolve's pipeline failed\n"); fflush(stderr); }
    return (pipes[key] = std::move(p)).get();
}
static const bool resolve_by_copy = [] { const char* e = getenv("WCW_NATIVE_VK_RESOLVE"); return e && !std::strcmp(e, "copy"); }();     // (the old way, to compare)
static const bool resolve_probe = [] { const char* e = getenv("WCW_NATIVE_VK_RESOLVE"); return e && !std::strcmp(e, "probe"); }();     // (a test: every other picture shows the sums' last bits)
static const float resolve_up = float(env_int("WCW_NATIVE_VK_RESOLVE_UP", 3));      // (the rule: see arcade_view_resolve.vk.hlsl. 3: measured)

static float cur_k_ = 1.0f;     // how far along between the last two updates this picture is (the shaders' `blend`)
struct PushScene { float blend, sx, sy, unused; uint32_t draw; };      // (arcade_view_scene.vk.hlsl's PushC: 20 bytes)
static void draw_all(const Frame& f, int target, float ox, float oy, float tw, float th, int tgt_w, int tgt_h) {
    pl::RenderPipeline* last = nullptr; const Gpu* last_t = nullptr; int last_s = -1;
    // (all of the game's picture: all of this one's, whatever its shape. Part of it: the same part, in the middle)
    const float xs = target == 0 ? cur_xs_ : 1.0f, ys = target == 0 ? cur_ys_ : 1.0f, cx = ox + tw * 0.5f, cy = oy + th * 0.5f;
    for (size_t di = 0; di < f.draws.size(); di++) {
        const Draw& d = f.draws[di];
        if (!d.count || d.target != target) continue;
        Tex sv = white;
        if (d.tex == -1) { if (target == 1) continue; sv = use_video_ ? video_tex : tron_tex; }
        else if (d.tex == -3) { if (target == 1) continue; sv = shape_tex; }
        else if (d.tex == -4) { if (!native_has) continue; sv = native_tex; }      // (a native video; no picture of it decoded yet: the screen's part stays black)
        else if (d.tex >= 0 && d.tex < int(srvs.size()) && srvs[d.tex]) sv = srvs[d.tex];
        const bool scr = d.tex == -1;
        // (a sign's shape: written to its alpha; the picture over it keeps that alpha)
        pl::RenderPipeline* ps = target == 2 ? pipe(2, scr ? 1 : 2, 0, 0, scr ? 2 : 1) : pipe(target, scr ? 1 : 0, d.zmode, d.cull, 0); depth_draws_[d.zmode & 3]++;
        if (!ps) continue;
        if (ps != last) { fr->cl->setPipeline(ps); last = ps; }
        if (sv.get() != last_t) { fr->cl->setGraphicsDescriptorSet(sv->set.get(), 1); last_t = sv.get(); }
        const int si = (d.point ? 9 : 0) + (d.cms == 1 ? 1 : d.cms >= 2 ? 2 : 0) * 3 + (d.cmt == 1 ? 1 : d.cmt >= 2 ? 2 : 0);
        if (si != last_s) { fr->cl->setGraphicsDescriptorSet(sampler_sets[si].get(), 2); last_s = si; }
        const bool wx = d.sc[0] <= 0.0f && d.sc[2] >= 1.0f, wy = d.sc[1] <= 0.0f && d.sc[3] >= 1.0f;
        int l = int(wx ? ox : cx + (d.sc[0] * 2 - 1) * tw * 0.5f * xs), t = int(wy ? oy : cy + (d.sc[1] * 2 - 1) * th * 0.5f * ys);
        int r = int((wx ? ox + tw : cx + (d.sc[2] * 2 - 1) * tw * 0.5f * xs) + 0.99f), b = int((wy ? oy + th : cy + (d.sc[3] * 2 - 1) * th * 0.5f * ys) + 0.99f);
        l = std::max(l, 0); t = std::max(t, 0); r = std::min(r, tgt_w); b = std::min(b, tgt_h);     // (Vulkan: a scissor can't reach outside the target)
        if (r <= l || b <= t) continue;
        fr->cl->setScissors(pl::RenderRect(l, t, r, b));
        const PushScene pc{ cur_k_, xs, ys, 0.0f, uint32_t(di) };
        fr->cl->setGraphicsPushConstants(0, &pc, 0, sizeof pc);
        fr->cl->drawIndexedInstanced(d.count, 1, d.start, 0, 0);
    }
}

// the things the window's size decides. (the swap chain's pictures are plume's; these are ours)
static void make_targets() {
    W_ = int(sc->getWidth()); H_ = int(sc->getHeight());
    fb_back.clear();
    for (uint32_t i = 0; i < sc->getTextureCount(); i++) { const pl::RenderTexture* t = sc->getTexture(i); fb_back.push_back(dev->createFramebuffer(pl::RenderFramebufferDesc(&t, 1))); }
    scene_tex = make_target(W_, H_, 1, PIC, true);
    shot_tex = make_target(W_, H_, 1, BACK, false);
    main_rt = make_target(W_, H_, 4, PIC, true);      // [vkwin 2026-11-12] (true: the resolve's shader reads it)
    main_ds = make_target(W_, H_, 4, DEPTH, false);
    { const pl::RenderTexture* c = main_rt->tex.get(); fb_main = dev->createFramebuffer(pl::RenderFramebufferDesc(&c, 1, main_ds->tex.get())); }
    { const pl::RenderTexture* c = shot_tex->tex.get(); fb_shot = dev->createFramebuffer(pl::RenderFramebufferDesc(&c, 1)); }
    { const pl::RenderTexture* c = scene_tex->tex.get(); fb_scene = dev->createFramebuffer(pl::RenderFramebufferDesc(&c, 1)); }     // [vkwin 2026-11-12] (the resolve draws here)
}
static void drop_targets() {      // (only between pictures, with the card idle)
    fb_main.reset(); fb_shot.reset(); fb_scene.reset(); fb_back.clear(); scene_tex.reset(); shot_tex.reset(); main_rt.reset(); main_ds.reset();
}

// A picture of what the card is doing for a crash report / the [view] line
#ifdef _WIN32
static long rss_mb() { PROCESS_MEMORY_COUNTERS pm{}; pm.cb = sizeof pm; K32GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm); return long(pm.WorkingSetSize / 1048576); }     // [vkwin 2026-11-12] (as the Direct3D 12 line has it)
#else
static long rss_mb() { long a = 0, b = 0; if (FILE* f = fopen("/proc/self/statm", "r")) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); } return b * (sysconf(_SC_PAGESIZE) / 1024) / 1024; }
#endif

static void begin_frame() {
    const uint64_t n = sub_n + 1;
    fr = &slots[n % FRAMES];
    if (n > FRAMES) wait_upto(n - FRAMES);       // (this slot's last picture is done)
    collect_later();
    fr->up_off = 0;
    fr->cl->begin();
}
static void write_bmp(const char* path, const uint8_t* px, int w, int h, size_t pitch) {      // (px: B G R A, as the screen's pictures are in memory)
    FILE* o = fopen(path, "wb"); if (!o) return;
    const int sz = 54 + w * h * 3;
    u8 hdr[54] = { 'B', 'M' }; std::memcpy(hdr + 2, &sz, 4); hdr[10] = 54; hdr[14] = 40; std::memcpy(hdr + 18, &w, 4); std::memcpy(hdr + 22, &h, 4); hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, o);
    std::vector<u8> row((size_t(w) * 3 + 3) & ~size_t(3));
    for (int y = h - 1; y >= 0; y--) { const u8* q = px + size_t(y) * pitch; for (int x = 0; x < w; x++) { row[size_t(x) * 3] = q[x * 4]; row[size_t(x) * 3 + 1] = q[x * 4 + 1]; row[size_t(x) * 3 + 2] = q[x * 4 + 2]; } fwrite(row.data(), 1, row.size(), o); }
    fclose(o);
}

// [vkwin 2026-11-12] The program's end: this file's GPU objects are static, so exit() destroys them (the device, the swap chain) while this
// thread may still be using them: a WCW_NATIVE_GPU=both run crashed so in vkAcquireNextImageKHR as the game quit. Now exit() first asks
// the picture thread to stop: it lets the card finish and parks, and only then are the objects let go.
static std::atomic<int> exit_state_{ 0 };      // (1: exit asked, 2: parked)
static void at_exit() { exit_state_ = 1; for (int i = 0; i < 200 && exit_state_ != 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

static void run() {
    // the game's own window (main.cpp makes it on the game's thread, a moment from now)
    for (int i = 0; i < 400 && !window; i++) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    if (!window) { fprintf(stderr, "[view] no window to draw into\n"); fflush(stderr); return; }
#ifdef _WIN32
    // [vkwin 2026-11-12] Windows: the game window's HWND, the one Direct3D 12 draws into in mode 3 (plume makes the Vulkan surface of it)
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm) || !wm.info.win.window) { fprintf(stderr, "[view] the game's window has no HWND: %s\n", SDL_GetError()); fflush(stderr); return; }
    pl::RenderWindow target = wm.info.win.window;
#ifdef ARCADE_VIEW_VULKAN_TOO
    const bool both = both_chosen();
    if (both) {
        // (beside Direct3D 12, which has the game's window: a window of our own, the same size, off the screen)
        RECT c{}; GetClientRect(target, &c);
        WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "NoMercyVulkanCheck"; RegisterClassA(&wc);
        RECT r{ 0, 0, std::max<LONG>(c.right, 64), std::max<LONG>(c.bottom, 64) }; AdjustWindowRect(&r, WS_POPUP, FALSE);
        target = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, "NoMercyVulkanCheck", "", WS_POPUP | WS_VISIBLE, -30000, -30000, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
        fprintf(stderr, "[view] Vulkan beside Direct3D 12 (WCW_NATIVE_GPU=both): %ld x %ld, its own window\n", c.right, c.bottom); fflush(stderr);
    }
#else
    const bool both = false;
#endif
    rif = pl::CreateVulkanInterface();
#else
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)) { fprintf(stderr, "[view] the game's window wasn't made for Vulkan (SDL_WINDOW_VULKAN): no picture\n"); fflush(stderr); return; }
    if (mode_ != 3) { fprintf(stderr, "[view] WCW_NATIVE_VIEW=%d: on Linux the picture is always in the game's own window (mode 3)\n", mode_); fflush(stderr); }
    const pl::RenderWindow target = window;
    rif = pl::CreateVulkanInterface(window);
    const bool both = false;      // [deck 2026-11-13] (WCW_NATIVE_GPU=both is a Windows test; without this line Linux did not compile)
#endif
    if (!rif) { fprintf(stderr, "[view] no Vulkan (the loader or a driver is missing)\n"); fflush(stderr); return; }
    dev = rif->createDevice(getenv("WCW_NATIVE_GPU_NAME") ? getenv("WCW_NATIVE_GPU_NAME") : "");
    if (!dev) { fprintf(stderr, "[view] no Vulkan device\n"); fflush(stderr); return; }
    device_name = dev->getDescription().name;
    fprintf(stderr, "[view] Vulkan (through plume)\n"); fflush(stderr);   // (privacy 2026-11-13: not the graphics card's name)
    queue = dev->createCommandQueue(pl::RenderCommandListType::DIRECT);
    sc = queue->createSwapChain(pl::RenderSwapChainDesc(target, BACK, FRAMES));
    if (!sc || !sc->resize()) { fprintf(stderr, "[view] no swap chain\n"); fflush(stderr); return; }
    for (auto& s : slots) { s.cl = queue->createCommandList(); s.fence = dev->createCommandFence(); s.acquired = dev->createCommandSemaphore(); }
    for (uint32_t i = 0; i < sc->getTextureCount(); i++) released.push_back(dev->createCommandSemaphore());

    // what the shaders are given (set 0: the matrices and every draw's constants; set 1: a picture; set 2: how to read it) and the 48 bytes of push constants
    rng0[0] = pl::RenderDescriptorRange(pl::RenderDescriptorRangeType::STRUCTURED_BUFFER, 0, 1); rng0[1] = pl::RenderDescriptorRange(pl::RenderDescriptorRangeType::STRUCTURED_BUFFER, 1, 1);
    rng1[0] = pl::RenderDescriptorRange(pl::RenderDescriptorRangeType::TEXTURE, 0, 1); rng2[0] = pl::RenderDescriptorRange(pl::RenderDescriptorRangeType::SAMPLER, 0, 1);
    set_descs[0] = pl::RenderDescriptorSetDesc(rng0, 2); set_descs[1] = pl::RenderDescriptorSetDesc(rng1, 1); set_descs[2] = pl::RenderDescriptorSetDesc(rng2, 1);
    const pl::RenderPushConstantRange pcr(0, 0, 0, 48, pl::RenderShaderStageFlag::VERTEX | pl::RenderShaderStageFlag::PIXEL);
    layout = dev->createPipelineLayout(pl::RenderPipelineLayoutDesc(&pcr, 1, set_descs, 3, false, true));
    for (auto& s : slots) s.set0 = dev->createDescriptorSet(set_descs[0]);
    const pl::RenderShaderFormat spv = pl::RenderShaderFormat::SPIRV;
    sh_vs = dev->createShader(spv_scene_vs, spv_scene_vs_size, "vs", spv); sh_ps = dev->createShader(spv_scene_ps, spv_scene_ps_size, "ps", spv);
    sh_screen = dev->createShader(spv_scene_screen, spv_scene_screen_size, "ps_screen", spv); sh_shape = dev->createShader(spv_scene_shape, spv_scene_shape_size, "ps_shape", spv);
    sh_post_vs = dev->createShader(spv_post_vs, spv_post_vs_size, "vs", spv); sh_post_ps = dev->createShader(spv_post_ps, spv_post_ps_size, "ps", spv);
    sh_blit = dev->createShader(spv_post_blit, spv_post_blit_size, "ps_blit", spv);
    sh_resolve = dev->createShader(spv_post_resolve, spv_post_resolve_size, "ps_resolve", spv);      // [vkwin 2026-11-12]
    static const pl::RenderTextureAddressMode AM[3] = { pl::RenderTextureAddressMode::WRAP, pl::RenderTextureAddressMode::MIRROR, pl::RenderTextureAddressMode::CLAMP };
    for (int pt = 0; pt < 2; pt++) for (int a = 0; a < 3; a++) for (int c = 0; c < 3; c++) {
        pl::RenderSamplerDesc d; d.minFilter = d.magFilter = pt ? pl::RenderFilter::NEAREST : pl::RenderFilter::LINEAR; d.mipmapMode = pt ? pl::RenderMipmapMode::NEAREST : pl::RenderMipmapMode::LINEAR;
        d.addressU = AM[a]; d.addressV = AM[c]; d.addressW = pl::RenderTextureAddressMode::WRAP; d.maxAnisotropy = 1; d.anisotropyEnabled = false;
        const int i = pt * 9 + a * 3 + c;
        samplers[i] = dev->createSampler(d); sampler_sets[i] = dev->createDescriptorSet(set_descs[2]); sampler_sets[i]->setSampler(0, samplers[i].get());
    }
    make_targets();

    // the first picture's list: the pictures that are always there, and the targets in their first state
    begin_frame();
    tron_tex = make_target(TW, TH, 1, PIC, true); shape_tex = make_target(TW, TH, 1, PIC, true); tron_ds = make_target(TW, TH, 1, DEPTH, false);
    { const pl::RenderTexture* c = tron_tex->tex.get(); fb_tron = dev->createFramebuffer(pl::RenderFramebufferDesc(&c, 1, tron_ds->tex.get())); }
    { const pl::RenderTexture* c = shape_tex->tex.get(); fb_shape = dev->createFramebuffer(pl::RenderFramebufferDesc(&c, 1)); }
    to(*shape_tex, pl::RenderTextureLayout::COLOR_WRITE); fr->cl->setFramebuffer(fb_shape.get()); fr->cl->clearColor(0, NOTHING);     // (nothing, until a sign has a shape)
    to(*shape_tex, pl::RenderTextureLayout::SHADER_READ);
    { const u32 wh = 0xFFFFFFFFu; white = make_tex(1, 1, &wh, false); }
    { const u32 bk = 0xFF000000u; native_tex = make_tex(1, 1, &bk, false); native_w = native_h = 1; }
    { std::vector<u32> z(64 * 32, 0); video_tex = make_tex(64, 32, z.data(), false); }
    load_hd_folders();     // (the full-detail men's pictures: the wrestler folders' -- no2k15)
    fr->cl->end(); { const pl::RenderCommandList* l = fr->cl.get(); queue->executeCommandLists(&l, 1, nullptr, 0, nullptr, 0, fr->fence.get()); } sub_n++;
    wait_idle();

    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    auto now_s = [&] { return std::chrono::duration<double>(clk::now() - t0).count(); };
    const char* shots = getenv("WCW_NATIVE_SHOTS"); double next_shot = 1.0; int shot_n = 0;
    Frame f; double tp = 0;
    Frame next; bool next_ok = false; Pace pace;      // [visual 2026-11-19] (Smooth Motion's clock, as Direct3D 12's: the update taken, not yet due)
    const int env_interp = env_int("WCW_NATIVE_INTERP", -1), env_vsync = env_int("WCW_NATIVE_VSYNC", -1);    // (a test can hold these)
    const int burst_n = getenv("WCW_NATIVE_SHOT_BURST") ? atoi(getenv("WCW_NATIVE_SHOT_BURST")) : 0; int burst_left = 0;
    double sum_ms = 0, log_at = 5.0, update_time = 1.0 / 30.0; int pictures = 0, updates = 0; bool have = false;
    std::vector<float> xm;
    double next_hz = 0;
    std::atexit(at_exit);      // [vkwin 2026-11-12] (after the objects above: it runs before they're destroyed)
    for (;;) {
        if (exit_state_ == 1) { wait_idle(); exit_state_ = 2; for (;;) std::this_thread::sleep_for(std::chrono::hours(1)); }
        const bool interp = env_interp >= 0 ? env_interp != 0 : pic_smooth != 0, vsync = env_vsync >= 0 ? env_vsync != 0 : pic_limit == 0;
        bool got = false;
#ifdef ARCADE_VIEW_VULKAN_TOO
        if (both) { MSG m; while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&m); }
        if (both) { std::lock_guard<std::mutex> l(mtx_); if (have_ready2_) { std::swap(f, ready2_); have_ready2_ = false; got = true; } } else
#endif
        // [visual 2026-11-19] (paced, as Direct3D 12's -- arcade_view_shared.h Pace. Here the wait for the screen comes later, in
        //  acquireTexture: the update is taken when it's due by the time a refresh from now. WCW_NATIVE_PACE=0: as found, as before)
        const bool paced = pace_on() != 0 && !both && interp;     // (Smooth Motion only, as Direct3D 12's)
        if (!paced && next_ok) { std::swap(f, next); next_ok = false; got = true; }     // (Smooth Motion just turned off: the update held first)
        else if (!paced) { std::lock_guard<std::mutex> l(mtx_); if (have_ready_) { std::swap(f, ready_); have_ready_ = false; got = true; } }
        else {
            { std::lock_guard<std::mutex> l(mtx_); if (have_ready_ && !next_ok) { std::swap(next, ready_); have_ready_ = false; next_ok = true; } }
            if (next_ok) {
                const double refresh = 1000.0 / std::max(1u, screen_hz.load());
                const double now = clock_ms(arcade::platform::ticks()) + (have && interp && vsync ? refresh : 0.0);
                if (pace.due(f, have, next, now)) { std::swap(f, next); next_ok = false; got = true; pace.took(f, refresh, now); }
            }
        }
        double a = now_s();
        if (!got) {
            // (nothing from the game for a moment: the last one's picture goes on being shown, blended on)
            if (!have || !interp) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        } else {
            // (how long an update takes, as it's going: for how far along the blend is)
            const double since = a - tp;
            if (have && since > 0.008 && since < 0.2) update_time += (since - update_time) * 0.1;
            // (2026-11-09, the user: wrestlers' walks in the entrances "are jittery a bit". Measured here: how far each update's arrival
            //  is from the average, and how many pictures had to stand still waiting for one -- k held at 1 -- see the [view] line)
            if (have && since > 0.008 && since < 0.2) { const double off = since - update_time; jitter_sum_ += off * off; jitter_n_++; if (since > update_time * 1.25) late_updates_++; }
            tp = a; have = true; updates++;
        }
        if (a >= next_hz) { next_hz = a + 5.0; SDL_DisplayMode dm{}; const int di = SDL_GetWindowDisplayIndex(window); if (di >= 0 && SDL_GetCurrentDisplayMode(di, &dm) == 0 && dm.refresh_rate > 1) screen_hz = uint32_t(dm.refresh_rate); }
        // HDR10 isn't done on this backend yet: said once, left off
        if (pic_hdr != 0) { static bool said; if (!said) { said = true; fprintf(stderr, "[view] HDR asked for: not on the Vulkan picture yet (NOTES.md): left off\n"); fflush(stderr); } pic_hdr = 0; }
        // the window's size (a fullscreen switch, a drag), and the screen's rate being kept or not
        if (vsync != sc->isVsyncEnabled() || sc->needsResize()) {
            wait_idle(); drop_targets();
            if (vsync != sc->isVsyncEnabled()) sc->setVsyncEnabled(vsync);
            sc->resize(); make_targets();
        }
        bool draw = !sc->isEmpty();
        begin_frame();
        const uint64_t F = sub_n + 1;
        if (got) {
            for (NewTex& n : f.fresh) { if (int(srvs.size()) <= n.id) srvs.resize(n.id + 1); if (srvs[n.id]) release_later(srvs[n.id]); srvs[n.id] = make_tex(n.w, n.h, n.rgba.data()); }
            f.fresh.clear();
            for (int id : f.dead) if (id < int(srvs.size()) && srvs[id]) { release_later(srvs[id]); srvs[id].reset(); }
            f.dead.clear();
            // the update's points and triangles, in the next set of the ring (once the card is through with the picture that last used it)
            const int g = (cur_geo + 1) % int(RING);
            Geo& G = ring[g]; wait_upto(G.last_use);
            const uint64_t vn = f.v.size() * sizeof(V), in = f.idx.size() * 4u;
            grow(G.vb, G.vb_cap, vn, pl::RenderBufferFlag::VERTEX); grow(G.ib, G.ib_cap, in, pl::RenderBufferFlag::INDEX);
            if (vn) { void* p = G.vb->map(); std::memcpy(p, f.v.data(), vn); G.vb->unmap(); }
            if (in) { void* p = G.ib->map(); std::memcpy(p, f.idx.data(), in); G.ib->unmap(); }
            cur_geo = g; geo_v = f.v.size(); geo_i = f.idx.size();
            bool cam = false; for (const Draw& d : f.draws) if (d.target == 1 && d.count) { cam = true; break; }
            // (2026-11-09, the user: "The titantron sceen picture flickers sometimes, especially during matches as it displays the
            //  match on its screen". tron.c draws the live camera's pass only in the frame's last run of the scene (layer_first ==
            //  last_first_was), so an update can come with no camera pass while the screen is still live: that update showed the
            //  64 x 32 video memory in place of the camera's picture. The camera's last picture is kept unless no pass has come for
            //  several updates: then the screen really is on its video.)
            static int updates_without_cam = 1000;
            frame_cam_ = cam;
            if (cam) updates_without_cam = 0; else if (updates_without_cam < 1000) updates_without_cam++;
            const bool now_video = updates_without_cam > 6;
            if (now_video != use_video_) tron_switches_++;
            use_video_ = now_video;
            if (use_video_ && f.video.size() == 64u * 32u) {
                const pl::RenderBuffer* up; uint64_t off; u8* to_ = upload(256u * 32u, 16, &off, &up);
                std::memcpy(to_, f.video.data(), 256u * 32u);
                to(*video_tex, pl::RenderTextureLayout::COPY_DEST, pl::RenderBarrierStage::COPY);
                pl::RenderTextureCopyLocation dst; dst.type = pl::RenderTextureCopyType::SUBRESOURCE; dst.texture = video_tex->tex.get(); dst.subresource.mipLevel = 0; dst.subresource.arrayIndex = 0;
                fr->cl->copyTextureRegion(dst, pl::RenderTextureCopyLocation::PlacedFootprint(up, PIC, 64, 32, 1, 64, off));
                to(*video_tex, pl::RenderTextureLayout::SHADER_READ);
            }
        }
        // 2026-11-10: a native Titantron video's picture, the one due now by the game's clock (arcade_media.cpp), copied up when
        // it's a new one. Asked every picture drawn, not every game update: a 60-a-second file shows at 60.
        if (const std::shared_ptr<const arcade_media::Picture> pic = arcade_media_video_picture()) {
            if (pic->serial != native_serial || !native_has) {
                const u32 pw = u32(pic->width), ph = u32(pic->height);
                if (pw > 0 && ph > 0 && pic->rgba.size() >= size_t(pw) * ph * 4) {
                    if (pw != native_w || ph != native_h) {      // (another size: a new texture in the old one's place; the old one goes when the card's done with it)
                        release_later(native_tex);
                        native_tex = std::make_shared<Gpu>(); native_tex->tex = dev->createTexture(pl::RenderTextureDesc::Texture2D(pw, ph, 1, PIC)); set_of(*native_tex);
                        native_w = pw; native_h = ph;
                    }
                    const pl::RenderBuffer* up; uint64_t off; u8* to_ = upload(uint64_t(pw) * ph * 4, 16, &off, &up);
                    std::memcpy(to_, pic->rgba.data(), size_t(pw) * ph * 4);
                    to(*native_tex, pl::RenderTextureLayout::COPY_DEST, pl::RenderBarrierStage::COPY);
                    pl::RenderTextureCopyLocation dst; dst.type = pl::RenderTextureCopyType::SUBRESOURCE; dst.texture = native_tex->tex.get(); dst.subresource.mipLevel = 0; dst.subresource.arrayIndex = 0;
                    fr->cl->copyTextureRegion(dst, pl::RenderTextureCopyLocation::PlacedFootprint(up, PIC, pw, ph, 1, pw, off));
                    to(*native_tex, pl::RenderTextureLayout::SHADER_READ);
                    native_serial = pic->serial; native_has = true;
                }
            }
        }
        else native_has = false;        // (no video, or the next one's first picture isn't here yet: the last video's isn't shown for it)

        uint32_t image = 0; bool acquired = false;
        if (draw && cur_geo >= 0) {
            acquired = sc->acquireTexture(fr->acquired.get(), &image);      // (waits for the screen's rate, if it's kept: as late as possible for the least delay)
            if (!acquired) draw = false;
        } else draw = false;
        if (!draw) {      // (hidden, or nothing to draw yet: the pictures made all the same; nothing to show)
            fr->cl->end(); { const pl::RenderCommandList* l = fr->cl.get(); pl::RenderCommandSemaphore* ws = acquired ? fr->acquired.get() : nullptr;
              queue->executeCommandLists(&l, 1, ws ? &ws : nullptr, ws ? 1 : 0, nullptr, 0, fr->fence.get()); } sub_n = F;
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        a = now_s();
        Geo& G = ring[cur_geo]; G.last_use = F;
        // how far along between the last two updates
        float k = 1.0f;
        if (interp && !f.snap && paced) { k = pace.k(f, clock_ms(arcade::platform::ticks())); if (k >= 1.0f) held_pictures_++; }     // [visual 2026-11-19]
        else if (interp && !f.snap) { k = float((a - tp) / update_time); if (k >= 1.0f) held_pictures_++; k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k; }
        pictures_seen_++;
        cur_k_ = k;
        // this picture's matrices
        float vp_now[2][16];
        for (int t = 0; t < 2; t++) {
            const Cam& c = f.cam[t];
            if (c.moving && k < 1.0f) { float v[16], pr[16]; put_together(c.va, c.vb, k, v); for (int i = 0; i < 16; i++) pr[i] = c.pa[i] + (c.pb[i] - c.pa[i]) * k; mul4(v, pr, vp_now[t]); }
            else std::memcpy(vp_now[t], c.vp, 64);
        }
        const size_t nx = f.xf.size();
        xm.resize(std::max<size_t>(nx, 1) * 16);
        if (!nx) for (int i = 0; i < 16; i++) xm[i] = (i % 5 == 0);
        for (size_t i = 0; i < nx; i++) {
            const Xf& x = f.xf[i];
            if (x.kind == 1) {
                float w[16];
                // (2026-11-09, the user: the Austin WWE 13 model's "thin seperator lines between parts of his model". Each part's matrix
                //  is blended on its own: turn, stretch and place apart. Two parts that meet at a joint meet there in both updates, but
                //  blended apart they needn't in between, so a hairline can open on the pictures between updates. Blended as a whole
                //  matrix, straight, a point both parts share stays shared on every picture (what a turn gives up: the part shrinks a
                //  little mid-turn, 1% at 15 degrees an update). WCW_NATIVE_LINEAR=1 to compare; not the default until a test says so.)
                static const bool linear_parts = env_int("WCW_NATIVE_LINEAR", 0) != 0;
                if (linear_parts && k < 1.0f) { float A[16], B[16]; put_together(x.a, x.b, 0.0f, A); put_together(x.a, x.b, 1.0f, B); for (int j = 0; j < 16; j++) w[j] = A[j] + (B[j] - A[j]) * k; }
                else put_together(x.a, x.b, k, w);
                mul4(w, vp_now[x.target], &xm[i * 16]);
            }
            else std::memcpy(&xm[i * 16], x.direct, 64);
        }
        // set 0: the matrices, and every draw's constants (the push constant says which)
        {   const bool r1 = grow(fr->xf, fr->xf_cap, xm.size() * 4u, pl::RenderBufferFlag::STORAGE), r2 = grow(fr->draws, fr->draws_cap, std::max<size_t>(f.draws.size(), 1) * sizeof(DrawCB), pl::RenderBufferFlag::STORAGE);
            if (r1) fr->set0->setBuffer(0, fr->xf.get(), fr->xf_cap);
            if (r2) fr->set0->setBuffer(1, fr->draws.get(), fr->draws_cap);
            { void* p = fr->xf->map(); std::memcpy(p, xm.data(), xm.size() * 4u); fr->xf->unmap(); }
            { u8* p = static_cast<u8*>(fr->draws->map()); for (size_t i = 0; i < f.draws.size(); i++) std::memcpy(p + i * sizeof(DrawCB), &f.draws[i].cb, sizeof(DrawCB)); fr->draws->unmap(); } }
        auto& cl = *fr->cl;
        cl.setGraphicsPipelineLayout(layout.get());
        cl.setGraphicsDescriptorSet(fr->set0.get(), 0);
        { const pl::RenderVertexBufferView vbv(pl::RenderBufferReference(G.vb.get()), uint32_t(geo_v * sizeof(V))); cl.setVertexBuffers(0, &vbv, 1, &in_slot);
          const pl::RenderIndexBufferView ibv(pl::RenderBufferReference(G.ib.get()), uint32_t(geo_i * 4u), pl::RenderFormat::R32_UINT); cl.setIndexBuffer(&ibv); }
        // the Titantron's picture first: the list's own camera pass, into a target of its own at full detail
        // [visual 2026-11-19] (as Direct3D 12's: an update with no camera pass while the screen is live keeps the picture it has)
        const bool tron_kept = tron_keep_on() && !frame_cam_ && !use_video_ && tron_has_;
        if (!tron_kept) {
        to(*tron_tex, pl::RenderTextureLayout::COLOR_WRITE); to(*tron_ds, pl::RenderTextureLayout::DEPTH_WRITE);
        cl.setFramebuffer(fb_tron.get()); cl.clearColor(0, BLACK); cl.clearDepth(true, 1.0f);
        cl.setViewports(pl::RenderViewport(0, 0, float(TW), float(TH)));
        draw_all(f, 1, 0, 0, float(TW), float(TH), TW, TH);
        to(*tron_tex, pl::RenderTextureLayout::SHADER_READ);
        tron_has_ = frame_cam_;
        }
        // a sign's own shape, if there's one: that picture cut to it (tron.c mask_compose: cleared to nothing, the shape, the picture over it)
        { bool shaped = false; for (const Draw& d : f.draws) if (d.target == 2 && d.count) { shaped = true; break; }
          if (shaped) {
              to(*shape_tex, pl::RenderTextureLayout::COLOR_WRITE);
              cl.setFramebuffer(fb_shape.get()); cl.clearColor(0, NOTHING);
              draw_all(f, 2, 0, 0, float(TW), float(TH), TW, TH);
          }
          to(*shape_tex, pl::RenderTextureLayout::SHADER_READ); }
        // then the frame
        to(*main_rt, pl::RenderTextureLayout::COLOR_WRITE); to(*main_ds, pl::RenderTextureLayout::DEPTH_WRITE);
        cl.setFramebuffer(fb_main.get()); cl.clearColor(0, BLACK); cl.clearDepth(true, 1.0f);
        // (the picture's shape, as large as it fits. Wider than the game's 4 : 3: more of the view at the sides; narrower: more above and below. Nothing's stretched: what's on the screen, the HUD, stays its shape, in the middle)
        const float shape = SHAPES[pic_shape.load()];
        float vw = float(W_), vh = float(H_);
        if (vw > vh * shape) vw = vh * shape; else vh = vw / shape;
        const float vx = (W_ - vw) * 0.5f, vy = (H_ - vh) * 0.5f;
        cl.setViewports(pl::RenderViewport(vx, vy, vw, vh));
        cur_xs_ = std::min(1.0f, (4.0f / 3.0f) / shape); cur_ys_ = std::min(1.0f, shape / (4.0f / 3.0f));
        draw_all(f, 0, vx, vy, vw, vh, W_, H_);
        // its four samples a pixel made one, into the picture of its own; from there the last step (the CRT, or a plain copy) into the screen's picture
        if (resolve_by_copy) {
            to(*main_rt, pl::RenderTextureLayout::RESOLVE_SOURCE, pl::RenderBarrierStage::COPY); to(*scene_tex, pl::RenderTextureLayout::RESOLVE_DEST, pl::RenderBarrierStage::COPY);      // (the resolve is a copy-stage command)
            cl.resolveTexture(scene_tex->tex.get(), main_rt->tex.get());
        } else {
            // [vkwin 2026-11-12] by a shader, the fraction of the four samples' average dropped as Direct3D 12's resolve does (arcade_view_resolve.vk.hlsl)
            to(*main_rt, pl::RenderTextureLayout::SHADER_READ); to(*scene_tex, pl::RenderTextureLayout::COLOR_WRITE);
            cl.setFramebuffer(fb_scene.get());
            cl.setViewports(pl::RenderViewport(0, 0, float(W_), float(H_))); cl.setScissors(pl::RenderRect(0, 0, W_, H_));
            cl.setPipeline(resolve_pipe());
            { const float rc[4] = { resolve_probe && (sub_n & 1) ? 1.0f : 0.0f, resolve_up, 0, 0 }; cl.setGraphicsPushConstants(0, rc, 0, sizeof rc); }
            cl.setGraphicsDescriptorSet(main_rt->set.get(), 1); cl.setGraphicsDescriptorSet(sampler_sets[8].get(), 2);
            cl.drawInstanced(3, 1, 0, 0);
        }
        to(*scene_tex, pl::RenderTextureLayout::SHADER_READ);
        const int crt_now = pic_crt.load();
        const bool post = crt_now != 0;
        auto last_step = [&](pl::RenderFramebuffer* fb) {
            cl.setFramebuffer(fb);
            cl.setViewports(pl::RenderViewport(0, 0, float(W_), float(H_))); cl.setScissors(pl::RenderRect(0, 0, W_, H_));
            cl.setPipeline(post_pipe(!post));
            if (post) {
                const float pc[12] = { vx / W_, vy / H_, vw / W_, vh / H_, float(W_), float(H_), 0, 0, float(crt_now), 0.0f, 200.0f / 10000.0f, 0 };      // (white at 200 nits, as in HDR: unused until HDR is)
                cl.setGraphicsPushConstants(0, pc, 0, sizeof pc);
            }
            cl.setGraphicsDescriptorSet(scene_tex->set.get(), 1); cl.setGraphicsDescriptorSet(sampler_sets[8].get(), 2);      // (smooth, clamped)
            cl.drawInstanced(3, 1, 0, 0);
        };
        const double el = now_s();
        // a picture to keep (QA): the last step again, into a picture of its own, and copied out of that
        const bool want_shot = !both && shots && !f.draws.empty() && (el > next_shot || burst_left > 0);
        // [vkwin 2026-11-12] WCW_NATIVE_GPU=both: every picture is read back into a ring of the last RING_N; when Direct3D 12's shot
        // NNNN.bmp turns up, the ring is kept (three pictures later) as vk\NNNN_k.bmp, k 0 the oldest: one of them is the same game frame.
        static const int RING_N = 8; static std::unique_ptr<pl::RenderBuffer> ring_rb[RING_N]; static int ring_at = 0, ring_have = 0, d3d_next = 0, dump_in = -1;
        if (both && shots && !f.draws.empty()) {
            if (!ring_rb[0]) for (auto& b : ring_rb) b = dev->createBuffer(pl::RenderBufferDesc::ReadbackBuffer(uint64_t(W_) * H_ * 4));
            to(*shot_tex, pl::RenderTextureLayout::COLOR_WRITE); last_step(fb_shot.get());
            to(*shot_tex, pl::RenderTextureLayout::COPY_SOURCE, pl::RenderBarrierStage::COPY);
            VkBufferImageCopy c{}; c.bufferRowLength = uint32_t(W_); c.bufferImageHeight = uint32_t(H_); c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; c.imageExtent = { uint32_t(W_), uint32_t(H_), 1 };
            vkCmdCopyImageToBuffer(static_cast<pl::VulkanCommandList*>(fr->cl.get())->vk, static_cast<pl::VulkanTexture*>(shot_tex->tex.get())->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<pl::VulkanBuffer*>(ring_rb[ring_at].get())->vk, 1, &c);
            ring_at = (ring_at + 1) % RING_N; if (ring_have < RING_N) ring_have++;
            if (dump_in < 0) { char d3[1024]; snprintf(d3, sizeof d3, "%s/%04d.bmp", shots, d3d_next); if (std::filesystem::exists(d3)) dump_in = 3; }
        }
        std::unique_ptr<pl::RenderBuffer> rb;
        if (want_shot) {
            to(*shot_tex, pl::RenderTextureLayout::COLOR_WRITE); last_step(fb_shot.get());
            to(*shot_tex, pl::RenderTextureLayout::COPY_SOURCE, pl::RenderBarrierStage::COPY);
            rb = dev->createBuffer(pl::RenderBufferDesc::ReadbackBuffer(uint64_t(W_) * H_ * 4));
            VkBufferImageCopy c{}; c.bufferRowLength = uint32_t(W_); c.bufferImageHeight = uint32_t(H_); c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; c.imageExtent = { uint32_t(W_), uint32_t(H_), 1 };
            vkCmdCopyImageToBuffer(static_cast<pl::VulkanCommandList*>(fr->cl.get())->vk, static_cast<pl::VulkanTexture*>(shot_tex->tex.get())->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<pl::VulkanBuffer*>(rb.get())->vk, 1, &c);
        }
        pl::RenderTexture* back = sc->getTexture(image);
        cl.barriers(pl::RenderBarrierStage::GRAPHICS, pl::RenderTextureBarrier(back, pl::RenderTextureLayout::COLOR_WRITE));
        last_step(fb_back[image].get());
        cl.barriers(pl::RenderBarrierStage::GRAPHICS, pl::RenderTextureBarrier(back, pl::RenderTextureLayout::PRESENT));      // (GRAPHICS, not NONE: plume keeps the stage to wait on at this image's next use, after the screen hands it back)
        cl.end();
        { const pl::RenderCommandList* l = fr->cl.get(); pl::RenderCommandSemaphore* ws = fr->acquired.get(); pl::RenderCommandSemaphore* ss = released[image].get();
          queue->executeCommandLists(&l, 1, &ws, 1, &ss, 1, fr->fence.get()); sub_n = F;
          const double c = now_s(); sum_ms += (c - a) * 1000.0; pictures++;
          sc->present(image, &ss, 1); }
        if (el > log_at && pictures) {
            size_t live = 0; for (const Tex& t : srvs) if (t) live++;
            fprintf(stderr, "[view] %.1f pictures a second from %.1f game updates a second, %.2f ms of work a picture, %zu triangles, %zu points, %zu matrices (%llu matched, %llu world, %llu direct so far), %zu textures live of %zu made, %ld MB; %llu cuts, %llu cameras lost, the camera's biggest smooth move %.1f, turn %.1f; worst matrix put back %.6f\n",
                    pictures / 5.0, updates / 5.0, sum_ms / pictures, f.idx.size() / 3, f.v.size(), f.xf.size(), (unsigned long long)xf_matched_.load(), (unsigned long long)xf_world_.load(), (unsigned long long)xf_direct_.load(), live, srvs.size(), rss_mb(), (unsigned long long)cuts_.load(), (unsigned long long)cam_lost_.load(), worst_move_.load(), worst_turn_.load(), worst_key_.load());
            fprintf(stderr, "[view] the Titantron: %llu changes between its live picture and the video in memory\n", (unsigned long long)tron_switches_);
            fprintf(stderr, "[view] depth (WCW_NATIVE_DEPTH %d): draws without %llu, solid %llu, see-through %llu (made: pulled %llu, not pulled -- another projection %llu)\n", world_depth_on(),
                    (unsigned long long)depth_draws_[0].exchange(0), (unsigned long long)depth_draws_[1].exchange(0), (unsigned long long)depth_draws_[3].exchange(0), (unsigned long long)see_pulled_.exchange(0), (unsigned long long)see_kept_.exchange(0));
            fprintf(stderr, "[view] Smooth Motion: updates %.1f ms apart on average, %.2f ms off it (rms), %llu late (25%% or more); %llu of %llu pictures held waiting for an update\n",
                    update_time * 1000.0, jitter_n_ ? std::sqrt(jitter_sum_ / double(jitter_n_)) * 1000.0 : 0.0, (unsigned long long)late_updates_, (unsigned long long)held_pictures_, (unsigned long long)pictures_seen_);
            { const uint64_t n = read_n_.exchange(0), ns = read_ns_.exchange(0);
              if (updates) fprintf(stderr, "[read] %.3f ms an update reading the lists (%llu lists)\n", double(ns) / 1e6 / updates, (unsigned long long)n); }
            fflush(stderr); log_at = el + 5.0; sum_ms = 0; pictures = 0; updates = 0;
        }
        if (both && dump_in >= 0 && dump_in-- == 0) {
            wait_upto(F);
            char dir[1024]; snprintf(dir, sizeof dir, "%s/vk", shots); std::filesystem::create_directories(dir);
            for (int j = 0; j < ring_have; j++) {
                const int slot = (ring_at - ring_have + j + RING_N) % RING_N;
                char pth[1024]; snprintf(pth, sizeof pth, "%s/%04d_%d.bmp", dir, d3d_next, j);
                if (void* mp = ring_rb[slot]->map()) { write_bmp(pth, static_cast<const uint8_t*>(mp), W_, H_, size_t(W_) * 4); ring_rb[slot]->unmap(); }
            }
            d3d_next++;
        }
        if (want_shot) {
            if (burst_left > 0) burst_left--; else { burst_left = burst_n; next_shot = el + (getenv("WCW_NATIVE_SHOT_EVERY") ? atof(getenv("WCW_NATIVE_SHOT_EVERY")) : 4.0); }
            wait_upto(F);
            char pth[1024]; snprintf(pth, sizeof pth, "%s/%04d.bmp", shots, shot_n++);
            if (void* mp = rb->map()) { write_bmp(pth, static_cast<const uint8_t*>(mp), W_, H_, size_t(W_) * 4); rb->unmap(); }
            if (burst_n) { fprintf(stderr, "[view] shot %d: %.3f of the way, the game's update %s\n", shot_n - 1, k, got ? "new" : "held"); fflush(stderr); }
        }
    }
}

} // namespace (this file's own)

// ---- the backend's doorway: what arcade_view.cpp calls ----
#ifdef ARCADE_VIEW_VULKAN_TOO
void vulkan_backend_run() { run(); }      // [vkwin 2026-11-12] (beside Direct3D 12 on Windows: arcade_view_gpu.h)
void vulkan_backend_status(char* out, size_t size) {
#else
void backend_run() { run(); }
void backend_status(char* out, size_t size) {
#endif
    if (!dev) { snprintf(out, size, "no device"); return; }
    snprintf(out, size, "Vulkan (plume), %zu textures", srvs.size());
}

} // namespace arcade_view
