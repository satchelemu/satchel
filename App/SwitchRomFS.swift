import CryptoKit
import Foundation

/// Independent IVFC/RomFS reader. Format references: hactool ivfc.h and
/// nca.c. Every returned byte is verified through the complete hash chain;
/// publisher signatures are outside this reader. No extraction to disk.
final class SwitchIntegrityStorage {
    private struct Level { let offset: UInt64; let size: UInt64; let block: UInt64 }
    private let levels: [Level]
    private let master: Data
    private let source: (UInt64, UInt64) throws -> Data
    private let cancelled: () -> Bool
    private var cache: [String: Data] = [:]
    private var cachedBytes = 0
    private var active = true
    private(set) var verifiedBlocks = 0
    private var expanded: SwitchContentStorage?
    var size: UInt64 { expanded?.size ?? levels.last!.size }
    static let maximumRead: UInt64 = 1024 * 1024

    init(header: Data, sectionSize: UInt64, cancelled: @escaping () -> Bool = { false },
         read: @escaping (UInt64, UInt64) throws -> Data) throws {
        guard header.count == 512, header[8..<12] == Data("IVFC".utf8),
              header.rfs32(12) == 0x20000, header.rfs32(16) == 32,
              header.rfs32(20) == 7 else { throw SwitchExecutableError.unsupported }
        var parsed: [Level] = []
        for i in 0..<6 {
            let p = 24 + i * 24
            let offset = header.rfs64(p), size = header.rfs64(p + 8), exponent = header.rfs32(p + 16)
            guard (9...20).contains(exponent), header.rfs32(p + 20) == 0,
                  size > 0, offset <= sectionSize, size <= sectionSize - offset else { throw SwitchExecutableError.invalid }
            let block = UInt64(1) << exponent
            guard offset % block == 0 else { throw SwitchExecutableError.invalid }
            let blocks = (size - 1) / block + 1
            guard blocks <= UInt64.max / 32,
                  blocks * 32 <= (i == 0 ? 32 : parsed[i - 1].size) else { throw SwitchExecutableError.invalid }
            parsed.append(Level(offset: offset, size: size, block: block))
        }
        let sorted = parsed.sorted { $0.offset < $1.offset }
        for i in 1..<sorted.count {
            guard sorted[i - 1].offset + sorted[i - 1].size <= sorted[i].offset else { throw SwitchExecutableError.invalid }
        }
        levels = parsed; master = header.subdata(in: 0xc8..<0xe8)
        source = read; self.cancelled = cancelled
        let raw = SwitchContentStorage(size: levels.last!.size) { [unowned self] offset, count in
            var output = Data(), done: UInt64 = 0
            while done < count {
                let take = min(Self.maximumRead, count - done)
                output.append(try self.readLevel(self.levels.count - 1, offset: offset + done, count: take)); done += take
            }
            return output
        }
        expanded = try SwitchContentStorage.compressed(header: header, verified: raw, cancelled: cancelled)
    }

    func read(_ offset: UInt64, count: UInt64) throws -> Data {
        try checkAvailable()
        guard count <= Self.maximumRead else { throw SwitchExecutableError.limit }
        return try expanded!.bytes(offset, count)
    }

    func invalidate() { active = false; cache.removeAll(); cachedBytes = 0 }
    func checkAvailable() throws {
        guard active else { throw SwitchExecutableError.invalid }
        if cancelled() { throw CancellationError() }
    }

    private func readLevel(_ level: Int, offset: UInt64, count: UInt64) throws -> Data {
        if cancelled() { throw CancellationError() }
        let entry = levels[level]
        guard offset <= entry.size, count <= entry.size - offset else { throw SwitchExecutableError.invalid }
        var result = Data(); result.reserveCapacity(Int(count))
        // Batch only required, uncached leaf blocks. Verification still checks
        // each block through its parent chain before returning any bytes.
        // The span is bounded by this read (at most two 1 MiB blocks).
        var batch: (offset: UInt64, bytes: Data)?
        if level == levels.count - 1, count > 0 {
            let first = offset / entry.block, last = (offset + count - 1) / entry.block
            var missingFirst: UInt64?, missingLast: UInt64 = 0
            for index in first...last where cache["\(level):\(index)"] == nil {
                if missingFirst == nil { missingFirst = index }; missingLast = index
            }
            if let missingFirst {
                let start = missingFirst * entry.block
                let end = missingLast == (entry.size - 1) / entry.block ? entry.size : (missingLast + 1) * entry.block
                let length = end - start
                let bytes = try source(entry.offset + start, length)
                guard bytes.count == length else { throw SwitchExecutableError.invalid }
                batch = (start, bytes)
            }
        }
        var position = offset
        while position < offset + count {
            let bytes = try block(level, index: position / entry.block, batch: batch)
            let start = Int(position % entry.block), take = min(bytes.count - start, Int(offset + count - position))
            result.append(bytes[start..<(start + take)]); position += UInt64(take)
        }
        return result
    }

