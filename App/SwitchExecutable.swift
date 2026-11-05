import CryptoKit
import Foundation

struct SwitchProgramCandidate: Equatable {
    let titleID: String
    let contentID: String
}

struct SwitchExecutableModule {
    let name: String
    let data: Data
}

struct SwitchProgramImage {
    let titleID: String
    let modules: [SwitchExecutableModule]
    let processMetadata: Data?
    let exeFSBytes: UInt64
    let verifiedBlocks: Int
}

enum SwitchExecutableError: Error, LocalizedError {
    case missingKey(String), invalidKeys, invalid, integrity, unsupported, ambiguous, noProgram, limit
    case unrelatedUpdate, missingTicket, personalizedTicket
    case addOnRequiresUpdate(UInt32)
    case unsupportedFeature(String)

    var errorDescription: String? {
        switch self {
        case .addOnRequiresUpdate(let version): return "This DLC requires game content version \(version) or newer. Import and select its matching game update."
        case .unrelatedUpdate: return "The update's CNMT title ID or version does not match the selected base game."
        case .missingTicket: return "The selected package is missing the ticket for its rights-managed content."
        case .personalizedTicket: return "This personalized ticket requires console-specific RSA unwrapping, which is not supported yet."
        case .missingKey: return "Your prod.keys is missing a key this game needs. Export a fresh prod.keys from your own updated console and replace it in Settings > Console keys."
        case .invalidKeys: return "These keys could not read the game. Replace prod.keys from your own console in Settings > Console keys. If it still fails, check that the game dump is complete."
        case .invalid: return "The executable archive is truncated or malformed."
        case .integrity: return "Executable archive integrity verification failed."
        case .unsupported: return "This archive uses an encryption or storage feature the executable loader does not support yet."
        case .unsupportedFeature(let detail): return detail
        case .ambiguous: return "The package contains multiple Program archives with the same title ID."
        case .noProgram: return "The selected title has no loadable Program archive in this package."
        case .limit: return "The executable exceeds the bounded loader limits."
        }
    }
}

/// Read-only NCA3 -> verified ExeFS -> compressed NSO bytes, not game execution.
/// Layout references (independent implementation):
/// https://github.com/SciresM/hactool/blob/master/nca.h (ISC, SciresM)
/// https://github.com/SciresM/hactool/blob/master/pfs0.h (ISC, SciresM)
/// LibHac 0.19.0, MIT, Alex Barney, cd4004af194a51712812c680f0a028ba9152c0f8.
/// No external source is copied, no keys embedded, no signatures claimed verified.
/// The caller owns security scope and coordinated access for the whole operation.
enum SwitchExecutableReader {
    private static let maximumModule: UInt64 = 128 * 1024 * 1024
    private static let maximumImage: UInt64 = 512 * 1024 * 1024
    private static let maximumMetadata: UInt64 = 16 * 1024 * 1024

    /// The storage is valid only inside operation, while its coordinated source
    /// remains open. Reads verify IVFC blocks lazily; no full RomFS extraction.
    static func withRomFS<T>(_ url: URL, keys: [String: Data], titleID: String,
                            cancelled: @escaping () -> Bool = { false },
                            operation: (SwitchRomFS) throws -> T) throws -> T {
        try cancellation(cancelled)
        let file = try Source(url)
        defer { try? file.handle.close() }
        guard let nca = try headers(url, file: file, keys: keys, cancelled: cancelled)
            .first(where: { $0.titleID == titleID.lowercased() }) else { throw SwitchExecutableError.noProgram }
        guard nca.header[0x230..<0x240].allSatisfy({ $0 == 0 }),
              let section = nca.sections.first(where: { $0.index == 1 }),
              section.fs[2] == 0, section.fs[3] == 3, [1, 3].contains(section.fs[4]),
              section.fs[5] == 0, section.fs[0xe8..<0x140].allSatisfy({ $0 == 0 }),
              section.fs[0x148..<0x200].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        let sectionKey = try contentKey(nca, keys: keys)
        let storage = try SwitchIntegrityStorage(header: section.fs, sectionSize: section.size, cancelled: cancelled) { offset, size in
            try decryptedSection(file: file, nca: nca, section: section, key: sectionKey,
                                 offset: offset, size: size, cancelled: cancelled)
        }
        defer { storage.invalidate() }
        return try operation(SwitchRomFS(storage: storage, cancelled: cancelled))
    }

