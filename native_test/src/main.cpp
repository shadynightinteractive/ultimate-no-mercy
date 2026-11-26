// No Mercy native test: a throwaway proof, separate from the mod and its exe. Two 2K15 men, one arena, a few moves, in plain C++ on Direct3D 11: no N64 memory, threads or display lists.
// The men are skinned on the GPU; their fingers are morphs in the vertex shader; the Titantron is a real second render of the whole scene at full detail. Animations are No Mercy's own files, decoded (tools/export.py).
//   NoMercyNativeTest.exe [--novsync] [--bench seconds] [--shots dir seconds] [--cpu]
// Keys: arrows move, Shift run, Z punch, X kick, C clothesline, V suplex, B finisher, T taunt, F1 CPU plays you, Esc quit. Pad: stick / d-pad, A punch, B kick, X clothesline, Y suplex, RB finisher, LB taunt, RT run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <xinput.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef uint32_t u32; typedef uint8_t u8;
template <class T> static void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { char b_[256]; snprintf(b_, 256, "%s failed: 0x%08lx", #x, (unsigned long)hr_); fail(b_); } } while (0)
static FILE* logf_;
static void fail(const char* m) { if (logf_) { fprintf(logf_, "FATAL %s\n", m); fflush(logf_); } MessageBoxA(nullptr, m, "No Mercy native test", MB_OK); ExitProcess(1); }

// ---- maths: row vectors, as the game: p x M ------------------------------------------------------------------
struct M4 { float m[16]; };
static M4 mul(const M4& a, const M4& b) { M4 o; for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float s = 0; for (int k = 0; k < 4; k++) s += a.m[r * 4 + k] * b.m[k * 4 + c]; o.m[r * 4 + c] = s; } return o; }
static M4 ident() { M4 o{}; o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1; return o; }
struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return { a.x / l, a.y / l, a.z / l }; }
static M4 look_at(V3 eye, V3 at) {   // right-handed, as the game's
    V3 z = norm(sub(eye, at)), x = norm(cross({ 0, 1, 0 }, z)), y = cross(z, x);
    return { { x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, -dot(x, eye), -dot(y, eye), -dot(z, eye), 1 } };
}
static M4 persp(float fovy, float aspect, float zn, float zf) {
    float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect;
    return { { xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0 } };
}
static const float DEG = 3.14159265f / 180.0f;
static void rotm(const float* r, float* f) {        // the game's (func_80012BE0; data/announce/fk.py)
    float s1 = std::sin(r[0] * DEG), c2 = std::cos(r[0] * DEG), s3 = std::sin(r[1] * DEG), c4 = std::cos(r[1] * DEG), s5 = std::sin(r[2] * DEG), c6 = std::cos(r[2] * DEG);
    std::memset(f, 0, 64);
    f[0] = c6 * c4; f[1] = s5 * c2 + c6 * s3 * s1; f[2] = s5 * s1 - c6 * s3 * c2;
    f[4] = -s5 * c4; f[5] = c6 * c2 - s5 * s3 * s1; f[6] = c6 * s1 + s5 * s3 * c2;
    f[8] = s3; f[9] = -c4 * s1; f[10] = c4 * c2; f[15] = 1;
}

// ---- the pack ------------------------------------------------------------------------------------------------
struct Batch { u32 tex, start, count; };
struct Joint { u32 bone, parent; float x, y, z; };
struct Key { float rot[20][3], pos[20][3]; };
struct Clip { std::string name; std::vector<Key> keys; int frames; };
struct Man {
    std::string name; ID3D11Buffer* vb = nullptr; ID3D11Buffer* ib = nullptr; std::vector<Batch> batches; std::vector<Joint> joints; u32 tris = 0;
};
struct ArenaBatch { int tex; u32 cms, cmt, start, count; };
static std::vector<u8> pak; static size_t pk;
static u32 rd32() { u32 v; std::memcpy(&v, &pak[pk], 4); pk += 4; return v; }
static float rdf() { float v; std::memcpy(&v, &pak[pk], 4); pk += 4; return v; }

static ID3D11Device* dev; static ID3D11DeviceContext* ctx; static IDXGISwapChain* swap;
static ID3D11RenderTargetView* rtv; static ID3D11DepthStencilView* dsv;
static std::vector<ID3D11ShaderResourceView*> srvs;
static Man men[2]; static std::vector<Clip> clips;
static ID3D11Buffer* arena_vb; static std::vector<ArenaBatch> arena_batches; static u32 arena_tris;
static int WIDTH = 1280, HEIGHT = 720;

static const char* SHADER = R"(
cbuffer Scene : register(b0) { row_major float4x4 vp; };
cbuffer ManCB : register(b1) { row_major float4x4 bones[20]; float4 shape[4]; };   // (shape: [hand * 2 + 0] thumb index middle ring, [hand * 2 + 1] little)
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VI { float3 p : POSITION; float2 uv : TEXCOORD0; float s : TEXCOORD1; float3 c : TEXCOORD2; float3 o : TEXCOORD3; float3 w : TEXCOORD4; uint4 b : TEXCOORD5; };
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VO vs_man(VI v) {
    float3 p = v.p;
    uint f = v.b.w;
    if (f) {                      // his fingers: toward closed, or toward straight, by the pose's shape
        uint hand = f > 8 ? 1 : 0, g = (f > 8 ? f - 8 : f) - 1;
        float a = g < 4 ? shape[hand * 2][g] : shape[hand * 2 + 1][0];
        p += a >= 0 ? v.c * a : v.o * (-a);
    }
    float4 q = float4(p, 1), wp = float4(0, 0, 0, 0);
    wp += v.w.x * mul(q, bones[v.b.x]); wp += v.w.y * mul(q, bones[v.b.y]); wp += v.w.z * mul(q, bones[v.b.z]);
    wp.w = 1;
    VO o; o.pos = mul(wp, vp); o.uv = v.uv; o.col = float4(v.s, v.s, v.s, 1); return o;
}
float4 ps_man(VO i) : SV_Target { return float4(tex.Sample(smp, i.uv).rgb * i.col.rgb, 1); }
struct AI { float3 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; };
VO vs_arena(AI v) { VO o; o.pos = mul(float4(v.p, 1), vp); o.uv = v.uv; o.col = v.c; return o; }
float4 ps_arena(VO i) : SV_Target { float4 t = tex.Sample(smp, i.uv); clip(t.a - 0.5); return float4(t.rgb * i.col.rgb, 1); }
float4 ps_screen(VO i) : SV_Target { return float4(tex.Sample(smp, float2(i.uv.x, i.uv.y)).rgb, 1); }
struct FI { float3 p : POSITION; float4 c : COLOR0; };
VO vs_flat(FI v) { VO o; o.pos = mul(float4(v.p, 1), vp); o.uv = float2(0, 0); o.col = v.c; return o; }
VO vs_hud(FI v) { VO o; o.pos = float4(v.p, 1); o.uv = float2(0, 0); o.col = v.c; return o; }
float4 ps_flat(VO i) : SV_Target { return i.col; }
)";
static ID3D11VertexShader* vs_man, * vs_arena, * vs_flat, * vs_hud; static ID3D11PixelShader* ps_man, * ps_arena, * ps_screen, * ps_flat;
static ID3D11InputLayout* il_man, * il_arena, * il_flat;
static ID3D11Buffer* cb_scene, * cb_man, * dyn_vb;
static ID3D11SamplerState* samplers[3][3], * smp_man; static ID3D11RasterizerState* rs_back, * rs_none; static ID3D11DepthStencilState* ds_on, * ds_read, * ds_off; static ID3D11BlendState* bs_alpha, * bs_off;
static ID3D11RenderTargetView* tron_rtv; static ID3D11ShaderResourceView* tron_srv; static ID3D11DepthStencilView* tron_dsv; static const int TRON_W = 512, TRON_H = 256;

