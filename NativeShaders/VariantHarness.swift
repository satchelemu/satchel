import Foundation

@main struct VariantHarness {
    static func main() throws {
        // One private request per line keeps the harness alive across the census.
        while let line = readLine() {
            do {
                guard let job = try JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                      let input = job["input"] as? String, let output = job["output"] as? String,
                      let kind = job["kind"] as? String else { throw NSError(domain: "ShaderJob", code: 1) }
                let source = try String(contentsOfFile: input, encoding: .utf8)
                let variant: ShaderVariant?
                switch kind {
                case "bv": variant = ShaderVariants.vin(source)
                case "bp": variant = ShaderVariants.pull(source)
                case "bx": variant = ShaderVariants.side(source)
                case "bf": variant = ShaderVariants.fetch(source, bindings: job["bindings"] as? [[String: Any]] ?? [], mappingOnly: job["mappingOnly"] as? Bool ?? false)
                default: throw NSError(domain: "ShaderJob", code: 2)
                }
                if let variant {
                    try variant.source.write(toFile: output, atomically: true, encoding: .utf8)
                    let data = try JSONSerialization.data(withJSONObject: ["status": "ok", "info": variant.info], options: [.sortedKeys])
                    print(String(decoding: data, as: UTF8.self))
                } else { print("{\"status\":\"not-applicable\"}") }
            } catch { print("{\"status\":\"error\"}") }
            fflush(stdout)
        }
    }
}
