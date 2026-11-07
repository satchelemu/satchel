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
        let buffered = try SatchelShaderWarmup(store: store, compiler: compiler,
            maximumConcurrentRecords: 4, workingMemoryBudgetBytes: 1024 * 1024 * 1024, maximumBufferedRecords: 16)
        buffered.configure(.select(foreground: true, charging: false, constrained: false))
        try require(buffered.concurrentRecords == 4)
        for policy in [SatchelWarmupPolicy.select(foreground: true, charging: false, constrained: false), .conservative] {
            buffered.configure(policy)
            var read = 0, acknowledged = 0, peak = 0, finished = 0, early = 0, behind = false
            // Each record is reported as it finishes, before its batch's progress, so a slow
            // batch still moves the job's progress.
            buffered.recordFinished = { result in
                if case .success = result { finished += 1 }
                if finished > acknowledged { early += 1 }
            }
            let state = try buffered.run(totalRecords: 33, next: {
                guard read < 33 else { return nil }
                read += 1; peak = max(peak, read - acknowledged); return request
            }, progress: { state, _ in
                if finished != state.completedRecords { behind = true }
                acknowledged = state.completedRecords
            })
            buffered.recordFinished = nil
            try require(state.concurrentRecords == policy.lanes && state.completedRecords == 33 && peak == 16)
            try require(finished == 33 && early == 33 && !behind)
            try require(state.sourceCacheHits == 33 && state.compiledLibraries == 0 && state.liveLibraryHits + state.warmupLibraryHits == 33)
        }
        try require(buffered.concurrentRecords == 2)
        // Distinct cold requests exercise NativeAOT and source generation on
        // four lanes, then compare every translated field with serial execution.
        buffered.configure(.select(foreground: true, charging: false, constrained: false))
        var distinct: [SatchelShaderRequest] = []
        let controlOffset = 0x38 + Int(try ShaderRecord.word(request.record, 0x2c))
        for x in 2...17 {
            var bytes = request.record; ShaderRecord.put(UInt32(x), &bytes, controlOffset + 0x748)
            distinct.append(SatchelShaderRequest(record: bytes, stage: .compute))
        }
        var distinctRead = 0
        let cold = try buffered.run(totalRecords: distinct.count, next: {
            guard distinctRead < distinct.count else { return nil }; defer { distinctRead += 1 }; return distinct[distinctRead]
        })
        try require(cold.completedRecords == 16 && cold.sourceCacheHits == 0 && cold.concurrentRecords == 4)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        for request in distinct {
            let serial = try SatchelShaderTranslator.translate(request)
            guard let concurrent = try store.load(requestSHA256: serial.requestSHA256) else { throw SatchelShaderCacheError.corruptEntry }
            try require(try encoder.encode(serial) == encoder.encode(concurrent))
        }
        buffered.configure(.conservative)
        // Eviction from the live library cache must not repeat a successful warm
        // compile in this driver. New drivers still reacquire executable code.
        compiler.trimLiveLibraries()
        var repeatReads = 0
        let duplicate = try warmup.run(totalRecords: 1, next: {
            defer { repeatReads += 1 }; return repeatReads == 0 ? request : nil
        })
        try require(duplicate.warmupLibraryHits == 1 && duplicate.compiledLibraries == 0)
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
