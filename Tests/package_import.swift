import CryptoKit
import Foundation

@main struct PackageImportTests {
    static func check(_ condition: @autoclosure () throws -> Bool) rethrows {
        let value = try condition(); precondition(value)
    }
    static func fixture(_ url: URL, payloadBytes: UInt64) throws {
        var bytes = Data("PFS0".utf8)
        func integer(_ value: UInt64, _ count: Int) {
            for shift in 0..<count { bytes.append(UInt8(truncatingIfNeeded: value >> (shift * 8))) }
        }
        let name = Data("00000000000000000000000000000000.nca\0".utf8)
        integer(1, 4); integer(UInt64(name.count), 4); integer(0, 4)
        integer(0, 8); integer(payloadBytes, 8); integer(0, 4); integer(0, 4)
        bytes.append(name)
        try bytes.write(to: url)
        let file = try FileHandle(forWritingTo: url)
        defer { try? file.close() }
        try file.truncate(atOffset: UInt64(bytes.count) + payloadBytes)
    }

    static func main() throws {
        if CommandLine.arguments.count == 3, CommandLine.arguments[1] == "--fixture" {
            let url = URL(fileURLWithPath: CommandLine.arguments[2])
            guard !FileManager.default.fileExists(atPath: url.path) else { fatalError("Fixture destination already exists") }
            try fixture(url, payloadBytes: 1024)
            return
        }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("satchel-import-tests-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let library = root.appendingPathComponent("library")
        try FileManager.default.createDirectory(at: library, withIntermediateDirectories: true)
        let source = root.appendingPathComponent("synthetic.nsp")
        try fixture(source, payloadBytes: 256 * 1024 * 1024)
        let originalSize = try FileManager.default.attributesOfItem(atPath: source.path)[.size] as! NSNumber
        var events = 0
        var previous: UInt64 = 0
        let result = try SwitchPackageImport.copy(from: source, into: library, cancelled: { false }) { copied, total in
            precondition(copied >= previous && copied <= total)
            precondition(copied - previous <= 1024 * 1024)
            previous = copied; events += 1
        }
        precondition(result.summary.format == .nsp && result.summary.entryCount == 1)
        precondition(result.bytes == originalSize.uint64Value && previous == result.bytes && events > 256)
        let saved = library.appendingPathComponent(result.id + ".nsp")
        // Independently hash the saved file in different chunk boundaries.
        let file = try FileHandle(forReadingFrom: saved)
        var hash = SHA256()
        while let data = try file.read(upToCount: 131071), !data.isEmpty { hash.update(data: data) }
        try file.close()
        precondition(hash.finalize().map { String(format: "%02x", $0) }.joined() == result.id)
        print("PASS: 256 MiB streaming import, bounded chunks, monotonic progress, independent saved-content hash")

        try Data("altered cache".utf8).write(to: saved)
        let repaired = try SwitchPackageImport.copy(from: source, into: library, cancelled: { false }, progress: { _, _ in })
        precondition(repaired.id == result.id)
        try check(try SwitchPackageReader.inspect(saved).entryCount == 1)
        try check(try FileManager.default.contentsOfDirectory(atPath: library.path).count == 1)
        print("PASS: reimport repairs an altered local copy and deduplicates by content")

        var cancel = false
        do {
            _ = try SwitchPackageImport.copy(from: source, into: library, cancelled: { cancel }) { copied, _ in
                if copied >= 3 * 1024 * 1024 { cancel = true }
            }
            fatalError("Cancelled import succeeded")
        } catch is CancellationError { }
        try check(try FileManager.default.contentsOfDirectory(atPath: library.path).count == 1)
        try check(try FileManager.default.attributesOfItem(atPath: source.path)[.size] as! NSNumber == originalSize)
        try check(try SwitchPackageReader.inspect(saved).entryCount == 1)
        print("PASS: mid-copy cancellation removes only its partial file and preserves source/library")

        let growing = root.appendingPathComponent("changing.nsp")
        try fixture(growing, payloadBytes: 2 * 1024 * 1024)
        var changed = false
        do {
            _ = try SwitchPackageImport.copy(from: growing, into: library, cancelled: { false }) { copied, _ in
                if copied > 0 && !changed {
                    changed = true
                    let handle = try! FileHandle(forWritingTo: growing)
                    _ = try! handle.seekToEnd(); try! handle.write(contentsOf: Data([1])); try! handle.close()
                }
            }
            fatalError("Growing source was accepted")
        } catch let error as SwitchImportFailure {
            precondition(error.message.contains("changed size"))
        }
        try check(try FileManager.default.contentsOfDirectory(atPath: library.path).count == 1)
        print("PASS: changing source is rejected without publishing an incomplete package")

        let interrupted = library.appendingPathComponent(".import-" + UUID().uuidString)
        try Data([1, 2, 3]).write(to: interrupted)
        let unrelated = library.appendingPathComponent(".import-not-our-file")
        try Data([4]).write(to: unrelated)
        let symlink = library.appendingPathComponent(".import-" + UUID().uuidString)
        try FileManager.default.createSymbolicLink(at: symlink, withDestinationURL: source)
        try SwitchPackageImport.removeInterruptedCopies(in: library)
        precondition(!FileManager.default.fileExists(atPath: interrupted.path))
        precondition(FileManager.default.fileExists(atPath: unrelated.path))
        precondition(FileManager.default.fileExists(atPath: symlink.path))
        try check(try SwitchPackageReader.inspect(saved).entryCount == 1)
        try check(try FileManager.default.attributesOfItem(atPath: source.path)[.size] as! NSNumber == originalSize)
        print("PASS: fresh-start cleanup removes owned partial files only, preserving games, unrelated files and symlink targets")
    }
}
