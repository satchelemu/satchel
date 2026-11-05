// Diagnostic executable built with the shader source closure under test.
// Inputs and translated payloads stay outside the checkout. Output is counts only.
import Foundation

@main struct CapturedRequestEquivalence {
    static func main() throws {
        let input = URL(fileURLWithPath: CommandLine.arguments[1])
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        let rows = try JSONSerialization.jsonObject(with: Data(contentsOf: input)) as! [[String: Any]]
        var checked = 0, variants = 0, missing = 0, different = 0
        for row in rows {
            try autoreleasepool {
                guard let recordPath = row["recordPath"] as? String else { missing += 1; return }
                let captured = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: recordPath))) as! [String: Any]
                let envelope = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: row["cachePath"] as! String))) as! [String: Any]
                let payload = Data(base64Encoded: envelope["payload"] as! String)!
                let expected = try JSONDecoder().decode(SatchelTranslatedShader.self, from: payload)
                guard SatchelShaderTranslator.sha256(payload) == envelope["payloadSHA256"] as? String else {
                    throw SatchelShaderCacheError.corruptEntry
                }
                let record = Data(base64Encoded: captured["record"] as! String)!
                guard SatchelShaderTranslator.sha256(record) == expected.recordSHA256 else {
                    throw SatchelShaderCacheError.corruptEntry
                }
                let stage = SatchelShaderStage(rawValue: captured["stage"] as! String)!
                let base = SatchelShaderRequest(record: record, stage: stage, expectedKey: expected.key)
                let normalized = try SatchelShaderTranslator.normalized(base)
                var matching: SatchelShaderRequest?
                for mask in row["masks"] as! [UInt32] {
                    let locations = Set((0..<32).filter { mask & (UInt32(1) << $0) != 0 })
                    let request = SatchelShaderRequest(record: record, stage: stage, wantedPixelLocations: locations, expectedKey: expected.key)
                    if try SatchelShaderTranslator.identity(request, normalized: normalized) == expected.requestSHA256 {
                        matching = request; break
                    }
                }
                guard let request = matching else { missing += 1; return }
                let actual = try SatchelShaderTranslator.translate(request)
                let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
                // Compare every field, including the complete MSL, all reflection,
                // resource extents and variant identities. No payload is printed.
                if try encoder.encode(actual) != encoder.encode(expected) { different += 1 }
                checked += 1; variants += actual.sources.count
            }
        }
        let result: [String: Any] = ["requests": rows.count, "checked": checked, "variants": variants,
            "missing": missing, "different": different, "passed": checked == rows.count && missing == 0 && different == 0]
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys, .prettyPrinted]).write(to: output)
        print("Captured request equivalence: \(checked)/\(rows.count), missing \(missing), different \(different)")
        if checked != rows.count || missing != 0 || different != 0 { exit(1) }
    }
}
