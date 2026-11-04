import Foundation

extension ShaderTransforms {
    static func feedback(_ src: String, layout: [Any], geometry: Bool = false) throws -> String {
        guard layout.count == 4, let m = match(#"(?s)\bvertex\s+main0_out\s+main0\s*\((.*?)\)\s*\{"#, src) else { throw ShaderTransformError.unsupported("invalid feedback layout or entry") }
        let params = m[1], body = slice(src, m.end)
        let vertex = match(#"uint\s+(\w+)\s*\[\[vertex_id\]\]"#, params)
        let instance = match(#"uint\s+(\w+)\s*\[\[instance_id\]\]"#, params)
        let vid = vertex?[1] ?? "dm_vid", iid = instance?[1] ?? "dm_iid"
        let structure = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, src)?[1] ?? ""
        let names = Dictionary(uniqueKeysWithValues: matches(#"float4 (\w+) \[\[user\(locn(\d+)\)\]\]"#, structure).map { (Int($0[2])!, $0[1]) })
        var stores: [String] = [], buffers: [Int] = []
        for b in 0..<4 {
            guard let e = layout[b] as? [String: Any] else { continue }
            guard let stride = e["stride"] as? Int, let outputs = e["outputs"] as? [[Int]], stride > 0 else { throw ShaderTransformError.unsupported("invalid feedback stream") }
            buffers.append(b); var offset = 0
            for output in outputs {
                guard output.count == 2, (1...4).contains(output[1]) else { throw ShaderTransformError.unsupported("invalid feedback output") }
                for (i,c) in ["x","y","z","w"].prefix(output[1]).enumerated() {
                    let value = names[output[0]].map { "as_type<uint>(out.\($0).\(c))" } ?? (c == "w" ? "0x3f800000u" : "0u")
                    stores.append("dm_tf\(b)[dm_tf.offset[\(b)] / 4 + dm_linear * \(stride / 4) + \(offset + i)] = \(value);")
                }
                offset += output[1]
            }
            if stride != offset * 4 { throw ShaderTransformError.unsupported("inconsistent feedback stride") }
        }
        var extra = (vertex == nil ? ", uint dm_vid [[vertex_id]]" : "") + (instance == nil ? ", uint dm_iid [[instance_id]]" : "")
        extra += buffers.map { ", device uint* dm_tf\($0) [[buffer(\(8 + $0))]]" }.joined()
        extra += ", constant dm_tf_capture& dm_tf [[buffer(14)]]"
        var pro = "uint dm_linear = (\(iid) - dm_tf.base_instance) * dm_tf.count + \(vid) - dm_tf.first;\n"
        if geometry { pro += "\(vid) = dm_tf.order[dm_linear];\n" }
        let declaration = "\nstruct dm_tf_capture { uint4 offset; uint4 args; device const uint* order; uint first, count, base_instance, pad; ulong reserved; };\n"
        return declaration + "\nvertex void main0_tf(\(params)\(extra))\n{\n" + pro + body.replacingOccurrences(of: "return out;", with: "{ if (dm_linear < dm_tf.args.x) { " + stores.joined(separator: " ") + " } return; }")
    }
}
