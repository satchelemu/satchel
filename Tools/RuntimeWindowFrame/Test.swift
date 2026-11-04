import Foundation
import CoreGraphics

@main struct WindowFrameTests {
    @MainActor static var checks = 0
    @MainActor static func check(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        if !value() { fatalError("FAIL: \(message)") }
    }
    @MainActor static func pumpMain() async {
        await withCheckedContinuation { continuation in DispatchQueue.main.async { continuation.resume() } }
    }
    @MainActor static func surface() -> (SwitchRuntimeSurfaceState, UInt64, UInt64) {
        let state = SwitchRuntimeSurfaceState()
        guard let binding = state.beginBinding() else { fatalError("binding") }
        state.update(binding: binding, pixelWidth: 31, pixelHeight: 17, attached: true)
        guard let snapshot = state.snapshot() else { fatalError("snapshot") }
        return (state, binding, snapshot.epoch)
    }
    @MainActor static func begin(_ epoch: UInt64, _ mode: UInt32) -> SatchelWindowFrameFixtureReport {
        var report = SatchelWindowFrameFixtureReport()
        let status = RuntimeGraphicsWorker.perform { satchel_window_frame_fixture_begin(epoch, mode, &report) }
        check(status == UInt32(SATCHEL_WINDOW_FIXTURE_READY), "real original C fixture returns READY")
        check(report.ready == 1 && report.fixture_token != 0 && report.window_ticket != 0, "real owner tokens retained")
        check(report.epoch == epoch && report.width == 31 && report.height == 17 && report.row_stride == 124 && report.byte_size == 2108, "actual readback metadata")
        check(report.cpu_destroyed == mode, "requested actual Process/CPU lifetime mode")
        check(report.instructions > 0 && report.dispatch_events > 0, "original interpreter/GPU path executed")
        return report
    }
    @MainActor static func finish(_ token: UInt64) async {
        var final = SatchelWindowFrameFixtureReport()
        var status = UInt32(SATCHEL_WINDOW_FIXTURE_BUSY)
        for _ in 0..<1000 {
            status = RuntimeGraphicsWorker.perform {
                RuntimeGraphicsWorker.fixtureCollect()
                return satchel_window_frame_fixture_finish(token, &final)
            }
            if status == UInt32(SATCHEL_WINDOW_FIXTURE_READY) { break }
            check(status == UInt32(SATCHEL_WINDOW_FIXTURE_BUSY), "bounded CPU-free finish remains retryable")
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        check(status == UInt32(SATCHEL_WINDOW_FIXTURE_READY) && final.cleanup_matched == 1 && final.resources_retained == 0, "actual owner/GPU resources return to baseline")
    }
    @MainActor static func checkImage(_ presenter: SwitchRuntimeFramePresenter, epoch: UInt64) {
        autoreleasepool {
            guard let frame = presenter.frame else { fatalError("FAIL: presenter has no copied frame") }
            check(frame.epoch == epoch && frame.sequence != 0, "presenter receives actual epoch/token order")
            check(frame.image.width == 31 && frame.image.height == 17 && frame.image.bytesPerRow == 124, "image dimensions/stride derive actual frame")
            check(frame.image.colorSpace?.name == CGColorSpace.linearSRGB, "explicit real linear-sRGB image")
            var bytes = [UInt8](repeating: 0, count: 31 * 17 * 4)
            bytes.withUnsafeMutableBytes { memory in
                let bitmap = CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue))
                let context = CGContext(data: memory.baseAddress, width: 31, height: 17, bitsPerComponent: 8, bytesPerRow: 124,
                    space: CGColorSpace(name: CGColorSpace.linearSRGB)!, bitmapInfo: bitmap.rawValue)!
                context.interpolationQuality = .none
                context.draw(frame.image, in: CGRect(x: 0, y: 0, width: 31, height: 17))
            }
            for (index, byte) in bytes.enumerated() { check(byte == (index % 4 == 0 || index % 4 == 3 ? 255 : 0), "real rasterized red RGB survives C lease→Swift copy→CGImage draw") }
        }
    }
    @MainActor static func journey(cpuDestroyed: UInt32, releaseMode: UInt32) async throws {
        let (state, binding, epoch) = surface()
        let presenter = SwitchRuntimeFramePresenter(surface: state)
        check(SwitchRuntimeFrame.retainedBackingBytes == 0, "initial Swift backing budget zero")
        let report = begin(epoch, cpuDestroyed)
        RuntimeGraphicsWorker.perform { satchel_window_test_release_mode(releaseMode) }
        let count = try RuntimeGraphicsWorker.perform {
            try RuntimeGraphicsWorker.copyWindowFrames(ticket: report.window_ticket, epoch: epoch, surface: state, presenter: presenter, cancelled: { false })
        }
        check(count == 1, "exact production worker copies one READY frame")
        let pending = RuntimeGraphicsWorker.perform { RuntimeGraphicsWorker.pendingFrameLeases(window: report.window_ticket) }
        check(pending == (releaseMode == 0 ? 0 : 1), "failed release retained as numeric CPU-free entry")
        check(RuntimeGraphicsWorker.perform { satchel_window_test_release_injections() } == (releaseMode == 0 ? 0 : 1), "test-only release fault is explicit")
        await pumpMain()
        checkImage(presenter, epoch: epoch)
        check(SwitchRuntimeFrame.retainedBackingBytes == 2108, "copied image owns separately charged bytes")
        if releaseMode == 1 {
            var retained = SatchelWindowFrameFixtureReport()
            let status = RuntimeGraphicsWorker.perform { satchel_window_frame_fixture_finish(report.fixture_token, &retained) }
            check(status == UInt32(SATCHEL_WINDOW_FIXTURE_BUSY) && retained.resources_retained != 0, "real retained C lease prevents CPU-free retirement")
        }
        RuntimeGraphicsWorker.perform { RuntimeGraphicsWorker.fixtureCollect() }
        check(RuntimeGraphicsWorker.perform { RuntimeGraphicsWorker.pendingFrameLeases(window: report.window_ticket) } == 0, "actual retry or stale-token terminal result drains Swift entry")
        if releaseMode != 0 { check(RuntimeGraphicsWorker.perform { satchel_window_test_release_calls() } >= 2, "collector actually retries failed release") }
        state.detach(binding: binding);presenter.detach()
        await pumpMain()
        check(presenter.frame == nil && SwitchRuntimeFrame.retainedBackingBytes == 0, "detach releases copied image/provider budget")
        await finish(report.fixture_token)
    }
    @MainActor static func cancelledBeforeCopy() async throws {
        let (state, binding, epoch) = surface();let presenter = SwitchRuntimeFramePresenter(surface: state)
        let report = begin(epoch, 1)
        RuntimeGraphicsWorker.perform { satchel_window_test_release_mode(0) }
        let count = try RuntimeGraphicsWorker.perform {
            try RuntimeGraphicsWorker.copyWindowFrames(ticket: report.window_ticket, epoch: epoch, surface: state, presenter: presenter, cancelled: { true })
        }
        check(count == 0 && SwitchRuntimeFrame.retainedBackingBytes == 0, "pre-copy cancellation allocates no Swift image")
        check(RuntimeGraphicsWorker.perform { satchel_window_test_release_calls() } == 0, "no unowned lease release fabricated")
        await pumpMain();check(presenter.frame == nil, "cancelled READY frame is never offered")
        state.detach(binding: binding);presenter.detach();await finish(report.fixture_token)
    }
    @MainActor static func main() async throws {
        try await journey(cpuDestroyed: 0, releaseMode: 0)
        try await journey(cpuDestroyed: 1, releaseMode: 0)
        try await cancelledBeforeCopy()
        try await journey(cpuDestroyed: 1, releaseMode: 1)
        try await journey(cpuDestroyed: 1, releaseMode: 2)
        print("Window frame consumer: \(checks) original Swift/real-C GPU assertions; exact extracted worker, CPU-free leases and CoreGraphics pixels. Synthetic surface lifecycle, no UIKit/game/display claim.")
    }
}
