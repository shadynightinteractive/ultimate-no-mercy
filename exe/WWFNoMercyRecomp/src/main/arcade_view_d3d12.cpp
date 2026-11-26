// [xplat 2026-11-12] The picture backend for Windows: Direct3D 12 (HLSL compiled at run time, a DXGI swap chain, HDR10, BMP shots).
// This is the half of arcade_view.cpp that used to follow the display-list reader, MOVED, NOT REWRITTEN: every line between
// "namespace arcade_view {" and "backend_run" below is the file's former lines 745-1449, byte for byte. (tools/split_view.py makes it
// from the base copy and checks that.) It talks to the reader through arcade_view_shared.h. Windows only: CMake builds it there.
// (2026-11-13, privacy: two log lines in it changed since -- "[view] Direct3D 12" without the graphics card's name, and a
// picture file named relative to the game folder -- so the byte-for-byte check now holds apart from those two lines.)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "arcade_host.h"   // (arcade::shown: paths for the log, privacy 2026-11-13)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxguids.h> // (MinGW: the D3D12 interfaces' IDs for __uuidof, as main.cpp)
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "ultramodern/ultramodern.hpp"
#include "arcade_view_shared.h"
#include "arcade_view_gpu.h"
#include "arcade_media.h"

#include <filesystem>
std::vector<std::filesystem::path> arcade_wrestlers_hd_pictures();     // (arcade_wrestlers.cpp)

namespace arcade_view {

static const char* HLSL = R"(
cbuffer DrawCBuf : register(b1) { float4 uvx; float4 vpx; float4 prim; float4 env; int4 cc[4]; float4 zx; };
cbuffer Blend : register(b0) { float blend; float2 shape_; float unused_; };
struct XfM { row_major float4x4 m; };
StructuredBuffer<XfM> xfm : register(t1);
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VI { float3 p : POSITION; float3 pp : TEXCOORD1; float2 st : TEXCOORD0; float4 c : COLOR0; uint xf : BLENDINDICES; };
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VO vs(VI v) { VO o; float4 q = mul(float4(lerp(v.pp, v.p, blend), 1), xfm[v.xf].m);
    if (zx.y > 0 && q.w > 0) { float s = min(zx.y * zx.w / q.w, 0.5); float n = q.z + q.w; s = n > 0 ? min(s, 0.9 * n / (n - zx.z)) : 0; q = q * (1 - s) + float4(0, 0, zx.z, 0) * s; }     // (a see-through draw pulled zx.y units toward the eye: zx.z, zx.w the projection's z term and w scale. Never past the near plane: n, how far in front of it the point is, keeps a tenth)
    q.x = q.x * vpx.x + q.w * vpx.z; q.y = q.y * vpx.y + q.w * vpx.w; q.z = (q.z + q.w) * 0.5; q.xy *= shape_; o.pos = q; o.uv = v.st * uvx.xy + uvx.zw; o.col = v.c; return o; }
float4 ps_screen(VO i) : SV_Target { return float4(tex.Sample(smp, i.uv).rgb, 1); }
float3 ca(int k, float3 c, float4 t, float4 s) { switch (k) { case 0: return c; case 1: case 2: return t.rgb; case 3: return prim.rgb; case 4: return s.rgb; case 5: return env.rgb; case 6: return float3(1,1,1); default: return float3(0,0,0);} }
float3 cc_(int k, float3 c, float a_, float4 t, float4 s) { switch (k) { case 0: return c; case 1: case 2: return t.rgb; case 3: return prim.rgb; case 4: return s.rgb; case 5: return env.rgb; case 7: return a_.xxx; case 8: case 9: return t.aaa; case 10: return prim.aaa; case 11: return s.aaa; case 12: return env.aaa; default: return float3(0,0,0);} }
float3 cd(int k, float3 c, float4 t, float4 s) { switch (k) { case 0: return c; case 1: case 2: return t.rgb; case 3: return prim.rgb; case 4: return s.rgb; case 5: return env.rgb; case 6: return float3(1,1,1); default: return float3(0,0,0);} }
float aa(int k, float a_, float4 t, float4 s) { switch (k) { case 0: return a_; case 1: case 2: return t.a; case 3: return prim.a; case 4: return s.a; case 5: return env.a; case 6: return 1; default: return 0;} }
float ac(int k, float4 t, float4 s) { switch (k) { case 1: case 2: return t.a; case 3: return prim.a; case 4: return s.a; case 5: return env.a; default: return 0;} }
float4 ps(VO i) : SV_Target {
    float4 t = tex.Sample(smp, i.uv), s = i.col;
    float3 c = float3(0,0,0); float a_ = 0;
    for (int n = 0; n < 2; n++) {
        int4 C = cc[n * 2], A = cc[n * 2 + 1];
        float3 c2 = (ca(C.x, c, t, s) - ca(C.y == 6 ? 8 : C.y, c, t, s)) * cc_(C.z, c, a_, t, s) + cd(C.w, c, t, s);
        float a2 = (aa(A.x, a_, t, s) - aa(A.y, a_, t, s)) * ac(A.z, t, s) + aa(A.w, a_, t, s);
        c = c2; a_ = a2;
    }
    if (a_ < 0.02 || a_ < zx.x) discard;
    return float4(saturate(c), saturate(a_));
}
float4 ps_shape(VO i) : SV_Target { ps(i); return float4(0, 0, 0, 1); }     // (a sign's shape: written, full, where its picture isn't see-through)
)";
// the picture's last step, when it has one: our CRT, and HDR10. (Each its own: either can be on without the other)
static const char* HLSL_POST = R"(
cbuffer Post : register(b1) { float4 region; float4 size; float4 mode; };
Texture2D scene : register(t0); SamplerState smp : register(s0);
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs(uint id : SV_VertexID) { VO o; o.uv = float2((id << 1) & 2, id & 2); o.pos = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1); return o; }
float3 pq(float3 l) {    // SMPTE ST 2084: l in 0..1 of 10,000 nits
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 p = pow(max(l, 0), m1);
    return pow((c1 + c2 * p) / (1 + c3 * p), m2);
}
float4 ps(VO i) : SV_Target {
    float2 uv = i.uv;
    const int crt = int(mode.x);
    // where in the game's picture this is (0..1 across it)
    float2 r = (uv - region.xy) / region.zw;
    if (crt >= 2) {                  // the tube's curve
        float2 c = r * 2 - 1; c *= 1 + dot(c, c) * float2(0.018, 0.028); r = c * 0.5 + 0.5;
        if (r.x < 0 || r.x > 1 || r.y < 0 || r.y > 1) return mode.y > 0.5 ? float4(pq(float3(0, 0, 0)), 1) : float4(0, 0, 0, 1);
        uv = r * region.zw + region.xy;
    }
    float3 c = pow(scene.Sample(smp, uv).rgb, 2.2);
    if (crt >= 1) {                  // the beam: the game's 240 lines, a bright line wider than a dark one
        const float f = frac(r.y * 240) - 0.5, l = dot(c, float3(0.2126, 0.7152, 0.0722)), w = lerp(0.22, 0.42, sqrt(l));
        c *= exp(-f * f / (2 * w * w)) * 1.55;
    }
    if (crt >= 2) {                  // the mask (a triad of stripes, about a CRT's width at any size) and the tube's darker corners
        const float t = max(1.0, round(size.y / 720.0));
        const int k = int(fmod(floor(i.pos.x / t), 3.0));
        float3 m = float3(0.82, 0.82, 0.82); if (k == 0) m.r = 1.18; else if (k == 1) m.g = 1.18; else m.b = 1.18;
        c *= m;
        const float2 v = r * 2 - 1; c *= 1 - 0.18 * pow(dot(v, v) * 0.5, 2);
    }
    if (mode.y > 0.5) {              // HDR10: Rec. 709 to Rec. 2020, white at the paper's level, ST 2084
        const float3 b = float3(dot(c, float3(0.6274, 0.3293, 0.0433)), dot(c, float3(0.0691, 0.9195, 0.0114)), dot(c, float3(0.0164, 0.0880, 0.8956)));
        return float4(pq(b * mode.z), 1);
    }
    return float4(pow(saturate(c), 1 / 2.2), 1);
}
)";
// ---- Direct3D 12 -----------------------------------------------------------------------------------------------------
static const UINT FRAMES = 3;
static ID3D12Device* dev; static ID3D12CommandQueue* queue; static IDXGISwapChain3* sc; static ID3D12GraphicsCommandList* cl; static ID3D12Fence* fence;
static HANDLE fence_ev, latency_ev; static uint64_t fence_val; static bool tearing;
static ID3D12DescriptorHeap* heap_srv, * heap_keep, * heap_smp, * heap_rtv, * heap_dsv; static UINT inc_srv, inc_smp, inc_rtv, inc_dsv, srv_cap = 1u << 16;     // (heap_keep: the pictures' table as the CPU keeps it, heap_srv the copy the shaders see: it can grow)
static ID3D12RootSignature* root; static ID3DBlob* b_vs, * b_ps, * b_screen, * b_shape, * b_post_vs, * b_post_ps;
static std::unordered_map<int, ID3D12PipelineState*> pipes;
struct Tex { ID3D12Resource* res = nullptr; u32 srv = 0; };
struct FrameRes { ID3D12CommandAllocator* alloc = nullptr; ID3D12Resource* up = nullptr; u8* up_p = nullptr; UINT64 up_cap = 0, up_off = 0; D3D12_GPU_VIRTUAL_ADDRESS up_va = 0; uint64_t done = 0; };
static FrameRes frs[FRAMES]; static FrameRes* fr;
static std::vector<Tex> srvs; static Tex white, video_tex, tron_tex, shape_tex, scene_tex;
static Tex native_tex; static u32 native_w, native_h; static uint64_t native_serial; static bool native_has;     // (2026-11-10: texture -4, a native Titantron video's picture: its size, which picture it holds, and whether it holds this video's at all)
static DXGI_FORMAT back_format = DXGI_FORMAT_R8G8B8A8_UNORM; static bool hdr_on;
static float cur_xs_ = 1.0f, cur_ys_ = 1.0f;
static ID3D12Resource* backs[FRAMES], * main_rt, * main_ds, * tron_ds, * vb, * ib; static UINT64 vb_cap, ib_cap;
static D3D12_RESOURCE_STATES shape_state, tron_state;
static int W_ = 1280, H_ = 960; static const int TW = 512, TH = 256; static const DXGI_FORMAT PIC = DXGI_FORMAT_R8G8B8A8_UNORM;
static u32 next_srv_ = 8; static std::vector<u32> free_srv_;      // (0..7: not used)
struct Later { uint64_t after; IUnknown* res; int slot; };
static std::vector<Later> later_;

