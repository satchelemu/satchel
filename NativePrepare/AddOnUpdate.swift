import Darwin
import Foundation

/// Data-only transaction. The signed Mach-O bytes and their bindings are never changed.
enum AddOnUpdate {
    enum Phase: String { case checking, cloning, extracting, binding, verifying, publishing }

    @discardableResult static func run(root: URL, sources: [URL], keys: [String: Data], token: PrepareCancellation,
                                      profiles: [NativeGameProfile] = NativeGameProfile.all,
                                      reserveBytes: UInt64 = 1024 * 1024 * 1024,
                                      progress: (Phase) throws -> Void = { _ in },
                                      bytes: @escaping (UInt64, UInt64) -> Void = { _, _ in }) throws -> Int {
        let fm = FileManager.default
        try PreparedPackageAccess.begin(); defer { PreparedPackageAccess.end() }
        guard !SatchelNativeSessionStarted(), PreparedLaunch.current == nil else { throw PrepareFailure(message: "Quit the game before adding DLC.") }
        try token.check()
        try progress(.checking)
        let values = try root.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else {
            throw PrepareFailure(message: "The prepared package is not a regular folder.")
        }
        _ = try PreparedPackage.verify(root: root, profiles: profiles, cancelled: { token.cancelled })
        let manifestURL = root.appendingPathComponent("manifest.json")
        let original = try Data(contentsOf: manifestURL)
        guard var manifest = try JSONSerialization.jsonObject(with: original) as? [String: Any],
              let title = manifest["titleID"] as? String, let application = UInt64(title, radix: 16),
              let version = manifest["contentVersion"] as? UInt32,
              var files = manifest["files"] as? [String: Any] else {
            throw PrepareFailure(message: "This package has no authenticated content version. Prepare it once with the current app before adding DLC.")
        }
        let previous = PreparedAddOn.read(from: root)
        let parent = root.deletingLastPathComponent()
        // Only this transaction's private scratch folders are reclaimed after an interruption.
        for abandoned in try fm.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil)
            where abandoned.lastPathComponent.hasPrefix(".satchel-dlc-") {
            try? fm.removeItem(at: PreparedPackage.stampURL(for: abandoned.appendingPathComponent("package")))
            try fm.removeItem(at: abandoned)
        }
        let scratch = parent.appendingPathComponent(".satchel-dlc-" + UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: scratch, withIntermediateDirectories: false)
        let stage = scratch.appendingPathComponent("package", isDirectory: true)
        defer {
            try? fm.removeItem(at: scratch)
            try? fm.removeItem(at: PreparedPackage.stampURL(for: stage))
        }
        func reserve() throws {
            let free = (try fm.attributesOfFileSystem(forPath: parent.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
            guard free >= reserveBytes else { throw SwitchExecutableError.limit }
        }
        try reserve()
        try progress(.cloning)
        try token.check()
        // APFS copy-on-write gives new inodes without duplicating the base, update or shaders.
        // Refuse a non-cloning filesystem instead of silently copying tens of gigabytes.
        guard clonefile(root.path, stage.path, UInt32(CLONE_NOFOLLOW)) == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        try token.check()
        let incoming = scratch.appendingPathComponent("incoming", isDirectory: true)
        try fm.createDirectory(at: incoming, withIntermediateDirectories: false)
        try progress(.extracting)
        let additions = try SwitchAddOnContent.write(sources: sources, applicationID: application,
            applicationVersion: version, keys: keys, to: incoming, cancelled: { token.cancelled },
            reserveBytes: reserveBytes, progress: bytes)
        guard !additions.isEmpty else { throw PrepareFailure(message: "No matching DLC was found in these imports.") }
        var merged = Dictionary(uniqueKeysWithValues: previous.map { ($0.titleID, $0) })
        var count = 0
        try progress(.binding)
        try fm.createDirectory(at: stage.appendingPathComponent("aoc"), withIntermediateDirectories: true)
        for row in additions {
            try token.check()
            if let old = merged[row.titleID] {
                if old.version > row.version { continue }
                if old.version == row.version {
                    guard old == row else { throw SwitchExecutableError.ambiguous }
                    continue
                }
            }
            let destination = stage.appendingPathComponent(row.path)
            if fm.fileExists(atPath: destination.path) { try fm.removeItem(at: destination) }
            try fm.moveItem(at: incoming.appendingPathComponent(row.path), to: destination)
            merged[row.titleID] = row
            files[row.path] = ["bytes": row.bytes, "sha256": row.sha256]
            count += 1
        }
        try token.check()
        guard count > 0 else { return 0 }
        manifest["addOnContent"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(merged.values.sorted { $0.index < $1.index }))
        manifest["files"] = files
        try JSONSerialization.data(withJSONObject: manifest, options: [.sortedKeys, .prettyPrinted])
            .write(to: stage.appendingPathComponent("manifest.json"), options: .atomic)
        try progress(.verifying)
        _ = try PreparedPackage.verify(root: stage, profiles: profiles, forceFull: true, cancelled: { token.cancelled })
        try reserve()
        try progress(.publishing)
        // Refuse a concurrently replaced source, even when it was changed outside Satchel.
        guard try Data(contentsOf: manifestURL) == original else {
            throw PrepareFailure(message: "The prepared package changed while DLC was being added. Try again.")
        }
        try token.beginFinalizing()
        guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, root.path, UInt32(RENAME_SWAP)) == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        // Publication is committed. Cache writes and scratch cleanup cannot relabel it a failure.
        // File fingerprints survive the rename. A failed cache write simply causes full verification.
        if let stamp = try? Data(contentsOf: PreparedPackage.stampURL(for: stage)) {
            try? stamp.write(to: PreparedPackage.stampURL(for: root), options: .atomic)
        }
        return count
    }
}
