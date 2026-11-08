import Foundation

/// What Satchel keeps on this iPhone, measured off the main thread for Settings > Storage.
struct StorageUsage: Sendable, Equatable {
    /// One game's share, by title ID.
    struct Game: Sendable, Equatable, Identifiable {
        let id: UInt64
        var imported: Int64 = 0
        var prepared: Int64 = 0
        var saves: Int64 = 0
        /// Shader caches, logs and launch records in the game's runtime folder.
        var caches: Int64 = 0
        var total: Int64 { imported + prepared + saves + caches }
    }
    /// Imported files (Documents/Homebrew).
    var imported: Int64 = 0
    /// Prepared package folders, staging folders excluded.
    var prepared: Int64 = 0
    var saves: Int64 = 0
    var caches: Int64 = 0
    /// Shader packs imported for Prepare (Documents/ShaderPacks).
    var shaderPacks: Int64 = 0
    /// Temporary folders left by a Prepare or Add DLC that was stopped before it finished.
    var leftovers: Int64 = 0
    var leftoverCount = 0
    var free: Int64?
    var packages: [String: Int64] = [:]
    /// Biggest first.
    var games: [Game] = []
}

/// File walks for Settings. Off the main thread only: a runtime folder can hold many files.
enum StorageMeter {
    /// Prepare stages in PreparedGames/.prepare-<UUID> (Pipeline.swift) and Add DLC in
    /// PreparedGames/.satchel-dlc-<UUID> (AddOnUpdate.swift). Both remove their own folder when
    /// they finish, fail or are cancelled, so one found while neither runs was left by a run
    /// the system ended.
    static let leftoverPrefixes = [".prepare-", ".satchel-dlc-"]

    static func isLeftover(_ name: String) -> Bool { leftoverPrefixes.contains { name.hasPrefix($0) } }

    static func size(of url: URL) -> Int64 {
        let keys: Set<URLResourceKey> = [.totalFileAllocatedSizeKey, .fileAllocatedSizeKey, .isRegularFileKey, .isDirectoryKey]
        guard let values = try? url.resourceValues(forKeys: keys) else { return 0 }
        func bytes(_ values: URLResourceValues) -> Int64 { Int64(values.totalFileAllocatedSize ?? values.fileAllocatedSize ?? 0) }
        if values.isDirectory != true { return values.isRegularFile == true ? bytes(values) : 0 }
        guard let walk = FileManager.default.enumerator(at: url, includingPropertiesForKeys: Array(keys)) else { return 0 }
        var total: Int64 = 0
        for case let file as URL in walk {
            total += autoreleasepool {
                guard let values = try? file.resourceValues(forKeys: keys), values.isRegularFile == true else { return 0 }
                return bytes(values)
            }
        }
        return total
    }

    /// Measures `documents`. `files` maps each imported file to its game's title ID, and
    /// `packages` each prepared folder to its title ID (both read from the library and artwork).
    static func measure(documents: URL, files: [(url: URL, title: UInt64?)], packages: [String: UInt64]) -> StorageUsage {
        let fm = FileManager.default
        var usage = StorageUsage()
        var games: [UInt64: StorageUsage.Game] = [:]
        func add(_ title: UInt64, _ change: (inout StorageUsage.Game) -> Void) {
            var game = games[title] ?? StorageUsage.Game(id: title); change(&game); games[title] = game
        }
        usage.imported = size(of: documents.appendingPathComponent("Homebrew", isDirectory: true))
        for file in files {
            guard let title = file.title else { continue }
            let bytes = size(of: file.url)
            add(title) { $0.imported += bytes }
        }
        let prepared = documents.appendingPathComponent("PreparedGames", isDirectory: true)
        for name in (try? fm.contentsOfDirectory(atPath: prepared.path)) ?? [] {
            let bytes = size(of: prepared.appendingPathComponent(name, isDirectory: true))
            if isLeftover(name) { usage.leftovers += bytes; usage.leftoverCount += 1; continue }
            guard !name.hasPrefix(".") else { continue }
            usage.packages[name] = bytes; usage.prepared += bytes
            if let title = packages[name] { add(title) { $0.prepared += bytes } }
        }
        let runtime = documents.appendingPathComponent("NativeRuntime", isDirectory: true)
        for name in (try? fm.contentsOfDirectory(atPath: runtime.path)) ?? [] {
            let folder = runtime.appendingPathComponent(name, isDirectory: true)
            var saves: Int64 = 0, caches: Int64 = 0
            for entry in (try? fm.contentsOfDirectory(atPath: folder.path)) ?? [] {
                let bytes = size(of: folder.appendingPathComponent(entry))
                if SaveStore.holdsSaves(entry) { saves += bytes } else { caches += bytes }
            }
            usage.saves += saves; usage.caches += caches
            if name.count == 16, let title = UInt64(name, radix: 16) { add(title) { $0.saves += saves; $0.caches += caches } }
        }
        usage.shaderPacks = size(of: documents.appendingPathComponent("ShaderPacks", isDirectory: true))
        usage.free = (try? documents.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]))?.volumeAvailableCapacityForImportantUsage
        usage.games = games.values.filter { $0.total > 0 }.sorted { $0.total == $1.total ? $0.id < $1.id : $0.total > $1.total }
        return usage
    }

    /// Deletes leftover staging folders only, never a package. The caller makes sure no
    /// Prepare or Add DLC runs. Returns what could not be removed.
    static func removeLeftovers(documents: URL) -> [String] {
        let fm = FileManager.default
        let prepared = documents.appendingPathComponent("PreparedGames", isDirectory: true)
        var failures: [String] = []
        for name in (try? fm.contentsOfDirectory(atPath: prepared.path)) ?? [] where isLeftover(name) {
            do { try fm.removeItem(at: prepared.appendingPathComponent(name, isDirectory: true)) }
            catch { failures.append(error.localizedDescription) }
        }
        return failures
    }
}
