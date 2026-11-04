import CryptoKit
import Foundation

@main
struct PackageReaderTests {
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

    static func main() throws {
        let scratch = FileManager.default.temporaryDirectory.appendingPathComponent("satchel-package-tests-" + UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: scratch) }
        let url = scratch.appendingPathComponent("fixture.bin")
        var assertions = 0
        func check(_ condition: Bool) throws {
            assertions += 1
            if !condition { throw NSError(domain: "PackageTestAssertion", code: assertions) }
        }
        func inspect(_ bytes: Data) throws -> SwitchPackageSummary {
            try bytes.write(to: url)
            return try SwitchPackageReader.inspect(url)
        }
        func reject(_ bytes: Data, _ expected: SwitchPackageError) throws {
            do { _ = try inspect(bytes) }
            catch let error as SwitchPackageError { try check(String(describing: error) == String(describing: expected)); return }
            throw NSError(domain: "AcceptedInvalidFixture", code: assertions)
        }
        let payload = Data([1, 2, 3, 4])
        let nsp = directory(["a.nca", "a.tik"], payloads: [payload, payload], hashed: false)
        let summary = try inspect(nsp)
        try check(summary.format == .nsp && summary.entryCount == 2 && summary.contentBytes == 8)
        let decoded = try JSONDecoder().decode(SwitchPackageSummary.self, from: JSONEncoder().encode(summary))
        try check(decoded.contentBytes == 8 && decoded.format == .nsp)
        try reject(Data(), .unsupported)
        try reject(Data("NSZ0".utf8), .unsupported)
        try reject(Data(nsp.prefix(12)), .invalid)
        try reject(Data(nsp.dropLast()), .invalid)
        try reject(directory([], payloads: [], hashed: false), .invalid)
        try reject(directory(["empty"], payloads: [Data()], hashed: false), .invalid)
        for name in ["..", ".", "../a", "a/b", "a\\b", "a\nb", ""] {
            try reject(directory([name], payloads: [payload], hashed: false), .invalid)
        }
        try reject(directory(["A", "a"], payloads: [payload, payload], hashed: false), .invalid)
        try reject(directory([String(repeating: "a", count: 1025)], payloads: [payload], hashed: false), .invalid)
        var bad = nsp
        put(UInt64.max, in: &bad, at: 16, width: 8); try reject(bad, .invalid)
        bad = nsp; put(UInt64.max, in: &bad, at: 24, width: 8); try reject(bad, .invalid)
        bad = nsp; put(0, in: &bad, at: 40, width: 8); try reject(bad, .invalid)
        bad = nsp; put(UInt64(UInt32.max), in: &bad, at: 32, width: 4); try reject(bad, .invalid)
        bad = nsp; put(8193, in: &bad, at: 4, width: 4); try reject(bad, .limit)
        bad = nsp; put(16 * 1024 * 1024 + 1, in: &bad, at: 8, width: 4); try reject(bad, .limit)
        bad = nsp; bad[12] = 1; try reject(bad, .invalid)
        bad = nsp; bad[36] = 1; try reject(bad, .invalid)
        bad = nsp; bad[64] = 0xff; try reject(bad, .invalid)
        bad = directory(["x"], payloads: [payload], hashed: false)
        bad[41] = 1; try reject(bad, .invalid)

        let secure = directory(["content.nca"], payloads: [payload], hashed: true)
        let cart = xci(secure)
        let cartSummary = try inspect(cart)
        try check(cartSummary.format == .xci && cartSummary.entryCount == 1 && cartSummary.contentBytes == 4)
        try check(try inspect(xci(secure, prefix: 4096)).contentBytes == 4)
        try reject(xci(secure, partition: "normal"), .invalid)
        try reject(xci(directory([], payloads: [], hashed: true)), .invalid)
        bad = cart; bad[0x140] ^= 1; try reject(bad, .hashMismatch)
        bad = cart; bad[bad.count - 1] ^= 1; try reject(bad, .hashMismatch)
        bad = secure; bad[bad.count - 1] ^= 1; try reject(xci(bad), .hashMismatch)
        bad = secure; bad[16 + 24] = 1; try reject(xci(bad), .invalid)
        bad = secure; put(5, in: &bad, at: 16 + 20, width: 4); try reject(xci(bad), .invalid)
        bad = secure; put(0, in: &bad, at: 16 + 20, width: 4); try reject(xci(bad), .invalid)
        bad = cart; put(UInt64.max, in: &bad, at: 0x130, width: 8); try reject(bad, .invalid)
        bad = cart; put(1, in: &bad, at: 0x138, width: 8); try reject(bad, .invalid)
        bad = cart; put(16 * 1024 * 1024 + 1, in: &bad, at: 0x138, width: 8); try reject(bad, .limit)
        try reject(Data(cart.dropLast()), .invalid)
        // A root partition's advertised tail may include absent final padding;
        // its entire secure directory/content/hash ranges must still be present.
        bad = cart
        put(UInt64(secure.count + 1024), in: &bad, at: 0xf000 + 24, width: 8)
        let rootHeaderSize = cart.count - 0xf000 - secure.count
        bad.replaceSubrange(0x140..<0x160, with: Data(SHA256.hash(data: bad[0xf000..<(0xf000 + rootHeaderSize)])))
        try check(try inspect(bad).contentBytes == 4)
        try reject(Data(bad.dropLast()), .invalid)
        var badNormal = bad
        badNormal.replaceSubrange((0xf000 + 80)..<(0xf000 + 86), with: Data("normal".utf8))
        try reject(badNormal, .invalid)
        put(UInt64.max, in: &bad, at: 0xf000 + 24, width: 8)
        try reject(bad, .invalid)

        // A >4-GiB logical file occupies only a few filesystem blocks. The reader
        // must inspect 64-bit directory sizes without allocating/reading its payload.
        var sparseHeader = directory(["large.nca"], payloads: [Data()], hashed: false)
        let largeSize: UInt64 = 5 * 1024 * 1024 * 1024
        put(largeSize, in: &sparseHeader, at: 24, width: 8)
        try sparseHeader.write(to: url)
        let sparse = try FileHandle(forWritingTo: url)
        try sparse.truncate(atOffset: UInt64(sparseHeader.count) + largeSize)
        try sparse.close()
        try check(try SwitchPackageReader.inspect(url).contentBytes == largeSize)
        do { _ = try SwitchPackageReader.inspect(url, cancelled: { true }); throw NSError(domain: "CancellationIgnored", code: 1) }
        catch is CancellationError { assertions += 1 }

        // Cancellation partway through a multi-chunk HFS0 hash.
        let chunky = xci(directory(["large.nca"], payloads: [Data(repeating: 7, count: 2 * 1024 * 1024)], hashed: true))
        try chunky.write(to: url)
        var checks = 0
        do {
            _ = try SwitchPackageReader.inspect(url, cancelled: { checks += 1; return checks == 35 })
            throw NSError(domain: "CancellationIgnored", code: 2)
        } catch is CancellationError { assertions += 1 }
        try check(try SwitchPackageReader.inspect(url).contentBytes == 2 * 1024 * 1024)
        print("Package reader: \(assertions) synthetic assertions passed (including 5-GiB sparse file and hash cancellation).")
        for (index, path) in CommandLine.arguments.dropFirst().enumerated() {
            let actual = try SwitchPackageReader.inspect(URL(fileURLWithPath: path))
            print("Actual package \(index + 1): \(actual.format.rawValue), \(actual.entryCount) entries, \(actual.contentBytes) content bytes; container checks passed.")
        }
    }
}
