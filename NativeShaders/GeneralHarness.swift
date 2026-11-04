import Foundation
import Metal

@main struct ShaderGeneralHarness {
    static func input(_ query: [String: Any]) throws -> (SatchelShaderInput, FileHandle) {
        guard let path = query["path"] as? String else { throw SatchelShaderError.invalidRequest }
        let handle = try FileHandle(forReadingFrom: URL(fileURLWithPath: path))
        let size = try handle.seekToEnd(), offset = (query["offset"] as? NSNumber)?.uint64Value ?? 0
        let count = (query["count"] as? NSNumber)?.uint64Value ?? size
        guard offset <= size, count <= size - offset else { try handle.close(); throw SatchelShaderDiscoveryError.bounds }
        return (SatchelShaderInput(count: count) { position, length in
            try handle.seek(toOffset: offset + position)
            return try handle.read(upToCount: length) ?? Data()
        }, handle)
    }
    static func discover(_ query: [String: Any]) throws -> [String: Any] {
        guard let directory = query["scratch"] as? String, let output = query["output"] as? String else {
            throw SatchelShaderError.invalidRequest
        }
        let decoder = try SatchelZstdArchiveDecoder(scratchDirectory: URL(fileURLWithPath: directory), reserveBytes: 35 * 1024 * 1024 * 1024)
        if let dictionary = query["dictionary"] as? [String: Any] {
            let (data, handle) = try input(dictionary); defer { try? handle.close() }
            try decoder.loadDictionaryArchive(data)
        }
        let (data, handle) = try input(query); defer { try? handle.close() }
        var records: [[String: Any]] = []
        let summary = try SatchelShaderDiscovery.discover(data, origin: query["origin"] as? String ?? "fixture", decoder: decoder,
            cancelled: { query["cancelled"] as? Bool ?? false }) { record in
            records.append(["key": record.key, "stage": record.request.stage.rawValue, "origin": record.origin,
                            "program": record.program as Any? ?? NSNull()])
        }
        let result: [String: Any] = ["records": records, "recognizedArchives": summary.recognizedArchives,
            "unknownFiles": summary.unknownFiles, "runtimeCaptureRequired": summary.runtimeCaptureRequired]
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]).write(to: URL(fileURLWithPath: output), options: .atomic)
        return ["status": "ok", "records": summary.records]
    }

    @available(macOS 15.0, iOS 18.0, *)
    static func selftest(_ query: [String: Any]) throws -> [String: Any] {
        guard let path = query["scratch"] as? String, let device = MTLCreateSystemDefaultDevice() else { throw SatchelShaderError.invalidRequest }
        let root = URL(fileURLWithPath: path), compiler = SatchelShaderCompiler(device: device)
        let storeRoot = root.appendingPathComponent("sources")
        let store = try SatchelShaderStore(directory: storeRoot, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
        var control = Data(count: 0x880), code = Data(count: 32)
        ShaderRecord.put(0x98761234, &control, 0); ShaderRecord.put(0x880, &control, 0x20)
        ShaderRecord.put(32, &control, 0x700); ShaderRecord.put(32, &control, 0x704)
        for offset in [0x748, 0x74c, 0x750] { ShaderRecord.put(1, &control, offset) }
        var exit = UInt64(0xe30000000007000f).littleEndian
        withUnsafeBytes(of: &exit) { code.replaceSubrange(8..<16, with: $0) }
        let request = try SatchelShaderRequest(control: control, code: code, stage: .compute)
        let translated = try store.prepare(request)
        let reloaded = try SatchelShaderStore(directory: storeRoot, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
        guard let cached = try reloaded.load(requestSHA256: translated.requestSHA256), cached.sources == translated.sources,
              cached.reflectionJSON == translated.reflectionJSON else { throw SatchelShaderCacheError.corruptEntry }
        let other = try SatchelShaderStore(directory: storeRoot, deviceIdentity: compiler.deviceIdentity + "/different", reserveBytes: 35 * 1024 * 1024 * 1024)
        guard try other.load(requestSHA256: translated.requestSHA256) == nil else { throw SatchelShaderCacheError.corruptEntry }
        let directories = try FileManager.default.contentsOfDirectory(at: storeRoot, includingPropertiesForKeys: nil)
        for directory in directories {
            let file = directory.appendingPathComponent(translated.requestSHA256 + ".json")
            if FileManager.default.fileExists(atPath: file.path) { try Data("damaged".utf8).write(to: file, options: .atomic) }
        }
        let rebuilt = try reloaded.prepare(request)
        guard rebuilt.sources == translated.sources else { throw SatchelShaderCacheError.corruptEntry }
        let publications = try SatchelShaderPublicationService(directory: root.appendingPathComponent("runtime"), device: device,
            reserveBytes: 35 * 1024 * 1024 * 1024)
        SatchelShaderRuntime.install(publications)
        func prepare(_ facts: Data?, _ snapshot: UnsafeMutablePointer<UnsafeMutableRawPointer?>?) -> Int32 {
            control.withUnsafeBytes { c in code.withUnsafeBytes { bytes in translated.key.withCString { key in
                if let facts { return facts.withUnsafeBytes { f in satchel_shader_runtime_prepare_snapshot(c.baseAddress, control.count,
                    bytes.baseAddress, code.count, key, 16, f.baseAddress, facts.count, 0, 0, snapshot) } }
                return satchel_shader_runtime_prepare_snapshot(c.baseAddress, control.count, bytes.baseAddress, code.count, key, 16, nil, 0, 0, 0, snapshot)
            } } }
        }
        var prepared: UnsafeMutableRawPointer?
        guard prepare(nil, &prepared) == 5, let snapshot = prepared else {
            throw SatchelShaderError.translationFailed
        }
        defer { satchel_shader_runtime_release_snapshot(snapshot); SatchelShaderRuntime.install(nil) }
        let firstRevision = satchel_shader_runtime_revision(snapshot)
        var oldPath = [CChar](repeating: 0, count: 4096)
        var replacement: UnsafeMutableRawPointer?
        guard satchel_shader_runtime_directory(snapshot, &oldPath, oldPath.count) > 0,
              satchel_shader_runtime_directory(snapshot, &oldPath, 1) == -1,
              prepare(Data("{\"debug\":false}".utf8), &replacement) == 5,
              let newer = replacement else {
            throw SatchelShaderError.translationFailed
        }
        defer { satchel_shader_runtime_release_snapshot(newer) }
        guard satchel_shader_runtime_revision(newer) > firstRevision else { throw SatchelShaderError.translationFailed }
        SatchelShaderRuntime.install(nil)
        guard let retainedLibrary = "b".withCString({ satchel_shader_runtime_library(snapshot, $0, 1) }),
              let nativeLibrary = Unmanaged<AnyObject>.fromOpaque(retainedLibrary).takeRetainedValue() as? MTLLibrary,
              nativeLibrary.makeFunction(name: "main0") != nil else { throw SatchelShaderError.translationFailed }
        let original = """
        #include <metal_stdlib>
        using namespace metal;
        kernel void fixture(device uint* values [[buffer(0)]], uint index [[thread_position_in_grid]]) { values[index] = index + 1; }
        vertex float4 vertex_fixture(uint index [[vertex_id]]) { float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[index],0,1); }
        fragment float4 fragment_fixture() { return float4(1,0,0,1); }
        """
        let first = try compiler.compile(source: original), second = try compiler.compile(source: original)
        guard !first.liveCacheHit, second.liveCacheHit else { throw SatchelShaderError.translationFailed }
        let archiveRoot = root.appendingPathComponent("pipelines")
        let archives = try SatchelPipelineArchiveCache(device: device, deviceIdentity: compiler.deviceIdentity, directory: archiveRoot,
            reserveBytes: 35 * 1024 * 1024 * 1024)
        let descriptor = MTLComputePipelineDescriptor(); descriptor.computeFunction = first.library.makeFunction(name: "fixture")
        let identity = Data("original-compute-fixture-v1".utf8)
        let cold = try archives.compute(descriptor, identity: identity)
        let restarted = try SatchelPipelineArchiveCache(device: device, deviceIdentity: compiler.deviceIdentity, directory: archiveRoot,
            reserveBytes: 35 * 1024 * 1024 * 1024)
        let warm = try restarted.compute(descriptor, identity: identity)
        guard !cold.persistentHit, cold.persisted, warm.persistentHit,
              let buffer = device.makeBuffer(length: 32 * 4, options: .storageModeShared),
              let queue = device.makeCommandQueue(), let command = queue.makeCommandBuffer(), let encoder = command.makeComputeCommandEncoder() else {
            throw SatchelShaderError.translationFailed
        }
        encoder.setComputePipelineState(warm.pipeline); encoder.setBuffer(buffer, offset: 0, index: 0)
        encoder.dispatchThreads(MTLSize(width: 32, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 32, height: 1, depth: 1))
        encoder.endEncoding(); command.commit(); command.waitUntilCompleted()
        guard command.status == .completed else { throw SatchelShaderError.translationFailed }
        let values = buffer.contents().bindMemory(to: UInt32.self, capacity: 32)
        guard (0..<32).allSatisfy({ values[$0] == UInt32($0 + 1) }) else { throw SatchelShaderError.translationFailed }
        let render = MTLRenderPipelineDescriptor()
        render.vertexFunction = first.library.makeFunction(name: "vertex_fixture")
        render.fragmentFunction = first.library.makeFunction(name: "fragment_fixture")
        render.colorAttachments[0].pixelFormat = .rgba8Unorm
        let renderIdentity = Data("original-render-fixture-rgba8-v1".utf8)
        let renderCold = try restarted.render(render, identity: renderIdentity)
        let renderWarm = try restarted.render(render, identity: renderIdentity)
        guard !renderCold.persistentHit, renderCold.persisted, renderWarm.persistentHit else { throw SatchelShaderError.translationFailed }
        let warmup = try WarmupChecks.run(request: request, root: root.appendingPathComponent("warmup"), device: device)
        return ["status": "ok", "warmup": warmup, "sourcePersistence": true, "devicePartition": true, "corruptionRebuild": true,
                "liveLibraryHit": true, "computeArchiveRestartHit": true, "computeResults": 32, "renderArchiveHit": true,
                "runtimeCABI": true, "immutablePublication": true, "snapshotLifetime": true]
    }
    static func main() {
        while let line = readLine() {
            autoreleasepool {
                do {
                    guard let query = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any] else { throw SatchelShaderError.invalidRequest }
                    let result: [String: Any]
                    if query["selftest"] as? Bool == true {
                        if #available(macOS 15.0, iOS 18.0, *) { result = try selftest(query) }
                        else { throw SatchelShaderError.invalidRequest }
                    } else { result = try discover(query) }
                    print(String(decoding: try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]), as: UTF8.self))
                } catch let error as SatchelShaderError { print("{\"status\":\"error\",\"reason\":\"\(error)\"}") }
                catch let error as SatchelShaderDiscoveryError { print("{\"status\":\"error\",\"reason\":\"\(error)\"}") }
                catch let error as SatchelShaderCompilationError { print("{\"status\":\"error\",\"reason\":\"\(error)\"}") }
                catch { print("{\"status\":\"error\",\"reason\":\"private archive or cache failure\"}") }
            }
            fflush(stdout)
        }
    }
}