    private func block(_ level: Int, index: UInt64, batch: (offset: UInt64, bytes: Data)? = nil) throws -> Data {
        if cancelled() { throw CancellationError() }
        let entry = levels[level], key = "\(level):\(index)"
        if let bytes = cache[key] { return bytes }
        guard index < (entry.size - 1) / entry.block + 1 else { throw SwitchExecutableError.invalid }
        let offset = index * entry.block, count = min(entry.block, entry.size - offset)
        var bytes: Data
        if let batch, offset >= batch.offset, offset - batch.offset <= batch.bytes.count,
           count <= UInt64(batch.bytes.count) - (offset - batch.offset) {
            let start = Int(offset - batch.offset)
            bytes = batch.bytes.subdata(in: start..<start + Int(count))
        } else { bytes = try source(entry.offset + offset, count) }
        guard bytes.count == count else { throw SwitchExecutableError.invalid }
        // IVFC hashes zero padding to the block boundary, including the last block.
        bytes.append(Data(count: Int(entry.block - count)))
        let expected = level == 0 ? master : try readLevel(level - 1, offset: index * 32, count: 32)
        guard Data(SHA256.hash(data: bytes)) == expected else { throw SwitchExecutableError.integrity }
        verifiedBlocks += 1
        // Bound retained data independently of the archive size. Verified bytes,
        // not just verdicts, are cached so mutable backing files cannot bypass checks.
        if cachedBytes + bytes.count > 8 * 1024 * 1024 { cache.removeAll(keepingCapacity: true); cachedBytes = 0 }
        cache[key] = bytes; cachedBytes += bytes.count
        return bytes
    }
}

final class SwitchRomFS {
    struct File { fileprivate let owner: UUID; fileprivate let offset: UInt64; let size: UInt64 }
    private let identity = UUID()
    private let storage: SwitchIntegrityStorage
    private let directories: Data
    private let files: Data
    private let dataOffset: UInt64
    private let directoryStarts: Set<UInt32>
    private let fileStarts: Set<UInt32>
    private let cancelled: () -> Bool
    var verifiedBlocks: Int { storage.verifiedBlocks }
    var directoryCount: Int { directoryStarts.count }
    var fileCount: Int { fileStarts.count }
    var imageBytes: UInt64 { storage.size }

    /// Verified raw image access for the guest's read-only IStorage interface.
    /// The guest SDK may parse RomFS itself; this does not extract files.
    func readStorage(_ offset: UInt64, count: UInt64) throws -> Data {
        try storage.read(offset, count: count)
    }

