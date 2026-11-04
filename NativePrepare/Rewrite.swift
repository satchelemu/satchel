import Foundation

struct PrepareFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

final class PrepareCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    private var finalizing = false
    @discardableResult func cancel() -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard !finalizing else { return false }
        value = true; return true
    }
    /// Cancellation and the final cleanup commit have one ordering, including task expiration.
    func beginFinalizing() throws {
        lock.lock(); defer { lock.unlock() }
        if value { throw CancellationError() }
        finalizing = true
    }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func check() throws { if cancelled { throw CancellationError() } }
}

private final class RewriteProgressContext {
    let token: PrepareCancellation
    let progress: (Double, UInt64, UInt64) -> Void
    init(_ token: PrepareCancellation, _ progress: @escaping (Double, UInt64, UInt64) -> Void) {
        self.token = token; self.progress = progress
    }
}

enum PrepareRewrite {
    static func write(_ module: SwitchDecodedModule, to directory: URL, token: PrepareCancellation, progress: @escaping (Double, UInt64, UInt64) -> Void = { _, _, _ in }) throws {
        var request = nr_request()
        request.abi_version = NR_ABI_VERSION
        request.bss_size = UInt64(module.bssSize)
        request.max_image_bytes = 512 * 1024 * 1024
        request.max_patch_bytes = 64 * 1024 * 1024
        request.max_function_bytes = 4 * 1024 * 1024
        request.max_records = 2_000_000
        let context = RewriteProgressContext(token, progress)
        request.context = Unmanaged.passUnretained(context).toOpaque()
        request.cancelled = { pointer in
            guard let pointer else { return 1 }
            return Unmanaged<RewriteProgressContext>.fromOpaque(pointer).takeUnretainedValue().token.cancelled ? 1 : 0
        }
        withUnsafeMutableBytes(of: &request.build_id) { $0.copyBytes(from: module.buildID) }
        var result: OpaquePointer?
        var error = nr_error()
        let status = module.name.withCString { name in
            module.buildID.withUnsafeBytes { build in
                satchel_registry_profile(build.bindMemory(to: UInt8.self).baseAddress, name, &request.profile)
            }
            request.module_name = name
            return module.segments[0].bytes.withUnsafeBytes { a in
                module.segments[1].bytes.withUnsafeBytes { b in
                    module.segments[2].bytes.withUnsafeBytes { c in
                        request.segments = (nr_segment(bytes: a.bindMemory(to: UInt8.self).baseAddress, size: a.count, offset: module.segments[0].offset),
                            nr_segment(bytes: b.bindMemory(to: UInt8.self).baseAddress, size: b.count, offset: module.segments[1].offset),
                            nr_segment(bytes: c.bindMemory(to: UInt8.self).baseAddress, size: c.count, offset: module.segments[2].offset))
                        return nr_rewrite_progress(&request, &result, &error) { pointer, phase, done, total in
                            guard let pointer else { return }
                            let context = Unmanaged<RewriteProgressContext>.fromOpaque(pointer).takeUnretainedValue()
                            let fraction = (Double(phase) + (total == 0 ? 1 : Double(done) / Double(total))) / 8
                            context.progress(min(0.98, fraction * 0.98), done, total)
                        }
                    }
                }
            }
        }
        withExtendedLifetime(context) {}
        if status == NR_CANCELLED { throw CancellationError() }
        guard status == NR_OK, let result else {
            let detail = withUnsafeBytes(of: error.message) { bytes in
                String(decoding: bytes.prefix(while: { $0 != 0 }), as: UTF8.self)
            }
            throw PrepareFailure(message: "Native rewrite failed for \(module.name), status \(status.rawValue), offset \(error.offset): \(detail)")
        }
        defer { nr_result_free(result) }
        let artifacts: [(nr_artifact, String)] = [(NR_IMAGE, "patched.bin"), (NR_FIXUPS, "fixups.tsv"), (NR_GATES, "gates.tsv")]
        for (kind, suffix) in artifacts {
            try token.check()
            let bytes = nr_result_get(result, kind)
            let data: Data
            if bytes.size == 0 { data = Data() }
            else {
                guard let pointer = bytes.data else { throw PrepareFailure(message: "Native rewriter returned a missing artifact buffer.") }
                data = Data(bytes: pointer, count: bytes.size)
            }
            try data.write(to: directory.appendingPathComponent(module.name + "." + suffix))
        }
        progress(1, UInt64(nr_result_get(result, NR_IMAGE).size), UInt64(nr_result_get(result, NR_IMAGE).size))
    }
}
