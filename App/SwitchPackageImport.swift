import CryptoKit
import Darwin
import Foundation

struct SwitchImportFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

struct SwitchPackageImport {
    let id: String
    let bytes: UInt64
    let summary: SwitchPackageSummary

    // The app has one library and calls this before accepting any imports.
    // A process kill cannot execute defer; recover only our uncommitted files.
    static func removeInterruptedCopies(in directory: URL) throws {
        for url in try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil) {
            let name = url.lastPathComponent
            let prefix = ".import-"
            guard name.hasPrefix(prefix), name.count == prefix.count + 36,
                  UUID(uuidString: String(name.dropFirst(prefix.count))) != nil else { continue }
            let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
            guard attributes[.type] as? FileAttributeType == .typeRegular else { continue }
            try FileManager.default.removeItem(at: url)
        }
    }

    // Call within a coordinated, security-scoped read. Memory use is independent
    // of game size; partial files never become library entries.
    static func copy(from source: URL, into directory: URL,
                     cancelled: () -> Bool,
                     progress: (UInt64, UInt64) -> Void) throws -> SwitchPackageImport {
        try copy(parts: [source], into: directory, cancelled: cancelled, progress: progress)
    }

    static func copy(parts sources: [URL], into directory: URL,
                     cancelled: () -> Bool,
                     progress: (UInt64, UInt64) -> Void) throws -> SwitchPackageImport {
        func checkCancellation() throws {
            if cancelled() { throw CancellationError() }
        }
        try checkCancellation()
        let input = try SwitchPackageStream(urls: sources)
        let size = input.size
        // The same container checks run across part boundaries before any copy.
        _ = try SwitchPackageReader.inspect(input, cancelled: cancelled)
        let capacity = try directory.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey, .volumeAvailableCapacityKey])
        let available = capacity.volumeAvailableCapacityForImportantUsage ?? capacity.volumeAvailableCapacity.map(Int64.init)
        guard let available, available >= 0, UInt64(available) >= size + 256 * 1024 * 1024 else {
            throw SwitchImportFailure(message: "Not enough free space for a local copy. Free at least \(ByteCountFormatter.string(fromByteCount: Int64(size + 256 * 1024 * 1024), countStyle: .file)) and try again.")
        }
        let temporary = directory.appendingPathComponent(".import-" + UUID().uuidString)
        guard FileManager.default.createFile(atPath: temporary.path, contents: nil) else {
            throw SwitchImportFailure(message: "Could not create the local import file.")
        }
        defer { try? FileManager.default.removeItem(at: temporary) }
        let output = try FileHandle(forWritingTo: temporary)
        defer { try? output.close() }
        var hash = SHA256()
        var copied: UInt64 = 0
        progress(0, size)
        while copied < size {
            try checkCancellation()
            let count = try autoreleasepool {
                let chunk = try input.read(at: copied, count: min(1024 * 1024, size - copied), cancelled: cancelled)
                guard !chunk.isEmpty else {
                    throw SwitchImportFailure(message: "The selected file ended before its advertised size. Download it fully in Files and try again.")
                }
                try output.write(contentsOf: chunk)
                hash.update(data: chunk)
                return chunk.count
            }
            copied += UInt64(count)
            progress(copied, size)
        }
        do { try input.checkUnchanged() }
        catch let error as SwitchSplitError { throw SwitchImportFailure(message: error.message) }
        try output.synchronize()
        try checkCancellation()
        let summary = try SwitchPackageReader.inspect(temporary, cancelled: cancelled)
        let digest = hash.finalize().map { String(format: "%02x", $0) }.joined()
        let target = directory.appendingPathComponent(digest + "." + summary.format.rawValue)
        try checkCancellation()
        // Same-volume rename atomically publishes the complete copy and repairs
        // an altered cache copy on reimport without touching the source.
        guard rename(temporary.path, target.path) == 0 else {
            throw SwitchImportFailure(message: "Could not finish saving the local game package (error \(errno)).")
        }
        return SwitchPackageImport(id: digest, bytes: copied, summary: summary)
    }
}
