import Foundation
import Metal

@main struct WarmupHarness {
    static func main() {
        do {
            guard #available(macOS 15.0, iOS 18.0, *), CommandLine.arguments.count == 4,
                  let device = MTLCreateSystemDefaultDevice() else { throw SatchelShaderError.invalidRequest }
            let manifest = URL(fileURLWithPath: CommandLine.arguments[1]), root = URL(fileURLWithPath: CommandLine.arguments[2])
            guard let rows = try JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as? [[String: Any]], rows.count <= 500 else {
                throw SatchelShaderError.invalidRequest
            }
            let compiler = SatchelShaderCompiler(device: device, maximumLiveLibraries: 512)
            let store = try SatchelShaderStore(directory: root, deviceIdentity: compiler.deviceIdentity, reserveBytes: 35 * 1024 * 1024 * 1024)
            let warmup = try SatchelShaderWarmup(store: store, compiler: compiler)
            let modes = CommandLine.arguments[3] == "initial" ? ["emptyApplicationCache", "liveLibraries"] : ["restartedProcess"]
            for mode in modes {
                var index = 0
                let state = try warmup.run(totalRecords: rows.count, next: {
                    guard index < rows.count else { return nil }
                    let row = rows[index]; index += 1
                    guard let path = row["input"] as? String, let stage = (row["stage"] as? String).flatMap(SatchelShaderStage.init(rawValue:)) else { throw SatchelShaderError.invalidRequest }
                    return SatchelShaderRequest(record: try Data(contentsOf: URL(fileURLWithPath: path)), stage: stage,
                        factsJSON: try row["facts"].map { try JSONSerialization.data(withJSONObject: $0) },
                        wantedPixelLocations: Set(row["wanted"] as? [Int] ?? []), mappingOnly: row["mappingOnly"] as? Bool ?? false,
                        dualSource: row["dual"] as? Bool ?? false, expectedKey: row["key"] as? String)
                }, progress: { state, records in
                    let object: [String: Any] = ["mode": mode, "progress": try JSONSerialization.jsonObject(with: JSONEncoder().encode(state)),
                        "records": try JSONSerialization.jsonObject(with: JSONEncoder().encode(records))]
                    print(String(decoding: try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]), as: UTF8.self)); fflush(stdout)
                })
                guard state.completedRecords == rows.count else { throw SatchelShaderError.translationFailed }
            }
        } catch let error as SatchelShaderError {
            fputs("Warm-up failed: \(error)\n", stderr); exit(1)
        } catch { fputs("Warm-up failed: private IO, compiler or cache error\n", stderr); exit(1) }
    }
}
