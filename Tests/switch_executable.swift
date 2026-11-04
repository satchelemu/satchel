import CryptoKit
import Foundation

@main
struct ExecutableTests {
    static let headerKey = Data((0..<32).map(UInt8.init))
    static let areaKey = Data((32..<48).map(UInt8.init))
    static let contentKey = Data(repeating: 0x71, count: 16)
    static let keys = ["header_key": headerKey, "key_area_key_application_00": areaKey]
    static let title: UInt64 = 0x0100000000001000
    static func put(_ n: UInt64, _ bytes: inout Data, _ offset: Int, _ count: Int) {
        for i in 0..<count { bytes[offset + i] = UInt8(truncatingIfNeeded: n >> (i * 8)) }
    }
    static func hex(_ value: String) -> Data {
        Data(stride(from: 0, to: value.count, by: 2).map { i in
            UInt8(value[value.index(value.startIndex, offsetBy: i)..<value.index(value.startIndex, offsetBy: i + 2)], radix: 16)!
        })
    }
    static func transform(_ data: Data, key: Data, mode: Int, encrypt: Bool = true, counter: Data = Data(repeating: 0, count: 16)) throws -> Data {
        var output = Data(count: data.count)
        let status = output.withUnsafeMutableBytes { out in data.withUnsafeBytes { input in key.withUnsafeBytes { k in
            let source = input.bindMemory(to: UInt8.self).baseAddress!, destination = out.bindMemory(to: UInt8.self).baseAddress!, material = k.bindMemory(to: UInt8.self).baseAddress!
            if mode == 0 { return satchel_switch_aes_ecb(encrypt ? 1 : 0, material, source, destination, data.count) }
            if mode == 1 { return satchel_switch_aes_xts(encrypt ? 1 : 0, material, 0, source, destination, data.count) }
            return counter.withUnsafeBytes { satchel_switch_aes_ctr(material, $0.bindMemory(to: UInt8.self).baseAddress!, source, destination, data.count) }
        } } }
        guard status == 0 else { throw SwitchExecutableError.invalid }
        return output
    }
    static func pfs(_ entries: [(String, Data)]) -> Data {
        var names = Data(), table = Data(repeating: 0, count: 16 + entries.count * 24), offset = 0
        table.replaceSubrange(0..<4, with: Data("PFS0".utf8)); put(UInt64(entries.count), &table, 4, 4)
        for (i, entry) in entries.enumerated() {
            put(UInt64(offset), &table, 16 + i * 24, 8); put(UInt64(entry.1.count), &table, 24 + i * 24, 8)
            put(UInt64(names.count), &table, 32 + i * 24, 4)
            names.append(contentsOf: entry.0.utf8); names.append(0); offset += entry.1.count
        }
        put(UInt64(names.count), &table, 8, 4); table.append(names)
        for entry in entries { table.append(entry.1) }
        return table
    }
    static func module(_ marker: UInt8) -> Data {
        var result = Data(repeating: marker, count: 640)
        result.replaceSubrange(0..<4, with: Data("NSO0".utf8)); return result
    }
    // Extraction fixture only. Semantic META/ACI0/ACID validation belongs to
    // process_metadata.c; these original bytes exercise authenticated transport.
    static let metadata = Data("META".utf8) + Data(repeating: 3, count: 124)
    static func nca(titleID: UInt64 = title, rights: Bool = false, encryption: UInt8 = 3,
                    extended: Bool = false, badFsHash: Bool = false, badMaster: Bool = false,
                    badLayer: Bool = false, overlap: Bool = false, moduleMagic: Bool = true,
                    badDirectory: Bool = false, oversizedModule: Bool = false,
                    processMetadata: Data? = metadata, badMetadataRange: Bool = false,
                    duplicateMetadata: Bool = false) throws -> Data {
        var payloads = [("main", moduleMagic ? module(0x51) : Data(repeating: 0, count: 640)), ("rtld", module(0x42))]
        if let processMetadata { payloads.append(("main.npdm", processMetadata)) }
        if duplicateMetadata { payloads.append(("main.npdm", metadata)) }
        var exefs = pfs(payloads)
        if badDirectory { put(0, &exefs, 16 + 24, 8) }
        if oversizedModule { put(128 * 1024 * 1024 + 1, &exefs, 24, 8) }
        if badMetadataRange { put(UInt64.max, &exefs, 24 + 2 * 24, 8) }
        let blocks = (exefs.count + 511) / 512
        var hashes = Data()
        for i in 0..<blocks { hashes.append(contentsOf: SHA256.hash(data: exefs[(i * 512)..<min(exefs.count, (i + 1) * 512)])) }
        let pfsOffset = (hashes.count + 511) / 512 * 512
        var fs = Data(repeating: 0, count: 512)
        put(2, &fs, 0, 2); fs[2] = 1; fs[3] = 2; fs[4] = encryption
        fs.replaceSubrange(8..<40, with: Data(SHA256.hash(data: hashes)))
        if badMaster { fs[8] ^= 1 }
        put(512, &fs, 0x28, 4); put(badLayer ? 3 : 2, &fs, 0x2c, 4)
        put(UInt64(hashes.count), &fs, 0x38, 8); put(UInt64(pfsOffset), &fs, 0x40, 8); put(UInt64(exefs.count), &fs, 0x48, 8)
        let upper = Data([1, 2, 3, 4, 5, 6, 7, 8]); fs.replaceSubrange(0x140..<0x148, with: upper.reversed())
        if extended { fs[0x180] = 1 }
        var section = Data(repeating: 0, count: pfsOffset + blocks * 512)
        section.replaceSubrange(0..<hashes.count, with: hashes); section.replaceSubrange(pfsOffset..<(pfsOffset + exefs.count), with: exefs)
        var counter = upper; for shift in (0..<8).reversed() { counter.append(UInt8(truncatingIfNeeded: (UInt64(0xc00) / 16) >> (shift * 8))) }
        if encryption != 1 { section = try transform(section, key: contentKey, mode: 2, counter: counter) }
        var header = Data(repeating: 0, count: 0xc00)
        header.replaceSubrange(0x200..<0x204, with: Data("NCA3".utf8)); header[0x204] = 1
        put(UInt64(header.count + section.count), &header, 0x208, 8); put(titleID, &header, 0x210, 8)
        if rights { header[0x230] = 1 }
        put(6, &header, 0x240, 4); put(UInt64((header.count + section.count) / 512), &header, 0x244, 4)
        header.replaceSubrange(0x400..<0x600, with: fs)
        header.replaceSubrange(0x280..<0x2a0, with: Data(SHA256.hash(data: fs)))
        if badFsHash { header[0x280] ^= 1 }
        if overlap {
            header.replaceSubrange(0x250..<0x260, with: header[0x240..<0x250])
            header.replaceSubrange(0x600..<0x800, with: fs)
            header.replaceSubrange(0x2a0..<0x2c0, with: Data(SHA256.hash(data: fs)))
        }
        var area = Data(repeating: 0, count: 64); area.replaceSubrange(32..<48, with: contentKey)
        header.replaceSubrange(0x300..<0x340, with: try transform(area, key: areaKey, mode: 0))
        var result = try transform(header, key: headerKey, mode: 1); result.append(section); return result
    }
    static func run() throws {
        let scratch = FileManager.default.temporaryDirectory.appendingPathComponent("satchel-executable-" + UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: scratch) }
        let url = scratch.appendingPathComponent("fixture.nsp")
        let id = String(format: "%016llx", title), content = String(repeating: "1", count: 32)
        var assertions = 0
        func check(_ value: Bool) throws { assertions += 1; if !value { throw NSError(domain: "SyntheticAssertion", code: assertions) } }
        func archive(_ nca: Data) throws { try pfs([(content + ".nca", nca)]).write(to: url) }
        func load() throws -> SwitchProgramImage { try SwitchExecutableReader.load(url, keys: keys, titleID: id) }
        func reject(_ data: Data, _ expected: SwitchExecutableError) throws {
            try archive(data)
            do { _ = try load() }
            catch let error as SwitchExecutableError { try check(String(describing: error) == String(describing: expected)); return }
            throw NSError(domain: "AcceptedMalformedFixture", code: assertions)
        }
        // Published AES known-answer values: FIPS-197 AES-128 and NIST
        // SP800-38A F.5.1 AES-CTR. No user credentials are fixture inputs.
        let zero = Data(repeating: 0, count: 16)
        let ecbKey = hex("000102030405060708090a0b0c0d0e0f"), ecbPlain = hex("00112233445566778899aabbccddeeff")
        let ecbCipher = hex("69c4e0d86a7b0430d8cdb78070b4c55a")
        try check(try transform(ecbPlain, key: ecbKey, mode: 0) == ecbCipher)
        try check(try transform(ecbCipher, key: ecbKey, mode: 0, encrypt: false) == ecbPlain)
        let ctrKey = hex("2b7e151628aed2a6abf7158809cf4f3c")
        let iv = hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff")
        let plain = hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51")
        let encrypted = hex("874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff")
        try check(try transform(plain, key: ctrKey, mode: 2, counter: iv) == encrypted)
        try check(try transform(encrypted, key: ctrKey, mode: 2, counter: iv) == plain)
        try check(try transform(Data(plain.prefix(19)), key: ctrKey, mode: 2, counter: iv) == encrypted.prefix(19))
        let sectors = Data((0..<1536).map { UInt8(truncatingIfNeeded: $0 * 11) })
        let xts = try transform(sectors, key: headerKey, mode: 1)
        try check(try transform(xts, key: headerKey, mode: 1, encrypt: false) == sectors)
        try check(xts.prefix(512) != xts[512..<1024])
        var invalidOutput = Data(count: 1024)
        let invalidStatuses = invalidOutput.withUnsafeMutableBytes { output in headerKey.withUnsafeBytes { material in sectors.withUnsafeBytes { input in
            let k = material.bindMemory(to: UInt8.self).baseAddress!, s = input.bindMemory(to: UInt8.self).baseAddress!, d = output.bindMemory(to: UInt8.self).baseAddress!
            return [satchel_switch_aes_ecb(0, k, s, d, 15), satchel_switch_aes_ctr(k, k, s, d, 0),
                    satchel_switch_aes_xts(0, k, 0, s, d, 513), satchel_switch_aes_xts(0, k, UInt64.max, s, d, 1024)]
        } } }
        for status in invalidStatuses { try check(status != 0) }
        let valid = try nca()
        try archive(valid)
        let candidates = try SwitchExecutableReader.programs(url, keys: keys)
        try check(candidates == [SwitchProgramCandidate(titleID: id, contentID: content)])
        let image = try load()
        try check(image.titleID == id && image.modules.map(\.name) == ["rtld", "main"] && image.verifiedBlocks == 3)
        try check(image.modules[0].data == module(0x42) && image.modules[1].data == module(0x51))
        try check(image.processMetadata == metadata)
        try archive(nca(processMetadata: nil))
        let withoutMetadata = try load()
        try check(withoutMetadata.processMetadata == nil && withoutMetadata.modules.count == 2)
        try reject(nca(processMetadata: Data(repeating: 0, count: 127)), .limit)
        let maximumMetadata = Data(repeating: 0x56, count: 1024 * 1024)
        try archive(nca(processMetadata: maximumMetadata))
        try check(try load().processMetadata == maximumMetadata)
        try reject(nca(processMetadata: Data(repeating: 0x57, count: 1024 * 1024 + 1)), .limit)
        try reject(nca(badMetadataRange: true), .invalid)
        try reject(nca(duplicateMetadata: true), .invalid)
        try archive(nca(encryption: 1)); try check(try load().modules.count == 2)
        try reject(nca(rights: true), .unsupported)
        try reject(nca(encryption: 4), .unsupported)
        try reject(nca(extended: true), .unsupported)
        try reject(nca(badFsHash: true), .integrity)
        try reject(nca(badMaster: true), .integrity)
        try reject(nca(badLayer: true), .unsupported)
        try reject(nca(overlap: true), .invalid)
        try reject(nca(moduleMagic: false), .invalid)
        try reject(nca(badDirectory: true), .invalid)
        try reject(nca(oversizedModule: true), .invalid)
        var corrupted = valid; corrupted[0xc00 + 512 + 100] ^= 1
        try reject(corrupted, .integrity)
        // Corruption within main.npdm fails full ExeFS authentication.
        corrupted = valid; corrupted[0xc00 + 512 + 1450] ^= 1
        try reject(corrupted, .integrity)
        try archive(valid)
        do { _ = try SwitchExecutableReader.load(url, keys: keys, titleID: "bad"); throw NSError(domain: "WrongTitleAccepted", code: 1) }
        catch SwitchExecutableError.noProgram { assertions += 1 }
        do { _ = try SwitchExecutableReader.programs(url, keys: [:]); throw NSError(domain: "MissingKeyAccepted", code: 1) }
        catch SwitchExecutableError.missingKey { assertions += 1 }
        do { _ = try SwitchExecutableReader.programs(url, keys: ["header_key": zero]); throw NSError(domain: "ShortKeyAccepted", code: 1) }
        catch SwitchExecutableError.invalidKeys { assertions += 1 }
        do { _ = try SwitchExecutableReader.load(url, keys: keys, titleID: id, cancelled: { true }); throw NSError(domain: "CancellationIgnored", code: 1) }
        catch is CancellationError { assertions += 1 }
        var totalChecks = 0
        _ = try SwitchExecutableReader.load(url, keys: keys, titleID: id, cancelled: { totalChecks += 1; return false })
        var partialChecks = 0
        do {
            _ = try SwitchExecutableReader.load(url, keys: keys, titleID: id, cancelled: { partialChecks += 1; return partialChecks >= totalChecks / 2 })
            throw NSError(domain: "MidLoadCancellationIgnored", code: 1)
        } catch is CancellationError { try check(partialChecks > 1 && partialChecks < totalChecks) }
        try pfs([(content + ".nca", valid), (String(repeating: "2", count: 32) + ".nca", try nca(titleID: title + 0x1000, rights: true))]).write(to: url)
        try check(try SwitchExecutableReader.programs(url, keys: keys).count == 2)
        try check(try load().modules.count == 2)
        try pfs([(content + ".nca", valid), (String(repeating: "2", count: 32) + ".nca", valid)]).write(to: url)
        do { _ = try load(); throw NSError(domain: "DuplicateTitleAccepted", code: 1) }
        catch SwitchExecutableError.ambiguous { assertions += 1 }

