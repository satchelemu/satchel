import Foundation
import Metal

// Original synthetic viewport fixture adapted from NX2Apple's MIT geometry tests.
@available(macOS 15.0, iOS 18.0, *)
enum GeometryChecks {
    static func run() throws -> [String: Any] {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            throw SatchelShaderError.invalidRequest
        }
        func require(_ value: Bool) throws { if !value { throw SatchelShaderError.translationFailed } }
        func rejects(_ body: () throws -> Void) throws {
            do { try body() } catch { return }
            throw SatchelShaderError.translationFailed
        }
        var header = Data(count: 20)
        ShaderRecord.put(1 << 24, &header, 0)
        ShaderRecord.put(1 << 24, &header, 8)
        ShaderRecord.put(7 << 24, &header, 12)
        ShaderRecord.put(1, &header, 16)
        for topology in 0..<5 {
            let layout = try ShaderGeometry.outputLayout(header, topology: topology)
            try require(layout["vertices"] == [1, 2, 2, 3, 3][topology])
            try require(layout["output"] == [1, 6, 6, 7, 7][topology])
        }
        try rejects { _ = try ShaderGeometry.outputLayout(Data(), topology: 0) }
        try rejects { _ = try ShaderGeometry.outputLayout(header, topology: 5) }
        try rejects { _ = try ShaderGeometry.captureInput("unknown vertex_output;") }
        try rejects { _ = try ShaderGeometry.topologyPointer("kernel void main0() {}") }
        try rejects { _ = try ShaderGeometry.flattenSets(Data(count: 20)) }
        let vertex = """
        #include <metal_stdlib>
        using namespace metal;
        struct main0_out {
            float4 gl_Position [[position]];
            float gl_PointSize [[point_size]];
        };
        vertex main0_out main0(uint vid [[vertex_id]], constant uint& mask [[buffer(0)]], constant uint& points [[buffer(1)]]) {
            main0_out out = {};
            float2 positions[] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
            out.gl_Position = float4(points ? float2(0.0) : positions[vid], 0.0, 1.0);
            out.gl_PointSize = 64.0;
            float bits = as_type<float>(mask);
            spvArrayCopyFromStackToStack(gl_BuiltIn_5253, as_type<int>(bits).elements);
            return out;
        }
        """
        try rejects { _ = try ShaderGeometry.reader(vertex.replacingOccurrences(of: "as_type<int>(bits).elements", with: "as_type<int>(bits + 1.0).elements")) }
        let lowered = try ShaderTransforms.depth(ShaderGeometry.reader(vertex)).1
        let source = lowered + "\nfragment float4 pixel() { return float4(1.0, 0.0, 0.0, 1.0); }\n"
        let library = try SatchelShaderCompiler(device: device).compile(source: source).library
        let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 16, height: 16, mipmapped: false)
        td.usage = .renderTarget; td.storageMode = .shared
        guard let texture = device.makeTexture(descriptor: td) else { throw SatchelShaderError.invalidRequest }
        var count = 0
        for entry in ["main0", "main0_z"] {
            let pd = MTLRenderPipelineDescriptor()
            pd.vertexFunction = library.makeFunction(name: entry); pd.fragmentFunction = library.makeFunction(name: "pixel")
            pd.colorAttachments[0].pixelFormat = .rgba8Unorm
            let pipeline = try device.makeRenderPipelineState(descriptor: pd)
            for clip in [MTLDepthClipMode.clip, .clamp] {
                for isPoint: UInt32 in [0, 1] {
                    for value: UInt32 in [0, 1, 2, 3, 0x80000000, 0xffffffff] {
                        try autoreleasepool {
                            let pass = MTLRenderPassDescriptor()
                            pass.colorAttachments[0].texture = texture
                            pass.colorAttachments[0].loadAction = .clear; pass.colorAttachments[0].storeAction = .store
                            guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
                                throw SatchelShaderError.invalidRequest
                            }
                            encoder.setRenderPipelineState(pipeline); encoder.setDepthClipMode(clip)
                            var mask = value, points = isPoint
                            encoder.setVertexBytes(&mask, length: 4, index: 0); encoder.setVertexBytes(&points, length: 4, index: 1)
                            encoder.drawPrimitives(type: isPoint == 1 ? .point : .triangle, vertexStart: 0, vertexCount: isPoint == 1 ? 1 : 3)
                            encoder.endEncoding(); command.commit(); command.waitUntilCompleted()
                            try require(command.status == .completed)
                            var pixel = [UInt8](repeating: 0, count: 4)
                            texture.getBytes(&pixel, bytesPerRow: 4, from: MTLRegionMake2D(8, 8, 1, 1), mipmapLevel: 0)
                            try require((pixel[0] != 0) == (value & 1 != 0))
                        }
                        count += 1
                    }
                }
            }
        }
        return ["gpuViewportCases": count, "passthroughLayouts": 5, "malformedRejected": 6]
    }
}
