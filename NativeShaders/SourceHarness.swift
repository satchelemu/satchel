import Foundation

// Development-only stage fixture. The full harness starts from Maxwell.
@main struct SourceHarness {
    static func main() {
        while let line = readLine() {
            autoreleasepool { process(line) }
            fflush(stdout)
        }
    }
    static func process(_ line: String) {
            do {
                guard let request = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                      let job = request["job"] as? [String: Any], let input = request["input"] as? String,
                      let resources = request["resources"] as? [String: Any], let output = request["output"] as? String else {
                    throw ShaderTransformError.unsupported("invalid source fixture")
                }
                let source = try String(contentsOfFile: input, encoding: .utf8)
                let attrs = ShaderTransforms.integerInputs(source)
                if request["integerInputsOnly"] as? Bool == true {
                    let bytes = try JSONSerialization.data(withJSONObject: attrs, options: [.sortedKeys])
                    print(String(decoding: bytes, as: UTF8.self)); return
                }
                let result = try ShaderSourcePipeline.prepare(source: source, resources: resources, job: job,
                    wanted: Set(request["wanted"] as? [Int] ?? []), mappingOnly: request["mappingOnly"] as? Bool ?? false,
                    dual: request["dual"] as? Bool ?? false)
                let directory = URL(fileURLWithPath: output)
                for (suffix, text) in result.sources { try text.write(to: directory.appendingPathComponent(suffix + ".metal"), atomically: true, encoding: .utf8) }
                try ShaderReflectionJSON.encode(result.reflection).write(to: directory.appendingPathComponent("reflection.json"), options: .atomic)
                let base = result.sources["b"]!, binds = result.reflection["bindings"] as! [[String: Any]]
                let c1 = (job["c1"] as? String ?? "").count / 2
                let extents = ShaderBufferAnalysis.extents(base, bindings: binds)
                let summary: [String: Any] = ["status": "ok", "c1_unproven": ShaderBufferAnalysis.c1Unproven(base, bytes: c1),
                    "extents": Dictionary(uniqueKeysWithValues: extents.map { (String($0.key),$0.value) }),
                    "unwritten": ShaderBufferAnalysis.unwritten(base, bindings: binds)]
                let data = try JSONSerialization.data(withJSONObject: summary, options: [.sortedKeys])
                print(String(decoding: data, as: UTF8.self))
            } catch ShaderTransformError.unsupported(let reason) {
                let data = try! JSONSerialization.data(withJSONObject: ["status": "error", "reason": reason])
                print(String(decoding: data, as: UTF8.self))
            } catch { print("{\"status\":\"error\",\"reason\":\"source fixture IO or JSON\"}") }
    }
}
