import Foundation

/// Small, bounded reads of public signing metadata. Never maps or reads game code.
enum SignatureExpiry {
    static let warningInterval: TimeInterval = 3 * 24 * 60 * 60
    static let metadataLimit = 4 * 1024 * 1024

    struct Warning: Identifiable, Equatable {
        let id: String
        let name: String
        let expiry: Date
        let isApp: Bool

        func message(now: Date) -> String {
            let date = expiry.formatted(date: .abbreviated, time: .shortened)
            let timing = expiry <= now ? "expired on \(date)" : "expires on \(date)"
            return isApp
                ? "Satchel's signature \(timing). Refresh or re-sign Satchel with SideStore, AltStore or Xcode so it keeps opening. Keep the installed app and its library."
                : "\(name)'s signing certificate \(timing). Renew your signing identity in Prepare, then prepare this game again. Re-import its original files if needed. Refreshing Satchel alone does not renew a prepared game's signature."
        }
    }

    static func warnings(app: Date?, games: [(String, String, Date)], now: Date) -> [Warning] {
        var result: [Warning] = []
        if let app, app <= now.addingTimeInterval(warningInterval) {
            result.append(Warning(id: "app", name: "Satchel", expiry: app, isApp: true))
        }
        // Show every due game, including certificates that survive the app's next refresh.
        for (id, name, expiry) in games where expiry <= now.addingTimeInterval(warningInterval) {
            result.append(Warning(id: id, name: name, expiry: expiry, isApp: false))
        }
        return result.sorted { $0.expiry < $1.expiry }
    }

    static func profileExpiry(_ plist: Data) -> Date? {
        guard plist.count <= metadataLimit,
              let values = try? PropertyListSerialization.propertyList(from: plist, format: nil) as? [String: Any]
        else { return nil }
        return values["ExpirationDate"] as? Date
    }

    /// Use the earlier installed profile or executable signer deadline, never install time.
    static func appExpiry(profile: Date?, signer: Date?) -> Date? {
        [profile, signer].compactMap { $0 }.min()
    }

    static func boundedData(_ url: URL) throws -> Data {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        let data = try file.read(upToCount: metadataLimit + 1) ?? Data()
        guard data.count <= metadataLimit else { throw ReadFailure.invalid }
        return data
    }

    private enum ReadFailure: Error { case invalid }

    /// Only the Mach-O header, load commands and CMS slot are needed for an expiry notice.
    static func gameCMS(_ url: URL) throws -> Data {
        try codeCMS(url, executable: false)
    }

    static func codeCMS(_ url: URL, executable: Bool) throws -> Data {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        let fileSize = try file.seekToEnd()
        func read(_ offset: UInt64, _ count: Int) throws -> Data {
            guard count >= 0, count <= metadataLimit, offset <= fileSize, UInt64(count) <= fileSize - offset else { throw ReadFailure.invalid }
            try file.seek(toOffset: offset)
            let data = try file.read(upToCount: count) ?? Data()
            guard data.count == count else { throw ReadFailure.invalid }
            return data
        }
        func u32(_ data: Data, _ offset: Int, big: Bool = false) throws -> Int {
            guard offset >= 0, offset <= data.count - 4 else { throw ReadFailure.invalid }
            var result: UInt32 = 0
            for i in 0..<4 { result |= UInt32(data[offset + i]) << (8 * (big ? 3 - i : i)) }
            return Int(result)
        }
        let header = try read(0, 32)
        guard try u32(header, 0) == 0xfeedfacf, try u32(header, 4) == 0x0100000c, try u32(header, 12) == (executable ? 2 : 6) else { throw ReadFailure.invalid }
        let count = try u32(header, 16), length = try u32(header, 20)
        guard length <= metadataLimit, count <= length / 8 else { throw ReadFailure.invalid }
        let commands = try read(32, length)
        var cursor = 0, signature: (Int, Int)?
        for _ in 0..<count {
            let kind = try u32(commands, cursor), size = try u32(commands, cursor + 4)
            guard size >= 8, size % 8 == 0, size <= commands.count - cursor else { throw ReadFailure.invalid }
            if kind == 0x1d {
                guard size == 16, signature == nil else { throw ReadFailure.invalid }
                signature = (try u32(commands, cursor + 8), try u32(commands, cursor + 12))
            }
            cursor += size
        }
        guard cursor == length, let (offset, size) = signature, offset >= 32 + length, size >= 12,
              UInt64(offset) <= fileSize, UInt64(size) <= fileSize - UInt64(offset) else { throw ReadFailure.invalid }
        let superHeader = try read(UInt64(offset), 12)
        let blobLength = try u32(superHeader, 4, big: true), slots = try u32(superHeader, 8, big: true)
        guard try u32(superHeader, 0, big: true) == 0xfade0cc0,
              blobLength >= 12, blobLength <= size, slots <= 64, slots <= (blobLength - 12) / 8 else { throw ReadFailure.invalid }
        let index = try read(UInt64(offset + 12), slots * 8)
        var cms: Data?
        for slot in 0..<slots {
            let kind = try u32(index, slot * 8, big: true), start = try u32(index, slot * 8 + 4, big: true)
            guard start >= 12 + slots * 8, start <= blobLength - 8 else { throw ReadFailure.invalid }
            if kind != 0x10000 { continue }
            let wrapper = try read(UInt64(offset + start), 8)
            let extent = try u32(wrapper, 4, big: true)
            guard cms == nil, try u32(wrapper, 0, big: true) == 0xfade0b01,
                  extent > 8, extent <= blobLength - start else { throw ReadFailure.invalid }
            cms = try read(UInt64(offset + start + 8), extent - 8)
        }
        guard let cms else { throw ReadFailure.invalid }
        return cms
    }
}
