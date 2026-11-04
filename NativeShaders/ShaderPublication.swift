import Foundation
import Metal

public struct SatchelShaderPublication: Sendable {
    public let key: String
    public let stage: SatchelShaderStage
    public let requestSHA256: String
    public let directory: URL
    public let revision: UInt64
    public let sourceSHA256: [String: String]
}

// Publications are immutable request directories. A renderer keeps one snapshot
// for both its reflection read and library load, avoiding mixed generations.
@available(iOS 18.0, macOS 15.0, *)
public final class SatchelShaderPublicationService {
    public let compiler: SatchelShaderCompiler
    public let skippedControlRanges: [Range<Int>]
    private let store: SatchelShaderStore
    private let directory: URL
    private let reserveBytes: UInt64
    private let maximumPublicationBytes: UInt64
    private var publicationBytes: UInt64 = 0
    private let lock = NSLock()
    private var published: [String: SatchelShaderPublication] = [:]
    private var revision: UInt64 = 0

    public init(directory: URL, device: MTLDevice, maximumPublicationBytes: UInt64 = 8 * 1024 * 1024 * 1024,
                sourceCacheBytes: Int = 2 * 1024 * 1024 * 1024, reserveBytes: UInt64 = 512 * 1024 * 1024,
                skippedControlRanges: [Range<Int>] = []) throws {
        guard reserveBytes <= UInt64(Int.max - 128 * 1024 * 1024), maximumPublicationBytes <= 64 * 1024 * 1024 * 1024,
              skippedControlRanges.count <= 16 else {
            throw SatchelShaderCacheError.invalidIdentity
        }
        self.directory = directory.appendingPathComponent("publications", isDirectory: true)
        self.reserveBytes = reserveBytes
        self.maximumPublicationBytes = maximumPublicationBytes
        self.skippedControlRanges = skippedControlRanges
        compiler = SatchelShaderCompiler(device: device)
        store = try SatchelShaderStore(directory: directory.appendingPathComponent("source-cache"),
            deviceIdentity: compiler.deviceIdentity, maximumBytes: sourceCacheBytes, reserveBytes: Int(reserveBytes))
        try FileManager.default.createDirectory(at: self.directory, withIntermediateDirectories: true)
        guard let files = FileManager.default.enumerator(at: self.directory, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]) else {
            throw SatchelShaderCacheError.invalidIdentity
        }
        for case let file as URL in files {
            let info = try file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
            guard info.isSymbolicLink != true else { throw SatchelShaderCacheError.invalidIdentity }
            if info.isRegularFile == true {
                let size = UInt64(max(0, info.fileSize ?? 0))
                guard size <= maximumPublicationBytes - min(publicationBytes, maximumPublicationBytes) else {
                    throw SatchelShaderCacheError.insufficientSpace
                }
                publicationBytes += size
            }
        }
    }

    public func current(key: String) -> SatchelShaderPublication? {
        lock.lock(); defer { lock.unlock() }
        return published[key]
    }

    public func prepare(_ request: SatchelShaderRequest, cancelled: () -> Bool = { false }) throws -> SatchelShaderPublication {
        try autoreleasepool {
            let shader = try store.prepare(request, cancelled: cancelled)
            lock.lock(); defer { lock.unlock() }
            if let have = published[shader.key], have.requestSHA256 == shader.requestSHA256 { return have }
            if cancelled() { throw SatchelShaderError.cancelled }
            let fm = FileManager.default, destination = directory.appendingPathComponent(shader.requestSHA256, isDirectory: true)
            let hashes = shader.sources.mapValues { SatchelShaderTranslator.sha256(Data($0.utf8)) }
            let reflections = shader.variantReflectionJSON ?? ["b": shader.reflectionJSON]
            let reflectionBytes = reflections.values.reduce(UInt64(0)) { $0 + UInt64($1.count) }
            let sourceBytes = shader.sources.values.reduce(UInt64(0)) { $0 + UInt64($1.utf8.count) }
            guard sourceBytes <= 64 * 1024 * 1024, published[shader.key] != nil || published.count < 100_000,
                  Set(shader.sources.keys).isSubset(of: SatchelTranslatedShader.allowedVariants) else {
                throw SatchelShaderDiscoveryError.limit
            }
            if !fm.fileExists(atPath: destination.path) {
                guard sourceBytes + reflectionBytes + 1024 * 1024 <= maximumPublicationBytes - min(publicationBytes, maximumPublicationBytes) else {
                    throw SatchelShaderCacheError.insufficientSpace
                }
                let free = (try fm.attributesOfFileSystem(forPath: directory.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
                guard free >= reserveBytes + sourceBytes + reflectionBytes + 1024 * 1024 else {
                    throw SatchelShaderCacheError.insufficientSpace
                }
                let temporary = directory.appendingPathComponent(UUID().uuidString + ".pending", isDirectory: true)
                try fm.createDirectory(at: temporary, withIntermediateDirectories: false)
                defer { try? fm.removeItem(at: temporary) }
                for (suffix, source) in shader.sources {
                    if cancelled() { throw SatchelShaderError.cancelled }
                    try Data(source.utf8).write(to: temporary.appendingPathComponent(shader.key + "_" + suffix + ".metal"))
                }
                for (suffix, reflection) in reflections {
                    try reflection.write(to: temporary.appendingPathComponent(shader.key + "_" + suffix + ".json"))
                }
                let analysis: [String: Any] = ["requestSHA256": shader.requestSHA256, "recordSHA256": shader.recordSHA256,
                    "c1Unproven": shader.c1Unproven, "constantBufferExtents": Dictionary(uniqueKeysWithValues: shader.constantBufferExtents.map { (String($0.key), $0.value) }),
                    "unwrittenStorageBindings": shader.unwrittenStorageBindings, "sourceSHA256": hashes,
                    "reflectionSHA256": SatchelShaderTranslator.sha256(shader.reflectionJSON),
                    "variantReflectionSHA256": reflections.mapValues { SatchelShaderTranslator.sha256($0) },
                    "implementation": SatchelShaderTranslator.implementationID]
                let analysisData = try JSONSerialization.data(withJSONObject: analysis, options: [.sortedKeys])
                try analysisData.write(to: temporary.appendingPathComponent("analysis.json"))
                try fm.moveItem(at: temporary, to: destination)
                publicationBytes += sourceBytes + reflectionBytes + UInt64(analysisData.count)
            }
            // Reused publications must agree with the verified source-cache entry.
            for (suffix, expected) in hashes {
                let path = destination.appendingPathComponent(shader.key + "_" + suffix + ".metal")
                guard (try path.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) <= 64 * 1024 * 1024,
                      SatchelShaderTranslator.sha256(try Data(contentsOf: path)) == expected else { throw SatchelShaderCacheError.corruptEntry }
            }
            for (suffix, expected) in reflections {
                let path = destination.appendingPathComponent(shader.key + "_" + suffix + ".json")
                guard (try path.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) == expected.count,
                      try Data(contentsOf: path) == expected else { throw SatchelShaderCacheError.corruptEntry }
            }
            revision &+= 1
            let result = SatchelShaderPublication(key: shader.key, stage: request.stage, requestSHA256: shader.requestSHA256,
                directory: destination, revision: revision, sourceSHA256: hashes)
            published[shader.key] = result
            return result
        }
    }

    public func library(_ publication: SatchelShaderPublication, variant: String = "b",
                        cancelled: () -> Bool = { false }) throws -> SatchelCompiledShader {
        guard let expected = publication.sourceSHA256[variant] else { throw SatchelShaderCompilationError.missingVariant }
        let url = publication.directory.appendingPathComponent(publication.key + "_" + variant + ".metal")
        guard (try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) <= 64 * 1024 * 1024 else {
            throw SatchelShaderCompilationError.invalidSource
        }
        let bytes = try Data(contentsOf: url)
        guard SatchelShaderTranslator.sha256(bytes) == expected, let source = String(data: bytes, encoding: .utf8) else {
            throw SatchelShaderCacheError.corruptEntry
        }
        return try compiler.compile(source: source, cancelled: cancelled)
    }
}