static ID3DBlob* compile(const char* entry, const char* target) {
    ID3DBlob* b = nullptr, * e = nullptr;
    HRESULT hr = D3DCompile(SHADER, strlen(SHADER), "shader", nullptr, nullptr, entry, target, 0, 0, &b, &e);
    if (FAILED(hr)) fail(e ? (const char*)e->GetBufferPointer() : "shader");
    return b;
}
static void make_buffer(ID3D11Buffer** out, const void* data, UINT bytes, UINT bind, bool dynamic) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.BindFlags = bind; d.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE; d.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = data;
    CHECK(dev->CreateBuffer(&d, data ? &s : nullptr, out));
}
static void load_pack(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) fail("no data\\game.pak next to the program");
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); pak.resize(n); fread(pak.data(), 1, n, f); fclose(f);
    if (std::memcmp(pak.data(), "NMT1", 4)) fail("bad pack");
    pk = 4;
    u32 nt = rd32();
    for (u32 i = 0; i < nt; i++) {
        u32 w = rd32(), h = rd32(), len = rd32();
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 0; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D* t; CHECK(dev->CreateTexture2D(&d, nullptr, &t));
        ctx->UpdateSubresource(t, 0, nullptr, &pak[pk], w * 4, 0);
        ID3D11ShaderResourceView* s; CHECK(dev->CreateShaderResourceView(t, nullptr, &s)); ctx->GenerateMips(s); srvs.push_back(s); t->Release();
        pk += len;
    }
    u32 nm = rd32();
    for (u32 i = 0; i < nm && i < 2; i++) {
        Man& M = men[i]; char nb[17] = {}; std::memcpy(nb, &pak[pk], 16); pk += 16; M.name = nb;
        u32 nv = rd32(), ni = rd32();
        make_buffer(&M.vb, &pak[pk], nv * 64, D3D11_BIND_VERTEX_BUFFER, false); pk += nv * 64;
        make_buffer(&M.ib, &pak[pk], ni * 4, D3D11_BIND_INDEX_BUFFER, false); pk += ni * 4;
        M.tris = ni / 3;
        u32 nb2 = rd32(); for (u32 b = 0; b < nb2; b++) { Batch x; x.tex = rd32(); x.start = rd32(); x.count = rd32(); M.batches.push_back(x); }
        u32 nj = rd32(); for (u32 j = 0; j < nj; j++) { Joint x; x.bone = rd32(); x.parent = rd32(); x.x = rdf(); x.y = rdf(); x.z = rdf(); M.joints.push_back(x); }
    }
    u32 nc = rd32();
    for (u32 i = 0; i < nc; i++) {
        Clip c; char nb[17] = {}; std::memcpy(nb, &pak[pk], 16); pk += 16; c.name = nb;
        u32 nk = rd32(); c.frames = (int)rd32(); c.keys.resize(nk);
        for (u32 k = 0; k < nk; k++) for (int b = 0; b < 20; b++) { for (int a = 0; a < 3; a++) c.keys[k].rot[b][a] = rdf(); for (int a = 0; a < 3; a++) c.keys[k].pos[b][a] = rdf(); }
        clips.push_back(std::move(c));
    }
    u32 nav = rd32(); make_buffer(&arena_vb, &pak[pk], nav * 24, D3D11_BIND_VERTEX_BUFFER, false); pk += nav * 24; arena_tris = nav / 3;
    u32 nab = rd32();
    for (u32 i = 0; i < nab; i++) { ArenaBatch a; std::memcpy(&a.tex, &pak[pk], 4); pk += 4; a.cms = rd32(); a.cmt = rd32(); a.start = rd32(); a.count = rd32(); arena_batches.push_back(a); }
    std::vector<u8>().swap(pak);
}

