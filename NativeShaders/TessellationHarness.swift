import Foundation
import Metal

// Mac GPU checks for the tessellation route (Tessellation.swift, Tessellator.swift).
// Inputs are only the public synthetic corpus rows (public_corpus.py) on stdin; only
// counts and pass or fail leave this harness, never shader text.
// 1. Tessellator: every domain, spacing, winding and point mode over a grid of levels:
//    bounds, barycentric sums, orientation, total area, a closed edge manifold whose
//    boundary lies on the domain's edges, the outer edge subdivision (exact for equal
//    spacing, mirrored and correctly counted for fractional), discards and capacity drops.
// 2. Translated chain: a vertex shader's capture entry, a translated hull kernel, the
//    tessellator and a translated domain shader drawn into four float targets, against a
//    reference draw of the same patches: identical coverage with back faces culled, and
//    the control point, patch and level values the domain shader read.
// 3. Interface refusals: unknown shapes fail explicitly instead of being dropped.

struct HarnessFailure: Error { let message: String }
func require(_ condition: Bool, _ message: @autoclosure () -> String) throws {
    if !condition { throw HarnessFailure(message: message()) }
}

final class GPU {
    let device: MTLDevice, queue: MTLCommandQueue, tess: MTLLibrary
    let count: MTLComputePipelineState, scan: MTLComputePipelineState, emit: MTLComputePipelineState
    init() throws {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { throw HarnessFailure(message: "no Metal device") }
        self.device = device; self.queue = queue
        let library = try device.makeLibrary(source: SatchelShaderTessellator.source, options: nil)
        func pipeline(_ name: String) throws -> MTLComputePipelineState {
            guard let f = library.makeFunction(name: name) else { throw HarnessFailure(message: "missing tessellator function") }
            return try device.makeComputePipelineState(function: f)
        }
        tess = library
        count = try pipeline(SatchelShaderTessellator.countFunction)
        scan = try pipeline(SatchelShaderTessellator.scanFunction)
        emit = try pipeline(SatchelShaderTessellator.emitFunction)
    }
    func buffer(_ length: Int) -> MTLBuffer { device.makeBuffer(length: max(length, 16), options: .storageModeShared)! }
    func buffer<T>(_ values: [T]) -> MTLBuffer {
        values.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: max($0.count, 16), options: .storageModeShared)! }
    }
}

struct TessOutput {
    var ranges: [(firstVertex: Int, vertices: Int, firstIndex: Int, indices: Int)] = []
    var points: [SIMD4<Float>] = [], indices: [UInt32] = []
    var indexCount = 0, vertexCount = 0, dropped = 0
}
struct TessMode { var domain: UInt32, spacing: UInt32, cw: Bool, points: Bool }

// Runs the three tessellator kernels over patch records already in `patches`.
func tessellate(_ gpu: GPU, _ mode: TessMode, patches: MTLBuffer, count n: Int, maxVertices: Int = 1 << 22, maxIndices: Int = 1 << 24,
                into command: MTLCommandBuffer) -> (MTLBuffer, MTLBuffer, MTLBuffer, MTLBuffer) {
    var params: [UInt32] = [mode.domain, mode.spacing, mode.cw ? 1 : 0, mode.points ? 1 : 0, UInt32(n), UInt32(maxVertices), UInt32(maxIndices), 0]
    let ranges = gpu.buffer(16 * max(n, 1)), totals = gpu.buffer(SatchelShaderTessellator.totalsBytes)
    let points = gpu.buffer(16 * min(maxVertices, 1 << 22)), indices = gpu.buffer(4 * min(maxIndices, 1 << 24))
    let encoder = command.makeComputeCommandEncoder()!
    encoder.setBuffer(patches, offset: 0, index: 0); encoder.setBytes(&params, length: 32, index: 1)
    encoder.setBuffer(ranges, offset: 0, index: 2); encoder.setBuffer(totals, offset: 0, index: 3)
    encoder.setBuffer(points, offset: 0, index: 4); encoder.setBuffer(indices, offset: 0, index: 5)
    let groups = MTLSize(width: (max(n, 1) + 63) / 64, height: 1, depth: 1), width = MTLSize(width: 64, height: 1, depth: 1)
    encoder.setComputePipelineState(gpu.count); encoder.dispatchThreadgroups(groups, threadsPerThreadgroup: width)
    encoder.memoryBarrier(scope: .buffers)
    encoder.setComputePipelineState(gpu.scan)
    encoder.dispatchThreadgroups(MTLSize(width: 1, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 256, height: 1, depth: 1))
    encoder.memoryBarrier(scope: .buffers)
    encoder.setComputePipelineState(gpu.emit); encoder.dispatchThreadgroups(groups, threadsPerThreadgroup: width)
    encoder.endEncoding()
    return (ranges, totals, points, indices)
}
func read(_ ranges: MTLBuffer, _ totals: MTLBuffer, _ points: MTLBuffer, _ indices: MTLBuffer, patches n: Int) -> TessOutput {
    var out = TessOutput()
    let r = ranges.contents().bindMemory(to: UInt32.self, capacity: 4 * max(n, 1))
    for i in 0..<n { out.ranges.append((Int(r[4 * i]), Int(r[4 * i + 1]), Int(r[4 * i + 2]), Int(r[4 * i + 3]))) }
    let t = totals.contents().bindMemory(to: UInt32.self, capacity: 12)
    out.indexCount = Int(t[0]); out.vertexCount = Int(t[5]); out.dropped = Int(t[9])
    let p = points.contents().bindMemory(to: SIMD4<Float>.self, capacity: out.vertexCount)
    out.points = (0..<out.vertexCount).map { p[$0] }
    let x = indices.contents().bindMemory(to: UInt32.self, capacity: out.indexCount)
    out.indices = (0..<out.indexCount).map { x[$0] }
    return out
}
func patchRecord(outer: [Float], inner: [Float]) -> [Float] {
    var record = [Float](repeating: 0, count: ShaderTessellation.patchBytes / 4)
    for i in 0..<4 { record[i] = outer[i] }
    record[4] = inner[0]; record[5] = inner[1]
    return record
}

