import Foundation

@main struct ShaderAPIHarness {
    static func main() {
        while let line = readLine() {
            autoreleasepool {
                do {
                    guard let query = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                          let path = query["input"] as? String,
                          let output = query["output"] as? String,
                          let rawStage = query["stage"] as? String,
                          let stage = SatchelShaderStage(rawValue: rawStage) else {
                        throw SatchelShaderError.invalidRequest
                    }
                    let facts = try query["facts"].map { try JSONSerialization.data(withJSONObject: $0, options: [.sortedKeys]) }
                    let request = SatchelShaderRequest(record: try Data(contentsOf: URL(fileURLWithPath: path)),
                        stage: stage, factsJSON: facts, wantedPixelLocations: Set(query["wanted"] as? [Int] ?? []),
                        mappingOnly: query["mappingOnly"] as? Bool ?? false,
                        dualSource: query["dual"] as? Bool ?? false, expectedKey: query["key"] as? String)
                    let result = try SatchelShaderTranslator.translate(request) { query["cancelled"] as? Bool ?? false }
                    let data = try JSONEncoder().encode(result)
                    try data.write(to: URL(fileURLWithPath: output), options: .atomic)
                    print("{\"status\":\"ok\"}")
                } catch let error as SatchelShaderError {
                    print("{\"status\":\"error\",\"reason\":\"\(error)\"}")
                } catch { print("{\"status\":\"error\",\"reason\":\"private IO or JSON failure\"}") }
            }
            fflush(stdout)
        }
    }
}
