import Foundation
import Metal

struct WarmupProgress: Codable, Sendable {
    var stage = "Discovering known shaders"
    var completed = 0
    var total = 0
    var shaders = 0
    var libraries = 0
    var sourceHits = 0
    var pipelines = 0
    var archiveHits = 0
    var deferred = 0
    var failed = 0
    var firstFailure: String?
    var seconds: Double = 0
    var state = "running"
    var phase = 0
    // Optional for compatibility with progress written by earlier releases.
    var phaseOne: WarmupPhaseOneTiming? = nil
    /// The run's finished work in whole units, for iOS's progress: one per scanned file
    /// while discovering, then one per shader, then one per pipeline variant. Within a run
    /// `unitsDone` never goes back; `unitsTotal` grows only when discovery has found the
    /// shaders. Absent in progress written by earlier releases.
    var unitsDone: Int? = nil
    var unitsTotal: Int? = nil
    var fraction: Double? { total > 0 ? Double(completed) / Double(total) : nil }
    /// iOS's progress: `unitsDone` of `unitsTotal`, never more done than total, never an empty
    /// total. A finished run is whole even when it had nothing to build.
    var systemUnits: (done: Int64, total: Int64) {
        let total = Int64(max(1, unitsTotal ?? 0, unitsDone ?? 0))
        return (phase >= 3 ? total : Int64(max(0, unitsDone ?? 0)), total)
    }
}

/// The words a stopped run leaves, the same in progress.json and the saved job.
enum WarmupStopWords {
    static let paused = "Paused. Play is ready; resume to reuse cached work."
    static let stoppedByIOS = "Stopped by iOS in the background. It resumes when you return."
    static let stopped = "Warm-up stopped. Play remains ready; cached work is kept."
}

struct WarmupPhaseOneTiming: Codable, Sendable {
    var seconds: Double = 0
    var lookupSeconds: Double = 0
    var translationSeconds: Double = 0
    var persistenceSeconds: Double = 0
    var compilationSeconds: Double = 0
    var reusedLibraries = 0
    var lanes = 2
}

