import Foundation
import CryptoKit

// Only hashes leave this harness. Inputs come exclusively from the public generator.
@main struct PublicParityHarness {
    static func hash(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }
    static func main() {
        while let line = readLine() {
            autoreleasepool {
                do {
                    guard satchel_shader_runtime_features() == 0,
                          let request = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                          let hex = request["code"] as? String,
                          let job = request["job"] as? [String: Any] else {
                        throw ShaderTransformError.unsupported("invalid public request")
                    }
                    var code = Data()
                    var at = hex.startIndex
                    while at < hex.endIndex {
                        let end = hex.index(at, offsetBy: 2)
                        guard let byte = UInt8(hex[at..<end], radix: 16) else {
                            throw ShaderTransformError.unsupported("invalid public code")
                        }
                        code.append(byte); at = end
                    }
                    let raw = try NativeShaderTranslation.translate(code: code, job: job)
                    let result = try NativeShaderTranslation.prepare(code: code, job: job,
                        wanted: Set(request["wanted"] as? [Int] ?? []),
                        mappingOnly: request["mappingOnly"] as? Bool ?? false,
                        dual: request["dual"] as? Bool ?? false)
                    var hashes = result.sources.mapValues { hash(Data($0.utf8)) }
                    hashes["spirv"] = hash(raw.spirv)
                    hashes["reader"] = hash(raw.reader)
                    hashes["resources"] = hash(try JSONSerialization.data(withJSONObject: raw.resources, options: [.sortedKeys]))
                    hashes["reflection"] = hash(try ShaderReflectionJSON.encode(result.reflection))
                    let data = try JSONSerialization.data(withJSONObject: hashes, options: [.sortedKeys])
                    print(String(decoding: data, as: UTF8.self))
                } catch {
                    print("{\"error\":\"public translation failed\"}")
                }
            }
            fflush(stdout)
        }
    }
}
