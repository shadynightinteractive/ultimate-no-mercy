// [vkwin 2026-11-12] The Vulkan backend's MSAA resolve, done by a shader instead of vkCmdResolveImage, so the frame's four samples a
// pixel come out as Direct3D 12's ResolveSubresource makes them. Measured on Windows (the same frames drawn by both): the
// copy resolve came out one level brighter than Direct3D 12 on about 7,300 edge pixels a frame (7,337 +1 against 313 -1).
// Here: the four samples as 8-bit values, summed, and the average rounded the way Direct3D 12's came out. Measured with the probe below on
// still pictures (WCW_NATIVE_GPU=both, the same frames): sum mod 4 = 0: 18,431,199 channels all equal; 1: 258 of 258 rounded down;
// 3: 318 of 318 rounded up; 2 (a half): 222 down, 3 up. So: up from 3 (pc.mode.y = 3, WCW_NATIVE_VK_RESOLVE_UP to try another).
// Drawn with arcade_view_post.vk.hlsl's vs (a triangle over the target) into scene_tex. (WCW_NATIVE_VK_RESOLVE=copy: the old way.)
// probe (pc.mode.x = 1, a test): the sum's last two bits per channel, x 64, in place of the colour.
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct ResolveC { float4 mode; };
[[vk::push_constant]] ResolveC pc;
[[vk::binding(0, 1)]] Texture2DMS<float4> frame_ms;
float4 ps_resolve(VO i) : SV_Target {
    const int2 p = int2(i.pos.xy);
    uint4 s = 0;
    [unroll] for (int k = 0; k < 4; k++) s += uint4(round(saturate(frame_ms.Load(p, k)) * 255.0));
    if (pc.mode.x > 0.5) return float4((s & 3u) * 64u) / 255.0;
    const uint up = uint(pc.mode.y);      // the sum's last two bits from which the average goes up a level: 3 (measured); 4 never, 2 half up
    return float4((s >> 2) + uint4((s & 3u) >= up)) / 255.0;
}
