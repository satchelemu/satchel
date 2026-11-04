import Foundation

@main struct TranslationHarness {
    static func main() {
        while let line = readLine() {
            autoreleasepool { process(line) }
            fflush(stdout)
        }
    }
    static func process(_ line: String) {
            do {
                guard let request = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                      var job = request["job"] as? [String: Any], let input = request["input"] as? String,
                      let output = request["output"] as? String else { throw ShaderTransformError.unsupported("invalid harness request") }
                var code = try Data(contentsOf: URL(fileURLWithPath: input))
                let started = ProcessInfo.processInfo.systemUptime
                if request["record"] as? Bool == true {
                    guard let stage = job["stage"] as? String else { throw ShaderTransformError.unsupported("missing record stage") }
                    let normalized = try ShaderRecord.normalize(code, stage: stage, facts: request["facts"] as? [String: Any], expectedKey: job["key"] as? String)
                    code = normalized.code
                    var normalizedJob = normalized.job
                    normalizedJob["source"] = job["source"]
                    normalizedJob["pc_paired"] = job["pc_paired"]
                    job = normalizedJob
                }
                let result = try NativeShaderTranslation.prepare(code: code, job: job,
                    wanted: Set(request["wanted"] as? [Int] ?? []), mappingOnly: request["mappingOnly"] as? Bool ?? false,
                    dual: request["dual"] as? Bool ?? false)
                let preparationSeconds = ProcessInfo.processInfo.systemUptime - started
                let directory = URL(fileURLWithPath: output)
                for (suffix, source) in result.sources { try source.write(to: directory.appendingPathComponent(suffix + ".metal"), atomically: true, encoding: .utf8) }
                try ShaderReflectionJSON.encode(result.reflection).write(to: directory.appendingPathComponent("reflection.json"), options: .atomic)
                let base = result.sources["b"]!, bindings = result.reflection["bindings"] as! [[String: Any]]
                let extents = ShaderBufferAnalysis.extents(base, bindings: bindings)
                let summary: [String: Any] = ["status": "ok", "preparation_seconds": preparationSeconds,
                    "c1_unproven": ShaderBufferAnalysis.c1Unproven(base, bytes: (job["c1"] as? String ?? "").count / 2),
                    "extents": Dictionary(uniqueKeysWithValues: extents.map { (String($0.key), $0.value) }),
                    "unwritten": ShaderBufferAnalysis.unwritten(base, bindings: bindings)]
                let data = try JSONSerialization.data(withJSONObject: summary, options: [.sortedKeys])
                print(String(decoding: data, as: UTF8.self))
            } catch ShaderTransformError.unsupported(let reason) {
                // Only fixed stage errors and numeric status values reach logs.
                let data = try! JSONSerialization.data(withJSONObject: ["status": "error", "reason": reason])
                print(String(decoding: data, as: UTF8.self))
            } catch { print("{\"status\":\"error\",\"reason\":\"harness IO or JSON failure\"}") }
    }
}
