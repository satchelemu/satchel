import Foundation
import Metal

@main struct WarmupHarness {
    static func main() {
        do {
            let args = CommandLine.arguments
            guard args.count == 3 || (args.count == 4 && ["--probe", "--sizes"].contains(args[3])) || (args.count == 5 && args[3] == "--sample") else { throw SatchelShaderError.invalidRequest }
            let package = URL(fileURLWithPath: args[1]), runtime = URL(fileURLWithPath: args[2])
            try FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
            let begin = ProcessInfo.processInfo.systemUptime
            _ = try PreparedPackage.verify(root: package, forceFull: true)
            print("Verified package in \(ProcessInfo.processInfo.systemUptime - begin) seconds"); fflush(stdout)
            if args.count == 5 {
                try sample(package: package, runtime: runtime, inventoryRuntime: URL(fileURLWithPath: args[4]), deadline: begin + 570)
                return
            }
            if args.count == 4 && args[3] == "--sizes" {
                let manifest = try WarmupRunner.bounded(package.appendingPathComponent("manifest.json"), maximum: 32 * 1024 * 1024)
                let directory = runtime.appendingPathComponent("warmup/" + SatchelShaderTranslator.sha256(manifest))
                let inventory = try JSONDecoder().decode(WarmupInventory.self, from: WarmupRunner.bounded(directory.appendingPathComponent("inventory.json"), maximum: 32 * 1024 * 1024))
                let resources = try ShaderWarmupResources.get(directory: runtime.appendingPathComponent("shader-cache"), reserve: 35 * 1024 * 1024 * 1024)
                var sizes: [[String: Any]] = []
                for (ordinal, row) in inventory.records.filter({ $0.stage == .fragment }).prefix(36).enumerated() {
                    let request = try inventory.request(row, directory: directory)
                    let normalized = try SatchelShaderTranslator.normalized(request)
                    let id = try SatchelShaderTranslator.identity(request, normalized: normalized)
                    if try resources.store.load(requestSHA256: id) != nil { continue }
                    let shader = try SatchelShaderTranslator.translate(request)
                    try resources.store.save(shader)
                    guard let restored = try resources.store.load(requestSHA256: id), restored.requestSHA256 == shader.requestSHA256,
                          restored.recordSHA256 == shader.recordSHA256, restored.sources == shader.sources else { throw SatchelShaderCacheError.corruptEntry }
                    sizes.append(["cacheRoundTrip":true,"ordinal":ordinal,"variantBytes":shader.sources.mapValues { $0.utf8.count },"payloadBytes":try JSONEncoder().encode(shader).count,"reflectionBytes":shader.reflectionJSON.count])
                }
                let data = try JSONSerialization.data(withJSONObject: sizes, options: .sortedKeys)
                try data.write(to: runtime.appendingPathComponent("entry-sizes.json"), options: .atomic)
                print(String(decoding: data, as: UTF8.self)); return
            }
            if args.count == 4 {
                let stop = ProbeStop()
                var last = WarmupProgress()
                do {
                    _ = try WarmupRunner.run(package: package, runtime: runtime, reserve: 35 * 1024 * 1024 * 1024, cancelled: { stop.value }) { state in
                        last = state
                        if state.phase == 1 && state.completed >= 36 { stop.cancel() }
                    }
                } catch SatchelShaderError.cancelled { }
                var probe: [String: Any] = ["completed":last.completed,"shaders":last.shaders,"failed":last.failed,"sourceHits":last.sourceHits,"purpose":"early failure diagnosis, not a full timing"]
                if let failure = last.firstFailure { probe["firstFailure"] = failure }
                let data = try JSONSerialization.data(withJSONObject: probe, options: .sortedKeys)
                try data.write(to: runtime.appendingPathComponent("probe.json"), options: .atomic)
                print(String(decoding: data, as: UTF8.self)); return
            }
            var last = -Double.infinity
            let result = try WarmupRunner.run(package: package, runtime: runtime, reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { state in
                if state.seconds - last >= 30 || state.state != "running" {
                    last = state.seconds
                    print(String(decoding: try! JSONEncoder().encode(state), as: UTF8.self)); fflush(stdout)
                }
            }
            try JSONEncoder().encode(result).write(to: runtime.appendingPathComponent("result.json"), options: .atomic)
        } catch { fputs("Warm-up harness failed: \(error.localizedDescription)\n", stderr); exit(1) }
    }
}

private final class ProbeStop {
    private let lock = NSLock()
    private var stopped = false
    var value: Bool { lock.lock(); defer { lock.unlock() }; return stopped }
    func cancel() { lock.lock(); stopped = true; lock.unlock() }
}

// Bounded benchmark only. Production continues to use WarmupRunner without a sample.
private extension WarmupHarness {
    static func sample(package: URL, runtime: URL, inventoryRuntime: URL, deadline: Double) throws {
        let started = ProcessInfo.processInfo.systemUptime
        let cancelled = { ProcessInfo.processInfo.systemUptime >= deadline }
        func check() throws { if cancelled() { throw SatchelShaderError.cancelled } }
        let manifest = try WarmupRunner.bounded(package.appendingPathComponent("manifest.json"), maximum: 32 * 1024 * 1024)
        let digest = SatchelShaderTranslator.sha256(manifest)
        let directory = inventoryRuntime.appendingPathComponent("warmup/" + digest)
        let inventory = try JSONDecoder().decode(WarmupInventory.self, from: WarmupRunner.bounded(directory.appendingPathComponent("inventory.json"), maximum: 32 * 1024 * 1024))
        guard inventory.version == WarmupInventory.currentVersion, inventory.manifest == digest else { throw SatchelShaderError.invalidRequest }
        func spaced<T>(_ rows: [T], count: Int) -> [T] {
            guard !rows.isEmpty else { return [] }
            return (0..<min(count, rows.count)).map { rows[$0 * rows.count / min(count, rows.count)] }
        }
        let all = Dictionary(uniqueKeysWithValues: inventory.records.map { ($0.key, $0) })
        let pairs = spaced(inventory.pairs.filter { $0.count == 2 && all[$0[0]]?.stage == .vertex && all[$0[1]]?.stage == .fragment }, count: 128)
        var selected = Set(pairs.flatMap { $0 })
        for (stage, count) in [(SatchelShaderStage.fragment, 240), (.vertex, 260), (.compute, 4)] {
            let existing = selected.filter { all[$0]?.stage == stage }.count
            for row in spaced(inventory.records.filter { $0.stage == stage && !selected.contains($0.key) }, count: count - existing) { selected.insert(row.key) }
        }
        let rows = inventory.records.filter { selected.contains($0.key) }
        guard rows.count <= 2000 else { throw SatchelShaderError.invalidRequest }
        let cache = runtime.appendingPathComponent("shader-cache")
        guard !FileManager.default.fileExists(atPath: cache.path) else { throw SatchelShaderError.invalidRequest }
        let reserve: UInt64 = 35 * 1024 * 1024 * 1024
        let resources = try ShaderWarmupResources.get(directory: cache, reserve: Int(reserve))
        let warmer = try SatchelShaderWarmup(store: resources.store, compiler: resources.compiler)
        var requests: [String: String] = [:], masks: [String: UInt32] = [:]
        var phaseSeconds: [String: Double] = [:], counts: [String: Int] = [:]
        var sourceHits = 0, libraries = 0, pipelines = 0, archiveHits = 0, deferred = 0, failed = 0
        func save(_ state: String) throws {
            let data = try JSONSerialization.data(withJSONObject: [
                "state": state, "seconds": ProcessInfo.processInfo.systemUptime - started,
                "selectedRecords": rows.count, "selectedPairs": pairs.count, "completedByStage": counts,
                "phaseSeconds": phaseSeconds, "sourceHits": sourceHits, "libraries": libraries,
                "pipelines": pipelines, "archiveHits": archiveHits, "deferred": deferred, "failed": failed,
                "fullRecords": inventory.records.count, "fullPairs": inventory.pairs.count,
                "selection": "128 evenly spaced known pairs, filled by evenly spaced stage records to 240 fragment, 260 vertex, 4 compute; cached discovery, empty application shader and pipeline caches"
            ], options: [.sortedKeys])
            try data.write(to: runtime.appendingPathComponent("sample.json"), options: .atomic)
            print(String(decoding: data, as: UTF8.self)); fflush(stdout)
        }
        do {
            for (stage, label) in [(SatchelShaderStage.fragment, "fragment"), (.vertex, "vertex"), (.compute, "compute")] {
                let begin = ProcessInfo.processInfo.systemUptime
                let stageRows = rows.filter { $0.stage == stage }
                if stage == .vertex {
                    var wanted: [String: UInt32] = [:]
                    for pair in pairs { wanted[pair[0], default: 0] |= masks[pair[1]] ?? 0 }
                    try resources.mergeWanted(wanted)
                }
                for start in stride(from: 0, to: stageRows.count, by: 2) {
                    try check()
                    let batch = Array(stageRows[start..<min(start + 2, stageRows.count)])
                    let work = try batch.map { row in
                        let mask = row.stage == .vertex ? resources.mask(row.key) : 0
                        return try inventory.request(row, directory: directory, wanted: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 }))
                    }
                    var sent = 0
                    _ = try warmer.run(totalRecords: work.count, next: {
                        guard sent < work.count else { return nil }; defer { sent += 1 }; return work[sent]
                    }, cancelled: cancelled) { _, records in
                        for record in records {
                            requests[record.key] = record.requestSHA256
                            sourceHits += record.sourceCacheHit ? 1 : 0; libraries += record.compileSeconds.count
                            counts[label, default: 0] += 1
                        }
                    }
                    for row in batch where stage == .fragment {
                        if let id = requests[row.key], let shader = try resources.store.load(requestSHA256: id), let source = shader.sources["b"] {
                            let regex = try NSRegularExpression(pattern: #"\[\[user\(locn([0-9]+)\)"#), text = source as NSString
                            for match in regex.matches(in: source, range: NSRange(location: 0, length: text.length)) {
                                if let i = Int(text.substring(with: match.range(at: 1))), (0..<32).contains(i) { masks[row.key, default: 0] |= UInt32(1) << i }
                            }
                        }
                    }
                }
                phaseSeconds[label] = ProcessInfo.processInfo.systemUptime - begin
                try save("running")
            }
            let begin = ProcessInfo.processInfo.systemUptime
            let archive = try SatchelWarmupPipelines(directory: URL(fileURLWithPath: SatchelPipelineArchiveDirectory(runtime.path)), reserveBytes: reserve)
            defer { try? archive.flush() }
            func shader(_ key: String, pulling: Bool) throws -> (MTLLibrary, [AnyHashable: Any], Bool) {
                guard let id = requests[key], let value = try resources.store.load(requestSHA256: id),
                      let info = try JSONSerialization.jsonObject(with: value.reflectionJSON) as? [AnyHashable: Any] else { throw SatchelShaderError.invalidRequest }
                let suffix = pulling && info["pull"] is NSDictionary ? "bp" : "b"
                let compiled = try resources.compiler.compile(value, variant: suffix, cancelled: cancelled)
                return (compiled.library, info, compiled.layerOutput)
            }
            for pair in pairs {
                try check()
                try autoreleasepool {
                    let (v, vi, layer) = try shader(pair[0], pulling: true), (f, fi, _) = try shader(pair[1], pulling: false)
                    if layer { deferred += 4; return }
                    for zero in [false, true] { for coverage in [false, true] {
                        try check()
                        var error: NSError?
                        let result = archive.warmVertex(v, fragment: f, vertexInfo: vi, fragmentInfo: fi, zero: zero, coverage: coverage, error: &error)
                        if result < 0 { failed += 1 }
                        else if result == 1 { archiveHits += 1 }
                        else if result == 2 { pipelines += 1 }
                        else { deferred += 1 }
                    } }
                }
            }
            try archive.flush()
            phaseSeconds["pipelines"] = ProcessInfo.processInfo.systemUptime - begin
            try save(failed + deferred == 0 ? "complete" : "partial")
        } catch {
            try? save(cancelled() ? "deadline" : "failed")
            throw error
        }
    }
}
