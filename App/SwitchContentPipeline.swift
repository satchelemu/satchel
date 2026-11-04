import CryptoKit
import Foundation

struct SwitchContentSelection {
    let applicationID: UInt64
    let baseVersion: UInt32
    let patchVersion: UInt32?
    let programContentID: String
}

/// Synchronous native preparation API. Call on a background queue while the
/// caller holds coordinated, security-scoped access to immutable imports.
/// Sources and verified storage are valid only inside operation. Errors contain
/// no keys, paths, ticket contents or game bytes. No publisher signature claim.
enum SwitchContentPipeline {
    enum Phase { case identification, authentication, composition }
    typealias Progress = (Phase, UInt64, UInt64, Double) -> Void

    struct PreparedContent {
        let selection: SwitchContentSelection
        let program: SwitchProgramImage
        let romFS: SwitchRomFS
    }

    /// Inspect authenticated CNMTs for selection without hashing/extracting the
    /// Program archives. withContent performs those additional checks.
    static func select(base: URL, update: URL? = nil, keys: [String: Data],
                       applicationID: UInt64, programIndex: UInt8 = 0,
                       cancelled: @escaping () -> Bool = { false }) throws -> SwitchContentSelection {
        let original = try SwitchExecutableReader.ContentPackage(base, keys: keys, cancelled: cancelled)
        defer { original.close() }
        let patch = try update.map { try SwitchExecutableReader.ContentPackage($0, keys: keys, cancelled: cancelled) }
        defer { patch?.close() }
        let (baseMeta, patchMeta) = try SwitchContentMeta.select(base: original.metadata,
            update: patch?.metadata, applicationID: applicationID)
        _ = try original.program(baseMeta, index: programIndex)
        let chosen = try (patch ?? original).program(patchMeta ?? baseMeta, index: programIndex)
        return SwitchContentSelection(applicationID: applicationID, baseVersion: baseMeta.version,
                                      patchVersion: patchMeta?.version, programContentID: chosen.contentID)
    }

