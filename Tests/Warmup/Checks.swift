import Foundation
import Metal

@main struct Checks {
    static func main() throws {
        let reuse = CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--archive-reuse"
        let root = URL(fileURLWithPath: CommandLine.arguments[reuse ? 2 : 1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        guard let device = MTLCreateSystemDefaultDevice() else { fatalError("No Metal device") }
        if reuse { try archiveReuse(root: root, device: device); return }
        var checks = 0
        func check(_ result: Bool) { precondition(result); checks += 1 }
        func rejects(_ body: () throws -> Void) { do { try body(); fatalError("Unexpected acceptance") } catch { checks += 1 } }
        for foreground in [false, true] { for charging in [false, true] { for constrained in [false, true] {
            let policy = SatchelWarmupPolicy.select(foreground: foreground, charging: charging, constrained: constrained)
            check(policy.lanes == (!constrained && (foreground || charging) ? 4 : 2))
            check(policy.qos == (foreground && !constrained ? .userInitiated : .utility))
        } } }
        let trial = SatchelWarmupTrial(arguments: ["app", "-SatchelWarmupTrial", "cold-1", "-SatchelWarmupRecords", "1024"])
        check(trial.name == "cold-1" && trial.records == 1024)
        for invalid in ["../escape", "", String(repeating: "a", count: 49), "a/b"] {
            let trial = SatchelWarmupTrial(arguments: ["-SatchelWarmupTrial", invalid, "-SatchelWarmupRecords", "1024"])
            check(trial.name == nil && trial.records == nil)
        }
        check(SatchelWarmupTrial(arguments: ["-SatchelWarmupRecords", "1024"]).records == nil)
        // The old journal must remain decodable with absent timing fields.
        let oldProgress = Data(#"{"stage":"paused","completed":2,"total":3,"shaders":2,"libraries":2,"sourceHits":0,"pipelines":0,"archiveHits":0,"deferred":0,"failed":0,"seconds":1,"state":"paused","phase":1}"#.utf8)
        check(try JSONDecoder().decode(WarmupProgress.self, from: oldProgress).phaseOne == nil)
        func originalSub(_ pattern: String, _ source: String) -> String {
            let regex = try! NSRegularExpression(pattern: pattern)
            var out = source
            for result in regex.matches(in: source, range: NSRange(location: 0, length: (source as NSString).length)).reversed() {
                let value = (source as NSString).substring(with: result.range)
                out = (out as NSString).replacingCharacters(in: result.range, with: "[" + value + "]")
            }
            return out
        }
        for source in ["", "abc abc", "é👋a\nb", String(repeating: "abc float4(x);\n", count: 1000)] {
            for pattern in ["a", "(?=a)", "^|$", "[a-z]+", "xyz"] {
                check(sub(pattern, source) { "[" + $0[0] + "]" } == originalSub(pattern, source))
                check(match(pattern, source)?.range() == matches(pattern, source).first?.range())
            }
        }
        check(!WarmupRunner.diagnostic(ShaderTransformError.unsupported("synthetic shader contents")).contains("synthetic shader contents"))
        var control = Data(count: 0x880), code = Data(count: 32)
        ShaderRecord.put(0x98761234, &control, 0); ShaderRecord.put(0x880, &control, 0x20)
        ShaderRecord.put(32, &control, 0x700); ShaderRecord.put(32, &control, 0x704)
        for offset in [0x748, 0x74c, 0x750] { ShaderRecord.put(1, &control, offset) }
        var instruction = UInt64(0xe30000000007000f).littleEndian
        withUnsafeBytes(of: &instruction) { code.replaceSubrange(8..<16, with: $0) }
        let request = try SatchelShaderRequest(control: control, code: code, stage: .compute)
        let m5 = try WarmupChecks.run(request: request, root: root.appendingPathComponent("m5"), device: device)
        check(m5["failedInventoryNotCompleted"] as? Bool == true)
        let decoder = try WarmupArchiveDecoder(scratch: root.appendingPathComponent("decode"), reserve: 35 * 1024 * 1024 * 1024, cancelled: { false })
        let payload = Data("hello".utf8)
        let yaz = Data("Yaz0".utf8) + Data([0,0,0,5]) + Data(count: 8) + Data([0xff]) + payload
        func input(_ data: Data) -> SatchelShaderInput { .init(count: UInt64(data.count)) { data.subdata(in: Int($0)..<Int($0) + $1) } }
        try decoder.withExpanded(input(yaz)) { check(try $0.read(offset: 0, count: 5) == payload) }
        rejects { try decoder.withExpanded(input(yaz.dropLast())) { _ in } }
        let broken = Data("Yaz0".utf8) + Data([0,0,0,5]) + Data(count: 8) + Data([0,0,0,0])
        rejects { try decoder.withExpanded(input(broken)) { _ in } }
        let cancel = try WarmupArchiveDecoder(scratch: root.appendingPathComponent("cancel"), reserve: 0, cancelled: { true })
        rejects { try cancel.withExpanded(input(yaz)) { _ in } }
        // A BAHS table can contain coincidental descriptor-shaped bytes. Only a
        // normalized record is discovered, while container bounds stay enforced.
        var archiveBytes = Data(count: 0x2400), archiveControl = control
        archiveBytes.replaceSubrange(0..<4, with: Data("BAHS".utf8))
        ShaderRecord.put(0x400, &archiveBytes, 0x18); ShaderRecord.put(0x2000, &archiveBytes, 0x1c)
        ShaderRecord.put(0x120, &archiveControl, 0x700); ShaderRecord.put(0x120, &archiveControl, 0x704)
        for (descriptor, ctl, shader) in [(0x80, 0x100, 0), (0x90, 0xa00, 0x200)] {
            ShaderRecord.put(UInt32(shader), &archiveBytes, descriptor)
            ShaderRecord.put(0x880, &archiveBytes, descriptor + 8)
            ShaderRecord.put(UInt32(ctl - descriptor), &archiveBytes, descriptor + 12)
            archiveBytes.replaceSubrange(ctl..<ctl + 0x880, with: archiveControl)
        }
        ShaderRecord.put(0x12345679, &archiveBytes, 0x2000)
        archiveBytes.replaceSubrange(0x2100..<0x2120, with: code)
        var discovered: [SatchelDiscoveredShader] = []
        _ = try SatchelShaderDiscovery.discover(input(archiveBytes), origin: "synthetic") { discovered.append($0) }
        check(discovered.count == 1 && discovered[0].request.stage == .compute)
        var badBounds = archiveBytes; ShaderRecord.put(0x4000, &badBounds, 0x18)
        rejects { _ = try SatchelShaderDiscovery.discover(input(badBounds), origin: "bad") { _ in } }
        let fullPackage = root.appendingPathComponent("full-package")
        try FileManager.default.createDirectory(at: fullPackage, withIntermediateDirectories: true)
        try Data("{\"format\":2}".utf8).write(to: fullPackage.appendingPathComponent("manifest.json"))
        func u64(_ number: UInt64) -> Data { var n = number.littleEndian; return withUnsafeBytes(of: &n) { Data($0) } }
        let name = Data("ShaderData.bfsha".utf8)
        var indexData = u64(1) + u64(1) + Data(count: 8)
        indexData += u64(32) + u64(UInt64(archiveBytes.count)) + Data(count: 8)
        indexData[44] = UInt8(name.count); indexData[46] = 1; indexData += name
        let romfs = Data("DMROMFS1".utf8) + u64(UInt64(32 + archiveBytes.count)) + u64(UInt64(indexData.count)) + u64(2) + archiveBytes + indexData
        try romfs.write(to: fullPackage.appendingPathComponent("romfs.img"))
        let fullRuntime = root.appendingPathComponent("full-runtime")
        // iOS sees whole units of finished work: done never goes back and never passes the total.
        var systemValues: [(done: Int64, total: Int64)] = []
        let firstPass = try WarmupRunner.run(package: fullPackage, runtime: fullRuntime, reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { systemValues.append($0.systemUnits) }
        check(zip(systemValues, systemValues.dropFirst()).allSatisfy { pair in pair.0.done <= pair.1.done && pair.0.total <= pair.1.total })
        check(systemValues.allSatisfy { $0.done <= $0.total })
        check(firstPass.state == "complete" && firstPass.shaders == 1 && firstPass.failed == 0)
        check(firstPass.unitsDone == firstPass.unitsTotal && firstPass.systemUnits.done == firstPass.systemUnits.total)
        let resumedUnits = ProgressLog()
        let resumed = try WarmupRunner.run(package: fullPackage, runtime: fullRuntime, reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { resumedUnits.add($0) }
        check(resumed.shaders == 1 && resumed.sourceHits == 1 && resumed.failed == 0)
        // The cached inventory skips discovery: one unit for the one shader, none for pairs.
        check(resumed.unitsTotal == 1 && resumed.unitsDone == 1)
        check(resumedUnits.values.contains { $0.phase == 1 && $0.unitsTotal == 1 })
        // Shaders the game drew in play (its capture) are built first, pairs as well. Order
        // only: nothing is dropped or repeated.
        let rows = ["03", "01", "04", "02"].map { WarmupInventory.Record(key: String(repeating: "0", count: 14) + $0, stage: .fragment, digest: "") }
        let ordered = WarmupRunner.playedFirst(rows, played: [rows[2].key, rows[3].key]) { $0.key }
        check(ordered.map(\.key) == [rows[2].key, rows[3].key, rows[0].key, rows[1].key])
        check(WarmupRunner.playedFirst(["b", "a", "c"], played: ["c"]) { $0 } == ["c", "b", "a"])
        check(WarmupRunner.playedFirst(["b", "a"], played: []) { $0 } == ["b", "a"])
        // Isolated timing trials still read normal first-use capture. They do
        // not move or modify the capture while writing their own warm-up cache.
        let captured = root.appendingPathComponent("normal-capture")
        try FileManager.default.createDirectory(at: captured, withIntermediateDirectories: true)
        var captureControl = control; ShaderRecord.put(2, &captureControl, 0x748)
        let captureRequest = try SatchelShaderRequest(control: captureControl, code: code, stage: .compute)
        let captureKey = try SatchelShaderTranslator.normalized(captureRequest).key
        try captureControl.write(to: captured.appendingPathComponent(captureKey + ".ctl"))
        try code.write(to: captured.appendingPathComponent(captureKey + ".code"))
        let trialResult = try WarmupRunner.run(package: fullPackage, runtime: root.appendingPathComponent("trial-runtime"),
            reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }, captureDirectory: captured) { _ in }
        check(trialResult.state == "complete" && trialResult.shaders == 2 && trialResult.failed == 0)
        check(try Data(contentsOf: captured.appendingPathComponent(captureKey + ".ctl")) == captureControl)
        check(try Data(contentsOf: captured.appendingPathComponent(captureKey + ".code")) == code)
        // General library and UI archives can contain shaders even when their
        // filenames have no shader hint. Exercise discovery through a real SARC.
        var sarc = Data(count: 0x60)
        sarc.replaceSubrange(0..<4, with: Data("SARC".utf8))
        sarc[4] = 0x14; sarc[6] = 0xff; sarc[7] = 0xfe
        ShaderRecord.put(UInt32(0x60 + archiveBytes.count), &sarc, 8)
        ShaderRecord.put(0x60, &sarc, 12)
        sarc.replaceSubrange(0x14..<0x18, with: Data("SFAT".utf8))
        sarc[0x18] = 12; sarc[0x1a] = 1
        ShaderRecord.put(0x01000000, &sarc, 0x24)
        ShaderRecord.put(UInt32(archiveBytes.count), &sarc, 0x2c)
        sarc.replaceSubrange(0x30..<0x34, with: Data("SFNT".utf8)); sarc[0x34] = 8
        let memberName = Data("payload.sharcb".utf8)
        sarc.replaceSubrange(0x38..<0x38 + memberName.count, with: memberName); sarc += archiveBytes
        var compressed = Data("Yaz0".utf8)
        var expandedSize = UInt32(sarc.count).bigEndian
        withUnsafeBytes(of: &expandedSize) { compressed.append(contentsOf: $0) }
        compressed += Data(count: 8)
        for start in stride(from: 0, to: sarc.count, by: 8) { compressed.append(0xff); compressed += sarc[start..<min(start + 8, sarc.count)] }
        for (filename, bytes) in [("common.sarc", sarc), ("layout.blarc.szs", compressed), ("common.sharcb", archiveBytes)] {
            let package = root.appendingPathComponent(filename + "-package")
            try FileManager.default.createDirectory(at: package, withIntermediateDirectories: true)
            let name = Data(filename.utf8)
            var index = u64(1) + u64(1) + Data(count: 8)
            index += u64(32) + u64(UInt64(bytes.count)) + Data(count: 8)
            index[44] = UInt8(name.count); index[46] = 1; index += name
            let image = Data("DMROMFS1".utf8) + u64(UInt64(32 + bytes.count)) + u64(UInt64(index.count)) + u64(2) + bytes + index
            try image.write(to: package.appendingPathComponent("romfs.img"))
            let found = try WarmupInventory.discover(package: package, directory: root.appendingPathComponent(filename + "-inventory"), manifest: "synthetic", reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { _, _ in }
            check(found.records.count == 1 && found.records[0].key == discovered[0].key && found.version == WarmupInventory.currentVersion)
        }
        // A reviewed package uses its bound Metal library without a RomFS extraction.
        let reviewed = root.appendingPathComponent("reviewed-package")
        let relativeLibrary = "msl/b-" + NativePreparationRecipe.shaderChain + "/0000000000000001_b.metallib"
        let reviewedLibrary = reviewed.appendingPathComponent(relativeLibrary)
        try FileManager.default.createDirectory(at: reviewedLibrary.deletingLastPathComponent(), withIntermediateDirectories: true)
        let fixtureLibrary = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().appendingPathComponent("synthetic.metallib")
        try FileManager.default.copyItem(at: fixtureLibrary, to: reviewedLibrary)
        func bindReviewed() throws {
            let bytes = try Data(contentsOf: reviewedLibrary)
            let manifest: [String: Any] = ["format":1,"shaderChain":NativePreparationRecipe.shaderChain,
                "files":[relativeLibrary:["size":bytes.count,"sha256":SatchelShaderTranslator.sha256(bytes)]]]
            try JSONSerialization.data(withJSONObject: manifest).write(to: reviewed.appendingPathComponent("manifest.json"))
        }
        try bindReviewed()
        let ready = try WarmupRunner.run(package: reviewed, runtime: root.appendingPathComponent("reviewed-runtime"), reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { _ in }
        check(ready.state == "complete" && ready.shaders == 1 && ready.libraries == 1 && ready.failed == 0)
        try Data("invalid synthetic library".utf8).write(to: reviewedLibrary); try bindReviewed()
        let malformed = try WarmupRunner.run(package: reviewed, runtime: root.appendingPathComponent("reviewed-runtime"), reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { _ in }
        check(malformed.state == "partial" && malformed.failed == 1 && malformed.firstFailure != nil)
        let cacheRoot = root.appendingPathComponent("bounded-cache")
        let boundedStore = try SatchelShaderStore(directory: cacheRoot, deviceIdentity: "synthetic", reserveBytes: 35 * 1024 * 1024 * 1024)
        let partition = try FileManager.default.contentsOfDirectory(at: cacheRoot, includingPropertiesForKeys: nil).first!
        let id = String(repeating: "a", count: 64), oversized = partition.appendingPathComponent(String(repeating: "a", count: 64) + ".json")
        check(FileManager.default.createFile(atPath: oversized.path, contents: nil))
        let sparse = try FileHandle(forWritingTo: oversized)
        try sparse.truncate(atOffset: 193 * 1024 * 1024); try sparse.close()
        rejects { _ = try boundedStore.load(requestSHA256: id) }
        let compiler = SatchelShaderCompiler(device: device)
        let vertex = try compiler.compile(source: "#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint id [[vertex_id]]) { return float4(float(id),0,0,1); }")
        let fragment = try compiler.compile(source: "#include <metal_stdlib>\nusing namespace metal; fragment float4 f() { return float4(1); }")
        let vi: [AnyHashable: Any] = ["chain":"26dc2fa9477c","entry":"v","entry_z":"v","vertex_inputs":[]]
        let fi: [AnyHashable: Any] = ["chain":"26dc2fa9477c","entry":"f"]
        let path = root.appendingPathComponent("archives")
        let archive = try SatchelWarmupPipelines(directory: path, reserveBytes: 35 * 1024 * 1024 * 1024)
        var error: NSError?
        let first = archive.warmVertex(vertex.library, fragment: fragment.library, vertexInfo: vi, fragmentInfo: fi, zero: false, coverage: false, error: &error)
        check(first == 2 && error == nil)
        try archive.flush()
        let reopened = try SatchelWarmupPipelines(directory: path, reserveBytes: 35 * 1024 * 1024 * 1024)
        check(reopened.warmVertex(vertex.library, fragment: fragment.library, vertexInfo: vi, fragmentInfo: fi, zero: false, coverage: false, error: &error) == 1)
        // A distinct alpha-to-coverage state must build its own archive base.
        check(reopened.warmVertex(vertex.library, fragment: fragment.library, vertexInfo: vi, fragmentInfo: fi, zero: false, coverage: true, error: &error) == 2)
        try reopened.flush()
        check(try FileManager.default.contentsOfDirectory(at: path, includingPropertiesForKeys: nil).filter { $0.pathExtension == "pending" }.isEmpty)
        let child = Process()
        child.executableURL = URL(fileURLWithPath: CommandLine.arguments[0])
        child.arguments = ["--archive-reuse", root.path]
        try child.run(); child.waitUntilExit()
        check(child.terminationStatus == 0)
        // The production runner can be cancelled before reading any game image.
        let package = root.appendingPathComponent("package")
        try FileManager.default.createDirectory(at: package, withIntermediateDirectories: true)
        try Data("{\"format\":2}".utf8).write(to: package.appendingPathComponent("manifest.json"))
        var paused = false, pausedStage = ""
        rejects { _ = try WarmupRunner.run(package: package, runtime: root.appendingPathComponent("runtime"), reserve: 35 * 1024 * 1024 * 1024, cancelled: { true }) {
            paused = $0.state == "paused"; pausedStage = $0.stage } }
        check(paused && pausedStage == WarmupStopWords.paused)
        // An iOS stop writes the same words to progress.json as the saved job shows.
        var stopped = WarmupProgress()
        rejects { _ = try WarmupRunner.run(package: package, runtime: root.appendingPathComponent("runtime"), reserve: 35 * 1024 * 1024 * 1024,
            cancelled: { true }, expired: { true }) { stopped = $0 } }
        check(stopped.state == "paused" && stopped.stage == WarmupStopWords.stoppedByIOS)
        let written = try JSONDecoder().decode(WarmupProgress.self, from: Data(contentsOf: root.appendingPathComponent("runtime/warmup")
            .appendingPathComponent(SatchelShaderTranslator.sha256(Data("{\"format\":2}".utf8))).appendingPathComponent("progress.json")))
        check(written.stage == WarmupStopWords.stoppedByIOS)
        print("PASS: \(checks) integration assertions and M5 bounded warm-up, cancellation, resume and failure controls")
    }
    private final class ProgressLog {
        var values: [WarmupProgress] = []
        func add(_ value: WarmupProgress) { values.append(value) }
    }
    private static func archiveReuse(root: URL, device: MTLDevice) throws {
        let compiler = SatchelShaderCompiler(device: device)
        let vertex = try compiler.compile(source: "#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint id [[vertex_id]]) { return float4(float(id),0,0,1); }")
        let fragment = try compiler.compile(source: "#include <metal_stdlib>\nusing namespace metal; fragment float4 f() { return float4(1); }")
        let vi: [AnyHashable: Any] = ["chain":"26dc2fa9477c","entry":"v","entry_z":"v","vertex_inputs":[]]
        let fi: [AnyHashable: Any] = ["chain":"26dc2fa9477c","entry":"f"]
        let archive = try SatchelWarmupPipelines(directory: root.appendingPathComponent("archives"), reserveBytes: 35 * 1024 * 1024 * 1024)
        for coverage in [false, true] {
            var error: NSError?
            precondition(archive.warmVertex(vertex.library, fragment: fragment.library, vertexInfo: vi, fragmentInfo: fi, zero: false, coverage: coverage, error: &error) == 1 && error == nil)
        }
        let resumed = try WarmupRunner.run(package: root.appendingPathComponent("full-package"), runtime: root.appendingPathComponent("full-runtime"), reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { _ in }
        precondition(resumed.state == "complete" && resumed.shaders == 1 && resumed.sourceHits == 1)
        print("PASS: fresh-process shader cache and pipeline archive reuse for both alpha-to-coverage states")
    }

}
