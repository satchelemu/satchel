// Present filter quality and cost on the Mac (NativeHost/graphics/README.md, "Present filters").
//
// present-quality SHADER OUT_WxH REFERENCE TEST [options]
//   SHADER     NativeHost/graphics/PresentFilter.metal (the code the engine embeds)
//   OUT_WxH    the presented size, for example 2853x1604 (the phone's inner screen viewport)
//   REFERENCE  a raw scan-out capture at render scale 1.0 (the engine's DM_CTL "shot", no DM_NVN_SHOT_OUT)
//   TEST       a raw scan-out capture of the same scene at a reduced render scale
// Options: --floor PNG   a second 1.0 capture of the scene: its distance to REFERENCE is the
//                        run-to-run noise floor (animation between the two captures)
//          --crop x,y,w,h  a region in OUT pixels (repeatable); metrics are per region and whole
//          --save DIR      write each presented variant as PNG (keep game images out of the repository)
//          --time N        GPU time of each pass, median of N command buffers (default 60)
//          --variants a,b  subset of off,edge,edge1,spatial,edge-spatial
//          --stride S      the smoothing step in source texels (the scan-out width over the 3D view's width)
//          --lower-left 1  the captures are raw textures in storage order of a lower-left-origin game
//                          (quality-diagnostics.py fNNN.raw.png): the present flips as the engine's does
//          --check PNG     the engine's presented picture of TEST (DM_NVN_SHOT_OUT): count differing bytes
//                          against each variant, which verifies that the engine runs this filter
// Every variant presents TEST; REFERENCE is presented with the current bilinear filter, which is
// what the player sees at 1.0. Metrics on Rec. 601 luma of the gamma bytes: SSIM (11x11 Gaussian,
// sigma 1.5), SSIM over the reference's edge pixels (Sobel magnitude above 0.1) and PSNR.
import Foundation
import Metal
import MetalFX
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

func fail(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }
var args = Array(CommandLine.arguments.dropFirst())
// present-quality --sharpness PNG...: mean absolute luma Laplacian of each raw capture (a burst's
// reduced-scale frames score lower), to pick the frames of a quality-diagnostics.py burst.
if args.first == "--sharpness" {
    for path in args.dropFirst() {
        guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil), let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { fail("cannot read \(path)") }
        let w = img.width, h = img.height; var b = [UInt8](repeating: 0, count: w * h * 4)
        b.withUnsafeMutableBytes { p in CGContext(data: p.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h)) }
        func l(_ x: Int, _ y: Int) -> Double { let k = 4 * (y * w + x); return 0.299 * Double(b[k]) + 0.587 * Double(b[k + 1]) + 0.114 * Double(b[k + 2]) }
        var sum = 0.0
        for y in 1..<(h - 1) { for x in 1..<(w - 1) { sum += abs(4 * l(x, y) - l(x - 1, y) - l(x + 1, y) - l(x, y - 1) - l(x, y + 1)) } }
        print(String(format: "%.3f %@", sum / Double((w - 2) * (h - 2)), path))
    }
    exit(0)
}
guard args.count >= 4 else { fail("usage: present-quality SHADER OUT_WxH REFERENCE TEST [--floor PNG] [--crop x,y,w,h] [--save DIR] [--time N] [--variants list]") }
let shaderPath = args[0], outSpec = args[1], refPath = args[2], testPath = args[3]
var floorPath: String? = nil, saveDir: String? = nil, timeN = 60
var stride: Float = 1, lowerLeft = false, checkPath: String? = nil
var crops: [(String, [Int])] = []
var variants = ["off", "edge", "edge1", "spatial", "edge-spatial"]
var i = 4
while i < args.count {
    let a = args[i]; guard i + 1 < args.count else { fail("missing value for \(a)") }
    let v = args[i + 1]; i += 2
    switch a {
    case "--floor": floorPath = v
    case "--save": saveDir = v
    case "--time": timeN = Int(v) ?? 60
    case "--stride": stride = Float(v) ?? 1
    case "--lower-left": lowerLeft = v == "1"
    case "--check": checkPath = v
    case "--variants": variants = v.split(separator: ",").map(String.init)
    case "--crop":
        let p = v.split(separator: ",").compactMap { Int($0) }; guard p.count == 4 else { fail("bad crop \(v)") }
        crops.append(("crop\(crops.count + 1) \(v)", p))
    default: fail("unknown option \(a)")
    }
}
let wh = outSpec.split(separator: "x").compactMap { Int($0) }
guard wh.count == 2, wh[0] > 0, wh[1] > 0 else { fail("bad output size \(outSpec)") }
let OW = wh[0], OH = wh[1]

