import CryptoKit
import Foundation

/// Bounded streaming decoder using M5's pinned BSD zstd bridge. One frame only.
final class SwitchZstdReader {
    private let context: UnsafeMutableRawPointer
    private let source: SwitchContentStorage
    private let cancelled: () -> Bool
    private var input = Data(), cursor = 0
    private var loaded: UInt64 = 0
    private(set) var consumed: UInt64 = 0
    private(set) var ended = false

    init(_ source: SwitchContentStorage, cancelled: @escaping () -> Bool) throws {
        guard let context = satchel_zstd_create(nil, 0, 27) else { throw SwitchExecutableError.limit }
        self.context = context; self.source = source; self.cancelled = cancelled
    }
    deinit { satchel_zstd_free(context) }
    func read(_ count: Int) throws -> Data {
        guard count > 0, count <= 1024 * 1024 else { throw SwitchExecutableError.limit }
        var output = Data(count: count), written = 0
        while written < count && !ended {
            if cancelled() { throw CancellationError() }
            if cursor == input.count && loaded < source.size {
                input = try source.bytes(loaded, min(65536, source.size - loaded))
                loaded += UInt64(input.count); cursor = 0
            }
            var available = input.count - cursor, capacity = count - written
            let status = input.withUnsafeBytes { src in output.withUnsafeMutableBytes { dst in
                satchel_zstd_step(context, src.baseAddress?.advanced(by: cursor), &available,
                             dst.baseAddress!.advanced(by: written), &capacity)
            } }
            guard status >= 0, status == 0 || available > 0 || capacity > 0 else { throw SwitchExecutableError.invalid }
            cursor += available; consumed += UInt64(available); written += capacity; ended = status == 0
        }
        output.removeSubrange(written..<output.count)
        return output
    }
    func finish(allowTrailing: Bool = false) throws {
        guard try read(1).isEmpty, ended, allowTrailing || consumed == source.size else { throw SwitchExecutableError.invalid }
    }
    static func decode(_ input: SwitchContentStorage, bytes: UInt64, cancelled: @escaping () -> Bool) throws -> Data {
        guard bytes > 0, bytes <= 512 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        let reader = try Self(input, cancelled: cancelled)
        var output = Data()
        while output.count < bytes {
            let part = try reader.read(Int(min(1024 * 1024, bytes - UInt64(output.count))))
            guard !part.isEmpty else { throw SwitchExecutableError.invalid }; output.append(part)
        }
        try reader.finish(); return output
    }
}

/// NCZSECTN/NCZBLOCK transport, based on nicoboss/nsz (MIT), revision
/// 148218ff89bb4b607d07730a8cf87d5e1f249ebc. Reconstruct encrypted NCA bytes;
/// the original CNMT, FS and content checks still run on the reconstructed view.
enum SwitchNCZ {
    private struct Region { let start: UInt64; let end: UInt64; let type: UInt64; let key: Data; let counter: Data }
    static func entries(_ entries: [SwitchPackageEntry], source: SwitchExecutableReader.Source,
                        keys: [String: Data], cancelled: @escaping () -> Bool, progress: SwitchByteProgress = { _, _ in }) throws -> [SwitchPackageEntry] {
        var result: [SwitchPackageEntry] = []
        for entry in entries {
            if cancelled() { throw CancellationError() }
            guard entry.name.lowercased().hasSuffix(".ncz") else { result.append(entry); continue }
            let input = SwitchContentStorage(size: entry.size) { offset, count in
                try source.read(entry.offset + offset, size: count, cancelled: cancelled)
            }
            let url = try source.scratchFile()
            let size = try expand(input, to: url, keys: keys, cancelled: cancelled, progress: progress)
            let offset = try source.attach(url, size: size)
            result.append(SwitchPackageEntry(name: String(entry.name.dropLast(1)) + "a", offset: offset, size: size))
        }
        return result
    }