// The rule Tessellator.swift implements, restated for the boundary checks.
func expectedEdge(_ level: Float, _ spacing: UInt32) -> (n: Int, f: Float) {
    let level = level.isNaN ? 0 : level
    switch spacing {
    case 1: let f = min(max(level, 2), 64); let n = Int((f / 2).rounded(.up)) * 2; return (n, min(f, Float(n)))
    case 2: let f = min(max(level, 1), 63); let n = Int(((f - 1) / 2).rounded(.up)) * 2 + 1; return (n, min(f, Float(n)))
    default: let n = Int(min(max(level, 1), 64).rounded(.up)); return (n, Float(n))
    }
}

var tessellatorCases = 0, tessellatorPatches = 0, triangles = 0, lines = 0
func checkPatch(_ out: TessOutput, patch i: Int, mode: TessMode, outer: [Float], inner: [Float]) throws {
    let range = out.ranges[i]
    let label = "domain \(mode.domain) spacing \(mode.spacing) cw \(mode.cw) points \(mode.points) outer \(outer) inner \(inner)"
    let relevant = mode.domain == 1 ? Array(outer[0..<3]) : mode.domain == 2 ? outer : Array(outer[0..<2])
    if relevant.contains(where: { !($0 > 0) }) { try require(range.vertices == 0 && range.indices == 0, "discarded patch emitted: \(label)"); return }
    try require(range.vertices > 0, "empty patch: \(label)")
    let pts = Array(out.points[range.firstVertex..<range.firstVertex + range.vertices])
    for p in pts {
        try require(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1 && p.z >= 0 && p.z <= 1, "point outside the domain: \(label)")
        try require(p.w.bitPattern == UInt32(i), "point without its patch: \(label)")
        if mode.domain == 1 { try require(abs(p.x + p.y + p.z - 1) < 2e-6, "barycentric sum: \(label)") } else { try require(p.z == 0, "w in a quad: \(label)") }
    }
    if mode.points { try require(range.indices == 0, "indices in point mode: \(label)"); return }
    let idx = out.indices[range.firstIndex..<range.firstIndex + range.indices].map { Int($0) - range.firstVertex }
    try require(idx.allSatisfy { $0 >= 0 && $0 < range.vertices }, "index outside its patch: \(label)")
    func onBoundary(_ p: SIMD4<Float>) -> [Int] {   // which outer edges (level numbering) a point lies on
        if mode.domain == 1 { return [p.x == 0 ? 0 : -1, p.y == 0 ? 1 : -1, p.z == 0 ? 2 : -1].filter { $0 >= 0 } }
        return [p.x == 0 ? 0 : -1, p.y == 0 ? 1 : -1, p.x == 1 ? 2 : -1, p.y == 1 ? 3 : -1].filter { $0 >= 0 }
    }
    if mode.domain == 0 {
        let count = expectedEdge(outer[0], 0).n, d = expectedEdge(outer[1], mode.spacing)
        try require(range.vertices == count * (d.n + 1) && range.indices == 2 * count * d.n, "isoline counts: \(label)")
        for k in 0..<count { try require(pts[k * (d.n + 1)].y == Float((k * 65536 + count / 2) / count) / 65536, "isoline spacing: \(label)") }
        lines += count * d.n; return
    }
    try require(range.indices % 3 == 0, "partial triangle: \(label)")
    var area: Double = 0, directed: [Int: Int] = [:]
    for t in stride(from: 0, to: idx.count, by: 3) {
        let a = pts[idx[t]], b = pts[idx[t + 1]], c = pts[idx[t + 2]]
        let signed = (Double(b.x - a.x) * Double(c.y - a.y) - Double(c.x - a.x) * Double(b.y - a.y)) / 2
        let oriented = mode.cw ? -signed : signed
        try require(oriented >= -1e-9, "triangle against the winding: \(label)")
        area += oriented
        for (u, v) in [(idx[t], idx[t + 1]), (idx[t + 1], idx[t + 2]), (idx[t + 2], idx[t])] {
            try require(u != v, "repeated vertex in a triangle: \(label)")
            try require(directed[u * 65536 + v] == nil, "edge used twice in one direction: \(label)")
            directed[u * 65536 + v] = 1
        }
    }
    try require(abs(area - (mode.domain == 1 ? 0.5 : 1.0)) < 1e-5, "covered area \(area): \(label)")
    // Every edge without its reverse is on the domain boundary, both ends on one outer edge.
    var boundary: [Int: Set<Int>] = [:]
    for key in directed.keys where directed[(key % 65536) * 65536 + key / 65536] == nil {
        let common = Set(onBoundary(pts[key / 65536])).intersection(onBoundary(pts[key % 65536]))
        try require(!common.isEmpty, "open edge inside the domain: \(label)")
        for e in common { boundary[e, default: []].formUnion([key / 65536, key % 65536]) }
    }
    let levels = mode.domain == 1 ? Array(outer[0..<3]) : outer
    let innerOne = mode.domain == 1 ? expectedEdge(inner[0], mode.spacing).n == 1 : expectedEdge(inner[0], mode.spacing).n == 1 && expectedEdge(inner[1], mode.spacing).n == 1
    let single = mode.spacing != 1 && innerOne && levels.allSatisfy { expectedEdge($0, mode.spacing).n == 1 }
    for (e, level) in levels.enumerated() {
        // Parameter along outer edge e, measured from the corner its numbering starts at.
        let along: (SIMD4<Float>) -> Float = mode.domain == 1 ? [{ $0.z }, { $0.x }, { $0.y }][e] : [{ 1 - $0.y }, { $0.x }, { $0.y }, { 1 - $0.x }][e]
        let ts = (boundary[e] ?? []).map { along(pts[$0]) }.sorted()
        let expected = expectedEdge(level, mode.spacing), n = single ? 1 : expected.n
        try require(ts.count == n + 1 || (expected.f < Float(expected.n) && Set(ts).count <= n + 1), "outer edge \(e) has \(ts.count) points, expected \(n + 1): \(label)")
        if expected.f >= Float(expected.n) || single {
            for (k, t) in ts.enumerated() { try require(abs(t - Float(k) / Float(n)) <= 0.5 / 65536 + 1e-7 && t + ts[n - k] == 1, "outer edge \(e) point \(k) at \(t): \(label)") }
        } else {
            for k in 0..<ts.count { try require(ts[k] + ts[ts.count - 1 - k] == 1, "outer edge \(e) not mirrored: \(label)") }
            let segments = zip(ts.dropFirst(), ts).map { $0 - $1 }, full = 1 / expected.f
            let long = segments.filter { abs($0 - full) <= 2.0 / 65536 }.count
            try require(long >= expected.n - 2, "outer edge \(e) has \(long) full segments of \(expected.n): \(label)")
        }
    }
    triangles += idx.count / 3
}

