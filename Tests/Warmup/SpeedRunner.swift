import Foundation
import Metal

// Replay a private captured inventory through the unmodified production runner.
// The tiny fixture package contains no copied RomFS, game dylib or credentials.
@main struct SpeedRunner {
    static func main() {
        do { try run() }
        catch { fputs("Runner measurement failed: \(String(reflecting: type(of: error))) (\((error as NSError).code))\n", stderr); exit(1) }
    }
    static func run() throws {
        let args = CommandLine.arguments
        guard args.count == 5 || args.count == 6 else { throw SatchelShaderError.invalidRequest }
        let rows = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: args[1]))) as! [[String: Any]]
        let original = URL(fileURLWithPath: args[2]), root = URL(fileURLWithPath: args[3]), output = URL(fileURLWithPath: args[4])
        let package = root.appendingPathComponent("fixture"), runtime = root.appendingPathComponent("runtime")
        let fm = FileManager.default
        try fm.createDirectory(at: package, withIntermediateDirectories: true)
        let manifest = Data(#"{"format":2,"shaderChain":"26dc2fa9477c"}"#.utf8)
        try manifest.write(to: package.appendingPathComponent("manifest.json"))
        let digest = SatchelShaderTranslator.sha256(manifest)
        let directory = runtime.appendingPathComponent("warmup/" + digest)
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        var records: [String: WarmupInventory.Record] = [:]
        for row in rows {
            let captured = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: row["recordPath"] as! String))) as! [String: Any]
            let record = Data(base64Encoded: captured["record"] as! String)!
            let stage = SatchelShaderStage(rawValue: captured["stage"] as! String)!
            let request = SatchelShaderRequest(record: record, stage: stage)
            let key = try SatchelShaderTranslator.normalized(request).key
            if records[key] != nil { continue }
            try record.write(to: directory.appendingPathComponent(key + ".record"))
            records[key] = .init(key: key, stage: stage, digest: SatchelShaderTranslator.sha256(record))
        }
        var pairs = Set<String>()
        var pairPaths = ["shader_pairs.tsv", "pso_pairs.tsv"].map { original.appendingPathComponent("dm/" + $0) }
        if args.count == 6 { pairPaths.append(URL(fileURLWithPath: args[5])) }
        for path in pairPaths {
            guard let data = try? WarmupRunner.bounded(path, maximum: 32 * 1024 * 1024) else { continue }
            for line in String(decoding: data, as: UTF8.self).split(separator: "\n") where !line.hasPrefix("#") {
                let c = line.split(separator: "\t").map(String.init)
                if c.count >= 2, records[c[0]]?.stage == .vertex, records[c[1]]?.stage == .fragment {
                    pairs.insert(c.prefix(2).joined(separator: "\t"))
                }
            }
        }
        let inventory = WarmupInventory(manifest: digest, records: records.values.sorted { $0.key < $1.key },
            pairs: pairs.sorted().prefix(32).map { $0.components(separatedBy: "\t") }, scannedFiles: 0)
        try JSONEncoder().encode(inventory).write(to: directory.appendingPathComponent("inventory.json"))
        var phase = 0, phaseStarted: Double = 0, phaseSeconds: [String: Double] = [:]
        let result = try WarmupRunner.run(package: package, runtime: runtime, reserve: 35 * 1024 * 1024 * 1024, cancelled: { false }) { state in
            if state.phase != phase {
                phaseSeconds[String(phase)] = state.seconds - phaseStarted
                phase = state.phase; phaseStarted = state.seconds
            }
        }
        unsetenv("DM_SHADER_EVENTS")
        let resources = try ShaderWarmupResources.get(directory: runtime.appendingPathComponent("shader-cache"), reserve: 35 * 1024 * 1024 * 1024)
        var translated: [String: String] = [:], libraries: [String: [String]] = [:]
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        for row in inventory.records {
            let mask = row.stage == .vertex ? resources.mask(row.key) : 0
            let request = try inventory.request(row, directory: directory, wanted: Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 }))
            let id = try SatchelShaderTranslator.identity(request, normalized: SatchelShaderTranslator.normalized(request))
            guard let value = try resources.store.load(requestSHA256: id) else { throw SatchelShaderCacheError.corruptEntry }
            translated[id] = SatchelShaderTranslator.sha256(try encoder.encode(value))
            for variant in value.sources.keys.sorted() {
                let library = try resources.compiler.compile(value, variant: variant)
                libraries[id + "/" + variant + "/" + library.sourceSHA256] = library.library.functionNames.sorted()
            }
        }
        let data = try JSONSerialization.data(withJSONObject: ["progress": try JSONSerialization.jsonObject(with: encoder.encode(result)),
            "records": records.count, "pairs": inventory.pairs.count, "phaseSeconds": phaseSeconds, "translated": translated, "libraries": libraries], options: [.sortedKeys])
        try data.write(to: output, options: .atomic)
        print("Runner: \(records.count) requests, \(inventory.pairs.count) pairs, \(result.failed) failures, \(result.seconds) seconds")
        guard result.failed == 0 else { throw SatchelShaderError.translationFailed }
    }
}
