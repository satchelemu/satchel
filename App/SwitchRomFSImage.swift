import CryptoKit
import Darwin
import Foundation

/// NX2Apple DMROMFS1 writer. No extracted directory, no whole-file buffering.
/// The caller provides a stable composed RomFS and owns its source lifetime.
/// Index metadata has explicit limits (one million entries, 64 MiB names).
/// Data reads are at most 1 MiB. Callbacks run synchronously on the caller's queue.
enum SwitchRomFSImage {
    struct Progress {
        enum Stage { case enumerating, copying, publishing }
        let stage: Stage
        let completedBytes: UInt64
        let totalBytes: UInt64
        let completedFiles: Int
        let totalFiles: Int
        var imageCompletedBytes: UInt64 = 0
        var imageTotalBytes: UInt64 = 0
    }
    struct Result {
        let fileCount: Int
        let entryCount: Int
        let dataBytes: UInt64
        let imageBytes: UInt64
        let sha256: Data
    }

    static func write(_ rom: SwitchRomFS, to destination: URL,
                      cancelled: () -> Bool = { false },
                      progress: (Progress) -> Void = { _ in }) throws -> Result {
        func check() throws { if cancelled() { throw CancellationError() } }
        try check()
        progress(Progress(stage: .enumerating, completedBytes: 0, totalBytes: 0, completedFiles: 0, totalFiles: rom.fileCount))
        let entries = try rom.tree(cancelled: cancelled)
        var total: UInt64 = 0, poolSize: UInt64 = 0
        for entry in entries {
            try check()
            if let file = entry.file {
                guard file.size <= UInt64(Int64.max) - total else { throw SwitchExecutableError.limit }
                total += file.size
            }
            poolSize += UInt64(entry.name.count)
        }
        let indexSize = UInt64(entries.count) * 24 + poolSize
        guard total <= UInt64(Int64.max) - indexSize - 32 else { throw SwitchExecutableError.limit }
        let temporary = destination.deletingLastPathComponent().appendingPathComponent(".dmromfs-" + UUID().uuidString + ".part")
        let fd = Darwin.open(temporary.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR)
        guard fd >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        let handle = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        var published = false
        defer {
            try? handle.close()
            if !published { _ = Darwin.unlink(temporary.path) }
        }
        var header = Data("DMROMFS1".utf8) + Data(count: 24)
        header.cpPut(32 + total, at: 8, count: 8); header.cpPut(indexSize, at: 16, count: 8)
        header.cpPut(UInt64(entries.count), at: 24, count: 8)
        var digest = SHA256()
        var completed: UInt64 = 0, files = 0, imageCompleted: UInt64 = 0
        let imageTotal = 32 + total + indexSize
        func write(_ bytes: Data) throws {
            try handle.write(contentsOf: bytes); digest.update(data: bytes)
            imageCompleted += UInt64(bytes.count)
            progress(Progress(stage: .copying, completedBytes: completed, totalBytes: total,
                completedFiles: files, totalFiles: rom.fileCount,
                imageCompletedBytes: imageCompleted, imageTotalBytes: imageTotal))
        }
        try write(header)
        for entry in entries {
            guard let file = entry.file else { continue }
            var position: UInt64 = 0
            while position < file.size {
                try check()
                let count = min(SwitchIntegrityStorage.maximumRead, file.size - position)
                try autoreleasepool {
                    let bytes = try rom.read(file, offset: position, count: count)
                    guard bytes.count == count else { throw SwitchExecutableError.invalid }
                    try write(bytes)
                }
                position += count; completed += count
            }
            files += 1
        }
        var dataOffset: UInt64 = 32, nameOffset: UInt64 = 0
        // Buffer at most 1 MiB of index records or names, independently of the
        // total index size. Drain Foundation's autoreleased I/O objects per write.
        var indexBuffer = Data()
        func flushIndex() throws {
            try autoreleasepool { try write(indexBuffer) }
            indexBuffer.removeAll(keepingCapacity: true)
        }
        for entry in entries {
            try check()
            var record = Data(count: 24)
            if let file = entry.file {
                record.cpPut(dataOffset, at: 0, count: 8); record.cpPut(file.size, at: 8, count: 8)
                record[22] = 1; dataOffset += file.size
            } else {
                record.cpPut(entry.firstChild, at: 0, count: 8); record.cpPut(entry.childCount, at: 8, count: 8)
            }
            record.cpPut(nameOffset, at: 16, count: 4); record.cpPut(UInt64(entry.name.count), at: 20, count: 2)
            indexBuffer.append(record); nameOffset += UInt64(entry.name.count)
            if indexBuffer.count >= 1024 * 1024 - 24 { try flushIndex() }
        }
        try flushIndex()
        for entry in entries {
            try check()
            if indexBuffer.count + entry.name.count > 1024 * 1024 { try flushIndex() }
            indexBuffer.append(entry.name)
        }
        try flushIndex()
        try handle.synchronize()
        progress(Progress(stage: .publishing, completedBytes: completed, totalBytes: total,
                          completedFiles: files, totalFiles: rom.fileCount,
                          imageCompletedBytes: imageCompleted, imageTotalBytes: imageTotal))
        try check()
        guard Darwin.rename(temporary.path, destination.path) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        published = true
        return Result(fileCount: files, entryCount: entries.count, dataBytes: total, imageBytes: 32 + total + indexSize, sha256: Data(digest.finalize()))
    }
}