        // Large sparse fixture reaches the per-module policy limit with valid NCA,
        // FS-header, hash-table and PFS-directory-block hashes. No large payload
        // allocation/read is needed to reject its 129-MiB compressed main module.
        let largeModule: UInt64 = 129 * 1024 * 1024
        let largePfsSize = largeModule + 64
        let largeBlockSize: UInt64 = 65536
        let largeBlocks = (largePfsSize + largeBlockSize - 1) / largeBlockSize
        let tableSize = largeBlocks * 32
        let pfsOffset = (tableSize + 511) / 512 * 512
        let sectionSize = (pfsOffset + largePfsSize + 511) / 512 * 512
        var firstBlock = Data(repeating: 0, count: Int(largeBlockSize))
        firstBlock.replaceSubrange(0..<4, with: Data("PFS0".utf8))
        put(1, &firstBlock, 4, 4); put(24, &firstBlock, 8, 4)
        put(largeModule, &firstBlock, 24, 8)
        firstBlock.replaceSubrange(40..<45, with: Data("main\0".utf8))
        var largeHashes = Data(repeating: 0, count: Int(tableSize))
        largeHashes.replaceSubrange(0..<32, with: Data(SHA256.hash(data: firstBlock)))
        var header = try transform(Data(valid.prefix(0xc00)), key: headerKey, mode: 1, encrypt: false)
        put(0xc00 + sectionSize, &header, 0x208, 8)
        put((0xc00 + sectionSize) / 512, &header, 0x244, 4)
        var fs = header.subdata(in: 0x400..<0x600)
        fs.replaceSubrange(8..<40, with: Data(SHA256.hash(data: largeHashes)))
        put(largeBlockSize, &fs, 0x28, 4); put(tableSize, &fs, 0x38, 8)
        put(pfsOffset, &fs, 0x40, 8); put(largePfsSize, &fs, 0x48, 8)
        header.replaceSubrange(0x400..<0x600, with: fs)
        header.replaceSubrange(0x280..<0x2a0, with: Data(SHA256.hash(data: fs)))
        var outer = pfs([(content + ".nca", Data())])
        put(0xc00 + sectionSize, &outer, 24, 8)
        try outer.write(to: url)
        let sparse = try FileHandle(forWritingTo: url)
        try sparse.seekToEnd(); try sparse.write(contentsOf: transform(header, key: headerKey, mode: 1))
        func ctrFor(_ offset: UInt64) -> Data {
            var counter = Data(fs[0x140..<0x148].reversed())
            for shift in (0..<8).reversed() { counter.append(UInt8(truncatingIfNeeded: (offset / 16) >> (shift * 8))) }
            return counter
        }
        try sparse.seek(toOffset: UInt64(outer.count) + 0xc00)
        try sparse.write(contentsOf: transform(largeHashes, key: contentKey, mode: 2, counter: ctrFor(0xc00)))
        try sparse.seek(toOffset: UInt64(outer.count) + 0xc00 + pfsOffset)
        try sparse.write(contentsOf: transform(firstBlock, key: contentKey, mode: 2, counter: ctrFor(0xc00 + pfsOffset)))
        try sparse.truncate(atOffset: UInt64(outer.count) + 0xc00 + sectionSize); try sparse.close()
        do { _ = try load(); throw NSError(domain: "OversizedModuleAccepted", code: 1) }
        catch SwitchExecutableError.limit { assertions += 1 }
        print("Executable reader: \(assertions) synthetic assertions passed; AES known-answer vectors, multi-sector XTS, authenticated synthetic NCA/ExeFS, limits, malformed data, and cancellation.")