func tessellatorChecks(_ gpu: GPU) throws {
    let levels: [Float] = [1, 1.25, 2, 2.5, 3, 4, 5.75, 7, 16.5, 63.5, 64, 100]
    var cases: [(TessMode, [Float], [Float])] = []
    for domain in [UInt32(1), 2] { for spacing in [UInt32(0), 1, 2] { for (j, o) in levels.enumerated() { for (k, i) in levels.enumerated() where (j + k) % 3 == 0 || j == k {
        let skew = levels[(j + 3) % levels.count], other = levels[(k + 5) % levels.count]
        cases.append((TessMode(domain: domain, spacing: spacing, cw: (j + k) % 2 == 1, points: false), [o, skew, i, other], [i, skew]))
    } } } }
    for spacing in [UInt32(0), 1, 2] { for o in levels { for d in levels {
        cases.append((TessMode(domain: 0, spacing: spacing, cw: false, points: false), [o, d, 0, 0], [0, 0]))
    } } }
    for domain in [UInt32(0), 1, 2] {   // point mode, discards and a NaN level
        cases.append((TessMode(domain: domain, spacing: 2, cw: false, points: true), [3.5, 2, 7, 5], [4.5, 2]))
        for bad in [Float(0), -1, .nan] { cases.append((TessMode(domain: domain, spacing: 0, cw: false, points: false), [bad, 2, 3, 4], [2, 2])) }
    }
    cases.append((TessMode(domain: 1, spacing: 0, cw: false, points: false), [2, 3, 4, 0], [.nan, 0]))
    // Grouped by mode, many patches per dispatch, as the host would run them.
    var groups: [String: [(TessMode, [Float], [Float])]] = [:]
    for c in cases { groups["\(c.0.domain)-\(c.0.spacing)-\(c.0.cw)-\(c.0.points)", default: []].append(c) }
    for (_, group) in groups.sorted(by: { $0.key < $1.key }) {
        let records = gpu.buffer(group.flatMap { patchRecord(outer: $0.1, inner: $0.2) })
        let command = gpu.queue.makeCommandBuffer()!
        let (r, t, p, x) = tessellate(gpu, group[0].0, patches: records, count: group.count, into: command)
        command.commit(); command.waitUntilCompleted()
        try require(command.status == .completed, "tessellator command failed")
        let out = read(r, t, p, x, patches: group.count)
        try require(out.dropped == 0 && out.vertexCount == out.ranges.map { $0.vertices }.reduce(0, +)
                    && out.indexCount == out.ranges.map { $0.indices }.reduce(0, +), "tessellator totals")
        for (i, c) in group.enumerated() { try checkPatch(out, patch: i, mode: c.0, outer: c.1, inner: c.2) }
        tessellatorCases += 1; tessellatorPatches += group.count
    }
    // Two quads sharing an edge at a fractional level produce the same points along it.
    let shared: [Float] = [5.5, 3, 9.25, 3]
    let records = gpu.buffer(patchRecord(outer: shared, inner: [4, 4]) + patchRecord(outer: [9.25, 3, 5.5, 3], inner: [6, 2]))
    var command = gpu.queue.makeCommandBuffer()!
    var (r, t, p, x) = tessellate(gpu, TessMode(domain: 2, spacing: 2, cw: false, points: false), patches: records, count: 2, into: command)
    command.commit(); command.waitUntilCompleted()
    var out = read(r, t, p, x, patches: 2)
    let right = Set(out.points[out.ranges[0].firstVertex..<out.ranges[0].firstVertex + out.ranges[0].vertices].filter { $0.x == 1 }.map { $0.y })
    let left = Set(out.points[out.ranges[1].firstVertex..<out.ranges[1].firstVertex + out.ranges[1].vertices].filter { $0.x == 0 }.map { $0.y })
    try require(right == left && right.count == expectedEdge(9.25, 2).n + 1, "shared edge points differ")
    // A full point buffer drops whole patches from the end and the arguments cover the rest.
    let many = gpu.buffer((0..<8).flatMap { _ in patchRecord(outer: [8, 8, 8, 8], inner: [8, 8]) })
    command = gpu.queue.makeCommandBuffer()!
    (r, t, p, x) = tessellate(gpu, TessMode(domain: 2, spacing: 0, cw: false, points: false), patches: many, count: 8, maxVertices: 300, into: command)
    command.commit(); command.waitUntilCompleted()
    out = read(r, t, p, x, patches: 8)
    let kept = out.ranges.filter { $0.vertices > 0 }
    try require(out.dropped > 0 && out.dropped + kept.count == 8 && out.vertexCount == kept.map { $0.vertices }.reduce(0, +)
                && out.vertexCount <= 300 && out.indexCount == kept.map { $0.indices }.reduce(0, +), "capacity drop")
    tessellatorCases += 2; tessellatorPatches += 10
}

