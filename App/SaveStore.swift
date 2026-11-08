import Foundation
import CryptoKit
import Darwin

/// Save bytes are opaque to Satchel. Validation covers the title binding, complete
/// file inventory, paths, sizes and checksums, not a game's private save semantics.
final class SaveStore: @unchecked Sendable {
    struct Entry: Codable, Equatable {
        let path: String
        let directory: Bool
        let size: Int
        let sha256: String
        let modified: Date
        let guestName: String?
    }
    struct Manifest: Codable {
        let format: Int
        let titleID: String
        let created: Date
        let lastSaved: Date
        let entries: [Entry]
    }
    struct Backup: Identifiable, Sendable {
        let url: URL
        let date: Date
        let bytes: Int64
        var id: String { url.lastPathComponent }
    }
    struct Summary: Sendable {
        let lastSaved: Date?
        let bytes: Int64
        let backups: [Backup]
    }
    static let backupLimit = Int64(3 * SaveArchive.maxArchiveBytes)
    static let guestAttribute = "org.d2rmobile.guest-name"
    private static let lock = NSRecursiveLock()
    private let root: URL
    private let beginAccess: (String, Bool) throws -> Void
    private let endAccess: () -> Void
    private let clone: (URL, URL) throws -> Void
    private let fm = FileManager.default
    private let backupBytes: Int64

    init(root: URL, backupBytes: Int64 = SaveStore.backupLimit,
         beginAccess: @escaping (String, Bool) throws -> Void = { _, _ in },
         endAccess: @escaping () -> Void = {},
         clone: @escaping (URL, URL) throws -> Void = { from, to in
             guard clonefile(from.path, to.path, UInt32(CLONE_NOFOLLOW)) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
         }) {
        self.root = root; self.backupBytes = backupBytes
        self.beginAccess = beginAccess; self.endAccess = endAccess; self.clone = clone
    }