// ---- the match ----------------------------------------------------------------------------------------------
enum { C_IDLE, C_WALK, C_RUN, C_PUNCH, C_HIT, C_KICK, C_CLOTHES, C_CLOTHES_D, C_SUPLEX, C_SUPLEX_D, C_STUNNER, C_STUNNER_D, C_ROCKBOTTOM, C_ROCKBOTTOM_D, C_TAUNT_A, C_TAUNT_R, C_GETUP, C_GETUP2 };
enum { S_FREE, S_ACT, S_DOWN };
static const float FLOOR_Y = 90.0f, RING = 195.0f, FRAME_RATE = 30.0f, BLEND = 0.13f;
// the fingers' shapes: the game's (hd.c hd_shape): thumb index middle ring little, in 256ths: closed +, straight -
static const short SHAPE[11][5] = { {200,200,200,200,200}, {0,-256,-256,-256,-256}, {0,-256,-256,-256,-256}, {256,256,-256,256,256}, {0,200,-256,-256,-256}, {180,-256,256,256,256}, {256,256,256,256,256}, {200,-256,256,256,256}, {120,-256,170,170,170}, {256,-256,-256,256,256}, {180,-256,200,200,200} };
struct Wrestler {
    float x = 0, z = 0, facing = 0;      // dir (sin, cos)
    int clip = C_IDLE; float t = 0; int state = S_FREE; int partner_clip = -1;
    float hp = 1.0f, down_t = 0; bool hit_done = false;
    float rot[20][3], pos[20][3]; float prot[20][3], ppos[20][3]; float blend = 0;
    M4 world[20]; float shape[4][4];
    int shapeL = 0, shapeR = 0; bool is_rock = false;
    float cpu_t = 1.0f; bool cpu = false; bool moving = false, running = false;
    bool following = false;
};
static Wrestler W[2];
static float clip_len(int c) { return (float)clips[c].frames / FRAME_RATE; }
static void sample(const Clip& c, float t, float rot[20][3], float pos[20][3]) {
    float f = t * FRAME_RATE / 2.0f;
    int n = (int)c.keys.size();
    if (f < 0) f = 0;
    int a = (int)f; float k = f - a;
    if (a >= n - 1) { a = n - 1; k = 0; }
    int b = std::min(a + 1, n - 1);
    for (int i = 0; i < 20; i++) for (int j = 0; j < 3; j++) {
        float r0 = c.keys[a].rot[i][j], r1 = c.keys[b].rot[i][j], d = r1 - r0;
        if (d > 180) d -= 360; if (d < -180) d += 360;
        rot[i][j] = r0 + d * k;
        pos[i][j] = c.keys[a].pos[i][j] + (c.keys[b].pos[i][j] - c.keys[a].pos[i][j]) * k;
    }
}
// where a point of the clip's space goes in the world, for him
static void clip_to_world(const Wrestler& w, float cx, float cz, float& ox, float& oz) {
    float yaw = w.facing + 3.14159265f, c = std::cos(yaw), s = std::sin(yaw);      // (the clips face -z)
    ox = cx * c + cz * s + w.x; oz = -cx * s + cz * c + w.z;
}
static void start_clip(Wrestler& w, int clip) {
    // hold the pose so far to blend from; and carry on from where the last clip left him
    std::memcpy(w.prot, w.rot, sizeof w.rot); std::memcpy(w.ppos, w.pos, sizeof w.pos); w.blend = BLEND;
    float r[20][3], p[20][3];
    sample(clips[clip], 0, r, p);
    float wx, wz, nx, nz;
    clip_to_world(w, w.pos[1][0], w.pos[1][2], wx, wz);
    Wrestler tmp = w; tmp.x = 0; tmp.z = 0;
    clip_to_world(tmp, p[1][0], p[1][2], nx, nz);
    w.x = wx - nx; w.z = wz - nz;
    w.clip = clip; w.t = 0; w.hit_done = false;
}
static void face_other(Wrestler& a, const Wrestler& b) { a.facing = std::atan2(b.x - a.x, b.z - a.z); }
static float dist(const Wrestler& a, const Wrestler& b) { float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }
static void act(Wrestler& a, int clip) { start_clip(a, clip); a.state = S_ACT; a.following = false; }
// a move on two men: the second plays his side of it in the same space, the first's (the files are made that way: root 0 for both)
static void paired(Wrestler& a, Wrestler& d, int ca, int cd) {
    face_other(a, d);
    d.facing = a.facing;
    // a stands where he is; d's clip starts at the shared origin
    act(a, ca);
    std::memcpy(d.prot, d.rot, sizeof d.rot); std::memcpy(d.ppos, d.pos, sizeof d.pos); d.blend = BLEND;
    d.x = a.x; d.z = a.z; d.clip = cd; d.t = 0; d.state = S_ACT; d.following = true; d.hit_done = false;
}
static void try_attack(Wrestler& a, Wrestler& d, int move, bool me) {
    if (a.state != S_FREE) return;
    float r = dist(a, d);
    switch (move) {
    case 0: face_other(a, d); act(a, C_PUNCH); break;
    case 1: face_other(a, d); act(a, C_KICK); break;
    case 2: if (r < 120 && d.state == S_FREE) { paired(a, d, C_CLOTHES, C_CLOTHES_D); d.hp -= 0.06f; } else { face_other(a, d); act(a, C_PUNCH); } break;
    case 3: if (r < 100 && d.state == S_FREE) { paired(a, d, C_SUPLEX, C_SUPLEX_D); d.hp -= 0.10f; } break;
    case 4: if (r < 100 && d.state == S_FREE) { if (a.is_rock) paired(a, d, C_ROCKBOTTOM, C_ROCKBOTTOM_D); else paired(a, d, C_STUNNER, C_STUNNER_D); d.hp -= 0.18f; } break;
    case 5: act(a, a.is_rock ? C_TAUNT_R : C_TAUNT_A); break;
    }
    (void)me;
}
static void update(Wrestler& w, Wrestler& o, float dt, float mx, float mz, bool run) {
    w.t += dt;
    if (w.blend > 0) w.blend -= dt;
    float len = clip_len(w.clip);
    if (w.state == S_ACT) {
        // a strike lands once, about a third of the way through
        if ((w.clip == C_PUNCH || w.clip == C_KICK) && !w.hit_done && w.t > len * 0.35f) {
            w.hit_done = true;
            if (dist(w, o) < 110 && o.state != S_DOWN && !o.following && o.clip != C_GETUP && o.clip != C_GETUP2) { act(o, C_HIT); o.hp -= 0.03f; }
        }
        if (w.t >= len) {
            bool downed = w.clip == C_CLOTHES_D || w.clip == C_SUPLEX_D || w.clip == C_STUNNER_D || w.clip == C_ROCKBOTTOM_D;
            if (downed) { w.state = S_DOWN; w.down_t = 1.2f; w.t = len; w.following = false; }
            else if (w.clip == C_GETUP || w.clip == C_GETUP2 || true) { start_clip(w, C_IDLE); w.state = S_FREE; w.following = false; }
        }
    } else if (w.state == S_DOWN) {
        w.t = len; w.down_t -= dt;
        if (w.down_t <= 0) { start_clip(w, C_GETUP); w.state = S_ACT; }
    } else {
        w.moving = mx != 0 || mz != 0;
        if (w.moving) {
            float sp = run ? 300.0f : 130.0f, l = std::sqrt(mx * mx + mz * mz); if (l > 1) { mx /= l; mz /= l; }
            w.x += mx * sp * dt; w.z += mz * sp * dt;
            if (run) w.facing = std::atan2(mx, mz); else face_other(w, o);
            int want = run ? C_RUN : C_WALK;
            if (w.clip != want) start_clip(w, want);
        } else {
            face_other(w, o);
            if (w.clip != C_IDLE) start_clip(w, C_IDLE);
        }
        if (w.t >= len) w.t = std::fmod(w.t, len);
    }
    w.x = std::max(-RING, std::min(RING, w.x)); w.z = std::max(-RING, std::min(RING, w.z));
}
// his skeleton: each bone keeps the animation's rotation; its place is where its parent carries HIS joint (src/hd.c)
static void pose(Wrestler& w, const Man& man) {
    float r[20][3], p[20][3];
    float t = w.t;
    sample(clips[w.clip], t, r, p);
    std::memcpy(w.rot, r, sizeof r); std::memcpy(w.pos, p, sizeof p);
    if (w.blend > 0) {
        float k = w.blend / BLEND;
        for (int i = 0; i < 20; i++) for (int j = 0; j < 3; j++) {
            float d = w.prot[i][j] - r[i][j]; if (d > 180) d -= 360; if (d < -180) d += 360;
            r[i][j] += d * k;
        }
        // (the pelvis: its own height, the blend's; where it stands was carried on in start_clip)
        p[1][1] += (w.ppos[1][1] - p[1][1]) * k;
    }
    M4 B[20]; for (int i = 0; i < 20; i++) B[i] = ident();
    for (int b = 1; b < 20; b++) rotm(r[b], B[b].m);
    // the pelvis: the file's place is its pivot, (0, 10, 0) up from the model's origin (matches the game's matrices: QA RAM)
    B[1].m[12] = p[1][0] - 10.0f * B[1].m[4]; B[1].m[13] = p[1][1] - 10.0f * B[1].m[5]; B[1].m[14] = p[1][2] - 10.0f * B[1].m[6];
    for (const Joint& J : man.joints) {
        const float* q = B[J.parent].m; float* m = B[J.bone].m;
        float wx = J.x * q[0] + J.y * q[4] + J.z * q[8] + q[12], wy = J.x * q[1] + J.y * q[5] + J.z * q[9] + q[13], wz = J.x * q[2] + J.y * q[6] + J.z * q[10] + q[14];
        m[12] = wx - (J.x * m[0] + J.y * m[4] + J.z * m[8]); m[13] = wy - (J.x * m[1] + J.y * m[5] + J.z * m[9]); m[14] = wz - (J.x * m[2] + J.y * m[6] + J.z * m[10]);
    }
    float yaw = w.facing + 3.14159265f, c = std::cos(yaw), s = std::sin(yaw);
    M4 G = { { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, w.x, FLOOR_Y, w.z, 1 } };
    for (int b = 0; b < 20; b++) w.world[b] = mul(B[b], G);
    // the hands: the shape the game's pose names
    float len = clip_len(w.clip), u = w.t / std::max(len, 0.001f);
    int L = 0, R = 0;
    if (w.clip == C_TAUNT_A && u > 0.20f && u < 0.92f) L = R = 3;                 // (pose 7: both middle fingers)
    else if (w.clip == C_TAUNT_R && u > 0.15f && u < 0.9f) { L = 2; R = 0; }      // (pose 9)
    else if (w.clip == C_PUNCH || w.clip == C_CLOTHES || w.clip == C_STUNNER || w.clip == C_ROCKBOTTOM) L = R = 6;   // (fists)
    w.shapeL = L; w.shapeR = R;
    for (int h = 0; h < 2; h++) {
        const short* sh = SHAPE[h ? L : R];
        for (int i = 0; i < 4; i++) w.shape[h * 2][i] = sh[i] / 256.0f;
        w.shape[h * 2 + 1][0] = sh[4] / 256.0f; w.shape[h * 2 + 1][1] = w.shape[h * 2 + 1][2] = w.shape[h * 2 + 1][3] = 0;
    }
}

