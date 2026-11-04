import Foundation

/// Metadata available without Control data. Add-on content normally has no title string.
struct SwitchAddOnInfo: Codable, Equatable, Identifiable {
    let titleID: UInt64
    let applicationID: UInt64
    let version: UInt32
    let requiredApplicationVersion: UInt32
    let bytes: UInt64
    var id: UInt64 { titleID }
    var name: String { "DLC " + String(format: "%016llX", titleID) }

    init(_ meta: SwitchContentMeta) {
        titleID = meta.titleID; applicationID = meta.applicationID; version = meta.version
        requiredApplicationVersion = meta.requiredApplicationVersion
        bytes = meta.contents.reduce(0) { $0 + $1.size }
    }
}

enum SwitchAddOnContent {
    /// The pinned engine accepts indices 1 through 2000. Reject unsupported indices explicitly.
    static func index(titleID: UInt64, applicationID: UInt64) throws -> Int {
        guard applicationID <= UInt64.max - 0x1000, titleID > applicationID + 0x1000,
              titleID - applicationID - 0x1000 <= 2000 else { throw SwitchExecutableError.unsupported }
        return Int(titleID - applicationID - 0x1000)
    }

    static func verifiedMetadata(_ package: SwitchExecutableReader.ContentPackage) throws -> [SwitchContentMeta] {
        let metas = package.metadata.filter { $0.type == 0x82 }
        for meta in metas {
            for ref in meta.contents {
                guard let nca = package.archives.first(where: { $0.contentID == ref.id }) else { throw SwitchExecutableError.invalid }
                try package.verify(nca, in: meta)
            }
        }
        return metas
    }

    static func withRomFS<T>(_ package: SwitchExecutableReader.ContentPackage, meta: SwitchContentMeta,
                            operation: (SwitchRomFS) throws -> T) throws -> T {
        guard meta.type == 0x82 else { throw SwitchExecutableError.unsupported }
        let refs = meta.contents.filter { $0.type == 2 }
        guard refs.count == 1, refs[0].idOffset == 0,
              let nca = package.archives.first(where: { $0.contentID == refs[0].id }),
              [4, 5].contains(nca.header[0x205]), nca.header.cp64(0x210) == meta.titleID,
              nca.sections.count == 1, let section = nca.sections.first,
              section.index == 0, section.fs[2] == 0, section.fs[3] == 3,
              section.fs[0x100..<0x140].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        for ref in meta.contents {
            guard let archive = package.archives.first(where: { $0.contentID == ref.id }) else { throw SwitchExecutableError.invalid }
            try package.verify(archive, in: meta)
        }
        let raw = try package.storage(nca, section: section)
        let verified = try SwitchIntegrityStorage(header: section.fs, sectionSize: raw.size,
            cancelled: package.cancelled, read: raw.bytes)
        defer { verified.invalidate() }
        return try operation(SwitchRomFS(storage: verified, cancelled: package.cancelled))
    }

    /// Streams DLC directly into the unpublished package. Callers own staging cleanup on failure.
    static func write(sources: [URL], applicationID: UInt64, applicationVersion: UInt32,
                      keys: [String: Data], to stage: URL, cancelled: @escaping () -> Bool, reserveBytes: UInt64 = 1024 * 1024 * 1024,
                      progress: @escaping (UInt64, UInt64) -> Void = { _, _ in }) throws -> [PreparedAddOn] {
        var candidates: [UInt64: (URL, SwitchContentMeta)] = [:]
        for url in Array(Set(sources)).sorted(by: { $0.path < $1.path }) {
            let package = try SwitchExecutableReader.ContentPackage(url, keys: keys, cancelled: cancelled)
            defer { package.close() }
            for meta in package.metadata where meta.type == 0x82 && meta.applicationID == applicationID {
                if let previous = candidates[meta.titleID] {
                    if meta.version < previous.1.version { continue }
                    if meta.version == previous.1.version {
                        guard meta.contents.map(\.hash) == previous.1.contents.map(\.hash),
                              meta.requiredApplicationVersion == previous.1.requiredApplicationVersion,
                              meta.dataPatchID == previous.1.dataPatchID else { throw SwitchExecutableError.ambiguous }
                        continue
                    }
                }
                candidates[meta.titleID] = (url, meta)
            }
        }
        let directory = stage.appendingPathComponent("aoc", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
        var result: [PreparedAddOn] = []
        for (_, candidate) in candidates.sorted(by: { $0.key < $1.key }) {
            let (url, meta) = candidate
            guard meta.requiredApplicationVersion <= applicationVersion else { throw SwitchExecutableError.addOnRequiresUpdate(meta.requiredApplicationVersion) }
            let index = try index(titleID: meta.titleID, applicationID: applicationID)
            let package = try SwitchExecutableReader.ContentPackage(url, keys: keys, cancelled: cancelled)
            defer { package.close() }
            guard let current = package.metadata.first(where: { $0.titleID == meta.titleID && $0.type == 0x82 && $0.version == meta.version }),
                  current.version == meta.version, current.contents.map(\.hash) == meta.contents.map(\.hash),
                  current.requiredApplicationVersion == meta.requiredApplicationVersion,
                  current.applicationID == applicationID, current.dataPatchID == meta.dataPatchID else { throw SwitchExecutableError.integrity }
            let path = "aoc/\(index).img"
            let image = try withRomFS(package, meta: current) { rom in
                let tree = try rom.tree(cancelled: cancelled)
                let required = tree.reduce(UInt64(32)) { $0 + ($1.file?.size ?? 0) + 24 + UInt64($1.name.count) }
                let free = (try FileManager.default.attributesOfFileSystem(forPath: stage.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
                guard free >= required && free - required >= reserveBytes else { throw SwitchExecutableError.limit }
                return try SwitchRomFSImage.write(rom, to: stage.appendingPathComponent(path), cancelled: cancelled) {
                    progress($0.imageCompletedBytes, $0.imageTotalBytes)
                }
            }
            result.append(PreparedAddOn(titleID: String(format: "%016llX", meta.titleID),
                applicationID: String(format: "%016llX", applicationID), version: meta.version,
                requiredApplicationVersion: meta.requiredApplicationVersion, index: index, path: path,
                bytes: image.imageBytes, sha256: image.sha256.cpHex))
        }
        return result
    }
}
