import Foundation

private final class ReadResults: @unchecked Sendable {
    private let lock = NSLock()
    private var invalid = 0
    private var reads = 0
    func record(_ value: SwitchRuntimeSurfaceState.Snapshot?) {
        lock.lock(); defer { lock.unlock() }
        reads += 1
        if let value, value.epoch == 0 || value.pixelWidth == 0 || value.pixelHeight == 0 { invalid += 1 }
    }
    func result() -> (Int, Int) {
        lock.lock(); defer { lock.unlock() }
        return (reads, invalid)
    }
}

@main struct RuntimeSurfaceTests {
    @MainActor static var checks = 0
    @MainActor static func check(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        if !condition() { fatalError(message) }
    }

    @MainActor static func main() {
        let state = SwitchRuntimeSurfaceState()
        check(state.snapshot() == nil && !state.validate(epoch: 0), "fresh owner has no attached surface")
        let first = state.beginBinding()!
        state.update(binding: first, pixelWidth: 0, pixelHeight: 100, attached: true)
        check(state.snapshot() == nil, "zero width cannot attach")
        state.update(binding: first, pixelWidth: 100, pixelHeight: 0, attached: true)
        check(state.snapshot() == nil, "zero height cannot attach")
        state.update(binding: first, pixelWidth: 1280, pixelHeight: 720, attached: false)
        check(state.snapshot() == nil, "nonzero layout without attachment cannot attach")
        state.update(binding: first, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let attached = state.snapshot()!
        check(state.validate(epoch: attached.epoch), "attached epoch validates")
        state.update(binding: first, pixelWidth: 1920, pixelHeight: 1080, attached: true)
        check(state.snapshot() == .init(epoch: attached.epoch, pixelWidth: 1920, pixelHeight: 1080), "resize preserves live identity and updates dimensions")
        state.update(binding: first, pixelWidth: 1920, pixelHeight: 1080, attached: true)
        check(state.snapshot()?.epoch == attached.epoch, "repeat layout does not churn epochs")
        // UIView/window replacement and scene inactivity explicitly detach.
        state.detach(binding: first)
        check(state.snapshot() == nil && !state.validate(epoch: attached.epoch), "detach immediately revokes old epoch")
        state.update(binding: first, pixelWidth: 1920, pixelHeight: 1080, attached: true)
        let reattached = state.snapshot()!
        check(reattached.epoch > attached.epoch && !state.validate(epoch: attached.epoch), "reattachment never reuses epoch")
        let replacement = state.beginBinding()!
        check(state.snapshot() == nil && !state.validate(epoch: reattached.epoch), "replacement binding invalidates old attachment")
        state.update(binding: first, pixelWidth: 1, pixelHeight: 1, attached: true)
        check(state.snapshot() == nil, "old view cannot publish after replacement")
        state.update(binding: replacement, pixelWidth: 640, pixelHeight: 360, attached: true)
        let replaced = state.snapshot()!
        state.detach(binding: first)
        state.endBinding(first)
        check(state.snapshot() == replaced, "late old-view detach/dismantle cannot revoke new view")
        state.update(binding: replacement, pixelWidth: 0, pixelHeight: 360, attached: true)
        check(state.snapshot() == nil && !state.validate(epoch: replaced.epoch), "zero-size layout invalidates live epoch")
        state.update(binding: replacement, pixelWidth: 640, pixelHeight: 360, attached: true)
        let final = state.snapshot()!
        state.endBinding(replacement)
        state.endBinding(replacement)
        state.update(binding: replacement, pixelWidth: 640, pixelHeight: 360, attached: true)
        check(state.snapshot() == nil && !state.validate(epoch: final.epoch), "dismantle is idempotent and prevents resurrection")

        let other = SwitchRuntimeSurfaceState()
        let otherBinding = other.beginBinding()!
        other.update(binding: otherBinding, pixelWidth: 1, pixelHeight: 1, attached: true)
        check(other.snapshot()!.epoch > final.epoch, "different state owners share monotonic epoch namespace")
        check(!state.validate(epoch: other.snapshot()!.epoch), "epoch does not attach a different owner")
        other.endBinding(otherBinding)

        let exhausted = SwitchRuntimeSurfaceState(testLastEpoch: UInt64.max - 2)
        let lastBinding = exhausted.beginBinding()!
        exhausted.update(binding: lastBinding, pixelWidth: 1, pixelHeight: 1, attached: true)
        check(exhausted.snapshot()?.epoch == UInt64.max, "last nonzero epoch can be issued")
        exhausted.update(binding: lastBinding, pixelWidth: 2, pixelHeight: 2, attached: true)
        check(exhausted.snapshot()?.epoch == UInt64.max, "resize needs no new epoch at exhaustion")
        exhausted.detach(binding: lastBinding)
        exhausted.update(binding: lastBinding, pixelWidth: 1, pixelHeight: 1, attached: true)
        check(exhausted.snapshot() == nil, "exhaustion fails detached without wrapping")
        check(exhausted.beginBinding() == nil, "exhausted binding cannot be reused")

        let concurrent = SwitchRuntimeSurfaceState()
        let token = concurrent.beginBinding()!
        let results = ReadResults()
        let started = DispatchSemaphore(value: 0)
        let start = DispatchSemaphore(value: 0)
        let group = DispatchGroup()
        for _ in 0..<4 {
            group.enter()
            DispatchQueue.global().async {
                started.signal(); start.wait()
                for _ in 0..<10_000 {
                    let value = concurrent.snapshot()
                    results.record(value)
                    if let value { _ = concurrent.validate(epoch: value.epoch) }
                }
                group.leave()
            }
        }
        for _ in 0..<4 { started.wait() }
        for _ in 0..<4 { start.signal() }
        var last: UInt64 = 0
        for i in 0..<2_000 {
            concurrent.update(binding: token, pixelWidth: UInt32(i + 1), pixelHeight: 720, attached: true)
            let current = concurrent.snapshot()!
            check(current.epoch > last, "concurrent lifecycle fresh epochs strictly advance")
            last = current.epoch
            concurrent.detach(binding: token)
            check(!concurrent.validate(epoch: last), "detached epoch never validates")
        }
        group.wait()
        let counts = results.result()
        check(counts.0 == 40_000 && counts.1 == 0, "concurrent snapshots are whole valid values or nil")
        concurrent.endBinding(token)
        check(concurrent.snapshot() == nil, "reader stress retains no attachment")
        print("Runtime surface state: \(checks) assertions and \(counts.0) concurrent worker snapshots; no UIKit/runtime/frame execution.")
    }
}
