// [xplat 2026-11-12] The CRT / HDR last step for the Vulkan backend: the same HLSL as Direct3D 12's HLSL_POST (see arcade_view_scene.vk.hlsl);
// its constants come as a push constant, the scene picture and its sampler in sets 1 and 2 (set 0, the frame's, is bound but not used here).
// ps_blit is new: the plain copy to the screen when neither the CRT nor HDR is on (Direct3D 12 resolves straight into its back buffer; Vulkan's
// back buffer may be BGRA while the scene is RGBA, and a resolve can't swap them).
struct PostC { float4 region; float4 size; float4 mode; };
[[vk::push_constant]] PostC pc;
[[vk::binding(0, 1)]] Texture2D scene;
[[vk::binding(0, 2)]] SamplerState smp;
#define region pc.region
#define size pc.size
#define mode pc.mode
// ---- the same as Direct3D 12 ----
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

float4 ps_blit(VO i) : SV_Target { return float4(scene.Sample(smp, i.uv).rgb, 1); }     // (new, Vulkan only)
