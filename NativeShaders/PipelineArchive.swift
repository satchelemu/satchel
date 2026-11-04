import Foundation
import Metal

public struct SatchelRenderPipelineResult {
    public let pipeline: MTLRenderPipelineState
    public let persistentHit: Bool
    public let persisted: Bool
}
public struct SatchelComputePipelineResult {
    public let pipeline: MTLComputePipelineState
    public let persistentHit: Bool
    public let persisted: Bool
}

// One shared instance per device partition. Metal checks the actual descriptor
// against the archive, so an incomplete caller identity causes a miss, not reuse
// of a pipeline with different state. Supply all pipeline state for useful hits.
public final class SatchelPipelineArchiveCache {
    private let device: MTLDevice
    private let directory: URL
    private let maximumBytes: Int
    private let reserveBytes: Int
    private let lock = NSLock()
    private var entries: [URL: (bytes: Int, touched: Date)] = [:]
    private var totalBytes = 0

    public init(device: MTLDevice, deviceIdentity: String, directory: URL,
                maximumBytes: Int = 1024 * 1024 * 1024, reserveBytes: Int = 512 * 1024 * 1024) throws {
        guard maximumBytes >= 64 * 1024 * 1024, reserveBytes >= 0 else { throw SatchelShaderCacheError.invalidIdentity }
        self.device = device; self.maximumBytes = maximumBytes; self.reserveBytes = reserveBytes
        self.directory = directory.appendingPathComponent(SatchelShaderTranslator.sha256(Data(deviceIdentity.utf8)), isDirectory: true)
        let fm = FileManager.default
        try fm.createDirectory(at: self.directory, withIntermediateDirectories: true)
        for file in try fm.contentsOfDirectory(at: self.directory, includingPropertiesForKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
            where file.pathExtension == "metalarc" {
            let info = try file.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
            guard info.isRegularFile == true, info.isSymbolicLink != true else { continue }
            let bytes = info.fileSize ?? 0
            entries[file] = (bytes, info.contentModificationDate ?? .distantPast); totalBytes += bytes
        }
    }

    private func archive(_ url: URL?) throws -> MTLBinaryArchive {
        let descriptor = MTLBinaryArchiveDescriptor(); descriptor.url = url
        return try device.makeBinaryArchive(descriptor: descriptor)
    }
    private func path(_ identity: Data, kind: String) -> URL {
        directory.appendingPathComponent(SatchelShaderTranslator.sha256(Data(kind.utf8) + identity) + ".metalarc")
    }
    private func touch(_ url: URL) {
        if let entry = entries[url] { entries[url] = (entry.bytes, Date()) }
        try? FileManager.default.setAttributes([.modificationDate: Date()], ofItemAtPath: url.path)
    }
    private func persist(_ archive: MTLBinaryArchive, at url: URL) -> Bool {
        let temporary = directory.appendingPathComponent(UUID().uuidString + ".pending")
        let fm = FileManager.default
        defer { try? fm.removeItem(at: temporary) }
        do {
            let free = (try fm.attributesOfFileSystem(forPath: directory.path)[.systemFreeSize] as? NSNumber)?.int64Value ?? 0
            guard free >= Int64(reserveBytes) + 64 * 1024 * 1024 else { return false }
            try archive.serialize(to: temporary)
            let size = try temporary.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max
            guard size <= 64 * 1024 * 1024 else { return false }
            let replaced = entries[url]?.bytes ?? 0
            if totalBytes - replaced + size > maximumBytes {
                for entry in entries.sorted(by: { $0.value.touched < $1.value.touched })
                    where entry.key != url && totalBytes - replaced + size > maximumBytes {
                    try fm.removeItem(at: entry.key); totalBytes -= entry.value.bytes; entries.removeValue(forKey: entry.key)
                }
            }
            guard totalBytes - replaced + size <= maximumBytes else { return false }
            if fm.fileExists(atPath: url.path) { _ = try fm.replaceItemAt(url, withItemAt: temporary) }
            else { try fm.moveItem(at: temporary, to: url) }
            totalBytes += size - replaced; entries[url] = (size, Date())
            return true
        } catch { return false }
    }

    public func render(_ descriptor: MTLRenderPipelineDescriptor, identity: Data,
                       cancelled: () -> Bool = { false }) throws -> SatchelRenderPipelineResult {
        lock.lock(); defer { lock.unlock() }
        if cancelled() { throw SatchelShaderError.cancelled }
        let url = path(identity, kind: "render"), working = descriptor.copy() as! MTLRenderPipelineDescriptor
        if let entry = entries[url], entry.bytes <= 64 * 1024 * 1024, let saved = try? archive(url) {
            working.binaryArchives = [saved]
            if let pipeline = try? device.makeRenderPipelineState(descriptor: working, options: .failOnBinaryArchiveMiss, reflection: nil) {
                touch(url)
                return SatchelRenderPipelineResult(pipeline: pipeline, persistentHit: true, persisted: true)
            }
        }
        do {
            let fresh = try archive(nil); working.binaryArchives = [fresh]
            try fresh.addRenderPipelineFunctions(descriptor: working)
            let pipeline = try device.makeRenderPipelineState(descriptor: working)
            if cancelled() { throw SatchelShaderError.cancelled }
            return SatchelRenderPipelineResult(pipeline: pipeline, persistentHit: false, persisted: persist(fresh, at: url))
        } catch let error as SatchelShaderError { throw error }
        catch { throw SatchelShaderCompilationError.failed((error as NSError).code) }
    }

    public func compute(_ descriptor: MTLComputePipelineDescriptor, identity: Data,
                        cancelled: () -> Bool = { false }) throws -> SatchelComputePipelineResult {
        lock.lock(); defer { lock.unlock() }
        if cancelled() { throw SatchelShaderError.cancelled }
        let url = path(identity, kind: "compute"), working = descriptor.copy() as! MTLComputePipelineDescriptor
        if let entry = entries[url], entry.bytes <= 64 * 1024 * 1024, let saved = try? archive(url) {
            working.binaryArchives = [saved]
            if let pipeline = try? device.makeComputePipelineState(descriptor: working, options: .failOnBinaryArchiveMiss, reflection: nil) {
                touch(url)
                return SatchelComputePipelineResult(pipeline: pipeline, persistentHit: true, persisted: true)
            }
        }
        do {
            let fresh = try archive(nil); working.binaryArchives = [fresh]
            try fresh.addComputePipelineFunctions(descriptor: working)
            let pipeline = try device.makeComputePipelineState(descriptor: working, options: [], reflection: nil)
            if cancelled() { throw SatchelShaderError.cancelled }
            return SatchelComputePipelineResult(pipeline: pipeline, persistentHit: false, persisted: persist(fresh, at: url))
        } catch let error as SatchelShaderError { throw error }
        catch { throw SatchelShaderCompilationError.failed((error as NSError).code) }
    }
}
