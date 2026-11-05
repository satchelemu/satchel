import Foundation

public enum SatchelShaderCacheError: Error { case invalidIdentity, corruptEntry, insufficientSpace, entryTooLarge }

public struct SatchelShaderPreparation {
    public let shader: SatchelTranslatedShader
    public let sourceCacheHit: Bool
    public let lookupSeconds: Double
    public let translationSeconds: Double
    public let persistenceSeconds: Double
}

// The directory must be private app storage. The cache never owns imported games.
public final class SatchelShaderStore {
    // One valid Odyssey record has three individually bounded variants totaling
    // 86.9 MB of encoded payload. Keep a bounded envelope that can hold them.
    private static let maximumPayloadBytes = 128 * 1024 * 1024
    private static let maximumEntryBytes = 192 * 1024 * 1024
    private let directory: URL
    private let lock = NSLock()
    private let pending = NSCondition()
    private var inFlight: Set<String> = []
    private let maximumBytes: Int
    private let reserveBytes: Int
    private var indexed = false
    private var entries: [URL: (bytes: Int, touched: Date)] = [:]
    private var totalBytes = 0
    private struct Envelope: Codable {
        let version: Int
        let implementation: String
        let request: String
        let payloadSHA256: String
        let payload: Data
    }

    public init(directory: URL, deviceIdentity: String, maximumBytes: Int = 2 * 1024 * 1024 * 1024,
                reserveBytes: Int = 512 * 1024 * 1024) throws {
        guard maximumBytes >= 64 * 1024 * 1024, reserveBytes >= 0, !deviceIdentity.isEmpty else {
            throw SatchelShaderCacheError.invalidIdentity
        }
        let partition = SatchelShaderTranslator.sha256(Data((SatchelShaderTranslator.implementationID + "/" + deviceIdentity).utf8))
        self.directory = directory.appendingPathComponent(partition, isDirectory: true)
        self.maximumBytes = maximumBytes; self.reserveBytes = reserveBytes
        try FileManager.default.createDirectory(at: self.directory, withIntermediateDirectories: true)
    }

    private func file(_ request: String) throws -> URL {
        guard request.count == 64, request.allSatisfy({ "0123456789abcdef".contains($0) }) else {
            throw SatchelShaderCacheError.invalidIdentity
        }
        return directory.appendingPathComponent(request + ".json")
    }

    public func load(requestSHA256: String) throws -> SatchelTranslatedShader? {
        let waiting = SatchelShaderMetrics.now()
        lock.lock(); SatchelShaderMetrics.event("store-lock", start: waiting)
        defer { lock.unlock() }
        let url = try file(requestSHA256)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        do {
            let info = try url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
            guard info.isRegularFile == true, info.isSymbolicLink != true,
                  (info.fileSize ?? Int.max) <= Self.maximumEntryBytes else { throw SatchelShaderCacheError.corruptEntry }
            let envelope = try JSONDecoder().decode(Envelope.self, from: Data(contentsOf: url))
            guard envelope.version == 1, envelope.implementation == SatchelShaderTranslator.implementationID,
                  envelope.payload.count <= Self.maximumPayloadBytes,
                  envelope.request == requestSHA256,
                  SatchelShaderTranslator.sha256(envelope.payload) == envelope.payloadSHA256 else {
                throw SatchelShaderCacheError.corruptEntry
            }
            let shader = try JSONDecoder().decode(SatchelTranslatedShader.self, from: envelope.payload)
            guard shader.requestSHA256 == requestSHA256, shader.recordSHA256.count == 64,
                  shader.key.count == 16, shader.key.allSatisfy({ "0123456789abcdef".contains($0) }),
                  shader.recordSHA256.allSatisfy({ "0123456789abcdef".contains($0) }),
                  shader.sources["b"] != nil, Set(shader.sources.keys).isSubset(of: SatchelTranslatedShader.allowedVariants),
                  shader.constantBufferExtents.allSatisfy({ (0..<16).contains($0.key) && $0.value >= 0 && $0.value <= 1024 * 1024 * 1024 }),
                  shader.unwrittenStorageBindings.allSatisfy({ (0..<32).contains($0) }),
                  (try JSONSerialization.jsonObject(with: shader.reflectionJSON)) is [String: Any] else {
                throw SatchelShaderCacheError.corruptEntry
            }
            if let variants = shader.variantReflectionJSON {
                guard Set(variants.keys) == Set(shader.sources.keys), variants["b"] == shader.reflectionJSON else {
                    throw SatchelShaderCacheError.corruptEntry
                }
                for value in variants.values {
                    guard (try JSONSerialization.jsonObject(with: value)) is [String: Any] else { throw SatchelShaderCacheError.corruptEntry }
                }
            } else if shader.sources.keys.contains(where: { $0.hasPrefix("bg") }) {
                throw SatchelShaderCacheError.corruptEntry
            }
            try FileManager.default.setAttributes([.modificationDate: Date()], ofItemAtPath: url.path)
            if let entry = entries[url] { entries[url] = (entry.bytes, Date()) }
            return shader
        } catch {
            // A damaged cache can always be rebuilt from the authenticated record.
            try? FileManager.default.removeItem(at: url)
            if let entry = entries.removeValue(forKey: url) { totalBytes -= entry.bytes }
            throw SatchelShaderCacheError.corruptEntry
        }
    }