// The translated chain. The vertex shader is written here, as the host's capture input.
let vertexSource = """
#include <metal_stdlib>
using namespace metal;
struct main0_out
{
    float4 m_0 [[user(locn0)]];
    float4 gl_Position [[position]];
};
vertex main0_out main0(uint gl_VertexIndex [[vertex_id]], const device float4* dm_test_position [[buffer(1)]])
{
    main0_out out = {};
    out.m_0 = float4(float(gl_VertexIndex) + 0.5, float(gl_VertexIndex) * 2.0, 3.0, 4.0);
    out.gl_Position = dm_test_position[gl_VertexIndex];
    return out;
}
"""
let fragmentSource = """
#include <metal_stdlib>
using namespace metal;
struct dm_in { float4 l0 [[user(locn0)]]; float4 l1 [[user(locn1)]]; float4 l2 [[user(locn2), flat]]; float4 l3 [[user(locn3), flat]]; };
struct dm_out { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; float4 c3 [[color(3)]]; };
fragment dm_out dm_test_fragment(dm_in in [[stage_in]]) { dm_out o; o.c0 = in.l0; o.c1 = in.l1; o.c2 = in.l2; o.c3 = in.l3; return o; }
struct dm_ref { float4 position [[position]]; uint patch [[user(locn0), flat]]; };
vertex dm_ref dm_ref_vertex(uint v [[vertex_id]], const device float4* position [[buffer(1)]], constant uint& per [[buffer(2)]]) {
    dm_ref o; o.position = position[v]; o.patch = v / per + 1u; return o;
}
fragment uint4 dm_ref_fragment(dm_ref in [[stage_in]]) { return uint4(in.patch, 0u, 0u, 1u); }
"""

