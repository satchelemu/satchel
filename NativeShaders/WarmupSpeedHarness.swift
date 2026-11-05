import Foundation
import Metal

// Real Prepare phase 1 driver, with private captured requests and counts-only logs.
@main struct WarmupSpeedHarness {
    static func main() {
        let done = DispatchSemaphore(value: 0)
        // Match WarmupJob's background caller for both binaries. Foreground
        // scaling is a separate phone policy, not an extra Mac benchmark lane.
        DispatchQueue.global(qos: .utility).async {
            do { try run() }
            catch { fputs("Warm-up measurement failed: \(String(reflecting: type(of: error))) (\((error as NSError).code))\n", stderr); exit(1) }
            done.signal()
        }
        done.wait()
    }
    static func run() throws {
        let args = CommandLine.arguments
        guard args.count == 5, let device = MTLCreateSystemDefaultDevice() else { throw SatchelShaderError.invalidRequest }
        let rows = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: args[1]))) as! [[String: Any]]
        let root = URL(fileURLWithPath: args[2]), output = URL(fileURLWithPath: args[3])
        let compiler = SatchelShaderCompiler(device: device, maximumLiveLibraries: 64)
        var requests: [SatchelShaderRequest] = []
        for row in rows {
            let captured = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: row["recordPath"] as! String))) as! [String: Any]
            let envelope = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: row["cachePath"] as! String))) as! [String: Any]
            let expected = try JSONDecoder().decode(SatchelTranslatedShader.self, from: Data(base64Encoded: envelope["payload"] as! String)!)
            let record = Data(base64Encoded: captured["record"] as! String)!
            let stage = SatchelShaderStage(rawValue: captured["stage"] as! String)!
            let base = SatchelShaderRequest(record: record, stage: stage, expectedKey: expected.key)
            let normalized = try SatchelShaderTranslator.normalized(base)
            var found = false
            for mask in row["masks"] as! [UInt32] {
                let request = SatchelShaderRequest(record: record, stage: stage,
                    wantedPixelLocations: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 }), expectedKey: expected.key)
                if try SatchelShaderTranslator.identity(request, normalized: normalized) == expected.requestSHA256 {
                    requests.append(request); found = true; break
                }
            }
            guard found else { throw SatchelShaderError.invalidRequest }
        }
        let environment = ProcessInfo.processInfo.environment
        let primed = environment["SATCHEL_WARMUP_PRIME"] == "1"
        let passes = min(5, max(1, Int(environment["SATCHEL_WARMUP_PASSES"] ?? "1") ?? 1))
        let events = environment["DM_SHADER_EVENTS"]
        unsetenv("DM_SHADER_EVENTS")
        if primed {
            // Prime inside this process. Switching executable identities can
            // invalidate otherwise apparently warm Apple compiler-cache trials.
            for request in requests {
                try autoreleasepool {
                    let shader = try SatchelShaderTranslator.translate(request)
                    for variant in shader.sources.keys.sorted() { _ = try compiler.compile(shader, variant: variant) }
                }
            }
        }
        var results: [[String: Any]] = []
        for pass in 0..<passes {
            compiler.trimLiveLibraries()
            if let events { setenv("DM_SHADER_EVENTS", events + "-pass\(pass).tsv", 1) }
            let cache = passes == 1 ? root : root.appendingPathComponent("pass\(pass)")
            results.append(try measure(requests, root: cache, compiler: compiler, label: args[4]))
        }
        var result = results.last!
        result["passes"] = results; result["compilerPrimed"] = primed
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]).write(to: output, options: .atomic)
        print("Measured \(requests.count) requests in \(passes) passes; compiler primed: \(primed)")
    }
    static func measure(_ requests: [SatchelShaderRequest], root: URL, compiler: SatchelShaderCompiler, label: String) throws -> [String: Any] {
        let store = try SatchelShaderStore(directory: root, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
        #if WARMUP_SPEED
        let warmer = try SatchelShaderWarmup(store: store, compiler: compiler, maximumBufferedRecords: 16)
        #else
        let warmer = try SatchelShaderWarmup(store: store, compiler: compiler)
        #endif
        var sent = 0, records: [SatchelShaderWarmupRecord] = []
        let state = try warmer.run(totalRecords: requests.count, next: {
            guard sent < requests.count else { return nil }; defer { sent += 1 }; return requests[sent]
        }, progress: { _, batch in records += batch })
        unsetenv("DM_SHADER_EVENTS")
        // Canonical translated bytes and library function sets are compared by
        // request/variant. Metal does not expose executable library serialization.
        var outputs: [String: String] = [:], librarySet: [String: [String]] = [:]
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        for row in records {
            guard let shader = try store.load(requestSHA256: row.requestSHA256) else { throw SatchelShaderCacheError.corruptEntry }
            outputs[row.requestSHA256] = SatchelShaderTranslator.sha256(try encoder.encode(shader))
            for variant in shader.sources.keys.sorted() {
                let compiled = try compiler.compile(shader, variant: variant)
                librarySet[row.requestSHA256 + "/" + variant + "/" + compiled.sourceSHA256] = compiled.library.functionNames.sorted()
            }
        }
        let result: [String: Any] = ["label": label, "progress": try JSONSerialization.jsonObject(with: encoder.encode(state)),
            "records": try JSONSerialization.jsonObject(with: encoder.encode(records)), "translated": outputs, "libraries": librarySet]
        return result
    }
}
