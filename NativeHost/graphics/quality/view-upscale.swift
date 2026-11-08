// Resamples of a game's reduced 3D view on the Mac (NativeHost/graphics/README.md, "3D view resample").
//
// view-upscale VIEW.raw OUT_WxH OUTDIR [--time N]
//   VIEW.raw  the view texture the game upscales, saved with the Mac host's DM_CTL "texraw <serial> <path>"
//             (header "width height Metal-format bytes-per-row", then the rows); keep it outside the repository
//   OUT_WxH   the target the game upscales it into, for example 1920x1080
//   OUTDIR    receives one sRGB-encoded PNG per resample, as the game's final copy into its RGBA8 sRGB
//             scan-out encodes it, for quality/present-quality.swift to compare with a 1.0 capture
// Resamples: bilinear, bicubic (Catmull-Rom, sixteen point loads), lanczos2, MetalFX spatial in linear
// mode and in perceptual mode (on sRGB-encoded input). With VIEW_SHADER=ViewUpscale.metal also "view",
// the engine's own resample (nine bilinear taps) on the captured texture itself; with
// EDGE_FILTER=PresentFilter.metal also the present's edge smoothing at the view's resolution before
// bilinear and bicubic. Prints the GPU time of each resample (median of N command buffers, default 30).
import Foundation
import Metal
import MetalFX
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

