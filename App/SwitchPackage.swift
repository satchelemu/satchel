import CryptoKit
import Foundation

typealias SwitchByteProgress = (UInt64, UInt64) -> Void

enum SwitchPackageFormat: String, Codable { case xci, nsp, xcz, nsz }

/// Container structure only: no NCA decryption, title/update matching, signature
/// verification, or assertion that encrypted game content can execute.
/// Counts describe NSP entries or XCI secure-partition entries; contentBytes is
/// their total declared file size, excluding container headers and padding.
struct SwitchPackageSummary: Codable {
    let format: SwitchPackageFormat
    let entryCount: Int
    let contentBytes: UInt64
}

struct SwitchPackageEntry {
    let name: String
    let offset: UInt64
    let size: UInt64
}

enum SwitchPackageError: Error, LocalizedError {
    case unsupported, invalid, limit, hashMismatch, unreadable

    var errorDescription: String? {
        switch self {
        case .unsupported: return "This file is not a supported XCI, NSP, XCZ or NSZ container."
        case .invalid: return "The package directory is incomplete, empty, overlapping, or malformed."
        case .limit: return "The package exceeds the bounded container inspection limits."
        case .hashMismatch: return "An XCI container hash does not match its declared region."
        case .unreadable: return "The package could not be read. Check that the file is available locally."
        }
    }
}

enum SwitchPackageReader {
    // Layout: https://switchbrew.org/wiki/XCI and https://switchbrew.org/wiki/NCA
    // Independent file-backed parser. Limits are importer policy, not format maxima.
    // Metadata <=16 MiB/directory, <=8192 entries, <=1024 UTF-8 bytes/name;
    // each declared hash region <=16 MiB, aggregate hashes <=128 MiB. XCI hashes
    // cover the root header and declared root/secure entry regions, not all game
    // bytes. NSP has no container hashes. Call from a worker with file access
    // coordination/security scope owned by the caller. CancellationError is propagated.
    private static let metadataLimit: UInt64 = 16 * 1024 * 1024
    private static let hashBudget: UInt64 = 128 * 1024 * 1024
    private static let chunkSize: UInt64 = 256 * 1024

    static func inspect(_ url: URL, cancelled: () -> Bool = { false }) throws -> SwitchPackageSummary {
        try checkCancellation(cancelled)
        let (format, directory) = try inspectDirectory(open(url), cancelled: cancelled)
        return try summary(format, directory)
    }

    static func inspect(_ stream: SwitchPackageStream, cancelled: () -> Bool = { false }) throws -> SwitchPackageSummary {
        let (format, directory) = try inspectDirectory(stream, cancelled: cancelled)
        return try summary(format, directory)
    }

    /// File-relative extents after the same checks used by inspect(). No content
    /// bytes or keys are exposed; callers must retain coordinated access to URL.
    static func contentEntries(_ url: URL, cancelled: () -> Bool = { false }, progress: SwitchByteProgress = { _, _ in }) throws -> [SwitchPackageEntry] {
        try checkCancellation(cancelled)
        let (_, directory) = try inspectDirectory(open(url), cancelled: cancelled, progress: progress)
        return directory.entries.map { SwitchPackageEntry(name: $0.name, offset: $0.offset, size: $0.size) }
    }

    private static func open(_ url: URL) throws -> SwitchPackageStream {
        do { return try SwitchPackageStream(urls: [url]) }
        catch let error as SwitchPackageError { throw error }
        catch { throw SwitchPackageError.unreadable }
    }