var chainCases = 0, chainPixels = 0
func chain(_ gpu: GPU, hull: PreparedShaderSources, domain: PreparedShaderSources, patches: Int, hullOuter: [Float], hullInner: [Float]) throws {
    let size = 96
    guard let ht = hull.reflection["tessellation"] as? [String: Any], let dt = domain.reflection["tessellation"] as? [String: Any],
          let cps = ht["output_vertices"] as? Int, let inputBytes = ht["input_bytes_per_vertex"] as? Int,
          let words = ht["capture_words"] as? [Int], let domainName = dt["domain"] as? String, let spacingName = dt["spacing"] as? String,
          let winding = dt["winding"] as? String else { throw HarnessFailure(message: "tessellation reflection") }
    let mode = TessMode(domain: ["isolines": 0, "triangles": 1, "quads": 2][domainName]!, spacing: ["equal": 0, "fractional_even": 1, "fractional_odd": 2][spacingName]!,
                        cw: winding == "cw", points: false)
    let label = "\(domainName) \(spacingName) \(winding)"
    // Patch p's control points: a triangle or parallelogram, counter-clockwise in clip space.
    var positions: [SIMD4<Float>] = []
    for p in 0..<patches {
        let x0: Float = p == 0 ? -0.913 : 0.071, y0: Float = p == 0 ? -0.877 : 0.043, a: Float = 0.807, b: Float = 0.789
        positions += cps == 3 ? [SIMD4(x0 + a, y0, 0, 1), SIMD4(x0, y0 + b, 0, 1), SIMD4(x0, y0, 0, 1)]
                              : [SIMD4(x0, y0, 0, 1), SIMD4(x0 + a, y0, 0, 1), SIMD4(x0 + a, y0 + b, 0, 1), SIMD4(x0, y0 + b, 0, 1)]
    }
    let vertexLibrary = try gpu.device.makeLibrary(source: vertexSource + (try ShaderTransforms.capture(vertexSource)), options: nil)
    let hullLibrary = try gpu.device.makeLibrary(source: hull.sources["b"]!, options: nil)
    let domainLibrary = try gpu.device.makeLibrary(source: domain.sources["b"]!, options: nil)
    let testLibrary = try gpu.device.makeLibrary(source: fragmentSource, options: nil)
    let positionBuffer = gpu.buffer(positions)
    // 1. Capture: slot k of the 144-word map holds the hull's input word k.
    var slots = [Int16](repeating: -1, count: 144)
    for (i, w) in words.enumerated() { slots[w] = Int16(i) }
    let vertices = patches * cps, capture = gpu.buffer(4 * words.count * vertices)
    func captureBytes() -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: 336)
        withUnsafeBytes(of: capture.gpuAddress) { bytes.replaceSubrange(0..<8, with: $0) }
        let info: [UInt32] = [0, UInt32(vertices), 0, 1, UInt32(words.count)]
        info.withUnsafeBytes { bytes.replaceSubrange(16..<36, with: $0) }
        slots.withUnsafeBytes { bytes.replaceSubrange(36..<324, with: $0) }
        return bytes
    }
    var captureInfo = captureBytes()
    let capturePipeline = MTLRenderPipelineDescriptor()
    capturePipeline.vertexFunction = vertexLibrary.makeFunction(name: "main0_gs"); capturePipeline.isRasterizationEnabled = false
    let captureState = try gpu.device.makeRenderPipelineState(descriptor: capturePipeline)
    let command = gpu.queue.makeCommandBuffer()!
    let pass = MTLRenderPassDescriptor(); pass.renderTargetWidth = size; pass.renderTargetHeight = size; pass.defaultRasterSampleCount = 1
    var render = command.makeRenderCommandEncoder(descriptor: pass)!
    render.setRenderPipelineState(captureState); render.setVertexBuffer(positionBuffer, offset: 0, index: 1)
    render.setVertexBytes(&captureInfo, length: captureInfo.count, index: 13)
    render.useResource(capture, usage: .write, stages: .vertex)
    render.drawPrimitives(type: .point, vertexStart: 0, vertexCount: vertices); render.endEncoding()
    // 2. Hull: one threadgroup per patch.
    let hullState = try gpu.device.makeComputePipelineState(function: hullLibrary.makeFunction(name: "main0")!)
    let records = gpu.buffer(ShaderTessellation.patchBytes * patches), controlPoints = gpu.buffer(ShaderTessellation.controlPointBytes * cps * patches)
    var params: [UInt32] = [UInt32(cps), UInt32(patches)], input = [UInt8](repeating: 0, count: 16)
    let compute = command.makeComputeCommandEncoder()!
    compute.setComputePipelineState(hullState)
    compute.setBuffer(records, offset: 0, index: 26); compute.setBuffer(controlPoints, offset: 0, index: 28)
    compute.setBytes(&params, length: 8, index: 29); compute.setBytes(&captureInfo, length: captureInfo.count, index: 13)
    compute.setBytes(&input, length: 16, index: 14); compute.useResource(capture, usage: .read)
    compute.setThreadgroupMemoryLength((max(inputBytes, 16) * cps + 15) / 16 * 16, index: 0)
    compute.dispatchThreadgroups(MTLSize(width: patches, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: cps, height: 1, depth: 1))
    compute.endEncoding()
    // 3. Tessellator, 4. domain draw, 5. reference draw.
    let (ranges, totals, points, indices) = tessellate(gpu, mode, patches: records, count: patches, into: command)
    func target(_ format: MTLPixelFormat) -> MTLTexture {
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: size, height: size, mipmapped: false)
        d.usage = [.renderTarget, .shaderRead]; d.storageMode = .shared
        return gpu.device.makeTexture(descriptor: d)!
    }
    let colors = (0..<4).map { _ in target(.rgba32Float) }, reference = target(.rgba32Uint)
    let draw = MTLRenderPipelineDescriptor()
    draw.vertexFunction = domainLibrary.makeFunction(name: "main0_z"); draw.fragmentFunction = testLibrary.makeFunction(name: "dm_test_fragment")
    for i in 0..<4 { draw.colorAttachments[i].pixelFormat = .rgba32Float }
    let drawState = try gpu.device.makeRenderPipelineState(descriptor: draw)
    let colorPass = MTLRenderPassDescriptor()
    for i in 0..<4 { colorPass.colorAttachments[i].texture = colors[i]; colorPass.colorAttachments[i].loadAction = .clear
        colorPass.colorAttachments[i].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0); colorPass.colorAttachments[i].storeAction = .store }
    render = command.makeRenderCommandEncoder(descriptor: colorPass)!
    let primitive: MTLPrimitiveType = mode.domain == 0 ? .line : .triangle
    render.setRenderPipelineState(drawState)
    if mode.domain != 0 { render.setCullMode(.back); render.setFrontFacing(mode.cw ? .clockwise : .counterClockwise) }
    var perPatch = UInt32(cps)
    render.setVertexBuffer(points, offset: 0, index: 16); render.setVertexBuffer(records, offset: 0, index: 17)
    render.setVertexBuffer(controlPoints, offset: 0, index: 18); render.setVertexBytes(&perPatch, length: 4, index: 19)
    render.drawIndexedPrimitives(type: primitive, indexType: .uint32, indexBuffer: indices, indexBufferOffset: 0,
                                 indirectBuffer: totals, indirectBufferOffset: SatchelShaderTessellator.indexedArgumentsOffset)
    render.endEncoding()
    let ref = MTLRenderPipelineDescriptor()
    ref.vertexFunction = testLibrary.makeFunction(name: "dm_ref_vertex"); ref.fragmentFunction = testLibrary.makeFunction(name: "dm_ref_fragment")
    ref.colorAttachments[0].pixelFormat = .rgba32Uint
    let refState = try gpu.device.makeRenderPipelineState(descriptor: ref)
    let refPass = MTLRenderPassDescriptor(); refPass.colorAttachments[0].texture = reference; refPass.colorAttachments[0].loadAction = .clear
    refPass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0); refPass.colorAttachments[0].storeAction = .store
    render = command.makeRenderCommandEncoder(descriptor: refPass)!
    render.setRenderPipelineState(refState); render.setCullMode(.back); render.setFrontFacing(mode.cw ? .clockwise : .counterClockwise)
    render.setVertexBuffer(positionBuffer, offset: 0, index: 1); render.setVertexBytes(&perPatch, length: 4, index: 2)
    var order: [UInt32] = []
    for p in 0..<patches {
        let b = UInt32(p * cps)
        let tris: [UInt32] = cps == 3 ? [b, b + 1, b + 2] : [b, b + 1, b + 2, b, b + 2, b + 3]
        order += mode.cw ? stride(from: 0, to: tris.count, by: 3).flatMap { [tris[$0], tris[$0 + 2], tris[$0 + 1]] } : tris
    }
    if mode.domain != 0 {
        render.drawIndexedPrimitives(type: .triangle, indexCount: order.count, indexType: .uint32, indexBuffer: gpu.buffer(order), indexBufferOffset: 0)
    }
    render.endEncoding()
    command.commit(); command.waitUntilCompleted()
    try require(command.status == .completed, "chain command failed: \(label)")
    let out = read(ranges, totals, points, indices, patches: patches)
    try require(out.dropped == 0 && out.ranges.allSatisfy({ $0.vertices > 0 }), "chain tessellation: \(label)")
    // The levels the hull wrote arrive in the patch record unchanged (no half precision).
    let record = records.contents().bindMemory(to: Float.self, capacity: ShaderTessellation.patchBytes / 4 * patches)
    for p in 0..<patches {
        try require((0..<4).allSatisfy { record[p * 132 + $0] == hullOuter[$0] } && record[p * 132 + 4] == hullInner[0] && record[p * 132 + 5] == hullInner[1], "hull levels: \(label)")
    }
    func pixels(_ t: MTLTexture) -> [SIMD4<Float>] {
        var data = [SIMD4<Float>](repeating: .zero, count: size * size)
        t.getBytes(&data, bytesPerRow: 16 * size, from: MTLRegionMake2D(0, 0, size, size), mipmapLevel: 0)
        return data
    }
    var ids = [SIMD4<UInt32>](repeating: .zero, count: size * size)
    reference.getBytes(&ids, bytesPerRow: 16 * size, from: MTLRegionMake2D(0, 0, size, size), mipmapLevel: 0)
    let c = colors.map(pixels)
    var covered = 0
    for i in 0..<(size * size) {
        let drawn = c[1][i].w == 1
        if mode.domain != 0 { try require(drawn == (ids[i].x != 0), "coverage differs from the reference at pixel \(i): \(label)") }
        guard drawn else { continue }
        covered += 1
        // Which patch: the reference for surfaces, the primitive ID for lines.
        let p = mode.domain != 0 ? Int(ids[i].x) - 1 : Int(c[3][i].z.bitPattern)
        try require((0..<patches).contains(p) && c[3][i].z.bitPattern == UInt32(p), "primitive ID: \(label)")
        let v = Float(p * cps + 1)   // control point 1's vertex output, through capture, hull and record
        try require(abs(c[0][i].x - (v + 0.5)) < 1e-4 && abs(c[0][i].y - 2 * v) < 1e-4 && abs(c[0][i].z - 3) < 1e-4 && abs(c[0][i].w - 4) < 1e-4, "control point value: \(label)")
        try require(abs(c[1][i].x - 0.25) < 1e-6 && abs(c[1][i].y - 0.5) < 1e-6 && abs(c[1][i].z - 0.75) < 1e-6, "patch value: \(label)")
        let outer = SIMD4<Float>(hullOuter[0], hullOuter[1], hullOuter[2], mode.domain == 1 ? 0 : hullOuter[3])
        try require(c[2][i] == outer, "outer levels: \(label)")
        try require(c[3][i].x == hullInner[0] && c[3][i].y == (mode.domain == 1 ? 0 : hullInner[1]), "inner levels: \(label)")
    }
    try require(covered > (mode.domain == 0 ? 50 : 1000), "too little drawn (\(covered) pixels): \(label)")
    chainCases += 1; chainPixels += covered
}

