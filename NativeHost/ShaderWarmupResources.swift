import Foundation
import Metal

/// The warm-up and renderer use the same bounded compiler and source store.
final class ShaderWarmupResources: @unchecked Sendable {
    let store: SatchelShaderStore
    let compiler: SatchelShaderCompiler
    private let lock = NSLock()
    private var wanted: [String: UInt32] = [:]
    let directory: URL
    private static let poolLock = NSLock()
    private static var pool: [String: ShaderWarmupResources] = [:]
    private static var recent: [String] = []
    private static let workers: OperationQueue = {
        let queue = OperationQueue()
        queue.name = "Satchel shader work"
        queue.qualityOfService = .utility
        queue.maxConcurrentOperationCount = 2
        return queue
    }()
    private var preparing: [String: ShaderWork<SatchelTranslatedShader>] = [:]
    private var compiling: [String: ShaderWork<SatchelCompiledShader>] = [:]
    private var queuedBytes = 0
    private var prefetched: Set<String> = []
    private struct Startup: Codable {
        let request: String
        var variants: [String]
    }
    private var startup: [Startup] = []
    private var observed: [Startup] = []
    private var started = false
    private var replayingStartup = false
    private var session: UInt64 = 0
    private static let metadata = DispatchQueue(label: "Satchel shader startup index", qos: .utility)
    static func get(directory: URL, reserve: Int = 1536 * 1024 * 1024) throws -> ShaderWarmupResources {
        poolLock.lock(); defer { poolLock.unlock() }
        let canonical = directory.standardizedFileURL.resolvingSymlinksInPath()
        let key = canonical.path
        if let value = pool[key] {
            recent.removeAll { $0 == key }; recent.append(key)
            return value
        }
        let value = try ShaderWarmupResources(directory: canonical, reserve: reserve)
        recent.append(key)
        while recent.count > 3 { pool.removeValue(forKey: recent.removeFirst()) }
        pool[key] = value; return value
    }
    private init(directory: URL, reserve: Int) throws {
        guard let device = MTLCreateSystemDefaultDevice() else { throw SatchelShaderError.invalidRequest }
        self.directory = directory
        compiler = SatchelShaderCompiler(device: device, maximumLiveLibraries: 64)
        store = try SatchelShaderStore(directory: directory, deviceIdentity: compiler.deviceIdentity,
                                       maximumBytes: 12 * 1024 * 1024 * 1024, reserveBytes: reserve)
        let file = directory.appendingPathComponent("wanted.json")
        if let info = try? file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]),
           info.isRegularFile == true, info.isSymbolicLink != true, (info.fileSize ?? Int.max) <= 8 * 1024 * 1024,
           let data = try? Data(contentsOf: file), let values = try? JSONDecoder().decode([String: UInt32].self, from: data),
           values.count <= 100_000, values.keys.allSatisfy({ NativePreparationRecipe.hex($0, count: 16) }) {
            wanted = values
        }
        let index = directory.appendingPathComponent("startup.json")
        if let info = try? index.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]),
           info.isRegularFile == true, info.isSymbolicLink != true, (info.fileSize ?? Int.max) <= 128 * 1024,
           let data = try? Data(contentsOf: index), let values = try? JSONDecoder().decode([Startup].self, from: data),
           values.count <= 128, values.allSatisfy({ NativePreparationRecipe.hex($0.request, count: 64) &&
               !$0.variants.isEmpty && Set($0.variants).isSubset(of: SatchelTranslatedShader.allowedVariants) }) {
            startup = values
        }
    }
    func mask(_ key: String) -> UInt32 { lock.lock(); defer { lock.unlock() }; return wanted[key] ?? 0 }
    func mergeWanted(_ values: [String: UInt32]) throws {
        lock.lock(); defer { lock.unlock() }
        for (key, mask) in values { wanted[key, default: 0] |= mask }
        guard wanted.count <= 100_000 else { throw SatchelShaderError.invalidRequest }
        try JSONEncoder().encode(wanted).write(to: directory.appendingPathComponent("wanted.json"), options: .atomic)
    }

    // All queued payloads are value snapshots. No worker retains guest pointers or
    // installs signal handlers. NativeAOT continues to own SIGUSR1.
    private func translation(_ request: SatchelShaderRequest, demanded: Bool) -> ShaderWork<SatchelTranslatedShader>? {
        let mask = request.wantedPixelLocations.sorted().map(String.init).joined(separator: ",")
        let facts = request.factsJSON.map { SatchelShaderTranslator.sha256($0) } ?? "none"
        let ranges = request.skippedControlRanges.map { "\($0.lowerBound):\($0.upperBound)" }.joined(separator: ",")
        let identity = [SatchelShaderTranslator.sha256(request.record), request.stage.rawValue, mask, facts,
                        String(request.mappingOnly), String(request.dualSource), ranges, request.expectedKey ?? "none"].joined(separator: "/")
        lock.lock(); defer { lock.unlock() }
        if let task = preparing[identity] { if demanded { task.promote() }; return task }
        if !demanded && prefetched.contains(identity) { return nil }
        if !demanded && (preparing.count + compiling.count >= 64 || queuedBytes + request.record.count > 32 * 1024 * 1024) { return nil }
        if !demanded && prefetched.count < 65536 { prefetched.insert(identity) }
        let task = ShaderWork<SatchelTranslatedShader>(demanded: demanded)
        preparing[identity] = task; queuedBytes += request.record.count
        task.operation.addExecutionBlock { [self, weak task] in
            guard let task else { return }
            let result = Result { try autoreleasepool { try store.prepare(request) } }
            task.finish(result)
            lock.lock(); preparing.removeValue(forKey: identity); queuedBytes -= request.record.count; lock.unlock()
            if case .success(let shader) = result, request.stage != .vertex {
                // A vertex may still need fragment-input padding. Compile it only
                // on exact demand or from the observed startup list.
                for variant in ["b"] where shader.sources[variant] != nil {
                    _ = compilation(shader, variant: variant, demanded: false)
                }
            }
        }
        Self.workers.addOperation(task.operation)
        return task
    }
    func prepare(_ request: SatchelShaderRequest) throws -> SatchelTranslatedShader {
        try translation(request, demanded: true)!.value()
    }
    func prefetch(_ request: SatchelShaderRequest) {
        lock.lock(); let replaying = replayingStartup; lock.unlock()
        // A recorded launch already has exact requests to replay. Registration
        // would add unused shaders and new padding combinations ahead of demand.
        if !replaying { _ = translation(request, demanded: false) }
    }

    private func compilation(_ shader: SatchelTranslatedShader, variant: String, demanded: Bool) -> ShaderWork<SatchelCompiledShader>? {
        let identity = shader.requestSHA256 + "/" + variant
        lock.lock(); defer { lock.unlock() }
        if let task = compiling[identity] { if demanded { task.promote() }; return task }
        let bytes = shader.sources.values.reduce(0) { $0 + $1.utf8.count }
        if !demanded && (preparing.count + compiling.count >= 64 || queuedBytes + bytes > 64 * 1024 * 1024) { return nil }
        let task = ShaderWork<SatchelCompiledShader>(demanded: demanded)
        compiling[identity] = task; queuedBytes += bytes
        task.operation.addExecutionBlock { [self, weak task] in
            guard let task else { return }
            task.finish(Result { try autoreleasepool { try compiler.compile(shader, variant: variant) } })
            lock.lock(); compiling.removeValue(forKey: identity); queuedBytes -= bytes; lock.unlock()
        }
        Self.workers.addOperation(task.operation)
        return task
    }
    func compile(_ shader: SatchelTranslatedShader, variant: String) throws -> SatchelCompiledShader {
        let value = try compilation(shader, variant: variant, demanded: true)!.value()
        lock.lock(); let generation = session; lock.unlock()
        let request = shader.requestSHA256
        Self.metadata.async { self.remember(request, variant: variant, generation: generation) }
        return value
    }
    // Only successful, actually requested libraries enter the next-launch list.
    // The source store and Metal OS cache retain their existing version partitions.
    private func remember(_ request: String, variant: String, generation: UInt64) {
        lock.lock()
        guard generation == session else { lock.unlock(); return }
        if let i = observed.firstIndex(where: { $0.request == request }) {
            if observed[i].variants.contains(variant) { lock.unlock(); return }
            observed[i].variants.append(variant)
        } else {
            guard observed.count < 128 else { lock.unlock(); return }
            observed.append(Startup(request: request, variants: [variant]))
        }
        let requests = Set(observed.map(\.request))
        startup = Array((observed + startup.filter { !requests.contains($0.request) }).prefix(128))
        let snapshot = startup; lock.unlock()
        // A tiny atomic metadata write, never shader source or a guest record.
        if let data = try? JSONEncoder().encode(snapshot) {
            try? data.write(to: directory.appendingPathComponent("startup.json"), options: .atomic)
        }
    }
    func endSession() {
        // Finish the tiny index writes before resetting its session generation.
        Self.metadata.sync { }
        lock.lock(); defer { lock.unlock() }
        session &+= 1; started = false; replayingStartup = false; observed.removeAll(); prefetched.removeAll()
    }
    func warmKnown(wait: Bool = false, cancelled: @escaping () -> Bool = { false }) {
        lock.lock()
        guard !started else { lock.unlock(); return }
        started = true; replayingStartup = !startup.isEmpty
        // Leave half the 64-library live cache available for actual draws. A
        // 128-entry replay evicted its own work and competed with first use.
        var remaining = 32
        let known = startup.compactMap { row -> Startup? in
            guard remaining > 0 else { return nil }
            let variants = Array(row.variants.prefix(remaining)); remaining -= variants.count
            return Startup(request: row.request, variants: variants)
        }
        let generation = session; lock.unlock()
        // One producer streams the bounded startup list. It does not enqueue the
        // entire inventory or hold every translated shader in memory at once.
        let producer = DispatchWorkItem(qos: .utility) { [self] in
            var complete = true
            for row in known {
                lock.lock(); let current = session == generation; lock.unlock()
                if cancelled() || !current { complete = false; break }
                autoreleasepool {
                    guard let shader = try? store.load(requestSHA256: row.request) else { return }
                    let batch = row.variants.compactMap { compilation(shader, variant: $0, demanded: false) }
                    for task in batch { _ = try? task.value() }
                }
            }
            if !complete {
                lock.lock(); if session == generation { started = false }; lock.unlock()
            }
        }
        DispatchQueue.global(qos: .utility).async(execute: producer)
        if wait { producer.wait() }
    }
}

private final class ShaderWork<Value>: @unchecked Sendable {
    let operation = BlockOperation()
    private let lock = NSLock()
    private var result: Result<Value, Error>?
    init(demanded: Bool) {
        operation.qualityOfService = demanded ? .userInitiated : .utility
        operation.queuePriority = demanded ? .veryHigh : .normal
    }
    func promote() { operation.qualityOfService = .userInitiated; operation.queuePriority = .veryHigh }
    func finish(_ value: Result<Value, Error>) { lock.lock(); result = value; lock.unlock() }
    func value() throws -> Value {
        operation.waitUntilFinished()
        lock.lock(); defer { lock.unlock() }
        return try result!.get()
    }
}
