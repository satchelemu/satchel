import Foundation
import Metal
import Darwin

@main struct GeometryHarness {
    static func main() {
        if CommandLine.arguments.contains("selftest") {
            do {
                if #available(macOS 15.0, iOS 18.0, *) {
                    print(String(decoding: try JSONSerialization.data(withJSONObject: GeometryChecks.run(), options: [.sortedKeys]), as: UTF8.self))
                } else { throw SatchelShaderError.invalidRequest }
            } catch { fputs("Geometry synthetic GPU checks failed\n", stderr); exit(1) }
            return
        }
        while let line = readLine() {
            autoreleasepool {
                do {
                    guard let query = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                          let input = query["input"] as? String, let output = query["output"] as? String else {
                        throw ShaderTransformError.unsupported("invalid geometry harness request")
                    }
                    let start = ProcessInfo.processInfo.systemUptime
                    var normalized = try ShaderRecord.normalize(Data(contentsOf: URL(fileURLWithPath: input)), stage: "gs",
                        facts: query["facts"] as? [String: Any], expectedKey: query["key"] as? String)
                    var job = normalized.job
                    job["source"] = query["source"]; job["pc_paired"] = query["pc_paired"]
                    normalized = NormalizedShader(key: normalized.key, code: normalized.code, job: job)
                    let result = try ShaderGeometry.prepare(code: normalized.code, job: normalized.job)
                    let translationSeconds = ProcessInfo.processInfo.systemUptime - start
                    let record = try Data(contentsOf: URL(fileURLWithPath: input))
                    let facts = try (query["facts"] as? [String: Any]).map { try JSONSerialization.data(withJSONObject: $0) }
                    let request = SatchelShaderRequest(record: record, stage: .geometry, factsJSON: facts, expectedKey: normalized.key)
                    let publicResult = try SatchelShaderTranslator.translate(request)
                    guard publicResult.sources == result.sources, publicResult.variantReflectionJSON?.count == 7,
                          publicResult.constantBufferExtents.isEmpty, publicResult.unwrittenStorageBindings.isEmpty else {
                        throw ShaderTransformError.unsupported("geometry public API differs")
                    }
                    for (suffix, expected) in result.variantReflections {
                        var reference = expected
                        if reference["source"] != nil { reference["source"] = "native" }
                        if reference["pc_paired"] != nil { reference["pc_paired"] = "" }
                        guard try publicResult.reflection(variant: suffix) == ShaderReflectionJSON.encode(reference) else {
                            throw ShaderTransformError.unsupported("geometry public reflection differs")
                        }
                    }
                    var cancelled = false
                    do { _ = try SatchelShaderTranslator.translate(request, cancelled: { true }) }
                    catch SatchelShaderError.cancelled { cancelled = true }
                    guard cancelled else { throw ShaderTransformError.unsupported("geometry cancellation ignored") }
                    let directory = URL(fileURLWithPath: output)
                    for (suffix, text) in result.sources {
                        try Data(text.utf8).write(to: directory.appendingPathComponent(suffix + ".metal"))
                        try ShaderReflectionJSON.encode(result.variantReflections[suffix]!).write(to: directory.appendingPathComponent(suffix + ".json"))
                    }
                    var timings: [String: Double] = [:]
                    if query["compile"] as? Bool == true {
                        if #available(macOS 15.0, iOS 18.0, *), let device = MTLCreateSystemDefaultDevice() {
                            let compiler = SatchelShaderCompiler(device: device)
                            let store = try SatchelShaderStore(directory: directory.appendingPathComponent("cache"), deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
                            try store.save(publicResult)
                            guard let cached = try store.load(requestSHA256: publicResult.requestSHA256), cached.variantReflectionJSON == publicResult.variantReflectionJSON else {
                                throw ShaderTransformError.unsupported("geometry cache reflection mismatch")
                            }
                            let service = try SatchelShaderPublicationService(directory: directory.appendingPathComponent("publications"), device: device, reserveBytes: 35 * 1024 * 1024 * 1024)
                            let publication = try service.prepare(request)
                            for suffix in publicResult.sources.keys {
                                let data = try Data(contentsOf: publication.directory.appendingPathComponent(publicResult.key + "_" + suffix + ".json"))
                                guard data == (try publicResult.reflection(variant: suffix)) else { throw ShaderTransformError.unsupported("geometry publication mismatch") }
                            }
                            let codeSize = Int(try ShaderRecord.word(record, 0x2c))
                            let ctl = record.subdata(in: 0x38 + codeSize..<record.count), bytes = record.subdata(in: 0x38..<0x38 + codeSize)
                            SatchelShaderRuntime.install(service)
                            defer { SatchelShaderRuntime.install(nil) }
                            var snapshot: UnsafeMutableRawPointer?
                            let status = ctl.withUnsafeBytes { c in bytes.withUnsafeBytes { b in publicResult.key.withCString { k in
                                satchelShaderRuntimePrepareSnapshot(c.baseAddress, ctl.count, b.baseAddress, bytes.count, k, 16, nil, 0, 0, 0, &snapshot)
                            } } }
                            guard status == 2, let snapshot else { throw ShaderTransformError.unsupported("geometry runtime stage mismatch") }
                            satchelShaderRuntimeReleaseSnapshot(snapshot)
                            for suffix in result.sources.keys.sorted() {
                                let compiled = try compiler.compile(source: result.sources[suffix]!)
                                guard compiled.library.makeFunction(name: "main0") != nil else {
                                    throw ShaderTransformError.unsupported("missing geometry function")
                                }
                                if suffix != "bgv" {
                                    _ = try device.makeComputePipelineState(function: compiled.library.makeFunction(name: "main0")!)
                                }
                                timings[suffix] = compiled.seconds
                            }
                        } else { throw ShaderTransformError.unsupported("Metal compiler unavailable") }
                    }
                    var usage = rusage()
                    guard getrusage(RUSAGE_SELF, &usage) == 0 else { throw SatchelShaderError.invalidRequest }
                    let response: [String: Any] = ["status": "ok", "sources": result.sources.count,
                        "translateSeconds": translationSeconds, "compileSeconds": timings, "publicAPI": true, "pid": getpid(), "peakRSSBytes": usage.ru_maxrss]
                    print(String(decoding: try JSONSerialization.data(withJSONObject: response, options: [.sortedKeys]), as: UTF8.self))
                } catch ShaderTransformError.unsupported(let reason) {
                    print(String(decoding: try! JSONSerialization.data(withJSONObject: ["status": "error", "reason": reason]), as: UTF8.self))
                } catch {
                    print("{\"status\":\"error\",\"reason\":\"geometry IO, compiler or pipeline failure\"}")
                }
            }
            fflush(stdout)
        }
    }
}
