import Foundation

public struct SatchelShaderWarmupRecord: Codable, Sendable {
    public let key: String
    public let requestSHA256: String
    public let sourceCacheHit: Bool
    public let lookupSeconds: Double
    public let translationSeconds: Double
    public let persistenceSeconds: Double
    public let compileSeconds: [String: Double]
    public let liveLibraryHits: Int
    public let warmupLibraryHits: Int
    public let fragmentInputMask: UInt32
}

public struct SatchelShaderWarmupProgress: Codable, Sendable {
    public fileprivate(set) var completedRecords = 0
    public fileprivate(set) var sourceCacheHits = 0
    public fileprivate(set) var compiledLibraries = 0
    public fileprivate(set) var liveLibraryHits = 0
    public fileprivate(set) var warmupLibraryHits = 0
    public fileprivate(set) var translationSeconds: Double = 0
    public fileprivate(set) var compilationSeconds: Double = 0
    public fileprivate(set) var elapsedSeconds: Double = 0
    public let totalRecords: Int?
    public let concurrentRecords: Int
}

// Synchronous bounded driver. Run on the app's existing background task queue.
// The producer and progress callbacks run serially on the calling thread.
// Cancellation can be queried concurrently and must be safe on worker threads.
@available(iOS 18.0, macOS 15.0, *)
public final class SatchelShaderWarmup {
    public let compiler: SatchelShaderCompiler
    public private(set) var concurrentRecords: Int
    private let maximumRecords: Int
    private let store: SatchelShaderStore
    private let batchSize: Int
    private let queue: OperationQueue
    private let warmedLock = NSLock()
    private var warmed: Set<String> = []

    public init(store: SatchelShaderStore, compiler: SatchelShaderCompiler, maximumConcurrentRecords: Int = 2,
                workingMemoryBudgetBytes: Int = 512 * 1024 * 1024, estimatedBytesPerWorker: Int = 256 * 1024 * 1024,
                maximumBufferedRecords: Int? = nil, qualityOfService: QualityOfService = .utility) throws {
        guard (1...4).contains(maximumConcurrentRecords), estimatedBytesPerWorker > 0,
              workingMemoryBudgetBytes >= estimatedBytesPerWorker else { throw SatchelShaderError.invalidRequest }
        self.store = store; self.compiler = compiler
        concurrentRecords = min(maximumConcurrentRecords, workingMemoryBudgetBytes / estimatedBytesPerWorker)
        maximumRecords = concurrentRecords
        batchSize = max(concurrentRecords, min(maximumBufferedRecords ?? concurrentRecords, 16))
        queue = OperationQueue(); queue.name = "Satchel optional shader warm-up"
        queue.maxConcurrentOperationCount = concurrentRecords; queue.qualityOfService = qualityOfService
    }

    // Call only between drained batches. The compiler and translation lanes
    // use the same bounded policy, without changing renderer specialization.
    func configure(_ policy: SatchelWarmupPolicy) {
        concurrentRecords = min(maximumRecords, policy.lanes)
        queue.maxConcurrentOperationCount = concurrentRecords
        queue.qualityOfService = policy.qos
        compiler.setMaximumConcurrentCompilations(concurrentRecords)
    }

    private func one(_ request: SatchelShaderRequest, cancelled: () -> Bool) throws -> SatchelShaderWarmupRecord {
        try autoreleasepool {
            let prepared = try store.prepareMeasured(request, cancelled: cancelled)
            var seconds: [String: Double] = [:], hits = 0, warmHits = 0
            for suffix in prepared.shader.sources.keys.sorted() {
                if cancelled() { throw SatchelShaderError.cancelled }
                let hash = SatchelShaderTranslator.sha256(Data(prepared.shader.sources[suffix]!.utf8))
                warmedLock.lock(); let seen = warmed.contains(hash); warmedLock.unlock()
                if seen { seconds[suffix] = 0; warmHits += 1; continue }
                let compiled = try compiler.compile(prepared.shader, variant: suffix, cancelled: cancelled)
                seconds[suffix] = compiled.seconds
                if compiled.liveCacheHit { hits += 1 }
                warmedLock.lock(); if warmed.count < 65536 { warmed.insert(hash) }; warmedLock.unlock()
            }
            var mask: UInt32 = 0
            if request.stage == .fragment, let source = prepared.shader.sources["b"] {
                for found in matches(#"\[\[user\(locn([0-9]+)\)"#, source) {
                    if let i = Int(found[1]), (0..<32).contains(i) { mask |= UInt32(1) << i }
                }
            }
            return SatchelShaderWarmupRecord(key: prepared.shader.key, requestSHA256: prepared.shader.requestSHA256,
                sourceCacheHit: prepared.sourceCacheHit, lookupSeconds: prepared.lookupSeconds,
                translationSeconds: prepared.translationSeconds, persistenceSeconds: prepared.persistenceSeconds,
                compileSeconds: seconds, liveLibraryHits: hits, warmupLibraryHits: warmHits, fragmentInputMask: mask)
        }
    }

