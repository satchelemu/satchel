import CryptoKit
import Foundation

@main @MainActor struct ContentTests {
    static let title = ExecutableTests.title
    static let put = ExecutableTests.put
    static let transform = ExecutableTests.transform
    static var assertions = 0
    static func check(_ value: Bool) throws {
        assertions += 1
        guard value else { throw NSError(domain: "ContentAssertion", code: assertions) }
    }
    static func fails(_ body: () throws -> Void) throws {
        do { try body() } catch { assertions += 1; return }
        throw NSError(domain: "AcceptedMalformedContent", code: assertions)
    }
    static func tree(_ records: [Data], end: UInt64) -> (Data, Data) {
        let stride = records[0].count, per = (0x4000 - 16) / stride, sets = (records.count + per - 1) / per
        var header = Data("BKTR".utf8) + Data(count: 12)
        put(1, &header, 4, 4); put(UInt64(records.count), &header, 8, 4)
        var bytes = Data(count: (sets + 1) * 0x4000)
        put(UInt64(sets), &bytes, 4, 4); put(end, &bytes, 8, 8)
        for set in 0..<sets {
            let first = set * per, count = min(per, records.count - first), p = (set + 1) * 0x4000
            put(records[first].cp64(0), &bytes, 16 + set * 8, 8)
            put(UInt64(set), &bytes, p, 4); put(UInt64(count), &bytes, p + 4, 4)
            put(first + count == records.count ? end : records[first + count].cp64(0), &bytes, p + 8, 8)
            for i in 0..<count { bytes.replaceSubrange(p + 16 + i * stride..<p + 16 + (i + 1) * stride, with: records[first + i]) }
        }
        return (header, bytes)
    }
    static func record(_ virtual: UInt64, _ physical: UInt64, _ source: UInt64) -> Data {
        var b = Data(count: 20); put(virtual, &b, 0, 8); put(physical, &b, 8, 8); put(source, &b, 16, 4); return b
    }
    static func ticket(signature: UInt64 = 0x10004, rights: Data) throws -> Data {
        let start = [0x10000, 0x10003].contains(signature) ? 0x240 : ([0x10001, 0x10004].contains(signature) ? 0x140 : 0x80)
        var b = Data(count: start + 0x180)
        put(signature, &b, 0, 4); b[start + 0x140] = 2
        let wrapped = try transform(ExecutableTests.contentKey, ExecutableTests.areaKey, 0, true, Data(count: 16))
        b.replaceSubrange(start + 0x40..<start + 0x50, with: wrapped)
        b.replaceSubrange(start + 0x160..<start + 0x170, with: rights)
        put(UInt64(b.count), &b, start + 0x178, 4)
        return b
    }
    static func cnmt(_ nca: Data, type: UInt8, id: UInt64, version: UInt64) -> Data {
        let start = type == 0x80 ? 0x30 : 0x38
        var b = Data(count: start + 56 + 32)
        put(id, &b, 0, 8); put(version, &b, 8, 4); b[12] = type; b[20] = type == 0x81 ? 8 : 0
        put(UInt64(start - 32), &b, 14, 2); put(1, &b, 16, 2)
        put(type == 0x80 ? title + 0x800 : title, &b, 32, 8)
        let hash = Data(SHA256.hash(data: nca))
        b.replaceSubrange(start..<start + 32, with: hash); b.replaceSubrange(start + 32..<start + 48, with: hash.prefix(16))
        put(UInt64(nca.count), &b, start + 48, 5); b[start + 54] = 1
        if type == 0x81 {
            // Omitted distribution delta fragments must not hide missing Program
            // content. This record deliberately has no archive in the package.
            var delta = Data(count: 56)
            let hash = Data(SHA256.hash(data: Data("synthetic omitted delta".utf8)))
            delta.replaceSubrange(0..<32, with: hash); delta.replaceSubrange(32..<48, with: hash.prefix(16))
            put(0xc00, &delta, 48, 5); delta[54] = 6
            b.insert(contentsOf: delta, at: start + 56); put(2, &b, 16, 2)
        }
        return b
    }
    static func pfsSection(_ payload: Data) -> (Data, Data) {
        let count = (payload.count + 511) / 512
        var hashes = Data()
        for i in 0..<count { hashes.append(Data(SHA256.hash(data: payload[i * 512..<min((i + 1) * 512, payload.count)]))) }
        let start = (hashes.count + 511) & ~511
        var fs = Data(count: 512), bytes = hashes
        bytes.append(Data(count: start - bytes.count)); bytes.append(payload)
        bytes.append(Data(count: (512 - bytes.count % 512) % 512))
        put(2, &fs, 0, 2); fs[2] = 1; fs[3] = 2; fs[4] = 3
        fs.replaceSubrange(8..<40, with: Data(SHA256.hash(data: hashes)))
        put(512, &fs, 0x28, 4); put(2, &fs, 0x2c, 4); put(UInt64(hashes.count), &fs, 0x38, 8)
        put(UInt64(start), &fs, 0x40, 8); put(UInt64(payload.count), &fs, 0x48, 8)
        return (fs, bytes)
    }
    static func nca(_ sections: [(Data, Data)], contentType: UInt8 = 0, id: UInt64 = title,
                    rights: Data? = nil) throws -> Data {
        var head = Data(count: 0xc00), payload = Data()
        head.replaceSubrange(0x200..<0x204, with: Data("NCA3".utf8)); head[0x204] = 1; head[0x205] = contentType
        put(id, &head, 0x210, 8)
        if let rights { head.replaceSubrange(0x230..<0x240, with: rights) }
        var area = Data(count: 64); area.replaceSubrange(32..<48, with: ExecutableTests.contentKey)
        head.replaceSubrange(0x300..<0x340, with: try transform(area, ExecutableTests.areaKey, 0, true, Data(count: 16)))
        for (i, pair) in sections.enumerated() {
            let offset = 0xc00 + payload.count, fs = pair.0
            put(UInt64(offset / 512), &head, 0x240 + i * 16, 4)
            put(UInt64((offset + pair.1.count) / 512), &head, 0x244 + i * 16, 4)
            head.replaceSubrange(0x400 + i * 512..<0x600 + i * 512, with: fs)
            head.replaceSubrange(0x280 + i * 32..<0x2a0 + i * 32, with: Data(SHA256.hash(data: fs)))
            var encrypted = Data(), position = 0
            while position < pair.1.count {
                let end: Int, generation: UInt64
                if fs[4] == 4 && position < Int(fs.cp64(0x100)) {
                    end = position < 512 ? 512 : Int(fs.cp64(0x100)); generation = position < 512 ? 7 : 9
                } else { end = pair.1.count; generation = UInt64(fs.cp32(0x140)) }
                var upper = fs.subdata(in: 0x140..<0x148); upper.cpPut(generation, at: 0, count: 4)
                var counter = Data(upper.reversed()) + Data(count: 8)
                let value = UInt64(offset + position) / 16
                for k in 0..<8 { counter[8 + k] = UInt8(truncatingIfNeeded: value >> ((7 - k) * 8)) }
                let clear = fs[4] == 1 || (fs[4] == 4 && position >= 512 && position < Int(fs.cp64(0x100)) &&
                    pair.1[Int(fs.cp64(0x120)) + 0x4000 + 40] == 1)
                let bytes = pair.1.subdata(in: position..<end)
                encrypted.append(clear ? bytes : try transform(bytes, ExecutableTests.contentKey, 2, true, counter))
                position = end
            }
            payload.append(encrypted)
        }
        put(UInt64(head.count + payload.count), &head, 0x208, 8)
        return try transform(head, ExecutableTests.headerKey, 1, true, Data(count: 16)) + payload
    }
    static func patch(_ base: (Data, Data), _ target: (Data, Data), clearRegion: Bool = false, counterMetadata: Bool = false) -> (Data, Data) {
        var fs = target.0, payload = target.1
        var records: [Data] = []
        for i in stride(from: 0, to: payload.count, by: 512) {
            let same = i + 512 <= base.1.count && base.1[i..<i + 512] == payload[i..<i + 512]
            records.append(record(UInt64(i), UInt64(i), same ? 0 : 1))
        }
        let indirect = tree(records, end: UInt64(payload.count)), start = payload.count
        payload.append(indirect.1)
        var c0 = Data(count: 16), c1 = Data(count: 16)
        put(7, &c0, 12, 4); put(512, &c1, 0, 8); put(9, &c1, 12, 4); c1[8] = clearRegion ? 1 : 0
        let counters = tree([c0, c1], end: UInt64(counterMetadata ? payload.count : start)), ctrStart = payload.count
        payload.append(counters.1)
        let descriptorOffset = payload.count, descriptor = Data(repeating: 0x36, count: 512)
        payload.append(descriptor)
        fs[5] = 1
        put(UInt64(descriptorOffset), &fs, 0x1a0, 8); put(512, &fs, 0x1a8, 8)
        fs.replaceSubrange(0x1b0..<0x1d0, with: Data(SHA256.hash(data: descriptor)))
        fs[4] = 4
        put(UInt64(start), &fs, 0x100, 8); put(UInt64(indirect.1.count), &fs, 0x108, 8)
        fs.replaceSubrange(0x110..<0x120, with: indirect.0)
        put(UInt64(ctrStart), &fs, 0x120, 8); put(UInt64(counters.1.count), &fs, 0x128, 8)
        fs.replaceSubrange(0x130..<0x140, with: counters.0)
        return (fs, payload)
    }
    static func package(_ program: Data, type: UInt8, version: UInt64, rights: Data? = nil, compressed: Bool = false) throws -> Data {
        let meta = try nca([pfsSection(ExecutableTests.pfs([("Application.cnmt", cnmt(program, type: type, id: type == 0x80 ? title : title + 0x800, version: version))]))], contentType: 1, id: type == 0x80 ? title : title + 0x800)
        func name(_ b: Data) -> String { Data(SHA256.hash(data: b)).prefix(16).cpHex }
        var stored = program
        if compressed {
            guard program.count > 0x4000 else { throw SwitchExecutableError.invalid }
            var section = Data(count: 64)
            put(0x4000, &section, 0, 8); put(UInt64(program.count - 0x4000), &section, 8, 8); put(1, &section, 16, 8)
            stored = Data(program.prefix(0x4000)) + Data("NCZSECTN".utf8) + Data([1,0,0,0,0,0,0,0]) + section + zstdFrame(Data(program.dropFirst(0x4000)))
        }
        var entries = [(name(program) + (compressed ? ".ncz" : ".nca"), stored), (name(meta) + ".cnmt.nca", meta)]
        if let rights { entries.append(("fixture.tik", try ticket(rights: rights))) }
        return ExecutableTests.pfs(entries)
    }
    static func verified(_ raw: Data) throws -> SwitchIntegrityStorage {
        func hashes(_ data: Data) -> Data {
            var out = Data()
            for p in stride(from: 0, to: data.count, by: 512) {
                var block = data.subdata(in: p..<min(p + 512, data.count))
                block.append(Data(count: 512 - block.count)); out.append(Data(SHA256.hash(data: block)))
            }
            return out
        }
        var levels = [raw]
        for _ in 0..<5 { levels.insert(hashes(levels[0]), at: 0) }
        var header = Data(count: 512), bytes = Data()
        header.replaceSubrange(8..<12, with: Data("IVFC".utf8))
        put(0x20000, &header, 12, 4); put(32, &header, 16, 4); put(7, &header, 20, 4)
        for (i, level) in levels.enumerated() {
            put(UInt64(bytes.count), &header, 24 + i * 24, 8); put(UInt64(level.count), &header, 32 + i * 24, 8)
            put(9, &header, 40 + i * 24, 4); bytes.append(level)
            bytes.append(Data(count: (512 - bytes.count % 512) % 512))
        }
        header.replaceSubrange(0xc8..<0xe8, with: hashes(levels[0]))
        return try SwitchIntegrityStorage(header: header, sectionSize: UInt64(bytes.count)) { offset, count in
            bytes.subdata(in: Int(offset)..<Int(offset + count))
        }
    }
    static func directoryFixture(_ nodes: [(String, Data?)]) -> Data {
        let dirs = [""] + nodes.filter { $0.1 == nil }.map { $0.0 }, files = nodes.filter { $0.1 != nil }
        func parent(_ path: String) -> String { path.split(separator: "/").dropLast().joined(separator: "/") }
        func name(_ path: String) -> Data { Data((path.split(separator: "/").last.map(String.init) ?? "").utf8) }
        var dirTable = Data(), fileTable = Data(), dirOffsets: [String: Int] = [:], fileOffsets: [String: Int] = [:]
        for path in dirs {
            dirOffsets[path] = dirTable.count; dirTable.append(Data(count: (24 + name(path).count + 3) & ~3))
        }
        for (path, _) in files {
            fileOffsets[path] = fileTable.count; fileTable.append(Data(count: (32 + name(path).count + 3) & ~3))
        }
        func link(_ value: Int?) -> UInt64 { value.map(UInt64.init) ?? UInt64(UInt32.max) }
        for path in dirs {
            let p = dirOffsets[path]!, kids = dirs.filter { !$0.isEmpty && parent($0) == path }
            let siblings = dirs.filter { !$0.isEmpty && parent($0) == parent(path) }
            let next = siblings.firstIndex(of: path).flatMap { $0 + 1 < siblings.count ? siblings[$0 + 1] : nil }
            put(UInt64(dirOffsets[parent(path)]!), &dirTable, p, 4)
            put(link(next.flatMap { dirOffsets[$0] }), &dirTable, p + 4, 4)
            put(link(kids.first.flatMap { dirOffsets[$0] }), &dirTable, p + 8, 4)
            put(link(files.first(where: { parent($0.0) == path }).flatMap { fileOffsets[$0.0] }), &dirTable, p + 12, 4)
            put(UInt64(UInt32.max), &dirTable, p + 16, 4); put(UInt64(name(path).count), &dirTable, p + 20, 4)
            dirTable.replaceSubrange(p + 24..<p + 24 + name(path).count, with: name(path))
        }
        var payload = Data()
        for (path, value) in files {
            let p = fileOffsets[path]!, data = value!, siblings = files.filter { parent($0.0) == parent(path) }
            let at = siblings.firstIndex(where: { $0.0 == path })!
            let next = at + 1 < siblings.count ? fileOffsets[siblings[at + 1].0] : nil
            put(UInt64(dirOffsets[parent(path)]!), &fileTable, p, 4); put(link(next), &fileTable, p + 4, 4)
            put(UInt64(payload.count), &fileTable, p + 8, 8); put(UInt64(data.count), &fileTable, p + 16, 8)
            put(UInt64(UInt32.max), &fileTable, p + 24, 4); put(UInt64(name(path).count), &fileTable, p + 28, 4)
            fileTable.replaceSubrange(p + 32..<p + 32 + name(path).count, with: name(path)); payload.append(data)
        }
        var raw = Data(count: 80)
        put(80, &raw, 0, 8)
        for (i, pair) in [(80, 0), (80, dirTable.count), (80 + dirTable.count, 0), (80 + dirTable.count, fileTable.count)].enumerated() {
            put(UInt64(pair.0), &raw, 8 + i * 16, 8); put(UInt64(pair.1), &raw, 16 + i * 16, 8)
        }
        put(UInt64(80 + dirTable.count + fileTable.count), &raw, 72, 8)
        raw.append(dirTable); raw.append(fileTable); raw.append(payload); return raw
    }

