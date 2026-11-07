import Foundation

// A compute tessellator for the domain shaders of Tessellation.swift. Written for Satchel
// from the OpenGL 4.6 specification's description of primitive generation (section 11.2.2),
// not ported: no MIT tessellator source was available on this machine. Integer (equal)
// spacing places vertices exactly as the specification defines them. Fractional spacing
// follows its rules (levels rounded up to even or odd, n - 2 equal segments and two
// shorter ones placed symmetrically next to the edge's centre), but the exact vertex
// positions the Switch's tessellator uses for fractional levels are not reproduced.
// Outer edge positions use 16.16 fixed point and mirror exactly about the edge's centre,
// so two patches sharing an edge at the same level produce the same points (no cracks).
// Counter-clockwise means a positive signed area in the (u, v) plane, u right and v up.
//
// Host use, per tessellated draw with P patches (records from the hull kernel):
//   dm_tess_count  P threads: each patch's vertex and index counts;
//   dm_tess_scan   one threadgroup (up to 1024 threads): offsets, the totals and the
//                  indirect draw arguments; patches past either capacity are dropped and
//                  counted, never truncated;
//   dm_tess_emit   P threads: domain points (u, v, w, patch as bits) and uint32 indices;
// then the domain vertex function draws the points with the index list (triangles,
// lines for isolines) or as points in point mode, through the totals' indirect arguments.
public enum SatchelShaderTessellator {
    public static let countFunction = "dm_tess_count", scanFunction = "dm_tess_scan", emitFunction = "dm_tess_emit"
    public static let paramsBytes = 32, rangeBytes = 16, totalsBytes = 48
    /// Byte offsets in the totals buffer of MTLDrawIndexedPrimitivesIndirectArguments and MTLDrawPrimitivesIndirectArguments.
    public static let indexedArgumentsOffset = 0, pointArgumentsOffset = 20
    public static let source = #"""
#include <metal_stdlib>
using namespace metal;

struct dm_tess_patch { float outer[4]; float inner[2]; float pad[2]; float4 user[31]; };
struct dm_tess_params {
    uint domain;        // 0 isolines, 1 triangles, 2 quads
    uint spacing;       // 0 equal, 1 fractional even, 2 fractional odd
    uint cw;            // 1: clockwise triangles in (u, v)
    uint point_mode;    // 1: points, no index list
    uint patches;
    uint max_vertices;  // capacity of the point buffer, in points
    uint max_indices;   // capacity of the index buffer, in indices
    uint pad;
};
struct dm_tess_range { uint first_vertex; uint vertices; uint first_index; uint indices; };
struct dm_tess_totals {
    uint index_count; uint instance_count; uint index_start; int base_vertex; uint base_instance;
    uint vertex_count; uint point_instance_count; uint vertex_start; uint point_base_instance;
    atomic_uint dropped; atomic_uint vertex_end; atomic_uint index_end;
};

struct dm_level { uint n; float f; };
static dm_level dm_level_of(float level, uint spacing) {
    dm_level l;
    if (isnan(level)) level = 0.0f;   // takes the lowest level
    if (spacing == 1) { l.f = clamp(level, 2.0f, 64.0f); l.n = uint(ceil(l.f * 0.5f)) * 2u; }
    else if (spacing == 2) { l.f = clamp(level, 1.0f, 63.0f); l.n = uint(ceil((l.f - 1.0f) * 0.5f)) * 2u + 1u; }
    else { l.n = uint(ceil(clamp(level, 1.0f, 64.0f))); l.f = float(l.n); }
    if (l.f >= float(l.n)) l.f = float(l.n);
    return l;
}
// Position of vertex k of an edge in 16.16 fixed point, mirrored about the centre.
static uint dm_tpos(uint k, dm_level l) {
    uint n = l.n;
    if (k == 0u) return 0u;
    if (k >= n) return 65536u;
    if (2u * k == n) return 32768u;
    bool mirror = 2u * k > n;
    uint j = mirror ? n - k : k, p;
    if (l.f >= float(n)) p = (j * 65536u + n / 2u) / n;
    else {
        // n - 2 segments of 1 / f and two of (f - n + 2) / 2f; the shorter ones are the
        // central pair (n even) or the pair beside the central segment (n odd).
        float a = 1.0f / l.f, b = (l.f - float(n) + 2.0f) / (2.0f * l.f);
        uint shortBefore = (n & 1u) == 0u ? (j >= n / 2u ? 1u : 0u) : (j >= (n - 1u) / 2u ? 1u : 0u);
        float prefix = float(j) * a - float(shortBefore) * (a - b);
        p = min(uint(rint(prefix * 65536.0f)), 32768u);
    }
    return mirror ? 65536u - p : p;
}
static dm_level dm_inner_ring(dm_level l, uint ring) {
    dm_level r; r.n = l.n - 2u * ring; r.f = min(l.f - 2.0f * float(ring), float(r.n));
    return r;
}

struct dm_sink {
    device float4* points; device uint* indices;
    uint first_vertex, first_index, nv, ni, patch, cw, point_mode, emit;
};
static void dm_vertex(thread dm_sink& o, float u, float v, float w) {
    if (o.emit) o.points[o.first_vertex + o.nv] = float4(u, v, w, as_type<float>(o.patch));
    o.nv++;
}
static void dm_tri(thread dm_sink& o, uint a, uint b, uint c) {
    if (o.point_mode) return;
    if (o.emit) {
        device uint* p = o.indices + o.first_index + o.ni;
        p[0] = o.first_vertex + a; p[1] = o.first_vertex + (o.cw ? c : b); p[2] = o.first_vertex + (o.cw ? b : c);
    }
    o.ni += 3u;
}
static void dm_line(thread dm_sink& o, uint a, uint b) {
    if (o.point_mode) return;
    if (o.emit) { device uint* p = o.indices + o.first_index + o.ni; p[0] = o.first_vertex + a; p[1] = o.first_vertex + b; }
    o.ni += 2u;
}

// A ring of patch vertices: kind 0 a closed loop of sides (corner first), 1 a single point,
// 2 a horizontal line of segs[0] segments, 3 a vertical line of segs[1] segments.
struct dm_ring { uint base; uint kind; uint sides; uint segs[4]; uint start[4]; };
static uint dm_ring_segs(thread const dm_ring& r, uint s) {
    if (r.kind == 1u) return 0u;
    if (r.kind == 2u) return (s & 1u) == 0u ? r.segs[0] : 0u;
    if (r.kind == 3u) return (s & 1u) == 1u ? r.segs[1] : 0u;
    return r.segs[s];
}
static uint dm_ring_vertex(thread const dm_ring& r, uint s, uint k) {
    if (r.kind == 1u) return r.base;
    if (r.kind == 2u) { uint m = r.segs[0]; return r.base + (s == 0u ? k : s == 1u ? m : s == 2u ? m - k : 0u); }
    if (r.kind == 3u) { uint m = r.segs[1]; return r.base + (s == 0u ? 0u : s == 1u ? k : s == 2u ? m : m - k); }
    if (k >= r.segs[s]) { s = s + 1u == r.sides ? 0u : s + 1u; k = 0u; }
    return r.base + r.start[s] + k;
}
static dm_ring dm_loop(uint base, uint sides, uint s0, uint s1, uint s2, uint s3) {
    dm_ring r; r.base = base; r.kind = 0u; r.sides = sides;
    r.segs[0] = s0; r.segs[1] = s1; r.segs[2] = s2; r.segs[3] = s3;
    r.start[0] = 0u; r.start[1] = s0; r.start[2] = s0 + s1; r.start[3] = s0 + s1 + s2;
    return r;
}
// Triangles between side s of ring A and side s of ring B, A outside B, both running
// counter-clockwise: a deterministic zipper by segment midpoints.
static void dm_zip(thread dm_sink& o, thread const dm_ring& A, thread const dm_ring& B, uint s) {
    uint a = dm_ring_segs(A, s), b = dm_ring_segs(B, s), i = 0u, j = 0u;
    while (i < a || j < b) {
        if (j == b || (i < a && (2u * i + 1u) * b <= (2u * j + 1u) * a)) {
            dm_tri(o, dm_ring_vertex(A, s, i), dm_ring_vertex(A, s, i + 1u), dm_ring_vertex(B, s, j)); i++;
        } else {
            dm_tri(o, dm_ring_vertex(A, s, i), dm_ring_vertex(B, s, j + 1u), dm_ring_vertex(B, s, j)); j++;
        }
    }
}
static bool dm_discarded(float x) { return !(x > 0.0f); }   // NaN or not positive

static void dm_triangles(thread dm_sink& o, const device dm_tess_patch& p, uint spacing) {
    if (dm_discarded(p.outer[0]) || dm_discarded(p.outer[1]) || dm_discarded(p.outer[2])) return;
    dm_level e0 = dm_level_of(p.outer[0], spacing), e1 = dm_level_of(p.outer[1], spacing), e2 = dm_level_of(p.outer[2], spacing);
    dm_level in = dm_level_of(p.inner[0], spacing);
    if (in.n == 1u && e0.n == 1u && e1.n == 1u && e2.n == 1u) {
        dm_vertex(o, 1.0f, 0.0f, 0.0f); dm_vertex(o, 0.0f, 1.0f, 0.0f); dm_vertex(o, 0.0f, 0.0f, 1.0f);
        dm_tri(o, 0u, 1u, 2u); return;
    }
    if (in.n == 1u) { in.n = spacing == 2u ? 3u : 2u; in.f = spacing == 2u ? 1.0f : 2.0f; }   // 1 + epsilon
    // Ring 0: side 0 from (1,0,0) to (0,1,0) is the w = 0 edge (outer 2), side 1 to (0,0,1)
    // the u = 0 edge (outer 0), side 2 back the v = 0 edge (outer 1).
    dm_ring outer = dm_loop(0u, 3u, e2.n, e0.n, e1.n, 0u);
    for (uint k = 0u; k < e2.n; k++) { float t = float(dm_tpos(k, e2)) / 65536.0f; dm_vertex(o, 1.0f - t, t, 0.0f); }
    for (uint k = 0u; k < e0.n; k++) { float t = float(dm_tpos(k, e0)) / 65536.0f; dm_vertex(o, 0.0f, 1.0f - t, t); }
    for (uint k = 0u; k < e1.n; k++) { float t = float(dm_tpos(k, e1)) / 65536.0f; dm_vertex(o, t, 0.0f, 1.0f - t); }
    const float3 corner[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
    dm_ring previous = outer;
    for (uint r = 1u; r <= in.n / 2u; r++) {
        uint m = in.n - 2u * r;
        dm_ring ring = {};
        if (m == 0u) { ring.base = o.nv; ring.kind = 1u; ring.sides = 3u; dm_vertex(o, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f); }
        else {
            // Corners where the perpendiculars through edge vertex r meet: 2 T_r of the way to the centre.
            float s = 2.0f * float(dm_tpos(r, in)) / 65536.0f;
            dm_level lm = dm_inner_ring(in, r);
            ring = dm_loop(o.nv, 3u, m, m, m, 0u);
            for (uint side = 0u; side < 3u; side++) {
                float3 a = mix(corner[side], float3(1.0f / 3.0f), s), b = mix(corner[(side + 1u) % 3u], float3(1.0f / 3.0f), s);
                for (uint k = 0u; k < m; k++) { float3 q = mix(a, b, float(dm_tpos(k, lm)) / 65536.0f); dm_vertex(o, q.x, q.y, 1.0f - q.x - q.y); }
            }
        }
        for (uint side = 0u; side < 3u; side++) dm_zip(o, previous, ring, side);
        if (m == 1u) dm_tri(o, dm_ring_vertex(ring, 0u, 0u), dm_ring_vertex(ring, 1u, 0u), dm_ring_vertex(ring, 2u, 0u));
        previous = ring;
    }
}

static void dm_quads(thread dm_sink& o, const device dm_tess_patch& p, uint spacing) {
    for (uint i = 0u; i < 4u; i++) if (dm_discarded(p.outer[i])) return;
    dm_level e0 = dm_level_of(p.outer[0], spacing), e1 = dm_level_of(p.outer[1], spacing);
    dm_level e2 = dm_level_of(p.outer[2], spacing), e3 = dm_level_of(p.outer[3], spacing);
    dm_level i0 = dm_level_of(p.inner[0], spacing), i1 = dm_level_of(p.inner[1], spacing);
    if (i0.n == 1u && i1.n == 1u && e0.n == 1u && e1.n == 1u && e2.n == 1u && e3.n == 1u) {
        dm_vertex(o, 0, 0, 0); dm_vertex(o, 1, 0, 0); dm_vertex(o, 1, 1, 0); dm_vertex(o, 0, 1, 0);
        dm_tri(o, 0u, 1u, 2u); dm_tri(o, 0u, 2u, 3u); return;
    }
    if (i0.n == 1u) { i0.n = spacing == 2u ? 3u : 2u; i0.f = spacing == 2u ? 1.0f : 2.0f; }
    if (i1.n == 1u) { i1.n = spacing == 2u ? 3u : 2u; i1.f = spacing == 2u ? 1.0f : 2.0f; }
    // Ring 0 from (0,0): side 0 the v = 0 edge (outer 1), side 1 u = 1 (outer 2), side 2 v = 1 (outer 3), side 3 u = 0 (outer 0).
    dm_ring previous = dm_loop(0u, 4u, e1.n, e2.n, e3.n, e0.n);
    for (uint k = 0u; k < e1.n; k++) dm_vertex(o, float(dm_tpos(k, e1)) / 65536.0f, 0.0f, 0.0f);
    for (uint k = 0u; k < e2.n; k++) dm_vertex(o, 1.0f, float(dm_tpos(k, e2)) / 65536.0f, 0.0f);
    for (uint k = 0u; k < e3.n; k++) dm_vertex(o, 1.0f - float(dm_tpos(k, e3)) / 65536.0f, 1.0f, 0.0f);
    for (uint k = 0u; k < e0.n; k++) dm_vertex(o, 0.0f, 1.0f - float(dm_tpos(k, e0)) / 65536.0f, 0.0f);
    uint rings = min(i0.n, i1.n) / 2u;
    for (uint r = 1u; r <= rings; r++) {
        uint m0 = i0.n - 2u * r, m1 = i1.n - 2u * r;
        float U = float(dm_tpos(r, i0)) / 65536.0f, V = float(dm_tpos(r, i1)) / 65536.0f;
        dm_level h = dm_inner_ring(i0, r), w = dm_inner_ring(i1, r);
        dm_ring ring = {};
        if (m0 == 0u && m1 == 0u) { ring.base = o.nv; ring.kind = 1u; ring.sides = 4u; dm_vertex(o, 0.5f, 0.5f, 0.0f); }
        else if (m1 == 0u) {
            ring.base = o.nv; ring.kind = 2u; ring.sides = 4u; ring.segs[0] = m0; ring.segs[1] = 0u;
            for (uint k = 0u; k <= m0; k++) dm_vertex(o, mix(U, 1.0f - U, float(dm_tpos(k, h)) / 65536.0f), 0.5f, 0.0f);
        } else if (m0 == 0u) {
            ring.base = o.nv; ring.kind = 3u; ring.sides = 4u; ring.segs[0] = 0u; ring.segs[1] = m1;
            for (uint k = 0u; k <= m1; k++) dm_vertex(o, 0.5f, mix(V, 1.0f - V, float(dm_tpos(k, w)) / 65536.0f), 0.0f);
        } else {
            ring = dm_loop(o.nv, 4u, m0, m1, m0, m1);
            for (uint k = 0u; k < m0; k++) dm_vertex(o, mix(U, 1.0f - U, float(dm_tpos(k, h)) / 65536.0f), V, 0.0f);
            for (uint k = 0u; k < m1; k++) dm_vertex(o, 1.0f - U, mix(V, 1.0f - V, float(dm_tpos(k, w)) / 65536.0f), 0.0f);
            for (uint k = 0u; k < m0; k++) dm_vertex(o, mix(1.0f - U, U, float(dm_tpos(k, h)) / 65536.0f), 1.0f - V, 0.0f);
            for (uint k = 0u; k < m1; k++) dm_vertex(o, U, mix(1.0f - V, V, float(dm_tpos(k, w)) / 65536.0f), 0.0f);
        }
        for (uint side = 0u; side < 4u; side++) dm_zip(o, previous, ring, side);
        if (r == rings && ring.kind == 0u) {   // the innermost ring is one cell wide: fill it
            if (m1 == 1u) for (uint k = 0u; k < m0; k++) {
                uint a = dm_ring_vertex(ring, 0u, k), b = dm_ring_vertex(ring, 0u, k + 1u);
                uint c = dm_ring_vertex(ring, 2u, m0 - k), d = dm_ring_vertex(ring, 2u, m0 - k - 1u);
                dm_tri(o, a, b, c); dm_tri(o, b, d, c);
            } else for (uint k = 0u; k < m1; k++) {
                uint a = dm_ring_vertex(ring, 1u, k), b = dm_ring_vertex(ring, 1u, k + 1u);
                uint c = dm_ring_vertex(ring, 3u, m1 - k), d = dm_ring_vertex(ring, 3u, m1 - k - 1u);
                dm_tri(o, a, b, c); dm_tri(o, b, d, c);
            }
        }
        previous = ring;
    }
}

static void dm_isolines(thread dm_sink& o, const device dm_tess_patch& p, uint spacing) {
    if (dm_discarded(p.outer[0]) || dm_discarded(p.outer[1])) return;
    uint lines = dm_level_of(p.outer[0], 0u).n;   // the line count is always integer spaced
    dm_level d = dm_level_of(p.outer[1], spacing);
    for (uint i = 0u; i < lines; i++) {
        float v = float((i * 65536u + lines / 2u) / lines) / 65536.0f;
        for (uint k = 0u; k <= d.n; k++) dm_vertex(o, float(dm_tpos(k, d)) / 65536.0f, v, 0.0f);
        for (uint k = 0u; k < d.n; k++) dm_line(o, i * (d.n + 1u) + k, i * (d.n + 1u) + k + 1u);
    }
}

static void dm_patch_run(thread dm_sink& o, const device dm_tess_patch& p, constant dm_tess_params& params) {
    if (params.domain == 1u) dm_triangles(o, p, params.spacing);
    else if (params.domain == 2u) dm_quads(o, p, params.spacing);
    else dm_isolines(o, p, params.spacing);
}

kernel void dm_tess_count(const device dm_tess_patch* patches [[buffer(0)]], constant dm_tess_params& params [[buffer(1)]],
                          device dm_tess_range* ranges [[buffer(2)]], uint id [[thread_position_in_grid]]) {
    if (id >= params.patches) return;
    dm_sink o = {}; o.patch = id; o.cw = params.cw; o.point_mode = params.point_mode; o.emit = 0u;
    dm_patch_run(o, patches[id], params);
    ranges[id].vertices = o.nv; ranges[id].indices = o.ni;
}

kernel void dm_tess_scan(constant dm_tess_params& params [[buffer(1)]], device dm_tess_range* ranges [[buffer(2)]],
                         device dm_tess_totals& totals [[buffer(3)]], uint t [[thread_index_in_threadgroup]],
                         uint threads [[threads_per_threadgroup]]) {
    threadgroup uint2 sums[1024];
    uint n = params.patches, per = (n + threads - 1u) / threads, begin = min(t * per, n), end = min(begin + per, n);
    if (t == 0u) {
        atomic_store_explicit(&totals.dropped, 0u, memory_order_relaxed);
        atomic_store_explicit(&totals.vertex_end, 0u, memory_order_relaxed);
        atomic_store_explicit(&totals.index_end, 0u, memory_order_relaxed);
    }
    uint2 sum = uint2(0u);
    for (uint i = begin; i < end; i++) sum += uint2(ranges[i].vertices, ranges[i].indices);
    sums[t] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup | mem_flags::mem_device);
    if (t == 0u) { uint2 run = uint2(0u); for (uint i = 0u; i < threads; i++) { uint2 v = sums[i]; sums[i] = run; run += v; } }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    // Offsets only grow, so the accepted patches are a prefix and the arguments cover them all.
    uint2 at = sums[t];
    for (uint i = begin; i < end; i++) {
        uint2 count = uint2(ranges[i].vertices, ranges[i].indices);
        if (count.x > params.max_vertices - min(at.x, params.max_vertices) || count.y > params.max_indices - min(at.y, params.max_indices)) {
            if (count.x) atomic_fetch_add_explicit(&totals.dropped, 1u, memory_order_relaxed);
            ranges[i].vertices = 0u; ranges[i].indices = 0u; ranges[i].first_vertex = 0u; ranges[i].first_index = 0u;
        } else {
            ranges[i].first_vertex = at.x; ranges[i].first_index = at.y;
            if (count.x) atomic_fetch_max_explicit(&totals.vertex_end, at.x + count.x, memory_order_relaxed);
            if (count.y) atomic_fetch_max_explicit(&totals.index_end, at.y + count.y, memory_order_relaxed);
        }
        at += count;
    }
    threadgroup_barrier(mem_flags::mem_device);
    if (t == 0u) {
        uint vertices = atomic_load_explicit(&totals.vertex_end, memory_order_relaxed), indices = atomic_load_explicit(&totals.index_end, memory_order_relaxed);
        totals.index_count = indices; totals.instance_count = 1u; totals.index_start = 0u; totals.base_vertex = 0; totals.base_instance = 0u;
        totals.vertex_count = vertices; totals.point_instance_count = 1u; totals.vertex_start = 0u; totals.point_base_instance = 0u;
    }
}

kernel void dm_tess_emit(const device dm_tess_patch* patches [[buffer(0)]], constant dm_tess_params& params [[buffer(1)]],
                         const device dm_tess_range* ranges [[buffer(2)]], device float4* points [[buffer(4)]],
                         device uint* indices [[buffer(5)]], uint id [[thread_position_in_grid]]) {
    if (id >= params.patches || ranges[id].vertices == 0u) return;
    dm_sink o = {}; o.points = points; o.indices = indices; o.first_vertex = ranges[id].first_vertex; o.first_index = ranges[id].first_index;
    o.patch = id; o.cw = params.cw; o.point_mode = params.point_mode; o.emit = 1u;
    dm_patch_run(o, patches[id], params);
}
"""#
}
