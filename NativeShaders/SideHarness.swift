import Foundation

// Writes the base source and the side source (bx) of each public synthetic request to a private
// folder, for NativeShaders/check-side.py. Inputs come exclusively from the public generator.
@main struct SideHarness {
    static func main() {
        guard CommandLine.arguments.count == 2 else { exit(2) }
        let folder = URL(fileURLWithPath: CommandLine.arguments[1])
        while let line = readLine() {
            autoreleasepool {
                do {
                    guard let request = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                          let name = request["name"] as? String, let hex = request["code"] as? String,
                          let job = request["job"] as? [String: Any] else {
                        throw ShaderTransformError.unsupported("invalid public request")
                    }
                    var code = Data(), at = hex.startIndex
                    while at < hex.endIndex {
                        let end = hex.index(at, offsetBy: 2)
                        guard let byte = UInt8(hex[at..<end], radix: 16) else { throw ShaderTransformError.unsupported("invalid public code") }
                        code.append(byte); at = end
                    }
                    let result = try NativeShaderTranslation.prepare(code: code, job: job,
                        wanted: Set(request["wanted"] as? [Int] ?? []),
                        mappingOnly: request["mappingOnly"] as? Bool ?? false,
                        dual: request["dual"] as? Bool ?? false)
                    for kind in ["b", "bx"] {
                        if let source = result.sources[kind] {
                            try source.write(to: folder.appendingPathComponent(name + "." + kind + ".metal"), atomically: true, encoding: .utf8)
                        }
                    }
                    var reply: [String: Any] = ["name": name, "side": result.sources["bx"] != nil]
                    for key in ["alpha", "logic"] {
                        if let entries = (result.reflection[key] as? [String: Any])?["entries"] { reply[key] = entries }
                    }
                    let data = try JSONSerialization.data(withJSONObject: reply, options: [.sortedKeys])
                    print(String(decoding: data, as: UTF8.self))
                } catch {
                    print("{\"error\":\"public translation failed\"}")
                }
            }
            fflush(stdout)
        }
    }
}