    public func save(_ shader: SatchelTranslatedShader) throws {
        let waiting = SatchelShaderMetrics.now()
        lock.lock(); SatchelShaderMetrics.event("store-lock", start: waiting)
        defer { lock.unlock() }
        let url = try file(shader.requestSHA256)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let payload = try encoder.encode(shader)
        guard payload.count <= Self.maximumPayloadBytes else { throw SatchelShaderCacheError.entryTooLarge }
        let envelope = Envelope(version: 1, implementation: SatchelShaderTranslator.implementationID,
            request: shader.requestSHA256, payloadSHA256: SatchelShaderTranslator.sha256(payload), payload: payload)
        let data = try encoder.encode(envelope)
        let fm = FileManager.default
        let capacity = (try fm.attributesOfFileSystem(forPath: directory.path)[.systemFreeSize] as? NSNumber)?.int64Value ?? 0
        guard capacity >= Int64(reserveBytes) + Int64(data.count) * 2 else { throw SatchelShaderCacheError.insufficientSpace }
        if !indexed {
            let files = try fm.contentsOfDirectory(at: directory,
                includingPropertiesForKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
            for entry in files where entry.pathExtension == "json" {
                let info = try entry.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
                guard info.isRegularFile == true, info.isSymbolicLink != true else { continue }
                let bytes = info.fileSize ?? 0
                entries[entry] = (bytes, info.contentModificationDate ?? .distantPast); totalBytes += bytes
            }
            indexed = true
        }
        let replacedBytes = entries[url]?.bytes ?? 0
        if totalBytes - replacedBytes + data.count > maximumBytes {
            for entry in entries.sorted(by: { $0.value.touched < $1.value.touched })
                where entry.key != url && totalBytes - replacedBytes + data.count > maximumBytes {
                try fm.removeItem(at: entry.key)
                entries.removeValue(forKey: entry.key); totalBytes -= entry.value.bytes
            }
        }
        guard totalBytes - replacedBytes + data.count <= maximumBytes else { throw SatchelShaderCacheError.insufficientSpace }
        try data.write(to: url, options: .atomic)
        totalBytes += data.count - replacedBytes; entries[url] = (data.count, Date())
    }

    public func prepare(_ request: SatchelShaderRequest, cancelled: () -> Bool = { false }) throws -> SatchelTranslatedShader {
        try prepareMeasured(request, cancelled: cancelled).shader
    }

    public func prepareMeasured(_ request: SatchelShaderRequest, cancelled: () -> Bool = { false }) throws -> SatchelShaderPreparation {
        let started = ProcessInfo.processInfo.systemUptime
        if cancelled() { throw SatchelShaderError.cancelled }
        let normalized = try SatchelShaderTranslator.normalized(request)
        let identity = try SatchelShaderTranslator.identity(request, normalized: normalized)
        pending.lock()
        while inFlight.contains(identity) {
            if cancelled() { pending.unlock(); throw SatchelShaderError.cancelled }
            _ = pending.wait(until: Date(timeIntervalSinceNow: 0.1))
        }
        inFlight.insert(identity); pending.unlock()
        defer { pending.lock(); inFlight.remove(identity); pending.broadcast(); pending.unlock() }
        do {
            if let cached = try SatchelShaderMetrics.measure("source-load", { try load(requestSHA256: identity) }) {
                if cancelled() { throw SatchelShaderError.cancelled }
                return SatchelShaderPreparation(shader: cached, sourceCacheHit: true,
                    lookupSeconds: ProcessInfo.processInfo.systemUptime - started, translationSeconds: 0, persistenceSeconds: 0)
            }
        } catch SatchelShaderCacheError.corruptEntry { }
        let lookupEnd = ProcessInfo.processInfo.systemUptime
        let shader = try SatchelShaderMetrics.measure("translation") { try SatchelShaderTranslator.translate(request, cancelled: cancelled) }
        if cancelled() { throw SatchelShaderError.cancelled }
        let translationEnd = ProcessInfo.processInfo.systemUptime
        try SatchelShaderMetrics.measure("source-save") { try save(shader) }
        return SatchelShaderPreparation(shader: shader, sourceCacheHit: false, lookupSeconds: lookupEnd - started,
            translationSeconds: translationEnd - lookupEnd, persistenceSeconds: ProcessInfo.processInfo.systemUptime - translationEnd)
    }
}