    static func zstdFrame(_ input: Data) -> Data {
        // Original synthetic frame, raw zstd blocks, no encoder dependency.
        var frame = Data([0x28, 0xb5, 0x2f, 0xfd, 0xa0])
        var length = Data(count: 4); put(UInt64(input.count), &length, 0, 4); frame.append(length)
        var at = 0
        while at < input.count {
            let count = min(65536, input.count - at)
            var block = Data(count: 3); put(UInt64(count << 3 | (at + count == input.count ? 1 : 0)), &block, 0, 3)
            frame.append(block); frame.append(input.subdata(in: at..<at+count)); at += count
        }
        return frame
    }
    static func nczTests(_ scratch: URL) throws {
        let plain = Data((0..<131072).map { UInt8(truncatingIfNeeded: $0) })
        var header = Data(count: 0xc00)
        header.replaceSubrange(0x200..<0x204, with: Data("NCA3".utf8)); put(UInt64(0x4000 + plain.count), &header, 0x208, 8)
        let prefix = try transform(header, ExecutableTests.headerKey, 1, true, Data(count: 16)) + Data(count: 0x3400)
        var ctr = Data(count: 16); ctr[14] = 4
        let encrypted = try transform(plain, ExecutableTests.contentKey, 2, true, ctr), expected = prefix + encrypted
        var section = Data(count: 64)
        put(0x4000, &section, 0, 8); put(UInt64(plain.count), &section, 8, 8); put(3, &section, 16, 8)
        section.replaceSubrange(32..<48, with: ExecutableTests.contentKey)
        var ncz = prefix + Data("NCZSECTN".utf8) + Data([1,0,0,0,0,0,0,0]) + section
        let base = ncz
        ncz.append(zstdFrame(plain))
        func expand(_ input: Data, _ name: String) throws -> Data {
            let url = scratch.appendingPathComponent(name)
            defer { try? FileManager.default.removeItem(at: url) }
            let size = try SwitchNCZ.expand(SwitchContentStorage(size: UInt64(input.count)) { o, n in input.subdata(in: Int(o)..<Int(o+n)) }, to: url, keys: ExecutableTests.keys, cancelled: { false })
            let data = try Data(contentsOf: url); try check(data.count == size); return data
        }
        try check(try expand(ncz, "stream.nca") == expected)
        let compressed = Data(base64Encoded: "KLUv/aAAAAIAXQgABBAAAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8wMTIzNDU2Nzg5Ojs8PT4/QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXp7fH1+f4CBgoOEhYaHiImKi4yNjo+QkZKTlJWWl5iZmpucnZ6foKGio6SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr/AwcLDxMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3+Dh4uPk5ebn6Onq6+zt7u/w8fLz9PX29/j5+vv8/f7/AQAA/f4D+ZoC")!
        try check(try expand(base + compressed, "compressed.nca") == expected)
        var covered = Data(count: 128)
        put(0x2000, &covered, 8, 8); put(1, &covered, 16, 8)
        put(0x2000, &covered, 64, 8); put(0x2000, &covered, 72, 8); put(1, &covered, 80, 8)
        let prefixRegions = prefix + Data("NCZSECTN".utf8) + Data([3,0,0,0,0,0,0,0]) + covered + section + compressed
        try check(try expand(prefixRegions, "prefix-regions.nca") == expected)
        var compressedBlock = Data("NCZBLOCK".utf8) + Data([2,1,0,17]) + Data(count: 16)
        put(1, &compressedBlock, 12, 4); put(UInt64(plain.count), &compressedBlock, 16, 8)
        put(UInt64(compressed.count), &compressedBlock, 24, 4)
        try check(try expand(base + compressedBlock + compressed, "compressed-block.nca") == expected)
        var block = Data("NCZBLOCK".utf8) + Data([2,1,0,16]) + Data(count: 12)
        put(2, &block, 12, 4); put(UInt64(plain.count), &block, 16, 8)
        var sizes = Data(count: 8); put(65536, &sizes, 0, 4); put(65536, &sizes, 4, 4)
        try check(try expand(base + block + sizes + plain, "block.nca") == expected)
        for point in [0x4000, 0x4008, 0x4010, 0x4018, 0x4020, 0x4028, ncz.count-1] {
            var corrupt = ncz; corrupt[point] ^= 255
            if point == ncz.count - 1 {
                // Transport is not an authenticity boundary: an unchecksummed
                // frame can decode, but the reconstructed NCA digest must differ.
                try check(try Data(SHA256.hash(data: expand(corrupt, "changed.nca"))) != Data(SHA256.hash(data: expected)))
            } else { try fails { _ = try expand(corrupt, "invalid.nca") } }
        }
        try fails { _ = try expand(Data(ncz.dropLast()), "short.nca") }
        try fails { _ = try expand(ncz + Data([0]), "trailing.nca") }
        let stream = SwitchContentStorage(size: UInt64(ncz.count)) { o,n in ncz.subdata(in: Int(o)..<Int(o+n)) }
        let cancelURL = scratch.appendingPathComponent("cancel.nca")
        try fails { _ = try SwitchNCZ.expand(stream, to: cancelURL, keys: ExecutableTests.keys, cancelled: { true }) }
        try check(!FileManager.default.fileExists(atPath: cancelURL.path))
        let archive = ExecutableTests.pfs([("00000000000000000000000000000000.ncz",ncz)])
        let url = scratch.appendingPathComponent("synthetic.nsz"); try archive.write(to: url)
        try check(try SwitchPackageReader.inspect(url).format == .nsz)
        let input = try SwitchExecutableReader.Source(url)
        let entries = try SwitchNCZ.entries(SwitchPackageReader.contentEntries(url), source: input, keys: ExecutableTests.keys, cancelled: { false })
        try check(entries.count == 1 && entries[0].name.hasSuffix(".nca"))
        try check(try input.read(entries[0].offset, size: entries[0].size, cancelled: { false }) == expected)
    }