guard let dev = MTLCreateSystemDefaultDevice(), let q = dev.makeCommandQueue() else { fail("no Metal device") }
let lib: MTLLibrary
do { lib = try dev.makeLibrary(source: String(contentsOfFile: shaderPath, encoding: .utf8), options: nil) } catch { fail("shader: \(error)") }
func pso(_ fs: String, _ fmt: MTLPixelFormat) -> MTLRenderPipelineState {
    let d = MTLRenderPipelineDescriptor()
    d.vertexFunction = lib.makeFunction(name: "satchel_filter_vs"); d.fragmentFunction = lib.makeFunction(name: fs)
    d.colorAttachments[0].pixelFormat = fmt
    do { return try dev.makeRenderPipelineState(descriptor: d) } catch { fail("pipeline \(fs): \(error)") }
}
let sd = MTLSamplerDescriptor(); sd.minFilter = .linear; sd.magFilter = .linear
let samp = dev.makeSamplerState(descriptor: sd)!
let present = pso("satchel_present_fs", .bgra8Unorm), edgeOut = pso("satchel_edge_fs", .bgra8Unorm), edgeSrc = pso("satchel_edge_fs", .rgba8Unorm)

func load(_ path: String) -> MTLTexture {
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil), let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { fail("cannot read \(path)") }
    let w = img.width, h = img.height
    var bytes = [UInt8](repeating: 0, count: w * h * 4)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let ok = bytes.withUnsafeMutableBytes { p -> Bool in
        guard let ctx = CGContext(data: p.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: cs, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return false }
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h)); return true
    }
    if !ok { fail("cannot decode \(path)") }
    let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: false)
    td.usage = [.shaderRead]
    let t = dev.makeTexture(descriptor: td)!
    t.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0, withBytes: bytes, bytesPerRow: w * 4)
    return t
}
func target(_ w: Int, _ h: Int, _ f: MTLPixelFormat, extra: MTLTextureUsage = []) -> MTLTexture {
    let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: f, width: w, height: h, mipmapped: false)
    td.usage = MTLTextureUsage([.renderTarget, .shaderRead]).union(extra); td.storageMode = .private
    return dev.makeTexture(descriptor: td)!
}
// The crop record exactly as the engine writes it for a whole-texture crop.
func crop(_ t: MTLTexture) -> [Float] {
    let w = Float(t.width), h = Float(t.height)
    return [0, 0, 1, 1, 0.5 / w, 0.5 / h, (w - 0.5) / w, (h - 0.5) / h]
}
func draw(_ cb: MTLCommandBuffer, _ p: MTLRenderPipelineState, _ src: MTLTexture, _ dst: MTLTexture) {
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = dst; rp.colorAttachments[0].loadAction = .dontCare; rp.colorAttachments[0].storeAction = .store
    let en = cb.makeRenderCommandEncoder(descriptor: rp)!
    en.setRenderPipelineState(p); en.setFragmentTexture(src, index: 0); en.setFragmentSamplerState(samp, index: 0)
    var c = crop(src) + [stride, 0, 0, 0]
    if lowerLeft && p === present { c[1] += c[3]; c[3] = -c[3] }
    en.setFragmentBytes(&c, length: 48, index: 0)
    en.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); en.endEncoding()
}
var scalers: [String: MTLFXSpatialScaler] = [:]
func spatial(_ cb: MTLCommandBuffer, _ src: MTLTexture, _ dst: MTLTexture) {
    let key = "\(src.width)x\(src.height)"
    if scalers[key] == nil {
        let d = MTLFXSpatialScalerDescriptor()
        d.inputWidth = src.width; d.inputHeight = src.height; d.outputWidth = dst.width; d.outputHeight = dst.height
        d.colorTextureFormat = src.pixelFormat; d.outputTextureFormat = dst.pixelFormat; d.colorProcessingMode = .perceptual
        guard let s = d.makeSpatialScaler(device: dev) else { fail("MetalFX spatial scaler unavailable") }
        scalers[key] = s
    }
    let s = scalers[key]!
    s.colorTexture = src; s.outputTexture = dst; s.inputContentWidth = src.width; s.inputContentHeight = src.height
    s.encode(commandBuffer: cb)
}
// Input and intermediate textures MetalFX reads need its usage bits.
func fxInput(_ t: MTLTexture) -> MTLTexture {
    let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: t.pixelFormat, width: t.width, height: t.height, mipmapped: false)
    d.usage = [.shaderRead, .renderTarget]; d.storageMode = .private
    let c = dev.makeTexture(descriptor: d)!
    let cb = q.makeCommandBuffer()!; let b = cb.makeBlitCommandEncoder()!; b.copy(from: t, to: c); b.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    return c
}
func outTex() -> MTLTexture { target(OW, OH, .bgra8Unorm, extra: [.shaderWrite]) }
// One variant: the passes as separate timed steps.
func run(_ name: String, _ src: MTLTexture) -> (MTLTexture, [(String, (MTLCommandBuffer) -> Void)]) {
    let out = outTex()
    switch name {
    case "off": return (out, [("bilinear present", { draw($0, present, src, out) })])
    case "edge1": return (out, [("edge present (single pass)", { draw($0, edgeOut, src, out) })])
    case "edge":
        let mid = target(src.width, src.height, .rgba8Unorm)
        return (out, [("edge pass", { draw($0, edgeSrc, src, mid) }), ("bilinear present", { draw($0, present, mid, out) })])
    case "spatial":
        let input = fxInput(src)
        return (out, [("MetalFX spatial", { spatial($0, input, out) })])
    case "edge-spatial":
        let mid = target(src.width, src.height, .rgba8Unorm)
        return (out, [("edge pass", { draw($0, edgeSrc, src, mid) }), ("MetalFX spatial", { spatial($0, mid, out) })])
    default: fail("unknown variant \(name)")
    }
}
func execute(_ steps: [(String, (MTLCommandBuffer) -> Void)]) {
    let cb = q.makeCommandBuffer()!; for s in steps { s.1(cb) }; cb.commit(); cb.waitUntilCompleted()
    if let e = cb.error { fail("GPU error \(e)") }
}
func timing(_ steps: [(String, (MTLCommandBuffer) -> Void)]) -> [(String, Double)] {
    var r: [(String, Double)] = []
    for s in steps {
        var v: [Double] = []
        for k in 0..<(timeN + 5) {
            let cb = q.makeCommandBuffer()!; s.1(cb); cb.commit(); cb.waitUntilCompleted()
            if k >= 5 { v.append((cb.gpuEndTime - cb.gpuStartTime) * 1000) }
        }
        v.sort(); r.append((s.0, v.isEmpty ? .nan : v[v.count / 2]))
    }
    return r
}
func readback(_ t: MTLTexture) -> [UInt8] {
    let row = t.width * 4
    let buf = dev.makeBuffer(length: row * t.height, options: .storageModeShared)!
    let cb = q.makeCommandBuffer()!; let b = cb.makeBlitCommandEncoder()!
    b.copy(from: t, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0), sourceSize: MTLSize(width: t.width, height: t.height, depth: 1),
           to: buf, destinationOffset: 0, destinationBytesPerRow: row, destinationBytesPerImage: row * t.height)
    b.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    return Array(UnsafeBufferPointer(start: buf.contents().assumingMemoryBound(to: UInt8.self), count: row * t.height))
}
func save(_ t: MTLTexture, _ path: String, region: [Int]? = nil) {
    var px = readback(t); var w = t.width, h = t.height
    if let r = region {
        var c = [UInt8](repeating: 0, count: r[2] * r[3] * 4)
        for y in 0..<r[3] { for x in 0..<(r[2] * 4) { c[y * r[2] * 4 + x] = px[(r[1] + y) * w * 4 + r[0] * 4 + x] } }
        px = c; w = r[2]; h = r[3]
    }
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    px.withUnsafeMutableBytes { p in
        let ctx = CGContext(data: p.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: cs,
                            bitmapInfo: CGImageAlphaInfo.noneSkipFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)!
        let img = ctx.makeImage()!
        let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, UTType.png.identifier as CFString, 1, nil)!
        CGImageDestinationAddImage(dst, img, nil); CGImageDestinationFinalize(dst)
    }
}
// BGRA8 bytes to Rec. 601 luma in [0, 1].
func luma(_ b: [UInt8]) -> [Float] {
    var l = [Float](repeating: 0, count: b.count / 4)
    for k in 0..<l.count { l[k] = (0.114 * Float(b[4 * k]) + 0.587 * Float(b[4 * k + 1]) + 0.299 * Float(b[4 * k + 2])) / 255 }
    return l
}
let gauss: [Float] = { let r = 5, s: Float = 1.5; var g = (-r...r).map { exp(-Float($0 * $0) / (2 * s * s)) }; let t = g.reduce(0, +); g = g.map { $0 / t }; return g }()
func blur(_ a: [Float]) -> [Float] {
    var h = [Float](repeating: 0, count: a.count), o = [Float](repeating: 0, count: a.count)
    a.withUnsafeBufferPointer { a in h.withUnsafeMutableBufferPointer { h in
        for y in 0..<OH { let r = y * OW; for x in 0..<OW { var s: Float = 0; for k in -5...5 { s += gauss[k + 5] * a[r + min(max(x + k, 0), OW - 1)] }; h[r + x] = s } } } }
    h.withUnsafeBufferPointer { h in o.withUnsafeMutableBufferPointer { o in
        for y in 0..<OH { for x in 0..<OW { var s: Float = 0; for k in -5...5 { s += gauss[k + 5] * h[min(max(y + k, 0), OH - 1) * OW + x] }; o[y * OW + x] = s } } } }
    return o
}
func ssimMap(_ x: [Float], _ y: [Float]) -> [Float] {
    let c1: Float = 0.0001, c2: Float = 0.0009
    let mx = blur(x), my = blur(y)
    let xx = blur(zip(x, x).map(*)), yy = blur(zip(y, y).map(*)), xy = blur(zip(x, y).map(*))
    var m = [Float](repeating: 0, count: x.count)
    for k in 0..<m.count {
        let vx = xx[k] - mx[k] * mx[k], vy = yy[k] - my[k] * my[k], cxy = xy[k] - mx[k] * my[k]
        m[k] = ((2 * mx[k] * my[k] + c1) * (2 * cxy + c2)) / ((mx[k] * mx[k] + my[k] * my[k] + c1) * (vx + vy + c2))
    }
    return m
}
func edges(_ l: [Float]) -> [Bool] {
    var e = [Bool](repeating: false, count: l.count)
    for y in 1..<(OH - 1) { for x in 1..<(OW - 1) {
        func p(_ dx: Int, _ dy: Int) -> Float { l[(y + dy) * OW + x + dx] }
        let gx = p(1, -1) + 2 * p(1, 0) + p(1, 1) - p(-1, -1) - 2 * p(-1, 0) - p(-1, 1)
        let gy = p(-1, 1) + 2 * p(0, 1) + p(1, 1) - p(-1, -1) - 2 * p(0, -1) - p(1, -1)
        e[y * OW + x] = (gx * gx + gy * gy).squareRoot() > 0.1 * 4
    } }
    return e
}
let regions: [(String, [Int])] = [("whole", [0, 0, OW, OH])] + crops
func metrics(_ ref: [Float], _ refEdges: [Bool], _ t: [Float]) -> [String] {
    let s = ssimMap(ref, t)
    return regions.map { (_, r) in
        var sum = 0.0, n = 0, esum = 0.0, en = 0, se = 0.0
        for y in r[1]..<min(r[1] + r[3], OH) { for x in r[0]..<min(r[0] + r[2], OW) {
            let k = y * OW + x; sum += Double(s[k]); n += 1
            if refEdges[k] { esum += Double(s[k]); en += 1 }
            let d = Double(ref[k] - t[k]); se += d * d } }
        let psnr = 10 * log10(1 / max(se / Double(max(n, 1)), 1e-12))
        return String(format: "ssim %.4f edge-ssim %.4f (%d px) psnr %.2f", sum / Double(max(n, 1)), en > 0 ? esum / Double(en) : .nan, en, psnr)
    }
}

