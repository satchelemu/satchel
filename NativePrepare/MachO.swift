import CryptoKit
import Foundation

/// Thin arm64 iOS dylib. One RX segment, separate RW module segments, no
/// constructors, fixups or guest imports. The only exported symbol is our marker.
enum PrepareMachO {
    struct Piece {
        let module: String
        let segment: String
        let section: String
        let source: URL
        let start: Int
        let count: Int
        var offset = 0
    }
    static func rounded(_ n: Int) -> Int { (n + 0x3fff) & ~0x3fff }
    static func split(name: String, source: URL, layout: [String: String], size: Int) throws -> [Piece] {
        func number(_ key: String) throws -> Int {
            guard let value = layout[key], let n = Int(value.replacingOccurrences(of: "0x", with: ""), radix: 16) else {
                throw PrepareFailure(message: "Invalid module layout.")
            }
            return n
        }
        guard !name.isEmpty, name.utf8.count <= 10, name.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber) }),
              size > 0, size <= 512 * 1024 * 1024 else { throw PrepareFailure(message: "Invalid module size or name.") }
        let text = rounded(try number("TEXT_END")), patch = try number("PATCH_OFF")
        let key = try number("KEY_PAGE_OFF"), keySize = try number("KEY_PAGE_SIZE"), end = rounded(size)
        guard text > 0, text <= end, key >= 0, key % 0x4000 == 0, keySize == 0x4000 else {
            throw PrepareFailure(message: "Invalid module page layout.")
        }
        var ranges: [(Int, Int)]
        if patch < 0 {
            let fileEnd = try number("FILE_IMAGE_LEN"), back = layout["BACK_LEN"] == nil ? 0 : try number("BACK_LEN")
            guard fileEnd >= text, fileEnd <= end, fileEnd % 0x4000 == 0, key >= fileEnd,
                  back >= 0, back % 0x4000 == 0, back <= end - fileEnd,
                  back == 0 || size % 0x4000 == 0 else { throw PrepareFailure(message: "Invalid front/back layout.") }
            ranges = [(0, text), (text, fileEnd), (fileEnd, end - back)]
            if back > 0 { ranges.append((end - back, end)) }
        } else {
            guard patch % 0x4000 == 0, text <= key, key + keySize <= patch, patch <= end else {
                throw PrepareFailure(message: "Invalid patch layout.")
            }
            ranges = [(0, text), (text, patch), (patch, end)]
        }
        return ranges.enumerated().map { i, range in
            Piece(module: name, segment: i == 1 ? "__DM\(name)W" : "__TEXT",
                  section: i == 1 ? "__img" : "__dm\(name)x\(i == 0 ? 0 : i - 1)",
                  source: source, start: range.0, count: range.1 - range.0)
        }
    }
    static func layout(_ url: URL) throws -> [String: String] {
        guard try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max < 65536 else {
            throw PrepareFailure(message: "Module layout exceeds limit.")
        }
        var result: [String: String] = [:]
        for line in try String(contentsOf: url, encoding: .utf8).split(separator: "\n") {
            let parts = line.split(whereSeparator: { $0.isWhitespace })
            guard parts.count == 3, parts[0] == "#", result[String(parts[1])] == nil else {
                throw PrepareFailure(message: "Malformed module layout.")
            }
            result[String(parts[1])] = String(parts[2])
        }
        return result
    }
    @discardableResult
    static func write(modules: [String], nros: [String] = [], from directory: URL, to output: URL, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws -> [String: Any] {
        try token.check()
        guard !FileManager.default.fileExists(atPath: output.path) else { throw PrepareFailure(message: "Container destination already exists.") }
        var pieces: [Piece] = []
        for name in modules {
            let file = directory.appendingPathComponent(name + ".patched.bin")
            let size = try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
            pieces += try split(name: name, source: file, layout: layout(directory.appendingPathComponent(name + ".fixups.tsv")), size: size)
        }
        var nroTable = Data(), nroOffsets = [0, 0, 0]
        for name in nros {
            let source = directory.appendingPathComponent(name + ".patched.bin")
            let split = try split(name: name, source: source, layout: layout(directory.appendingPathComponent(name + ".fixups.tsv")),
                                  size: source.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
            guard split.count == 3 else { throw PrepareFailure(message: "Packed NRO with a back patch area is unsupported by the pinned loader.") }
            nroTable.append(contentsOf: name.utf8); nroTable.append(Data(count: 32 - name.utf8.count))
            for (i, piece) in split.enumerated() {
                nroTable.u64(nroOffsets[i]); nroTable.u64(piece.count); nroOffsets[i] += piece.count
                pieces.append(Piece(module: name, segment: i == 1 ? "__DM_NRO_W" : "__TEXT",
                    section: i == 1 ? "__img" : i == 0 ? "__dm_nro_x0" : "__dm_nro_x1",
                    source: source, start: piece.start, count: piece.count))
            }
        }
        // Group packed dynamic pieces into three sections, preserving per-section order.
        let startup = pieces.filter { !nros.contains($0.module) }
        let dynamicPieces = pieces.filter { nros.contains($0.module) }
        pieces = startup + ["__dm_nro_x0", "__img", "__dm_nro_x1"].flatMap { section in dynamicPieces.filter { $0.section == section } }
        var cursor = 0x8000
        for i in pieces.indices where pieces[i].segment == "__TEXT" { pieces[i].offset = cursor; cursor += pieces[i].count }
        let textEnd = cursor
        for i in pieces.indices where pieces[i].segment != "__TEXT" { pieces[i].offset = cursor; cursor += pieces[i].count }
        let nroTableOffset = cursor
        if !nros.isEmpty { cursor += rounded(nroTable.count) }
        let link = cursor
        guard link < Int(UInt32.max) else { throw PrepareFailure(message: "Container exceeds Mach-O offset limit.") }
        var commands = Data(), count: UInt32 = 0
        func command(_ value: Data) { commands.append(value); count += 1 }
        func section(_ name: String, _ segment: String, _ offset: Int, _ size: Int, _ alignment: UInt32, _ flags: UInt32 = 0) -> Data {
            var d = Data(); d.name(name); d.name(segment); d.u64(offset); d.u64(size)
            d.words([UInt32(offset), alignment, 0, 0, flags, 0, 0, 0]); return d
        }
        func segment(_ name: String, _ offset: Int, _ size: Int, _ protection: UInt32, _ sections: [Data]) {
            var d = Data(); d.words([0x19, UInt32(72 + 80 * sections.count)]); d.name(name)
            d.u64(offset); d.u64(rounded(size)); d.u64(offset); d.u64(size)
            d.words([protection, protection, UInt32(sections.count), 0]); sections.forEach { d.append($0) }; command(d)
        }
        var textSections = [section("__text", "__TEXT", 0x4000, 0, 0, 0x80000400), section("__const", "__TEXT", 0x4000, 20, 0)]
        func grouped(_ selected: [Piece]) -> [Data] {
            var result: [Data] = [], seen = Set<String>()
            for p in selected where seen.insert(p.segment + "/" + p.section).inserted {
                let count = selected.filter { $0.segment == p.segment && $0.section == p.section }.reduce(0) { $0 + $1.count }
                result.append(section(p.section, p.segment, p.offset, count, 14))
            }
            return result
        }
        textSections += grouped(pieces.filter { $0.segment == "__TEXT" })
        segment("__TEXT", 0, textEnd, 5, textSections)
        var seenWritable = Set<String>()
        for p in pieces where p.segment != "__TEXT" && seenWritable.insert(p.segment).inserted {
            let rows = pieces.filter { $0.segment == p.segment }
            segment(p.segment, p.offset, rows.reduce(0) { $0 + $1.count }, 3, grouped(rows))
        }
        if !nros.isEmpty { segment("__DATA", nroTableOffset, rounded(nroTable.count), 3, [section("__dm_nro_tab", "__DATA", nroTableOffset, nroTable.count, 3)]) }
        segment("__LINKEDIT", link, 96, 1, [])
        func dylib(_ cmd: UInt32, _ name: String, _ stamp: UInt32, _ version: UInt32, _ compatibility: UInt32) {
            let length = (24 + name.utf8.count + 1 + 7) & ~7
            var d = Data(); d.words([cmd, UInt32(length), 24, stamp, version, compatibility]); d.append(contentsOf: name.utf8); d.append(Data(count: length - d.count)); command(d)
        }
        dylib(0xd, "@rpath/SatchelPreparedGame.dylib", 1, 0, 0)
        var dyld = Data(); dyld.words([0x80000022, 48, 0, 0, 0, 0, 0, 0, 0, 0, UInt32(link), 40]); command(dyld)
        var symbols = Data(); symbols.words([2, 24, UInt32(link + 48), 1, UInt32(link + 64), 32]); command(symbols)
        var dynamic = Data(); dynamic.words([0xb, 80, 0, 0, 0, 1, 1, 0] + Array(repeating: 0, count: 12)); command(dynamic)
        // UUID is derived below from the unsigned content, never copied from a game.
        let uuidPosition = 32 + commands.count + 8
        var uuid = Data(); uuid.words([0x1b, 24]); uuid.append(Data(count: 16)); command(uuid)
        var build = Data(); build.words([0x32, 32, 2, 0x1a0000, 0x1b0200, 1, 3, 0x69e20100]); command(build)
        var source = Data(); source.words([0x2a, 16, 0, 0]); command(source)
        var crypt = Data(); crypt.words([0x2c, 24, 0x4000, UInt32(textEnd - 0x4000), 0, 0]); command(crypt)
        dylib(0xc, "/usr/lib/libSystem.B.dylib", 2, 0x54f2803, 0x10000)
        var functions = Data(); functions.words([0x26, 16, UInt32(link + 40), 8]); command(functions)
        var dataCode = Data(); dataCode.words([0x29, 16, UInt32(link + 48), 0]); command(dataCode)
        var header = Data(); header.words([0xfeedfacf, 0x100000c, 0, 6, count, UInt32(commands.count), 0x100085, 0]); header.append(commands)
        guard header.count <= 0x4000 else { throw PrepareFailure(message: "Too many module sections.") }
        guard FileManager.default.createFile(atPath: output.path, contents: nil) else { throw PrepareFailure(message: "Cannot create container.") }
        let file = try FileHandle(forWritingTo: output); defer { try? file.close() }
        try file.write(contentsOf: header)
        try file.seek(toOffset: 0x4000); try file.write(contentsOf: Data("Satchel container 1\0".utf8))
        let totalWork = UInt64(pieces.reduce(0) { $0 + $1.count } + link + 96)
        var completedWork: UInt64 = 0
        progress(0, totalWork)
        for p in pieces {
            try token.check()
            let input = try FileHandle(forReadingFrom: p.source); defer { try? input.close() }
            try input.seek(toOffset: UInt64(p.start)); try file.seek(toOffset: UInt64(p.offset))
            var remaining = p.count
            while remaining > 0 {
                try token.check()
                try autoreleasepool {
                    let n = min(remaining, 1024 * 1024)
                    var bytes = try input.read(upToCount: n) ?? Data()
                    bytes.append(Data(count: n - bytes.count)); try file.write(contentsOf: bytes); remaining -= n
                    completedWork += UInt64(n); progress(completedWork, totalWork)
                }
            }
        }
        if !nros.isEmpty { try file.seek(toOffset: UInt64(nroTableOffset)); try file.write(contentsOf: nroTable) }
        // Export trie for _satchel_game_container_v1 at 0x4000, empty function
        // starts, one external N_SECT symbol, and its string table.
        var exports = Data([0, 1]); exports.append(Data("_satchel_game_container_v1\0".utf8)); exports.append(contentsOf: [34, 0, 0, 0, 0, 4, 0, 128, 128, 1, 0])
        guard exports.count == 40 else { throw PrepareFailure(message: "Invalid marker export.") }
        exports.append(Data(count: 8)); exports.words([2]); exports.append(contentsOf: [15, 2, 0, 0]); exports.u64(0x4000)
        var strings = Data([32, 0]); strings.append(Data("_satchel_game_container_v1\0".utf8)); strings.append(Data(count: 32 - strings.count)); exports.append(strings)
        try file.seek(toOffset: UInt64(link)); try file.write(contentsOf: exports); try file.synchronize()
        // Match the installed ld-27106.1 content UUID: SHA-256 of each
        // 16 KiB block, SHA-256 of those digests, then SHA-256 with the
        // install-name leaf as salt. UUID version/variant bits follow ld.
        let reader = try FileHandle(forReadingFrom: output)
        defer { try? reader.close() }
        var blockRoot = SHA256()
        while try autoreleasepool(invoking: { () throws -> Bool in
            try token.check()
            guard let bytes = try reader.read(upToCount: 0x4000), !bytes.isEmpty else { return false }
            blockRoot.update(data: Data(SHA256.hash(data: bytes)))
            completedWork += UInt64(bytes.count); progress(completedWork, totalWork); return true
        }) {}
        let rootHash = blockRoot.finalize()
        var salted = Data("/SatchelPreparedGame.dylib".utf8); salted.append(contentsOf: rootHash)
        var id = Data(SHA256.hash(data: salted).prefix(16)); id[6] = (id[6] & 15) | 0x30; id[8] = (id[8] & 63) | 0x80
        try file.seek(toOffset: UInt64(uuidPosition)); try file.write(contentsOf: id); try file.synchronize()
        var metadata: [String: Any] = [:]
        for name in modules + nros {
            metadata[name] = ["layout": try layout(directory.appendingPathComponent(name + ".fixups.tsv")),
                "pieces": pieces.filter { $0.module == name }.map { p -> [String: Any] in
                    ["segment": p.segment, "section": p.section, "offset": p.offset, "bytes": p.count,
                     "address": p.offset, "protection": p.segment == "__TEXT" ? 5 : 3]
                }]
        }
        return metadata
    }
}

private extension Data {
    mutating func words(_ values: [UInt32]) { for var v in values.map({ $0.littleEndian }) { Swift.withUnsafeBytes(of: &v) { append(contentsOf: $0) } } }
    mutating func u64(_ value: Int) { var v = UInt64(value).littleEndian; Swift.withUnsafeBytes(of: &v) { append(contentsOf: $0) } }
    mutating func name(_ value: String) { append(contentsOf: value.utf8); append(Data(count: 16 - value.utf8.count)) }
}
