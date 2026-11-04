import Foundation

extension ShaderTransforms {
    static func capture(_ src: String) throws -> String {
        guard let m = match(#"(?s)\bvertex main0_out main0\((.*?)\)\s*\{"#, src),
              let st = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, src) else { throw ShaderTransformError.unsupported("missing capture entry") }
        var params = m[1], pro = ""
        let body = slice(src, m.end)
        guard trim(body).hasSuffix("}"), body.contains("return out;") else { throw ShaderTransformError.unsupported("invalid capture body") }
        for (semantic, name) in [("vertex_id", "dm_vid"), ("instance_id", "dm_iid")] {
            if let found = match("uint (\\w+) \\[\\[\(semantic)\\]\\]", params) { pro += "    uint \(name) = \(found[1]);\n" }
            else { params += (params.isEmpty ? "" : ", ") + "uint \(name) [[\(semantic)]]" }
        }
        var words = (0..<144).map { $0 == 3 || $0 == 4 || ($0 >= 19 && $0 % 4 == 3) ? "0x3f800000u" : "0u" }
        let components = ["x", "y", "z", "w"]
        for line in st[1].components(separatedBy: "\n").map(trim) where !line.isEmpty {
            if let v = match(#"^(float4|uint4|int4) (\w+) \[\[user\(locn(\d+)\)\]\];$"#, line) {
                guard let location = Int(v[3]), location < 32 else { throw ShaderTransformError.unsupported("capture location outside map") }
                for c in 0..<4 { words[16 + location * 4 + c] = "as_type<uint>(out.\(v[2]).\(components[c]))" }; continue
            }
            if let v = match(#"^float4 (\w+) \[\[position(?:, invariant)?\]\];$"#, line) {
                for c in 0..<4 { words[c] = "as_type<uint>(out.\(v[1]).\(components[c]))" }; continue
            }
            if let v = match(#"^(float|uint|int) (\w+) \[\[(point_size|render_target_array_index|viewport_array_index)\]\];$"#, line) {
                let offset = ["point_size": 4, "render_target_array_index": 13, "viewport_array_index": 14][v[3]]!
                words[offset] = "as_type<uint>(out.\(v[2]))"; continue
            }
            if let v = match(#"^float (\w+)(?:\[(\d+)\])? \[\[clip_distance\]\](?: \[(\d+)\])?;$"#, line), let size = Int(v[2].isEmpty ? v[3] : v[2]), size <= 8 {
                for c in 0..<size { words[5 + c] = "as_type<uint>(out.\(v[1])[\(c)])" }; continue
            }
            if let v = match(#"^float (\w+) \[\[user\(clip(\d)\)\]\];$"#, line) {
                words[5 + Int(v[2])!] = "as_type<uint>(out.\(v[1]))"; continue
            }
            throw ShaderTransformError.unsupported("unknown capture output")
        }
        params += ", constant dm_geometry_capture& dm_gs [[buffer(13)]]"
        pro += "    uint4 dm_ci = dm_gs.info; device uint* dm_capture = dm_gs.dst;\n"
            + "    if (dm_vid - dm_ci.x >= dm_ci.y || dm_iid - dm_ci.z >= dm_ci.w) return;\n"
            + "    device uint* dm_p = dm_capture + ((ulong(dm_iid - dm_ci.z) * dm_ci.y + dm_vid - dm_ci.x) * dm_gs.stride);\n"
        let epilogue = "{ " + words.enumerated().map { "dm_cw(dm_gs, dm_p, \($0.offset), \($0.element));" }.joined(separator: " ") + " return; }"
        return "\nstruct dm_geometry_capture { device uint* dst; uint4 info; uint stride; short slot[144]; };\n"
            + "static inline void dm_cw(constant dm_geometry_capture& g, device uint* p, uint k, uint v) { short s = g.slot[k]; if (s >= 0) p[s] = v; }\n"
            + "vertex void main0_gs(" + params + ")\n{\n" + pro + body.replacingOccurrences(of: "return out;", with: epilogue)
    }
    static func layerEntries(_ full: String) -> (String, [String: Any]?) {
        guard let i = find(full, "struct main0_out\n{\n"), let j = find(full, "\n};", i), full.contains("gl_InstanceIndex"), !slice(full, i, j).contains("[[render_target_array_index]]") else { return (full, nil) }
        let fields = matches(#"\n    float4 (\w+) \[\[user\(locn(\d+)\)\]\];"#, slice(full, i, j))
        let locs = Dictionary(uniqueKeysWithValues: fields.map { ($0[1], Int($0[2])!) })
        guard let m = matches(#"\n    out\.(\w+)\.([xyzw]) = as_type<float>\("#, full).first(where: { locs[$0[1]] != nil }) else { return (full, nil) }
        let name = m[1], comp = m[2]
        var st = sub(#"\n    float gl_PointSize \[\[point_size\]\];"#, slice(full, i, j + 3)) { _ in "" }
        st = replacingFirst(st, "struct main0_out", "struct main0l_out")
        st = String(st.dropLast(3)) + "\n    uint gl_Layer [[render_target_array_index]];\n};"
        var add = "\n" + st + "\n"
        for (entry, new) in [("main0", "main0_l"), ("main0_z", "main0_lz")] {
            guard let a = match("\\bvertex\\s+main0_out\\s+\(entry)\\s*\\(", full), let b = find(full, "{", a.end), let end = close(full, b + 1, 123, 125) else { return (full, nil) }
            var fn = slice(full, a.start, end).replacingOccurrences(of: "main0_out", with: "main0l_out")
            fn = replacingFirst(fn, " \(entry)(", " \(new)(")
            fn = sub(#"\n    out\.gl_PointSize = [^;\n]*;"#, fn) { _ in "" }
            add += "\n" + fn.replacingOccurrences(of: "return out;", with: "out.gl_Layer = as_type<uint>(out.\(name).\(comp));\n    return out;") + "\n"
        }
        return (full + add, ["loc": locs[name]!, "comp": comp])
    }
    static func integerInputs(_ src: String) -> [String: String] {
        guard let index = find(src, "vertex main0_out main0(") else { return [:] }
        let body = slice(src, index)
        var out: [String: String] = [:]
        for field in matches(#"(\w+)\s+(\w+)\s*\[\[attribute\((\d+)\)\]\]"#, src) {
            let name = field[2], location = field[3]
            var nint = 0, bad = false
            for use in matches("in\\.\(escaped(name))\\b", body) {
                let start = (slice(body, 0, use.start) as NSString).range(of: "\n", options: .backwards).location
                let ls = start == NSNotFound ? 0 : start + 1
                let le = find(body, "\n", use.end) ?? (body as NSString).length
                if let m = match("^\\s*float\\s+(_\\w+)\\s*=\\s*in\\.\(escaped(name))\\.[xyzw];$", slice(body, ls, le)) {
                    for u in matches("\\b\(escaped(m[1]))\\b", body).dropFirst() {
                        let prefix = slice(body, 0, u.start)
                        if prefix.hasSuffix("as_type<int>(") || prefix.hasSuffix("as_type<uint>(") { nint += 1; continue }
                        let before = (prefix as NSString).range(of: "\n", options: .backwards).location
                        let first = before == NSNotFound ? 0 : before + 1
                        let last = find(body, "\n", u.end) ?? (body as NSString).length
                        if match("^\\s*[\\w.\\[\\]]+\\s*=\\s*\(escaped(m[1]));$", slice(body, first, last)) != nil { continue }
                        bad = true; break
                    }
                    if bad { break }
                } else {
                    let prefix = slice(body, 0, use.start)
                    if prefix.hasSuffix("as_type<int>(") || prefix.hasSuffix("as_type<uint>(") { nint += 1; continue }
                    bad = true; break
                }
            }
            if !bad && nint > 0 { out[location] = "uint" }
        }
        return out
    }
}
