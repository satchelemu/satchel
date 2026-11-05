import Foundation
import Metal

@available(macOS 15.0, iOS 18.0, *)
enum WarmupChecks {
    static func run(request: SatchelShaderRequest, root: URL, device: MTLDevice) throws -> [String: Any] {
        func require(_ value: Bool) throws { if !value { throw SatchelShaderError.translationFailed } }
        let compiler = SatchelShaderCompiler(device: device, maximumLiveLibraries: 8)
        let store = try SatchelShaderStore(directory: root, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
        let warmup = try SatchelShaderWarmup(store: store, compiler: compiler)
        var supplied = 0, completed = 0, maxOutstanding = 0, reports = 0
        let first = try warmup.run(totalRecords: 3, next: {
            guard supplied < 3 else { return nil }
            supplied += 1; maxOutstanding = max(maxOutstanding, supplied - completed)
            return request
        }, progress: { state, records in
            completed = state.completedRecords; reports += 1
            try require(records.count <= 2)
        })
        try require(first.completedRecords == 3 && first.sourceCacheHits == 2 && first.compiledLibraries == 1 && first.liveLibraryHits + first.warmupLibraryHits == 2 && first.warmupLibraryHits >= 1 && maxOutstanding == 2)
        var cancelledReads = 0, rejected = false
        do { try warmup.run(next: { cancelledReads += 1; return request }, cancelled: { true }) }
        catch SatchelShaderError.cancelled { rejected = true }
        try require(rejected && cancelledReads == 0)
        let one = try SatchelShaderWarmup(store: store, compiler: compiler, workingMemoryBudgetBytes: 256 * 1024 * 1024)
        let cancellation = WarmupCancellation()
        var afterProgressReads = 0, partial = 0
        rejected = false
        do {
            try one.run(next: { afterProgressReads += 1; return request }, cancelled: { cancellation.value }, progress: { state, _ in
                partial = state.completedRecords
                if partial == 1 { cancellation.cancel() }
            })
        } catch SatchelShaderError.cancelled { rejected = true }
        try require(rejected && partial == 1 && afterProgressReads == 1)
        var reloadReads = 0
        let newStore = try SatchelShaderStore(directory: root, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
        let reload = try SatchelShaderWarmup(store: newStore, compiler: SatchelShaderCompiler(device: device))
        let resumed = try reload.run(next: { defer { reloadReads += 1 }; return reloadReads == 0 ? request : nil })
        try require(resumed.completedRecords == 1 && resumed.sourceCacheHits == 1 && resumed.compiledLibraries == 1 && resumed.liveLibraryHits == 0)
        var failed = false, failuresRead = 0, successful = 0
        let invalid = SatchelShaderRequest(record: Data(), stage: .hull)
        do {
            try warmup.run(next: { defer { failuresRead += 1 }; return failuresRead == 0 ? request : failuresRead == 1 ? invalid : nil }, progress: { state, _ in successful = state.completedRecords })
        } catch { failed = true }
        try require(failed && successful == 1 && failuresRead == 2)
        var earlyReads = 0
        rejected = false
        do { try one.run(totalRecords: 2, next: { defer { earlyReads += 1 }; return earlyReads == 0 ? request : nil }) }
        catch SatchelShaderError.invalidRequest { rejected = true }
        try require(rejected)
        return ["shortInventoryRejected": true, "boundedRecords": maxOutstanding, "duplicateTranslationCoalesced": true, "duplicateCompilationCoalesced": true,
                "cancelBeforeRead": true, "cancelAfterProgress": true, "resumeSourceHit": true,
                "restartReacquiresLibrary": true, "failedInventoryNotCompleted": true, "progressCallbacks": reports]
    }
}

private final class WarmupCancellation {
    private let lock = NSLock()
    private var cancelled = false
    var value: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}