    private static func inspectDirectory(_ stream: SwitchPackageStream, cancelled: () -> Bool, progress: SwitchByteProgress = { _, _ in }) throws -> (SwitchPackageFormat, Directory) {
        try checkCancellation(cancelled)
        do {
            let file = Reader(stream)
            if try file.magic(at: 0, cancelled: cancelled) == "PFS0" {
                let directory = try file.directory(at: 0, length: file.size, hashed: false, cancelled: cancelled)
                _ = try summary(.nsp, directory)
                try file.verifyPending(cancelled: cancelled, progress: progress)
                return (directory.entries.contains { $0.name.lowercased().hasSuffix(".ncz") } ? .nsz : .nsp, directory)
            }
            let prefix: UInt64
            if try file.magic(at: 0x100, cancelled: cancelled) == "HEAD" { prefix = 0 }
            else if try file.magic(at: 0x1100, cancelled: cancelled) == "HEAD" { prefix = 0x1000 }
            else { throw SwitchPackageError.unsupported }
            let header = try file.read(at: prefix, count: 0x200, cancelled: cancelled)
            let rootOffset = try add(prefix, header.u64(0x130))
            let rootHashSize = header.u64(0x138)
            guard rootOffset >= prefix + 0x200, rootOffset <= file.size else { throw SwitchPackageError.invalid }
            let root = try file.directory(at: rootOffset, length: file.size - rootOffset, hashed: true,
                                          allowFinalSecurePaddingTrim: true, cancelled: cancelled)
            guard rootHashSize >= root.headerSize else { throw SwitchPackageError.invalid }
            try file.verify(at: rootOffset, count: rootHashSize, expected: header.subdata(in: 0x140..<0x160), cancelled: cancelled)
            guard let secure = root.entries.first(where: { $0.name == "secure" }) else { throw SwitchPackageError.invalid }
            // Trimmed cartridge dumps may omit trailing padding in the final secure
            // partition. Never synthesize bytes: all secure entries and declared hash
            // regions must fit the actual file, including the last content byte.
            let availableSecureSize = min(secure.size, file.size - secure.offset)
            let content = try file.directory(at: secure.offset, length: availableSecureSize, hashed: true, cancelled: cancelled)
            guard secure.hashSize >= content.headerSize else { throw SwitchPackageError.invalid }
            _ = try summary(.xci, content)
            try file.verifyPending(cancelled: cancelled, progress: progress)
            return (content.entries.contains { $0.name.lowercased().hasSuffix(".ncz") } ? .xcz : .xci, content)
        } catch is CancellationError { throw CancellationError() }
        catch let error as SwitchPackageError { throw error }
        catch { throw SwitchPackageError.unreadable }
    }

    private static func summary(_ format: SwitchPackageFormat, _ directory: Directory) throws -> SwitchPackageSummary {
        var bytes: UInt64 = 0
        for entry in directory.entries { bytes = try add(bytes, entry.size) }
        guard bytes > 0 else { throw SwitchPackageError.invalid }
        return SwitchPackageSummary(format: format, entryCount: directory.entries.count, contentBytes: bytes)
    }

    private static func add(_ a: UInt64, _ b: UInt64) throws -> UInt64 {
        let (value, overflow) = a.addingReportingOverflow(b)
        guard !overflow else { throw SwitchPackageError.invalid }
        return value
    }

    private static func checkCancellation(_ cancelled: () -> Bool) throws {
        if cancelled() { throw CancellationError() }
    }

    private struct Entry {
        let name: String
        let offset: UInt64
        let size: UInt64
        let hashSize: UInt64
        let digest: Data
    }

    private struct Directory {
        let entries: [Entry]
        let headerSize: UInt64
    }

    private final class Reader {
        let stream: SwitchPackageStream
        var size: UInt64 { stream.size }
        var hashedBytes: UInt64 = 0
        private var pending: [(UInt64, UInt64, Data)] = []

        init(_ stream: SwitchPackageStream) { self.stream = stream }

        func read(at offset: UInt64, count: UInt64, cancelled: () -> Bool) throws -> Data {
            guard count <= metadataLimit else { throw SwitchPackageError.limit }
            return try stream.read(at: offset, count: count, cancelled: cancelled)
        }

        func magic(at offset: UInt64, cancelled: () -> Bool) throws -> String? {
            guard offset <= size, size - offset >= 4 else { return nil }
            return String(data: try read(at: offset, count: 4, cancelled: cancelled), encoding: .ascii)
        }

        func verify(at offset: UInt64, count: UInt64, expected: Data, cancelled: () -> Bool) throws {
            guard count <= metadataLimit, count <= hashBudget - hashedBytes else { throw SwitchPackageError.limit }
            guard offset <= size, count <= size - offset else { throw SwitchPackageError.invalid }
            hashedBytes += count
            pending.append((offset, count, expected))
        }

