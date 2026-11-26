// [xplat 2026-11-12] The picture's shaders for the Vulkan backend (arcade_view_vulkan.cpp). THE SAME HLSL as the Direct3D 12 backend's
// (arcade_view_d3d12.cpp, the HLSL string): every line below the "---- the same as Direct3D 12 ----" mark is copied from it unchanged
// (tools/check_shaders.py compares them). Only how the resources are given differs, because Vulkan has no root constants / root
// descriptors / descriptor tables the way Direct3D 12 has: above the mark, the names the shader code uses (uvx, vpx, prim, env, cc,
// zx, blend, shape_) are macros for the same values, found in a push constant (blend, shape_, which draw) and in a storage buffer
// with every draw's constants (draws), the matrices in a second one (xfm), the picture in set 1 and how to read it in set 2.
// Compiled at build time with dxc (-spirv -fvk-invert-y: Vulkan's clip space has y down; the picture comes out the same way up).
struct DrawCBData { float4 uvx; float4 vpx; float4 prim; float4 env; int4 cc[4]; float4 zx; };     // (arcade_view::DrawCB, 144 bytes)
struct PushC { float blend; float2 shape_; float unused_; uint draw; };
[[vk::push_constant]] PushC pc;
struct XfM { row_major float4x4 m; };
[[vk::binding(0, 0)]] StructuredBuffer<XfM> xfm;
[[vk::binding(1, 0)]] StructuredBuffer<DrawCBData> draws;
[[vk::binding(0, 1)]] Texture2D tex;
[[vk::binding(0, 2)]] SamplerState smp;
#define uvx draws[pc.draw].uvx
#define vpx draws[pc.draw].vpx
#define prim draws[pc.draw].prim
#define env draws[pc.draw].env
#define cc draws[pc.draw].cc
#define zx draws[pc.draw].zx
#define blend pc.blend
#define shape_ pc.shape_
// ---- the same as Direct3D 12 ----
struct VI { [[vk::location(0)]] float3 p : POSITION; [[vk::location(1)]] float3 pp : TEXCOORD1; [[vk::location(2)]] float2 st : TEXCOORD0; [[vk::location(3)]] float4 c : COLOR0; [[vk::location(4)]] uint xf : BLENDINDICES; };
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