    static func withContent<T>(base: URL, update: URL? = nil, keys: [String: Data],
                               applicationID: UInt64, programIndex: UInt8 = 0,
                               cancelled: @escaping () -> Bool = { false },
                               progress: @escaping Progress = { _, _, _, _ in },
                               identified: ([String]) throws -> Void = { _ in },
                               operation: (PreparedContent) throws -> T) throws -> T {
        progress(.identification, 0, 0, 0)
        let basePackage = try SwitchExecutableReader.ContentPackage(base, keys: keys, cancelled: cancelled) { done, total in
            progress(.identification, done, total, total == 0 ? 0 : min(0.49, Double(done) / Double(total) * 0.49))
        }
        defer { basePackage.close() }
        let patchPackage = try update.map { url in
            try SwitchExecutableReader.ContentPackage(url, keys: keys, cancelled: cancelled) { done, total in
                progress(.identification, done, total, 0.5 + (total == 0 ? 0 : Double(done) / Double(total) * 0.49))
            }
        }
        defer { patchPackage?.close() }
        try identified(basePackage.metadata.filter { $0.type == 0x80 }.map { String(format: "%016llx", $0.applicationID) })
        let (baseMeta, patchMeta) = try SwitchContentMeta.select(base: basePackage.metadata,
            update: patchPackage?.metadata, applicationID: applicationID)
        let original = try basePackage.program(baseMeta, index: programIndex)
        let chosenPackage = patchPackage ?? basePackage, chosenMeta = patchMeta ?? baseMeta
        let chosen = try chosenPackage.program(chosenMeta, index: programIndex)
        // Full encrypted Program hashes bind every BKTR table as well as data to
        // the selected CNMT. Hash checks remain streaming and cancellable.
        let hashBytes = original.entry.size + (patchPackage == nil ? 0 : chosen.entry.size)
        progress(.authentication, 0, hashBytes, 0)
        try basePackage.verify(original, in: baseMeta) { done, _ in progress(.authentication, done, hashBytes, Double(done) / Double(hashBytes)) }
        if patchPackage != nil {
            try chosenPackage.verify(chosen, in: chosenMeta) { done, _ in progress(.authentication, original.entry.size + done, hashBytes, Double(original.entry.size + done) / Double(hashBytes)) }
        }
        progress(.composition, 0, 0, 0)
        func section(_ index: Int) throws -> (SwitchExecutableReader.Section, SwitchContentStorage) {
            guard let selected = chosen.sections.first(where: { $0.index == index }) else { throw SwitchExecutableError.noProgram }
            let report: SwitchByteProgress = { done, total in
                let value = total == 0 ? 0 : Double(done) / Double(total)
                progress(.composition, done, total, (index == 0 ? 0 : 0.8) + value * 0.05)
            }
            let raw = try chosenPackage.storage(chosen, section: selected, progress: report)
            if selected.fs.cp64(0x108) == 0 { return (selected, raw) }
            guard patchPackage != nil else { throw SwitchExecutableError.unsupportedFeature("An indirect storage table requires a matching base and update pair.") }
            let source: SwitchContentStorage
            if let baseSection = original.sections.first(where: { $0.index == index }) {
                source = try basePackage.storage(original, section: baseSection, progress: report)
            } else { source = SwitchContentStorage(size: 0) { _, _ in throw SwitchExecutableError.invalid } }
            let tableOffset = selected.fs.cp64(0x100), tableSize = selected.fs.cp64(0x108)
            guard tableOffset <= raw.size, tableSize <= raw.size - tableOffset else { throw SwitchExecutableError.invalid }
            let tree = try SwitchBucketTree(header: selected.fs.subdata(in: 0x110..<0x120), size: tableSize,
                entrySize: 20, cancelled: cancelled, progress: report) { offset, size in try raw.bytes(tableOffset + offset, size) }
            let composed = try SwitchContentStorage.indirect(tree, base: source, patch: raw, cancelled: cancelled)
            return (SwitchExecutableReader.Section(index: selected.index, offset: selected.offset,
                                                  size: composed.size, fs: selected.fs), composed)
        }
        let (code, codeStorage) = try section(0)
        guard code.fs[2] == 1, code.fs[3] == 2, code.fs.cp32(0x2c) == 2 else {
            throw SwitchExecutableError.unsupportedFeature("Unsupported ExeFS layout: filesystem \(code.fs[2]), hash type \(code.fs[3]), hash layers \(code.fs.cp32(0x2c)).")
        }
        let reader = SwitchExecutableReader.VerifiedCode(section: code) { offset, count, check in
            if check() { throw CancellationError() }
            return try codeStorage.bytes(offset, count)
        }
        reader.progress = { done, total in progress(.composition, done, total, 0.1 + 0.4 * Double(done) / Double(max(1, total))) }
        reader.extractionProgress = { done, total in progress(.composition, done, total, 0.5 + 0.3 * Double(done) / Double(max(1, total))) }
        try reader.initialize(cancelled: cancelled)
        let program = try autoreleasepool {
            try SwitchExecutableReader.loadCode(reader, titleID: String(format: "%016llx", applicationID), cancelled: cancelled)
        }
        let (rom, romStorage) = try section(1)
        guard rom.fs[2] == 0, rom.fs[3] == 3 else {
            throw SwitchExecutableError.unsupportedFeature("Unsupported RomFS layout: filesystem \(rom.fs[2]), hash type \(rom.fs[3]).")
        }
        let verified = try SwitchIntegrityStorage(header: rom.fs, sectionSize: romStorage.size,
                                                  cancelled: cancelled, read: romStorage.bytes)
        defer { verified.invalidate() }
        let fs = try SwitchRomFS(storage: verified, cancelled: cancelled) { done, total in
            progress(.composition, done, total, 0.9 + 0.09 * Double(done) / Double(max(1, total)))
        }
        return try operation(PreparedContent(selection: SwitchContentSelection(applicationID: applicationID,
            baseVersion: baseMeta.version, patchVersion: patchMeta?.version, programContentID: chosen.contentID),
            program: program, romFS: fs))
    }
}

extension SwitchExecutableReader {
    final class ContentPackage {
        let source: Source
        let archives: [Nca]
        let keys: [String: Data]
        let cancelled: () -> Bool
        private var authenticated = Set<String>()
        private var tickets: [Data: Data] = [:]
        private(set) var metadata: [SwitchContentMeta] = []