    static func expand(_ input: SwitchContentStorage, to outputURL: URL, keys: [String: Data],
                       cancelled: @escaping () -> Bool, progress: SwitchByteProgress = { _, _ in }) throws -> UInt64 {
        guard input.size >= 0x4050 else { throw SwitchExecutableError.invalid }
        let prefix = try input.bytes(0, 0x4000)
        let key = try SwitchExecutableReader.key("header_key", count: 32, keys: keys)
        let header = try SwitchExecutableReader.crypt(Data(prefix.prefix(0xc00)), key: key, mode: .xts)
        guard header[0x200..<0x204] == Data("NCA3".utf8) else { throw SwitchExecutableError.unsupported }
        let size = header.cp64(0x208)
        guard size > 0x4000, size <= 128 * 1024 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        let table = try input.bytes(0x4000, 16), count = table.cp64(8)
        guard table.prefix(8) == Data("NCZSECTN".utf8), count > 0, count <= 65536 else { throw SwitchExecutableError.invalid }
        let records = try input.bytes(0x4010, count * 64)
        var regions: [Region] = [], previous: UInt64 = 0
        for i in 0..<Int(count) {
            let p = i * 64, start = records.cp64(p), length = records.cp64(p + 8), type = records.cp64(p + 16)
            guard start >= previous, length > 0, start <= size, length <= size - start,
                  records.cp64(p + 24) == 0 else { throw SwitchExecutableError.invalid }
            guard [1, 3, 4].contains(type) else {
                throw SwitchExecutableError.unsupportedFeature("NCZ encryption type \(type) is not supported.")
            }
            if i > 0 { guard start == previous else { throw SwitchExecutableError.invalid } }
            regions.append(Region(start: start, end: start + length, type: type,
                key: records.subdata(in: p+32..<p+48), counter: records.subdata(in: p+48..<p+64)))
            previous = start + length
        }
        guard previous == size else { throw SwitchExecutableError.invalid }
        // NCZ keeps the first 16 KiB verbatim, even when several original
        // encryption regions fit wholly inside that prefix.
        regions.removeAll { $0.end <= 0x4000 }
        guard !regions.isEmpty else { throw SwitchExecutableError.invalid }
        if regions[0].start > 0x4000 {
            regions.insert(Region(start: 0x4000, end: regions[0].start, type: 1, key: Data(), counter: Data()), at: 0)
        }
        let fm = FileManager.default
        func reserve(_ bytes: UInt64) throws {
            let free = (try fm.attributesOfFileSystem(forPath: outputURL.deletingLastPathComponent().path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
            guard free >= bytes + 512 * 1024 * 1024 else {
                throw SwitchExecutableError.unsupportedFeature("Not enough local storage to expand this NCZ archive safely.")
            }
        }
        try reserve(size)
        guard fm.createFile(atPath: outputURL.path, contents: nil, attributes: [.posixPermissions: 0o600]) else { throw SwitchExecutableError.invalid }
        var complete = false
        defer { if !complete { try? fm.removeItem(at: outputURL) } }
        let output = try FileHandle(forWritingTo: outputURL); defer { try? output.close() }
        try output.write(contentsOf: prefix)
        var position: UInt64 = 0x4000, regionIndex = 0
        progress(position, size)
        func write(_ bytes: Data) throws {
            if cancelled() { throw CancellationError() }
            try reserve(UInt64(bytes.count))
            guard UInt64(bytes.count) <= size - position else { throw SwitchExecutableError.invalid }
            var used = 0
            while used < bytes.count {
                while position >= regions[regionIndex].end { regionIndex += 1 }
                let region = regions[regionIndex], take = min(bytes.count - used, Int(region.end - position))
                var part = bytes.subdata(in: used..<used+take)
                if region.type == 3 || region.type == 4 {
                    let skip = Int(position % 16)
                    var counter = Data(region.counter.prefix(8))
                    for shift in (0..<8).reversed() { counter.append(UInt8(truncatingIfNeeded: (position / 16) >> (shift * 8))) }
                    let encoded = try SwitchExecutableReader.crypt(Data(count: skip) + part, key: region.key, mode: .ctr(counter))
                    part = Data(encoded.dropFirst(skip))
                }
                try output.write(contentsOf: part); position += UInt64(take); used += take
            }
            progress(position, size)
        }
        var cursor = 0x4010 + count * 64
        let magic = try input.bytes(cursor, min(8, input.size - cursor))
        if magic == Data("NCZBLOCK".utf8) {
            let block = try input.bytes(cursor, 24), exponent = block[11], blocks = UInt64(block.cp32(12))
            guard block[8] == 2, block[9] == 1, block[10] == 0, (14...32).contains(exponent),
                  block.cp64(16) == size - 0x4000 else { throw SwitchExecutableError.invalid }
            let blockSize = UInt64(1) << exponent
            guard blocks == (size - 0x4001) / blockSize + 1, blocks <= 1_000_000 else { throw SwitchExecutableError.limit }
            let sizes = try input.bytes(cursor + 24, blocks * 4)
            cursor += 24 + blocks * 4
            for i in 0..<Int(blocks) {
                let stored = UInt64(sizes.cp32(i * 4)), expanded = min(blockSize, size - position)
                guard stored > 0, stored <= expanded, cursor <= input.size, stored <= input.size - cursor else { throw SwitchExecutableError.invalid }
                let start = cursor
                if stored == expanded {
                    var copied: UInt64 = 0
                    while copied < stored {
                        try autoreleasepool {
                            let part = try input.bytes(start + copied, min(65536, stored - copied))
                            try write(part); copied += UInt64(part.count)
                        }
                    }
                } else {
                    let frame = try SwitchZstdReader(SwitchContentStorage(size: stored) { o, n in try input.bytes(start + o, n) }, cancelled: cancelled)
                    let end = position + expanded
                    while position < end {
                        try autoreleasepool {
                            let part = try frame.read(Int(min(65536, end - position)))
                            guard !part.isEmpty else { throw SwitchExecutableError.invalid }; try write(part)
                        }
                    }
                    try frame.finish()
                }
                cursor += stored
            }
            guard cursor == input.size else { throw SwitchExecutableError.invalid }
        } else {
            let start = cursor
            let frame = try SwitchZstdReader(SwitchContentStorage(size: input.size - start) { o, n in try input.bytes(start + o, n) }, cancelled: cancelled)
            while position < size {
                try autoreleasepool {
                    let part = try frame.read(Int(min(65536, size - position)))
                    guard !part.isEmpty else { throw SwitchExecutableError.invalid }; try write(part)
                }
            }
            try frame.finish()
        }
        guard position == size else { throw SwitchExecutableError.invalid }
        try output.synchronize(); complete = true
        return size
    }
}