    static func contentKey(_ nca: Nca, keys: [String: Data], xts: Bool = false) throws -> Data {
        let generation = max(nca.header[0x206], nca.header[0x220])
        let revision = generation > 0 ? generation - 1 : 0
        let categories = ["application", "ocean", "system"]
        guard nca.header[0x207] < categories.count else { throw SwitchExecutableError.unsupported }
        let areaKey = try key("key_area_key_\(categories[Int(nca.header[0x207])])_\(String(format: "%02x", revision))", count: 16, keys: keys)
        return try crypt(nca.header.subdata(in: 0x300..<0x340), key: areaKey, mode: .ecb).subdata(in: xts ? 0..<32 : 32..<48)
    }

    static func programs(_ url: URL, keys: [String: Data], cancelled: () -> Bool = { false }) throws -> [SwitchProgramCandidate] {
        try cancellation(cancelled)
        let file = try Source(url)
        defer { try? file.handle.close() }
        return try headers(url, file: file, keys: keys, cancelled: cancelled).map {
            SwitchProgramCandidate(titleID: $0.titleID, contentID: $0.contentID)
        }
    }

    static func load(_ url: URL, keys: [String: Data], titleID: String,
                     cancelled: @escaping () -> Bool = { false }) throws -> SwitchProgramImage {
        try cancellation(cancelled)
        guard titleID.utf8.count == 16, titleID.utf8.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) }) else {
            throw SwitchExecutableError.noProgram
        }
        let file = try Source(url)
        defer { try? file.handle.close() }
        let candidates = try headers(url, file: file, keys: keys, cancelled: cancelled)
        guard let nca = candidates.first(where: { $0.titleID == titleID.lowercased() }) else { throw SwitchExecutableError.noProgram }
        // Recognize every title, but reject unsupported storage on the selected
        // title only. An unsupported second application must not hide the first.
        guard nca.header[0x230..<0x240].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        for section in nca.sections {
            guard [1, 3].contains(section.fs[4]), [2, 3].contains(section.fs[3]), section.fs[5] == 0,
                  section.fs[0x148..<0x200].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
        }
        guard let code = nca.sections.first(where: { $0.index == 0 }), code.fs[2] == 1,
              code.fs[3] == 2, code.fs.le32(0x2c) == 2 else { throw SwitchExecutableError.unsupported }
        let sectionKey = try contentKey(nca, keys: keys)
        let reader = VerifiedCode(file: file, nca: nca, section: code, key: sectionKey)
        try reader.initialize(cancelled: cancelled)
        return try loadCode(reader, titleID: nca.titleID, cancelled: cancelled)
    }

    static func loadCode(_ reader: VerifiedCode, titleID: String,
                         cancelled: () -> Bool) throws -> SwitchProgramImage {
        let pfsHeader = try reader.read(0, size: 16, cancelled: cancelled)
        guard pfsHeader.prefix(4) == Data("PFS0".utf8), pfsHeader.le32(12) == 0 else { throw SwitchExecutableError.invalid }
        let count = UInt64(pfsHeader.le32(4)), stringsSize = UInt64(pfsHeader.le32(8))
        guard count > 0, count <= 128, stringsSize <= maximumMetadata else { throw SwitchExecutableError.limit }
        let directorySize = 16 + count * 24 + stringsSize
        guard directorySize <= maximumMetadata, directorySize <= reader.pfsSize else { throw SwitchExecutableError.invalid }
        let directory = try reader.read(0, size: directorySize, cancelled: cancelled)
        let strings = directory.subdata(in: Int(16 + count * 24)..<Int(directorySize))
        var names = Set<String>(), entries: [(String, UInt64, UInt64)] = []
        var moduleBytes: UInt64 = 0
        for index in 0..<Int(count) {
            try cancellation(cancelled)
            let start = 16 + index * 24
            let relative = directory.le64(start), size = directory.le64(start + 8)
            let nameOffset = Int(directory.le32(start + 16))
            guard directory.le32(start + 20) == 0, relative <= reader.pfsSize - directorySize,
                  size <= reader.pfsSize - directorySize - relative, nameOffset < strings.count,
                  let end = strings[nameOffset..<min(strings.count, nameOffset + 256)].firstIndex(of: 0), end > nameOffset,
                  let name = String(data: strings[nameOffset..<end], encoding: .utf8),
                  !name.contains("/"), !name.contains("\\"), name != ".", name != "..",
                  !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }),
                  names.insert(name.lowercased()).inserted else { throw SwitchExecutableError.invalid }
            if isModule(name) {
                guard size >= 256, size <= maximumModule, size <= maximumImage - moduleBytes else { throw SwitchExecutableError.limit }
                moduleBytes += size
            }
            entries.append((name, directorySize + relative, size))
        }
        let extents = entries.filter { $0.2 > 0 }.sorted { $0.1 < $1.1 }
        for index in 1..<max(1, extents.count) {
            guard extents[index].1 >= extents[index - 1].1 + extents[index - 1].2 else { throw SwitchExecutableError.invalid }
        }
        guard entries.contains(where: { $0.0 == "main" }), entries.contains(where: { $0.0 == "rtld" }) else { throw SwitchExecutableError.noProgram }
        var modules: [SwitchExecutableModule] = []
        let selected = entries.filter { isModule($0.0) }.sorted { moduleOrder($0.0) < moduleOrder($1.0) }
        let extractionTotal = selected.reduce(UInt64(0)) { $0 + $1.2 }
        var extracted: UInt64 = 0
        defer { reader.readProgress = { _, _ in } }
        for (name, offset, size) in selected {
            reader.readProgress = { done, _ in reader.extractionProgress(extracted + done, extractionTotal) }
            let data = try reader.read(offset, size: size, cancelled: cancelled)
            guard data.prefix(4) == Data("NSO0".utf8) else { throw SwitchExecutableError.invalid }
            modules.append(SwitchExecutableModule(name: name, data: data))
            extracted += size
        }
        reader.readProgress = { _, _ in }
        var metadata: Data?
        if let entry = entries.first(where: { $0.0 == "main.npdm" }) {
            guard entry.2 >= 0x80, entry.2 <= 1024 * 1024 else { throw SwitchExecutableError.limit }
            metadata = try reader.read(entry.1, size: entry.2, cancelled: cancelled)
        }
        // Extracted bytes already passed their block hashes. Check every remaining
        // block, including non-module files, before exposing the program.
        try reader.verifyAll(cancelled: cancelled)
        try cancellation(cancelled)
        return SwitchProgramImage(titleID: titleID, modules: modules, processMetadata: metadata,
                                  exeFSBytes: reader.pfsSize, verifiedBlocks: Int(reader.blockCount))
    }

    /// Reads only metadata from the selected base Application. Source ownership
    /// and security-scoped coordination match load(); nothing is extracted.
    static func controlMetadata(_ url: URL, keys: [String: Data], titleID: String,
                                cancelled: @escaping () -> Bool = { false }) throws -> SwitchControlMetadata {
        try cancellation(cancelled)
        guard titleID.utf8.count == 16, let numericID = UInt64(titleID, radix: 16), numericID != 0 else {
            throw SwitchExecutableError.noProgram
        }
        let file = try Source(url); defer { try? file.handle.close() }
        let archives = try headers(url, file: file, keys: keys, includeMetadata: true, cancelled: cancelled)
        guard let program = archives.first(where: { $0.header[0x205] == 0 && $0.titleID == titleID.lowercased() }) else {
            throw SwitchExecutableError.noProgram
        }
        let metas = archives.filter { $0.header[0x205] == 1 && $0.titleID == program.titleID }
        guard metas.count == 1 else { throw metas.isEmpty ? SwitchExecutableError.invalid : SwitchExecutableError.ambiguous }
        let meta = metas[0]
        guard meta.entry.size <= 16 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        _ = try verifyArchiveHash(file, nca: meta, expected: nil, cancelled: cancelled)
        guard meta.sections.count == 1, let section = meta.sections.first,
              section.index == 0, section.fs[2] == 1, section.fs[3] == 2,
              section.fs.le32(0x2c) == 2 else { throw SwitchExecutableError.unsupported }
        try controlStorageSupported(meta, section)
        let metaKey = try contentKey(meta, keys: keys)
        let reader = VerifiedCode(file: file, nca: meta, section: section, key: metaKey)
        try reader.initialize(cancelled: cancelled)
        guard reader.pfsSize <= 16 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        let entries = try metadataEntries(reader, cancelled: cancelled)
        let cnmts = entries.filter { $0.name.hasSuffix(".cnmt") }
        guard cnmts.count == 1, cnmts[0].size <= 1024 * 1024 else { throw SwitchExecutableError.invalid }
        try reader.verifyAll(cancelled: cancelled)
        let bytes = try reader.read(cnmts[0].offset, size: cnmts[0].size, cancelled: cancelled)
        let record = try SwitchApplicationMeta.parse(bytes, metaTitleID: numericID)
        // Every declared base content must exist at its declared size. Only the
        // selected Control digest is additionally compared with its CNMT hash.
        for content in record.contents {
            guard let archive = archives.first(where: { $0.contentID == content.id }),
                  archive.entry.size == content.size else { throw SwitchExecutableError.invalid }
        }
        let selected = try record.selected(programID: program.contentID)
        guard selected.program.size == program.entry.size else { throw SwitchExecutableError.invalid }
        let controls = archives.filter { $0.contentID == selected.control.id }
        guard controls.count == 1 else { throw SwitchExecutableError.invalid }
        let control = controls[0]
        guard control.header[0x205] == 2, control.titleID == program.titleID,
              control.entry.size == selected.control.size else { throw SwitchExecutableError.invalid }
        guard control.entry.size <= 64 * 1024 * 1024 else { throw SwitchExecutableError.limit }
        _ = try verifyArchiveHash(file, nca: control, expected: selected.control.hash, cancelled: cancelled)
        guard control.sections.count == 1, let romSection = control.sections.first,
              romSection.index == 0, romSection.fs[2] == 0, romSection.fs[3] == 3 else {
            throw SwitchExecutableError.unsupported
        }
        try controlStorageSupported(control, romSection)
        let controlKey = try contentKey(control, keys: keys)
        let storage = try SwitchIntegrityStorage(header: romSection.fs, sectionSize: romSection.size, cancelled: cancelled) { offset, size in
            try decryptedSection(file: file, nca: control, section: romSection, key: controlKey,
                                 offset: offset, size: size, cancelled: cancelled)
        }
        defer { storage.invalidate() }
        let romfs = try SwitchRomFS(storage: storage, cancelled: cancelled)
        guard let nacpFile = try romfs.open("/control.nacp"), nacpFile.size == 0x4000 else { throw SwitchExecutableError.invalid }
        let values = try SwitchControlValues.parse(romfs.read(nacpFile, offset: 0, count: nacpFile.size))
        try cancellation(cancelled)
        return SwitchControlMetadata(applicationID: record.applicationID, applicationVersion: record.version,
            programContentID: program.contentID, controlContentID: control.contentID,
            parentalControlFlags: values.parentalFlags, ratingAges: values.ratings, addOnContentBaseID: values.addOnBaseID,
            metaHashBytes: meta.entry.size, controlHashBytes: control.entry.size,
            metaVerifiedBlocks: Int(reader.blockCount), controlVerifiedBlocks: romfs.verifiedBlocks)
    }

    private static func controlStorageSupported(_ nca: Nca, _ section: Section) throws {
        guard nca.header[0x230..<0x240].allSatisfy({ $0 == 0 }),
              [1, 3].contains(section.fs[4]), section.fs[5] == 0,
              section.fs[0xe8..<0x140].allSatisfy({ $0 == 0 }),
              section.fs[0x148..<0x200].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
    }

    static func verifyArchiveHash(_ file: Source, nca: Nca, expected: Data?, cancelled: () -> Bool, progress: SwitchByteProgress = { _, _ in }) throws -> Data {
        var hasher = SHA256(), offset: UInt64 = 0
        progress(0, nca.entry.size)
        while offset < nca.entry.size {
            try cancellation(cancelled)
            let count = min(UInt64(4 * 1024 * 1024), nca.entry.size - offset)
            try autoreleasepool {
                hasher.update(data: try file.read(nca.entry.offset + offset, size: count, cancelled: cancelled))
            }
            offset += count
            progress(offset, nca.entry.size)
        }
        let digest = Data(hasher.finalize())
        let id = digest.prefix(16).map { String(format: "%02x", $0) }.joined()
        guard id == nca.contentID, expected == nil || expected == digest else { throw SwitchExecutableError.integrity }
        return digest
    }

    struct MetadataFile { let name: String; let offset: UInt64; let size: UInt64 }
    static func metadataEntries(_ reader: VerifiedCode, cancelled: () -> Bool) throws -> [MetadataFile] {
        let head = try reader.read(0, size: 16, cancelled: cancelled)
        guard head.prefix(4) == Data("PFS0".utf8), head.le32(12) == 0 else { throw SwitchExecutableError.invalid }
        let count = UInt64(head.le32(4)), stringsSize = UInt64(head.le32(8))
        guard count > 0, count <= 128, stringsSize > 0, stringsSize <= 1024 * 1024 else { throw SwitchExecutableError.limit }
        let directorySize = 16 + count * 24 + stringsSize
        guard directorySize <= reader.pfsSize, directorySize <= 2 * 1024 * 1024 else { throw SwitchExecutableError.invalid }
        let directory = try reader.read(0, size: directorySize, cancelled: cancelled)
        let strings = directory.subdata(in: Int(16 + count * 24)..<Int(directorySize))
        var names = Set<String>(), result: [MetadataFile] = []
        for index in 0..<Int(count) {
            try cancellation(cancelled)
            let p = 16 + index * 24, nameOffset = Int(directory.le32(16 + index * 24 + 16))
            let offset = directory.le64(p), size = directory.le64(p + 8)
            guard directory.le32(p + 20) == 0, offset <= reader.pfsSize - directorySize,
                  size > 0, size <= reader.pfsSize - directorySize - offset,
                  nameOffset < strings.count,
                  let end = strings[nameOffset..<min(strings.count, nameOffset + 256)].firstIndex(of: 0), end > nameOffset,
                  let name = String(data: strings[nameOffset..<end], encoding: .utf8),
                  name != ".", name != "..", !name.contains("/"), !name.contains("\\"),
                  !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }),
                  names.insert(name.lowercased()).inserted else { throw SwitchExecutableError.invalid }
            result.append(MetadataFile(name: name, offset: directorySize + offset, size: size))
        }
        let ranges = result.sorted { $0.offset < $1.offset }
        for index in 1..<max(1, ranges.count) {
            guard ranges[index - 1].offset + ranges[index - 1].size <= ranges[index].offset else { throw SwitchExecutableError.invalid }
        }
        return result
    }

    private static func isModule(_ name: String) -> Bool {
        name == "rtld" || name == "main" || name == "sdk" ||
            (name.utf8.count == 7 && name.hasPrefix("subsdk") && (48...57).contains(name.utf8.last!))
    }

    private static func moduleOrder(_ name: String) -> Int {
        if name == "rtld" { return 0 }
        if name == "main" { return 1 }
        if name == "sdk" { return 12 }
        return 2 + Int(name.utf8.last! - 48)
    }

    private static func cancellation(_ cancelled: () -> Bool) throws {
        if cancelled() { throw CancellationError() }
    }

    static func key(_ name: String, count: Int, keys: [String: Data]) throws -> Data {
        guard let data = keys[name] else { throw SwitchExecutableError.missingKey(name) }
        guard data.count == count else { throw SwitchExecutableError.invalidKeys }
        return data
    }

    enum Mode { case ecb, xts, ctr(Data) }

    static func crypt(_ input: Data, key: Data, mode: Mode, firstSector: UInt64 = 0) throws -> Data {
        var output = Data(count: input.count)
        let status = output.withUnsafeMutableBytes { destination in
            input.withUnsafeBytes { source in
                key.withUnsafeBytes { material in
                    let k = material.bindMemory(to: UInt8.self).baseAddress!
                    let s = source.bindMemory(to: UInt8.self).baseAddress!
                    let d = destination.bindMemory(to: UInt8.self).baseAddress!
                    switch mode {
                    case .ecb: return satchel_switch_aes_ecb(0, k, s, d, input.count)
                    case .xts: return satchel_switch_aes_xts(0, k, firstSector, s, d, input.count)
                    case .ctr(let counter):
                        return counter.withUnsafeBytes { satchel_switch_aes_ctr(k, $0.bindMemory(to: UInt8.self).baseAddress!, s, d, input.count) }
                    }
                }
            }
        }
        guard status == 0 else { throw SwitchExecutableError.invalid }
        return output
    }

    struct Section {
        let index: Int
        let offset: UInt64
        let size: UInt64
        let fs: Data
    }
    struct Nca {
        let entry: SwitchPackageEntry
        let contentID: String
        let titleID: String
        let header: Data
        let sections: [Section]
    }

    static func headers(_ url: URL, file: Source, keys: [String: Data], includeMetadata: Bool = false, cancelled: () -> Bool, entries suppliedEntries: [SwitchPackageEntry]? = nil) throws -> [Nca] {
        let headerKey = try key("header_key", count: 32, keys: keys)
        let entries = try suppliedEntries ?? SwitchPackageReader.contentEntries(url, cancelled: cancelled)
        var result: [Nca] = [], contentIDs = Set<String>(), titleIDs = Set<String>()
        for entry in entries where entry.name.lowercased().hasSuffix(".nca") {
            try cancellation(cancelled)
            let parts = entry.name.lowercased().split(separator: ".", omittingEmptySubsequences: false)
            guard parts.count == 2 || (parts.count == 3 && parts[1] == "cnmt"),
                  parts[0].utf8.count == 32, parts[0].utf8.allSatisfy({ (48...57).contains($0) || (97...102).contains($0) }),
                  contentIDs.insert(String(parts[0])).inserted, entry.size >= 0xc00 else { throw SwitchExecutableError.invalid }
            let header = try crypt(file.read(entry.offset, size: 0xc00, cancelled: cancelled), key: headerKey, mode: .xts)
            guard header[0x200..<0x204] == Data("NCA3".utf8) else { throw SwitchExecutableError.unsupported }
            guard header.le64(0x208) == entry.size, header[0x204] <= 1, header[0x205] <= 5 else { throw SwitchExecutableError.invalid }
            var sections: [Section] = []
            for index in 0..<4 {
                let start = UInt64(header.le32(0x240 + index * 16)) * 512
                let end = UInt64(header.le32(0x244 + index * 16)) * 512
                if start == 0 && end == 0 { continue }
                guard start >= 0xc00, start < end else { throw SwitchExecutableError.invalid }
                let fs = header.subdata(in: (0x400 + index * 512)..<(0x600 + index * 512))
                guard Data(SHA256.hash(data: fs)) == header.subdata(in: (0x280 + index * 32)..<(0x2a0 + index * 32)) else {
                    throw SwitchExecutableError.integrity
                }
                guard fs.le16(0) == 2 else { throw SwitchExecutableError.unsupported }
                if fs.le16(0x170) == 0 {
                    guard end <= entry.size else { throw SwitchExecutableError.invalid }
                } else {
                    let physical = fs.le64(0x168), meta = fs.le64(0x148), size = fs.le64(0x150)
                    guard physical >= 0xc00, physical <= entry.size, meta <= entry.size - physical,
                          size <= entry.size - physical - meta else { throw SwitchExecutableError.invalid }
                }
                sections.append(Section(index: index, offset: start, size: end - start, fs: fs))
            }
            let sorted = sections.sorted { $0.offset < $1.offset }
            for index in 1..<max(1, sorted.count) {
                guard sorted[index].offset >= sorted[index - 1].offset + sorted[index - 1].size else { throw SwitchExecutableError.invalid }
            }
            if header[0x205] == 0 || includeMetadata {
                let id = String(format: "%016llx", header.le64(0x210))
                guard header.le64(0x210) != 0 else { throw SwitchExecutableError.invalid }
                if header[0x205] == 0 { guard titleIDs.insert(id).inserted else { throw SwitchExecutableError.ambiguous } }
                result.append(Nca(entry: entry, contentID: String(parts[0]), titleID: id, header: header, sections: sections))
            }
        }
        guard !result.isEmpty else { throw SwitchExecutableError.noProgram }
        return result.sorted { $0.titleID < $1.titleID }
    }

    final class Source {
        let handle: FileHandle
        private(set) var readBytes: UInt64 = 0
        private(set) var readCalls: UInt64 = 0
        private(set) var size: UInt64
        private var scratch: URL?
        private var attachments: [(UInt64, UInt64, FileHandle)] = []
        func scratchFile() throws -> URL {
            if scratch == nil {
                let root = FileManager.default.temporaryDirectory.appendingPathComponent("SatchelContent-" + UUID().uuidString, isDirectory: true)
                try FileManager.default.createDirectory(at: root, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
                scratch = root
            }
            return scratch!.appendingPathComponent(UUID().uuidString + ".nca")
        }
        func attach(_ url: URL, size bytes: UInt64) throws -> UInt64 {
            guard bytes <= UInt64.max - size else { throw SwitchExecutableError.limit }
            let start = size, file = try FileHandle(forReadingFrom: url)
            attachments.append((start, bytes, file)); size += bytes
            return start
        }
        deinit {
            for item in attachments { try? item.2.close() }
            if let scratch { try? FileManager.default.removeItem(at: scratch) }
        }
        init(_ url: URL) throws {
            handle = try FileHandle(forReadingFrom: url)
            do { size = try handle.seekToEnd() }
            catch { try? handle.close(); throw error }
        }
        func read(_ offset: UInt64, size count: UInt64, cancelled: () -> Bool) throws -> Data {
            try cancellation(cancelled)
            guard count <= maximumModule, offset <= size, count <= size - offset else { throw SwitchExecutableError.invalid }
            var handle = self.handle, at = offset
            if let item = attachments.first(where: { offset >= $0.0 && offset - $0.0 < $0.1 }) {
                guard count <= item.1 - (offset - item.0) else { throw SwitchExecutableError.invalid }
                handle = item.2; at = offset - item.0
            } else if let first = attachments.first {
                guard offset <= first.0, count <= first.0 - offset else { throw SwitchExecutableError.invalid }
            }
            try handle.seek(toOffset: at)
            var data = Data()
            data.reserveCapacity(Int(count))
            while data.count < count {
                try cancellation(cancelled)
                guard let part = try handle.read(upToCount: Int(min(4 * 1024 * 1024, count - UInt64(data.count)))), !part.isEmpty else { throw SwitchExecutableError.invalid }
                readBytes += UInt64(part.count); readCalls += 1
                data.append(part)
            }
            return data
        }
    }

    final class VerifiedCode {
        let section: Section
        let readSection: (UInt64, UInt64, () -> Bool) throws -> Data
        private let storedSize: UInt64
        private var expanded: SwitchContentStorage?
        var pfsSize: UInt64 { expanded?.size ?? storedSize }
        let pfsOffset: UInt64
        let blockSize: UInt64
        var blockCount: UInt64 { (storedSize + blockSize - 1) / blockSize }
        private var verified = Set<UInt64>()
        private var cachedBlock: (UInt64, Data)?
        var table = Data()
        var progress: SwitchByteProgress = { _, _ in }
        var extractionProgress: SwitchByteProgress = { _, _ in }
        var readProgress: SwitchByteProgress = { _, _ in }
        init(file: Source, nca: Nca, section: Section, key: Data) {
            self.section = section
            readSection = { offset, size, cancelled in
                try decryptedSection(file: file, nca: nca, section: section, key: key,
                                     offset: offset, size: size, cancelled: cancelled)
            }
            pfsOffset = section.fs.le64(0x40); storedSize = section.fs.le64(0x48)
            blockSize = UInt64(section.fs.le32(0x28))
        }
        init(section: Section, read: @escaping (UInt64, UInt64, () -> Bool) throws -> Data) {
            self.section = section; readSection = read
            pfsOffset = section.fs.le64(0x40); storedSize = section.fs.le64(0x48)
            blockSize = UInt64(section.fs.le32(0x28))
        }
        func initialize(cancelled: @escaping () -> Bool) throws {
            guard blockSize >= 512, blockSize <= maximumMetadata, blockSize.nonzeroBitCount == 1,
                  storedSize >= 16, storedSize <= maximumImage, pfsOffset <= section.size, storedSize <= section.size - pfsOffset else {
                throw SwitchExecutableError.invalid
            }
            let offset = section.fs.le64(0x30), size = section.fs.le64(0x38)
            guard size == blockCount * 32, size <= maximumMetadata, offset <= section.size, size <= section.size - offset,
                  offset + size <= pfsOffset || pfsOffset + storedSize <= offset else { throw SwitchExecutableError.invalid }
            table = try decrypted(offset, size: size, cancelled: cancelled)
            guard Data(SHA256.hash(data: table)) == section.fs.subdata(in: 8..<40) else { throw SwitchExecutableError.integrity }
            // LibHac Nca.OpenStorage verifies the stored hash layer before
            // applying CompressionInfo, for PartitionFS as well as RomFS.
            let raw = SwitchContentStorage(size: storedSize) { [unowned self] offset, count in
                try self.readVerified(offset, size: count, cancelled: cancelled)
            }
            expanded = try SwitchContentStorage.compressed(header: section.fs, verified: raw, cancelled: cancelled)
            guard pfsSize <= maximumImage else { throw SwitchExecutableError.limit }
        }
        func decrypted(_ offset: UInt64, size: UInt64, cancelled: () -> Bool) throws -> Data {
            try readSection(offset, size, cancelled)
        }
        func block(_ index: UInt64, cancelled: () -> Bool) throws -> Data {
            try cancellation(cancelled)
            if let cachedBlock, cachedBlock.0 == index { return cachedBlock.1 }
            let offset = index * blockSize
            guard index < blockCount else { throw SwitchExecutableError.invalid }
            let bytes = try decrypted(pfsOffset + offset, size: min(blockSize, storedSize - offset), cancelled: cancelled)
            guard Data(SHA256.hash(data: bytes)) == table.subdata(in: Int(index * 32)..<Int(index * 32 + 32)) else {
                throw SwitchExecutableError.integrity
            }
            verified.insert(index)
            cachedBlock = (index, bytes)
            return bytes
        }
        func read(_ offset: UInt64, size: UInt64, cancelled: () -> Bool) throws -> Data {
            try cancellation(cancelled)
            guard size > 0, size <= maximumModule, offset <= pfsSize, size <= pfsSize - offset,
                  let expanded else { throw SwitchExecutableError.invalid }
            var result = Data(), done: UInt64 = 0
            result.reserveCapacity(Int(size))
            while done < size {
                try cancellation(cancelled)
                let take = min(16 * 1024 * 1024, size - done)
                result.append(try expanded.bytes(offset + done, take)); done += take
                readProgress(done, size)
            }
            return result
        }
        private func readVerified(_ offset: UInt64, size: UInt64, cancelled: () -> Bool) throws -> Data {
            guard size > 0, size <= maximumModule, offset <= storedSize, size <= storedSize - offset else { throw SwitchExecutableError.invalid }
            var result = Data(); result.reserveCapacity(Int(size))
            var position = offset
            while position < offset + size {
                let bytes = try block(position / blockSize, cancelled: cancelled)
                let start = Int(position % blockSize), count = min(bytes.count - start, Int(offset + size - position))
                result.append(bytes[start..<(start + count)]); position += UInt64(count)
            }
            return result
        }
        func verifyAll(cancelled: () -> Bool) throws {
            for index in 0..<blockCount {
                try cancellation(cancelled)
                if !verified.contains(index) { _ = try block(index, cancelled: cancelled) }
                progress(min((index + 1) * blockSize, storedSize), storedSize)
            }
        }
    }

    static func decryptedSection(file: Source, nca: Nca, section: Section, key: Data,
                                         offset: UInt64, size: UInt64, cancelled: () -> Bool) throws -> Data {
        guard offset <= section.size, size <= section.size - offset else { throw SwitchExecutableError.invalid }
        if section.fs[4] == 1 { return try file.read(nca.entry.offset + section.offset + offset, size: size, cancelled: cancelled) }
        let absolute = section.offset + offset, aligned = absolute & ~UInt64(15), skip = absolute - aligned
        guard size <= UInt64.max - skip else { throw SwitchExecutableError.invalid }
        let encrypted = try file.read(nca.entry.offset + aligned, size: size + skip, cancelled: cancelled)
        var counter = Data(section.fs[0x140..<0x148].reversed())
        let value = aligned / 16
        for shift in (0..<8).reversed() { counter.append(UInt8(truncatingIfNeeded: value >> (shift * 8))) }
        let plain = try crypt(encrypted, key: key, mode: .ctr(counter))
        return plain.subdata(in: Int(skip)..<plain.count)
    }
}

private extension Data {
    func le16(_ offset: Int) -> UInt16 { UInt16(self[offset]) | UInt16(self[offset + 1]) << 8 }
    func le32(_ offset: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[offset + $1]) << (8 * $1) } }
    func le64(_ offset: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[offset + $1]) << (8 * $1) } }
}
