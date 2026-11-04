import Foundation
import CoreGraphics
import Combine

// This budget covers our copied RGBA backing only, not compositor/driver memory.
private final class RuntimeFrameBudget: @unchecked Sendable {
    static let shared = RuntimeFrameBudget()
    private let lock = NSLock()
    private var bytes = 0
    static let frameLimit = 8 * 1024 * 1024
    static let totalLimit = 24 * 1024 * 1024
    func reserve(_ count: Int) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard count > 0, count <= Self.frameLimit, count <= Self.totalLimit - bytes else { return false }
        bytes += count
        return true
    }
    func release(_ count: Int) {
        lock.lock(); defer { lock.unlock() }
        precondition(count > 0 && count <= bytes)
        bytes -= count
    }
    func retainedBytes() -> Int {
        lock.lock(); defer { lock.unlock() }
        return bytes
    }
}

private final class RuntimeFrameBacking {
    let pointer: UnsafeMutableRawPointer
    let count: Int
    init?(_ count: Int) {
        guard RuntimeFrameBudget.shared.reserve(count) else { return nil }
        guard let pointer = malloc(count) else {
            RuntimeFrameBudget.shared.release(count)
            return nil
        }
        self.pointer = pointer
        self.count = count
    }
    deinit {
        free(pointer)
        RuntimeFrameBudget.shared.release(count)
    }
}

// Immutable owned image. There is no C frame/Window/CPU pointer in this value.
final class SwitchRuntimeFrame: @unchecked Sendable {
    let image: CGImage
    let epoch: UInt64
    let sequence: UInt64
    private init(image: CGImage, epoch: UInt64, sequence: UInt64) {
        self.image = image; self.epoch = epoch; self.sequence = sequence
    }

    // Caller borrows the real C lease only during copyBytes, then releases it on
    // the graphics worker. The pure surface checks are instantaneous, not leases.
    // Opaque canvas is an explicit host display policy: RGBA alpha is ignored.
    static func copy(info: SatchelFrameInfo, sequence: UInt64, surface: SwitchRuntimeSurfaceState,
                     copyBytes: (UnsafeMutableRawPointer, Int) -> Bool) -> SwitchRuntimeFrame? {
        guard info.version == 1, info.size == MemoryLayout<SatchelFrameInfo>.size,
              info.state == UInt32(SATCHEL_FRAME_LEASED.rawValue), info.token != 0,
              sequence != 0, surface.validate(epoch: info.epoch),
              info.format == SATCHEL_FRAME_RGBA8_UNORM, info.color_space == SATCHEL_FRAME_LINEAR_SRGB,
              info.origin == SATCHEL_FRAME_TOP_LEFT, info.width > 0, info.height > 0,
              UInt64(info.row_stride) >= UInt64(info.width) * 4,
              info.byte_size == UInt64(info.row_stride) * UInt64(info.height),
              info.byte_size <= UInt64(RuntimeFrameBudget.frameLimit),
              let colorSpace = CGColorSpace(name: CGColorSpace.linearSRGB),
              let backing = RuntimeFrameBacking(Int(info.byte_size)) else { return nil }
        guard copyBytes(backing.pointer, backing.count), surface.validate(epoch: info.epoch) else { return nil }
        let retained = Unmanaged.passRetained(backing)
        guard let provider = CGDataProvider(dataInfo: retained.toOpaque(), data: backing.pointer,
                size: backing.count, releaseData: { context, _, _ in
                    if let context { Unmanaged<RuntimeFrameBacking>.fromOpaque(context).release() }
                }) else {
            retained.release()
            return nil
        }
        let bitmap = CGBitmapInfo.byteOrder32Big.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue))
        guard let image = CGImage(width: Int(info.width), height: Int(info.height), bitsPerComponent: 8,
                bitsPerPixel: 32, bytesPerRow: Int(info.row_stride), space: colorSpace,
                bitmapInfo: bitmap, provider: provider, decode: nil, shouldInterpolate: false,
                intent: .defaultIntent) else { return nil }
        return SwitchRuntimeFrame(image: image, epoch: info.epoch, sequence: sequence)
    }

    static var retainedBackingBytes: Int { RuntimeFrameBudget.shared.retainedBytes() }
}

// At most one pending image; a slow UI replaces pending frames with newer ones.
// Ordering is by surface epoch then accepted presentation sequence, not token.
private final class RuntimeFrameMailbox: @unchecked Sendable {
    private let lock = NSLock()
    private var pending: SwitchRuntimeFrame?
    private var scheduled = false
    private var epoch: UInt64 = 0
    private var sequence: UInt64 = 0
    func offer(_ frame: SwitchRuntimeFrame) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard frame.epoch > epoch || (frame.epoch == epoch && frame.sequence > sequence) else { return false }
        epoch = frame.epoch; sequence = frame.sequence; pending = frame
        guard !scheduled else { return false }
        scheduled = true
        return true
    }
    func take() -> SwitchRuntimeFrame? {
        lock.lock(); defer { lock.unlock() }
        let value = pending
        pending = nil; scheduled = false
        return value
    }
    func clear() {
        lock.lock(); defer { lock.unlock() }
        pending = nil
        // Keep the outstanding drain scheduled. It can consume a newer frame.
    }
}

@MainActor final class SwitchRuntimeFramePresenter: ObservableObject {
    @Published private(set) var frame: SwitchRuntimeFrame?
    private nonisolated let surface: SwitchRuntimeSurfaceState
    private nonisolated let mailbox = RuntimeFrameMailbox()
    private var invalidationScheduled = false
    init(surface: SwitchRuntimeSurfaceState) { self.surface = surface }
    nonisolated func offer(_ frame: SwitchRuntimeFrame) {
        guard surface.validate(epoch: frame.epoch), mailbox.offer(frame) else { return }
        DispatchQueue.main.async { [weak self] in self?.drain() }
    }
    private func drain() {
        guard let next = mailbox.take(), surface.validate(epoch: next.epoch) else { return }
        frame = next
    }
    func detach() {
        mailbox.clear()
        if frame != nil { frame = nil }
    }

    // UIViewRepresentable updates may synchronously invalidate the surface.
    // Clear its UIKit image and mailbox immediately; publish observable changes
    // after the update, rechecking the epoch so a newer frame is never erased.
    func surfaceInvalidated() {
        mailbox.clear()
        guard frame != nil, !invalidationScheduled else { return }
        invalidationScheduled = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.invalidationScheduled = false
            if let frame = self.frame, !self.surface.validate(epoch: frame.epoch) {
                self.frame = nil
            }
        }
    }
}
