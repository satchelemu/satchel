import Foundation
import CryptoKit

struct SaveFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

/// A deliberately narrow ZIP dialect: UTF-8 names, stored entries, no links,
/// encryption, descriptors, extras or ZIP64. Files can open exported archives.
/// Import accepts Satchel exports only, with a separately validated manifest.
enum SaveArchive {
    static let maxBytes = 512 * 1024 * 1024
    static let maxArchiveBytes = maxBytes + 8 * 1024 * 1024
    static let maxEntries = 10_000
    static let chunkSize = 1024 * 1024

    struct Member {
        let name: String
        let offset: Int
        let size: Int
        let crc: UInt32
    }

    private static let crcTable: [UInt32] = (0..<256).map { value in
        var crc = UInt32(value)
        for _ in 0..<8 { crc = (crc >> 1) ^ (crc & 1 == 1 ? 0xedb88320 : 0) }
        return crc
    }

    static func crc(_ data: Data, seed: UInt32 = 0xffffffff) -> UInt32 {
        data.withUnsafeBytes { bytes in
            var value = seed
            for byte in bytes { value = crcTable[Int((value ^ UInt32(byte)) & 255)] ^ (value >> 8) }
            return value
        }
    }

    static func digest(_ url: URL) throws -> (size: Int, hash: String, crc: UInt32) {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var hash = SHA256(), checksum: UInt32 = 0xffffffff, size = 0
        while let chunk = try handle.read(upToCount: chunkSize), !chunk.isEmpty {
            try Task.checkCancellation()
            size += chunk.count
            guard size <= maxBytes else { throw SaveFailure(message: "This save exceeds the 512 MiB limit.") }
            hash.update(data: chunk); checksum = crc(chunk, seed: checksum)
        }
        return (size, hash.finalize().map { String(format: "%02x", $0) }.joined(), checksum ^ 0xffffffff)
    }

    final class Writer {
        private let file: FileHandle
        private var members: [Member] = []
        private var position = 0
        init(url: URL) throws {
            guard FileManager.default.createFile(atPath: url.path, contents: nil) else {
                throw SaveFailure(message: "Could not create the save archive.")
            }
            file = try FileHandle(forWritingTo: url)
        }
        deinit { try? file.close() }
        private func write(_ data: Data) throws {
            guard position + data.count <= maxArchiveBytes else { throw SaveFailure(message: "The save archive is too large.") }
            try file.write(contentsOf: data); position += data.count
        }
        func add(name: String, size: Int, crc: UInt32, source: () throws -> Data?) throws {
            guard members.count <= maxEntries, name.utf8.count <= 1024 else { throw SaveFailure(message: "Too many save entries or an oversized path.") }
            let member = Member(name: name, offset: position, size: size, crc: crc)
            var header = Data()
            header.appendLE(0x04034b50, 4); header.appendLE(20, 2); header.appendLE(0x800, 2)
            header.appendLE(0, 2); header.appendLE(0, 2); header.appendLE(33, 2)
            header.appendLE(Int(crc), 4); header.appendLE(size, 4); header.appendLE(size, 4)
            header.appendLE(name.utf8.count, 2); header.appendLE(0, 2); header.append(contentsOf: name.utf8)
            try write(header)
            var count = 0, checksum: UInt32 = 0xffffffff
            while let data = try source(), !data.isEmpty {
                try Task.checkCancellation()
                count += data.count
                guard count <= size else { throw SaveFailure(message: "The save changed while being archived.") }
                checksum = SaveArchive.crc(data, seed: checksum); try write(data)
            }
            guard count == size, checksum ^ 0xffffffff == crc else { throw SaveFailure(message: "The save changed while being archived.") }
            members.append(member)
        }
        func add(name: String, data: Data) throws {
            var pending: Data? = data
            try add(name: name, size: data.count, crc: SaveArchive.crc(data) ^ 0xffffffff) {
                defer { pending = nil }; return pending
            }
        }
        func finish() throws {
            let start = position
            for member in members {
                var row = Data()
                row.appendLE(0x02014b50, 4); row.appendLE(20, 2); row.appendLE(20, 2); row.appendLE(0x800, 2)
                row.appendLE(0, 2); row.appendLE(0, 2); row.appendLE(33, 2); row.appendLE(Int(member.crc), 4)
                row.appendLE(member.size, 4); row.appendLE(member.size, 4); row.appendLE(member.name.utf8.count, 2)
                row.appendLE(0, 2); row.appendLE(0, 2); row.appendLE(0, 2); row.appendLE(0, 2); row.appendLE(0, 4)
                row.appendLE(member.offset, 4); row.append(contentsOf: member.name.utf8)
                try write(row)
            }
            var end = Data()
            end.appendLE(0x06054b50, 4); end.appendLE(0, 2); end.appendLE(0, 2)
            end.appendLE(members.count, 2); end.appendLE(members.count, 2)
            end.appendLE(position - start, 4); end.appendLE(start, 4); end.appendLE(0, 2)
            try write(end); try file.synchronize(); try file.close()
        }
    }