    static func compressedStorageTests() throws {
        func entry(_ virtual: UInt64, _ physical: UInt64, _ type: UInt8, _ stored: UInt64 = 0) -> Data {
            var b = Data(count: 24); put(virtual, &b, 0, 8); put(physical, &b, 8, 8); b[16] = type; put(stored, &b, 20, 4); return b
        }
        // One literal and an overlapping match, then the required trailing literals.
        let lz4 = Data([0x1f, 65, 1, 0, 12, 0x50, 66, 67, 68, 69, 70])
        let expected = Data([1, 2, 3, 4]) + Data(count: 7) + Data(repeating: 65, count: 32) + Data([66, 67, 68, 69, 70])
        let table = tree([entry(0, 0, 0), entry(4, 0, 1), entry(11, 4, 3, UInt64(lz4.count))], end: UInt64(expected.count))
        let payload = Data([1, 2, 3, 4]) + lz4 + table.1
        var header = Data(count: 512)
        put(UInt64(4 + lz4.count), &header, 0x178, 8); put(UInt64(table.1.count), &header, 0x180, 8)
        header.replaceSubrange(0x188..<0x198, with: table.0)
        func open(_ header: Data, _ bytes: Data) throws -> SwitchContentStorage {
            try SwitchContentStorage.compressed(header: header, verified: SwitchContentStorage(size: UInt64(bytes.count)) { o, n in bytes.subdata(in: Int(o)..<Int(o + n)) }, cancelled: { false })
        }
        let decoded = try open(header, payload)
        try check(try decoded.bytes(0, decoded.size) == expected)
        try check(try decoded.bytes(2, 43) == expected.subdata(in: 2..<45))
        try check(try decoded.bytes(12, 3) == Data(repeating: 65, count: 3))
        for point in [0x178, 0x180, 0x188, 0x198] {
            var bad = header; bad[point] ^= 255
            try fails { _ = try open(bad, payload) }
        }
        for point in [4 + lz4.count + 0x4000 + 16 + 16, 4 + lz4.count + 0x4000 + 16 + 18,
                      4 + lz4.count + 0x4000 + 16 + 48 + 8, 4 + lz4.count + 0x4000 + 16 + 48 + 20] {
            var bad = payload; bad[point] ^= 255
            try fails { _ = try open(header, bad) }
        }
        var badLZ = payload; badLZ[6] = 0
        try fails { _ = try open(header, badLZ).bytes(11, 1) }
        try fails { _ = try decoded.bytes(UInt64.max, 1) }
        try fails { _ = try SwitchContentStorage.compressed(header: header, verified: SwitchContentStorage(size: UInt64(payload.count)) { _, _ in throw SwitchExecutableError.integrity }, cancelled: { false }) }
        try fails { _ = try SwitchContentStorage.compressed(header: header, verified: SwitchContentStorage(size: UInt64(payload.count)) { o, n in payload.subdata(in: Int(o)..<Int(o+n)) }, cancelled: { true }) }
    }

