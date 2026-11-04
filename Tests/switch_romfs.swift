import CryptoKit
import Foundation

@main struct RomFSTests {
    static func put(_ value: UInt64, _ bytes: inout Data, _ offset: Int, _ count: Int) {
        for i in 0..<count { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
    }
    static func fixture(_ mutate: (inout Data) -> Void = { _ in }) -> (Data, Data) {
        var rom = Data(count: 760)
        put(80, &rom, 0, 8)
        for (i, pair) in [(80, 4), (84, 24), (108, 4), (112, 40)].enumerated() {
            put(UInt64(pair.0), &rom, 8 + i * 16, 8); put(UInt64(pair.1), &rom, 16 + i * 16, 8)
        }
        put(160, &rom, 72, 8)
        put(UInt64(UInt32.max), &rom, 88, 4); put(UInt64(UInt32.max), &rom, 92, 4)
        put(UInt64(UInt32.max), &rom, 100, 4)
        put(UInt64(UInt32.max), &rom, 116, 4); put(600, &rom, 128, 8)
        put(UInt64(UInt32.max), &rom, 136, 4); put(5, &rom, 140, 4)
        rom.replaceSubrange(144..<149, with: Data("hello".utf8))
        for i in 0..<600 { rom[160 + i] = UInt8(truncatingIfNeeded: i) }
        mutate(&rom)
        func hashes(_ data: Data) -> Data {
            var result = Data()
            for offset in stride(from: 0, to: data.count, by: 512) {
                var block = data.subdata(in: offset..<min(offset + 512, data.count)); block.append(Data(count: 512 - block.count))
                result.append(contentsOf: SHA256.hash(data: block))
            }
            return result
        }
        var levels = [rom]
        for _ in 0..<5 { levels.insert(hashes(levels[0]), at: 0) }
        var header = Data(count: 512), section = Data()
        header.replaceSubrange(8..<12, with: Data("IVFC".utf8)); put(0x20000, &header, 12, 4)
        put(32, &header, 16, 4); put(7, &header, 20, 4)
        for (i, data) in levels.enumerated() {
            put(UInt64(section.count), &header, 24 + i * 24, 8); put(UInt64(data.count), &header, 32 + i * 24, 8)
            put(9, &header, 40 + i * 24, 4)
            section.append(data); section.append(Data(count: (512 - data.count % 512) % 512))
        }
        header.replaceSubrange(0xc8..<0xe8, with: hashes(levels[0]))
        return (header, section)
    }
    static func run() throws {
        var assertions = 0
        func check(_ okay: Bool) { precondition(okay); assertions += 1 }
        func fails(_ body: () throws -> Void) { do { try body(); preconditionFailure("Expected rejection") } catch { assertions += 1 } }
        func storage(_ pair: (Data, Data), cancelled: @escaping () -> Bool = { false }) throws -> SwitchIntegrityStorage {
            try SwitchIntegrityStorage(header: pair.0, sectionSize: UInt64(pair.1.count), cancelled: cancelled) { offset, count in
                pair.1.subdata(in: Int(offset)..<Int(offset + count))
            }
        }
        let pair = fixture(), verified = try storage(pair), fs = try SwitchRomFS(storage: verified)
        check(fs.directoryCount == 1 && fs.fileCount == 1 && fs.imageBytes == 760)
        let file = try fs.open("/hello")!
        check(file.size == 600)
        check(try fs.read(file, offset: 0, count: 600) == Data((0..<600).map { UInt8(truncatingIfNeeded: $0) }))
        check(try fs.read(file, offset: 340, count: 50) == Data((340..<390).map { UInt8(truncatingIfNeeded: $0) }))
        check(try fs.read(file, offset: 600, count: 0).isEmpty)
        check(try fs.open("/missing") == nil)
        check(try fs.verifySample() == 600)
        let metadataAtEnd = try SwitchRomFS(storage: storage(fixture { bytes in
            let tables = bytes.subdata(in: 80..<152)
            bytes.append(Data(count: 1200 - bytes.count))
            bytes.replaceSubrange(1024..<1096, with: tables)
            for (i, offset) in [1024, 1028, 1052, 1056].enumerated() { put(UInt64(offset), &bytes, 8 + i * 16, 8) }
        }))
        check(try metadataAtEnd.verifySample() == 600)
        fails { _ = try metadataAtEnd.read(file, offset: 0, count: 1) }
        for path in ["hello", "/", "/../hello", "/a//hello", "/a\\hello", "/hello/"] { fails { _ = try fs.open(path) } }
        fails { _ = try fs.read(file, offset: 601, count: 0) }
        fails { _ = try fs.read(file, offset: 1, count: 600) }
        fails { _ = try verified.read(UInt64.max, count: 1) }
        fails { _ = try verified.read(0, count: 1024 * 1024 + 1) }
        for level in 0..<6 {
            var corrupt = pair.1; corrupt[level * 512] ^= 1
            fails { _ = try SwitchRomFS(storage: storage((pair.0, corrupt))) }
        }
        var corruptLast = pair.1; corruptLast[2560 + 700] ^= 1
        let broken = try SwitchRomFS(storage: storage((pair.0, corruptLast)))
        fails { _ = try broken.read(broken.open("/hello")!, offset: 540, count: 60) }
        var rootHash = pair.0; rootHash[0xc8] ^= 1
        fails { _ = try SwitchRomFS(storage: storage((rootHash, pair.1))) }
        fails { _ = try SwitchRomFS(storage: storage(pair, cancelled: { true })) }
        for field in [8, 12, 16, 20, 40, 44] {
            var header = pair.0; header[field] ^= 0xff
            fails { _ = try storage((header, pair.1)) }
        }
        var overlap = pair.0; put(0, &overlap, 48, 8)
        fails { _ = try storage((overlap, pair.1)) }
        for mutation: (inout Data) -> Void in [
            { put(UInt64.max, &$0, 72, 8) },
            { put(UInt64.max, &$0, 128, 8) },
            { put(3, &$0, 96, 4) },
            { put(1, &$0, 112, 4) },
            { put(1, &$0, 140, 4); $0[144] = 0 },
            { put(1, &$0, 0, 8) }
        ] { fails { _ = try SwitchRomFS(storage: storage(fixture(mutation))) } }
        let cyclic = try SwitchRomFS(storage: storage(fixture { put(0, &$0, 116, 4) }))
        fails { _ = try cyclic.open("/hello") }
        fails { _ = try SwitchRomFS(storage: storage(fixture { put(112, &$0, 72, 8) })) }
        // Retain verified bytes, never cache a mere success verdict over mutable input.
        var mutable = pair.1
        let snapshot = try SwitchIntegrityStorage(header: pair.0, sectionSize: UInt64(mutable.count)) { offset, count in
            mutable.subdata(in: Int(offset)..<Int(offset + count))
        }
        let before = try snapshot.read(0, count: 80); mutable[2560] ^= 1
        check(try snapshot.read(0, count: 80) == before)
        let provider = SwitchStorageProvider(fs)
        let descriptor = provider.descriptor
        var output = [UInt8](repeating: 0xa5, count: 602)
        let readStatus = output.withUnsafeMutableBytes { bytes in
            descriptor.read(descriptor.context, 160, bytes.baseAddress!.advanced(by: 1), 600)
        }
        check(readStatus == SATCHEL_SWITCH_STORAGE_OK)
        check(output.first == 0xa5 && output.last == 0xa5)
        check(Array(output[1...600]) == (0..<600).map { UInt8(truncatingIfNeeded: $0) })
        check(descriptor.size == fs.imageBytes)
        check(descriptor.read(descriptor.context, 760, nil, 0) == SATCHEL_SWITCH_STORAGE_OK)
        check(descriptor.read(descriptor.context, 761, nil, 0) == SATCHEL_SWITCH_STORAGE_INVALID)
        check(descriptor.read(descriptor.context, UInt64.max, nil, 0) == SATCHEL_SWITCH_STORAGE_INVALID)
        check(descriptor.read(descriptor.context, 0, nil, 1) == SATCHEL_SWITCH_STORAGE_INVALID)
        check(descriptor.read(nil, 0, nil, 0) == SATCHEL_SWITCH_STORAGE_INVALID)
        let unchanged = output
        check(output.withUnsafeMutableBytes { descriptor.read(descriptor.context, 700, $0.baseAddress, 61) } == SATCHEL_SWITCH_STORAGE_INVALID)
        check(output.withUnsafeMutableBytes { descriptor.read(descriptor.context, 0, $0.baseAddress, Int(SATCHEL_SWITCH_STORAGE_READ_LIMIT) + 1) } == SATCHEL_SWITCH_STORAGE_INVALID)
        check(output == unchanged)
        // The first block is good, the second is corrupt: no prefix may escape.
        let corruptProvider = SwitchStorageProvider(broken)
        let bad = corruptProvider.descriptor
        check(output.withUnsafeMutableBytes { bad.read(bad.context, 160, $0.baseAddress, 600) } == SATCHEL_SWITCH_STORAGE_INTEGRITY)
        check(output == unchanged)
        var cancelled = false
        let cancelStorage = try storage(pair, cancelled: { cancelled })
        let cancelProvider = try SwitchStorageProvider(SwitchRomFS(storage: cancelStorage))
        let cancelDescriptor = cancelProvider.descriptor
        cancelled = true
        check(output.withUnsafeMutableBytes { cancelDescriptor.read(cancelDescriptor.context, 160, $0.baseAddress, 600) } == SATCHEL_SWITCH_STORAGE_CANCELLED)
        check(output == unchanged)
        verified.invalidate()
        fails { _ = try fs.open("/hello") }
        fails { _ = try fs.read(file, offset: 0, count: 1) }
        fails { _ = try fs.readStorage(0, count: 1) }
        check(output.withUnsafeMutableBytes { descriptor.read(descriptor.context, 0, $0.baseAddress, 1) } == SATCHEL_SWITCH_STORAGE_INVALID)
        check(output == unchanged)
        withExtendedLifetime([provider, corruptProvider, cancelProvider]) {}
        print("RomFS: \(assertions) original-fixture assertions passed; no game data used.")
        if CommandLine.arguments.count == 3 {
            let keys = try SwitchKeys.parse(Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])))
            try SwitchExecutableReader.withRomFS(URL(fileURLWithPath: CommandLine.arguments[2]), keys: keys, titleID: "0100ba4026e94000") { rom in
                // Only structural counts leave RAM; no file names or payloads.
                let sampled = try rom.verifySample()
                print("Real archive RomFS: directories=\(rom.directoryCount), files=\(rom.fileCount), imageBytes=\(rom.imageBytes), verifiedBlocks=\(rom.verifiedBlocks), sampledBytes=\(sampled)")
            }
        }
    }
    static func main() {
        do { try run() }
        catch { fputs("RomFS validation failed: \(error.localizedDescription)\n", stderr); exit(1) }
    }
}