// (this compiler's form of these two calls: the handle's handed back through a pointer)
static D3D12_CPU_DESCRIPTOR_HANDLE cpu_of(ID3D12DescriptorHeap* h, UINT inc, UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE c; h->GetCPUDescriptorHandleForHeapStart(&c); c.ptr += SIZE_T(inc) * i; return c; }
static D3D12_GPU_DESCRIPTOR_HANDLE gpu_of(ID3D12DescriptorHeap* h, UINT inc, UINT i) { D3D12_GPU_DESCRIPTOR_HANDLE c; h->GetGPUDescriptorHandleForHeapStart(&c); c.ptr += UINT64(inc) * i; return c; }
static void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    if (a == b) return;
    D3D12_RESOURCE_BARRIER q{}; q.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; q.Transition.pResource = r; q.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; q.Transition.StateBefore = a; q.Transition.StateAfter = b;
    cl->ResourceBarrier(1, &q);
}
static ID3D12Resource* make_buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES st) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = type;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = nullptr; dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, IID_PPV_ARGS(&r));
    return r;
}
// what the GPU may still be reading: let go once it's finished with the frame being built
static void release_later(IUnknown* r, int slot = -1) { later_.push_back({ fence_val + 1, r, slot }); }
static void collect_later() {
    const uint64_t done = fence->GetCompletedValue();
    size_t n = 0;
    for (size_t i = 0; i < later_.size(); i++) {
        if (later_[i].after <= done) { if (later_[i].res) later_[i].res->Release(); if (later_[i].slot >= 0) free_srv_.push_back(u32(later_[i].slot)); }
        else later_[n++] = later_[i];
    }
    later_.resize(n);
}
static void wait_for(uint64_t v) { if (v && fence->GetCompletedValue() < v) { fence->SetEventOnCompletion(v, fence_ev); WaitForSingleObject(fence_ev, INFINITE); } }
static void* upload(UINT64 n, UINT64 align, D3D12_GPU_VIRTUAL_ADDRESS* va, ID3D12Resource** res = nullptr, UINT64* off = nullptr) {
    UINT64 at = (fr->up_off + align - 1) & ~(align - 1);
    if (!fr->up || at + n > fr->up_cap) {
        if (fr->up) release_later(fr->up);
        fr->up_cap = std::max<UINT64>(fr->up_cap * 2, std::max<UINT64>(n * 2, UINT64(32) << 20));
        fr->up = make_buffer(fr->up_cap, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_RANGE none{ 0, 0 }; fr->up->Map(0, &none, reinterpret_cast<void**>(&fr->up_p)); fr->up_va = fr->up->GetGPUVirtualAddress();
        at = 0;
    }
    fr->up_off = at + n;
    if (va) *va = fr->up_va + at;
    if (res) *res = fr->up;
    if (off) *off = at;
    return fr->up_p + at;
}
static u32 new_srv() { if (!free_srv_.empty()) { const u32 v = free_srv_.back(); free_srv_.pop_back(); return v; } return next_srv_++; }
static void view_of(Tex& t) {
    D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = PIC; v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels = UINT(-1);
    t.srv = new_srv(); dev->CreateShaderResourceView(t.res, &v, cpu_of(heap_keep, inc_srv, t.srv));
    dev->CopyDescriptorsSimple(1, cpu_of(heap_srv, inc_srv, t.srv), cpu_of(heap_keep, inc_srv, t.srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
static ID3D12DescriptorHeap* make_heap(D3D12_DESCRIPTOR_HEAP_TYPE t, UINT n, bool vis) { D3D12_DESCRIPTOR_HEAP_DESC d{}; d.Type = t; d.NumDescriptors = n; d.Flags = vis ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE; ID3D12DescriptorHeap* h = nullptr; dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&h)); return h; }
// room for this many more pictures: the table made bigger when it has to be. (Between frames: nothing's using it)
static void srv_room(size_t more) {
    if (next_srv_ + more <= srv_cap) return;
    wait_for(fence_val);
    UINT cap = srv_cap; while (next_srv_ + more > cap) cap *= 2;
    auto* keep = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, cap, false), * vis = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, cap, true);
    if (!keep || !vis) { fprintf(stderr, "[view] the pictures' table can't grow to %u\n", cap); fflush(stderr); return; }
    dev->CopyDescriptorsSimple(next_srv_, cpu_of(keep, inc_srv, 0), cpu_of(heap_keep, inc_srv, 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev->CopyDescriptorsSimple(next_srv_, cpu_of(vis, inc_srv, 0), cpu_of(heap_keep, inc_srv, 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    heap_keep->Release(); heap_srv->Release(); heap_keep = keep; heap_srv = vis; srv_cap = cap;
    fprintf(stderr, "[view] the pictures' table grown to %u\n", cap); fflush(stderr);
}
// a picture, with its smaller versions made (Direct3D 12 has no call for that: each the last averaged), copied up in the frame's list
static Tex make_tex(u32 w, u32 h, const void* rgba) {
    Tex t;
    UINT mips = 1; for (u32 m = std::max(w, h); m > 1; m >>= 1) mips++;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = UINT16(mips); d.Format = PIC; d.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.res)))) return Tex{};
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(mips); UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, mips, 0, fp.data(), nullptr, nullptr, &total);
    ID3D12Resource* up = nullptr; UINT64 base = 0;
    u8* dst = static_cast<u8*>(upload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, nullptr, &up, &base));
    std::vector<u8> a(static_cast<const u8*>(rgba), static_cast<const u8*>(rgba) + size_t(w) * h * 4), b;
    u32 mw = w, mh = h;
    for (UINT m = 0; m < mips; m++) {
        u8* to = dst + fp[m].Offset;
        for (u32 y = 0; y < mh; y++) std::memcpy(to + size_t(y) * fp[m].Footprint.RowPitch, a.data() + size_t(y) * mw * 4, size_t(mw) * 4);
        D3D12_TEXTURE_COPY_LOCATION tl{}; tl.pResource = t.res; tl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; tl.SubresourceIndex = m;
        D3D12_TEXTURE_COPY_LOCATION fl{}; fl.pResource = up; fl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; fl.PlacedFootprint = fp[m]; fl.PlacedFootprint.Offset += base;
        cl->CopyTextureRegion(&tl, 0, 0, 0, &fl, nullptr);
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
    barrier(t.res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    view_of(t);
    return t;
}
// (no2k15, 2026-11-23) The two built-in full-detail men -- the WWE 2K15 test slots, whose pictures were native_view.pak
// (WCW_NATIVE_PAK) -- are removed: the full-detail men are the wrestler folders' own now, man 0 the first folder's.
// The full-detail men from wrestler folders: their pictures.pak files (six pictures each), man 0 the first.
static void load_hd_folders() {
    srvs.assign(HD_MEN * 6, Tex{});
    int man = 0;
    for (const auto& path : arcade_wrestlers_hd_pictures()) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (f) {
            char m[4]; u32 n = 0; fread(m, 1, 4, f); fread(&n, 4, 1, f);
            std::vector<u8> b;
            for (u32 i = 0; i < n && i < 6; i++) {
                u32 w, h, len; fread(&w, 4, 1, f); fread(&h, 4, 1, f); fread(&len, 4, 1, f);
                b.resize(len); fread(b.data(), 1, len, f);
                srvs[size_t(man) * 6 + i] = make_tex(w, h, b.data());
            }
            fclose(f);
        }
        else fprintf(stderr, "[view] %s can't be read: his pictures are missing\n", arcade::shown(path).c_str());
        man++;
    }
}
// target: 0 the frame, 1 the Titantron, 2 a sign's shape. shader: 0 the combiner, 1 the screen's picture, 2 the shape. blend: 0 over, 1 written, 2 colour only
static ID3D12PipelineState* pipe(int target, int shader, int zmode, int cull, int blend) {     // (cull: 0 none, 1 back, 2 front)
    const int key = target | shader << 2 | zmode << 4 | cull << 8 | blend << 10;
    auto it = pipes.find(key);
    if (it != pipes.end()) return it->second;
    static const D3D12_INPUT_ELEMENT_DESC ied[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root; d.VS = { b_vs->GetBufferPointer(), b_vs->GetBufferSize() };
    ID3DBlob* pb = shader == 0 ? b_ps : shader == 1 ? b_screen : b_shape; d.PS = { pb->GetBufferPointer(), pb->GetBufferSize() };
    d.InputLayout = { ied, 5 }; d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.SampleMask = UINT_MAX; d.NumRenderTargets = 1; d.RTVFormats[0] = PIC;
    d.SampleDesc.Count = target == 0 ? 4 : 1;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = cull == 1 ? D3D12_CULL_MODE_BACK : cull == 2 ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE; d.RasterizerState.FrontCounterClockwise = TRUE; d.RasterizerState.DepthClipEnable = TRUE; d.RasterizerState.MultisampleEnable = target == 0;
    auto& t = d.BlendState.RenderTarget[0]; t.RenderTargetWriteMask = blend == 2 ? (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE) : D3D12_COLOR_WRITE_ENABLE_ALL; t.LogicOp = D3D12_LOGIC_OP_NOOP;
    t.BlendEnable = blend == 0; t.SrcBlend = D3D12_BLEND_SRC_ALPHA; t.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D12_BLEND_OP_ADD; t.SrcBlendAlpha = D3D12_BLEND_ONE; t.DestBlendAlpha = D3D12_BLEND_ZERO; t.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.DSVFormat = target == 2 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_D32_FLOAT;
    if (target != 2 && (zmode & 3)) {
        d.DepthStencilState.DepthEnable = TRUE; d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        d.DepthStencilState.DepthWriteMask = (zmode & 3) == 3 ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
        // (a later layer, toward the camera: 16 of the depth value's least steps at its depth per level -- a float depth's step grows
        //  with the value, so this stays a hair at every distance -- and as much again per unit of slope, for a floor seen flat)
        const int lv = zmode >> 2;
        if (lv) { d.RasterizerState.DepthBias = -16 * lv; d.RasterizerState.SlopeScaledDepthBias = -1.0f * float(lv); }
    }
    ID3D12PipelineState* ps = nullptr;
    const HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&ps));
    if (FAILED(hr)) { fprintf(stderr, "[view] a pipeline failed: %08lX (target %d shader %d)\n", hr, target, shader); fflush(stderr); }
    pipes[key] = ps;
    return ps;
}
static bool use_video_;
static uint64_t tron_switches_;
// ---- [visual 2026-11-19] the picture trace (arcade_view_shared.h trace_out): a row a picture, written when the graphics card has
// finished it (the shadows' parts' samples that passed the depth test come back then: an occlusion query around each of their draws)
static const UINT TQ_MAX = 512;
static ID3D12QueryHeap* tq_heap; static ID3D12Resource* tq_read; static bool tq_on_; static UINT tq_base_, tq_n_;
struct TraceRow { bool on = false; uint64_t pic = 0, serial = 0, vi = 0, vi_prev = 0, vi_hand = 0; double t = 0, t_begin = 0, t_begin_prev = 0, t_hand = 0, t_take = 0, t_present = 0, upd_ms = 0; float k = 0;
                  int got = 0, snap = 0, interp = 0, vsync = 0, moving = 0, tron = 0, swaps = 0; uint64_t fresh_n = 0, fresh_px = 0; double t_up = 0, t_rec = 0; uint32_t ld[5] = {}; uint64_t w_direct = 0, w_blended = 0, scr_tris = 0, cam_tris = 0, sh_tris = 0, sh_k[3] = { 0, 0, 0 }; UINT qbase = 0, qn = 0; };