    static func sparseNCA(_ original: Data) throws -> Data {
        var header = try transform(original.prefix(0xc00), ExecutableTests.headerKey, 1, false, Data(count: 16))
        let sectionOffset = Int(header.cp32(0x250)) * 512, originalSize = Int(header.cp32(0x254)) * 512 - sectionOffset
        let virtualSize = originalSize + 1024 * 1024
        let table = tree([record(0, 0, 0), record(UInt64(originalSize), 0, 1)], end: UInt64(virtualSize))
        var fs = header.subdata(in: 0x600..<0x800)
        put(UInt64(originalSize), &fs, 0x148, 8); put(UInt64(table.1.count), &fs, 0x150, 8)
        fs.replaceSubrange(0x158..<0x168, with: table.0)
        put(UInt64(sectionOffset), &fs, 0x168, 8); put(2, &fs, 0x170, 2)
        header.replaceSubrange(0x600..<0x800, with: fs)
        header.replaceSubrange(0x2a0..<0x2c0, with: Data(SHA256.hash(data: fs)))
        put(UInt64((sectionOffset + virtualSize) / 512), &header, 0x254, 4)
        var ctr = Data(count: 16)
        let upper = (fs.cp64(0x140) & 0xffffffff00000000) | (2 << 16)
        for i in 0..<8 { ctr[i] = UInt8(truncatingIfNeeded: upper >> ((7-i)*8)); ctr[8+i] = UInt8(truncatingIfNeeded: UInt64(original.count / 16) >> ((7-i)*8)) }
        let encrypted = try transform(table.1, ExecutableTests.contentKey, 2, true, ctr)
        put(UInt64(original.count + encrypted.count), &header, 0x208, 8)
        return try transform(header, ExecutableTests.headerKey, 1, true, Data(count: 16)) + original.dropFirst(0xc00) + encrypted
    }

