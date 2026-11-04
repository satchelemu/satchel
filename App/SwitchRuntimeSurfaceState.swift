import Foundation

// Pure attachment metadata. No view, frame, CPU or callback is retained here.
final class SwitchRuntimeSurfaceState: @unchecked Sendable {
    struct Snapshot: Equatable, Sendable {
        let epoch: UInt64
        let pixelWidth: UInt32
        let pixelHeight: UInt32
    }

    private final class EpochSource: @unchecked Sendable {
        static let shared = EpochSource()
        private let lock = NSLock()
        private var last: UInt64
        init(last: UInt64 = 0) { self.last = last }
        func issue() -> UInt64? {
            lock.lock(); defer { lock.unlock() }
            guard last < UInt64.max else { return nil }
            last += 1
            return last
        }
    }

    private let lock = NSLock()
    private let epochs: EpochSource
    private var binding: UInt64?
    private var current: Snapshot?

    init() { epochs = .shared }

    func snapshot() -> Snapshot? {
        lock.lock(); defer { lock.unlock() }
        return current
    }

    // Instant validation, not a lease. A future main-thread presentation must
    // revalidate; this does not authorize rendering or attest frame contents.
    func validate(epoch: UInt64) -> Bool {
        lock.lock(); defer { lock.unlock() }
        return epoch != 0 && current?.epoch == epoch
    }

    @MainActor func beginBinding() -> UInt64? {
        lock.lock(); defer { lock.unlock() }
        current = nil
        binding = epochs.issue()
        return binding
    }

    @MainActor func update(binding token: UInt64, pixelWidth: UInt32,
                           pixelHeight: UInt32, attached: Bool) {
        lock.lock(); defer { lock.unlock() }
        guard binding == token else { return }
        guard attached, pixelWidth > 0, pixelHeight > 0 else {
            current = nil
            return
        }
        // A live surface keeps identity across ordinary layout changes.
        guard let epoch = current?.epoch ?? epochs.issue() else { return }
        current = Snapshot(epoch: epoch, pixelWidth: pixelWidth, pixelHeight: pixelHeight)
    }

    @MainActor func detach(binding token: UInt64) {
        lock.lock(); defer { lock.unlock() }
        if binding == token { current = nil }
    }

    @MainActor func endBinding(_ token: UInt64) {
        lock.lock(); defer { lock.unlock() }
        if binding == token { binding = nil; current = nil }
    }

    #if SURFACE_TESTING
    // Test-only exhaustion setup; absent from normal App compilation.
    init(testLastEpoch: UInt64) { epochs = EpochSource(last: testLastEpoch) }
    #endif
}