    // Source cache entries are durable before compilation starts. Re-running a
    // cancelled inventory reuses them. Always reacquire libraries in this process:
    // a persisted completion count is not proof Metal still has executable code.
    // Progress contains only completed records, including successes in a failed
    // batch. A thrown failure or cancellation never becomes a completed inventory.
    @discardableResult
    public func run(totalRecords: Int? = nil, next: () throws -> SatchelShaderRequest?,
                    cancelled: @escaping () -> Bool = { false },
                    progress: (SatchelShaderWarmupProgress, [SatchelShaderWarmupRecord]) throws -> Void = { _, _ in }) throws -> SatchelShaderWarmupProgress {
        guard totalRecords == nil || totalRecords! >= 0 else { throw SatchelShaderError.invalidRequest }
        let started = ProcessInfo.processInfo.systemUptime
        var state = SatchelShaderWarmupProgress(totalRecords: totalRecords, concurrentRecords: concurrentRecords)
        try progress(state, [])
        while true {
            if cancelled() { throw SatchelShaderError.cancelled }
            var batch: [SatchelShaderRequest] = []
            for _ in 0..<batchSize {
                if cancelled() { throw SatchelShaderError.cancelled }
                guard let request = try next() else { break }
                batch.append(request)
            }
            if batch.isEmpty {
                guard totalRecords == nil || totalRecords == state.completedRecords else { throw SatchelShaderError.invalidRequest }
                return state
            }
            if let totalRecords, batch.count > totalRecords - state.completedRecords { throw SatchelShaderError.invalidRequest }
            let results = WarmupBatch(count: batch.count)
            let operations = batch.enumerated().map { index, request in
                BlockOperation {
                    let result = Result { try self.one(request, cancelled: cancelled) }
                    results.set(result, at: index)
                }
            }
            queue.addOperations(operations, waitUntilFinished: true)
            var completed: [SatchelShaderWarmupRecord] = [], failure: Error?
            for result in results.values {
                switch result! {
                case .success(let record):
                    completed.append(record); state.completedRecords += 1
                    if record.sourceCacheHit { state.sourceCacheHits += 1 }
                    state.liveLibraryHits += record.liveLibraryHits
                    state.warmupLibraryHits += record.warmupLibraryHits
                    state.compiledLibraries += record.compileSeconds.count - record.liveLibraryHits - record.warmupLibraryHits
                    state.translationSeconds += record.translationSeconds
                    state.compilationSeconds += record.compileSeconds.values.reduce(0, +)
                case .failure(let error): if failure == nil { failure = error }
                }
            }
            state.elapsedSeconds = ProcessInfo.processInfo.systemUptime - started
            try progress(state, completed)
            if let failure { throw failure }
        }
    }
}

private final class WarmupBatch: @unchecked Sendable {
    let lock = NSLock()
    var values: [Result<SatchelShaderWarmupRecord, Error>?]
    init(count: Int) { values = Array(repeating: nil, count: count) }
    func set(_ result: Result<SatchelShaderWarmupRecord, Error>, at index: Int) {
        lock.lock(); values[index] = result; lock.unlock()
    }
}

// A pure policy so the app and Mac checks share the same bounds.
struct SatchelWarmupPolicy: Equatable, Sendable {
    let lanes: Int
    let foreground: Bool
    static let conservative = SatchelWarmupPolicy(lanes: 2, foreground: false)
    static func select(foreground: Bool, charging: Bool, constrained: Bool) -> Self {
        Self(lanes: !constrained && (foreground || charging) ? 4 : 2,
             foreground: foreground && !constrained)
    }
    var qos: QualityOfService { foreground ? .userInitiated : .utility }
}

// Developer measurement arguments isolate optional cache work from player data.
struct SatchelWarmupTrial {
    let name: String?
    let records: Int?
    init(arguments: [String]) {
        func value(_ flag: String) -> String? {
            guard let i = arguments.firstIndex(of: flag), i + 1 < arguments.count else { return nil }
            return arguments[i + 1]
        }
        let allowed = Set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
        let tag = value("-SatchelWarmupTrial")
        name = tag.flatMap { !$0.isEmpty && $0.count <= 48 && $0.allSatisfy(allowed.contains) ? $0 : nil }
        // A limit requires an isolated trial so an ordinary warm-up is unaffected.
        let count = value("-SatchelWarmupRecords").flatMap(Int.init)
        records = name != nil ? count.flatMap { (1...100_000).contains($0) ? $0 : nil } : nil
    }
}
