// Satchel present filters (DM_PRESENT_UPSCALE edge and edge-spatial; NativeHost/graphics/README.md).
// Same interface as the pinned engine's present pass: a full-screen triangle and a crop record
// (rect: the valid source rectangle, normalized; edge: the clamp of sample positions to the
// valid texel centres). The source holds the scan-out's gamma bytes, as the present samples them.
// present_filter.py embeds this file in the generated renderer; quality/present-quality.swift
// compiles the same file on the Mac, so measured quality and cost come from this code.
#include <metal_stdlib>
using namespace metal;

struct V { float4 p [[position]]; float2 uv; };
struct Crop { float4 rect; float4 edge; };
// The smoothing pass's record adds the step between neighbour samples in source texels (x).
// The present uses 1. A longer step (the scan-out's width over a reduced 3D view's width)
// treats that view's stairs as one-texel stairs but also blurs a HUD drawn at full size; the
// Mac comparison tool measures both (present-quality --stride).
struct Smooth { float4 rect; float4 edge; float4 step; };

vertex V satchel_filter_vs(uint id [[vertex_id]]) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}

// The engine's bilinear present, unchanged (for the Mac comparison tool).
fragment float4 satchel_present_fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant Crop &c [[buffer(0)]]) {
    return t.sample(s, clamp(in.uv * c.rect.zw + c.rect.xy, c.edge.xy, c.edge.zw));
}

static inline float4 fetch(texture2d<float> t, sampler s, float2 q, float4 e) { return t.sample(s, clamp(q, e.xy, e.zw)); }
static inline float luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }

// Edge smoothing at source position u: find the local edge, its orientation and the side the
// step lies on, walk along the edge to both ends, and move the sample across the edge by how
// far this position is from the nearer end (a long, shallow stair gets a gradual blend). Thin
// isolated detail gets a bounded blend toward its 3x3 neighbourhood. Flat and low-contrast
// areas, which include most HUD text interiors and gradients, return the plain sample.
static float4 edge_smooth(texture2d<float> t, sampler s, float2 u, float4 e, float stride) {
    const float kRelative = 0.125, kAbsolute = 0.0312, kSubpixel = 0.75;
    float2 px = stride / float2(t.get_width(), t.get_height());
    float4 cM = fetch(t, s, u, e);
    float lM = luma(cM);
    float lN = luma(fetch(t, s, u + float2(0, -px.y), e)), lS = luma(fetch(t, s, u + float2(0, px.y), e));
    float lW = luma(fetch(t, s, u + float2(-px.x, 0), e)), lE = luma(fetch(t, s, u + float2(px.x, 0), e));
    float hi = max(max(max(lN, lS), max(lW, lE)), lM), lo = min(min(min(lN, lS), min(lW, lE)), lM);
    float range = hi - lo;
    if (range < max(kAbsolute, hi * kRelative)) return cM;
    float lNW = luma(fetch(t, s, u + float2(-px.x, -px.y), e)), lNE = luma(fetch(t, s, u + float2(px.x, -px.y), e));
    float lSW = luma(fetch(t, s, u + float2(-px.x, px.y), e)), lSE = luma(fetch(t, s, u + float2(px.x, px.y), e));
    // Second differences across x (a vertical edge line) and across y (a horizontal one).
    float acrossX = 2 * abs(lW + lE - 2 * lM) + abs(lNW + lNE - 2 * lN) + abs(lSW + lSE - 2 * lS);
    float acrossY = 2 * abs(lN + lS - 2 * lM) + abs(lNW + lSW - 2 * lW) + abs(lNE + lSE - 2 * lE);
    bool horizontal = acrossY >= acrossX;
    float lNeg = horizontal ? lN : lW, lPos = horizontal ? lS : lE;
    float gNeg = abs(lNeg - lM), gPos = abs(lPos - lM);
    float2 step = horizontal ? float2(0, px.y) : float2(px.x, 0);
    float2 along = horizontal ? float2(px.x, 0) : float2(0, px.y);
    float lPair = lPos, gradient = gPos;
    if (gNeg >= gPos) { step = -step; lPair = lNeg; gradient = gNeg; }
    float lEdge = 0.5 * (lM + lPair), stop = 0.25 * gradient;
    float2 mid = u + 0.5 * step;
    // Walk both ways along the boundary between this row and the neighbour row; a sample on the
    // boundary is their average, so it ends where the pair stops straddling the edge.
    const float strides[10] = {1, 1, 1, 1, 1.5, 2, 2, 2, 4, 8};
    float2 a = mid, b = mid; float dA = 0, dB = 0; bool doneA = false, doneB = false;
    for (int i = 0; i < 10 && !(doneA && doneB); i++) {
        if (!doneA) { a -= along * strides[i]; dA = luma(fetch(t, s, a, e)) - lEdge; doneA = abs(dA) >= stop; }
        if (!doneB) { b += along * strides[i]; dB = luma(fetch(t, s, b, e)) - lEdge; doneB = abs(dB) >= stop; }
    }
    float distA = horizontal ? (u.x - a.x) / px.x : (u.y - a.y) / px.y;
    float distB = horizontal ? (b.x - u.x) / px.x : (b.y - u.y) / px.y;
    bool nearA = distA < distB;
    float dEnd = nearA ? dA : dB;
    // The edge ends here only if the luma past that end moves away from this pixel's side.
    bool ends = (dEnd < 0) != (lM - lEdge < 0);
    float offset = ends ? 0.5 - min(distA, distB) / (distA + distB) : 0;
    float average = (2 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12;
    float sub = saturate(abs(average - lM) / range);
    sub = smoothstep(0.0, 1.0, sub); sub = sub * sub * kSubpixel;
    return fetch(t, s, u + step * max(offset, sub), e);
}

// Two-pass form: smooth at the source's own texel centres into a crop-sized texture, which the
// usual present (bilinear or MetalFX) then shows. rect maps the target to the valid crop.
fragment float4 satchel_edge_fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant Smooth &c [[buffer(0)]]) {
    return edge_smooth(t, s, in.uv * c.rect.zw + c.rect.xy, c.edge, max(c.step.x, 1.0));
}
