import CryptoKit
import Darwin
import Foundation

/// APFS snapshots keep rollback bytes independent without allocating another game-sized payload.
/// All work runs before either app creates its library or starts a native session.
struct MigrationFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

final class MigrationStore {
    struct Entry: Codable, Equatable {
        let path: String
        let directory: Bool
        let bytes: UInt64
        let sha256: String
    }
    struct Manifest: Codable {
        let version: Int
        let entries: [Entry]
    }
    struct Plan: Codable {
        let roots: [String]
        let entries: [Entry]
    }
    let shared: URL
    let documents: URL
    let state: URL
    let fm = FileManager.default
    var progress: (String) -> Void = { _ in }
    // Test injection only: called after durable transitions, never used by the app.
    var checkpoint: (String) throws -> Void = { _ in }

    init(shared: URL, documents: URL, state: URL) {
        self.shared = Self.real(shared)
        self.documents = Self.real(documents)
        self.state = Self.real(state)
    }
    /// The real path. resolvingSymlinksInPath turns iOS's /private/var into /var, a symbolic link, which
    /// clonefile with CLONE_NOFOLLOW refuses (ELOOP) and which enumerators do not report back. A path that
    /// does not exist yet keeps its resolved parent.
    static func real(_ url: URL) -> URL {
        if let resolved = realpath(url.path, nil) {
            defer { free(resolved) }
            return URL(fileURLWithPath: String(cString: resolved), isDirectory: true)
        }
        let parent = url.deletingLastPathComponent()
        if parent.path == url.path { return url }
        return real(parent).appendingPathComponent(url.lastPathComponent, isDirectory: true)
    }
    var ready: Bool { fm.fileExists(atPath: shared.appendingPathComponent("manifest.json").path) }
    var complete: Bool { fm.fileExists(atPath: state.appendingPathComponent("complete.json").path) }
    var started: Bool { fm.fileExists(atPath: state.appendingPathComponent("started").path) }
    func fail(_ text: String) -> MigrationFailure { MigrationFailure(message: text) }
    func exists(_ url: URL) -> Bool { fm.fileExists(atPath: url.path) }
    func directory(_ url: URL) throws {
        try fm.createDirectory(at: url, withIntermediateDirectories: true)
        let attrs = try fm.attributesOfItem(atPath: url.path)
        guard attrs[.type] as? FileAttributeType == .typeDirectory else { throw fail("Migration found an unsafe directory. No original data was removed.") }
    }
    func write(_ data: Data, _ url: URL) throws {
        try directory(url.deletingLastPathComponent())
        try data.write(to: url, options: .atomic)
        let file = try FileHandle(forWritingTo: url)
        try file.synchronize(); try file.close()
    }
    func locked<T>(_ body: () throws -> T) throws -> T {
        try directory(shared)
        let fd = open(shared.appendingPathComponent("migration.lock").path, O_CREAT | O_RDWR | O_NOFOLLOW, 0o600)
        guard fd >= 0 else { throw fail("Migration could not open its lock.") }
        defer { close(fd) }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { throw fail("Migration is running in the other app. Wait for it to finish.") }
        defer { flock(fd, LOCK_UN) }
        return try body()
    }
    func child(_ root: URL, _ path: String) throws -> URL {
        let pieces = path.split(separator: "/", omittingEmptySubsequences: false)
        guard !pieces.isEmpty, pieces.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." && !$0.contains("\0") }) else {
            throw fail("Migration manifest contains an unsafe path.")
        }
        var url = root
        for piece in pieces {
            url.appendPathComponent(String(piece))
            if let attrs = try? fm.attributesOfItem(atPath: url.path), attrs[.type] as? FileAttributeType == .typeSymbolicLink {
                throw fail("Migration refuses symbolic links. Original files are unchanged.")
            }
        }
        return url
    }
    func digest(_ url: URL) throws -> String {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var hash = SHA256()
        var bytes: Int64 = 0
        while try autoreleasepool(invoking: { () throws -> Bool in
            guard let data = try file.read(upToCount: 4 * 1024 * 1024), !data.isEmpty else { return false }
            hash.update(data: data); bytes += Int64(data.count)
            if bytes % (256 * 1024 * 1024) == 0 {
                progress("Checking a large file: " + ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) + ". Keep the app open.")
            }
            return true
        }) {}
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
    func inventory(_ root: URL) throws -> [Entry] {
        guard exists(root) else { return [] }
        var failure: Error?
        // Relative URLs: the enumerator may report /private/var for a /var root, so cutting the root's
        // length off an absolute path would garble every name.
        guard let scan = fm.enumerator(at: root, includingPropertiesForKeys: nil, options: [.producesRelativePathURLs], errorHandler: { _, error in
            failure = error; return false
        }) else { throw fail("Migration could not read a folder.") }
        var entries: [Entry] = []
        while try autoreleasepool(invoking: { () throws -> Bool in
            guard let url = scan.nextObject() as? URL else { return false }
            let path = url.relativePath
            _ = try child(root, path)
            let attrs = try fm.attributesOfItem(atPath: url.path)
            let type = attrs[.type] as? FileAttributeType
            guard type == .typeRegular || type == .typeDirectory else { throw fail("Migration supports regular files and folders only.") }
            let dir = type == .typeDirectory
            entries.append(Entry(path: path, directory: dir, bytes: dir ? 0 : (attrs[.size] as? NSNumber)?.uint64Value ?? 0,
                                 sha256: dir ? "" : try digest(url)))
            if entries.count % 1000 == 0 { progress("Verified \(entries.count) files and folders. Keep the app open.") }
            return true
        }) {}
        if let failure { throw failure }
        return entries.sorted { $0.path < $1.path }
    }
    func verify(_ root: URL, _ entries: [Entry]) throws {
        guard try inventory(root) == entries else { throw fail("Migration verification failed. Originals and rollback data are retained. Do not retire Satchel Dev.") }
    }
    func clone(_ source: URL, _ destination: URL) throws {
        let type = try fm.attributesOfItem(atPath: source.path)[.type] as? FileAttributeType
        if type == .typeDirectory {
            try directory(destination)
            for name in try fm.contentsOfDirectory(atPath: source.path).sorted() {
                try autoreleasepool { try clone(child(source, name), child(destination, name)) }
            }
            return
        }
        guard type == .typeRegular else { throw fail("Migration refuses links and special files.") }
        try directory(destination.deletingLastPathComponent())
        // No fallback to copyItem: a full copy can exhaust a phone holding 60 GB of games.
        guard clonefile(source.path, destination.path, UInt32(CLONE_NOFOLLOW | CLONE_NOOWNERCOPY)) == 0 else {
            throw fail("APFS snapshot failed (errno \(errno)). Free some metadata space, unlock the phone and retry. Original data is retained; no full-copy fallback is used.")
        }
    }
    func snapshot(_ source: URL, _ destination: URL, entries: [Entry]) throws {
        try directory(destination)
        for (index, entry) in entries.enumerated() {
            try autoreleasepool {
                let target = try child(destination, entry.path)
                if entry.directory { try directory(target) }
                else if !exists(target) { try clone(child(source, entry.path), target) }
            }
            try checkpoint("snapshot-file")
            if index % 1000 == 0 { progress("Snapshot \(index) of \(entries.count). Game data blocks stay on this phone.") }
        }
        try verify(destination, entries)
    }
    func exportDocuments() throws {
        if ready {
            let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: shared.appendingPathComponent("manifest.json")))
            guard manifest.version == 1 else { throw fail("This migration format is not supported.") }
            try verify(shared.appendingPathComponent("Documents"), manifest.entries)
            try verify(documents, manifest.entries)
            return
        }
        progress("Checking source files. Keep Satchel Dev open and unlocked.")
        let entries = try inventory(documents)
        try checkpoint("export-inventory")
        try snapshot(documents, shared.appendingPathComponent("Documents"), entries: entries)
        try checkpoint("export-snapshot")
        // Detect a changed source before publishing the immutable snapshot.
        try verify(documents, entries)
        try write(JSONEncoder().encode(Manifest(version: 1, entries: entries)), shared.appendingPathComponent("manifest.json"))
        try checkpoint("export-ready")
    }
    func mergedLibrary(_ original: URL, _ incoming: URL) throws -> Data {
        func read(_ url: URL) throws -> [[String: Any]] {
            guard exists(url) else { return [] }
            guard let rows = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [[String: Any]],
                  rows.allSatisfy({ ($0["id"] as? String).map { $0.count == 64 && $0.allSatisfy { "0123456789abcdef".contains($0) } } == true }) else {
                throw fail("A library index is invalid. Migration stopped without replacing it.")
            }
            return rows
        }
        var rows = try read(original)
        var ids = Set(rows.compactMap { $0["id"] as? String })
        for row in try read(incoming) where ids.insert(row["id"] as! String).inserted { rows.append(row) }
        return try JSONSerialization.data(withJSONObject: rows, options: [.sortedKeys])
    }
    func makePlan() throws -> Plan {
        let incoming = shared.appendingPathComponent("Documents")
        let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: shared.appendingPathComponent("manifest.json")))
        guard manifest.version == 1 else { throw fail("This migration format is not supported.") }
        try verify(incoming, manifest.entries)
        let stage = state.appendingPathComponent("Stage")
        // Only unpublished task scratch can be discarded. Original and shared snapshots never are.
        if exists(stage) { try fm.removeItem(at: stage) }
        let mainEntries = try inventory(documents)
        try snapshot(documents, stage, entries: mainEntries)
        let mergedIndex = try mergedLibrary(documents.appendingPathComponent("Homebrew/library.json"), incoming.appendingPathComponent("Homebrew/library.json"))
        var expected = Dictionary(uniqueKeysWithValues: mainEntries.map { ($0.path, $0) })
        func graft(_ sourcePath: String, _ targetPath: String) {
            for key in Array(expected.keys) where key == targetPath || key.hasPrefix(targetPath + "/") { expected.removeValue(forKey: key) }
            for entry in manifest.entries where entry.path == sourcePath || entry.path.hasPrefix(sourcePath + "/") {
                let path = targetPath + entry.path.dropFirst(sourcePath.count)
                expected[path] = Entry(path: path, directory: entry.directory, bytes: entry.bytes, sha256: entry.sha256)
            }
        }
        let roots = try fm.contentsOfDirectory(atPath: incoming.path).sorted()
        for root in roots {
            let source = try child(incoming, root)
            let target = try child(stage, root)
            if (root == "PreparedGames" || root == "ShaderPacks"), exists(target) {
                for name in try fm.contentsOfDirectory(atPath: source.path).sorted() {
                    var destination = try child(target, name)
                    if root == "ShaderPacks", exists(destination) {
                        try fm.removeItem(at: destination)
                    } else if exists(destination) {
                        var suffix = 1
                        repeat {
                            destination = try child(target, name + "-from-dev-\(suffix)"); suffix += 1
                        } while exists(destination)
                    }
                    try clone(child(source, name), destination)
                    graft(root + "/" + name, root + "/" + destination.lastPathComponent)
                }
            } else if root == "Homebrew", exists(target) {
                for name in try fm.contentsOfDirectory(atPath: source.path).sorted() where name != "library.json" {
                    let destination = try child(target, name)
                    let file = try child(source, name)
                    if exists(destination) {
                        guard try digest(destination) == digest(file) else { throw fail("An imported file conflicts with the same library identifier. Both libraries are retained.") }
                    } else { try clone(file, destination) }
                    graft(root + "/" + name, root + "/" + name)
                }
            } else if root == "NativeRuntime", exists(target) {
                // Merge by title, but replace a whole save tree so stale slots cannot mix with Dev saves.
                for title in try fm.contentsOfDirectory(atPath: source.path).sorted() {
                    let destination = try child(target, title)
                    if exists(destination) { try fm.removeItem(at: destination) }
                    try clone(child(source, title), destination)
                    graft(root + "/" + title, root + "/" + title)
                }
            } else {
                // The entire original root is retained by publish(), including conflicting shader packs.
                if exists(target) { try fm.removeItem(at: target) }
                try clone(source, target)
                graft(root, root)
            }
        }
        if exists(incoming.appendingPathComponent("Homebrew/library.json")) {
            try write(mergedIndex, stage.appendingPathComponent("Homebrew/library.json"))
            expected["Homebrew/library.json"] = Entry(path: "Homebrew/library.json", directory: false,
                bytes: UInt64(mergedIndex.count), sha256: SHA256.hash(data: mergedIndex).map { String(format: "%02x", $0) }.joined())
        }
        // Every source file must survive exactly, except the intentionally merged library index.
        let entries = expected.values.sorted { $0.path < $1.path }
        try verify(stage, entries)
        let plan = Plan(roots: try fm.contentsOfDirectory(atPath: stage.path).sorted(), entries: entries)
        try write(JSONEncoder().encode(plan), state.appendingPathComponent("plan.json"))
        try checkpoint("adopt-plan")
        return plan
    }
    func move(_ source: URL, _ destination: URL) throws {
        // Exclusive rename never overwrites an unexpected destination or falls back to a copy.
        guard renamex_np(source.path, destination.path, UInt32(RENAME_EXCL)) == 0 else {
            throw fail("Migration publication stopped (errno \(errno)). Original and pending data are retained. Retry before opening the library.")
        }
    }
    func adoptDocuments() throws {
        try directory(state)
        try write(Data("1".utf8), state.appendingPathComponent("started"))
        let planURL = state.appendingPathComponent("plan.json")
        let plan = try exists(planURL) ? JSONDecoder().decode(Plan.self, from: Data(contentsOf: planURL)) : makePlan()
        let stage = state.appendingPathComponent("Stage")
        let backup = state.appendingPathComponent("Original")
        try directory(backup)
        for root in plan.roots {
            let pending = try child(stage, root)
            let target = try child(documents, root)
            let original = try child(backup, root)
            if exists(pending) {
                if exists(target) {
                    guard !exists(original) else { throw fail("Ambiguous migration state. Original data is retained for recovery.") }
                    try move(target, original)
                    try checkpoint("adopt-backup")
                }
                try move(pending, target)
                try checkpoint("adopt-publish")
            } else if !exists(target) { throw fail("A migration destination is missing. Rollback snapshots are retained.") }
        }
        try verify(documents, plan.entries)
        try checkpoint("adopt-verified")
    }
    func finish() throws {
        try write(Data("{\"version\":1,\"verified\":true}".utf8), state.appendingPathComponent("complete.json"))
        // Shared acknowledgement is deliberately after local completion. A retry can repair it.
        try write(Data("{\"version\":1,\"verified\":true}".utf8), shared.appendingPathComponent("adopted.json"))
    }
}