    init(storage: SwitchIntegrityStorage, cancelled: @escaping () -> Bool = { false }, progress: SwitchByteProgress = { _, _ in }) throws {
        self.storage = storage; self.cancelled = cancelled
        let header = try storage.read(0, count: 80)
        guard header.rfs64(0) == 80 else { throw SwitchExecutableError.invalid }
        dataOffset = header.rfs64(72)
        guard dataOffset >= 80, dataOffset <= storage.size else { throw SwitchExecutableError.invalid }
        var ranges: [(UInt64, UInt64)] = []
        for p in stride(from: 8, through: 56, by: 16) {
            let offset = header.rfs64(p), size = header.rfs64(p + 8)
            guard offset >= 80, offset <= storage.size, size <= storage.size - offset,
                  size <= 64 * 1024 * 1024 else { throw SwitchExecutableError.limit }
            ranges.append((offset, size))
        }
        let sorted = ranges.filter { $0.1 > 0 }.sorted { $0.0 < $1.0 }
        for i in 1..<max(1, sorted.count) {
            guard sorted[i - 1].0 + sorted[i - 1].1 <= sorted[i].0 else { throw SwitchExecutableError.invalid }
        }
        var tableBytes: UInt64 = 0
        let tableTotal = ranges[1].1 + ranges[3].1
        progress(0, tableTotal)
        func table(_ index: Int) throws -> Data {
            var result = Data(), position: UInt64 = 0
            while position < ranges[index].1 {
                let count = min(SwitchIntegrityStorage.maximumRead, ranges[index].1 - position)
                result.append(try storage.read(ranges[index].0 + position, count: count)); position += count
                tableBytes += count; progress(tableBytes, tableTotal)
            }
            return result
        }
        directories = try table(1); files = try table(3)
        func entries(_ table: Data, fixed: Int) throws -> Set<UInt32> {
            var starts = Set<UInt32>(), position = 0
            while position < table.count {
                if cancelled() { throw CancellationError() }
                guard table.count - position >= fixed else { throw SwitchExecutableError.invalid }
                let length = Int(table.rfs32(position + fixed - 4)), size = (fixed + length + 3) & ~3
                guard length <= 0x300, size <= table.count - position,
                      starts.count < 1_000_000 else { throw SwitchExecutableError.limit }
                starts.insert(UInt32(position)); position += size
            }
            return starts
        }
        directoryStarts = try entries(directories, fixed: 24); fileStarts = try entries(files, fixed: 32)
        guard directoryStarts.contains(0), directories.rfs32(20) == 0,
              directories.rfs32(0) == 0 else { throw SwitchExecutableError.invalid }
        // Validate all pointers and extents before exposing lookup. Traversal also
        // rejects cycles, duplicate names and cross-parent aliases on each path.
        for offset in directoryStarts {
            if cancelled() { throw CancellationError() }
            let p = Int(offset)
            guard directoryStarts.contains(directories.rfs32(p)),
                  validLink(directories.rfs32(p + 4), in: directoryStarts),
                  validLink(directories.rfs32(p + 8), in: directoryStarts),
                  validLink(directories.rfs32(p + 12), in: fileStarts) else { throw SwitchExecutableError.invalid }
            if offset != 0 { _ = try name(directories, at: p, fixed: 24) }
        }
        for offset in fileStarts {
            if cancelled() { throw CancellationError() }
            let p = Int(offset), relative = files.rfs64(p + 8), count = files.rfs64(p + 16)
            guard directoryStarts.contains(files.rfs32(p)), validLink(files.rfs32(p + 4), in: fileStarts),
                  relative <= storage.size - dataOffset, count <= storage.size - dataOffset - relative else { throw SwitchExecutableError.invalid }
            let absolute = dataOffset + relative
            guard ranges.allSatisfy({ count == 0 || $0.1 == 0 || absolute + count <= $0.0 || $0.0 + $0.1 <= absolute }) else {
                throw SwitchExecutableError.invalid
            }
            _ = try name(files, at: p, fixed: 32)
        }
    }

    private func validLink(_ link: UInt32, in starts: Set<UInt32>) -> Bool { link == UInt32.max || starts.contains(link) }
    private func name(_ bytes: Data, at position: Int, fixed: Int) throws -> String {
        let count = Int(bytes.rfs32(position + fixed - 4))
        guard count > 0, let value = String(data: bytes[(position + fixed)..<(position + fixed + count)], encoding: .utf8),
              value != ".", value != "..", !value.contains("/"), !value.contains("\\"),
              !value.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else { throw SwitchExecutableError.invalid }
        return value
    }

    func open(_ path: String) throws -> File? {
        try storage.checkAvailable()
        guard path.utf8.count <= 0x300, path.hasPrefix("/"), !path.contains("\\") else { throw SwitchExecutableError.invalid }
        let parts = path.dropFirst().split(separator: "/", omittingEmptySubsequences: false).map(String.init)
        guard !parts.isEmpty, parts.count <= 64, parts.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." }) else { throw SwitchExecutableError.invalid }
        var parent: UInt32 = 0
        for (index, part) in parts.enumerated() {
            let isFile = index == parts.count - 1, bytes = isFile ? files : directories, fixed = isFile ? 32 : 24
            var current = directories.rfs32(Int(parent) + (isFile ? 12 : 8))
            var visited = Set<UInt32>(), names = Set<String>(), found: UInt32?
            while current != UInt32.max {
                if cancelled() { throw CancellationError() }
                guard visited.insert(current).inserted, bytes.rfs32(Int(current)) == parent else { throw SwitchExecutableError.invalid }
                let value = try name(bytes, at: Int(current), fixed: fixed)
                guard names.insert(value).inserted else { throw SwitchExecutableError.invalid }
                if value == part { found = current }
                current = bytes.rfs32(Int(current) + 4)
            }
            guard let found else { return nil }
            if isFile { return File(owner: identity, offset: dataOffset + files.rfs64(Int(found) + 8), size: files.rfs64(Int(found) + 16)) }
            parent = found
        }
        return nil
    }

