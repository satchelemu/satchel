import Foundation
import CoreGraphics

@main struct RuntimeFrameTests {
    @MainActor static var checks = 0
    @MainActor static func check(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        if !value() { fatalError(message) }
    }
    static func info(_ epoch: UInt64, width: UInt32 = 2, height: UInt32 = 1) -> SatchelFrameInfo {
        var value = SatchelFrameInfo()
        value.version = 1; value.size = UInt32(MemoryLayout<SatchelFrameInfo>.size)
        value.state = UInt32(SATCHEL_FRAME_LEASED.rawValue)
        value.width = width; value.height = height; value.row_stride = width * 4
        value.format = UInt32(SATCHEL_FRAME_RGBA8_UNORM)
        value.color_space = UInt32(SATCHEL_FRAME_LINEAR_SRGB); value.origin = UInt32(SATCHEL_FRAME_TOP_LEFT)
        value.epoch = epoch; value.token = 1; value.byte_size = UInt64(value.row_stride) * UInt64(height)
        return value
    }
    static func make(_ surface: SwitchRuntimeSurfaceState, _ epoch: UInt64, _ sequence: UInt64) -> SwitchRuntimeFrame? {
        SwitchRuntimeFrame.copy(info: info(epoch), sequence: sequence, surface: surface) { pointer, count in
            memset(pointer, 255, count)
            return true
        }
    }
    @MainActor static func pumpMain() async {
        await withCheckedContinuation { continuation in DispatchQueue.main.async { continuation.resume() } }
    }
    @MainActor static func copyAndRender(_ surface: SwitchRuntimeSurfaceState, _ epoch: UInt64) {
        autoreleasepool {
            var source: [UInt8] = [255, 0, 0, 0, 0, 255, 0, 13]
            var copied = 0
            let frame = SwitchRuntimeFrame.copy(info: info(epoch), sequence: 1, surface: surface) { pointer, count in
                copied += 1
                source.withUnsafeBytes { pointer.copyMemory(from: $0.baseAddress!, byteCount: count) }
                return true
            }!
            check(copied == 1 && SwitchRuntimeFrame.retainedBackingBytes == 8, "one charged immutable backing")
            source = [UInt8](repeating: 0, count: 8)
            check(frame.epoch == epoch && frame.sequence == 1, "frame retains explicit epoch/order")
            check(frame.image.width == 2 && frame.image.height == 1 && frame.image.bytesPerRow == 8, "image uses actual dimensions")
            check(frame.image.colorSpace?.name == CGColorSpace.linearSRGB, "linear color profile is explicit")
            var output = [UInt8](repeating: 0, count: 8)
            output.withUnsafeMutableBytes { bytes in
                let bitmap = CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue))
                let context = CGContext(data: bytes.baseAddress, width: 2, height: 1, bitsPerComponent: 8,
                    bytesPerRow: 8, space: CGColorSpace(name: CGColorSpace.linearSRGB)!, bitmapInfo: bitmap.rawValue)!
                context.interpolationQuality = .none
                context.draw(frame.image, in: CGRect(x: 0, y: 0, width: 2, height: 1))
            }
            check(output == [255, 0, 0, 255, 0, 255, 0, 255], "real CoreGraphics draw preserves RGB/channel order and explicit opaque policy after source changed")
        }
        check(SwitchRuntimeFrame.retainedBackingBytes == 0, "image/provider destruction releases copied backing budget")
    }
    @MainActor static func refusalAndBudget(_ surface: SwitchRuntimeSurfaceState, _ epoch: UInt64) {
        autoreleasepool {
            var cases: [SatchelFrameInfo] = []
            for field in 0..<13 {
                var v = info(epoch)
                switch field {
                case 0: v.version = 0
                case 1: v.size = 0
                case 2: v.state = UInt32(SATCHEL_FRAME_READY.rawValue)
                case 3: v.token = 0
                case 4: v.epoch = 0
                case 5: v.format = 99
                case 6: v.color_space = 99
                case 7: v.origin = 99
                case 8: v.width = 0
                case 9: v.height = 0
                case 10: v.row_stride = 7; v.byte_size = 7
                case 11: v.byte_size += 1
                default: v.width = UInt32.max; v.row_stride = UInt32.max; v.height = UInt32.max; v.byte_size = UInt64.max
                }
                cases.append(v)
            }
            for value in cases {
                var called = false
                let frame = SwitchRuntimeFrame.copy(info: value, sequence: 1, surface: surface) { _, _ in called = true; return true }
                check(frame == nil && !called, "invalid metadata refuses before allocation/copy")
            }
            check(SwitchRuntimeFrame.copy(info: info(epoch), sequence: 0, surface: surface) { _, _ in fatalError("zero sequence copied") } == nil, "zero sequence refused")
            check(SwitchRuntimeFrame.copy(info: info(epoch), sequence: 1, surface: surface) { _, _ in false } == nil, "failed lease copy yields no frame")
            check(SwitchRuntimeFrame.retainedBackingBytes == 0, "refusal releases backing")
            var frames: [SwitchRuntimeFrame] = []
            let large = info(epoch, width: 2048, height: 1024)
            for sequence in 1...3 {
                let frame = SwitchRuntimeFrame.copy(info: large, sequence: UInt64(sequence), surface: surface) { p, n in memset(p, 0, n); return true }
                check(frame != nil, "bounded large frame admitted")
                frames.append(frame!)
            }
            check(SwitchRuntimeFrame.retainedBackingBytes == 24 * 1024 * 1024, "global copied-backing budget exact")
            var called = false
            check(SwitchRuntimeFrame.copy(info: large, sequence: 4, surface: surface) { _, _ in called = true; return true } == nil && !called, "budget exhausted before copy")
            withExtendedLifetime(frames) {}
        }
        check(SwitchRuntimeFrame.retainedBackingBytes == 0, "all retained backing released after budget fixture")
    }
    @MainActor static func paddedRows(_ surface: SwitchRuntimeSurfaceState, _ epoch: UInt64) {
        autoreleasepool {
            var metadata = info(epoch, width: 2, height: 2)
            metadata.row_stride = 12; metadata.byte_size = 24
            let source: [UInt8] = [255,0,0,0, 0,255,0,0, 91,92,93,94,
                                   0,0,255,0, 255,255,255,0, 95,96,97,98]
            let frame = SwitchRuntimeFrame.copy(info: metadata, sequence: 1, surface: surface) { p, n in
                source.withUnsafeBytes { p.copyMemory(from: $0.baseAddress!, byteCount: n) }
                return true
            }!
            var pixels = [UInt8](repeating: 0, count: 16)
            pixels.withUnsafeMutableBytes { bytes in
                let bitmap = CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue))
                let context = CGContext(data: bytes.baseAddress, width: 2, height: 2, bitsPerComponent: 8,
                    bytesPerRow: 8, space: CGColorSpace(name: CGColorSpace.linearSRGB)!, bitmapInfo: bitmap.rawValue)!
                context.interpolationQuality = .none
                context.draw(frame.image, in: CGRect(x: 0, y: 0, width: 2, height: 2))
            }
            check(pixels == [255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255],
                  "CoreGraphics draw preserves row ordering and skips explicit row padding")
        }
        check(SwitchRuntimeFrame.retainedBackingBytes == 0, "padded image releases backing")
    }
    @MainActor static func main() async {
        let surface = SwitchRuntimeSurfaceState()
        let binding = surface.beginBinding()!
        surface.update(binding: binding, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let epoch = surface.snapshot()!.epoch
        copyAndRender(surface, epoch)
        paddedRows(surface, epoch)
        refusalAndBudget(surface, epoch)
        check(SwitchRuntimeFrame.copy(info: info(epoch), sequence: 1, surface: surface) { _, _ in
            surface.detach(binding: binding)
            return true
        } == nil, "detach during copy prevents image publication")
        check(SwitchRuntimeFrame.retainedBackingBytes == 0, "detached copy releases budget")
        surface.update(binding: binding, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let nextEpoch = surface.snapshot()!.epoch
        let presenter = SwitchRuntimeFramePresenter(surface: surface)
        presenter.offer(make(surface, nextEpoch, 3)!)
        presenter.offer(make(surface, nextEpoch, 1)!)
        presenter.offer(make(surface, nextEpoch, 2)!)
        await pumpMain()
        check(presenter.frame?.sequence == 3, "out-of-order offers cannot replace a newer frame")
        presenter.offer(make(surface, nextEpoch, 4)!)
        surface.detach(binding: binding); presenter.detach()
        await pumpMain()
        check(presenter.frame == nil, "queued old-epoch frame cannot resurrect after detach")
        surface.update(binding: binding, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let finalEpoch = surface.snapshot()!.epoch
        DispatchQueue.concurrentPerform(iterations: 64) { index in
            if let frame = make(surface, finalEpoch, UInt64(index + 1)) { presenter.offer(frame) }
        }
        await pumpMain()
        check(presenter.frame?.epoch == finalEpoch && presenter.frame?.sequence == 64, "concurrent producer offers preserve newest sequence in new epoch")
        check(SwitchRuntimeFrame.retainedBackingBytes == 8, "one displayed copied buffer after mailbox drain")
        surface.detach(binding: binding)
        presenter.surfaceInvalidated()
        check(presenter.frame != nil, "representable invalidation defers observable publication")
        await pumpMain()
        check(presenter.frame == nil, "deferred invalidation clears old epoch image")
        surface.update(binding: binding, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let refreshedEpoch = surface.snapshot()!.epoch
        presenter.offer(make(surface, refreshedEpoch, 1)!)
        await pumpMain()
        surface.detach(binding: binding)
        surface.update(binding: binding, pixelWidth: 1280, pixelHeight: 720, attached: true)
        let newestEpoch = surface.snapshot()!.epoch
        presenter.offer(make(surface, newestEpoch, 1)!)
        presenter.surfaceInvalidated()
        // Invalidation clears the pending mailbox, so a fresh producer supplies
        // its next frame. The already scheduled drain runs before publication.
        presenter.offer(make(surface, newestEpoch, 2)!)
        await pumpMain()
        check(presenter.frame?.epoch == newestEpoch && presenter.frame?.sequence == 2,
              "deferred invalidation cannot erase a newer live image")
        surface.endBinding(binding); presenter.detach()
        await pumpMain()
        check(presenter.frame == nil && SwitchRuntimeFrame.retainedBackingBytes == 0, "end binding releases pending and visible buffers")
        print("Runtime frame copy/presenter: \(checks) original assertions, actual CoreGraphics draw and64 concurrent offers; no GPU/game/UIKit presentation claim.")
    }
}
