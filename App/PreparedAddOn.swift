import Foundation

/// A package binding, not an entitlement invented by the host. Each row comes from verified CNMT.
struct PreparedAddOn: Codable, Equatable {
    let titleID: String
    let applicationID: String
    let version: UInt32
    let requiredApplicationVersion: UInt32
    let index: Int
    let path: String
    let bytes: UInt64
    let sha256: String
}

extension PreparedAddOn {
    /// The engine enumerates the directory, so its entire membership must be bound, even on cache hits.
    static func validate(_ rows: [Self], titleID: String, contentVersion: UInt32?, root: URL,
                         files: [String: (bytes: UInt64, sha256: String)]) throws {
        func invalid() -> NSError {
            NSError(domain: "PreparedAddOn", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Add-on content does not match its package bindings."])
        }
        guard let application = UInt64(titleID, radix: 16), application <= UInt64.max - 0x1000,
              rows.count <= 2000 else { throw invalid() }
        var expected = Set<String>(), titles = Set<String>()
        for row in rows {
            guard row.applicationID == titleID, let id = UInt64(row.titleID, radix: 16),
                  row.titleID == String(format: "%016llX", id), titles.insert(row.titleID).inserted,
                  (1...2000).contains(row.index), id > application + 0x1000, id - application - 0x1000 == UInt64(row.index),
                  row.path == "aoc/\(row.index).img", expected.insert(row.path).inserted,
                  let version = contentVersion, row.requiredApplicationVersion <= version,
                  let file = files[row.path], file.bytes == row.bytes, file.sha256 == row.sha256 else { throw invalid() }
        }
        guard Set(files.keys.filter { $0 == "aoc" || $0.hasPrefix("aoc/") }) == expected else { throw invalid() }
        let directory = root.appendingPathComponent("aoc", isDirectory: true)
        let fm = FileManager.default
        if !fm.fileExists(atPath: directory.path) {
            guard rows.isEmpty else { throw invalid() }
            // A dangling symlink is not an absent directory.
            if (try? directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == true { throw invalid() }
            return
        }
        let values = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else { throw invalid() }
        let children = try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard Set(children.map { "aoc/" + $0.lastPathComponent }) == expected else { throw invalid() }
        for child in children {
            let v = try child.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard v.isRegularFile == true, v.isSymbolicLink != true else { throw invalid() }
        }
    }

    /// Display only. Launch independently validates both the rows and every image digest.
    static func read(from root: URL) -> [Self] {
        struct Summary: Decodable { let addOnContent: [PreparedAddOn]? }
        let file = root.appendingPathComponent("manifest.json")
        guard let size = try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize,
              size <= 32 * 1024 * 1024, let data = try? Data(contentsOf: file),
              let summary = try? JSONDecoder().decode(Summary.self, from: data) else { return [] }
        return summary.addOnContent ?? []
    }
}