        init(_ url: URL, keys: [String: Data], cancelled: @escaping () -> Bool, progress: SwitchByteProgress = { _, _ in }) throws {
            source = try Source(url); self.keys = keys; self.cancelled = cancelled
            do {
                let transport = try SwitchPackageReader.contentEntries(url, cancelled: cancelled, progress: progress)
                let entries = try SwitchNCZ.entries(transport, source: source, keys: keys, cancelled: cancelled, progress: progress)
                archives = try headers(url, file: source, keys: keys, includeMetadata: true, cancelled: cancelled, entries: entries)
                for entry in entries where entry.name.lowercased().hasSuffix(".tik") {
                    guard entry.size <= 64 * 1024 else { throw SwitchExecutableError.limit }
                    let bytes = try source.read(entry.offset, size: entry.size, cancelled: cancelled)
                    let ticket = try Self.ticket(bytes)
                    guard tickets[ticket.rights] == nil else { throw SwitchExecutableError.ambiguous }
                    tickets[ticket.rights] = ticket.encryptedKey
                }
                for meta in archives where meta.header[0x205] == 1 {
                    guard meta.entry.size <= 16 * 1024 * 1024, meta.sections.count == 1,
                          let section = meta.sections.first, section.index == 0, section.fs[2] == 1,
                          section.fs[3] == 2, section.fs.cp32(0x2c) == 2,
                          section.fs[0x100..<0x140].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.unsupported }
                    _ = try verifyArchiveHash(source, nca: meta, expected: nil, cancelled: cancelled)
                    authenticated.insert(meta.contentID)
                    let raw = try storage(meta, section: section)
                    let reader = VerifiedCode(section: section) { offset, count, _ in try raw.bytes(offset, count) }
                    try reader.initialize(cancelled: cancelled)
                    let files = try metadataEntries(reader, cancelled: cancelled).filter { $0.name.hasSuffix(".cnmt") }
                    guard files.count == 1 else { throw SwitchExecutableError.invalid }
                    try reader.verifyAll(cancelled: cancelled)
                    let parsed = try SwitchContentMeta.parse(reader.read(files[0].offset, size: files[0].size, cancelled: cancelled),
                                                            metaTitleID: meta.header.cp64(0x210))
                    for content in parsed.contents {
                        let present = archives.first(where: { $0.contentID == content.id })
                        if present == nil && parsed.type == 0x81 && content.type == 6 { continue }
                        guard let present, present.entry.size == content.size else { throw SwitchExecutableError.invalid }
                    }
                    metadata.append(parsed)
                }
                guard !metadata.isEmpty else { throw SwitchExecutableError.noProgram }
            } catch { try? source.handle.close(); throw error }
        }
        func close() { try? source.handle.close() }

        static func ticket(_ input: Data) throws -> (rights: Data, encryptedKey: Data) {
            let b = Data(input)
            guard b.count >= 4 else { throw SwitchExecutableError.invalid }
            let signature = b.cp32(0)
            let start: Int
            switch signature {
            case 0x10000, 0x10003: start = 0x240
            case 0x10001, 0x10004: start = 0x140
            case 0x10002, 0x10005: start = 0x80
            default: throw SwitchExecutableError.invalid
            }
            guard b.count >= start + 0x180, b[start + 0x140] == 2 else { throw SwitchExecutableError.invalid }
            guard b[start + 0x141] == 0 else { throw SwitchExecutableError.personalizedTicket }
            let rights = b.subdata(in: start + 0x160..<start + 0x170)
            guard rights.contains(where: { $0 != 0 }) else { throw SwitchExecutableError.invalid }
            let sectionBytes = UInt64(b.cp32(start + 0x174)), sectionOffset = UInt64(b.cp32(start + 0x178))
            guard sectionOffset >= UInt64(start + 0x180), sectionOffset <= b.count,
                  sectionBytes <= UInt64(b.count) - sectionOffset else { throw SwitchExecutableError.invalid }
            return (rights, b.subdata(in: start + 0x40..<start + 0x50))
        }