// ---- drawing --------------------------------------------------------------------------------------------------
static UINT draw_calls; static uint64_t tris_drawn;
struct FlatV { float x, y, z; u8 r, g, b, a; };
static std::vector<FlatV> flat;
static void flush_flat(ID3D11VertexShader* vs) {
    if (flat.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms; CHECK(ctx->Map(dyn_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms));
    std::memcpy(ms.pData, flat.data(), flat.size() * sizeof(FlatV)); ctx->Unmap(dyn_vb, 0);
    UINT st = sizeof(FlatV), of = 0;
    ctx->IASetVertexBuffers(0, 1, &dyn_vb, &st, &of); ctx->IASetInputLayout(il_flat);
    ctx->VSSetShader(vs, nullptr, 0); ctx->PSSetShader(ps_flat, nullptr, 0);
    ctx->Draw((UINT)flat.size(), 0); draw_calls++;
    flat.clear();
}
static void shadow(const Wrestler& w) {
    const float* m = w.world[1].m;
    float cx = m[12], cz = m[14], y = FLOOR_Y + 0.8f;
    const int N = 20;
    for (int i = 0; i < N; i++) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        flat.push_back({ cx, y, cz, 0, 0, 0, 120 });
        flat.push_back({ cx + std::cos(a1) * 42, y, cz + std::sin(a1) * 34, 0, 0, 0, 0 });
        flat.push_back({ cx + std::cos(a0) * 42, y, cz + std::sin(a0) * 34, 0, 0, 0, 0 });
    }
}
// the ropes: the game makes them as it goes, so they aren't in the captured arena: three a side, between the turnbuckles (their places: the captured pads, y 130-220, at +-224)
static void ropes() {
    static const float YS[3] = { 140.0f, 175.0f, 210.0f }; const float C = 224.0f, H = 1.9f;
    static const float SIDES[4][4] = { {-C,-C,C,-C}, {C,-C,C,C}, {C,C,-C,C}, {-C,C,-C,-C} };
    auto q = [](FlatV a, FlatV b, FlatV c, FlatV d) { flat.push_back(a); flat.push_back(b); flat.push_back(c); flat.push_back(a); flat.push_back(c); flat.push_back(d); };
    for (float y : YS) for (auto& e : SIDES) {
        float ax = e[0], az = e[1], bx = e[2], bz = e[3];
        float nx = (az == bz) ? 0 : H, nz = (az == bz) ? H : 0;
        auto V = [&](float x, float yy, float z, u8 sh) { return FlatV{ x, yy, z, (u8)(sh * 3 / 5), (u8)(sh / 14), (u8)(sh / 14), 255 }; };
        q(V(ax - nx, y + H, az - nz, 215), V(bx - nx, y + H, bz - nz, 215), V(bx + nx, y + H, bz + nz, 215), V(ax + nx, y + H, az + nz, 215));   // (top)
        q(V(ax - nx, y - H, az - nz, 95), V(bx - nx, y - H, bz - nz, 95), V(bx + nx, y - H, bz + nz, 95), V(ax + nx, y - H, az + nz, 95));       // (under)
        q(V(ax - nx, y + H, az - nz, 165), V(bx - nx, y + H, bz - nz, 165), V(bx - nx, y - H, bz - nz, 120), V(ax - nx, y - H, az - nz, 120));   // (sides)
        q(V(ax + nx, y + H, az + nz, 165), V(bx + nx, y + H, bz + nz, 165), V(bx + nx, y - H, bz + nz, 120), V(ax + nx, y - H, az + nz, 120));
    }
}
static int stress_men;       // (--stress N: N more men round the ring, whole, to see how many the machine can carry)
static void draw_scene(const M4& vp, bool to_tron) {
    D3D11_MAPPED_SUBRESOURCE ms; CHECK(ctx->Map(cb_scene, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)); std::memcpy(ms.pData, &vp, 64); ctx->Unmap(cb_scene, 0);
    ctx->VSSetConstantBuffers(0, 1, &cb_scene); ctx->VSSetConstantBuffers(1, 1, &cb_man);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // the arena
    ctx->RSSetState(rs_none); ctx->OMSetDepthStencilState(ds_on, 0); ctx->OMSetBlendState(bs_off, nullptr, 0xFFFFFFFF);
    UINT st = 24, of = 0; ctx->IASetVertexBuffers(0, 1, &arena_vb, &st, &of); ctx->IASetInputLayout(il_arena);
    ctx->VSSetShader(vs_arena, nullptr, 0); ctx->PSSetShader(ps_arena, nullptr, 0);
    bool screen_ps = false;
    for (const ArenaBatch& a : arena_batches) {
        ID3D11ShaderResourceView* s;
        if (a.tex < 0) {
            if (to_tron) continue;               // (a screen doesn't show itself)
            s = tron_srv;
            if (!screen_ps) { ctx->PSSetShader(ps_screen, nullptr, 0); screen_ps = true; }
        } else {
            s = srvs[a.tex];
            if (screen_ps) { ctx->PSSetShader(ps_arena, nullptr, 0); screen_ps = false; }
        }
        ctx->PSSetShaderResources(0, 1, &s);
        ID3D11SamplerState* sm = samplers[a.cms & 3][a.cmt & 3]; ctx->PSSetSamplers(0, 1, &sm);
        ctx->Draw(a.count, a.start); draw_calls++; tris_drawn += a.count / 3;
    }
    if (screen_ps) ctx->PSSetShader(ps_arena, nullptr, 0);
    ropes(); flush_flat(vs_flat);
    // the shadows
    ctx->OMSetBlendState(bs_alpha, nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(ds_read, 0);
    shadow(W[0]); shadow(W[1]); flush_flat(vs_flat);
    ctx->OMSetBlendState(bs_off, nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(ds_on, 0);
    // the men, whole, on the GPU
    ctx->RSSetState(rs_back); ctx->IASetInputLayout(il_man); ctx->VSSetShader(vs_man, nullptr, 0); ctx->PSSetShader(ps_man, nullptr, 0); ctx->PSSetSamplers(0, 1, &smp_man);
    for (int i = 0; i < 2; i++) {
        struct { M4 b[20]; float sh[4][4]; } cb;
        std::memcpy(cb.b, W[i].world, sizeof cb.b); std::memcpy(cb.sh, W[i].shape, sizeof cb.sh);
        CHECK(ctx->Map(cb_man, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)); std::memcpy(ms.pData, &cb, sizeof cb); ctx->Unmap(cb_man, 0);
        UINT mst = 64; ctx->IASetVertexBuffers(0, 1, &men[i].vb, &mst, &of); ctx->IASetIndexBuffer(men[i].ib, DXGI_FORMAT_R32_UINT, 0);
        for (const Batch& b : men[i].batches) { ctx->PSSetShaderResources(0, 1, &srvs[b.tex]); ctx->DrawIndexed(b.count, b.start, 0); draw_calls++; tris_drawn += b.count / 3; }
    }
    for (int n = 0; n < stress_men; n++) {
        int i = n & 1;
        float a = n * 6.2831853f / std::max(stress_men, 1), r = 200.0f + 45.0f * (n % 3);
        struct { M4 b[20]; float sh[4][4]; } cb;
        std::memcpy(cb.b, W[i].world, sizeof cb.b); std::memcpy(cb.sh, W[i].shape, sizeof cb.sh);
        float ox = std::cos(a) * r - W[i].world[1].m[12], oz = std::sin(a) * r - W[i].world[1].m[14];
        for (int b = 0; b < 20; b++) { cb.b[b].m[12] += ox; cb.b[b].m[14] += oz; }
        CHECK(ctx->Map(cb_man, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)); std::memcpy(ms.pData, &cb, sizeof cb); ctx->Unmap(cb_man, 0);
        UINT mst = 64; ctx->IASetVertexBuffers(0, 1, &men[i].vb, &mst, &of); ctx->IASetIndexBuffer(men[i].ib, DXGI_FORMAT_R32_UINT, 0);
        for (const Batch& b : men[i].batches) { ctx->PSSetShaderResources(0, 1, &srvs[b.tex]); ctx->DrawIndexed(b.count, b.start, 0); draw_calls++; tris_drawn += b.count / 3; }
    }
}
static void bar(float x0, float y0, float x1, float y1, u8 r, u8 g, u8 b) {
    FlatV q[6] = { {x0,y0,0,r,g,b,255},{x1,y0,0,r,g,b,255},{x1,y1,0,r,g,b,255},{x0,y0,0,r,g,b,255},{x1,y1,0,r,g,b,255},{x0,y1,0,r,g,b,255} };
    flat.insert(flat.end(), q, q + 6);
}
static void save_bmp(const char* path) {
    ID3D11Texture2D* back; CHECK(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back));
    D3D11_TEXTURE2D_DESC d; back->GetDesc(&d);
    D3D11_TEXTURE2D_DESC rd = d; rd.SampleDesc.Count = 1; rd.BindFlags = D3D11_BIND_RENDER_TARGET; rd.Usage = D3D11_USAGE_DEFAULT;
    ID3D11Texture2D* res; CHECK(dev->CreateTexture2D(&rd, nullptr, &res)); ctx->ResolveSubresource(res, 0, back, 0, d.Format);
    D3D11_TEXTURE2D_DESC sd = rd; sd.BindFlags = 0; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st; CHECK(dev->CreateTexture2D(&sd, nullptr, &st)); ctx->CopyResource(st, res);
    D3D11_MAPPED_SUBRESOURCE ms; CHECK(ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms));
    FILE* f = fopen(path, "wb");
    if (f) {
        int w = d.Width, h = d.Height, sz = 54 + w * h * 3;
        u8 hdr[54] = { 'B','M' }; std::memcpy(hdr + 2, &sz, 4); hdr[10] = 54; hdr[14] = 40; std::memcpy(hdr + 18, &w, 4); std::memcpy(hdr + 22, &h, 4); hdr[26] = 1; hdr[28] = 24;
        fwrite(hdr, 1, 54, f);
        std::vector<u8> row(((w * 3 + 3) & ~3));
        for (int y = h - 1; y >= 0; y--) { const u8* s = (const u8*)ms.pData + y * ms.RowPitch; for (int x = 0; x < w; x++) { row[x * 3] = s[x * 4 + 2]; row[x * 3 + 1] = s[x * 4 + 1]; row[x * 3 + 2] = s[x * 4]; } fwrite(row.data(), 1, row.size(), f); }
        fclose(f);
    }
    ctx->Unmap(st, 0); st->Release(); res->Release(); back->Release();
}