    func read(_ file: File, offset: UInt64, count: UInt64) throws -> Data {
        guard file.owner == identity, offset <= file.size, count <= file.size - offset else { throw SwitchExecutableError.invalid }
        return try storage.read(file.offset + offset, count: count)
    }

    struct TreeEntry {
        let name: Data
        let directory: UInt32?
        let file: File?
        var firstChild: UInt64 = 0
        var childCount: UInt64 = 0
    }

    /// Breadth-first directory groups, sorted by raw UTF-8 as DMROMFS1 requires.
    /// Every metadata entry must be reachable exactly once. This rejects hidden
    /// cycles, cross-parent aliases and duplicate file/directory names.
    func tree(cancelled check: () -> Bool = { false }, progress: SwitchByteProgress = { _, _ in }) throws -> [TreeEntry] {
        try storage.checkAvailable()
        var result = [TreeEntry(name: Data(), directory: 0, file: nil)]
        var seenDirectories: Set<UInt32> = [0], seenFiles = Set<UInt32>(), index = 0
        var nameBytes = 0
        let total = UInt64(directoryStarts.count + fileStarts.count)
        progress(1, total)
        while index < result.count {
            if cancelled() || check() { throw CancellationError() }
            guard let parent = result[index].directory else { index += 1; continue }
            var children: [TreeEntry] = [], names = Set<Data>()
            for isFile in [false, true] {
                let bytes = isFile ? files : directories, fixed = isFile ? 32 : 24
                var current = directories.rfs32(Int(parent) + (isFile ? 12 : 8))
                while current != UInt32.max {
                    if cancelled() || check() { throw CancellationError() }
                    guard bytes.rfs32(Int(current)) == parent else { throw SwitchExecutableError.invalid }
                    let inserted = isFile ? seenFiles.insert(current).inserted : seenDirectories.insert(current).inserted
                    guard inserted else { throw SwitchExecutableError.invalid }
                    let value = Data(try name(bytes, at: Int(current), fixed: fixed).utf8)
                    guard names.insert(value).inserted else { throw SwitchExecutableError.invalid }
                    nameBytes += value.count
                    guard nameBytes <= 64 * 1024 * 1024,
                          seenFiles.count + seenDirectories.count <= 1_000_000 else { throw SwitchExecutableError.limit }
                    if isFile {
                        children.append(TreeEntry(name: value, directory: nil, file: File(owner: identity,
                            offset: dataOffset + files.rfs64(Int(current) + 8), size: files.rfs64(Int(current) + 16))))
                    } else { children.append(TreeEntry(name: value, directory: current, file: nil)) }
                    progress(UInt64(seenFiles.count + seenDirectories.count), total)
                    current = bytes.rfs32(Int(current) + 4)
                }
            }
            children.sort { $0.name.lexicographicallyPrecedes($1.name) }
            result[index].firstChild = UInt64(result.count); result[index].childCount = UInt64(children.count)
            result.append(contentsOf: children); index += 1
        }
        guard seenDirectories.count == directoryStarts.count, seenFiles.count == fileStarts.count else { throw SwitchExecutableError.invalid }
        return result
    }

    /// Exercise lookup and a small authenticated asset read without exporting
    /// its path or contents. This is storage validation, not guest filesystem IPC.
    func verifySample() throws -> Int {
        try storage.checkAvailable()
        guard let first = fileStarts.sorted().first(where: { files.rfs64(Int($0) + 16) > 0 }) else { return 0 }
        var components = [try name(files, at: Int(first), fixed: 32)]
        var parent = files.rfs32(Int(first)), visited = Set<UInt32>()
        while parent != 0 {
            if cancelled() { throw CancellationError() }
            guard components.count < 64, visited.insert(parent).inserted else { throw SwitchExecutableError.invalid }
            components.append(try name(directories, at: Int(parent), fixed: 24))
            parent = directories.rfs32(Int(parent))
        }
        guard let file = try open("/" + components.reversed().joined(separator: "/")),
              file.offset == dataOffset + files.rfs64(Int(first) + 8),
              file.size == files.rfs64(Int(first) + 16) else { throw SwitchExecutableError.invalid }
        return try read(file, offset: 0, count: min(4096, file.size)).count
    }
}

private extension Data {
    func rfs32(_ p: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[p + $1]) << ($1 * 8) } }
    func rfs64(_ p: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[p + $1]) << ($1 * 8) } }
}