        func program(_ meta: SwitchContentMeta, index: UInt8) throws -> Nca {
            let refs = meta.contents.filter { $0.type == 1 && $0.idOffset == index }
            guard refs.count == 1, let nca = archives.first(where: { $0.contentID == refs[0].id }),
                  nca.header[0x205] == 0, meta.applicationID <= UInt64.max - UInt64(index),
                  nca.header.cp64(0x210) == meta.applicationID + UInt64(index) else { throw SwitchExecutableError.noProgram }
            return nca
        }
        func verify(_ nca: Nca, in meta: SwitchContentMeta, progress: SwitchByteProgress = { _, _ in }) throws {
            guard let ref = meta.contents.first(where: { $0.id == nca.contentID }) else { throw SwitchExecutableError.invalid }
            _ = try verifyArchiveHash(source, nca: nca, expected: ref.hash, cancelled: cancelled, progress: progress)
            authenticated.insert(nca.contentID)
        }
        func contentKey(_ nca: Nca) throws -> Data {
            let rights = nca.header.subdata(in: 0x230..<0x240)
            if rights.allSatisfy({ $0 == 0 }) { return try SwitchExecutableReader.contentKey(nca, keys: keys) }
            guard let encrypted = tickets[rights] else { throw SwitchExecutableError.missingTicket }
            let generation = max(nca.header[0x206], nca.header[0x220]), revision = generation > 0 ? generation - 1 : 0
            let kek = try key(String(format: "titlekek_%02x", revision), count: 16, keys: keys)
            return try crypt(encrypted, key: kek, mode: .ecb)
        }

        // LibHac 0.19 Nca.OpenSectionStorage/NcaSparseInfo, MIT, Alex Barney.
        // Full NCA authentication precedes this method. Sparse table IV uses the
        // physical address and sparse generation; payload IV uses virtual offsets.
        func sparseStorage(_ nca: Nca, section: Section, key: Data,
                           progress: SwitchByteProgress) throws -> SwitchContentStorage {
            let fs = section.fs, generation = fs.cp32(0x170) & 0xffff
            guard fs[0x172..<0x178].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
            if generation == 0 {
                guard fs[0x148..<0x178].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
                return SwitchContentStorage(size: section.size) { offset, count in
                    try self.source.read(nca.entry.offset + section.offset + offset, size: count, cancelled: self.cancelled)
                }
            }
            let offset = fs.cp64(0x148), size = fs.cp64(0x150), physical = fs.cp64(0x168)
            guard physical >= 0xc00, physical <= nca.entry.size, offset <= nca.entry.size - physical,
                  size <= nca.entry.size - physical - offset else { throw SwitchExecutableError.invalid }
            let header = fs.subdata(in: 0x158..<0x168)
            guard header.prefix(4) == Data("BKTR".utf8), header.cp32(4) <= 1, header.cp32(12) == 0 else { throw SwitchExecutableError.invalid }
            if header.cp32(8) == 0 {
                return SwitchContentStorage(size: section.size) { _, count in
                    if self.cancelled() { throw CancellationError() }; return Data(count: Int(count))
                }
            }
            var metaFS = fs
            metaFS[4] = 3; metaFS.cpPut(UInt64(generation) << 16, at: 0x140, count: 4)
            let meta = Section(index: section.index, offset: physical + offset, size: size, fs: metaFS)
            guard meta.offset % 16 == 0 else { throw SwitchExecutableError.invalid }
            let tree = try SwitchBucketTree(header: header, size: size, entrySize: 20, cancelled: cancelled, progress: progress) { at, count in
                try decryptedSection(file: self.source, nca: nca, section: meta, key: key, offset: at, size: count, cancelled: self.cancelled)
            }
            guard tree.end == section.size else { throw SwitchExecutableError.invalid }
            let payload = SwitchContentStorage(size: offset) { at, count in
                try self.source.read(nca.entry.offset + physical + at, size: count, cancelled: self.cancelled)
            }
            let zeros = SwitchContentStorage(size: UInt64(Int64.max)) { _, count in Data(count: Int(count)) }
            return try SwitchContentStorage.indirect(tree, base: payload, patch: zeros, cancelled: cancelled)
        }