func refusals() throws -> Int {
    var refused = 0
    func refuses(_ body: () throws -> Void) throws { do { try body() } catch is ShaderTransformError { refused += 1; return }; throw HarnessFailure(message: "an unknown tessellation interface was accepted") }
    let io = ShaderTessellation.Interface()
    try refuses { _ = try ShaderTessellation.hull("using namespace metal;\n", io) }
    try refuses { _ = try ShaderTessellation.domain("using namespace metal;\n", io) }
    let domain = "using namespace metal;\nstruct main0_out\n{\n    float4 gl_Position [[position]];\n};\n\nstruct main0_in\n{\n    float3 m_7 [[attribute(0)]];\n};\n\n"
        + "struct main0_patchIn\n{\n    patch_control_point<main0_in> gl_in;\n};\n\n[[ patch(triangle, 0) ]] vertex main0_out main0(main0_patchIn patchIn [[stage_in]], float3 gl_TessCoord [[position_in_patch]])\n{\n    main0_out out = {};\n    return out;\n}\n"
    var located = io; located.location[7] = 0
    try refuses { _ = try ShaderTessellation.domain(domain, located) }   // a three-component location
    try refuses { _ = try ShaderTessellation.domain(domain.replacingOccurrences(of: "float3 m_7", with: "float4 m_7").replacingOccurrences(of: "[[position_in_patch]]", with: "[[position_in_patch]], uint i [[instance_id]]"), located) }
    try refuses { _ = try ShaderTessellation.domain(domain.replacingOccurrences(of: "float3 m_7", with: "float4 m_9"), located) }   // no Location
    try refuses { _ = try ShaderTessellation.mode(io) }
    return refused
}

