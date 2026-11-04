import CryptoKit
import Foundation

@main @MainActor struct CompatibilityTests {
    static func main() {
        do { try run() } catch { print("FAIL compatibility: \(error)"); exit(1) }
    }
    static func run() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try ExecutableTests.run()
        try RomFSTests.run()
        try ContentTests.run(root)
        let put = ContentTests.put
        var assertions = 0
        func check(_ value: Bool) throws {
            assertions += 1; guard value else { throw PrepareFailure(message: "Compatibility assertion \(assertions)") }
        }
        func reject(_ body: () throws -> Void) throws {
            do { try body() } catch { assertions += 1; return }
            throw PrepareFailure(message: "Accepted invalid compatibility fixture \(assertions)")
        }
        var nro = try Data(contentsOf: root.deletingLastPathComponent().appendingPathComponent("nro-fixture.bin"))
        nro.replaceSubrange(0x10..<0x14, with: Data("NRO0".utf8)); put(0x3000, &nro, 0x18, 4)
        for i in 0..<3 { put(UInt64(i*0x1000), &nro, 0x20+i*8, 4); put(0x1000, &nro, 0x24+i*8, 4) }
        let digest = NativePreparationRecipe.hash(nro)
        var nrr = Data(count: 0x370)
        nrr.replaceSubrange(0..<4, with: Data("NRR0".utf8)); put(0x350, &nrr, 0x340, 4); put(1, &nrr, 0x344, 4)
        nrr.replaceSubrange(0x350..<0x370, with: Data(SHA256.hash(data: nro)))
        let compressed = ContentTests.zstdFrame(nro)
        func discover(_ payload: Data, registration: Data? = nil) throws -> [NativePreparationRecipe.NRO] {
            let raw = ContentTests.directoryFixture([("plugin.nrr", registration ?? nrr), ("archive.bin", payload)])
            let verified = try ContentTests.verified(raw); defer { verified.invalidate() }
            let fs = try SwitchRomFS(storage: verified)
            let scratch = root.appendingPathComponent(UUID().uuidString)
            try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: false)
            defer { try? FileManager.default.removeItem(at: scratch) }
            return try PrepareDiscovery.nros(fs, to: scratch, token: PrepareCancellation())
        }
        print("Checking registered NRO discovery")
        let loose = try discover(nro)
        try check(loose.count == 1 && loose[0].sha256 == digest)
        print("Loose discovery passed")
        let framed = try discover(Data(repeating: 9, count: 1024*1024-2) + compressed + Data([8,7,6]))
        try check(framed.count == 1 && framed[0].sha256 == digest)
        try reject { _ = try discover(Data(compressed.dropLast())) }
        var bad = nrr; bad[0x350] ^= 1
        try reject { _ = try discover(compressed, registration: bad) }

        // Minimal indexed archive with one current .nro path and one zstd member.
        var index = Data(count: 0x58)
        for offset in [4, 8, 16, 24, 28, 32] { put(1, &index, offset, 4) }
        index.append(Data(count: 24)) // Empty stream and hash tables.
        var path = Data(count: 32); put(0x7d0c5885, &path, 8, 4); path[12] = 3; index.append(path)
        index.append(Data(count: 8)) // path index to file info 0.
        index.append(Data(count: 0x1c)) // directory offset 0.
        index.append(Data(count: 16)) // file info to subinfo 0.
        index.append(Data(count: 12)) // subinfo to directory 0/data 0.
        var data = Data(count: 16)
        put(UInt64(compressed.count), &data, 4, 4); put(UInt64(nro.count), &data, 8, 4); put(3, &data, 12, 4)
        index.append(data); put(UInt64(index.count), &index, 0, 4)
        let packedIndex = ContentTests.zstdFrame(index)
        var archive = Data(count: 0x38)
        put(0xabcdef9876543210, &archive, 0, 8); put(0x38, &archive, 0x20, 8)
        put(UInt64(0x48 + packedIndex.count), &archive, 0x10, 8)
        var descriptor = Data(count: 16)
        put(16, &descriptor, 0, 4); put(UInt64(index.count), &descriptor, 4, 4); put(UInt64(packedIndex.count), &descriptor, 8, 4)
        archive.append(descriptor); archive.append(packedIndex); archive.append(compressed)
        print("Checking indexed NRO discovery")
        let found = try discover(archive)
        try check(found.count == 1 && found[0].sha256 == digest)
        var broken = archive; broken[0x20] = 0xff
        try reject { _ = try discover(broken) }
        var missing = nrr; put(2, &missing, 0x344, 4); missing.append(Data(repeating: 2, count: 32))
        try reject { _ = try discover(archive, registration: missing) }
        let nszURL = root.appendingPathComponent("authenticated.nsz")
        let source = try Data(contentsOf: nszURL)
        let entries = try SwitchPackageReader.contentEntries(nszURL)
        let secure = PackageReaderTests.directory(entries.map { $0.name }, payloads: entries.map {
            source.subdata(in: Int($0.offset)..<Int($0.offset+$0.size))
        }, hashed: true)
        let xczURL = root.appendingPathComponent("authenticated.xcz")
        try PackageReaderTests.xci(secure).write(to: xczURL)
        try check(try SwitchPackageReader.inspect(xczURL).format == .xcz)
        try SwitchContentPipeline.withContent(base: xczURL, keys: ExecutableTests.keys, applicationID: ContentTests.title) { content in
            try check(content.program.modules[1].data == ExecutableTests.module(0x51))
        }
        var corruptXCZ = try Data(contentsOf: xczURL); corruptXCZ[corruptXCZ.count-1] ^= 1
        try corruptXCZ.write(to: xczURL)
        try reject { _ = try SwitchPackageReader.inspect(xczURL) }
        print("PASS compatibility discovery assertions=\(assertions)")
    }
}
