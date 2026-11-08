// Satchel 3D view resample (DM_PRESENT_UPSCALE view; NativeHost/graphics/README.md, "3D view resample").
// view_upscale.py embeds this file in the generated renderer, which draws it in place of a game's own
// FSR 1 EASU draw from its reduced 3D view into the full-size target, before the game draws its HUD
// over that target. quality/view-upscale.swift compiles the same file on the Mac, so the measured
// quality and cost come from this code.
#include <metal_stdlib>
using namespace metal;

struct V { float4 p [[position]]; float2 uv; };
// size: the source view in texels (x, y) and their reciprocals (z, w).
struct ViewSource { float4 size; };

vertex V satchel_view_vs(uint id [[vertex_id]]) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}

// Catmull-Rom bicubic in nine bilinear taps (the middle two weights of each axis share one tap).
// The whole source maps to the whole target, texel centres to the matching positions, as the
// game's EASU maps them. The sampler clamps to the edge; negative lobes are clamped at zero
// (alpha to 0..1).
fragment float4 satchel_view_fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant ViewSource &c [[buffer(0)]]) {
    float2 q = in.uv * c.size.xy;
    float2 p1 = floor(q - 0.5) + 0.5, f = q - p1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 t0 = (p1 - 1) * c.size.zw, t3 = (p1 + 2) * c.size.zw, t12 = (p1 + w2 / w12) * c.size.zw;
    float4 r = 0;
    r += t.sample(s, float2(t0.x, t0.y)) * w0.x * w0.y;
    r += t.sample(s, float2(t12.x, t0.y)) * w12.x * w0.y;
    r += t.sample(s, float2(t3.x, t0.y)) * w3.x * w0.y;
    r += t.sample(s, float2(t0.x, t12.y)) * w0.x * w12.y;
    r += t.sample(s, float2(t12.x, t12.y)) * w12.x * w12.y;
    r += t.sample(s, float2(t3.x, t12.y)) * w3.x * w12.y;
    r += t.sample(s, float2(t0.x, t3.y)) * w0.x * w3.y;
    r += t.sample(s, float2(t12.x, t3.y)) * w12.x * w3.y;
    r += t.sample(s, float2(t3.x, t3.y)) * w3.x * w3.y;
    return float4(max(r.rgb, 0.0), saturate(r.a));
}
