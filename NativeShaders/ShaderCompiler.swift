import Foundation
import Metal
import Darwin

public enum SatchelShaderCompilationError: Error { case missingVariant, invalidSource, failed(Int) }

public struct SatchelCompiledShader {
    public let library: MTLLibrary
    public let sourceSHA256: String
    public let deviceIdentity: String
    public let liveCacheHit: Bool
    public let seconds: Double
    public let layerOutput: Bool
}

@available(iOS 18.0, macOS 15.0, *)
public final class SatchelShaderCompiler {
    public let device: MTLDevice
    public let deviceIdentity: String
    private let condition = NSCondition()
    private var activeCompilations = 0
    private var maximumConcurrentCompilations = 2
    private let maximumLiveLibraries: Int
    private var inFlight: Set<String> = []
    private var libraries: [String: (library: MTLLibrary, used: UInt64)] = [:]
    private var clock: UInt64 = 0

    public init(device: MTLDevice, maximumLiveLibraries: Int = 128) {
        self.device = device
        self.maximumLiveLibraries = max(1, min(maximumLiveLibraries, 512))
        var size = 0
        sysctlbyname("kern.osversion", nil, &size, nil, 0)
        var buffer = [CChar](repeating: 0, count: max(1, size))
        let status = sysctlbyname("kern.osversion", &buffer, &size, nil, 0)
        let os = status == 0 ? String(cString: buffer) : ProcessInfo.processInfo.operatingSystemVersionString
        self.deviceIdentity = "\(device.registryID)/\(device.name)/\(os)/msl32-relaxed-invariant-executable-default-v1"
    }

    public func compile(_ shader: SatchelTranslatedShader, variant: String = "b",
                        cancelled: () -> Bool = { false }) throws -> SatchelCompiledShader {
        guard let source = shader.sources[variant] else { throw SatchelShaderCompilationError.missingVariant }
        return try compile(source: source, cancelled: cancelled)
    }

    public func compile(source: String, cancelled: () -> Bool = { false }) throws -> SatchelCompiledShader {
        let bytes = Data(source.utf8)
        guard !bytes.isEmpty, bytes.count <= 64 * 1024 * 1024 else { throw SatchelShaderCompilationError.invalidSource }
        let hash = SatchelShaderTranslator.sha256(bytes)
        let key = deviceIdentity + "/" + hash
        condition.lock()
        while inFlight.contains(key) {
            if cancelled() { condition.unlock(); throw SatchelShaderError.cancelled }
            _ = condition.wait(until: Date(timeIntervalSinceNow: 0.1))
        }
        if cancelled() { condition.unlock(); throw SatchelShaderError.cancelled }
        clock &+= 1
        if let cached = libraries[key] {
            libraries[key] = (cached.library, clock); condition.unlock()
            return SatchelCompiledShader(library: cached.library, sourceSHA256: hash, deviceIdentity: deviceIdentity,
                liveCacheHit: true, seconds: 0, layerOutput: source.contains("render_target_array_index"))
        }
        inFlight.insert(key); condition.unlock()
        defer { condition.lock(); inFlight.remove(key); condition.broadcast(); condition.unlock() }
        let waiting = SatchelShaderMetrics.now()
        condition.lock()
        while activeCompilations >= maximumConcurrentCompilations {
            if cancelled() { condition.unlock(); throw SatchelShaderError.cancelled }
            _ = condition.wait(until: Date(timeIntervalSinceNow: 0.1))
        }
        activeCompilations += 1; condition.unlock()
        SatchelShaderMetrics.event("library-lane-wait", start: waiting)
        defer { condition.lock(); activeCompilations -= 1; condition.broadcast(); condition.unlock() }
        if cancelled() { throw SatchelShaderError.cancelled }
        let started = ProcessInfo.processInfo.systemUptime
        let library: MTLLibrary = try autoreleasepool {
            let options = MTLCompileOptions()
            options.languageVersion = .version3_2
            options.mathMode = .relaxed
            options.preserveInvariance = true
            options.optimizationLevel = .default
            options.libraryType = .executable
            do { return try SatchelShaderMetrics.measure("library-create") { try device.makeLibrary(source: source, options: options) } }
            catch { throw SatchelShaderCompilationError.failed((error as NSError).code) }
        }
        let seconds = ProcessInfo.processInfo.systemUptime - started
        condition.lock(); clock &+= 1
        libraries[key] = (library, clock)
        while libraries.count > maximumLiveLibraries,
              let oldest = libraries.min(by: { $0.value.used < $1.value.used })?.key {
            libraries.removeValue(forKey: oldest)
        }
        condition.unlock()
        if cancelled() { throw SatchelShaderError.cancelled }
        return SatchelCompiledShader(library: library, sourceSHA256: hash, deviceIdentity: deviceIdentity,
            liveCacheHit: false, seconds: seconds, layerOutput: source.contains("render_target_array_index"))
    }

    // Only optional warm-up raises this bound. Restore two lanes before gameplay.
    public func setMaximumConcurrentCompilations(_ count: Int) {
        condition.lock(); maximumConcurrentCompilations = max(1, min(count, 4))
        condition.broadcast(); condition.unlock()
    }

    public func trimLiveLibraries() {
        condition.lock(); libraries.removeAll(); condition.unlock()
    }
}

// Opt-in payload-free timing. One append per event keeps independent native and
// Swift producers from interleaving records. Times use CLOCK_MONOTONIC in ns.
public enum SatchelShaderMetrics {
    public static func now() -> UInt64 {
        var t = timespec(); clock_gettime(CLOCK_MONOTONIC, &t)
        return UInt64(t.tv_sec) * 1_000_000_000 + UInt64(t.tv_nsec)
    }
    public static func event(_ kind: String, start: UInt64) {
        guard let path = getenv("DM_SHADER_EVENTS") else { return }
        let end = now()
        var tid: UInt64 = 0; pthread_threadid_np(nil, &tid)
        let row = "\(start)\t\(end - start)\t\(tid)\t\(kind)\n"
        let fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0o600)
        guard fd >= 0 else { return }
        row.withCString { p in _ = write(fd, p, strlen(p)) }; close(fd)
    }
    public static func measure<T>(_ kind: String, _ body: () throws -> T) rethrows -> T {
        guard getenv("DM_SHADER_EVENTS") != nil else { return try body() }
        let start = now(); defer { event(kind, start: start) }
        return try body()
    }
}