        // Parse bounded directories first, then verify every declared region before
        // exposing entries. Knowing the work list makes byte progress exact.
        func verifyPending(cancelled: () -> Bool, progress: SwitchByteProgress) throws {
            var completed: UInt64 = 0
            progress(0, hashedBytes)
            for (offset, count, expected) in pending {
                var hash = SHA256(), position: UInt64 = 0
                repeat {
                    try checkCancellation(cancelled)
                    let length = min(UInt64(4 * 1024 * 1024), count - position)
                    hash.update(data: try read(at: offset + position, count: length, cancelled: cancelled))
                    position += length; completed += length
                    progress(completed, hashedBytes)
                } while position < count
                guard Data(hash.finalize()) == expected else { throw SwitchPackageError.hashMismatch }
            }
        }

        func directory(at offset: UInt64, length: UInt64, hashed: Bool,
                       allowFinalSecurePaddingTrim: Bool = false, cancelled: () -> Bool) throws -> Directory {
            guard offset <= size, length <= size - offset, length >= 16 else { throw SwitchPackageError.invalid }
            let header = try read(at: offset, count: 16, cancelled: cancelled)
            guard String(data: header.prefix(4), encoding: .ascii) == (hashed ? "HFS0" : "PFS0"), header.u32(12) == 0 else {
                throw SwitchPackageError.invalid
            }
            let count = UInt64(header.u32(4)), stringsSize = UInt64(header.u32(8))
            guard count > 0 else { throw SwitchPackageError.invalid }
            guard count <= 8192, stringsSize <= metadataLimit else { throw SwitchPackageError.limit }
            let stride: UInt64 = hashed ? 64 : 24
            let tableSize = count * stride
            let headerSize = 16 + tableSize + stringsSize
            guard headerSize <= metadataLimit else { throw SwitchPackageError.limit }
            guard headerSize <= length else { throw SwitchPackageError.invalid }
            let table = try read(at: offset + 16, count: tableSize, cancelled: cancelled)
            let strings = try read(at: offset + 16 + tableSize, count: stringsSize, cancelled: cancelled)
            let dataSize = length - headerSize
            var names = Set<String>()
            var entries: [Entry] = []
            for index in 0..<Int(count) {
                try checkCancellation(cancelled)
                let start = index * Int(stride)
                let relative = table.u64(start), size = table.u64(start + 8)
                let nameOffset = Int(table.u32(start + 16))
                guard relative <= dataSize, nameOffset < strings.count else { throw SwitchPackageError.invalid }
                let nameLimit = min(strings.count, nameOffset + 1025)
                guard let end = strings[nameOffset..<nameLimit].firstIndex(of: 0), end > nameOffset,
                      let name = String(data: strings[nameOffset..<end], encoding: .utf8),
                      !name.contains("/"), !name.contains("\\"), name != ".", name != "..",
                      !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }),
                      names.insert(name.lowercased()).inserted else { throw SwitchPackageError.invalid }
                guard size <= dataSize - relative || (allowFinalSecurePaddingTrim && name == "secure") else {
                    throw SwitchPackageError.invalid
                }
                let hashSize = hashed ? UInt64(table.u32(start + 20)) : 0
                // The 8-byte HFS0 field at 0x18 is reserved, not a hash offset.
                guard hashed ? table.u64(start + 24) == 0 : table.u32(start + 20) == 0 else { throw SwitchPackageError.invalid }
                guard hashSize <= size, !hashed || size == 0 || hashSize > 0 else { throw SwitchPackageError.invalid }
                let digest = hashed ? table.subdata(in: (start + 32)..<(start + 64)) : Data()
                let absolute = offset + headerSize + relative
                _ = try add(absolute, size)
                entries.append(Entry(name: name, offset: absolute, size: size, hashSize: hashSize, digest: digest))
            }
            let nonempty = entries.filter { $0.size > 0 }.sorted { $0.offset < $1.offset }
            for index in 1..<max(1, nonempty.count) {
                guard nonempty[index].offset >= nonempty[index - 1].offset + nonempty[index - 1].size else { throw SwitchPackageError.invalid }
            }
            for entry in entries where entry.size > self.size - entry.offset {
                guard allowFinalSecurePaddingTrim, entry.name == "secure", nonempty.last?.name == "secure" else {
                    throw SwitchPackageError.invalid
                }
            }
            if hashed {
                for entry in entries { try verify(at: entry.offset, count: entry.hashSize, expected: entry.digest, cancelled: cancelled) }
            }
            return Directory(entries: entries, headerSize: headerSize)
        }
    }
}

