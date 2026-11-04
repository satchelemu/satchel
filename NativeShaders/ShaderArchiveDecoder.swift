import Foundation

public final class SatchelZstdArchiveDecoder: SatchelShaderArchiveDecoder {
    private let scratch: URL
    private let maximumExpandedBytes: UInt64
    private let reserveBytes: UInt64
    private let cancelled: () -> Bool
    private let lock = NSLock()
    private var dictionaries: [UInt32: Data] = [:]

    public init(scratchDirectory: URL, maximumExpandedBytes: UInt64 = 512 * 1024 * 1024,
                reserveBytes: UInt64 = 512 * 1024 * 1024, cancelled: @escaping () -> Bool = { false }) throws {
        guard maximumExpandedBytes > 0, maximumExpandedBytes <= 4 * 1024 * 1024 * 1024,
              reserveBytes <= UInt64.max - 1024 * 1024 else {
            throw SatchelShaderDiscoveryError.limit
        }
        scratch = scratchDirectory; self.maximumExpandedBytes = maximumExpandedBytes
        self.reserveBytes = reserveBytes; self.cancelled = cancelled
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
    }

    // The content owner can locate a dictionary archive by its content/path. TOTK
    // supplies Pack/ZsDic.pack.zs. No title identifier is required by the decoder.
    public func loadDictionaryArchive(_ input: SatchelShaderInput) throws {
        var loaded: [UInt32: Data] = [:], total = 0
        try withExpanded(input) { archive in
            try SatchelShaderDiscovery.sarcMembers(archive, cancelled: cancelled) { _, member in
                guard member.count >= 8 else { return }
                let header = try member.read(offset: 0, count: 8)
                guard try ShaderRecord.word(header, 0) == 0xec30a437 else { return }
                guard member.count <= 16 * 1024 * 1024, loaded.count < 32 else { throw SatchelShaderDiscoveryError.limit }
                total += Int(member.count)
                guard total <= 16 * 1024 * 1024 else { throw SatchelShaderDiscoveryError.limit }
                let id = try ShaderRecord.word(header, 4), bytes = try member.bytes(0, Int(member.count))
                guard id != 0, loaded[id] == nil || loaded[id] == bytes else { throw SatchelShaderDiscoveryError.malformedArchive }
                loaded[id] = bytes
            }
        }
        lock.lock(); defer { lock.unlock() }
        // Use one decoder per content view so dictionary IDs cannot alias titles.
        dictionaries = loaded
    }

    public func withExpanded(_ input: SatchelShaderInput, body: (SatchelShaderInput) throws -> Void) throws {
        if cancelled() { throw SatchelShaderError.cancelled }
        let header = try input.read(offset: 0, count: Int(min(18, input.count)))
        let id = header.withUnsafeBytes { satchel_zstd_dictionary_id($0.baseAddress, header.count) }
        lock.lock(); let dictionary = dictionaries[id]; lock.unlock()
        guard id == 0 || dictionary != nil else { throw SatchelShaderDiscoveryError.decoderRequired }
        let bytes = dictionary ?? Data()
        guard let context = bytes.withUnsafeBytes({ satchel_zstd_create($0.baseAddress, bytes.count, 27) }) else {
            throw SatchelShaderDiscoveryError.malformedArchive
        }
        defer { satchel_zstd_free(context) }
        let fm = FileManager.default, url = scratch.appendingPathComponent(UUID().uuidString + ".shader-spill")
        guard fm.createFile(atPath: url.path, contents: nil) else { throw SatchelShaderDiscoveryError.bounds }
        defer { try? fm.removeItem(at: url) }
        let output = try FileHandle(forWritingTo: url)
        defer { try? output.close() }
        var offset: UInt64 = 0, written: UInt64 = 0, ended = false
        var buffer = Data(count: 65536)
        while offset < input.count {
            if cancelled() { throw SatchelShaderError.cancelled }
            let free = (try fm.attributesOfFileSystem(forPath: scratch.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
            guard free >= reserveBytes + 1024 * 1024 else { throw SatchelShaderCacheError.insufficientSpace }
            let chunk = try input.read(offset: offset, count: Int(min(65536, input.count - offset)))
            var consumed = 0, produced = buffer.count
            while consumed < chunk.count || produced == buffer.count {
                if cancelled() { throw SatchelShaderError.cancelled }
                var available = chunk.count - consumed; produced = buffer.count
                let status = chunk.withUnsafeBytes { source in buffer.withUnsafeMutableBytes { destination in
                    satchel_zstd_step(context, source.baseAddress!.advanced(by: consumed), &available, destination.baseAddress, &produced)
                } }
                if status == 1 && available == 0 && produced == 0 && consumed == chunk.count { break }
                guard status >= 0, available != 0 || produced != 0 || status == 0 else {
                    throw SatchelShaderDiscoveryError.malformedArchive
                }
                consumed += available
                guard UInt64(produced) <= maximumExpandedBytes - written else { throw SatchelShaderDiscoveryError.limit }
                written += UInt64(produced)
                try output.write(contentsOf: buffer.prefix(produced))
                if status == 0 {
                    guard offset + UInt64(consumed) == input.count else { throw SatchelShaderDiscoveryError.malformedArchive }
                    ended = true; break
                }
            }
            offset += UInt64(chunk.count)
        }
        guard ended else { throw SatchelShaderDiscoveryError.malformedArchive }
        try output.synchronize()
        let handle = try FileHandle(forReadingFrom: url); defer { try? handle.close() }
        let readLock = NSLock()
        try body(SatchelShaderInput(count: written) { offset, size in
            if self.cancelled() { throw SatchelShaderError.cancelled }
            readLock.lock(); defer { readLock.unlock() }
            try handle.seek(toOffset: offset)
            return try handle.read(upToCount: size) ?? Data()
        })
    }
}