    private func access<T>(_ title: String, allowRunning: Bool, _ body: (URL, URL) throws -> T) throws -> T {
        guard title.count == 16, title.utf8.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) }) else {
            throw SaveFailure(message: "The game's title ID is invalid.")
        }
        Self.lock.lock(); defer { Self.lock.unlock() }
        let id = title.uppercased()
        try beginAccess(id, allowRunning); defer { endAccess() }
        try Task.checkCancellation()
        let folder = root.appendingPathComponent(id, isDirectory: true)
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        try requireDirectory(root)
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        try requireDirectory(folder)
        // All temporary trees are ours, outside the engine's save and work paths.
        // A killed export or restore is cleaned on the next operation for this title.
        for url in try fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
            where url.lastPathComponent.hasPrefix(".satchel-save-") { try fm.removeItem(at: url) }
        let scratch = folder.appendingPathComponent(".satchel-save-" + UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: scratch, withIntermediateDirectories: false)
        defer { try? fm.removeItem(at: scratch) }
        return try body(folder, scratch)
    }

    private func snapshot(_ folder: URL, _ scratch: URL) throws -> URL? {
        let save = folder.appendingPathComponent("save", isDirectory: true)
        guard fm.fileExists(atPath: save.path) else { return nil }
        try requireDirectory(save)
        let copy = scratch.appendingPathComponent("snapshot", isDirectory: true)
        try clone(save, copy)
        return copy
    }
    private func requireDirectory(_ url: URL) throws {
        let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else { throw SaveFailure(message: "The save folder is not a regular directory.") }
    }
    private static func validPath(_ path: String) -> Bool {
        !path.isEmpty && path.utf8.count <= 768 && !path.contains("\\") && !path.contains(":") &&
        !path.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) &&
        path.split(separator: "/", omittingEmptySubsequences: false).allSatisfy { !$0.isEmpty && $0 != "." && $0 != ".." && $0.utf8.count <= 255 }
    }
    private func guestName(_ url: URL) throws -> String? {
        guard url.lastPathComponent.hasPrefix(".dm-case-") else { return nil }
        var buffer = [UInt8](repeating: 0, count: 768)
        let count = getxattr(url.path, Self.guestAttribute, &buffer, buffer.count, 0, XATTR_NOFOLLOW)
        if count < 0, errno == ENOATTR { return nil }
        guard count > 0, let name = String(bytes: buffer.prefix(count), encoding: .utf8),
              Self.validPath(name), !name.contains("/") else { throw SaveFailure(message: "A save filename mapping is invalid.") }
        return name
    }
    private func scan(_ save: URL, title: String) throws -> Manifest {
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey, .contentModificationDateKey, .linkCountKey]
        var entries: [Entry] = [], total = 0
        // clonefile recreates directories with new timestamps. Only file mtimes
        // describe game writes; a snapshot's directory dates are not save times.
        var lastSaved = Date.distantPast
        func walk(_ directory: URL, prefix: String) throws {
            for url in try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: Array(keys)).sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
                try Task.checkCancellation()
                let path = prefix + url.lastPathComponent, values = try url.resourceValues(forKeys: keys)
                guard Self.validPath(path), values.isSymbolicLink != true,
                      values.isDirectory == true || values.isRegularFile == true,
                      values.isDirectory == true || (values.linkCount ?? 1) == 1,
                      entries.count < SaveArchive.maxEntries else { throw SaveFailure(message: "The save contains an unsupported entry or too many files.") }
                let directory = values.isDirectory == true
                let digest = directory ? (size: 0, hash: "", crc: UInt32(0)) : try SaveArchive.digest(url)
                total += digest.size
                guard total <= SaveArchive.maxBytes else { throw SaveFailure(message: "This save exceeds the 512 MiB limit.") }
                let modified = values.contentModificationDate ?? .distantPast
                if !directory { lastSaved = max(lastSaved, modified) }
                entries.append(Entry(path: path, directory: directory, size: digest.size, sha256: digest.hash,
                                     modified: modified, guestName: try guestName(url)))
                if directory { try walk(url, prefix: path + "/") }
            }
        }
        try walk(save, prefix: "")
        return Manifest(format: 1, titleID: title.uppercased(), created: Date(), lastSaved: lastSaved, entries: entries)
    }

    func summary(title: String) throws -> Summary {
        try access(title, allowRunning: true) { folder, scratch in
            try rotate(folder)
            let copies = try backups(folder)
            guard let save = try snapshot(folder, scratch) else { return Summary(lastSaved: nil, bytes: 0, backups: copies) }
            let manifest = try scan(save, title: title)
            return Summary(lastSaved: manifest.entries.contains(where: { !$0.directory }) ? manifest.lastSaved : nil,
                           bytes: Int64(manifest.entries.reduce(0) { $0 + $1.size }), backups: copies)
        }
    }

    private func write(_ manifest: Manifest, from save: URL, to zip: URL) throws {
        let writer = try SaveArchive.Writer(url: zip)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let metadata = try encoder.encode(manifest)
        guard metadata.count <= 4 * 1024 * 1024 else { throw SaveFailure(message: "The save inventory is too large.") }
        try writer.add(name: "satchel-save.json", data: metadata)
        for entry in manifest.entries {
            let name = "save/" + entry.path + (entry.directory ? "/" : "")
            if entry.directory { try writer.add(name: name, data: Data()); continue }
            let url = save.appendingPathComponent(entry.path), digest = try SaveArchive.digest(url)
            guard digest.hash == entry.sha256, digest.size == entry.size else { throw SaveFailure(message: "The save snapshot changed.") }
            let file = try FileHandle(forReadingFrom: url)
            defer { try? file.close() }
            try writer.add(name: name, size: entry.size, crc: digest.crc) { try file.read(upToCount: SaveArchive.chunkSize) }
        }
        try writer.finish()
    }

    func export(title: String, name: String, to directory: URL) throws -> URL {
        try access(title, allowRunning: false) { folder, scratch in
            guard let save = try snapshot(folder, scratch) else { throw SaveFailure(message: "This game has no save yet.") }
            let manifest = try scan(save, title: title)
            guard manifest.entries.contains(where: { !$0.directory }) else { throw SaveFailure(message: "This game has no save yet.") }
            try fm.createDirectory(at: directory, withIntermediateDirectories: true)
            let safeName = String(name.unicodeScalars.map { CharacterSet.alphanumerics.contains($0) || $0 == " " || $0 == "-" ? String($0) : "_" }.joined().prefix(80))
            let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX"); formatter.dateFormat = "yyyy-MM-dd_HH-mm-ss"
            let destination = directory.appendingPathComponent("\(safeName.isEmpty ? "Satchel" : safeName)_\(formatter.string(from: Date())).zip")
            guard !fm.fileExists(atPath: destination.path) else { throw SaveFailure(message: "An export with this name already exists. Try again in a moment.") }
            let staged = scratch.appendingPathComponent("export.zip")
            try write(manifest, from: save, to: staged)
            try fm.moveItem(at: staged, to: destination)
            return destination
        }
    }

    private func backups(_ folder: URL) throws -> [Backup] {
        let directory = folder.appendingPathComponent("SaveBackups", isDirectory: true)
        guard fm.fileExists(atPath: directory.path) else { return [] }
        try requireDirectory(directory)
        return try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
            .filter { $0.lastPathComponent.hasPrefix("satchel-") && $0.pathExtension == "zip" }
            .map { url in
                let values = try url.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
                guard values.isRegularFile == true, values.isSymbolicLink != true else { throw SaveFailure(message: "An automatic backup is not a regular file.") }
                return Backup(url: url, date: values.contentModificationDate ?? .distantPast, bytes: Int64(values.fileSize ?? 0))
            }.sorted { $0.date == $1.date ? $0.id > $1.id : $0.date > $1.date }
    }
    private func fingerprint(_ manifest: Manifest) throws -> String {
        // Modification dates alone do not prove that game data changed.
        struct Content: Encodable { let path: String; let directory: Bool; let size: Int; let sha256: String; let guestName: String? }
        let rows = manifest.entries.sorted { $0.path < $1.path }.map { Content(path: $0.path, directory: $0.directory, size: $0.size, sha256: $0.sha256, guestName: $0.guestName) }
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        return SHA256.hash(data: try encoder.encode(rows)).map { String(format: "%02x", $0) }.joined()
    }
    private func rotate(_ folder: URL) throws {
        var copies = try backups(folder)
        var total = copies.reduce(Int64(0)) { $0 + $1.bytes }
        while copies.count > 3 || total > backupBytes {
            let oldest = copies.removeLast()
            try fm.removeItem(at: oldest.url)
            total -= oldest.bytes
        }
    }
    @discardableResult private func backup(_ manifest: Manifest, save: URL, folder: URL, scratch: URL) throws -> URL? {
        guard manifest.entries.contains(where: { !$0.directory }) else { return nil }
        if let latest = try backups(folder).first,
           let reader = try? SaveArchive.Reader(url: latest.url), let member = reader.members.first,
           member.name == "satchel-save.json", let data = try? reader.data(member),
           let previous = try? JSONDecoder().decode(Manifest.self, from: data),
           try fingerprint(previous) == fingerprint(manifest) {
            // Never rely on a manifest alone when preserving the pre-import save.
            // A damaged backup must be replaced even if its inventory still matches.
            let check = scratch.appendingPathComponent("backup-check")
            if latest.bytes <= backupBytes, (try? extract(latest.url, title: manifest.titleID, to: check)) != nil {
                try rotate(folder); return latest.url
            }
        }
        let staged = scratch.appendingPathComponent("backup.zip")
        try write(manifest, from: save, to: staged)
        let bytes = Int64(try staged.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
        guard bytes <= backupBytes else { throw SaveFailure(message: "This save exceeds the automatic backup storage limit. Export it before restoring.") }
        let directory = folder.appendingPathComponent("SaveBackups", isDirectory: true)
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        try requireDirectory(directory)
        let destination = directory.appendingPathComponent("satchel-\(UUID().uuidString).zip")
        let previousDate = try backups(folder).first?.date ?? .distantPast
        try fm.setAttributes([.modificationDate: max(Date(), previousDate.addingTimeInterval(0.001))], ofItemAtPath: staged.path)
        try fm.moveItem(at: staged, to: destination)
        // The new copy is durable before old copies are rotated. At most one extra
        // archive exists during publication; failures never remove the live save.
        try rotate(folder)
        return destination
    }
    @discardableResult func backupIfChanged(title: String) throws -> URL? {
        try access(title, allowRunning: true) { folder, scratch in
            guard let save = try snapshot(folder, scratch) else { return nil }
            return try backup(scan(save, title: title), save: save, folder: folder, scratch: scratch)
        }
    }

    /// Entries of a game's runtime folder that hold its saves: the save itself and every file
    /// the engine's save sync keeps beside it (`save_device`, `save.sync`, `save-backups` and
    /// the rest start with "save"), and Satchel's own backup list.
    static func holdsSaves(_ name: String) -> Bool { name == "SaveBackups" || name.hasPrefix("save") }

    /// Clears a deleted game's runtime folder: shader caches, logs, launch records and other
    /// temporary files go. With `keepSaves` every save entry stays. Without it the current
    /// save is first added to the backup list (a failed backup throws and deletes nothing),
    /// then everything except that list goes, so the save can still be restored if the game
    /// is added again. A folder that does not exist is fine. Returns the backup made, if any.
    @discardableResult func purge(title: String, keepSaves: Bool) throws -> URL? {
        // Never played here: nothing to clear, and no empty folder is left behind.
        if title.count == 16, !fm.fileExists(atPath: root.appendingPathComponent(title.uppercased(), isDirectory: true).path) { return nil }
        return try access(title, allowRunning: false) { folder, scratch in
            var backup: URL?
            if !keepSaves, let save = try snapshot(folder, scratch) {
                backup = try self.backup(scan(save, title: title), save: save, folder: folder, scratch: scratch)
            }
            for url in try fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil) {
                let name = url.lastPathComponent
                // The scratch tree is removed by `access` itself.
                if name.hasPrefix(".satchel-save-") { continue }
                if name == "SaveBackups" || (keepSaves && Self.holdsSaves(name)) { continue }
                try fm.removeItem(at: url)
            }
            return backup
        }
    }

    private func extract(_ archive: URL, title: String, to destination: URL) throws -> Manifest {
        let reader = try SaveArchive.Reader(url: archive)
        guard let first = reader.members.first, first.name == "satchel-save.json" else { throw SaveFailure(message: "The ZIP has no Satchel save manifest.") }
        let manifest = try JSONDecoder().decode(Manifest.self, from: reader.data(first))
        guard manifest.format == 1, manifest.titleID == title.uppercased() else { throw SaveFailure(message: "This save belongs to a different title or an unsupported archive version.") }
        guard manifest.entries.contains(where: { !$0.directory }), manifest.entries.count <= SaveArchive.maxEntries,
              reader.members.count == manifest.entries.count + 1,
              manifest.lastSaved.timeIntervalSince1970.isFinite else { throw SaveFailure(message: "The save inventory is incomplete.") }
        var paths = Set<String>(), logicalNames = Set<String>(), directories = Set<String>(), total = 0
        try fm.createDirectory(at: destination, withIntermediateDirectories: false)
        for (entry, member) in zip(manifest.entries, reader.members.dropFirst()) {
            try Task.checkCancellation()
            let folded = entry.path.precomposedStringWithCanonicalMapping.lowercased()
            let parent = (entry.path as NSString).deletingLastPathComponent
            let logical = parent + "/" + (entry.guestName ?? (entry.path as NSString).lastPathComponent)
            guard Self.validPath(entry.path), paths.insert(folded).inserted, logicalNames.insert(logical).inserted,
                  parent.isEmpty || directories.contains(parent), entry.size >= 0, entry.size <= SaveArchive.maxBytes,
                  entry.modified.timeIntervalSince1970.isFinite,
                  member.name == "save/" + entry.path + (entry.directory ? "/" : ""), member.size == entry.size else {
                throw SaveFailure(message: "The save contains an invalid or duplicate path.")
            }
            total += entry.size
            guard total <= SaveArchive.maxBytes else { throw SaveFailure(message: "This save exceeds the 512 MiB limit.") }
            let url = destination.appendingPathComponent(entry.path)
            if entry.directory {
                guard entry.size == 0, entry.sha256.isEmpty else { throw SaveFailure(message: "A save directory has file data.") }
                try reader.consume(member) { _ in }
                try fm.createDirectory(at: url, withIntermediateDirectories: false); directories.insert(entry.path)
            } else {
                guard fm.createFile(atPath: url.path, contents: nil) else { throw SaveFailure(message: "Could not stage the save.") }
                let output = try FileHandle(forWritingTo: url); defer { try? output.close() }
                var hash = SHA256()
                try reader.consume(member) { chunk in hash.update(data: chunk); try output.write(contentsOf: chunk) }
                guard hash.finalize().map({ String(format: "%02x", $0) }).joined() == entry.sha256 else { throw SaveFailure(message: "A save file checksum failed.") }
                try output.synchronize()
            }
            if let name = entry.guestName {
                guard url.lastPathComponent.hasPrefix(".dm-case-"), Self.validPath(name), !name.contains("/") else { throw SaveFailure(message: "A save filename mapping is invalid.") }
                let bytes = Array(name.utf8)
                guard setxattr(url.path, Self.guestAttribute, bytes, bytes.count, 0, XATTR_NOFOLLOW) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
            }
        }
        // Set directories after their children so extraction does not change dates.
        for entry in manifest.entries.reversed() {
            try fm.setAttributes([.modificationDate: entry.modified], ofItemAtPath: destination.appendingPathComponent(entry.path).path)
        }
        try fm.setAttributes([.modificationDate: manifest.lastSaved], ofItemAtPath: destination.path)
        return manifest
    }

    func restore(title: String, from archive: URL) throws {
        try access(title, allowRunning: false) { folder, scratch in
            let staged = scratch.appendingPathComponent("restored", isDirectory: true)
            _ = try extract(archive, title: title, to: staged)
            let current = folder.appendingPathComponent("save", isDirectory: true)
            if let old = try snapshot(folder, scratch) {
                // A backup failure refuses the restore before the atomic replacement.
                _ = try backup(scan(old, title: title), save: old, folder: folder, scratch: scratch)
                guard renameatx_np(AT_FDCWD, staged.path, AT_FDCWD, current.path, UInt32(RENAME_SWAP)) == 0 else {
                    throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
                }
            } else { try fm.moveItem(at: staged, to: current) }
        }
    }
}
