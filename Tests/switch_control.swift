import CryptoKit
import Foundation

@main struct ControlTests {
    static let headerKey = Data((0..<32).map(UInt8.init)), areaKey = Data((32..<48).map(UInt8.init)),
        contentKey = Data(repeating: 0x71, count: 16)
    static let keys = ["header_key": headerKey, "key_area_key_application_00": areaKey]
    static func put(_ n: UInt64, _ d: inout Data, _ p: Int, _ count: Int) {
        for i in 0..<count { d[p + i] = UInt8(truncatingIfNeeded: n >> (8 * i)) }
    }
    static func hash(_ d: Data) -> Data { Data(SHA256.hash(data: d)) }
    static func id(_ d: Data) -> String { hash(d).prefix(16).map { String(format: "%02x", $0) }.joined() }
    static func crypt(_ d: Data, _ k: Data, _ mode: Int, _ encrypt: Bool = true, _ counter: Data = Data(count: 16))
        throws -> Data
    {
        var out = Data(count: d.count)
        let result = out.withUnsafeMutableBytes { o in
            d.withUnsafeBytes { a in
                k.withUnsafeBytes { b in
                    let dst = o.bindMemory(to: UInt8.self).baseAddress!
                    let src = a.bindMemory(to: UInt8.self).baseAddress!
                    let key = b.bindMemory(to: UInt8.self).baseAddress!
                    if mode == 0 { return satchel_switch_aes_ecb(encrypt ? 1 : 0, key, src, dst, d.count) }
                    if mode == 1 { return satchel_switch_aes_xts(encrypt ? 1 : 0, key, 0, src, dst, d.count) }
                    return counter.withUnsafeBytes {
                        satchel_switch_aes_ctr(key, $0.bindMemory(to: UInt8.self).baseAddress!, src, dst, d.count)
                    }
                }
            }
        }
        guard result == 0 else { throw SwitchExecutableError.invalid }
        return out
    }
    static func pfs(_ entries: [(String, Data)]) -> Data {
        var d = Data(count: 16 + 24 * entries.count)
        var names = Data()
        var offset = 0
        d.replaceSubrange(0..<4, with: Data("PFS0".utf8))
        put(UInt64(entries.count), &d, 4, 4)
        for (i, e) in entries.enumerated() {
            put(UInt64(offset), &d, 16 + 24 * i, 8)
            put(UInt64(e.1.count), &d, 24 + 24 * i, 8)
            put(UInt64(names.count), &d, 32 + 24 * i, 4)
            names.append(Data(e.0.utf8))
            names.append(0)
            offset += e.1.count
        }
        put(UInt64(names.count), &d, 8, 4)
        d.append(names)
        for e in entries { d.append(e.1) }
        return d
    }
    static func pfsSection(_ d: Data) -> (Data, Data) {
        var hashes = Data()
        for p in stride(from: 0, to: d.count, by: 512) { hashes.append(hash(d.subdata(in: p..<min(d.count, p + 512)))) }
        let offset = (hashes.count + 511) / 512 * 512
        var fs = Data(count: 512)
        var section = Data(count: offset + (d.count + 511) / 512 * 512)
        put(2, &fs, 0, 2)
        fs[2] = 1
        fs[3] = 2
        fs[4] = 3
        fs.replaceSubrange(8..<40, with: hash(hashes))
        put(512, &fs, 0x28, 4)
        put(2, &fs, 0x2c, 4)
        put(UInt64(hashes.count), &fs, 0x38, 8)
        put(UInt64(offset), &fs, 0x40, 8)
        put(UInt64(d.count), &fs, 0x48, 8)
        section.replaceSubrange(0..<hashes.count, with: hashes)
        section.replaceSubrange(offset..<offset + d.count, with: d)
        return (fs, section)
    }
    static func controlSection(_ nacp: Data, _ name: String = "control.nacp", corruptIVFC: Bool = false) -> (Data, Data)
    {
        let fileEntrySize = (32 + name.utf8.count + 3) & ~3
        let dataOffset = 112 + fileEntrySize
        var rom = Data(count: dataOffset + nacp.count)
        put(80, &rom, 0, 8)
        for (i, pair) in [(80, 4), (84, 24), (108, 4), (112, fileEntrySize)].enumerated() {
            put(UInt64(pair.0), &rom, 8 + i * 16, 8)
            put(UInt64(pair.1), &rom, 16 + i * 16, 8)
        }
        put(UInt64(dataOffset), &rom, 72, 8)
        put(UInt64(UInt32.max), &rom, 88, 4)
        put(UInt64(UInt32.max), &rom, 92, 4)
        put(UInt64(UInt32.max), &rom, 100, 4)
        put(UInt64(UInt32.max), &rom, 116, 4)
        put(UInt64(nacp.count), &rom, 128, 8)
        put(UInt64(UInt32.max), &rom, 136, 4)
        put(UInt64(name.utf8.count), &rom, 140, 4)
        rom.replaceSubrange(144..<144 + name.utf8.count, with: Data(name.utf8))
        rom.replaceSubrange(dataOffset..<rom.count, with: nacp)
        func hashes(_ d: Data) -> Data {
            var out = Data()
            for p in stride(from: 0, to: d.count, by: 512) {
                var block = d.subdata(in: p..<min(d.count, p + 512))
                block.append(Data(count: 512 - block.count))
                out.append(hash(block))
            }
            return out
        }
        var levels = [rom]
        for _ in 0..<5 { levels.insert(hashes(levels[0]), at: 0) }
        var fs = Data(count: 512)
        var section = Data()
        put(2, &fs, 0, 2)
        fs[3] = 3
        fs[4] = 3
        fs.replaceSubrange(8..<12, with: Data("IVFC".utf8))
        put(0x20000, &fs, 12, 4)
        put(32, &fs, 16, 4)
        put(7, &fs, 20, 4)
        for (i, level) in levels.enumerated() {
            put(UInt64(section.count), &fs, 24 + i * 24, 8)
            put(UInt64(level.count), &fs, 32 + i * 24, 8)
            put(9, &fs, 40 + i * 24, 4)
            section.append(level)
            section.append(Data(count: (512 - level.count % 512) % 512))
        }
        fs.replaceSubrange(0xc8..<0xe8, with: hashes(levels[0]))
        if corruptIVFC { section[section.count - 512] ^= 1 }
        return (fs, section)
    }
    static func nca(_ title: UInt64, _ type: UInt8, _ pair: (Data, Data), rights: Bool = false) -> Data {
        var fs = pair.0
        var header = Data(count: 0xc00)
        var counter = Data(count: 16)
        counter[15] = 0xc0  // NCA-relative AES-CTR block index0xc00/16.
        fs.replaceSubrange(0x140..<0x148, with: Data(count: 8))
        let section = try! crypt(pair.1, contentKey, 2, true, counter)
        header.replaceSubrange(0x200..<0x204, with: Data("NCA3".utf8))
        header[0x204] = 1
        header[0x205] = type
        put(UInt64(header.count + section.count), &header, 0x208, 8)
        put(title, &header, 0x210, 8)
        put(6, &header, 0x240, 4)
        put(UInt64((header.count + section.count) / 512), &header, 0x244, 4)
        header.replaceSubrange(0x400..<0x600, with: fs)
        header.replaceSubrange(0x280..<0x2a0, with: hash(fs))
        var area = Data(count: 64)
        area.replaceSubrange(32..<48, with: contentKey)
        header.replaceSubrange(0x300..<0x340, with: try! crypt(area, areaKey, 0))
        if rights { header[0x230] = 1 }
        return try! crypt(header, headerKey, 1) + section
    }
    static func cnmt(_ title: UInt64, _ refs: [(Data, UInt8, UInt8)], _ mutate: (inout Data) -> Void = { _ in }) -> Data
    {
        var d = Data(count: 0x30 + 0x38 * refs.count + 32)
        put(title, &d, 0, 8)
        put(65536, &d, 8, 4)
        d[12] = 0x80
        put(16, &d, 14, 2)
        put(UInt64(refs.count), &d, 16, 2)
        put(title + 0x800, &d, 0x20, 8)
        for (i, r) in refs.enumerated() {
            let p = 0x30 + i * 0x38
            d.replaceSubrange(p..<p + 32, with: hash(r.0))
            d.replaceSubrange(p + 32..<p + 48, with: hash(r.0).prefix(16))
            put(UInt64(r.0.count), &d, p + 0x30, 5)
            d[p + 0x36] = r.1
            d[p + 0x37] = r.2
        }
        mutate(&d)
        let digest = hash(d.dropLast(32))
        d.replaceSubrange(d.count - 32..<d.count, with: digest)
        return d
    }
    struct Fixture {
        let title: UInt64, program: Data, control: Data, meta: Data, nacp: Data
        var entries: [(String, Data)] {
            [(id(control) + ".nca", control), (id(meta) + ".cnmt.nca", meta), (id(program) + ".nca", program)]
        }
    }
    static func fixture(
        _ title: UInt64, flags: UInt32 = 1, offset: UInt8 = 0, badIVFC: Bool = false, nacpSize: Int = 0x4000,
        cnmtMutation: (inout Data) -> Void = { _ in }, controlTitle: UInt64? = nil, controlRights: Bool = false
    ) -> Fixture {
        var nacp = Data(repeating: 0xA7, count: nacpSize)
        if nacpSize >= 0x3078 {
            put(UInt64(flags), &nacp, 0x3030, 4)
            for i in 0..<32 { nacp[0x3040 + i] = i == 0 ? 0xff : UInt8(i) }
            put(title + 0x1000, &nacp, 0x3070, 8)
        }
        let program = nca(
            title, 0,
            pfsSection(
                pfs([("rtld", Data("NSO0".utf8) + Data(count: 252)), ("main", Data("NSO0".utf8) + Data(count: 252))])))
        let control = nca(controlTitle ?? title, 2, controlSection(nacp, corruptIVFC: badIVFC), rights: controlRights)
        let meta = nca(
            title, 1,
            pfsSection(pfs([("Application.cnmt", cnmt(title, [(program, 1, offset), (control, 3, 0)], cnmtMutation))])))
        return Fixture(title: title, program: program, control: control, meta: meta, nacp: nacp)
    }
    static func run() throws {
        var assertions = 0
        func check(_ value: Bool) throws {
            assertions += 1
            if !value { throw NSError(domain: "ControlFixture", code: assertions) }
        }
        let scratch = FileManager.default.temporaryDirectory.appendingPathComponent(
            "satchel-control-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: scratch) }
        let url = scratch.appendingPathComponent("original.nsp")
        let a = fixture(0x0100_0000_0000_1000)
        let b = fixture(0x0100_0000_0000_2000, flags: 0)
        func write(_ entries: [(String, Data)]) throws { try pfs(entries).write(to: url) }
        func load(_ title: UInt64 = 0x0100_0000_0000_1000, _ cancel: @escaping () -> Bool = { false }) throws
            -> SwitchControlMetadata
        {
            try SwitchExecutableReader.controlMetadata(
                url, keys: keys, titleID: String(format: "%016llx", title), cancelled: cancel)
        }
        func fails(_ body: () throws -> Void, _ expected: SwitchExecutableError) throws {
            do { try body() } catch let e as SwitchExecutableError {
                try check(String(describing: e) == String(describing: expected))
                return
            }
            throw NSError(domain: "AcceptedControlFixture", code: assertions)
        }
        try write(b.entries + a.entries)
        let first = try load()
        let second = try load(b.title)
        try check(
            first.applicationID == a.title && first.applicationVersion == 65536
                && first.controlContentID == id(a.control) && first.programContentID == id(a.program))
        try check(
            first.parentalControlFlags == 1 && second.parentalControlFlags == 0
                && second.controlContentID == id(b.control))
        try check(first.ratingAges == [-1] + (1..<32).map(Int8.init) && first.addOnContentBaseID == a.title + 0x1000)
        try check(
            first.metaHashBytes == a.meta.count && first.controlHashBytes == a.control.count
                && first.metaVerifiedBlocks > 0 && first.controlVerifiedBlocks > 0)
        try check(try SwitchExecutableReader.programs(url, keys: keys).count == 2)
        try check(
            try SwitchExecutableReader.load(url, keys: keys, titleID: String(format: "%016llx", a.title)).modules.count
                == 2)
        // Original archive order and internal filename never select a title.
        try write(Array(a.entries.reversed()) + Array(b.entries.reversed()))
        try check(try load() == first)
        for variant in [fixture(a.title, offset: 1), fixture(a.title, controlRights: true)] {
            try write(variant.entries)
            try fails({ _ = try load() }, .unsupported)
        }
        try write(fixture(a.title, controlTitle: b.title).entries)
        try fails({ _ = try load() }, .invalid)
        try write(fixture(a.title, nacpSize: 0x3fff).entries)
        try fails({ _ = try load() }, .invalid)
        try write(fixture(a.title, badIVFC: true).entries)
        try fails({ _ = try load() }, .integrity)
        for mutate: (inout Data) -> Void in [
            { put(a.title + 1, &$0, 0, 8) }, { put(0xffff, &$0, 16, 2) }, { put(0, &$0, 16, 2) },
            { put(0xffff, &$0, 14, 2) }, { put(UInt64(a.program.count + 1), &$0, 0x60, 5) },
            { $0[0x30] ^= 1 }, { $0[0x30 + 0x37] = 1 }, { $0[0x30 + 0x35] = 1 }, { $0[0xC] = 0x81 },
            { $0[0x30 + 0x38 + 0x36] = 1 },
        ] {
            let malformed = fixture(a.title, cnmtMutation: mutate)
            try write(malformed.entries)
            do {
                _ = try load()
                throw NSError(domain: "AcceptedMalformedControl", code: assertions)
            } catch is SwitchExecutableError { assertions += 1 }
        }
        try write(a.entries.filter { !$0.0.hasSuffix("cnmt.nca") })
        try fails({ _ = try load() }, .invalid)
        try write(a.entries.filter { $0.0 != id(a.control) + ".nca" })
        try fails({ _ = try load() }, .invalid)
        // CNMT names a different actual Program, never infer the same title alone.
        let otherProgram = nca(a.title, 0, pfsSection(pfs([("different", Data(count: 512))])))
        let wrongMeta = nca(
            a.title, 1,
            pfsSection(pfs([("Application.cnmt", cnmt(a.title, [(otherProgram, 1, 0), (a.control, 3, 0)]))])))
        try write([
            (id(wrongMeta) + ".cnmt.nca", wrongMeta), (id(a.control) + ".nca", a.control),
            (id(a.program) + ".nca", a.program),
        ])
        try fails({ _ = try load() }, .invalid)
        var corrupt = a.control
        corrupt[0xc10] ^= 1
        try write([
            (id(a.meta) + ".cnmt.nca", a.meta), (id(a.control) + ".nca", corrupt), (id(a.program) + ".nca", a.program),
        ])
        try fails({ _ = try load() }, .integrity)
        corrupt = a.meta
        corrupt[0xc10] ^= 1
        try write([
            (id(a.meta) + ".cnmt.nca", corrupt), (id(a.control) + ".nca", a.control),
            (id(a.program) + ".nca", a.program),
        ])
        try fails({ _ = try load() }, .integrity)
        let duplicateMeta = nca(
            a.title, 1, pfsSection(pfs([("second.cnmt", cnmt(a.title, [(a.program, 1, 0), (a.control, 3, 0)]))])))
        try write(a.entries + [(id(duplicateMeta) + ".cnmt.nca", duplicateMeta)])
        try fails({ _ = try load() }, .ambiguous)
        try write(a.entries)
        var reads = 0
        _ = try load(
            a.title,
            {
                reads += 1
                return false
            })
        try check(reads > 20)
        for boundary in [1, reads / 3, reads / 2, reads - 1] {
            var count = 0
            do {
                _ = try load(
                    a.title,
                    {
                        count += 1
                        return count >= boundary
                    })
                throw NSError(domain: "ControlCancellationIgnored", code: boundary)
            } catch is CancellationError { try check(count == boundary) }
        }
        // Parser boundaries are independently exercised before any directory IO.
        for n in [0, 31, 47, 79] {
            try fails({ _ = try SwitchApplicationMeta.parse(Data(count: n), metaTitleID: a.title) }, .invalid)
        }
        let record = cnmt(a.title, [(a.program, 1, 0), (a.control, 3, 0)])
        for n in [record.count - 1, record.count + 1] {
            var bytes = record
            if n < bytes.count { bytes.removeLast() } else { bytes.append(0) }
            try fails({ _ = try SwitchApplicationMeta.parse(bytes, metaTitleID: a.title) }, .invalid)
        }
        try fails({ _ = try SwitchControlValues.parse(Data(count: 0x4001)) }, .invalid)
        // Duplicate references and distinct controls cannot be selected by order.
        for refs: [(Data, UInt8, UInt8)] in [
            [(a.program, 1, 0), (a.control, 3, 0), (a.control, 3, 0)],
            [(a.program, 1, 0), (a.control, 3, 1)],
            [(a.program, 1, 0)],
            [(a.program, 1, 0), (a.control, 3, 0), (b.control, 3, 1)],
        ] {
            let meta = nca(a.title, 1, pfsSection(pfs([("Application.cnmt", cnmt(a.title, refs))])))
            try write([
                (id(meta) + ".cnmt.nca", meta), (id(a.control) + ".nca", a.control),
                (id(b.control) + ".nca", b.control), (id(a.program) + ".nca", a.program),
            ])
            do {
                _ = try load()
                throw NSError(domain: "AmbiguousControlAccepted", code: assertions)
            } catch is SwitchExecutableError { assertions += 1 }
        }
        // Same first128 digest bits still require the complete256-bit Control hash.
        try write(fixture(a.title, cnmtMutation: { $0[0x30 + 0x38 + 16] ^= 1 }).entries)
        try fails({ _ = try load() }, .integrity)
        let duplicateCnmt = nca(a.title, 1, pfsSection(pfs([("a.cnmt", record), ("b.cnmt", record)])))
        try write([
            (id(duplicateCnmt) + ".cnmt.nca", duplicateCnmt), (id(a.program) + ".nca", a.program),
            (id(a.control) + ".nca", a.control),
        ])
        try fails({ _ = try load() }, .invalid)
        var malformedPfs = pfs([("Application.cnmt", record)])
        put(UInt64.max, &malformedPfs, 16, 8)
        let malformedMeta = nca(a.title, 1, pfsSection(malformedPfs))
        try write([
            (id(malformedMeta) + ".cnmt.nca", malformedMeta), (id(a.program) + ".nca", a.program),
            (id(a.control) + ".nca", a.control),
        ])
        try fails({ _ = try load() }, .invalid)
        var brokenPair = pfsSection(pfs([("Application.cnmt", record)]))
        brokenPair.0[8] ^= 1
        let badMaster = nca(a.title, 1, brokenPair)
        try write([
            (id(badMaster) + ".cnmt.nca", badMaster), (id(a.program) + ".nca", a.program),
            (id(a.control) + ".nca", a.control),
        ])
        try fails({ _ = try load() }, .integrity)
        brokenPair = pfsSection(pfs([("Application.cnmt", record)]))
        brokenPair.1[512 + 1] ^= 1
        let badBlock = nca(a.title, 1, brokenPair)
        try write([
            (id(badBlock) + ".cnmt.nca", badBlock), (id(a.program) + ".nca", a.program),
            (id(a.control) + ".nca", a.control),
        ])
        try fails({ _ = try load() }, .integrity)
        try write([
            (String(repeating: "f", count: 32) + ".cnmt.nca", a.meta), (id(a.program) + ".nca", a.program),
            (id(a.control) + ".nca", a.control),
        ])
        try fails({ _ = try load() }, .integrity)
        // Data slices may have nonzero indices; public parsers normalize them.
        let prefixed = Data([0x99]) + record
        let prefixedNacp = Data([0x99]) + a.nacp
        try check(try SwitchApplicationMeta.parse(prefixed.dropFirst(), metaTitleID: a.title).version == 65536)
        try check(try SwitchControlValues.parse(prefixedNacp.dropFirst()).parentalFlags == 1)
        // Original bounded parser mutation corpus: any accepted shape must still
        // have a valid nonzero ID, bounded references and coherent selection.
        var rng: UInt64 = 0x123_4567_89ab_cdef
        for _ in 0..<1024 {
            var bytes = record
            rng = rng &* 6_364_136_223_846_793_005 &+ 1
            let at = Int(rng % UInt64(bytes.count))
            bytes[at] ^= UInt8(truncatingIfNeeded: rng >> 32) | 1
            if rng & 7 == 0 { bytes = Data(bytes.prefix(Int((rng >> 8) % UInt64(bytes.count)))) }
            do {
                let parsed = try SwitchApplicationMeta.parse(bytes, metaTitleID: a.title)
                try check(parsed.applicationID == a.title && parsed.contents.count <= 4096)
            } catch is SwitchExecutableError { assertions += 1 }
        }
        let noNacp = nca(a.title, 2, controlSection(a.nacp, "other"))
        let noNacpMeta = nca(
            a.title, 1, pfsSection(pfs([("Application.cnmt", cnmt(a.title, [(a.program, 1, 0), (noNacp, 3, 0)]))])))
        try write([
            (id(noNacpMeta) + ".cnmt.nca", noNacpMeta), (id(a.program) + ".nca", a.program),
            (id(noNacp) + ".nca", noNacp),
        ])
        try fails({ _ = try load() }, .invalid)
        // A sparse65MiB Control is rejected before hashing/reading its body.
        let hugeSize: UInt64 = 65 * 1024 * 1024
        var hugeHeader = try crypt(Data(a.control.prefix(0xc00)), headerKey, 1, false)
        put(hugeSize, &hugeHeader, 0x208, 8)
        put(hugeSize / 512, &hugeHeader, 0x244, 4)
        let encryptedHeader = try crypt(hugeHeader, headerKey, 1)
        var hugeRecord = cnmt(a.title, [(a.program, 1, 0), (a.control, 3, 0)])
        put(hugeSize, &hugeRecord, 0x30 + 0x38 + 0x30, 5)
        let hugeMeta = nca(a.title, 1, pfsSection(pfs([("Application.cnmt", hugeRecord)])))
        var sparseOuter = pfs([
            (id(a.program) + ".nca", a.program), (id(hugeMeta) + ".cnmt.nca", hugeMeta),
            (id(a.control) + ".nca", Data()),
        ])
        put(hugeSize, &sparseOuter, 24 + 2 * 24, 8)
        try sparseOuter.write(to: url)
        let sparse = try FileHandle(forWritingTo: url)
        try sparse.seekToEnd()
        try sparse.write(contentsOf: encryptedHeader)
        try sparse.truncate(atOffset: UInt64(sparseOuter.count) + hugeSize)
        try sparse.close()
        try fails({ _ = try load() }, .limit)
        // Selected Program identity is checked against NCA type/title as well
        // as CNMT content ID and the separately tested declared archive size.
        for badProgram in [
            nca(a.title, 4, pfsSection(pfs([("not-program", Data(count: 512))]))),
            nca(b.title, 0, pfsSection(pfs([("rtld", Data(count: 512))]))),
        ] {
            let badMeta = nca(
                a.title, 1,
                pfsSection(pfs([("Application.cnmt", cnmt(a.title, [(badProgram, 1, 0), (a.control, 3, 0)]))])))
            try write([
                (id(badMeta) + ".cnmt.nca", badMeta), (id(badProgram) + ".nca", badProgram),
                (id(a.control) + ".nca", a.control),
            ])
            try fails({ _ = try load() }, .noProgram)
        }
        try write(fixture(a.title, flags: UInt32.max).entries)
        try check(try load().parentalControlFlags == UInt32.max)  // Raw metadata, never a policy decision.
        print(
            "Control reader: \(assertions) original multi-title, integrity, bounds and cancellation assertions passed")
        let env = ProcessInfo.processInfo.environment
        if let package = env["SATCHEL_SWITCH_TEST_ROM"], let keyPath = env["SATCHEL_SWITCH_TEST_KEYS"],
            let title = env["SATCHEL_SWITCH_TEST_TITLE"]
        {
            let keyData = try Data(contentsOf: URL(fileURLWithPath: keyPath))
            let material = try SwitchKeys.parse(keyData)
            for selected in title.split(separator: ",") {
                let metadata = try SwitchExecutableReader.controlMetadata(
                    URL(fileURLWithPath: package), keys: material, titleID: String(selected))
                print(
                    "Real Control metadata verified: version=\(metadata.applicationVersion), policyFlags=\(metadata.parentalControlFlags), ratings=\(metadata.ratingAges.count), metaBytes=\(metadata.metaHashBytes), controlBytes=\(metadata.controlHashBytes), metaBlocks=\(metadata.metaVerifiedBlocks), controlBlocks=\(metadata.controlVerifiedBlocks). Full Program hash and publisher signatures not checked."
                )
            }
        }
    }
    static func main() {
        do { try run() } catch {
            FileHandle.standardError.write(Data("Control reader failed: \(error)\n".utf8))
            exit(1)
        }
    }
}