@main struct TessellationHarness {
    static func main() {
        do {
            let gpu = try GPU()
            try tessellatorChecks(gpu)
            var prepared: [String: PreparedShaderSources] = [:]
            while let line = readLine() {
                guard let row = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any], let name = row["name"] as? String,
                      let hex = row["code"] as? String, let job = row["job"] as? [String: Any] else { throw HarnessFailure(message: "invalid request") }
                var code = Data(), at = hex.startIndex
                while at < hex.endIndex { let end = hex.index(at, offsetBy: 2); code.append(UInt8(hex[at..<end], radix: 16)!); at = end }
                do { prepared[name] = try NativeShaderTranslation.prepare(code: code, job: job) }
                catch { throw HarnessFailure(message: "translation failed: \(name)") }
            }
            for (hull, domain, outer, inner) in [("hull-tri", "domain-tri", [Float(3), 5, 2, 1], [Float(4), 1]), ("hull-tri", "domain-tri-even", [3, 5, 2, 1], [4, 1]),
                                                 ("hull-quad", "domain-quad-odd-cw", [3, 5, 2, 7], [3, 5]), ("hull-quad", "domain-isolines", [3, 5, 2, 7], [3, 5])] {
                guard let h = prepared[hull], let d = prepared[domain] else { throw HarnessFailure(message: "missing corpus rows") }
                try chain(gpu, hull: h, domain: d, patches: 2, hullOuter: outer, hullInner: inner)
            }
            let refused = try refusals()
            print("{\"tessellatorGroups\":\(tessellatorCases),\"tessellatorPatches\":\(tessellatorPatches),\"triangles\":\(triangles),\"lines\":\(lines),"
                  + "\"chainCases\":\(chainCases),\"chainPixels\":\(chainPixels),\"translated\":\(prepared.count),\"refusals\":\(refused),\"passed\":true}")
        } catch let failure as HarnessFailure {
            print("{\"passed\":false,\"failure\":\"\(failure.message)\"}"); exit(1)
        } catch {
            print("{\"passed\":false,\"failure\":\"\(type(of: error))\"}"); exit(1)
        }
    }
}
