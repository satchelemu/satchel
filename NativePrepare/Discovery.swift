import CryptoKit
import Foundation

/// Reads only authenticated container metadata and content. No process or compiler.
enum PrepareDiscovery {
    struct Selection { let titleID: UInt64; let name: String; let version: String; let update: URL? }
    static func select(base: URL, update: URL?, keys: [String: Data], titleID: UInt64?, token: PrepareCancellation, progress: @escaping SwitchByteProgress = { _, _ in }) throws -> Selection {

        let package = try SwitchExecutableReader.ContentPackage(base, keys: keys, cancelled: { token.cancelled }, progress: progress)
        defer { package.close() }
        let titles = package.metadata.filter { $0.type == 0x80 }
        let selected = titles.filter { titleID == nil || $0.titleID == titleID }
        guard selected.count == 1, let application = selected.first else {
            throw PrepareFailure(message: selected.isEmpty ? "No base application matches the selected title." : "This dump contains multiple applications. Select a title ID: " + selected.map { String(format: "%016llX", $0.titleID) }.joined(separator: ", "))
        }
        let effectiveUpdate = update ?? (package.metadata.contains { $0.type == 0x81 && $0.applicationID == application.titleID } ? base : nil)
        let patch = try effectiveUpdate.map { try SwitchExecutableReader.ContentPackage($0, keys: keys, cancelled: { token.cancelled }, progress: progress) }
        defer { patch?.close() }
        let (_, chosenPatch) = try SwitchContentMeta.select(base: package.metadata, update: patch?.metadata, applicationID: application.titleID)
        let chosen = chosenPatch ?? application, source = patch ?? package
        var name = String(format: "%016llX", application.titleID), version = "content-\(chosen.version)"
        if let reference = chosen.contents.first(where: { $0.type == 3 && $0.idOffset == 0 }) {
            guard let control = source.archives.first(where: { $0.contentID == reference.id }), control.header[0x205] == 2,
                  control.entry.size <= 64 * 1024 * 1024 else { throw SwitchExecutableError.invalid }
            try source.verify(control, in: chosen, progress: progress)
            guard let section = control.sections.first(where: { $0.index == 0 }), section.fs[2] == 0, section.fs[3] == 3 else { throw SwitchExecutableError.unsupported }
            let raw = try source.storage(control, section: section)
            let verified = try SwitchIntegrityStorage(header: section.fs, sectionSize: raw.size, cancelled: { token.cancelled }, read: raw.bytes)
            defer { verified.invalidate() }
            let romfs = try SwitchRomFS(storage: verified, cancelled: { token.cancelled })
            guard let file = try romfs.open("/control.nacp"), file.size == 0x4000 else { throw SwitchExecutableError.invalid }
            let nacp = try romfs.read(file, offset: 0, count: file.size)
            func text(_ at: Int, _ count: Int) -> String {
                String(decoding: nacp[at..<at + count].prefix(while: { $0 != 0 }), as: UTF8.self)
                    .filter { !$0.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) }
            }
            name = (0..<16).map { text($0 * 0x300, 0x200) }.first { !$0.isEmpty } ?? name
            let display = text(0x3060, 16); if !display.isEmpty { version = display }
        }
        return Selection(titleID: application.titleID, name: name, version: version, update: effectiveUpdate)
    }
    static func recipe(_ content: SwitchContentPipeline.PreparedContent, selection: Selection, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws -> NativePreparationRecipe {
        guard let npdm = content.program.processMetadata, npdm.count >= 0x80, npdm.prefix(4) == Data("META".utf8) else {
            throw PrepareFailure(message: "A valid main.npdm is required to generate a native profile.")
        }
        guard npdm[0xc] & 1 != 0 else { throw PrepareFailure(message: "This is a 32-bit ARM game. Native execution on this iPhone requires ARM64.") }
        let aci = Int(npdm.prep32(0x70)), aciSize = Int(npdm.prep32(0x74))
        guard aci >= 0x80, aciSize >= 0x40, aci <= npdm.count, aciSize <= npdm.count - aci,
              npdm[aci..<aci + 4] == Data("ACI0".utf8), npdm.prep64(aci + 0x10) == selection.titleID else { throw SwitchExecutableError.integrity }
        let modules = content.program.modules.filter { $0.name != "rtld" }
        var builds: [String: String] = [:], sdk = Set<String>(), usesHeap = false, largeBSS: UInt64 = 0
        let regex = try NSRegularExpression(pattern: "NintendoSDK_nnSdk-([0-9_]+)", options: .caseInsensitive)
        let total = modules.reduce(UInt64(0)) { $0 + UInt64($1.data.count) }
        var completed: UInt64 = 0
        progress(0, total)
        for module in modules {
            try token.check()
            try autoreleasepool {
                let decoded = try SwitchDecodedModule.decode(module)
                builds[module.name] = PrepareIO.hex(decoded.buildID.prefix(16))
                let end = decoded.segments.map { $0.offset + UInt64($0.bytes.count) }.max() ?? 0
                if ((end + UInt64(decoded.bssSize) + 0x3fff) & ~UInt64(0x3fff)) + 0x4000 > 0x7000000 { largeBSS += UInt64(decoded.bssSize) }
                for segment in decoded.segments {
                    if module.name == "main", segment.bytes.range(of: Data("_ZN2nn2os17SetMemoryHeapSizeEm".utf8)) != nil { usesHeap = true }
                    // Latin1 preserves byte positions without logging or retaining code text.
                    let text = String(data: segment.bytes, encoding: .isoLatin1) ?? ""
                    for match in regex.matches(in: text, range: NSRange(location: 0, length: text.utf16.count)) {
                        sdk.insert((text as NSString).substring(with: match.range(at: 1)))
                    }
                }
            }
            completed += UInt64(module.data.count); progress(completed, total)
        }
        let title = String(format: "%016llX", selection.titleID)
        let reviewed = NativeGameProfile.all.first { $0.titleID == title && $0.modules == modules.map(\.name) && $0.buildIDs == builds }
        let recipe = NativePreparationRecipe(schema: 1, titleID: title, version: selection.version,
            contentVersion: content.selection.patchVersion ?? content.selection.baseVersion, name: selection.name,
            modules: modules.map(\.name), buildIDs: builds, sdkVersions: sdk.sorted(), systemResource: UInt64(npdm.prep32(0x14)),
            usesHeap: usesHeap, largeBSS: largeBSS, usedMemory: 0x4000000, nros: [], reviewedVersion: reviewed?.version)
        try recipe.validate()
        return recipe
    }

    /// NRR hashes select dynamic code. Loose, embedded and zstd-framed NROs
    /// are found with bounded reads. Unresolved registrations fail explicitly.
    static func nros(_ romfs: SwitchRomFS, to scratch: URL, token: PrepareCancellation,
                     progress: @escaping (String, UInt64, UInt64, Double) -> Void = { _, _, _, _ in }) throws -> [NativePreparationRecipe.NRO] {
        let tree = try romfs.tree(cancelled: { token.cancelled })
        var wanted = Set<String>()
        func read(_ file: SwitchRomFS.File, _ offset: UInt64, _ size: UInt64) throws -> Data {
            guard size <= 512 * 1024 * 1024 else { throw SwitchExecutableError.limit }
            var result = Data(), done: UInt64 = 0
            while done < size { try token.check(); let n = min(1024 * 1024, size - done); result.append(try romfs.read(file, offset: offset + done, count: n)); done += n }
            return result
        }
        let registrations = tree.filter { $0.file != nil && String(decoding: $0.name, as: UTF8.self).lowercased().hasSuffix(".nrr") }
        for (index, entry) in registrations.enumerated() {
            progress("Checking dynamic module registrations (\(index + 1) of \(registrations.count))…", 0, 0,
                0.1 * Double(index) / Double(max(1, registrations.count)))
            guard let file = entry.file, file.size >= 0x350, file.size <= 1024 * 1024 else { throw PrepareFailure(message: "NRR registration file has an invalid size.") }
            let data = try read(file, 0, file.size)
            guard data.prefix(4) == Data("NRR0".utf8) else { throw PrepareFailure(message: "NRR registration header is invalid.") }
            let start = Int(data.prep32(0x340)), count = Int(data.prep32(0x344))
            guard count <= NativePreparationRecipe.maximumNROs, start >= 0x350, start <= data.count, count * 32 <= data.count - start else { throw SwitchExecutableError.limit }
            for i in 0..<count { wanted.insert(PrepareIO.hex(data.subdata(in: start + i * 32..<start + (i + 1) * 32))) }
        }
        guard wanted.count <= NativePreparationRecipe.maximumNROs else { throw SwitchExecutableError.limit }
        var found: [String: (URL, SwitchDecodedModule)] = [:]
        func collect(_ bytes: Data) throws {
            let digest = NativePreparationRecipe.hash(bytes)
            guard wanted.contains(digest), found[digest] == nil else { return }
            let decoded = try decodeNRO(bytes, name: "nro0")
            let url = scratch.appendingPathComponent(digest + ".nro")
            try bytes.write(to: url)
            found[digest] = (url, SwitchDecodedModule(name: decoded.name, segments: [], bssSize: decoded.bssSize, buildID: decoded.buildID))
        }
        func compressed(_ input: SwitchContentStorage) throws {
            // Port of pinned nxnative nro_collect.scan: inspect a frame prefix
            // before allocating the complete NRO. Arbitrary non-NRO frames do not
            // consume their expanded size. The NRR digest still selects code.
            let reader: SwitchZstdReader, header: Data
            do {
                reader = try SwitchZstdReader(input, cancelled: { token.cancelled })
                header = try reader.read(0x80)
            } catch is CancellationError { throw CancellationError() }
            catch { return }
            guard header.count == 0x80, header[0x10..<0x14] == Data("NRO0".utf8) else { return }
            let length = UInt64(header.prep32(0x18))
            guard length >= 0x80, length <= 512 * 1024 * 1024 else { throw SwitchExecutableError.limit }
            var bytes = header
            while bytes.count < length {
                try token.check()
                let part = try reader.read(Int(min(1024 * 1024, length - UInt64(bytes.count))))
                guard !part.isEmpty else { throw SwitchExecutableError.invalid }; bytes.append(part)
            }
            try reader.finish(allowTrailing: true)
            try collect(bytes)
        }
        // Scan loose files first, avoiding full archive scans when every hash is found.
        let files = tree.filter { $0.file != nil }.sorted { String(decoding: $0.name, as: UTF8.self).hasSuffix(".nro") && !String(decoding: $1.name, as: UTF8.self).hasSuffix(".nro") }
        for (fileIndex, entry) in files.enumerated() where !wanted.isSubset(of: Set(found.keys)) {
            let file = entry.file!
            func report(_ done: UInt64) {
                progress("Looking for dynamic modules (file \(fileIndex + 1) of \(files.count))…", done, file.size,
                    0.1 + 0.7 * (Double(fileIndex) + Double(done) / Double(max(1, file.size))) / Double(max(1, files.count)))
            }
            report(0)
            let input = SwitchContentStorage(size: file.size) { offset, count in try read(file, offset, count) }
            if let members = try PrepareNROArchive.members(input, cancelled: { token.cancelled }) {
                for member in members {
                    try token.check()
                    let head = try input.bytes(member.offset, min(4, member.stored))
                    try autoreleasepool {
                        if head == Data([0x28, 0xb5, 0x2f, 0xfd]) {
                            let data = try SwitchZstdReader.decode(SwitchContentStorage(size: member.stored) { o, n in
                                try input.bytes(member.offset + o, n)
                            }, bytes: member.size, cancelled: { token.cancelled })
                            try collect(data)
                        } else {
                            guard member.stored == member.size else { throw SwitchExecutableError.invalid }
                            try collect(read(file, member.offset, member.size))
                        }
                    }
                }
            }
            var position: UInt64 = 0
            while position < file.size && !wanted.isSubset(of: Set(found.keys)) {
                try token.check()
                let size = min(1024 * 1024 + 0x80, file.size - position)
                let chunk = try romfs.read(file, offset: position, count: min(size, SwitchIntegrityStorage.maximumRead))
                var search = chunk.startIndex
                while search < chunk.endIndex, let range = chunk.range(of: Data("NRO0".utf8), in: search..<chunk.endIndex) {
                    search = range.upperBound
                    guard position + UInt64(range.lowerBound) >= 0x10 else { continue }
                    let at = position + UInt64(range.lowerBound) - 0x10
                    guard at + 0x80 <= file.size else { continue }
                    let header = try read(file, at, 0x80), length = UInt64(header.prep32(0x18))
                    guard length >= 0x80, length <= 512 * 1024 * 1024, length <= file.size - at else { continue }
                    try autoreleasepool {
                        try collect(read(file, at, length))
                    }
                }
                search = chunk.startIndex
                while search < chunk.endIndex, let range = chunk.range(of: Data([0x28, 0xb5, 0x2f, 0xfd]), in: search..<chunk.endIndex) {
                    search = range.upperBound
                    let at = position + UInt64(range.lowerBound)
                    try autoreleasepool {
                        try compressed(SwitchContentStorage(size: file.size - at) { offset, count in try read(file, at + offset, count) })
                    }
                }
                report(position + UInt64(chunk.count))
                if chunk.count <= 0x80 { break }; position += UInt64(chunk.count - 0x80)
            }
        }
        guard wanted.isSubset(of: Set(found.keys)) else {
            throw PrepareFailure(message: "Dynamic module discovery: \(wanted.subtracting(found.keys).count) NRR-registered NROs were not found. No matching loose, embedded or zstd-framed module was found. A different archive codec or a missing registration payload needs a decoder.")
        }
        var result: [NativePreparationRecipe.NRO] = []
        for (index, digest) in wanted.sorted().enumerated() {
            try autoreleasepool {
                let source = found[digest]!.0, name = "nro\(index)", data = try Data(contentsOf: source)
                let module = try decodeNRO(data, name: name)
                try PrepareRewrite.write(module, to: scratch, token: token) { fraction, done, total in
                    progress("Rewriting dynamic module \(index + 1) of \(wanted.count)…", done, total,
                        0.8 + 0.2 * (Double(index) + fraction) / Double(max(1, wanted.count)))
                }
                result.append(.init(name: name, sha256: digest, bytes: UInt64(data.count), buildID: PrepareIO.hex(module.buildID.prefix(16))))
                try FileManager.default.removeItem(at: source)
            }
        }
        return result
    }
    static func decodeNRO(_ data: Data, name: String) throws -> SwitchDecodedModule {
        guard data.count >= 0x80, data[0x10..<0x14] == Data("NRO0".utf8), Int(data.prep32(0x18)) == data.count else { throw SwitchExecutableError.invalid }
        guard data.prep32(0x1c) & 2 == 0 else { throw PrepareFailure(message: "Compressed NRO modules (system 23+) are not supported by the pinned engine.") }
        var segments: [SwitchDecodedModule.Segment] = [], end = 0
        for i in 0..<3 {
            let start = Int(data.prep32(0x20 + i * 8)), size = Int(data.prep32(0x24 + i * 8))
            guard start == end, size % 0x1000 == 0, start <= data.count, size <= data.count - start else { throw SwitchExecutableError.invalid }
            segments.append(.init(offset: UInt64(start), bytes: data.subdata(in: start..<start + size))); end += size
        }
        guard end == data.count else { throw SwitchExecutableError.invalid }
        return SwitchDecodedModule(name: name, segments: segments, bssSize: data.prep32(0x38), buildID: data.subdata(in: 0x40..<0x60))
    }
}

extension Data {
    func prep32(_ at: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[at + $1]) << ($1 * 8) } }
    func prep64(_ at: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[at + $1]) << ($1 * 8) } }
}
