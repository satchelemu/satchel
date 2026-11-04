import CryptoKit
import Foundation

/// Independent, bounded parser for the exact no-entitlement signing recipe.
/// CMS is verified against the selected identity after all page and special hashes.
enum PrepareSignatureVerifier {
    static func verify(_ url: URL, identity: SatchelSigningIdentity, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws {
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        func require(_ condition: Bool) throws {
            guard condition else { throw PrepareFailure(message: "Prepared game signature structure or hashes did not verify.") }
        }
        func integer(_ bytes: Data, _ offset: Int, _ size: Int = 4, big: Bool = true) throws -> UInt64 {
            try require(offset >= 0 && size <= bytes.count && offset <= bytes.count - size)
            var value: UInt64 = 0
            for i in 0..<size { value |= UInt64(bytes[offset + i]) << (8 * (big ? size - 1 - i : i)) }
            return value
        }
        func u32(_ offset: Int) throws -> Int { Int(try integer(data, offset, big: false)) }
        try require(try u32(0) == 0xfeedfacf && u32(4) == 0x0100000c && u32(12) == 6)
        let count = try u32(16), commandBytes = try u32(20)
        try require(commandBytes <= data.count - 32 && count <= commandBytes / 8)
        let end = 32 + commandBytes
        var cursor = 32, signature: (Int, Int)?, text: (UInt64, UInt64)?
        for _ in 0..<count {
            try require(cursor <= end - 8)
            let command = try u32(cursor), size = try u32(cursor + 4)
            try require(size >= 8 && size % 8 == 0 && size <= end - cursor)
            if command == 0x19 {
                try require(size >= 72)
                let name = Data(data[(cursor + 8)..<(cursor + 24)])
                if name == Data(Array("__TEXT".utf8) + Array(repeating: 0, count: 10)) {
                    try require(text == nil)
                    text = (try integer(data, cursor + 40, 8, big: false), try integer(data, cursor + 48, 8, big: false))
                }
            }
            if command == 0x1d {
                try require(size == 16 && signature == nil)
                signature = (try u32(cursor + 8), try u32(cursor + 12))
            }
            cursor += size
        }
        try require(cursor == end)
        guard let (offset, size) = signature, let text else { throw PrepareFailure(message: "Prepared game signature or text segment is missing.") }
        try require(offset >= end && offset <= data.count && size == data.count - offset && size >= 12)
        let superblob = Data(data[offset...])
        func be(_ at: Int) throws -> Int { Int(try integer(superblob, at)) }
        let length = try be(4), slots = try be(8)
        try require(try be(0) == 0xfade0cc0 && length <= size && length >= 12 && slots == 3)
        try require(length >= 12 + slots * 8)
        var blobs: [Int: Data] = [:], ranges: [Range<Int>] = []
        for i in 0..<slots {
            let slot = try be(12 + i * 8), start = try be(16 + i * 8)
            try require(blobs[slot] == nil && start >= 12 + slots * 8 && start <= length - 8)
            let extent = try be(start + 4)
            try require(extent >= 8 && extent <= length - start)
            let range = start..<(start + extent)
            try require(!ranges.contains { $0.overlaps(range) })
            blobs[slot] = Data(superblob[range]); ranges.append(range)
        }
        try require(Set(blobs.keys) == Set([0, 2, 0x10000]))
        let cd = blobs[0]!, requirements = blobs[2]!, cms = blobs[0x10000]!
        func field(_ at: Int, _ size: Int = 4) throws -> UInt64 { try integer(cd, at, size) }
        try require(cd.count >= 88)
        try require(try field(0) == 0xfade0c02 && field(4) == cd.count && field(8) == 0x20400 && field(12) == 0)
        try require(try integer(requirements, 0) == 0xfade0c01 && integer(cms, 0) == 0xfade0b01)
        try require(cd[36] == 32 && cd[37] == 2 && cd[38] == 0 && cd[39] == 14)
        for at in [40, 44, 52] { try require(try field(at) == 0) }
        try require(try field(56, 8) == 0 && field(64, 8) == text.0 && field(72, 8) == text.1 && field(80, 8) == 0)
        let hashOffset = Int(try field(16)), special = Int(try field(24)), pages = Int(try field(28))
        try require(try field(32) == offset && special == 2 && pages == (offset + 16383) / 16384)
        try require(hashOffset >= 88 + 64 && hashOffset <= cd.count && pages * 32 == cd.count - hashOffset)
        func string(_ at: Int) throws -> String {
            let start = Int(try field(at)), limit = hashOffset - 64
            try require(start >= 88 && start < limit)
            guard let end = cd[start..<limit].firstIndex(of: 0), let value = String(data: cd[start..<end], encoding: .utf8) else {
                throw PrepareFailure(message: "Invalid signature identifier.")
            }
            return value
        }
        try require(try string(48) == identity.team && string(20) == PhonePreparedGameSigner.identifier)
        for page in 0..<pages {
            try token.check()
            let start = page * 16384, finish = min(start + 16384, offset), hash = hashOffset + page * 32
            try require(Data(SHA256.hash(data: data[start..<finish])) == cd[hash..<(hash + 32)])
            if page % 64 == 0 || page + 1 == pages { progress(UInt64(finish), UInt64(offset)) }
        }
        try require(cd[(hashOffset - 32)..<hashOffset] == Data(repeating: 0, count: 32))
        try require(cd[(hashOffset - 64)..<(hashOffset - 32)] == Data(SHA256.hash(data: requirements)))
        try identity.verifyCMS(Data(cms.dropFirst(8)), codeDirectory: cd)
        try token.check()
    }
}