static TraceRow trace_rows_[3];
// (WCW_VIEW_TRACE_SHADOWSHOTS=n: every picture copied back, and kept as a BMP when its shadows' samples fell under a fifth of the
//  recent pictures' -- n of them -- and the first picture after each that had them back: the dropout and the shadow's return)
static ID3D12Resource* sh_rb_[3]; static D3D12_PLACED_SUBRESOURCE_FOOTPRINT sh_fp_[3]; static bool sh_copied_[3];
static void write_bmp(const char* pth, const void* mp, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& rfp) {
    FILE* o = fopen(pth, "wb");
    if (!o) return;
    const int w = int(rfp.Footprint.Width), h = int(rfp.Footprint.Height), sz = 54 + w * h * 3;
    u8 hdr[54] = { 'B', 'M' }; std::memcpy(hdr + 2, &sz, 4); hdr[10] = 54; hdr[14] = 40; std::memcpy(hdr + 18, &w, 4); std::memcpy(hdr + 22, &h, 4); hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, o);
    std::vector<u8> row((size_t(w) * 3 + 3) & ~size_t(3));
    for (int y = h - 1; y >= 0; y--) { const u8* q = static_cast<const u8*>(mp) + size_t(y) * rfp.Footprint.RowPitch; for (int x = 0; x < w; x++) { row[size_t(x) * 3] = q[x * 4 + 2]; row[size_t(x) * 3 + 1] = q[x * 4 + 1]; row[size_t(x) * 3 + 2] = q[x * 4]; } fwrite(row.data(), 1, row.size(), o); }
    fclose(o);
}
static void shadow_shot(int slot, uint64_t pic, long long samples) {
    static int left = env_int("WCW_VIEW_TRACE_SHADOWSHOTS", 0); static std::vector<long long> recent; static bool after = false;
    if (left <= 0 || samples < 0 || !sh_copied_[slot]) return;
    std::vector<long long> v = recent; std::sort(v.begin(), v.end());
    const long long med = v.size() >= 8 ? v[v.size() / 2] : 0;
    const char* kind = nullptr;
    if (med > 20000 && samples * 5 < med) { kind = "shadow_gone"; after = true; left--; }
    else if (after && med > 20000 && samples * 5 >= med * 4) { kind = "shadow_back"; after = false; }
    if (samples * 5 >= med) { recent.push_back(samples); if (recent.size() > 15) recent.erase(recent.begin()); }
    if (!kind) return;
    char pth[MAX_PATH]; snprintf(pth, MAX_PATH, "%s.%s_%06llu.bmp", getenv("WCW_VIEW_TRACE"), kind, (unsigned long long)pic);
    void* mp = nullptr; if (SUCCEEDED(sh_rb_[slot]->Map(0, nullptr, &mp))) { write_bmp(pth, mp, sh_fp_[slot]); sh_rb_[slot]->Unmap(0, nullptr); }
}
static bool frame_cam_, tron_has_;      // [visual 2026-11-19] (the frame in hand brought the Titantron's camera pass; tron_tex holds a camera picture)
static void trace_flush(TraceRow& r) {
    if (!r.on) return;
    r.on = false;
    long long samples = -1;
    if (r.qn && tq_read) {
        void* mp = nullptr; D3D12_RANGE rr{ SIZE_T(r.qbase) * 8u, SIZE_T(r.qbase + r.qn) * 8u };
        if (SUCCEEDED(tq_read->Map(0, &rr, &mp))) { samples = 0; const uint64_t* q = static_cast<const uint64_t*>(mp); for (UINT i = 0; i < r.qn; i++) samples += (long long)q[r.qbase + i]; D3D12_RANGE none{ 0, 0 }; tq_read->Unmap(0, &none); }
    }
    shadow_shot(int(&r - trace_rows_), r.pic, samples);
    {   // (WCW_VIEW_TRACE_WINDOW=<from ms>,<to ms>: every 4th picture between, kept: one moment of two runs compared)
        static double w0 = -1, w1 = -1; static bool read_ = false;
        if (!read_) { read_ = true; if (const char* e = getenv("WCW_VIEW_TRACE_WINDOW")) sscanf(e, "%lf,%lf", &w0, &w1); }
        const int sl = int(&r - trace_rows_);
        if (w0 >= 0 && r.t >= w0 && r.t <= w1 && r.pic % 4u == 0u && sh_copied_[sl]) {
            char pth[MAX_PATH]; snprintf(pth, MAX_PATH, "%s.at_%06llu.bmp", getenv("WCW_VIEW_TRACE"), (unsigned long long)r.pic);
            void* mp = nullptr; if (SUCCEEDED(sh_rb_[sl]->Map(0, nullptr, &mp))) { write_bmp(pth, mp, sh_fp_[sl]); sh_rb_[sl]->Unmap(0, nullptr); }
        }
    }
    fprintf(trace_out(), "P,%llu,%.3f,%d,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.4f,%d,%d,%d,%.3f,%d,%llu,%llu,%d,%llu,%llu,%llu,%llu,%lld,%.3f,%llu,%llu,%d,%llu,%llu,%.3f,%.3f,%u,%u,%u,%u,%u\n",
        (unsigned long long)r.pic, r.t, r.got, (unsigned long long)r.serial, (unsigned long long)r.vi, (unsigned long long)r.vi_prev, (unsigned long long)r.vi_hand, r.t_begin, r.t_begin_prev, r.t_hand, r.t_take, r.k,
        r.snap, r.interp, r.vsync, r.upd_ms, r.moving, (unsigned long long)r.scr_tris, (unsigned long long)r.cam_tris, r.tron, (unsigned long long)r.sh_tris, (unsigned long long)r.sh_k[0], (unsigned long long)r.sh_k[1], (unsigned long long)r.sh_k[2], samples, r.t_present, (unsigned long long)r.w_direct, (unsigned long long)r.w_blended, r.swaps, (unsigned long long)r.fresh_n, (unsigned long long)(r.fresh_px / 1000u), r.t_up, r.t_rec, r.ld[0], r.ld[1], r.ld[2], r.ld[3], r.ld[4]);
}
static double jitter_sum_; static uint64_t jitter_n_, late_updates_, held_pictures_, pictures_seen_;   // (Smooth Motion's timing: the [view] line)     // (the Titantron between its live picture and the video in memory: how often, the [view] line)
static void draw_all(const Frame& f, int target, float ox, float oy, float tw, float th) {
    ID3D12PipelineState* last = nullptr;
    for (const Draw& d : f.draws) {
        if (!d.count || d.target != target) continue;
        u32 sv = white.srv;
        if (d.tex == -1) { if (target == 1) continue; sv = use_video_ ? video_tex.srv : tron_tex.srv; }
        else if (d.tex == -3) { if (target == 1) continue; sv = shape_tex.srv; }
        else if (d.tex == -4) { if (!native_has) continue; sv = native_tex.srv; }      // (a native video; no picture of it decoded yet: the screen's part stays black)
        else if (d.tex >= 0 && d.tex < int(srvs.size()) && srvs[d.tex].res) sv = srvs[d.tex].srv;
        const bool scr = d.tex == -1;
        // (a sign's shape: written to its alpha; the picture over it keeps that alpha)
        ID3D12PipelineState* ps = target == 2 ? pipe(2, scr ? 1 : 2, 0, 0, scr ? 2 : 1) : pipe(target, scr ? 1 : 0, d.zmode, d.cull, 0); depth_draws_[d.zmode & 3]++;
        if (!ps) continue;
        if (ps != last) { cl->SetPipelineState(ps); last = ps; }
        D3D12_GPU_VIRTUAL_ADDRESS va; std::memcpy(upload(sizeof(DrawCB), 256, &va), &d.cb, sizeof d.cb);
        cl->SetGraphicsRootConstantBufferView(1, va);
        cl->SetGraphicsRootDescriptorTable(3, gpu_of(heap_srv, inc_srv, sv));
        cl->SetGraphicsRootDescriptorTable(4, gpu_of(heap_smp, inc_smp, (d.point ? 9u : 0u) + (d.cms == 1 ? 1u : d.cms >= 2 ? 2u : 0u) * 3u + (d.cmt == 1 ? 1u : d.cmt >= 2 ? 2u : 0u)));
        const bool tq = tq_on_ && target == 0 && tq_n_ < TQ_MAX && trace_shadow(d);     // [visual 2026-11-19] (the trace)
        // (all of the game's picture: all of this one's, whatever its shape. Part of it: the same part, in the middle)
        const float xs = target == 0 ? cur_xs_ : 1.0f, ys = target == 0 ? cur_ys_ : 1.0f, cx = ox + tw * 0.5f, cy = oy + th * 0.5f;
        const bool wx = d.sc[0] <= 0.0f && d.sc[2] >= 1.0f, wy = d.sc[1] <= 0.0f && d.sc[3] >= 1.0f;
        D3D12_RECT r{ LONG(wx ? ox : cx + (d.sc[0] * 2 - 1) * tw * 0.5f * xs), LONG(wy ? oy : cy + (d.sc[1] * 2 - 1) * th * 0.5f * ys), LONG((wx ? ox + tw : cx + (d.sc[2] * 2 - 1) * tw * 0.5f * xs) + 0.99f), LONG((wy ? oy + th : cy + (d.sc[3] * 2 - 1) * th * 0.5f * ys) + 0.99f) };
        cl->RSSetScissorRects(1, &r);
        if (tq) cl->BeginQuery(tq_heap, D3D12_QUERY_TYPE_OCCLUSION, tq_base_ + tq_n_);
        cl->DrawIndexedInstanced(d.count, 1, d.start, 0, 0);
        if (tq) cl->EndQuery(tq_heap, D3D12_QUERY_TYPE_OCCLUSION, tq_base_ + tq_n_++);
    }
}
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_CLOSE) { ShowWindow(h, SW_HIDE); return 0; } return DefWindowProcA(h, m, w, l); }
static BOOL CALLBACK find_main(HWND h, LPARAM out) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    char cls[64] = {}; GetClassNameA(h, cls, 63);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && std::strcmp(cls, "NoMercyNativeView") && !std::strcmp(cls, "SDL_app")) { *reinterpret_cast<HWND*>(out) = h; return FALSE; }
    return TRUE;
}
static ID3D12Resource* make_target(int w, int h, int samples, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st, const float* cc) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = UINT64(w); d.Height = UINT(h); d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = fmt; d.SampleDesc.Count = UINT(samples); d.Flags = fl;
    D3D12_CLEAR_VALUE c{}; c.Format = fmt;
    if (fl & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) c.DepthStencil.Depth = 1.0f; else std::memcpy(c.Color, cc, 16);
    ID3D12Resource* r = nullptr; dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, &c, IID_PPV_ARGS(&r));
    return r;
}
static const float BLACK[4] = { 0, 0, 0, 1 }, NOTHING[4] = { 0, 0, 0, 0 };
// the things the window's size decides. (RTV: 0 the frame, 1 the Titantron, 2 the shape. DSV: 0 the frame, 1 the Titantron)
static void make_targets() {
    for (UINT i = 0; i < FRAMES; i++) { sc->GetBuffer(i, IID_PPV_ARGS(&backs[i])); dev->CreateRenderTargetView(backs[i], nullptr, cpu_of(heap_rtv, inc_rtv, 3 + i)); }
    scene_tex.res = make_target(W_, H_, 1, PIC, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, BLACK); view_of(scene_tex);
    main_rt = make_target(W_, H_, 4, PIC, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, BLACK);
    main_ds = make_target(W_, H_, 4, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr);
    dev->CreateRenderTargetView(main_rt, nullptr, cpu_of(heap_rtv, inc_rtv, 0));
    dev->CreateDepthStencilView(main_ds, nullptr, cpu_of(heap_dsv, inc_dsv, 0));
}
static void drop_targets() {
    for (auto*& b : backs) { b->Release(); b = nullptr; }
    main_rt->Release(); main_ds->Release(); scene_tex.res->Release(); free_srv_.push_back(scene_tex.srv); scene_tex = Tex{};
}
static ID3D12PipelineState* post_pipe() {
    const int key = 0x10000 | int(back_format);
    auto it = pipes.find(key);
    if (it != pipes.end()) return it->second;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root; d.VS = { b_post_vs->GetBufferPointer(), b_post_vs->GetBufferSize() }; d.PS = { b_post_ps->GetBufferPointer(), b_post_ps->GetBufferSize() };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.SampleMask = UINT_MAX; d.NumRenderTargets = 1; d.RTVFormats[0] = back_format; d.SampleDesc.Count = 1;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; d.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    ID3D12PipelineState* ps = nullptr; const HRESULT hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&ps));
    if (FAILED(hr)) { fprintf(stderr, "[view] the last step's pipeline failed: %08lX\n", hr); fflush(stderr); }
    pipes[key] = ps;
    return ps;
}
// the graphics card's gone (a driver reset, a hang): say so and quit properly, so the saves flush; nobody may be there to close the message
[[noreturn]] static void device_lost(HRESULT why) {
    fprintf(stderr, "[view] the graphics card was lost: %08lX, %08lX\n", why, dev ? dev->GetDeviceRemovedReason() : 0); fflush(stderr);
    std::thread([] { MessageBoxA(nullptr, "The graphics card stopped responding (a driver reset or crash).\n\nThe game will now close. Your saves are kept.", "No Mercy", MB_OK | MB_ICONERROR); }).detach();
    Sleep(15000);
    ultramodern::quit();
    for (;;) Sleep(1000);
}
static void log_debug() {
    static ID3D12InfoQueue* iq; static bool tried;
    if (!tried) { tried = true; if (getenv("WCW_NATIVE_D3DDEBUG")) { dev->QueryInterface(IID_PPV_ARGS(&iq)); fprintf(stderr, "[d3d12] the debug layer is %s\n", iq ? "on" : "NOT on: nothing's being checked"); fflush(stderr); } }
    if (!iq) return;
    const UINT64 n = iq->GetNumStoredMessages();
    for (UINT64 i = 0; i < n && i < 16; i++) { SIZE_T len = 0; iq->GetMessage(i, nullptr, &len); std::vector<char> b(len); auto* m = reinterpret_cast<D3D12_MESSAGE*>(b.data()); iq->GetMessage(i, m, &len); static std::unordered_map<int, int> seen; if (m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING && seen[int(m->ID)]++ == 0) { fprintf(stderr, "[d3d12] %s\n", m->pDescription); fflush(stderr); } }
    iq->ClearStoredMessages();
}
static void run() {
    WNDCLASSA wc{}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "NoMercyNativeView"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassA(&wc);
    RECT rc{ 0, 0, W_, H_ }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = nullptr, parent = nullptr;
    if (mode_ >= 2) {
        for (int i = 0; i < 200 && !parent; i++) { EnumWindows(find_main, reinterpret_cast<LPARAM>(&parent)); if (!parent) Sleep(50); }
        if (parent) { RECT c; GetClientRect(parent, &c); W_ = std::max<int>(c.right, 64); H_ = std::max<int>(c.bottom, 64); }
        if (parent && mode_ == 3) { hwnd = parent; fprintf(stderr, "[view] the game's renderer: in its window, %d x %d; the N64 renderer not started\n", W_, H_); fflush(stderr); }
        else if (parent) {
            hwnd = CreateWindowExA(WS_EX_NOACTIVATE, "NoMercyNativeView", "", WS_CHILD | WS_DISABLED, 0, 0, W_, H_, parent, nullptr, wc.hInstance, nullptr);    // (no input of its own: the game's window keeps the pads and keys)
            fprintf(stderr, "[view] native only: inside the game's window, %d x %d\n", W_, H_); fflush(stderr);
        }
    }
    if (!hwnd) hwnd = CreateWindowA("NoMercyNativeView", "No Mercy: the real game, drawn natively", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);

    IDXGIFactory6* fac = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) { fprintf(stderr, "[view] no DXGI factory\n"); return; }
    { IDXGIAdapter1* ad = nullptr;      // (the strongest graphics card there is)
      if (SUCCEEDED(fac->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&ad)))) { fprintf(stderr, "[view] Direct3D 12\n"); fflush(stderr); }   // (privacy 2026-11-13: not the graphics card's name)
      if (FAILED(D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) { fprintf(stderr, "[view] no Direct3D 12 device\n"); fflush(stderr); return; }
      if (ad) ad->Release(); }
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)); fence_ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    BOOL allow = FALSE; fac->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow); tearing = allow != FALSE;
    DXGI_SWAP_CHAIN_DESC1 scd{}; scd.Width = W_; scd.Height = H_; scd.Format = PIC; scd.SampleDesc.Count = 1; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.BufferCount = FRAMES; scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const UINT sc_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u); scd.Flags = sc_flags;
    IDXGISwapChain1* sc1 = nullptr;
    const HRESULT shr = fac->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr, nullptr, &sc1);
    if (FAILED(shr)) { fprintf(stderr, "[view] no swap chain: %08lX\n", shr); fflush(stderr); return; }
    sc1->QueryInterface(IID_PPV_ARGS(&sc)); sc1->Release();
    fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc->SetMaximumFrameLatency(1); latency_ev = sc->GetFrameLatencyWaitableObject();     // (the least delay there can be between a picture worked out and shown)
    heap_srv = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srv_cap, true); heap_keep = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srv_cap, false); heap_smp = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 18, true); heap_rtv = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 + FRAMES, false); heap_dsv = make_heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
    inc_srv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); inc_smp = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER); inc_rtv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); inc_dsv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (auto& r : frs) dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&r.alloc));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frs[0].alloc, nullptr, IID_PPV_ARGS(&cl));
    fr = &frs[0];
    // what the shaders are given: the blend, a draw's constants, the matrices, a picture, how to read it
    { D3D12_DESCRIPTOR_RANGE rs{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }, rm{ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, 0 };
      D3D12_ROOT_PARAMETER p[5] = {};
      p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.ShaderRegister = 0; p[0].Constants.Num32BitValues = 4; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
      p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[1].Descriptor.ShaderRegister = 1; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[2].Descriptor.ShaderRegister = 1; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
      p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable = { 1, &rs }; p[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[4].DescriptorTable = { 1, &rm }; p[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 5; rd.pParameters = p; rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
      ID3DBlob* sb = nullptr, * se = nullptr;
      if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sb, &se))) { fprintf(stderr, "[view] root signature: %s\n", se ? (const char*)se->GetBufferPointer() : "?"); fflush(stderr); return; }
      dev->CreateRootSignature(0, sb->GetBufferPointer(), sb->GetBufferSize(), IID_PPV_ARGS(&root)); }
    auto comp = [&](const char* en, const char* tg, ID3DBlob** out) { ID3DBlob* e = nullptr; if (FAILED(D3DCompile(HLSL, strlen(HLSL), "view", nullptr, nullptr, en, tg, 0, 0, out, &e))) { fprintf(stderr, "[view] shader: %s\n", e ? (const char*)e->GetBufferPointer() : "?"); fflush(stderr); return false; } return true; };
    { ID3DBlob* e = nullptr; if (FAILED(D3DCompile(HLSL_POST, strlen(HLSL_POST), "post", nullptr, nullptr, "vs", "vs_5_1", 0, 0, &b_post_vs, &e)) || FAILED(D3DCompile(HLSL_POST, strlen(HLSL_POST), "post", nullptr, nullptr, "ps", "ps_5_1", 0, 0, &b_post_ps, &e))) { fprintf(stderr, "[view] shader: %s\n", e ? (const char*)e->GetBufferPointer() : "?"); fflush(stderr); return; } }
    if (!comp("vs", "vs_5_1", &b_vs) || !comp("ps", "ps_5_1", &b_ps) || !comp("ps_screen", "ps_5_1", &b_screen) || !comp("ps_shape", "ps_5_1", &b_shape)) return;
    static const D3D12_TEXTURE_ADDRESS_MODE AM[3] = { D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP };
    for (int pt = 0; pt < 2; pt++) for (int a = 0; a < 3; a++) for (int c = 0; c < 3; c++) {
        D3D12_SAMPLER_DESC d{}; d.Filter = pt ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU = AM[a]; d.AddressV = AM[c]; d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; d.MaxLOD = D3D12_FLOAT32_MAX;
        dev->CreateSampler(&d, cpu_of(heap_smp, inc_smp, UINT(pt * 9 + a * 3 + c)));
    }
    make_targets();
    tron_tex.res = make_target(TW, TH, 1, PIC, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, BLACK); view_of(tron_tex); tron_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    shape_tex.res = make_target(TW, TH, 1, PIC, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, NOTHING); view_of(shape_tex); shape_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    tron_ds = make_target(TW, TH, 1, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr);
    dev->CreateRenderTargetView(tron_tex.res, nullptr, cpu_of(heap_rtv, inc_rtv, 1)); dev->CreateRenderTargetView(shape_tex.res, nullptr, cpu_of(heap_rtv, inc_rtv, 2));
    dev->CreateDepthStencilView(tron_ds, nullptr, cpu_of(heap_dsv, inc_dsv, 1));
    if (trace_out()) {     // [visual 2026-11-19] (the trace's occlusion queries: TQ_MAX a frame in flight)
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION; qh.Count = TQ_MAX * FRAMES; dev->CreateQueryHeap(&qh, IID_PPV_ARGS(&tq_heap));
        tq_read = make_buffer(UINT64(TQ_MAX) * FRAMES * 8u, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    { const u32 wh = 0xFFFFFFFFu; white = make_tex(1, 1, &wh); }
    { const u32 bk = 0xFF000000u; native_tex = make_tex(1, 1, &bk); native_w = native_h = 1; }      // (its place in the pictures' table, kept for good; the texture itself is remade at each video's size)
    { D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT; D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = 64; d.Height = 32; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = PIC; d.SampleDesc.Count = 1;
      dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&video_tex.res)); view_of(video_tex); }
    load_hd_folders();     // (the full-detail men's pictures: the wrestler folders' -- no2k15)
    cl->Close(); { ID3D12CommandList* l = cl; queue->ExecuteCommandLists(1, &l); }
    queue->Signal(fence, ++fence_val); frs[0].done = fence_val;

    const char* shots = getenv("WCW_NATIVE_SHOTS"); double next_shot = 1.0; int shot_n = 0;
    Frame f; LARGE_INTEGER fq, t0, tp; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0); tp = t0;
    Frame next; bool next_ok = false; Pace pace;      // [visual 2026-11-19] (Smooth Motion's clock: the update taken, not yet due)
    int env_interp = env_int("WCW_NATIVE_INTERP", -1), env_vsync = env_int("WCW_NATIVE_VSYNC", -1);    // (a test can hold these)
    // [qa12x 2026-11-17] The tests' "draw only what's captured" (WCW_QA_DRAW_EVERY, arcade_view.cpp qa_frame_wanted): only the
    // frames the reader hands over are drawn, each once, as it is (no blending between updates, nothing drawn while none comes),
    // with no wait for the screen; with WCW_NATIVE_SHOTS each is also kept, named by the game's seconds (its pictures / 30), as
    // qa.ps1 names its own shots. Off (unset): all as before.
    const bool qa_draw = qa_draw_every() > 0.0;
    const bool qa_frame_names = env_int("WCW_QA_SHOT_FRAMES", 0) > 0;     // [ljmenu10 2026-11-24] (a test's shots frame by frame: named to the thousandth of a second)
    if (qa_draw) { env_interp = 0; env_vsync = 0; }
    const int burst_n = getenv("WCW_NATIVE_SHOT_BURST") ? atoi(getenv("WCW_NATIVE_SHOT_BURST")) : 0; int burst_left = 0;
    double sum_ms = 0, log_at = 5.0, update_time = 1.0 / 30.0; int pictures = 0, updates = 0; bool have = false;
    std::vector<float> xm;
    MSG msg;
    const bool child = parent && mode_ == 2;     // (a window inside the game's, shown when there's a list to draw)
    bool shown = !child;
    uint64_t frame_n = 1;
    for (;;) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        const bool interp = env_interp >= 0 ? env_interp != 0 : pic_smooth != 0, vsync = env_vsync >= 0 ? env_vsync != 0 : pic_limit == 0;
        bool got = false, waited = false;
        // [visual 2026-11-19] (paced, arcade_view_shared.h Pace: the update is taken when the moment the picture shows has reached it,
        //  decided after the wait for the screen, when the picture is made. WCW_NATIVE_PACE=0: as found, as before. Smooth Motion only:
        //  without it each update is shown whole from a refresh, and taken as found it already keeps the game's beat (measured: every
        //  update 2 refreshes on a 60 Hz screen) -- paced, a moment near a refresh's edge put some on 1 or 3. Not for the tests' "draw
        //  only what's captured" either: each update drawn once as it comes.)
        const bool paced = pace_on() && !qa_draw && interp;
        if (!paced && next_ok) { std::swap(f, next); next_ok = false; got = true; }     // (Smooth Motion just turned off: the update held first)
        else if (!paced) { std::lock_guard<std::mutex> l(mtx_); if (have_ready_) { std::swap(f, ready_); have_ready_ = false; got = true; } }
        else {
            { std::lock_guard<std::mutex> l(mtx_); if (have_ready_ && !next_ok) { std::swap(next, ready_); have_ready_ = false; next_ok = true; } }
            if (next_ok) {
                if (have && shown && latency_ev && vsync) { WaitForSingleObjectEx(latency_ev, 200, TRUE); waited = true; }     // (a picture follows: blended on if the update isn't due)
                LARGE_INTEGER n; QueryPerformanceCounter(&n);
                const double now = clock_ms(uint64_t(n.QuadPart));
                if (!shown || pace.due(f, have, next, now)) { std::swap(f, next); next_ok = false; got = true; pace.took(f, 1000.0 / std::max(1u, screen_hz.load()), now); }
            }
        }
        LARGE_INTEGER a; QueryPerformanceCounter(&a);
        if (!got) {
            // (nothing from the game for a moment: its own picture shows, whatever that is)
            if (child && shown && double(a.QuadPart - tp.QuadPart) / fq.QuadPart > 0.4) { shown = false; ShowWindow(hwnd, SW_HIDE); }
            if (!have || !shown || (!interp && !waited)) { Sleep(1); continue; }
        } else {
            // (how long an update takes, as it's going: for how far along the blend is)
            const double since = double(a.QuadPart - tp.QuadPart) / fq.QuadPart;
            if (have && since > 0.008 && since < 0.2) update_time += (since - update_time) * 0.1;
            // (2026-11-09, the user: wrestlers' walks in the entrances "are jittery a bit". Measured here: how far each update's arrival
            //  is from the average, and how many pictures had to stand still waiting for one -- k held at 1 -- see the [view] line)
            if (have && since > 0.008 && since < 0.2) { const double off = since - update_time; jitter_sum_ += off * off; jitter_n_++; if (since > update_time * 1.25) late_updates_++; }
            tp = a; have = true; updates++;
            if (parent) {
                // (inside the game's window, the size of it)
                if (child && f.in_match != shown) { shown = f.in_match; ShowWindow(hwnd, shown ? SW_SHOWNOACTIVATE : SW_HIDE); }
                RECT c; GetClientRect(parent, &c);
                if (c.right > 16 && c.bottom > 16 && (c.right != W_ || c.bottom != H_)) {
                    W_ = c.right; H_ = c.bottom; if (child) SetWindowPos(hwnd, nullptr, 0, 0, W_, H_, SWP_NOZORDER | SWP_NOACTIVATE);
                    wait_for(fence_val);
                    drop_targets();
                    if (FAILED(sc->ResizeBuffers(FRAMES, W_, H_, DXGI_FORMAT_UNKNOWN, sc_flags))) device_lost(dev->GetDeviceRemovedReason());
                    make_targets();
                }
            }
        }
        const bool draw = shown;
        { static double next_hz = 0; const double now_s = double(a.QuadPart - t0.QuadPart) / fq.QuadPart;
          if (now_s >= next_hz) { next_hz = now_s + 5.0; DEVMODEA dm{}; dm.dmSize = sizeof dm; MONITORINFOEXA mi{}; mi.cbSize = sizeof mi; if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) && EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1u) screen_hz = dm.dmDisplayFrequency; } }
        // HDR on or off: the swap chain's format and colour space. (HDR10, where the screen can show it)
        { bool want = pic_hdr != 0;
          if (want && !hdr_on) {     // (only if Windows has the screen in HDR: the swap chain takes it either way, and on an SDR desktop the picture comes out blown)
              bool screen_hdr = false; IDXGIOutput* o = nullptr; DXGI_COLOR_SPACE_TYPE was = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
              if (SUCCEEDED(sc->GetContainingOutput(&o)) && o) { IDXGIOutput6* o6 = nullptr; if (SUCCEEDED(o->QueryInterface(IID_PPV_ARGS(&o6))) && o6) { DXGI_OUTPUT_DESC1 od{}; if (SUCCEEDED(o6->GetDesc1(&od))) { was = od.ColorSpace; screen_hdr = od.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; } o6->Release(); } o->Release(); }
              if (!screen_hdr) { static bool said; if (!said) { said = true; fprintf(stderr, "[view] HDR asked for: Windows doesn't have this screen in HDR (colour space %d): left off. (Settings > System > Display > Use HDR)\n", int(was)); fflush(stderr); } pic_hdr = 0; want = false; }
          }
          if (want != hdr_on) {
              static bool told; const DXGI_COLOR_SPACE_TYPE cs = want ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; UINT sup = 0;
              const DXGI_FORMAT nf = want ? DXGI_FORMAT_R10G10B10A2_UNORM : PIC;
              wait_for(fence_val); drop_targets();
              bool ok = SUCCEEDED(sc->ResizeBuffers(FRAMES, W_, H_, nf, sc_flags));
              if (ok) { sc->CheckColorSpaceSupport(cs, &sup); ok = (sup & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0 && SUCCEEDED(sc->SetColorSpace1(cs)); }
              if (ok) { hdr_on = want; back_format = nf; fprintf(stderr, "[view] HDR %s\n", want ? "on: HDR10" : "off"); fflush(stderr); }
              else {
                  if (!told) { told = true; fprintf(stderr, "[view] HDR asked for: this screen can't show it, as it's set up\n"); fflush(stderr); }
                  pic_hdr = 0; sc->ResizeBuffers(FRAMES, W_, H_, PIC, sc_flags); sc->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709); back_format = PIC; hdr_on = false;
              }
              make_targets();
          } }
        if (draw && latency_ev && vsync && !waited) WaitForSingleObjectEx(latency_ev, 200, TRUE);      // (the screen's rate: start each picture as late as possible, for the least delay. Not waiting for the screen: nothing to wait for)
        QueryPerformanceCounter(&a);
        if (got) srv_room(f.fresh.size());
        fr = &frs[frame_n++ % FRAMES];
        wait_for(fr->done);
        if (trace_out()) trace_flush(trace_rows_[fr - frs]);     // [visual 2026-11-19] (that picture's row: its samples are back)
        collect_later();
        fr->up_off = 0;
        fr->alloc->Reset(); cl->Reset(fr->alloc, nullptr);
        { ID3D12DescriptorHeap* hs[2] = { heap_srv, heap_smp }; cl->SetDescriptorHeaps(2, hs); }
        uint64_t fresh_n = 0, fresh_px = 0;     // [visual 2026-11-19] (the trace)
        if (got) {
            for (const NewTex& n : f.fresh) { fresh_n++; fresh_px += uint64_t(n.w) * n.h; }
            for (NewTex& n : f.fresh) { if (int(srvs.size()) <= n.id) srvs.resize(n.id + 1); srvs[n.id] = make_tex(n.w, n.h, n.rgba.data()); }
            f.fresh.clear();
            for (int id : f.dead) if (id < int(srvs.size()) && srvs[id].res) { release_later(srvs[id].res, int(srvs[id].srv)); srvs[id] = Tex{}; }
            f.dead.clear();
            // the update's points and triangles, up to the graphics card's own memory
            const UINT64 vn = f.v.size() * sizeof(V), in = f.idx.size() * 4u;
            if (vn > vb_cap) { if (vb) release_later(vb); vb_cap = vn * 2 + (1 << 20); vb = make_buffer(vb_cap, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON); }
            else if (vb) barrier(vb, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
            if (in > ib_cap) { if (ib) release_later(ib); ib_cap = in * 2 + (1 << 20); ib = make_buffer(ib_cap, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON); }
            else if (ib) barrier(ib, D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
            if (vn) { ID3D12Resource* up; UINT64 off; std::memcpy(upload(vn, 16, nullptr, &up, &off), f.v.data(), vn); cl->CopyBufferRegion(vb, 0, up, off, vn); }
            if (in) { ID3D12Resource* up; UINT64 off; std::memcpy(upload(in, 16, nullptr, &up, &off), f.idx.data(), in); cl->CopyBufferRegion(ib, 0, up, off, in); }
            if (vb) barrier(vb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            if (ib) barrier(ib, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
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
                ID3D12Resource* up; UINT64 off; u8* to = static_cast<u8*>(upload(256u * 32u, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, nullptr, &up, &off));
                std::memcpy(to, f.video.data(), 256u * 32u);
                barrier(video_tex.res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION tl{}; tl.pResource = video_tex.res; tl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION fl{}; fl.pResource = up; fl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; fl.PlacedFootprint.Offset = off; fl.PlacedFootprint.Footprint = { PIC, 64, 32, 1, 256 };
                cl->CopyTextureRegion(&tl, 0, 0, 0, &fl, nullptr);
                barrier(video_tex.res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }
        LARGE_INTEGER t_up_q; QueryPerformanceCounter(&t_up_q);     // [visual 2026-11-19] (the trace: the update's uploads done)
        // 2026-11-10: a native Titantron video's picture, the one due now by the game's clock (arcade_media.cpp), copied up when
        // it's a new one. Asked every picture drawn, not every game update: a 60-a-second file shows at 60. The decoded
        // picture comes from ordinary memory (the hardware decoder's too: it's read back); see NOTES.md for a copy-free path.
        if (const std::shared_ptr<const arcade_media::Picture> pic = arcade_media_video_picture()) {
            if (pic->serial != native_serial || !native_has) {
                const u32 pw = u32(pic->width), ph = u32(pic->height);
                if (pw != native_w || ph != native_h) {
                    // (another size: the texture's remade in the same place of the table. Rare -- once a video -- so it waits
                    //  for the graphics card to finish with the old one)
                    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = pw; d.Height = ph; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = PIC; d.SampleDesc.Count = 1;
                    ID3D12Resource* made = nullptr;
                    if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&made)))) {
                        wait_for(fence_val);
                        if (native_tex.res) native_tex.res->Release();
                        native_tex.res = made; native_w = pw; native_h = ph;
                        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = PIC; v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels = 1;
                        dev->CreateShaderResourceView(native_tex.res, &v, cpu_of(heap_keep, inc_srv, native_tex.srv));
                        dev->CopyDescriptorsSimple(1, cpu_of(heap_srv, inc_srv, native_tex.srv), cpu_of(heap_keep, inc_srv, native_tex.srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    }
                }
                if (pw == native_w && ph == native_h && pic->rgba.size() >= size_t(pw) * ph * 4) {
                    const u32 pitch = (pw * 4u + 255u) & ~255u;
                    ID3D12Resource* up; UINT64 off; u8* to = static_cast<u8*>(upload(UINT64(pitch) * ph, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, nullptr, &up, &off));
                    for (u32 y = 0; y < ph; y++) std::memcpy(to + size_t(y) * pitch, pic->rgba.data() + size_t(y) * pw * 4, size_t(pw) * 4);
                    barrier(native_tex.res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    D3D12_TEXTURE_COPY_LOCATION tl{}; tl.pResource = native_tex.res; tl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION fl{}; fl.pResource = up; fl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; fl.PlacedFootprint.Offset = off; fl.PlacedFootprint.Footprint = { PIC, pw, ph, 1, pitch };
                    cl->CopyTextureRegion(&tl, 0, 0, 0, &fl, nullptr);
                    barrier(native_tex.res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    native_serial = pic->serial; native_has = true;
                }
            }
        }
        else native_has = false;        // (no video, or the next one's first picture isn't here yet: the last video's isn't shown for it)
        if (!draw || !vb || !ib) {      // (hidden: the pictures made all the same; nothing to show)
            cl->Close(); { ID3D12CommandList* l = cl; queue->ExecuteCommandLists(1, &l); }
            queue->Signal(fence, ++fence_val); fr->done = fence_val;
            continue;
        }
        // how far along between the last two updates
        float k = 1.0f;
        if (interp && !f.snap && paced) { k = pace.k(f, clock_ms(uint64_t(a.QuadPart))); if (k >= 1.0f) held_pictures_++; }     // [visual 2026-11-19]
        else if (interp && !f.snap) { k = float(double(a.QuadPart - tp.QuadPart) / fq.QuadPart / update_time); if (k >= 1.0f) held_pictures_++; k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k; }
        pictures_seen_++;
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
        D3D12_GPU_VIRTUAL_ADDRESS xva; std::memcpy(upload(xm.size() * 4u, 64, &xva), xm.data(), xm.size() * 4u);
        cl->SetGraphicsRootSignature(root);
        const float bl[4] = { k, 1.0f, 1.0f, 0 }; cl->SetGraphicsRoot32BitConstants(0, 4, bl, 0);      // (the Titantron's picture is the shape it is)
        cl->SetGraphicsRootShaderResourceView(2, xva);
        D3D12_VERTEX_BUFFER_VIEW vbv{ vb->GetGPUVirtualAddress(), UINT(f.v.size() * sizeof(V)), UINT(sizeof(V)) };
        D3D12_INDEX_BUFFER_VIEW ibv{ ib->GetGPUVirtualAddress(), UINT(f.idx.size() * 4u), DXGI_FORMAT_R32_UINT };
        cl->IASetVertexBuffers(0, 1, &vbv); cl->IASetIndexBuffer(&ibv); cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // the Titantron's picture first: the list's own camera pass, into a target of its own at full detail
        // [visual 2026-11-19] (the frame brought no camera pass -- tron.c draws it only in what it takes for the scene's last run, and
        //  that guess misses now and then -- while the screen is still live: the picture it has is kept, not cleared to black)
        const bool tron_kept = tron_keep_on() && !frame_cam_ && !use_video_ && tron_has_;
        const int tron_code = use_video_ ? 1 : frame_cam_ ? 0 : tron_kept ? 3 : 2;      // (the trace: 0 the camera's, 1 the video in memory, 2 black, 3 kept)
        if (!tron_kept) {
        barrier(tron_tex.res, tron_state, D3D12_RESOURCE_STATE_RENDER_TARGET); tron_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        { auto r = cpu_of(heap_rtv, inc_rtv, 1); auto ds = cpu_of(heap_dsv, inc_dsv, 1); cl->OMSetRenderTargets(1, &r, FALSE, &ds); cl->ClearRenderTargetView(r, BLACK, 0, nullptr); cl->ClearDepthStencilView(ds, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr); }
        D3D12_VIEWPORT vt{ 0, 0, (float)TW, (float)TH, 0, 1 }; cl->RSSetViewports(1, &vt);
        draw_all(f, 1, 0, 0, float(TW), float(TH));
        barrier(tron_tex.res, tron_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); tron_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        tron_has_ = frame_cam_;
        }
        // a sign's own shape, if there's one: that picture cut to it (tron.c mask_compose: cleared to nothing, the shape, the picture over it)
        { bool shaped = false; for (const Draw& d : f.draws) if (d.target == 2 && d.count) { shaped = true; break; }
          if (shaped) {
              barrier(shape_tex.res, shape_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
              auto r = cpu_of(heap_rtv, inc_rtv, 2); cl->OMSetRenderTargets(1, &r, FALSE, nullptr); cl->ClearRenderTargetView(r, NOTHING, 0, nullptr);
              draw_all(f, 2, 0, 0, float(TW), float(TH));
              shape_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
          }
          barrier(shape_tex.res, shape_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); shape_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; }
        // [visual 2026-11-19] the trace's row for this picture
        TraceRow* trow = nullptr;
        if (trace_out()) {
            trow = &trace_rows_[fr - frs]; *trow = TraceRow{}; trow->on = true; sh_copied_[fr - frs] = false;
            trow->pic = pictures_seen_; trow->t = trace_ms(uint64_t(a.QuadPart)); trow->got = got; trow->serial = f.serial; trow->vi = f.vi; trow->vi_prev = f.vi_prev; trow->vi_hand = f.vi_hand;
            trow->t_begin = trace_ms(f.t_begin); trow->t_begin_prev = trace_ms(f.t_begin_prev); trow->t_hand = trace_ms(f.t_hand); trow->t_take = trace_ms(uint64_t(tp.QuadPart));
            trow->ld[0] = f.loads_keyed; trow->ld[1] = f.loads_matched; trow->ld[2] = f.verts_unmatched; trow->ld[3] = f.loads_plain; trow->ld[4] = f.verts_plain;
            trow->swaps = f.swaps; trow->fresh_n = fresh_n; trow->fresh_px = fresh_px; trow->t_up = trace_ms(uint64_t(t_up_q.QuadPart));
            trow->k = k; trow->snap = f.snap; trow->interp = interp; trow->vsync = vsync; trow->upd_ms = update_time * 1000.0; trow->moving = f.cam[0].moving; trow->tron = tron_code;
            for (const Draw& d : f.draws) {
                if (!d.count) continue;
                if (d.target == 1) { trow->cam_tris += d.count / 3; continue; }
                if (d.target != 0) continue;
                if (d.tex == -1 || d.tex == -3 || d.tex == -4) trow->scr_tris += d.count / 3;
                for (u32 j = 0; j + 2 < d.count; j += 3) {     // (the world's triangles: through a matrix as the update had it, or blended)
                    const Xf& x = f.xf[f.v[f.idx[d.start + j]].xf];
                    if (x.kind != 0) trow->w_blended++; else if (std::fabs(x.direct[3]) + std::fabs(x.direct[7]) + std::fabs(x.direct[11]) > 1e-6f) trow->w_direct++;
                }
                if (trace_shadow(d)) for (u32 j = 0; j + 2 < d.count; j += 3) { const Xf& x = f.xf[f.v[f.idx[d.start + j]].xf]; trow->sh_tris++; trow->sh_k[std::min<int>(x.kind, 2)]++; }
            }
            tq_on_ = tq_heap != nullptr; tq_base_ = UINT(fr - frs) * TQ_MAX; tq_n_ = 0;
            if (env_int("WCW_VIEW_TRACE_XF", 0) == 2 && pictures_seen_ % 60u == 0u)     // (the see-through draws' owners, one picture in 60)
                for (size_t di = 0; di < f.draws.size(); di++) {
                    const Draw& d = f.draws[di];
                    if (!d.count || d.target != 0 || (d.zmode & 3u) != 3u) continue;
                    const Xf& x = f.xf[f.v[f.idx[d.start]].xf];
                    fprintf(trace_out(), "D,%llu,%zu,%08X,%d,%d,%u,%d,%.3f,%.3f,%.3f,%.3f,%08X,%08X,%08X,%d\n", (unsigned long long)trow->pic, di, unsigned(u32(x.key)), int(x.kind), int(x.why), d.count / 3, d.tex, d.cb.prim[0], d.cb.prim[1], d.cb.prim[2], d.cb.prim[3], d.rm_l, d.rm_h, d.geom, int(d.zmode));
                }
            if (trace_xf())
                for (size_t i = 0; i < nx; i++) {
                    const Xf& x = f.xf[i]; const int cls = x.target == 0 ? trace_class(x.key) : 0;
                    static const int all = env_int("WCW_VIEW_TRACE_XF", 0);     // (2: every owned matrix of one picture in 60, to find what's what)
                    if (!cls && !(all == 2 && x.key && pictures_seen_ % 60u == 0u)) continue;
                    const float* o = &xm[i * 16 + 12];
                    fprintf(trace_out(), "X,%llu,%08X,%d,%d,%d,%.6f,%.6f,%.3f,%.2f,%.2f,%.2f\n", (unsigned long long)trow->pic, unsigned(u32(x.key)), cls, int(x.kind), int(x.why), o[3] != 0.0f ? o[0] / o[3] : 0.0f, o[3] != 0.0f ? o[1] / o[3] : 0.0f, o[3], x.kind == 1 ? x.b.t[0] : 0.0f, x.kind == 1 ? x.b.t[1] : 0.0f, x.kind == 1 ? x.b.t[2] : 0.0f);
                }
        }
        // then the frame
        { auto r = cpu_of(heap_rtv, inc_rtv, 0); auto ds = cpu_of(heap_dsv, inc_dsv, 0); cl->OMSetRenderTargets(1, &r, FALSE, &ds); cl->ClearRenderTargetView(r, BLACK, 0, nullptr); cl->ClearDepthStencilView(ds, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr); }
        // (the picture's shape, as large as it fits. Wider than the game's 4 : 3: more of the view at the sides; narrower: more above and below. Nothing's stretched: what's on the screen, the HUD, stays its shape, in the middle)
        const float shape = SHAPES[pic_shape.load()];
        float vw = float(W_), vh = float(H_);
        if (vw > vh * shape) vw = vh * shape; else vh = vw / shape;
        const float vx = (W_ - vw) * 0.5f, vy = (H_ - vh) * 0.5f;
        D3D12_VIEWPORT vp{ vx, vy, vw, vh, 0, 1 }; cl->RSSetViewports(1, &vp);
        cur_xs_ = std::min(1.0f, (4.0f / 3.0f) / shape); cur_ys_ = std::min(1.0f, shape / (4.0f / 3.0f));
        { const float b2[4] = { k, cur_xs_, cur_ys_, 0 }; cl->SetGraphicsRoot32BitConstants(0, 4, b2, 0); }
        draw_all(f, 0, vx, vy, vw, vh);
        if (trow && tq_n_) { cl->ResolveQueryData(tq_heap, D3D12_QUERY_TYPE_OCCLUSION, tq_base_, tq_n_, tq_read, UINT64(tq_base_) * 8u); trow->qbase = tq_base_; trow->qn = tq_n_; }
        tq_on_ = false;
        // its four samples a pixel made one, onto the screen's picture
        ID3D12Resource* back = backs[sc->GetCurrentBackBufferIndex()];
        const int crt_now = pic_crt.load();
        const bool post = crt_now != 0 || hdr_on;
        barrier(main_rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        D3D12_RESOURCE_STATES back_state;
        if (!post) {
            barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST); back_state = D3D12_RESOURCE_STATE_RESOLVE_DEST;
            cl->ResolveSubresource(back, 0, main_rt, 0, PIC);
        } else if (ID3D12PipelineState* pp = post_pipe()) {
            // to a picture of its own, and from there through the last step
            barrier(scene_tex.res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            cl->ResolveSubresource(scene_tex.res, 0, main_rt, 0, PIC);
            barrier(scene_tex.res, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET); back_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            auto r = cpu_of(heap_rtv, inc_rtv, 3 + sc->GetCurrentBackBufferIndex()); cl->OMSetRenderTargets(1, &r, FALSE, nullptr);
            D3D12_VIEWPORT fv{ 0, 0, float(W_), float(H_), 0, 1 }; cl->RSSetViewports(1, &fv);
            D3D12_RECT fr_{ 0, 0, W_, H_ }; cl->RSSetScissorRects(1, &fr_);
            cl->SetPipelineState(pp);
            const float pc[12] = { vx / W_, vy / H_, vw / W_, vh / H_, float(W_), float(H_), 0, 0, float(crt_now), hdr_on ? 1.0f : 0.0f, 200.0f / 10000.0f, 0 };   // (white at 200 nits)
            D3D12_GPU_VIRTUAL_ADDRESS pva; std::memcpy(upload(sizeof pc, 256, &pva), pc, sizeof pc);
            cl->SetGraphicsRootConstantBufferView(1, pva);
            cl->SetGraphicsRootDescriptorTable(3, gpu_of(heap_srv, inc_srv, scene_tex.srv));
            cl->SetGraphicsRootDescriptorTable(4, gpu_of(heap_smp, inc_smp, 8u));      // (smooth, clamped)
            cl->DrawInstanced(3, 1, 0, 0);
        } else { barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET); back_state = D3D12_RESOURCE_STATE_RENDER_TARGET; }
        barrier(main_rt, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        const double el = double(c.QuadPart - t0.QuadPart) / fq.QuadPart;
        // a picture to keep (QA): copied out of the screen's picture
        const bool tshot = trow && (tron_code == 2 || tron_code == 3) && trow->scr_tris && !hdr_on && trace_shot_wanted();     // [visual 2026-11-19] (the trace: a black Titantron kept)
        const bool want_shot = tshot || (shots && !hdr_on && !f.draws.empty() && (qa_draw ? got : (el > next_shot || burst_left > 0)));
        ID3D12Resource* rb = nullptr; D3D12_PLACED_SUBRESOURCE_FOOTPRINT rfp{};
        if (want_shot) {
            D3D12_RESOURCE_DESC bd; back->GetDesc(&bd); UINT64 total = 0; dev->GetCopyableFootprints(&bd, 0, 1, 0, &rfp, nullptr, nullptr, &total);
            rb = make_buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            barrier(back, back_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION sl{}; sl.pResource = back; sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = rb; dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dl.PlacedFootprint = rfp;
            cl->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
            barrier(back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        } else if (trow && (env_int("WCW_VIEW_TRACE_SHADOWSHOTS", 0) > 0 || getenv("WCW_VIEW_TRACE_WINDOW"))) {     // [visual 2026-11-19] (the trace's shadow shots: this picture kept until its samples are known)
            const int sl = int(fr - frs);
            D3D12_RESOURCE_DESC bd; back->GetDesc(&bd); UINT64 total = 0; dev->GetCopyableFootprints(&bd, 0, 1, 0, &sh_fp_[sl], nullptr, nullptr, &total);
            UINT64 have_w = 0; if (sh_rb_[sl]) { D3D12_RESOURCE_DESC rd; sh_rb_[sl]->GetDesc(&rd); have_w = rd.Width; }
            if (!sh_rb_[sl] || have_w < total) { if (sh_rb_[sl]) release_later(sh_rb_[sl]); sh_rb_[sl] = make_buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); }
            barrier(back, back_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION sl_{}; sl_.pResource = back; sl_.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = sh_rb_[sl]; dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dl.PlacedFootprint = sh_fp_[sl];
            cl->CopyTextureRegion(&dl, 0, 0, 0, &sl_, nullptr);
            barrier(back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
            sh_copied_[sl] = true;
        } else barrier(back, back_state, D3D12_RESOURCE_STATE_PRESENT);
        if (trow) { LARGE_INTEGER rq; QueryPerformanceCounter(&rq); trow->t_rec = trace_ms(uint64_t(rq.QuadPart)); }     // [visual 2026-11-19]
        cl->Close(); { ID3D12CommandList* l = cl; queue->ExecuteCommandLists(1, &l); }
        QueryPerformanceCounter(&c);
        sum_ms += double(c.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart; pictures++;
        const HRESULT phr = sc->Present(vsync ? 1 : 0, (!vsync && tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (FAILED(phr)) { log_debug(); device_lost(phr); }
        if (trow) { LARGE_INTEGER pq; QueryPerformanceCounter(&pq); trow->t_present = trace_ms(uint64_t(pq.QuadPart)); }
        queue->Signal(fence, ++fence_val); fr->done = fence_val;
        log_debug();
        if (el > log_at && pictures) {
            size_t live = 0; for (const Tex& t : srvs) if (t.res) live++;
            PROCESS_MEMORY_COUNTERS pm{}; pm.cb = sizeof pm; K32GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof pm);
            fprintf(stderr, "[view] %.1f pictures a second from %.1f game updates a second, %.2f ms of work a picture, %zu triangles, %zu points, %zu matrices (%llu matched, %llu world, %llu direct so far), %zu textures live of %zu made, %.0f MB; %llu cuts, %llu cameras lost, the camera's biggest smooth move %.1f, turn %.1f; worst matrix put back %.6f\n",
                pictures / 5.0, updates / 5.0, sum_ms / pictures, f.idx.size() / 3, f.v.size(), f.xf.size(), (unsigned long long)xf_matched_.load(), (unsigned long long)xf_world_.load(), (unsigned long long)xf_direct_.load(), live, srvs.size(), pm.WorkingSetSize / 1048576.0, (unsigned long long)cuts_.load(), (unsigned long long)cam_lost_.load(), worst_move_.load(), worst_turn_.load(), worst_key_.load());
            fprintf(stderr, "[view] the Titantron: %llu changes between its live picture and the video in memory\n", (unsigned long long)tron_switches_);
            fprintf(stderr, "[view] depth (WCW_NATIVE_DEPTH %d): draws without %llu, solid %llu, see-through %llu (made: pulled %llu, not pulled -- another projection %llu)\n", world_depth_on(),
                    (unsigned long long)depth_draws_[0].exchange(0), (unsigned long long)depth_draws_[1].exchange(0), (unsigned long long)depth_draws_[3].exchange(0), (unsigned long long)see_pulled_.exchange(0), (unsigned long long)see_kept_.exchange(0));
            fprintf(stderr, "[view] Smooth Motion: updates %.1f ms apart on average, %.2f ms off it (rms), %llu late (25%% or more); %llu of %llu pictures held waiting for an update\n",
                    update_time * 1000.0, jitter_n_ ? std::sqrt(jitter_sum_ / double(jitter_n_)) * 1000.0 : 0.0, (unsigned long long)late_updates_, (unsigned long long)held_pictures_, (unsigned long long)pictures_seen_);
            { const uint64_t n = read_n_.exchange(0), ns = read_ns_.exchange(0);
              if (updates) fprintf(stderr, "[read] %.3f ms an update reading the lists (%llu lists)\n", double(ns) / 1e6 / updates, (unsigned long long)n); }
            fflush(stderr); log_at = el + 5.0; sum_ms = 0; pictures = 0; updates = 0;
        }
        if (want_shot) {
            if (!tshot) { if (burst_left > 0) burst_left--; else { burst_left = burst_n; next_shot = el + (getenv("WCW_NATIVE_SHOT_EVERY") ? atof(getenv("WCW_NATIVE_SHOT_EVERY")) : 4.0); } }
            wait_for(fence_val);
            char pth[MAX_PATH];
            if (tshot) snprintf(pth, MAX_PATH, "%s.%s_%06llu.bmp", getenv("WCW_VIEW_TRACE"), tron_code == 2 ? "black" : "kept", (unsigned long long)trow->pic);
            else if (qa_draw) { snprintf(pth, MAX_PATH, qa_frame_names ? "%s\\%07.3fs.bmp" : "%s\\%05.1fs.bmp", shots, f.swaps / 30.0); shot_n++; }     // [qa12x 2026-11-17] (named by the game's seconds; [ljmenu10] WCW_QA_SHOT_FRAMES=1: to the thousandth, one name a frame -- with WCW_QA_DRAW_EVERY under 0.1 two frames had the same tenth and the second took the first's name)
            else snprintf(pth, MAX_PATH, "%s\\%04d.bmp", shots, shot_n++);
            void* mp = nullptr;
            if (SUCCEEDED(rb->Map(0, nullptr, &mp))) {
                if (FILE* o = fopen(pth, "wb")) {
                    const int w = int(rfp.Footprint.Width), h = int(rfp.Footprint.Height), sz = 54 + w * h * 3;
                    u8 hdr[54] = { 'B', 'M' }; std::memcpy(hdr + 2, &sz, 4); hdr[10] = 54; hdr[14] = 40; std::memcpy(hdr + 18, &w, 4); std::memcpy(hdr + 22, &h, 4); hdr[26] = 1; hdr[28] = 24;
                    fwrite(hdr, 1, 54, o);
                    std::vector<u8> row((size_t(w) * 3 + 3) & ~size_t(3));
                    for (int y = h - 1; y >= 0; y--) { const u8* q = static_cast<const u8*>(mp) + size_t(y) * rfp.Footprint.RowPitch; for (int x = 0; x < w; x++) { row[size_t(x) * 3] = q[x * 4 + 2]; row[size_t(x) * 3 + 1] = q[x * 4 + 1]; row[size_t(x) * 3 + 2] = q[x * 4]; } fwrite(row.data(), 1, row.size(), o); }
                    fclose(o);
                }
                rb->Unmap(0, nullptr);
            }
            rb->Release();
            if (burst_n) { fprintf(stderr, "[view] shot %d: %.3f of the way, the game's update %s\n", shot_n - 1, k, got ? "new" : "held"); fflush(stderr); }
        }
    }
}

// ---- the backend's doorway (new): what arcade_view.cpp calls ----
void backend_run() { run(); }
void backend_status(char* out, size_t size) {
    if (!dev) { snprintf(out, size, "no device"); return; }
    snprintf(out, size, "Direct3D 12, device state %08lX, %zu textures", (unsigned long)dev->GetDeviceRemovedReason(), srvs.size());
}

} // namespace arcade_view