func fail(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }
let a = Array(CommandLine.arguments.dropFirst())
guard a.count >= 3 else { fail("usage: view-upscale VIEW.raw OUT_WxH OUTDIR [--time N]") }
let wh = a[1].split(separator: "x").compactMap { Int($0) }; let OW = wh[0], OH = wh[1], outDir = a[2]
let timeN = a.count >= 5 && a[3] == "--time" ? Int(a[4]) ?? 30 : 30
let data = try! Data(contentsOf: URL(fileURLWithPath: a[0]))
let nl = data.firstIndex(of: 10)!
let hdr = String(data: data[..<nl], encoding: .ascii)!.split(separator: " ").compactMap { Int($0) }
let W = hdr[0], H = hdr[1], fmt = hdr[2], row = hdr[3]
guard let pf = MTLPixelFormat(rawValue: UInt(fmt)) else { fail("format \(fmt)") }
let dev = MTLCreateSystemDefaultDevice()!, q = dev.makeCommandQueue()!
let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: pf, width: W, height: H, mipmapped: false); td.usage = [.shaderRead]
let view = dev.makeTexture(descriptor: td)!
data.withUnsafeBytes { p in view.replace(region: MTLRegionMake2D(0, 0, W, H), mipmapLevel: 0, withBytes: p.baseAddress! + nl + 1, bytesPerRow: row) }
print("view \(W)x\(H) format \(fmt) -> \(OW)x\(OH) on \(dev.name)")
let src = """
#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; float2 uv; };
vertex V vs(uint i [[vertex_id]]) { float2 t = float2((i << 1) & 2, i & 2); V o; o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o; }
static float3 enc(float3 c) { c = saturate(c); return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308); }
fragment float4 bilinear(V v [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { return float4(t.sample(s, v.uv).rgb, 1); }
// Separable kernels evaluated with point loads at the source texel grid (clamped).
static float cr(float x) { x = abs(x); return x < 1 ? 1.5 * x * x * x - 2.5 * x * x + 1 : x < 2 ? -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2 : 0; }
static float lz(float x) { x = abs(x); if (x < 1e-5) return 1; if (x >= 2) return 0; float px = M_PI_F * x; return 2 * sin(px) * sin(px / 2) / (px * px); }
template <int R, typename K> static float3 conv(texture2d<float> t, float2 uv, K k) {
    float2 sz = float2(t.get_width(), t.get_height()), p = uv * sz - 0.5, f = floor(p); float3 acc = 0; float ws = 0;
    for (int j = -R + 1; j <= R; j++) for (int i = -R + 1; i <= R; i++) {
        float w = k(p.x - (f.x + i)) * k(p.y - (f.y + j));
        int2 c = clamp(int2(f) + int2(i, j), int2(0), int2(sz) - 1);
        acc += w * t.read(uint2(c)).rgb; ws += w; }
    return acc / ws;
}
struct CR { float operator()(float x) const { return cr(x); } }; struct LZ { float operator()(float x) const { return lz(x); } };
fragment float4 bicubic(V v [[stage_in]], texture2d<float> t [[texture(0)]]) { return float4(max(conv<2>(t, v.uv, CR()), 0.0), 1); }
fragment float4 lanczos2(V v [[stage_in]], texture2d<float> t [[texture(0)]]) { return float4(max(conv<2>(t, v.uv, LZ()), 0.0), 1); }
fragment float4 copyenc(V v [[stage_in]], texture2d<float> t [[texture(0)]]) { return float4(enc(t.read(uint2(v.p.xy)).rgb), 1); }
fragment float4 copydec(V v [[stage_in]], texture2d<float> t [[texture(0)]]) { float3 c = t.read(uint2(v.p.xy)).rgb; return float4(select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045), 1); }
fragment float4 copylin(V v [[stage_in]], texture2d<float> t [[texture(0)]]) { return float4(t.read(uint2(v.p.xy)).rgb, 1); }
"""
let lib = try! dev.makeLibrary(source: src, options: nil)
func pso(_ f: String, _ out: MTLPixelFormat) -> MTLRenderPipelineState {
    let d = MTLRenderPipelineDescriptor(); d.vertexFunction = lib.makeFunction(name: "vs"); d.fragmentFunction = lib.makeFunction(name: f)
    d.colorAttachments[0].pixelFormat = out; return try! dev.makeRenderPipelineState(descriptor: d)
}
let sd = MTLSamplerDescriptor(); sd.minFilter = .linear; sd.magFilter = .linear; sd.sAddressMode = .clampToEdge; sd.tAddressMode = .clampToEdge
let samp = dev.makeSamplerState(descriptor: sd)!
func tex(_ w: Int, _ h: Int, _ f: MTLPixelFormat, _ u: MTLTextureUsage = [.renderTarget, .shaderRead]) -> MTLTexture {
    let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: f, width: w, height: h, mipmapped: false); d.usage = u; d.storageMode = .private
    return dev.makeTexture(descriptor: d)!
}
func pass(_ cb: MTLCommandBuffer, _ p: MTLRenderPipelineState, _ s: MTLTexture, _ d: MTLTexture) {
    let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = d; rp.colorAttachments[0].loadAction = .dontCare; rp.colorAttachments[0].storeAction = .store
    let e = cb.makeRenderCommandEncoder(descriptor: rp)!; e.setRenderPipelineState(p); e.setFragmentTexture(s, index: 0); e.setFragmentSamplerState(samp, index: 0)
    e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); e.endEncoding()
}
// The view as RGBA16F (linear) and sRGB-encoded RGBA16F, MetalFX's inputs.
let lin = tex(W, H, .rgba16Float), encIn = tex(W, H, .rgba16Float)
do { let cb = q.makeCommandBuffer()!; pass(cb, pso("copylin", .rgba16Float), view, lin); pass(cb, pso("copyenc", .rgba16Float), view, encIn); cb.commit(); cb.waitUntilCompleted() }
func scaler(_ mode: MTLFXSpatialScalerColorProcessingMode) -> MTLFXSpatialScaler {
    let d = MTLFXSpatialScalerDescriptor(); d.inputWidth = W; d.inputHeight = H; d.outputWidth = OW; d.outputHeight = OH
    d.colorTextureFormat = .rgba16Float; d.outputTextureFormat = .rgba16Float; d.colorProcessingMode = mode
    return d.makeSpatialScaler(device: dev)!
}
// Optional: the present's edge smoothing (PresentFilter.metal) at the view's own resolution, on its sRGB-encoded colour, before the resample.
let filterPath = ProcessInfo.processInfo.environment["EDGE_FILTER"]
let edged = tex(W, H, .rgba16Float)
if let fp = filterPath {
    let fl = try! dev.makeLibrary(source: String(contentsOfFile: fp, encoding: .utf8), options: nil)
    let d = MTLRenderPipelineDescriptor(); d.vertexFunction = fl.makeFunction(name: "satchel_filter_vs"); d.fragmentFunction = fl.makeFunction(name: "satchel_edge_fs")
    d.colorAttachments[0].pixelFormat = .rgba16Float; let ep = try! dev.makeRenderPipelineState(descriptor: d)
    let mid = tex(W, H, .rgba16Float)
    let cb = q.makeCommandBuffer()!
    let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = mid; rp.colorAttachments[0].loadAction = .dontCare; rp.colorAttachments[0].storeAction = .store
    let e = cb.makeRenderCommandEncoder(descriptor: rp)!; e.setRenderPipelineState(ep); e.setFragmentTexture(encIn, index: 0); e.setFragmentSamplerState(samp, index: 0)
    var c: [Float] = [0, 0, 1, 1, 0.5 / Float(W), 0.5 / Float(H), (Float(W) - 0.5) / Float(W), (Float(H) - 0.5) / Float(H), 1, 0, 0, 0]
    e.setFragmentBytes(&c, length: 48, index: 0); e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); e.endEncoding()
    pass(cb, pso("copydec", .rgba16Float), mid, edged); cb.commit(); cb.waitUntilCompleted()
}
let fxl = scaler(.linear), fxp = scaler(.perceptual)
let outLin = tex(OW, OH, .rgba16Float, [.renderTarget, .shaderRead, .shaderWrite])
let final8 = tex(OW, OH, .rgba8Unorm)
typealias Step = (MTLCommandBuffer) -> Void
// Each variant: (resample step, whether its output is already sRGB-encoded)
var variants: [(String, Step, Bool)] = [
    ("bilinear", { pass($0, pso("bilinear", .rgba16Float), view, outLin) }, false),
    ("bicubic", { pass($0, pso("bicubic", .rgba16Float), view, outLin) }, false),
    ("lanczos2", { pass($0, pso("lanczos2", .rgba16Float), view, outLin) }, false),
    ("metalfx-linear", { cb in fxl.colorTexture = lin; fxl.outputTexture = outLin; fxl.inputContentWidth = W; fxl.inputContentHeight = H; fxl.encode(commandBuffer: cb) }, false),
    ("metalfx-perceptual", { cb in fxp.colorTexture = encIn; fxp.outputTexture = outLin; fxp.inputContentWidth = W; fxp.inputContentHeight = H; fxp.encode(commandBuffer: cb) }, true),
]
if filterPath != nil {
    variants.append(("edge-bilinear", { pass($0, pso("bilinear", .rgba16Float), edged, outLin) }, false))
    variants.append(("edge-bicubic", { pass($0, pso("bicubic", .rgba16Float), edged, outLin) }, false))
}
if let vp = ProcessInfo.processInfo.environment["VIEW_SHADER"] {   // the engine's resample (ViewUpscale.metal), on the captured texture itself
    let vl = try! dev.makeLibrary(source: String(contentsOfFile: vp, encoding: .utf8), options: nil)
    let d = MTLRenderPipelineDescriptor(); d.vertexFunction = vl.makeFunction(name: "satchel_view_vs"); d.fragmentFunction = vl.makeFunction(name: "satchel_view_fs")
    d.colorAttachments[0].pixelFormat = .rgba16Float; let vpso = try! dev.makeRenderPipelineState(descriptor: d)
    variants.append(("view", { cb in
        let rp = MTLRenderPassDescriptor(); rp.colorAttachments[0].texture = outLin; rp.colorAttachments[0].loadAction = .dontCare; rp.colorAttachments[0].storeAction = .store
        let e = cb.makeRenderCommandEncoder(descriptor: rp)!; e.setRenderPipelineState(vpso); e.setFragmentTexture(view, index: 0); e.setFragmentSamplerState(samp, index: 0)
        var c: [Float] = [Float(W), Float(H), 1 / Float(W), 1 / Float(H)]; e.setFragmentBytes(&c, length: 16, index: 0)
        e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); e.endEncoding() }, false))
}
try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)
let encP = pso("copyenc", .rgba8Unorm), linP = pso("copylin", .rgba8Unorm)
for (name, step, encoded) in variants {
    var times: [Double] = []
    for k in 0..<(timeN + 3) { let cb = q.makeCommandBuffer()!; step(cb); cb.commit(); cb.waitUntilCompleted(); if k >= 3 { times.append((cb.gpuEndTime - cb.gpuStartTime) * 1000) } }
    times.sort()
    let cb = q.makeCommandBuffer()!; pass(cb, encoded ? linP : encP, outLin, final8)
    let buf = dev.makeBuffer(length: OW * OH * 4, options: .storageModeShared)!
    let b = cb.makeBlitCommandEncoder()!
    b.copy(from: final8, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(), sourceSize: MTLSize(width: OW, height: OH, depth: 1), to: buf, destinationOffset: 0, destinationBytesPerRow: OW * 4, destinationBytesPerImage: OW * OH * 4)
    b.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    let ctx = CGContext(data: buf.contents(), width: OW, height: OH, bitsPerComponent: 8, bytesPerRow: OW * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: outDir + "/\(name).png") as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dst, ctx.makeImage()!, nil); CGImageDestinationFinalize(dst)
    print(String(format: "%@: gpu %.3f ms median of %d", name, times[times.count / 2], timeN))
}