    static func run(_ scratch: URL) throws {
        print("Checking NCZ stream and block transports")
        try nczTests(scratch)
        print("Checking compressed NCA storage")
        try compressedStorageTests()
        let rights = Data(repeating: 1, count: 16)
        for signature: UInt64 in 0x10000...0x10005 {
            let parsed = try SwitchExecutableReader.ContentPackage.ticket(ticket(signature: signature, rights: rights))
            try check(parsed.rights == rights && parsed.encryptedKey.count == 16)
        }
        let t = try ticket(rights: rights)
        for count in [0, 3, 100, t.count - 1] { try fails { _ = try SwitchExecutableReader.ContentPackage.ticket(t.prefix(count)) } }
        var personal = t; personal[0x281] = 1
        try fails { _ = try SwitchExecutableReader.ContentPackage.ticket(personal) }
        var range = t; put(UInt64.max, &range, 0x2b8, 4)
        try fails { _ = try SwitchExecutableReader.ContentPackage.ticket(range) }
        let keyText = "header_key = " + ExecutableTests.headerKey.cpHex + "\ntitlekek_00 = " + ExecutableTests.areaKey.cpHex
        try check(try SwitchKeys.parse(Data(keyText.utf8))["titlekek_00"] == ExecutableTests.areaKey)
        let records = (0..<1000).map { record(UInt64($0), UInt64($0), UInt64($0 % 2)) }
        let bucket = tree(records, end: 1000)
        func readTree(_ h: Data, _ b: Data) throws -> SwitchBucketTree {
            try SwitchBucketTree(header: h, size: UInt64(b.count), entrySize: 20, cancelled: { false }) { o, n in b.subdata(in: Int(o)..<Int(o + n)) }
        }
        let parsed = try readTree(bucket.0, bucket.1)
        try check(parsed.entries.count == 1000 && (try parsed.index(at: 999)) == 999)
        let a = SwitchContentStorage(size: 1000) { _, n in Data(repeating: 1, count: Int(n)) }
        let b = SwitchContentStorage(size: 1000) { _, n in Data(repeating: 2, count: Int(n)) }
        let composed = try SwitchContentStorage.indirect(parsed, base: a, patch: b, cancelled: { false })
        try check(try composed.bytes(815, 12) == Data((815..<827).map { UInt8($0 % 2 + 1) }))
        for p in [0, 4, 8, 16, 0x4000, 0x4004, 0x4008, 0x4010, 0x4024] {
            var corrupt = bucket.1; corrupt[p] ^= 255
            try fails { _ = try readTree(bucket.0, corrupt) }
        }
        var badRecord = record(0, UInt64.max, 0)
        var bad = tree([badRecord], end: 1)
        try fails { _ = try SwitchContentStorage.indirect(readTree(bad.0, bad.1), base: a, patch: b, cancelled: { false }) }
        badRecord = record(0, 0, 2); bad = tree([badRecord], end: 1)
        try fails { _ = try SwitchContentStorage.indirect(readTree(bad.0, bad.1), base: a, patch: b, cancelled: { false }) }
        let code = pfsSection(ExecutableTests.pfs([("main", ExecutableTests.module(0x51)), ("rtld", ExecutableTests.module(0x42))]))
        let newCode = pfsSection(ExecutableTests.pfs([("main", ExecutableTests.module(0x61)), ("rtld", ExecutableTests.module(0x42))]))
        var rom = RomFSTests.fixture(), newRom = RomFSTests.fixture { $0[700] ^= 1 }
        for pair in [0, 1] {
            if pair == 0 { put(2, &rom.0, 0, 2); rom.0[3] = 3; rom.0[4] = 3 }
            else { put(2, &newRom.0, 0, 2); newRom.0[3] = 3; newRom.0[4] = 3 }
        }
        let original = try nca([code, rom]), updated = try nca([patch(code, newCode), patch(rom, newRom)], rights: rights)
        let baseURL = scratch.appendingPathComponent("base.nsp"), updateURL = scratch.appendingPathComponent("update.nsp")
        try package(original, type: 0x80, version: 0).write(to: baseURL)
        try package(updated, type: 0x81, version: 2, rights: rights).write(to: updateURL)
        var paddedHeader = try transform(Data(original.prefix(0xc00)), ExecutableTests.headerKey, 1, false, Data(count: 16))
        let paddedSize = max(original.count, 0x10000)
        put(UInt64(paddedSize), &paddedHeader, 0x208, 8)
        let padded = try transform(paddedHeader, ExecutableTests.headerKey, 1, true, Data(count: 16)) + original.dropFirst(0xc00) + Data(count: paddedSize-original.count)
        let nszURL = scratch.appendingPathComponent("authenticated.nsz")
        let nsz = try package(padded, type: 0x80, version: 0, compressed: true)
        try nsz.write(to: nszURL)
        print("Checking CNMT-bound NSZ content")
        try SwitchContentPipeline.withContent(base: nszURL, keys: ExecutableTests.keys, applicationID: title) { content in
            try check(content.program.modules[1].data == ExecutableTests.module(0x51))
            let file = try content.romFS.open("/hello")!
            try check(try content.romFS.read(file, offset: 0, count: file.size) == Data((0..<600).map { UInt8(truncatingIfNeeded: $0) }))
        }
        var altered = nsz
        let nczEntry = try SwitchPackageReader.contentEntries(nszURL).first { $0.name.hasSuffix(".ncz") }!
        altered[Int(nczEntry.offset + nczEntry.size - 1)] ^= 1
        let badNSZ = scratch.appendingPathComponent("corrupt.nsz"); try altered.write(to: badNSZ)
        try fails { try SwitchContentPipeline.withContent(base: badNSZ, keys: ExecutableTests.keys, applicationID: title) { _ in } }
        let sparseURL = scratch.appendingPathComponent("sparse.nsp")
        try package(sparseNCA(original), type: 0x80, version: 0).write(to: sparseURL)
        try SwitchContentPipeline.withContent(base: sparseURL, keys: ExecutableTests.keys, applicationID: title) { content in
            let file = try content.romFS.open("/hello")!
            try check(try content.romFS.read(file, offset: 0, count: file.size) == Data((0..<600).map { UInt8(truncatingIfNeeded: $0) }))
        }
        var keys = ExecutableTests.keys; keys["titlekek_00"] = ExecutableTests.areaKey
        let out = scratch.appendingPathComponent("synthetic.img")
        var authenticatedBytes: [UInt64] = [], compositionBytes: [UInt64] = []
        try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title,
            progress: { phase, done, total, fraction in
                precondition(done <= total && fraction.isFinite && (0...1).contains(fraction))
                if phase == .authentication {
                    precondition(total == UInt64(original.count + updated.count))
                    authenticatedBytes.append(done)
                }
                if phase == .composition { compositionBytes.append(done) }
            }) { content in
            try check(authenticatedBytes.last == UInt64(original.count + updated.count))
            try check(zip(authenticatedBytes, authenticatedBytes.dropFirst()).allSatisfy { $0 <= $1 })
            try check(compositionBytes.contains { $0 > 0 })
            try check(content.selection.patchVersion == 2)
            try check(content.program.modules[1].data == ExecutableTests.module(0x61))
            let file = try content.romFS.open("/hello")!
            var expected = Data((0..<600).map { UInt8(truncatingIfNeeded: $0) }); expected[540] ^= 1
            try check(try content.romFS.read(file, offset: 0, count: file.size) == expected)
            var progress: [UInt64] = []
            let result = try SwitchRomFSImage.write(content.romFS, to: out, progress: { progress.append($0.completedBytes) })
            try check(result.fileCount == 1 && result.entryCount == 2 && progress.last == 600)
            let before = try Data(contentsOf: out)
            try check(result.sha256 == Data(SHA256.hash(data: before)))
            var cancel = false
            try fails { _ = try SwitchRomFSImage.write(content.romFS, to: out, cancelled: { cancel }) { p in if p.completedBytes > 0 { cancel = true } } }
            try check(try Data(contentsOf: out) == before)
            try check(try FileManager.default.contentsOfDirectory(atPath: scratch.path).allSatisfy { !$0.hasSuffix(".part") })
        }
        try SwitchContentPipeline.withContent(base: baseURL, keys: keys, applicationID: title) { content in
            try check(content.selection.patchVersion == nil && content.program.modules[1].data == ExecutableTests.module(0x51))
        }
        for at in [0xc00, 0xc00 + Int(patch(code, newCode).0.cp64(0x100)),
                   0xc00 + Int(patch(code, newCode).0.cp64(0x120)), 0xc00 + Int(patch(code, newCode).0.cp64(0x1a0))] {
            var corrupt = updated; corrupt[at] ^= 1
            try package(corrupt, type: 0x81, version: 2, rights: rights).write(to: updateURL)
            try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title) { _ in } }
        }
        try package(updated, type: 0x81, version: 2, rights: rights).write(to: updateURL)
        let clear = try nca([patch(code, newCode, clearRegion: true), patch(rom, newRom, clearRegion: true)], rights: rights)
        try package(clear, type: 0x81, version: 2, rights: rights).write(to: updateURL)
        try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title) { content in
            try check(content.program.modules[1].data == ExecutableTests.module(0x61))
            try check(try content.romFS.verifySample() == 600)
        }
        let extended = try nca([patch(code, newCode, counterMetadata: true), patch(rom, newRom, counterMetadata: true)], rights: rights)
        try package(extended, type: 0x81, version: 2, rights: rights).write(to: updateURL)
        try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title) { content in
            try check(content.program.modules[1].data == ExecutableTests.module(0x61))
            try check(try content.romFS.verifySample() == 600)
        }
        for end in [UInt64(512), UInt64.max & ~15] {
            var malformed = patch(rom, newRom, counterMetadata: true)
            let counter = Int(malformed.0.cp64(0x120))
            put(end, &malformed.1, counter + 8, 8)
            put(end, &malformed.1, counter + 0x4008, 8)
            try package(nca([code, malformed], rights: rights), type: 0x81, version: 2, rights: rights).write(to: updateURL)
            try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title) { _ in } }
        }
        try package(updated, type: 0x81, version: 2, rights: rights).write(to: updateURL)
        var wrongKeys = keys; wrongKeys["titlekek_00"] = Data(count: 16)
        try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: wrongKeys, applicationID: title) { _ in } }
        try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title, cancelled: { true }) { _ in } }
        try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: ExecutableTests.keys, applicationID: title) { _ in } }
        try package(updated, type: 0x81, version: 2).write(to: updateURL)
        try fails { _ = try SwitchContentPipeline.withContent(base: baseURL, update: updateURL, keys: keys, applicationID: title) { _ in } }
        let baseMeta = try SwitchContentMeta.parse(cnmt(original, type: 0x80, id: title, version: 0), metaTitleID: title)
        for version: UInt64 in [0, 2] {
            let wrong = try SwitchContentMeta.parse(cnmt(updated, type: 0x81, id: version == 0 ? title + 0x800 : title + 0x1000, version: version), metaTitleID: version == 0 ? title + 0x800 : title + 0x1000)
            try fails { _ = try SwitchContentMeta.select(base: [baseMeta], update: [wrong], applicationID: title) }
        }
        var badCnmt = cnmt(original, type: 0x80, id: title, version: 0); badCnmt[0x30] ^= 1
        try fails { _ = try SwitchContentMeta.parse(badCnmt, metaTitleID: title) }
        var compacted = cnmt(original, type: 0x80, id: title, version: 0); compacted[20] = 4
        try fails { _ = try SwitchContentMeta.parse(compacted, metaTitleID: title) }
        var cut = cnmt(original, type: 0x80, id: title, version: 0); cut.removeLast()
        try fails { _ = try SwitchContentMeta.parse(cut, metaTitleID: title) }
        var rawModule = Data(count: 265)
        rawModule.replaceSubrange(0..<4, with: Data("NSO0".utf8)); put(0x38, &rawModule, 12, 4)
        for (i, pair) in [(256, 4), (260, 2), (262, 3)].enumerated() {
            put(UInt64(pair.0), &rawModule, 16 + i * 16, 4); put(UInt64(i * 4096), &rawModule, 20 + i * 16, 4)
            put(UInt64(pair.1), &rawModule, 24 + i * 16, 4)
            let bytes = Data(repeating: UInt8(i + 1), count: pair.1)
            rawModule.replaceSubrange(pair.0..<pair.0 + pair.1, with: bytes)
            rawModule.replaceSubrange(160 + i * 32..<192 + i * 32, with: Data(SHA256.hash(data: bytes)))
        }
        put(16, &rawModule, 60, 4)
        let decoded = try SwitchDecodedModule.decode(SwitchExecutableModule(name: "main", data: rawModule))
        try check(decoded.bssSize == 16 && decoded.segments.map(\.offset) == [0, 4096, 8192])
        try check(decoded.segments[1].bytes == Data([2, 2]))
        var corruptModule = rawModule; corruptModule[256] ^= 1
        try fails { _ = try SwitchDecodedModule.decode(SwitchExecutableModule(name: "main", data: corruptModule)) }
        corruptModule = rawModule; put(0x80, &corruptModule, 12, 4)
        try fails { _ = try SwitchDecodedModule.decode(SwitchExecutableModule(name: "main", data: corruptModule)) }
        let incomplete = SwitchContentStorage(size: 10) { _, _ in Data() }
        try fails { _ = try incomplete.bytes(0, 10) }
        let nodes: [(String, Data?)] = [("z", nil), ("z/nested", nil), ("empty", nil),
            ("a", Data("lower".utf8)), ("A", Data("UPPER".utf8)), ("z/é", Data([1, 2, 3])),
            ("z/nested/leaf", Data()), ("z/nested/big", Data(repeating: 0x67, count: 1024 * 1024 + 33))]
        let directoryRaw = directoryFixture(nodes)
        let directoryFS = try SwitchRomFS(storage: verified(directoryRaw))
        let directoryImage = scratch.appendingPathComponent("tree.img")
        let treeResult = try SwitchRomFSImage.write(directoryFS, to: directoryImage)
        try check(treeResult.entryCount == nodes.count + 1 && treeResult.fileCount == 5)
        let oracle = scratch.appendingPathComponent("oracle-tree", isDirectory: true)
        if FileManager.default.fileExists(atPath: oracle.path) { try FileManager.default.removeItem(at: oracle) }
        try FileManager.default.createDirectory(at: oracle, withIntermediateDirectories: true)
        for (path, value) in nodes {
            let url = oracle.appendingPathComponent(path == "a" ? ".dm-case-0" : (path == "z/é" ? "z/.dm-case-1" : path))
            if let value { try value.write(to: url) }
            else { try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true) }
        }
        let originalImage = try Data(contentsOf: directoryImage)
        let damagedPair = RomFSTests.fixture()
        var damagedBytes = damagedPair.1; damagedBytes[2560 + 700] ^= 1
        let damagedStorage = try SwitchIntegrityStorage(header: damagedPair.0, sectionSize: UInt64(damagedBytes.count)) { offset, count in
            damagedBytes.subdata(in: Int(offset)..<Int(offset + count))
        }
        let damagedFS = try SwitchRomFS(storage: damagedStorage)
        try fails { _ = try SwitchRomFSImage.write(damagedFS, to: directoryImage) }
        try check(try Data(contentsOf: directoryImage) == originalImage)
        try check(try FileManager.default.contentsOfDirectory(atPath: scratch.path).allSatisfy { !$0.hasSuffix(".part") })
        var polls = 0
        try fails { _ = try SwitchRomFSImage.write(directoryFS, to: directoryImage, cancelled: { polls += 1; return polls == 4 }) }
        var cycle = directoryRaw; put(0, &cycle, 88, 4)
        let cyclic = try SwitchRomFS(storage: verified(cycle))
        try fails { _ = try cyclic.tree() }
        var unreachable = directoryRaw; put(UInt64(UInt32.max), &unreachable, 88, 4)
        let orphan = try SwitchRomFS(storage: verified(unreachable))
        try fails { _ = try orphan.tree() }
        try fails { _ = try SwitchRomFSImage.write(directoryFS, to: scratch.appendingPathComponent("absent/file")) }
        print("PASS content synthetic assertions=\(assertions)")
    }
    static func main() throws {
        let scratch = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        try ExecutableTests.run()
        try RomFSTests.run()
        try run(scratch)
    }
}