let ref = load(refPath), test = load(testPath)
print("reference \(ref.width)x\(ref.height), test \(test.width)x\(test.height), presented at \(OW)x\(OH) on \(dev.name)")
let (refOut, refSteps) = run("off", ref); execute(refSteps)
let refL = luma(readback(refOut)), refE = edges(refL)
func saveAll(_ t: MTLTexture, _ name: String) {
    guard let d = saveDir else { return }
    try? FileManager.default.createDirectory(atPath: d, withIntermediateDirectories: true)
    save(t, d + "/\(name).png")
    for (k, c) in crops.enumerated() { save(t, d + "/\(name)-crop\(k + 1).png", region: c.1) }
}
saveAll(refOut, "reference-off")
if let f = floorPath {
    let (o, s) = run("off", load(f)); execute(s); saveAll(o, "floor-off")
    for (k, m) in metrics(refL, refE, luma(readback(o))).enumerated() { print("floor (second 1.0 capture) \(regions[k].0): \(m)") }
}
for v in variants {
    let (o, steps) = run(v, test); execute(steps)
    saveAll(o, "test-\(v)")
    if let c = checkPath {
        let engine = load(c); let a = readback(o)
        let eb = readback({ let t = target(engine.width, engine.height, .rgba8Unorm); let cb = q.makeCommandBuffer()!; let b = cb.makeBlitCommandEncoder()!; b.copy(from: engine, to: t); b.endEncoding(); cb.commit(); cb.waitUntilCompleted(); return t }())
        var diff = 0, maxd = 0
        if engine.width == OW && engine.height == OH {
            for k in 0..<(OW * OH) { for ch in 0..<3 { let d = abs(Int(a[4 * k + ch]) - Int(eb[4 * k + 2 - ch])); if d > 0 { diff += 1; maxd = max(maxd, d) } } }
            print("\(v) check against engine picture: \(diff) differing bytes of \(OW * OH * 3), largest difference \(maxd)")
        } else { print("\(v) check: engine picture is \(engine.width)x\(engine.height), not \(OW)x\(OH)") }
    }
    for (k, m) in metrics(refL, refE, luma(readback(o))).enumerated() { print("\(v) \(regions[k].0): \(m)") }
    if timeN > 0 { for (pass, ms) in timing(steps) { print(String(format: "%@ gpu %@: %.3f ms median of %d", v, pass, ms, timeN)) } }
}
