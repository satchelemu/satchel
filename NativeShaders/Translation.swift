import Foundation

struct ShaderTranslation {
    let spirv: Data
    let resources: [String: Any]
    let reader: Data
}

// Both harness and eventual iOS Prepare use these exact in-process calls.
// Callers supply immutable normalized records and observed/profile query facts.
enum NativeShaderTranslation {
    static func translate(code: Data, job: [String: Any]) throws -> ShaderTranslation {
        let json = try JSONSerialization.data(withJSONObject: job, options: [.sortedKeys])
        guard code.count <= 16 * 1024 * 1024, json.count <= 1024 * 1024 else {
            throw ShaderTransformError.unsupported("translation request exceeds limit")
        }
        var result: UnsafeMutablePointer<UInt8>?, count: Int32 = 0
        let status = SatchelShaderMetrics.measure("native-aot") { code.withUnsafeBytes { c in json.withUnsafeBytes { j in
            satchel_shader_translate_v1(c.bindMemory(to: UInt8.self).baseAddress, Int32(code.count), j.bindMemory(to: UInt8.self).baseAddress, Int32(json.count), &result, &count)
        } } }
        guard status == 0, let result, count >= 16 else { throw ShaderTransformError.unsupported("NativeAOT status \(status)") }
        defer { satchel_shader_translate_free(result) }
        let data = Data(bytes: result, count: Int(count))
        let sizes = data.withUnsafeBytes { p in (0..<4).map { Int(UInt32(littleEndian: p.loadUnaligned(fromByteOffset: $0 * 4, as: UInt32.self))) } }
        guard sizes[0] == 1, 16 + sizes[1] + sizes[2] + sizes[3] == data.count,
              let resources = try JSONSerialization.jsonObject(with: data.subdata(in: 16+sizes[1]..<16+sizes[1]+sizes[2])) as? [String: Any] else {
            throw ShaderTransformError.unsupported("invalid NativeAOT result")
        }
        return ShaderTranslation(spirv: data.subdata(in: 16..<16+sizes[1]), resources: resources, reader: data.subdata(in: 16+sizes[1]+sizes[2]..<data.count))
    }
    static func cross(_ spirv: Data) throws -> String {
        var output: UnsafeMutablePointer<CChar>?, size = 0
        let status = SatchelShaderMetrics.measure("spirv-cross") { spirv.withUnsafeBytes { satchel_game_spirv_to_msl($0.baseAddress, spirv.count, &output, &size) } }
        guard status == 0, let output else { throw ShaderTransformError.unsupported("SPIRV-Cross status \(status)") }
        defer { satchel_game_msl_free(output) }
        guard let string = String(data: Data(bytes: output, count: size), encoding: .utf8) else { throw ShaderTransformError.unsupported("invalid MSL encoding") }
        return string
    }
    static func prepare(code: Data, job initial: [String: Any], wanted: Set<Int> = [], mappingOnly: Bool = false, dual: Bool = false, cancelled: () -> Bool = { false }) throws -> PreparedShaderSources {
        var job = initial
        guard job["key"] is String, let stage = job["stage"] as? String,
              ["vs", "ps", "cs", "gs"].contains(stage) else { throw ShaderTransformError.unsupported("stage outside current native preparation contract") }
        if stage == "gs" { return try ShaderGeometry.prepare(code: code, job: job, cancelled: cancelled) }
        var translated = try translate(code: code, job: job)
        var source = try cross(stage == "vs" ? ShaderSpirv.singleton(translated.spirv) : translated.spirv)
        if stage == "vs" {
            var attrs = job["attr"] as? [String: String] ?? [:]
            let extra = ShaderTransforms.integerInputs(source).filter { attrs[$0.key] == nil }
            if !extra.isEmpty {
                attrs.merge(extra) { _, new in new }; job["attr"] = attrs
                translated = try translate(code: code, job: job)
                source = try cross(ShaderSpirv.singleton(translated.spirv))
            }
        }
        return try SatchelShaderMetrics.measure("msl-transforms") { try ShaderSourcePipeline.prepare(source: source, resources: translated.resources, job: job,
                                                wanted: wanted, mappingOnly: mappingOnly, dual: dual) }
    }
}