        // Optional actual-file check holds returned compressed modules only in
        // memory, emits metadata only, and never writes or dumps keys/game bytes.
        if CommandLine.arguments.count == 4 {
            var actualKeys: [String: Data] = [:]
            let text = try String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
            for line in text.split(separator: "\n") {
                let parts = line.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                guard parts.count == 2, parts[0] == "header_key" || parts[0].hasPrefix("key_area_key_") else { continue }
                guard parts[1].count % 2 == 0, parts[1].utf8.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) }) else { throw SwitchExecutableError.invalidKeys }
                actualKeys[parts[0]] = hex(parts[1])
            }
            let actualURL = URL(fileURLWithPath: CommandLine.arguments[1])
            let candidates = try SwitchExecutableReader.programs(actualURL, keys: actualKeys)
            print("Actual package Program title IDs: \(candidates.map(\.titleID).joined(separator: ", ")).")
            let actual = try SwitchExecutableReader.load(actualURL, keys: actualKeys, titleID: CommandLine.arguments[3])
            print("Actual ExeFS: \(actual.exeFSBytes) bytes, \(actual.verifiedBlocks) blocks verified; \(actual.modules.count) modules.")
            for module in actual.modules {
                let flags = (0..<4).reduce(UInt32(0)) { $0 | UInt32(module.data[12 + $1]) << ($1 * 8) }
                print("Module \(module.name): \(module.data.count) compressed-file bytes, NSO flags \(flags).")
            }
        }
    }
    static func main() {
        do { try run() }
        catch let error as SwitchExecutableError { fputs("Executable test failed: \(error.localizedDescription)\n", stderr); exit(1) }
        catch { fputs("Executable test failed (details suppressed).\n", stderr); exit(1) }
    }
}