private extension Data {
    func u32(_ offset: Int) -> UInt32 {
        (0..<4).reduce(0) { $0 | UInt32(self[offset + $1]) << (8 * $1) }
    }
    func u64(_ offset: Int) -> UInt64 {
        (0..<8).reduce(0) { $0 | UInt64(self[offset + $1]) << (8 * $1) }
    }
}

// A bounded random-access view over a dump. Parts stay separate until bytes are
// copied directly into the final library file. No joined staging file is created.
struct SwitchDumpSelection {
    let urls: [URL]

    static func part(_ url: URL) -> (family: String, index: Int)? {
        let name = url.lastPathComponent
        if name.count >= 2, name.count <= 4, name.allSatisfy({ $0.isASCII && $0.isNumber }), let index = Int(name) {
            return ("numeric", index)
        }
        let ext = url.pathExtension.lowercased()
        // Game.nsz.00, Game.nsz.01: a numbered suffix after the game's own extension.
        if (2...4).contains(ext.count), ext.allSatisfy({ $0.isASCII && $0.isNumber }), let index = Int(ext) {
            let base = url.deletingPathExtension()
            let format = base.pathExtension.lowercased()
            guard ["xci", "nsp", "xcz", "nsz"].contains(format) else { return nil }
            return (base.deletingPathExtension().lastPathComponent + "." + format, index)
        }
        guard ext.hasPrefix("xc") || ext.hasPrefix("ns") else { return nil }
        let digits = ext.dropFirst(2)
        guard !digits.isEmpty, digits.count <= 4, digits.allSatisfy({ $0.isASCII && $0.isNumber }), let index = Int(digits) else { return nil }
        return (url.deletingPathExtension().lastPathComponent + "." + ext.prefix(2), index)
    }

    static func groups(_ urls: [URL]) -> [SwitchDumpSelection] {
        var groups: [[URL]] = [], positions: [String: Int] = [:]
        for url in urls {
            let key = part(url).map { url.deletingLastPathComponent().absoluteString + "|" + $0.family } ?? url.absoluteString
            if let position = positions[key] {
                if !groups[position].contains(url) { groups[position].append(url) }
            } else { positions[key] = groups.count; groups.append([url]) }
        }
        return groups.map { SwitchDumpSelection(urls: $0) }
    }

    // Synchronous facade for the background import queue. The accessor runs on
    // a separate operation queue; security scopes remain owned by the caller.
    func coordinated<T>(using coordinator: NSFileCoordinator, body: @escaping ([URL]) throws -> T) throws -> T {
        func access<U>(_ urls: [URL], _ operation: @escaping ([URL]) throws -> U) throws -> U {
            let intents = urls.map { NSFileAccessIntent.readingIntent(with: $0, options: .withoutChanges) }
            let state = SwitchCoordinatedResult(operation)
            let finished = DispatchSemaphore(value: 0)
            coordinator.coordinate(with: intents, queue: OperationQueue()) { error in
                defer { finished.signal() }
                state.value = Result {
                    if let error { throw error }
                    return try state.operation(intents.map { $0.url })
                }
            }
            finished.wait()
            guard let result = state.value else { throw SwitchPackageError.unreadable }
            return try result.get()
        }
        let parts = try access(urls) { try SwitchDumpSelection(urls: $0).resolved() }
        return try access(parts, body)
    }

    var name: String {
        guard let url = urls.first else { return "Game" }
        if Self.part(url)?.family == "numeric" { return url.deletingLastPathComponent().lastPathComponent }
        let name = url.deletingPathExtension()
        if Self.part(url) != nil, ["xci", "nsp", "xcz", "nsz"].contains(name.pathExtension.lowercased()) {
            return name.deletingPathExtension().lastPathComponent
        }
        return name.lastPathComponent
    }