// ---- the window and the loop ---------------------------------------------------------------------------------
static bool keys[256], keys_edge[256], quit_now;
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: quit_now = true; PostQuitMessage(0); return 0;
    case WM_KEYDOWN: if (w < 256) { if (!keys[w]) keys_edge[w] = true; keys[w] = true; } return 0;
    case WM_KEYUP: if (w < 256) keys[w] = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}
static void make_states() {
    ID3DBlob* b;
    b = compile("vs_man", "vs_5_0"); CHECK(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs_man));
    D3D11_INPUT_ELEMENT_DESC man_il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",3,DXGI_FORMAT_R32G32B32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",4,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",5,DXGI_FORMAT_R8G8B8A8_UINT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0} };
    CHECK(dev->CreateInputLayout(man_il, 7, b->GetBufferPointer(), b->GetBufferSize(), &il_man));
    b = compile("vs_arena", "vs_5_0"); CHECK(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs_arena));
    D3D11_INPUT_ELEMENT_DESC ar_il[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,20,D3D11_INPUT_PER_VERTEX_DATA,0} };
    CHECK(dev->CreateInputLayout(ar_il, 3, b->GetBufferPointer(), b->GetBufferSize(), &il_arena));
    b = compile("vs_flat", "vs_5_0"); CHECK(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs_flat));
    D3D11_INPUT_ELEMENT_DESC fl_il[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0} };
    CHECK(dev->CreateInputLayout(fl_il, 2, b->GetBufferPointer(), b->GetBufferSize(), &il_flat));
    b = compile("vs_hud", "vs_5_0"); CHECK(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs_hud));
    b = compile("ps_man", "ps_5_0"); CHECK(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps_man));
    b = compile("ps_arena", "ps_5_0"); CHECK(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps_arena));
    b = compile("ps_screen", "ps_5_0"); CHECK(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps_screen));
    b = compile("ps_flat", "ps_5_0"); CHECK(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps_flat));
    make_buffer(&cb_scene, nullptr, 64, D3D11_BIND_CONSTANT_BUFFER, true);
    make_buffer(&cb_man, nullptr, 20 * 64 + 64, D3D11_BIND_CONSTANT_BUFFER, true);
    make_buffer(&dyn_vb, nullptr, 16 * 4096, D3D11_BIND_VERTEX_BUFFER, true);
    static const D3D11_TEXTURE_ADDRESS_MODE AM[3] = { D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_CLAMP };  // (N64: cm 0 wrap, 1 mirror, 2 clamp)
    for (int s = 0; s < 3; s++) for (int t = 0; t < 3; t++) {
        D3D11_SAMPLER_DESC d{}; d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU = AM[s]; d.AddressV = AM[t]; d.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD = D3D11_FLOAT32_MAX;
        CHECK(dev->CreateSamplerState(&d, &samplers[s][t]));
    }
    D3D11_SAMPLER_DESC d{}; d.Filter = D3D11_FILTER_ANISOTROPIC; d.MaxAnisotropy = 8; d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; d.MaxLOD = D3D11_FLOAT32_MAX;
    CHECK(dev->CreateSamplerState(&d, &smp_man));
    D3D11_RASTERIZER_DESC r{}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_BACK; r.FrontCounterClockwise = TRUE; r.DepthClipEnable = TRUE; r.MultisampleEnable = TRUE;
    CHECK(dev->CreateRasterizerState(&r, &rs_back)); r.CullMode = D3D11_CULL_NONE; CHECK(dev->CreateRasterizerState(&r, &rs_none));
    D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; CHECK(dev->CreateDepthStencilState(&ds, &ds_on));
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; CHECK(dev->CreateDepthStencilState(&ds, &ds_read));
    ds.DepthEnable = FALSE; CHECK(dev->CreateDepthStencilState(&ds, &ds_off));
    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = 15; CHECK(dev->CreateBlendState(&bd, &bs_off));
    auto& t = bd.RenderTarget[0]; t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD; t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_ZERO; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    CHECK(dev->CreateBlendState(&bd, &bs_alpha));
    // the Titantron's picture: the whole scene again
    D3D11_TEXTURE2D_DESC td{}; td.Width = TRON_W; td.Height = TRON_H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* tt; CHECK(dev->CreateTexture2D(&td, nullptr, &tt)); CHECK(dev->CreateRenderTargetView(tt, nullptr, &tron_rtv)); CHECK(dev->CreateShaderResourceView(tt, nullptr, &tron_srv)); tt->Release();
    td.Format = DXGI_FORMAT_D32_FLOAT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL; CHECK(dev->CreateTexture2D(&td, nullptr, &tt)); CHECK(dev->CreateDepthStencilView(tt, nullptr, &tron_dsv)); tt->Release();
}