    final class Reader {
        private let file: FileHandle
        let members: [Member]
        init(url: URL) throws {
            let handle = try FileHandle(forReadingFrom: url)
            file = handle
            let length = try handle.seekToEnd()
            guard length >= 22, length <= maxArchiveBytes else { throw SaveFailure(message: "The ZIP is truncated or too large.") }
            func read(_ offset: Int, _ count: Int) throws -> Data {
                guard offset >= 0, count >= 0, offset + count <= length else { throw Self.invalid }
                try handle.seek(toOffset: UInt64(offset))
                guard let data = try handle.read(upToCount: count), data.count == count else { throw Self.invalid }
                return data
            }
            let end = try read(Int(length) - 22, 22)
            guard end.uint(0, 4) == 0x06054b50, end.uint(4, 2) == 0, end.uint(6, 2) == 0,
                  end.uint(8, 2) == end.uint(10, 2), end.uint(20, 2) == 0,
                  end.uint(10, 2) > 0, end.uint(10, 2) <= maxEntries + 1 else { throw Self.invalid }
            let directory = end.uint(16, 4), directorySize = end.uint(12, 4)
            guard directory + directorySize == Int(length) - 22 else { throw Self.invalid }
            var cursor = directory, localEnd = 0, total = 0, result: [Member] = [], names = Set<String>()
            for _ in 0..<end.uint(10, 2) {
                let row = try read(cursor, 46)
                guard row.uint(0, 4) == 0x02014b50, row.uint(4, 2) == 20, row.uint(6, 2) == 20,
                      row.uint(8, 2) == 0x800, row.uint(10, 2) == 0,
                      row.uint(20, 4) == row.uint(24, 4), row.uint(30, 2) == 0,
                      row.uint(32, 2) == 0, row.uint(34, 2) == 0, row.uint(36, 2) == 0, row.uint(38, 4) == 0,
                      row.uint(42, 4) == localEnd else { throw Self.invalid }
                let count = row.uint(28, 2)
                guard count > 0, count <= 1024 else { throw Self.invalid }
                let nameData = try read(cursor + 46, count)
                guard let name = String(data: nameData, encoding: .utf8), names.insert(name).inserted else { throw Self.invalid }
                let local = try read(localEnd, 30)
                guard local.uint(0, 4) == 0x04034b50,
                      local.subdata(in: 4..<28) == row.subdata(in: 6..<30),
                      local.uint(28, 2) == 0,
                      try read(localEnd + 30, count) == nameData else { throw Self.invalid }
                let size = row.uint(24, 4), offset = localEnd + 30 + count
                total += size
                guard total <= maxArchiveBytes, offset + size <= directory else { throw Self.invalid }
                result.append(Member(name: name, offset: offset, size: size, crc: UInt32(row.uint(16, 4))))
                localEnd = offset + size; cursor += 46 + count
            }
            guard cursor == directory + directorySize, localEnd == directory else { throw Self.invalid }
            members = result
        }
        deinit { try? file.close() }
        private static var invalid: SaveFailure { SaveFailure(message: "This is not a complete Satchel save ZIP. Export it again from Satchel.") }
        func consume(_ member: Member, body: (Data) throws -> Void) throws {
            try file.seek(toOffset: UInt64(member.offset))
            var remaining = member.size, checksum: UInt32 = 0xffffffff
            while remaining > 0 {
                try Task.checkCancellation()
                guard let data = try file.read(upToCount: min(remaining, chunkSize)), !data.isEmpty else { throw Self.invalid }
                remaining -= data.count; checksum = SaveArchive.crc(data, seed: checksum)
                try body(data)
            }
            guard checksum ^ 0xffffffff == member.crc else { throw SaveFailure(message: "The save ZIP checksum failed.") }
        }
        func data(_ member: Member) throws -> Data {
            guard member.size <= 4 * 1024 * 1024 else { throw Self.invalid }
            var data = Data(); try consume(member) { data.append($0) }; return data
        }
    }
}

private extension Data {
    mutating func appendLE(_ value: Int, _ count: Int) {
        for byte in 0..<count { append(UInt8(truncatingIfNeeded: value >> (byte * 8))) }
    }
    func uint(_ offset: Int, _ count: Int) -> Int {
        (0..<count).reduce(0) { $0 | (Int(self[offset + $1]) << ($1 * 8)) }
    }
}