        func storage(_ nca: Nca, section: Section, progress: SwitchByteProgress = { _, _ in }) throws -> SwitchContentStorage {
            guard authenticated.contains(nca.contentID) else { throw SwitchExecutableError.integrity }
            guard [1, 3, 4].contains(section.fs[4]), section.fs[5] <= 1 else {
                throw SwitchExecutableError.unsupportedFeature("Unsupported section \(section.index) encryption \(section.fs[4]) or metadata hash type \(section.fs[5]).")
            }
            guard section.fs[0x1d0..<0x200].allSatisfy({ $0 == 0 }) else {
                throw SwitchExecutableError.unsupportedFeature("Section \(section.index) has unsupported extended storage metadata.")
            }
            if section.fs[2] == 1 && !section.fs[0x178..<0x1a0].allSatisfy({ $0 == 0 }) {
                throw SwitchExecutableError.unsupportedFeature("Compressed PartitionFS sections are not supported.")
            }
            let material = try contentKey(nca)
            let encrypted = try sparseStorage(nca, section: section, key: material, progress: progress)
            func decrypt(_ offset: UInt64, _ size: UInt64, generation: UInt32? = nil) throws -> Data {
                if section.fs[4] == 1 { return try encrypted.bytes(offset, size) }
                let aligned = offset & ~UInt64(15), skip = offset - aligned
                var upper = section.fs.subdata(in: 0x140..<0x148)
                if let generation { upper.cpPut(UInt64(generation), at: 0, count: 4) }
                var counter = Data(upper.reversed())
                let value = (section.offset + aligned) / 16
                for shift in (0..<8).reversed() { counter.append(UInt8(truncatingIfNeeded: value >> (shift * 8))) }
                let plain = try crypt(encrypted.bytes(aligned, size + skip), key: material, mode: .ctr(counter))
                return plain.subdata(in: Int(skip)..<plain.count)
            }
            let ordinary = SwitchContentStorage(size: section.size) { offset, size in try decrypt(offset, size) }
            if section.fs[5] == 1 {
                // MetadataHashDataInfo is additional to the full CNMT-bound NCA
                // hash above. Verify its descriptor bytes, too. BKTR data is
                // authenticated by the full archive digest before routing reads.
                let offset = section.fs.cp64(0x1a0), size = section.fs.cp64(0x1a8)
                guard size > 0, size <= 16 * 1024 * 1024, offset <= ordinary.size,
                      size <= ordinary.size - offset else { throw SwitchExecutableError.invalid }
                let descriptor = try ordinary.bytes(offset, size)
                guard Data(SHA256.hash(data: descriptor)) == section.fs.subdata(in: 0x1b0..<0x1d0) else { throw SwitchExecutableError.integrity }
            } else {
                guard section.fs[0x1a0..<0x1d0].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
            }
            guard section.fs[4] == 4 else { return ordinary }
            let indirectOffset = section.fs.cp64(0x100), indirectSize = section.fs.cp64(0x108)
            let counterOffset = section.fs.cp64(0x120), counterSize = section.fs.cp64(0x128)
            guard indirectOffset > 0, indirectOffset % 16 == 0, indirectSize > 0,
                  indirectOffset <= counterOffset, indirectSize <= counterOffset - indirectOffset,
                  counterOffset <= section.size, counterSize <= section.size - counterOffset else { throw SwitchExecutableError.invalid }
            let tree = try SwitchBucketTree(header: section.fs.subdata(in: 0x130..<0x140), size: counterSize,
                entrySize: 16, cancelled: cancelled, progress: progress) { offset, size in try ordinary.bytes(counterOffset + offset, size) }
            // LibHac slices CTR-EX payload at the relocation table even when the
            // counter tree also covers that table. Metadata always uses the
            // ordinary counter, so never extend a payload read across this cut.
            guard tree.end >= indirectOffset, tree.end <= counterOffset,
                  tree.end % 16 == 0 else { throw SwitchExecutableError.invalid }
            for entry in tree.entries {
                guard entry.cp64(0) % 16 == 0, entry[8] <= 1,
                      entry[9..<12].allSatisfy({ $0 == 0 }) else { throw SwitchExecutableError.invalid }
            }
            return SwitchContentStorage(size: section.size) { offset, size in
                var result = Data(), position = offset
                while position < offset + size {
                    if self.cancelled() { throw CancellationError() }
                    if position >= indirectOffset {
                        result.append(try ordinary.bytes(position, offset + size - position)); break
                    }
                    let i = try tree.index(at: position), entry = tree.entries[i]
                    let take = min(offset + size - position, tree.limit(i) - position, indirectOffset - position)
                    if entry[8] == 1 {
                        result.append(try encrypted.bytes(position, take))
                    } else {
                        result.append(try decrypt(position, take, generation: entry.cp32(12)))
                    }
                    position += take
                }
                return result
            }
        }
    }
}