int main(int argc, char** argv) {
    bool vsync = true, all_cpu = false; double bench = 0, shot_every = 0; std::string shot_dir;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--novsync") vsync = false;
        else if (a == "--cpu") all_cpu = true;
        else if (a == "--bench" && i + 1 < argc) bench = atof(argv[++i]);
        else if (a == "--stress" && i + 1 < argc) stress_men = atoi(argv[++i]);
        else if (a == "--shots" && i + 2 < argc) { shot_dir = argv[++i]; shot_every = atof(argv[++i]); }
    }
    char exe[MAX_PATH]; GetModuleFileNameA(nullptr, exe, MAX_PATH); std::string folder = exe; folder = folder.substr(0, folder.find_last_of("\\/") + 1);
    logf_ = fopen((folder + "native_test.log").c_str(), "w");
    WNDCLASSA wc{}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "NoMercyNativeTest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    RECT rc{ 0, 0, WIDTH, HEIGHT }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("NoMercyNativeTest", "No Mercy native test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sc{}; sc.BufferCount = 1; sc.BufferDesc.Width = WIDTH; sc.BufferDesc.Height = HEIGHT; sc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sc.OutputWindow = hwnd; sc.SampleDesc.Count = 4; sc.Windowed = TRUE; sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    CHECK(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sc, &swap, &dev, nullptr, &ctx));
    ID3D11Texture2D* back; CHECK(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)); CHECK(dev->CreateRenderTargetView(back, nullptr, &rtv)); back->Release();
    D3D11_TEXTURE2D_DESC dd{}; dd.Width = WIDTH; dd.Height = HEIGHT; dd.MipLevels = 1; dd.ArraySize = 1; dd.Format = DXGI_FORMAT_D32_FLOAT; dd.SampleDesc.Count = 4; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* dt; CHECK(dev->CreateTexture2D(&dd, nullptr, &dt)); CHECK(dev->CreateDepthStencilView(dt, nullptr, &dsv)); dt->Release();
    make_states();
    LARGE_INTEGER fq, t0, t1; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    load_pack((folder + "data\\game.pak").c_str());
    QueryPerformanceCounter(&t1); double load_s = double(t1.QuadPart - t0.QuadPart) / fq.QuadPart;
    fprintf(logf_, "loaded in %.2f s: %s %u triangles, %s %u, arena %u, %zu textures, %zu clips\n", load_s, men[0].name.c_str(), men[0].tris, men[1].name.c_str(), men[1].tris, arena_tris, srvs.size(), clips.size()); fflush(logf_);

    W[0].x = -90; W[0].z = 0; W[1].x = 90; W[1].z = 0; W[1].is_rock = true; W[1].cpu = true; W[0].cpu = all_cpu;
    for (int i = 0; i < 2; i++) { sample(clips[C_IDLE], 0, W[i].rot, W[i].pos); std::memcpy(W[i].prot, W[i].rot, sizeof W[i].rot); std::memcpy(W[i].ppos, W[i].pos, sizeof W[i].pos); face_other(W[i], W[1 - i]); }
    uint64_t rng = 0x1234567;
    auto rnd = [&]() { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return (float)((rng >> 40) & 0xFFFF) / 65536.0f; };

    double elapsed = 0, last_title = 0, next_shot = 3.0; int shot_n = 0;
    LARGE_INTEGER prev = t1; std::vector<float> ftimes; ftimes.reserve(1 << 20); double measure_from = 2.0; uint64_t cpu_us_sum = 0;
    MSG msg;
    while (!quit_now) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (quit_now) break;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = float(double(now.QuadPart - prev.QuadPart) / fq.QuadPart); prev = now;
        if (dt > 0.1f) dt = 0.1f;
        elapsed += dt;
        if (elapsed > measure_from) ftimes.push_back(dt);
        // input
        XINPUT_STATE xs{}; bool pad = XInputGetState(0, &xs) == ERROR_SUCCESS; static WORD pb = 0; WORD bt = pad ? xs.Gamepad.wButtons : 0, be = bt & ~pb; pb = bt;
        float mx = 0, mz = 0;
        if (keys[VK_LEFT] || (bt & XINPUT_GAMEPAD_DPAD_LEFT)) mx += 1;   // (the camera looks along +z: its left is +x)
        if (keys[VK_RIGHT] || (bt & XINPUT_GAMEPAD_DPAD_RIGHT)) mx -= 1;
        if (keys[VK_UP] || (bt & XINPUT_GAMEPAD_DPAD_UP)) mz += 1;
        if (keys[VK_DOWN] || (bt & XINPUT_GAMEPAD_DPAD_DOWN)) mz -= 1;
        if (pad) { float lx = xs.Gamepad.sThumbLX / 32768.0f, ly = xs.Gamepad.sThumbLY / 32768.0f; if (std::fabs(lx) > 0.25f) mx -= lx; if (std::fabs(ly) > 0.25f) mz += ly; }
        bool run = keys[VK_SHIFT] || (pad && xs.Gamepad.bRightTrigger > 100);
        if (keys_edge[VK_ESCAPE]) quit_now = true;
        if (keys_edge[VK_F1]) W[0].cpu = !W[0].cpu;
        if (!W[0].cpu) {
            if (keys_edge['Z'] || (be & XINPUT_GAMEPAD_A)) try_attack(W[0], W[1], 0, true);
            if (keys_edge['X'] || (be & XINPUT_GAMEPAD_B)) try_attack(W[0], W[1], 1, true);
            if (keys_edge['C'] || (be & XINPUT_GAMEPAD_X)) try_attack(W[0], W[1], 2, true);
            if (keys_edge['V'] || (be & XINPUT_GAMEPAD_Y)) try_attack(W[0], W[1], 3, true);
            if (keys_edge['B'] || (be & XINPUT_GAMEPAD_RIGHT_SHOULDER)) try_attack(W[0], W[1], 4, true);
            if (keys_edge['T'] || (be & XINPUT_GAMEPAD_LEFT_SHOULDER)) try_attack(W[0], W[1], 5, true);
        }
        std::memset(keys_edge, 0, sizeof keys_edge);
        // the computer's men: up to the other, then something
        float cmx[2] = { W[0].cpu ? 0 : mx, 0 }, cmz[2] = { W[0].cpu ? 0 : mz, 0 }; bool crun[2] = { W[0].cpu ? false : run, false };
        for (int i = 0; i < 2; i++) {
            Wrestler& a = W[i]; Wrestler& d = W[1 - i];
            if (!a.cpu) continue;
            a.cpu_t -= dt;
            if (a.state != S_FREE) continue;
            float r = dist(a, d);
            if (r > 95 && d.state != S_DOWN) { float dx = d.x - a.x, dz = d.z - a.z; cmx[i] = dx / r; cmz[i] = dz / r; crun[i] = r > 260; }
            if (a.cpu_t <= 0 && r <= 110) {
                a.cpu_t = 0.8f + rnd() * 1.4f;
                float q = rnd();
                try_attack(a, d, q < 0.3f ? 0 : q < 0.5f ? 1 : q < 0.65f ? 2 : q < 0.8f ? 3 : q < 0.92f ? 4 : 5, false);
            } else if (a.cpu_t <= 0 && d.state == S_DOWN) { a.cpu_t = 2.0f; try_attack(a, d, 5, false); }
        }
        LARGE_INTEGER c0; QueryPerformanceCounter(&c0);
        update(W[0], W[1], dt, cmx[0], cmz[0], crun[0]); update(W[1], W[0], dt, cmx[1], cmz[1], crun[1]);
        // a man playing his side of a two-man move stays in the other's space
        for (int i = 0; i < 2; i++) if (W[i].following) { }
        pose(W[0], men[0]); pose(W[1], men[1]);
        LARGE_INTEGER c1; QueryPerformanceCounter(&c1); cpu_us_sum += (uint64_t)(double(c1.QuadPart - c0.QuadPart) * 1e6 / fq.QuadPart);
        // the cameras
        float px0 = W[0].world[1].m[12], pz0 = W[0].world[1].m[14], px1 = W[1].world[1].m[12], pz1 = W[1].world[1].m[14];
        float mx_ = (px0 + px1) * 0.5f, mz_ = (pz0 + pz1) * 0.5f, spread = std::sqrt((px0 - px1) * (px0 - px1) + (pz0 - pz1) * (pz0 - pz1));
        float back = 520.0f + spread * 0.75f;
        V3 eye{ mx_ * 0.6f, 215.0f + back * 0.12f, mz_ * 0.55f - back }, at{ mx_ * 0.85f, 205.0f, mz_ * 0.85f + 120.0f };
        M4 vp = mul(look_at(eye, at), persp(46.0f * DEG, float(WIDTH) / HEIGHT, 16.0f, 8192.0f));
        M4 hc = mul(look_at({ 820, 420, 0 }, { 0, 150, 0 }), persp(30.0f * DEG, 2.0f, 16.0f, 8192.0f));   // (the hard camera's: src/tron.c)
        draw_calls = 0; tris_drawn = 0;
        // the Titantron's picture, then the scene
        float clear_t[4] = { 0, 0, 0, 1 };
        ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
        ctx->OMSetRenderTargets(1, &tron_rtv, tron_dsv); ctx->ClearRenderTargetView(tron_rtv, clear_t); ctx->ClearDepthStencilView(tron_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT vt{ 0, 0, (float)TRON_W, (float)TRON_H, 0, 1 }; ctx->RSSetViewports(1, &vt);
        draw_scene(hc, true);
        ctx->PSSetShaderResources(0, 1, &none);
        ctx->OMSetRenderTargets(1, &rtv, dsv); float clear[4] = { 0.02f, 0.02f, 0.03f, 1 }; ctx->ClearRenderTargetView(rtv, clear); ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)WIDTH, (float)HEIGHT, 0, 1 }; ctx->RSSetViewports(1, &v);
        draw_scene(vp, false);
        // the HUD: how they stand
        ctx->OMSetDepthStencilState(ds_off, 0); ctx->RSSetState(rs_none);
        bar(-0.92f, 0.92f, -0.42f, 0.86f, 30, 30, 30); bar(-0.92f, 0.92f, -0.92f + 0.5f * std::max(W[0].hp, 0.0f), 0.86f, 60, 220, 90);
        bar(0.42f, 0.92f, 0.92f, 0.86f, 30, 30, 30); bar(0.92f - 0.5f * std::max(W[1].hp, 0.0f), 0.92f, 0.92f, 0.86f, 240, 200, 40);
        flush_flat(vs_hud);
        swap->Present(vsync ? 1 : 0, 0);
        if (shot_every > 0 && elapsed >= next_shot) { next_shot = elapsed + shot_every; char p[MAX_PATH]; snprintf(p, MAX_PATH, "%s\\%04d.bmp", shot_dir.c_str(), shot_n++); save_bmp(p); }
        if (elapsed - last_title > 0.5) { last_title = elapsed; char t[200]; snprintf(t, 200, "No Mercy native test  |  %.0f fps  |  %u draws, %llu triangles a frame", 1.0 / std::max(dt, 0.0001f), draw_calls, (unsigned long long)tris_drawn); SetWindowTextA(hwnd, t); }
        if (bench > 0 && elapsed >= bench) quit_now = true;
    }
    if (!ftimes.empty()) {
        std::sort(ftimes.begin(), ftimes.end());
        double sum = 0; for (float f : ftimes) sum += f;
        double avg = sum / ftimes.size();
        size_t p99 = (size_t)(ftimes.size() * 0.99), n = ftimes.size();
        fprintf(logf_, "%zu frames over %.1f s (vsync %s): average %.1f fps (%.2f ms); slowest 1%% of frames %.2f ms; game logic and bones %.0f us a frame; last frame %u draws, %llu triangles\n", n, sum, vsync ? "on" : "off", 1.0 / avg, avg * 1000, ftimes[p99] * 1000.0, (double)cpu_us_sum / std::max<size_t>(n, 1), draw_calls, (unsigned long long)tris_drawn);
    }
    fclose(logf_);
    return 0;
}