enum WarmupRunner {
    static func run(package: URL, runtime: URL, reserve: UInt64 = 1536 * 1024 * 1024,
                    cancelled: @escaping () -> Bool, captureDirectory: URL? = nil, execution: () -> SatchelWarmupPolicy = { .conservative },
                    expired: () -> Bool = { false }, progress: @escaping (WarmupProgress) -> Void) throws -> WarmupProgress {
        let fm = FileManager.default, started = ProcessInfo.processInfo.systemUptime
        let manifestData = try bounded(package.appendingPathComponent("manifest.json"), maximum: 32 * 1024 * 1024)
        guard manifestData.count <= 32 * 1024 * 1024,
              let manifest = try JSONSerialization.jsonObject(with: manifestData) as? [String: Any] else { throw SatchelShaderError.invalidRequest }
        let reviewed = manifest["format"] as? Int == 1
        let chain = manifest["shaderChain"] as? String ?? NativePreparationRecipe.shaderChain
        let digest = SatchelShaderTranslator.sha256(manifestData)
        let directory = runtime.appendingPathComponent("warmup/" + digest)
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let resources = try ShaderWarmupResources.get(directory: runtime.appendingPathComponent("shader-cache"), reserve: Int(reserve))
        var state = WarmupProgress(), lastStage = ""
        var lastReport = -Double.infinity
        var phaseOneStarted: Double?
        // Units finished in earlier phases, and units the later phases already know about.
        var unitBase = 0, unitTail = 0
        func emit() throws {
            state.seconds = ProcessInfo.processInfo.systemUptime - started
            if state.phase == 1, let phaseOneStarted { state.phaseOne?.seconds = ProcessInfo.processInfo.systemUptime - phaseOneStarted }
            state.unitsDone = unitBase + state.completed
            state.unitsTotal = unitBase + max(state.total, state.completed) + unitTail
            guard state.seconds - lastReport >= 0.2 || state.stage != lastStage || state.state != "running" || state.completed == state.total else { return }
            lastReport = state.seconds; lastStage = state.stage
            try JSONEncoder().encode(state).write(to: directory.appendingPathComponent("progress.json"), options: .atomic)
            progress(state)
        }
        func check() throws { if cancelled() { throw SatchelShaderError.cancelled } }
        func failed(_ error: Error, count: Int = 1) {
            state.failed += count
            if state.firstFailure == nil { state.firstFailure = diagnostic(error) }
        }
        do {
            try emit(); try check()
            let inventoryURL = directory.appendingPathComponent("inventory.json")
            var inventory: WarmupInventory
            if reviewed { inventory = WarmupInventory(manifest: digest, records: [], pairs: [], scannedFiles: 0) }
            else if let data = try? bounded(inventoryURL, maximum: 32 * 1024 * 1024),
               let cached = try? JSONDecoder().decode(WarmupInventory.self, from: data), cached.version == WarmupInventory.currentVersion, cached.manifest == digest,
               cached.records.count <= 100_000, cached.pairs.count <= 100_000 { inventory = cached }
            else {
                inventory = try WarmupInventory.discover(package: package, directory: directory, manifest: digest, reserve: reserve, cancelled: cancelled) { done, total in
                    state.completed = done; state.total = total; try? emit()
                }
            }
            // First-use capture is app-private and bounded. Normalize it again before use.
            let capture = captureDirectory ?? runtime.appendingPathComponent("shader-capture")
            var known = Set(inventory.records.map(\.key))
            let captures: [URL] = reviewed ? [] : ((try? fm.contentsOfDirectory(at: capture, includingPropertiesForKeys: nil)) ?? [])
            // The capture holds every shader stage and vertex/fragment pair the game drew in
            // play on this phone. Those are built first, so a partial run helps where the player
            // has been. Order only: every record and pair is still built, and resume still
            // reuses the cached work of each.
            var played = Set<String>(), playedPairs = Set<String>()
            for file in captures where file.pathExtension == "ctl" {
                try check()
                let key = file.deletingPathExtension().lastPathComponent
                if NativePreparationRecipe.hex(key, count: 16), played.count < 100_000 { played.insert(key) }
                guard NativePreparationRecipe.hex(key, count: 16), !known.contains(key), known.count < 100_000 else { continue }
                let ctl = try bounded(file, maximum: 0x880), code = try bounded(capture.appendingPathComponent(key + ".code"), maximum: 16 * 1024 * 1024)
                let stage = try SatchelShaderTranslator.stage(control: ctl, code: code)
                let request = try SatchelShaderRequest(control: ctl, code: code, stage: stage, expectedKey: key)
                guard try SatchelShaderTranslator.normalized(request).key == key else { throw SatchelShaderError.invalidRequest }
                try request.record.write(to: directory.appendingPathComponent(key + ".record"), options: .atomic)
                inventory.records.append(.init(key: key, stage: stage, digest: SatchelShaderTranslator.sha256(request.record))); known.insert(key)
            }
            var pairSet = Set(inventory.pairs.filter { $0.count == 2 }.map { $0.joined(separator: "\t") })
            var pairFiles = [package.appendingPathComponent("dm/shader_pairs.tsv"), package.appendingPathComponent("dm/pso_pairs.tsv")]
            if !reviewed { pairFiles.append(capture.appendingPathComponent("draw-pairs.tsv")) }
            let drawn = capture.appendingPathComponent("draw-pairs.tsv")
            for path in pairFiles {
                if let data = try? bounded(path, maximum: 32 * 1024 * 1024) {
                    for row in String(decoding: data, as: UTF8.self).split(separator: "\n") where !row.hasPrefix("#") {
                        let c = row.split(separator: "\t").map(String.init)
                        if c.count >= 2, c.prefix(2).allSatisfy({ NativePreparationRecipe.hex($0, count: 16) }) {
                            pairSet.insert(c.prefix(2).joined(separator: "\t"))
                            if path == drawn, playedPairs.count < 100_000 { playedPairs.insert(c.prefix(2).joined(separator: "\t")) }
                        }
                    }
                }
            }
            guard pairSet.count <= 100_000 else { throw SatchelShaderError.invalidRequest }
            inventory.pairs = playedFirst(pairSet.sorted(), played: playedPairs) { $0 }.map { $0.components(separatedBy: "\t") }
            let warmer = try SatchelShaderWarmup(store: resources.store, compiler: resources.compiler,
                maximumConcurrentRecords: 4, workingMemoryBudgetBytes: 1024 * 1024 * 1024, maximumBufferedRecords: 16)
            defer { resources.compiler.setMaximumConcurrentCompilations(2) }
            var requests: [String: String] = [:], masks: [String: UInt32] = [:]
            // Discovery's files stay counted; the pipeline variants are known from here on.
            unitBase = state.completed; unitTail = inventory.pairs.count * 4
            if reviewed {
                // These bound libraries are already translated and compiled. The format 1
                // renderer uses them directly, so translating their records again is wasted work.
                let files = (manifest["files"] as? [String: Any] ?? [:]).keys.filter {
                    $0.hasPrefix("msl/b-" + chain + "/") && $0.hasSuffix(".metallib")
                }.sorted()
                state.phase = 1; state.stage = "Loading prepared shaders"; state.completed = 0; state.total = files.count; try emit()
                for path in files {
                    try check()
                    autoreleasepool {
                        do {
                            let bytes = try bounded(package.appendingPathComponent(path), maximum: 64 * 1024 * 1024)
                            _ = try resources.compiler.device.makeLibrary(data: bytes.withUnsafeBytes { DispatchData(bytes: $0) })
                            state.libraries += 1
                            if path.hasSuffix("_b.metallib") { state.shaders += 1 }
                        } catch { failed(error) }
                    }
                    state.completed += 1; try emit()
                }
            } else {
                phaseOneStarted = ProcessInfo.processInfo.systemUptime; state.phaseOne = WarmupPhaseOneTiming()
                state.phase = 1; state.stage = "Translating and compiling shaders"; state.completed = 0; state.total = inventory.records.count; try emit()
                // Each shader counts as it finishes, not when its 16-record batch ends: in the
                // background one batch took over a minute, and iOS ends a task whose progress
                // stands still that long. A record stopped by cancellation is not counted.
                var successes = 0
                warmer.recordFinished = { result in
                    switch result {
                    case .success(let record):
                        requests[record.key] = record.requestSHA256; state.shaders += 1; state.libraries += record.compileSeconds.count; successes += 1
                        if record.sourceCacheHit { state.sourceHits += 1 }
                        masks[record.key] = record.fragmentInputMask
                        state.phaseOne?.lookupSeconds += record.lookupSeconds
                        state.phaseOne?.translationSeconds += record.translationSeconds
                        state.phaseOne?.persistenceSeconds += record.persistenceSeconds
                        state.phaseOne?.compilationSeconds += record.compileSeconds.values.reduce(0, +)
                        state.phaseOne?.reusedLibraries += record.liveLibraryHits + record.warmupLibraryHits
                        state.completed += 1
                    case .failure:
                        // Counted as failed with its batch below, as before.
                        if !cancelled() { state.completed += 1 }
                    }
                    // The batch's own emit below reports a write failure.
                    try? emit()
                }
                defer { warmer.recordFinished = nil }
                // Fragment inputs determine the exact vertex padding shared with first use.
                for group in [inventory.records.filter { $0.stage == .fragment }, inventory.records.filter { $0.stage != .fragment }] {
                    let rows = playedFirst(group, played: played) { $0.key }
                    if rows.first?.stage != .fragment {
                        var wanted: [String: UInt32] = [:]
                        for pair in inventory.pairs { wanted[pair[0], default: 0] |= masks[pair[1]] ?? 0 }
                        try resources.mergeWanted(wanted)
                    }
                    for start in stride(from: 0, to: rows.count, by: 16) {
                        try check()
                        let policy = execution(); warmer.configure(policy); state.phaseOne?.lanes = warmer.concurrentRecords
                        let batch = Array(rows[start..<min(start + 16, rows.count)])
                        let before = state.completed
                        var work: [SatchelShaderRequest] = []
                        for row in batch {
                            do {
                                let mask = row.stage == .vertex ? resources.mask(row.key) : 0
                                work.append(try inventory.request(row, directory: directory, wanted: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 })))
                            } catch { failed(error); state.completed += 1 }
                        }
                        var sent = 0
                        successes = 0
                        do {
                            _ = try warmer.run(totalRecords: work.count, next: {
                                guard sent < work.count else { return nil }; defer { sent += 1 }; return work[sent]
                            }, cancelled: cancelled)
                        } catch { try check(); failed(error, count: work.count - successes) }
                        // A batch that was not cancelled is complete: the same count as before.
                        state.completed = before + batch.count; try emit()
                    }
                }
            }
            unitBase += state.total; unitTail = 0
            state.phase = 2; state.stage = "Building device pipelines"; state.completed = 0; state.total = inventory.pairs.count * 4; try emit()
            let archive = try SatchelWarmupPipelines(directory: URL(fileURLWithPath: SatchelPipelineArchiveDirectory(runtime.path)), reserveBytes: reserve)
            // Flush successful batches even on cancellation. No partially written archive is published.
            defer { try? SatchelShaderMetrics.measure("archive-write") { try archive.flush() } }
            let msl = package.appendingPathComponent("msl/b-" + chain)
            func shader(_ key: String, pulling: Bool) throws -> (MTLLibrary, [AnyHashable: Any], Bool) {
                if reviewed, let data = try? bounded(msl.appendingPathComponent(key + "_b.json"), maximum: 4 * 1024 * 1024),
                   let info = try JSONSerialization.jsonObject(with: data) as? [AnyHashable: Any] {
                    let suffix = pulling && info["pull"] is NSDictionary ? "bp" : "b"
                    let file = msl.appendingPathComponent(key + "_" + suffix + ".metallib")
                    let bytes = try bounded(file, maximum: 64 * 1024 * 1024)
                    let library = try resources.compiler.device.makeLibrary(data: bytes.withUnsafeBytes { DispatchData(bytes: $0) })
                    return (library, info, false)
                }
                guard let id = requests[key], let value = try resources.store.load(requestSHA256: id),
                      let info = try JSONSerialization.jsonObject(with: value.reflectionJSON) as? [AnyHashable: Any] else { throw SatchelShaderError.invalidRequest }
                let suffix = pulling && info["pull"] is NSDictionary ? "bp" : "b"
                let compiled = try resources.compiler.compile(value, variant: suffix, cancelled: cancelled)
                return (compiled.library, info, compiled.layerOutput)
            }
            for pair in inventory.pairs {
                try check()
                try autoreleasepool {
                    do {
                        let (v, vi, layer) = try shader(pair[0], pulling: true), (f, fi, _) = try shader(pair[1], pulling: false)
                        if layer { state.deferred += 4; state.completed += 4; return }
                        for zero in [false, true] { for coverage in [false, true] {
                            try check()
                            do {
                                var error: NSError?
                                let result = archive.warmVertex(v, fragment: f, vertexInfo: vi, fragmentInfo: fi, zero: zero, coverage: coverage, error: &error)
                                if result < 0 { throw error ?? SatchelShaderError.translationFailed as NSError }
                                if result == 1 { state.archiveHits += 1 }
                                else if result == 2 { state.pipelines += 1 }
                                else { state.deferred += 1 }
                            } catch { try check(); failed(error) }
                            // Each variant counts as it is built, not only after its pair's four.
                            state.completed += 1; try emit()
                        } }
                    } catch { try check(); state.deferred += 4; state.completed += 4 }
                }
                // Match the pinned engine's bounded serializer period.
                if state.completed % 1000 == 0 { try SatchelShaderMetrics.measure("archive-write") { try archive.flush() } }
                try emit()
            }
            try SatchelShaderMetrics.measure("archive-write") { try archive.flush() }; try check()
            resources.endSession()
            resources.warmKnown(wait: true, cancelled: cancelled)
            try check()
            state.phase = 3; state.state = "complete"
            state.stage = "Known shaders warmed. New shaders still build while playing."
            if state.failed > 0 || state.deferred > 0 { state.state = "partial"; state.stage = "Warm-up finished with some shaders left to gameplay." }
            if state.failed == 0 && state.deferred == 0 && state.shaders == 0 && state.pipelines + state.archiveHits == 0 { state.stage = "No known shaders yet. Play to discover them, then warm up again." }
            try emit(); return state
        } catch {
            // The same words the saved job shows for this stop (WarmupJob.finish).
            state.state = cancelled() ? "paused" : "failed"
            state.stage = !cancelled() ? WarmupStopWords.stopped : expired() ? WarmupStopWords.stoppedByIOS : WarmupStopWords.paused
            try? emit(); throw error
        }
    }
    /// The rows the game used in play first, each part in its original order. A reorder only.
    static func playedFirst<Row>(_ rows: [Row], played: Set<String>, key: (Row) -> String) -> [Row] {
        guard !played.isEmpty else { return rows }
        return rows.filter { played.contains(key($0)) } + rows.filter { !played.contains(key($0)) }
    }
    // Compiler diagnostics can contain MSL. Persist only a typed error or code.
    static func diagnostic(_ error: Error) -> String {
        switch error {
        case let value as SatchelShaderError: return String(describing: value)
        case let value as SatchelShaderCompilationError: return String(describing: value)
        case let value as SatchelShaderCacheError: return String(describing: value)
        case let value as SatchelShaderDiscoveryError: return String(describing: value)
        default: return String(reflecting: type(of: error)) + " (code \((error as NSError).code))"
        }
    }
    static func bounded(_ url: URL, maximum: Int) throws -> Data {
        let attrs = try url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
        guard attrs.isRegularFile == true, attrs.isSymbolicLink != true, (attrs.fileSize ?? Int.max) <= maximum else { throw SatchelShaderError.invalidRequest }
        return try Data(contentsOf: url)
    }
}
