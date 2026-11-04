import CryptoKit
import Foundation

@main struct SplitDumpTests {
    static func put(_ value: UInt64, in bytes: inout Data, at offset: Int, width: Int) {
        for i in 0..<width { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
    }

    static func directory(_ names: [String], payloads: [Data], hashed: Bool) -> Data {
        let stride = hashed ? 64 : 24
        var strings = Data(), offsets: [Int] = []
        for name in names { offsets.append(strings.count); strings.append(contentsOf: name.utf8); strings.append(0) }
        var bytes = Data(repeating: 0, count: 16 + names.count * stride)
        bytes.replaceSubrange(0..<4, with: Data((hashed ? "HFS0" : "PFS0").utf8))
        put(UInt64(names.count), in: &bytes, at: 4, width: 4)
        put(UInt64(strings.count), in: &bytes, at: 8, width: 4)
        var offset = 0
        for i in names.indices {
            let entry = 16 + stride * i
            put(UInt64(offset), in: &bytes, at: entry, width: 8)
            put(UInt64(payloads[i].count), in: &bytes, at: entry + 8, width: 8)
            put(UInt64(offsets[i]), in: &bytes, at: entry + 16, width: 4)
            if hashed {
                put(UInt64(payloads[i].count), in: &bytes, at: entry + 20, width: 4)
                bytes.replaceSubrange((entry + 32)..<(entry + 64), with: Data(SHA256.hash(data: payloads[i])))
            }
            offset += payloads[i].count
        }
        bytes.append(strings)
        for payload in payloads { bytes.append(payload) }
        return bytes
    }

    static func xci(_ secure: Data, prefix: Int = 0, partition: String = "secure") -> Data {
        let root = directory([partition], payloads: [secure], hashed: true)
        let rootHeaderSize = root.count - secure.count
        var bytes = Data(repeating: 0, count: prefix + 0xf000)
        bytes.replaceSubrange((prefix + 0x100)..<(prefix + 0x104), with: Data("HEAD".utf8))
        put(0xf000, in: &bytes, at: prefix + 0x130, width: 8)
        put(UInt64(rootHeaderSize), in: &bytes, at: prefix + 0x138, width: 8)
        bytes.replaceSubrange((prefix + 0x140)..<(prefix + 0x160), with: Data(SHA256.hash(data: root.prefix(rootHeaderSize))))
        bytes.append(root)
        return bytes
    }

    static func check(_ condition: @autoclosure () throws -> Bool) rethrows {
        let passed = try condition(); precondition(passed)
    }
    static func reject(_ body: () throws -> Void) {
        do { try body(); fatalError("Invalid split accepted") } catch { }
    }
    static func real() throws {
        let source = URL(fileURLWithPath: CommandLine.arguments[2])
        let folder = URL(fileURLWithPath: CommandLine.arguments[3])
        let destination = URL(fileURLWithPath: CommandLine.arguments[4])
        let shouldImport = CommandLine.arguments[5] == "import"
        let selection = SwitchDumpSelection(urls: [folder])
        let urls = try selection.resolved()
        try check(try SwitchDumpSelection(urls: urls.reversed()).resolved() == urls)
        let stream = try SwitchPackageStream(urls: urls)
        let expected = try SwitchPackageReader.inspect(source)
        let actual = try SwitchPackageReader.inspect(stream)
        precondition(expected.format == actual.format && expected.entryCount == actual.entryCount && expected.contentBytes == actual.contentBytes)
        let original = try FileHandle(forReadingFrom: source)
        defer { try? original.close() }
        var position: UInt64 = 0, digest = SHA256()
        while position < stream.size {
            try autoreleasepool {
                let bytes = try stream.read(at: position, count: min(1048573, stream.size - position))
                let reference = try original.read(upToCount: bytes.count)
                precondition(reference == bytes)
                digest.update(data: bytes); position += UInt64(bytes.count)
            }
        }
        try check(try (original.read(upToCount: 1) ?? Data()).isEmpty)
        try stream.checkUnchanged()
        if shouldImport {
            try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
            let expectedID = digest.finalize().map { String(format: "%02x", $0) }.joined()
            let imported = try selection.coordinated(using: NSFileCoordinator()) { parts in
                try SwitchPackageImport.copy(parts: parts, into: destination, cancelled: { false }, progress: { _, _ in })
            }
            precondition(imported.id == expectedID && imported.bytes == stream.size)
            try check(try FileManager.default.contentsOfDirectory(atPath: destination.path).count == 1)
            let saved = destination.appendingPathComponent(imported.id + "." + imported.summary.format.rawValue)
            try check(try SwitchPackageReader.inspect(saved).entryCount == expected.entryCount)
        }
        print("PASS: real split container parity, every byte equal, reversed selection and folder resolution" + (shouldImport ? ", one verified library copy" : ", stream only"))
    }

    static func main() throws {
        if CommandLine.arguments.count == 6, CommandLine.arguments[1] == "real" { try real(); return }
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let scratch = root.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: scratch) }
        let library = scratch.appendingPathComponent("library")
        try FileManager.default.createDirectory(at: library, withIntermediateDirectories: true)
        let payload = Data((0..<200003).map { UInt8(truncatingIfNeeded: $0) })
        for (kind, bytes) in [("nsp", directory(["synthetic.nca"], payloads: [payload], hashed: false)),
                              ("xci", xci(directory(["synthetic.nca"], payloads: [payload], hashed: true)))] {
            let whole = scratch.appendingPathComponent("whole." + kind)
            try bytes.write(to: whole)
            let baseline = try SwitchPackageImport.copy(from: whole, into: library, cancelled: { false }, progress: { _, _ in })
            for style in ["numeric", "xc", "ns", "suffix"] {
                let folder = scratch.appendingPathComponent(kind + style)
                try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                // Split inside magic, integer, table, strings, XCI hash, payload.
                let cuts = [0, 2, 9, 25, 45, 0x142, 0xf002, 0xf065, 70001, bytes.count]
                var parts: [URL] = []
                for i in 0..<cuts.count - 1 {
                    let name = style == "numeric" ? String(format: "%02d", i)
                        : style == "suffix" ? "Game." + kind + String(format: ".%02d", i) : "Game." + style + String(i)
                    let url = folder.appendingPathComponent(name)
                    try bytes.subdata(in: cuts[i]..<cuts[i + 1]).write(to: url); parts.append(url)
                }
                let selection = SwitchDumpSelection(urls: parts.reversed())
                try check(try selection.resolved() == parts)
                try check(try SwitchDumpSelection(urls: [folder]).resolved() == parts)
                precondition(SwitchDumpSelection.groups(parts + parts).count == 1)
                let stream = try SwitchPackageStream(urls: parts)
                try check(try stream.read(at: 0, count: UInt64(bytes.count)) == bytes)
                for cut in cuts.dropFirst().dropLast() {
                    try check(try stream.read(at: UInt64(cut - 1), count: 3) == bytes.subdata(in: cut - 1..<cut + 2))
                }
                try check(try SwitchPackageReader.inspect(stream).format == baseline.summary.format)
                let imported = try selection.coordinated(using: NSFileCoordinator()) { urls in
                    try SwitchPackageImport.copy(parts: urls, into: library, cancelled: { false }, progress: { _, _ in })
                }
                precondition(imported.id == baseline.id && imported.bytes == baseline.bytes)
                let folderID = try SwitchDumpSelection(urls: [folder]).coordinated(using: NSFileCoordinator()) { urls in
                    try SwitchPackageImport.copy(parts: urls, into: library, cancelled: { false }, progress: { _, _ in }).id
                }
                precondition(folderID == baseline.id)
                reject { _ = try SwitchDumpSelection(urls: Array(parts.dropFirst())).resolved() }
                reject { _ = try SwitchDumpSelection(urls: parts.filter { $0 != parts[3] }).resolved() }
                reject { _ = try SwitchDumpSelection(urls: parts + [parts[0]]).resolved() }
                reject { _ = try stream.read(at: UInt64(bytes.count), count: 1) }
                do { _ = try stream.read(at: 0, count: 2, cancelled: { true }); fatalError("Cancellation ignored") }
                catch is CancellationError { }
                var cancel = false
                do {
                    _ = try SwitchPackageImport.copy(parts: parts, into: library, cancelled: { cancel }) { _, _ in cancel = true }
                    fatalError("Copy cancellation ignored")
                } catch is CancellationError { }
                // Truncate the final part: declared content extents must fail.
                let last = try FileHandle(forWritingTo: parts.last!)
                try last.truncate(atOffset: 1); try last.close()
                reject { _ = try SwitchPackageReader.inspect(SwitchPackageStream(urls: parts)) }
                reject { try stream.checkUnchanged() }
            }
        }
        try check(try FileManager.default.contentsOfDirectory(atPath: library.path).count == 2)
        let mixed = [scratch.appendingPathComponent("a.xc0"), scratch.appendingPathComponent("b.xc0")]
        precondition(SwitchDumpSelection.groups(mixed).count == 2)
        let link = scratch.appendingPathComponent("link")
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: scratch.appendingPathComponent("whole.xci"))
        reject { _ = try SwitchPackageStream(urls: [link]) }
        print("PASS: 8 split layouts, coordinated folders and parts, cross-boundary reads and hashes, identity, gaps, duplicate numbers, truncation, cancellation, cleanup and symlink rejection")
    }
}