    // Called while the selected folder or files are coordinated and scoped.
    func resolved() throws -> [URL] {
        guard let first = urls.first else { throw SwitchPackageError.invalid }
        let attributes = try FileManager.default.attributesOfItem(atPath: first.path)
        var parts = urls
        if attributes[.type] as? FileAttributeType == .typeDirectory {
            guard urls.count == 1 else { throw SwitchPackageError.invalid }
            parts = try FileManager.default.contentsOfDirectory(at: first, includingPropertiesForKeys: nil)
                .filter { !$0.lastPathComponent.hasPrefix(".") }
            guard !parts.isEmpty, parts.allSatisfy({ Self.part($0) != nil }) else {
                throw SwitchSplitError(message: "Choose a folder containing only one split dump, with parts 00, 01, matching xc0/xc1 or ns0/ns1 files, or Game.nsp.00, Game.nsp.01.")
            }
        } else if urls.count == 1, Self.part(first) == nil { return urls }
        let numbered = try parts.map { url -> (URL, String, Int) in
            guard let part = Self.part(url) else { throw SwitchPackageError.invalid }
            return (url, part.family, part.index)
        }.sorted { $0.2 < $1.2 }
        guard numbered.count <= 4096, let family = numbered.first?.1,
              numbered.enumerated().allSatisfy({ $0.element.1 == family && $0.element.2 == $0.offset }) else {
            throw SwitchSplitError(message: "Split parts must start at 00, xc0, ns0 or .00 and have no gaps or duplicate numbers. Select every part of one dump, or its folder.")
        }
        return numbered.map { $0.0 }
    }
}

struct SwitchSplitError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

final class SwitchPackageStream {
    private struct Part {
        let url: URL
        let start: UInt64
        let size: UInt64
        let modified: Date?
    }
    private var parts: [Part] = []
    private var handle: FileHandle?
    private var active = -1
    private(set) var size: UInt64 = 0

    // URLs are already ordered by the selection resolver. Open one part at a
    // time so FAT32 dumps do not consume one descriptor per chunk.
    init(urls: [URL]) throws {
        guard !urls.isEmpty, urls.count <= 4096 else { throw SwitchPackageError.limit }
        for url in urls {
            let attrs = try FileManager.default.attributesOfItem(atPath: url.path)
            guard attrs[.type] as? FileAttributeType == .typeRegular,
                  let bytes = attrs[.size] as? NSNumber, bytes.uint64Value > 0 || urls.count == 1 else { throw SwitchPackageError.invalid }
            let bytesCount = bytes.uint64Value
            guard bytesCount <= 128 * 1024 * 1024 * 1024 - size else { throw SwitchPackageError.limit }
            parts.append(Part(url: url, start: size, size: bytesCount, modified: attrs[.modificationDate] as? Date))
            size += bytesCount
        }
    }
    deinit { try? handle?.close() }

    func checkUnchanged() throws {
        for part in parts {
            let attrs = try FileManager.default.attributesOfItem(atPath: part.url.path)
            guard attrs[.type] as? FileAttributeType == .typeRegular,
                  (attrs[.size] as? NSNumber)?.uint64Value == part.size,
                  attrs[.modificationDate] as? Date == part.modified else {
                throw SwitchSplitError(message: "The selected file changed size or contents during import. Try again when its upload is complete.")
            }
        }
    }

    func read(at offset: UInt64, count: UInt64, cancelled: () -> Bool = { false }) throws -> Data {
        if cancelled() { throw CancellationError() }
        guard count <= 16 * 1024 * 1024, offset <= size, count <= size - offset else { throw SwitchPackageError.invalid }
        var result = Data(), position = offset
        while UInt64(result.count) < count {
            if cancelled() { throw CancellationError() }
            guard let index = parts.firstIndex(where: { position >= $0.start && position - $0.start < $0.size }) else { throw SwitchPackageError.invalid }
            let part = parts[index]
            if active != index {
                try handle?.close(); handle = nil; active = -1
                handle = try FileHandle(forReadingFrom: part.url); active = index
            }
            guard let handle else { throw SwitchPackageError.unreadable }
            try handle.seek(toOffset: position - part.start)
            let amount = min(256 * 1024, count - UInt64(result.count), part.size - (position - part.start))
            guard let bytes = try handle.read(upToCount: Int(amount)), !bytes.isEmpty else { throw SwitchPackageError.invalid }
            result.append(bytes); position += UInt64(bytes.count)
        }
        return result
    }
}

private final class SwitchCoordinatedResult<Value>: @unchecked Sendable {
    // Written by the accessor, then read only after its semaphore signal.
    let operation: ([URL]) throws -> Value
    init(_ operation: @escaping ([URL]) throws -> Value) { self.operation = operation }
    var value: Result<Value, Error>?
}
