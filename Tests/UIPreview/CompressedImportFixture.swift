import CryptoKit
import Foundation

// Container-only fixtures with named NCA or NCZ members and synthetic payload. These
// cover selection-to-import UI, classification and copying, never NCZ decoding.
enum CompressedImportFixture {
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


    /// -SatchelPreviewHomeImport nsz|xcz|broken: the home screen imports this once at launch,
    /// so its import card can be checked. "broken" is a small file that is not a package.
    static func homeSources(_ mode: String) throws -> [URL] {
        guard mode == "broken" else { return try sources(format: mode) }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("Broken Fixture.nsp")
        try Data("Synthetic file that is not a game package".utf8).write(to: url)
        return [url]
    }

    static func sources(format: String) throws -> [URL] {
        let whole = try source(format: format)
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewSplitImport") else { return [whole] }
        let bytes = try Data(contentsOf: whole)
        let count = 3, width = (bytes.count + count - 1) / count
        let mode = UserDefaults.standard.string(forKey: "SatchelPreviewSplitStyle") ?? "suffix"
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("Synthetic split " + format + " " + mode)
        if mode != "suffix" { try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true) }
        let parts = try (0..<count).map { index in
            let part: URL
            switch mode {
            case "folder": part = folder.appendingPathComponent(String(format: "%02d", index))
            case "selected": part = folder.appendingPathComponent("Synthetic." + (format.hasPrefix("xc") ? "xc" : "ns") + String(index))
            default: part = whole.appendingPathExtension(String(format: "%02d", index))
            }
            try bytes.subdata(in: (index * width)..<min((index + 1) * width, bytes.count)).write(to: part)
            return part
        }
        if mode == "folder" { return [folder] }
        return mode == "selected" ? Array(parts.reversed()) : parts
    }

    static func integrity(_ library: HomebrewLibrary) -> String {
        guard library.items.count == 1, let item = library.items.first,
              let actual = try? Data(contentsOf: library.localFile(item)),
              let expectedURL = try? source(format: item.fileExtension),
              let expected = try? Data(contentsOf: expectedURL) else { return "pending" }
        return actual == expected ? "one complete byte-identical container" : "copied bytes differ"
    }

    static func source(format: String) throws -> URL {
        let cartridge = format.hasPrefix("xc")
        let member = format.hasSuffix("z") ? "fixture.ncz" : "fixture.nca"
        let payload = Data("synthetic container payload, not executable content".utf8)
        let secure = directory([member], payloads: [payload], hashed: cartridge)
        let data = cartridge ? xci(secure) : secure
        let source = FileManager.default.temporaryDirectory.appendingPathComponent("Synthetic import." + format)
        try data.write(to: source, options: .atomic)
        return source
    }
}
