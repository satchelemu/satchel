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
}

public struct SatchelShaderWarmupProgress: Codable, Sendable {
    public fileprivate(set) var completedRecords = 0
    public fileprivate(set) var sourceCacheHits = 0
    public fileprivate(set) var compiledLibraries = 0
    public fileprivate(set) var liveLibraryHits = 0
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
    public let concurrentRecords: Int
    private let store: SatchelShaderStore

    public init(store: SatchelShaderStore, compiler: SatchelShaderCompiler, maximumConcurrentRecords: Int = 2,
                workingMemoryBudgetBytes: Int = 512 * 1024 * 1024, estimatedBytesPerWorker: Int = 256 * 1024 * 1024) throws {
        guard (1...2).contains(maximumConcurrentRecords), estimatedBytesPerWorker > 0,
              workingMemoryBudgetBytes >= estimatedBytesPerWorker else { throw SatchelShaderError.invalidRequest }
        self.store = store; self.compiler = compiler
        concurrentRecords = min(maximumConcurrentRecords, workingMemoryBudgetBytes / estimatedBytesPerWorker)
    }

    private func one(_ request: SatchelShaderRequest, cancelled: () -> Bool) throws -> SatchelShaderWarmupRecord {
        try autoreleasepool {
            let prepared = try store.prepareMeasured(request, cancelled: cancelled)
            var seconds: [String: Double] = [:], hits = 0
            for suffix in prepared.shader.sources.keys.sorted() {
                if cancelled() { throw SatchelShaderError.cancelled }
                let compiled = try compiler.compile(prepared.shader, variant: suffix, cancelled: cancelled)
                seconds[suffix] = compiled.seconds
                if compiled.liveCacheHit { hits += 1 }
            }
            return SatchelShaderWarmupRecord(key: prepared.shader.key, requestSHA256: prepared.shader.requestSHA256,
                sourceCacheHit: prepared.sourceCacheHit, lookupSeconds: prepared.lookupSeconds,
                translationSeconds: prepared.translationSeconds, persistenceSeconds: prepared.persistenceSeconds,
                compileSeconds: seconds, liveLibraryHits: hits)
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
            for _ in 0..<concurrentRecords {
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
            DispatchQueue.concurrentPerform(iterations: batch.count) { index in
                let result = Result { try self.one(batch[index], cancelled: cancelled) }
                results.set(result, at: index)
            }
            var completed: [SatchelShaderWarmupRecord] = [], failure: Error?
            for result in results.values {
                switch result! {
                case .success(let record):
                    completed.append(record); state.completedRecords += 1
                    if record.sourceCacheHit { state.sourceCacheHits += 1 }
                    state.liveLibraryHits += record.liveLibraryHits
                    state.compiledLibraries += record.compileSeconds.count - record.liveLibraryHits
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
