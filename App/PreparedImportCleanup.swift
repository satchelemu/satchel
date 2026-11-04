import Foundation

struct PreparedImportSource: Codable, Equatable {
    let titleID: UInt64
    let package: String
    let isUpdate: Bool
}

/// Only local imports and the fully copied, dedicated shader pack are cleanup candidates.
/// Saves, artwork, prepared packages and the user's source in Files are outside these roots.
enum PreparedImportCleanup {
    static let settingKey = "SatchelRemoveOriginalsAfterPrepare"
    static func enabled(in defaults: UserDefaults = .standard) -> Bool {
        defaults.object(forKey: settingKey) == nil || defaults.bool(forKey: settingKey)
    }

    struct Candidate {
        let url: URL
        let snapshot: [String: String]
        let parent: URL
    }
    struct Plan {
        let candidates: [Candidate]
        func remove() -> [String] {
            var failures: [String] = []
            for candidate in candidates {
                do {
                    let parent = candidate.url.deletingLastPathComponent()
                    let values = try parent.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                    guard values.isDirectory == true, values.isSymbolicLink != true,
                          parent.resolvingSymlinksInPath() == candidate.parent,
                          try PreparedImportCleanup.snapshot(candidate.url) == candidate.snapshot else {
                        throw failure("An original changed during cleanup and was kept.")
                    }
                    try FileManager.default.removeItem(at: candidate.url)
                } catch { failures.append(error.localizedDescription) }
            }
            return failures
        }
    }

    static func plan(documents: URL, originals: [URL], shaderPack: URL?, cancelled: () -> Bool = { false }) throws -> Plan {
        if cancelled() { throw CancellationError() }
        let fm = FileManager.default
        let homebrew = documents.appendingPathComponent("Homebrew", isDirectory: true)
        let packs = documents.appendingPathComponent("ShaderPacks", isDirectory: true)
        func requireParent(_ parent: URL, child: URL) throws {
            let values = try parent.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
            guard values.isDirectory == true, values.isSymbolicLink != true,
                  child.standardizedFileURL.deletingLastPathComponent() == parent.standardizedFileURL,
                  child.resolvingSymlinksInPath().deletingLastPathComponent() == parent.resolvingSymlinksInPath() else {
                throw failure("Cleanup refused a path outside the import folders.")
            }
        }
        var candidates: [Candidate] = []
        for file in Set(originals) {
            if cancelled() { throw CancellationError() }
            try requireParent(homebrew, child: file)
            let id = file.deletingPathExtension().lastPathComponent
            guard id.count == 64, id.allSatisfy({ "0123456789abcdef".contains($0) }),
                  ["xci", "nsp", "xcz", "nsz"].contains(file.pathExtension) else {
                throw failure("Cleanup refused a file that is not an imported game.")
            }
            if !fm.fileExists(atPath: file.path) { continue }
            let values = try file.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            guard values.isRegularFile == true, values.isSymbolicLink != true else {
                throw failure("Cleanup refused an import that is not a regular file.")
            }
            candidates.append(Candidate(url: file, snapshot: try snapshot(file), parent: homebrew.resolvingSymlinksInPath()))
        }
        // Packages compiled on first use have no shader pack to reclaim.
        guard let shaderPack else { return Plan(candidates: candidates) }
        try requireParent(packs, child: shaderPack)
        let packSnapshot = try snapshot(shaderPack, cancelled: cancelled)
        // Extra files mean the folder is shared or has another purpose. Keep it intact.
        let manifestURL = shaderPack.appendingPathComponent("shader-pack.json")
        guard (try manifestURL.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) < 32 * 1024 * 1024 else {
            throw failure("Shader pack manifest exceeds limit.")
        }
        let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: manifestURL)) as? [String: Any]
        if let files = manifest?["files"] as? [String: Any] {
            let allowed = Set(files.keys).union(["shader-pack.json"])
            let regular = Set(packSnapshot.keys.filter { packSnapshot[$0]?.hasPrefix("NSFileTypeRegular:") == true })
            if regular == allowed {
                candidates.append(Candidate(url: shaderPack, snapshot: packSnapshot, parent: packs.resolvingSymlinksInPath()))
            }
        }
        return Plan(candidates: candidates)
    }

    /// Include file identities and every descendant, rejecting links before any deletion.
    private static func snapshot(_ url: URL, cancelled: () -> Bool = { false }) throws -> [String: String] {
        let fm = FileManager.default
        func fingerprint(_ file: URL) throws -> String {
            let attrs = try fm.attributesOfItem(atPath: file.path)
            guard let type = attrs[.type] as? FileAttributeType,
                  type == .typeRegular || type == .typeDirectory else {
                throw failure("Cleanup refused a symlink or special file.")
            }
            return "\(type.rawValue):\(attrs[.systemNumber] ?? 0):\(attrs[.systemFileNumber] ?? 0):\(attrs[.size] ?? 0):\((attrs[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0)"
        }
        var result = ["": try fingerprint(url)]
        if result[""]!.hasPrefix("NSFileTypeDirectory:") {
            guard let enumerator = fm.enumerator(atPath: url.path) else { throw failure("Cannot inspect the shader pack for cleanup.") }
            while let name = enumerator.nextObject() as? String {
                if cancelled() { throw CancellationError() }
                guard result.count < 500_000 else { throw failure("Shader pack directory exceeds limit.") }
                result[name] = try fingerprint(url.appendingPathComponent(name))
            }
        }
        return result
    }

    private static func failure(_ message: String) -> NSError {
        NSError(domain: "PreparedImportCleanup", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
    }
}
