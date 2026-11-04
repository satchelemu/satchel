import Foundation

// Native ports of pinned NX2Apple dmsh_vin/pull/alpha/logic/fetch. See Notices.
enum ShaderVariants {
    static func members(_ m: ShaderMatch) -> [(String, String, Int)]? {
        var out: [(String, String, Int)] = []
        for line in m[1].components(separatedBy: "\n") {
            guard let f = match(#"^    (float4|uint4|int4) (m_\d+(?:_\d+)*) \[\[attribute\((\d+)\)\]\];$"#, line),
                  let location = Int(f[3]), location < 16 else { return nil }
            out.append((f[1], f[2], location))
        }
        return out
    }
    static func vin(_ msl: String) -> ShaderVariant? {
        guard let m = match(#"(?s)\nstruct main0_in\n\{\n(.*?)\n\};"#, msl), let fields = members(m) else { return nil }
        let ints = fields.filter { $0.0 != "float4" }
        guard !ints.isEmpty else { return nil }
        var slot = 16, shadows: [[String: Int]] = [], extra: [String] = []
        for (_, n, l) in ints {
            if slot >= 31 { break }
            extra.append("    float4 \(n)_vf [[attribute(\(slot))]];")
            shadows.append(["location": l, "float": slot]); slot += 1
        }
        for i in shadows.indices {
            if slot >= 31 { break }
            let (t, n, _) = ints[i], other = t == "uint4" ? "int4" : "uint4"
            extra.append("    \(other) \(n)_vo [[attribute(\(slot))]];")
            shadows[i][other == "int4" ? "sint" : "uint"] = slot; slot += 1
        }
        let head = slice(msl, 0, m.end) + "\n\nstruct main0_in_v\n{\n" + m[1] + "\n" + extra.joined(separator: "\n") + "\n};"
        var body = slice(msl, m.end)
        if let cut = find(body, "\nvertex void main0_tf(") { body = slice(body, 0, cut) + "\n" }
        var pro = "    main0_in in = {};\n"
        for (t, n, l) in fields {
            let e = shadows.first { $0["location"] == l }
            let ors = e == nil ? "" : " | as_type<\(t)>(dm_in.\(n)_vf)" + ((e?["sint"] != nil || e?["uint"] != nil) ? " | as_type<\(t)>(dm_in.\(n)_vo)" : "")
            pro += "    in.\(n) = dm_in.\(n)\(ors);\n"
        }
        for entry in ["main0", "main0_z"] {
            guard let h = match("\\nvertex main0_out \(entry)\\(main0_in in \\[\\[stage_in\\]\\]([^\\n]*)\\)\\n\\{\\n", body) else {
                if entry == "main0" { return nil }; continue
            }
            body = slice(body, 0, h.start) + "\nvertex main0_out \(entry)(main0_in_v dm_in [[stage_in]]\(h[1]))\n{\n" + pro + slice(body, h.end)
        }
        return ShaderVariant(source: head + body, info: shadows)
    }
    static func dropEntries(_ msl: String) -> String {
        let lines = msl.components(separatedBy: "\n")
        var out: [String] = [], i = 0
        while i < lines.count {
            if let m = match(#"^vertex \S+ (\w+)\("#, lines[i]), !["main0", "main0_z"].contains(m[1]) {
                while i < lines.count && lines[i] != "}" { i += 1 }
                i += 1; continue
            }
            out.append(lines[i]); i += 1
        }
        return out.joined(separator: "\n")
    }
    static func pull(_ msl: String) -> ShaderVariant? {
        guard let m = match(#"(?s)\nstruct main0_in\n\{\n(.*?)\n\};"#, msl), let fields = members(m), !msl.contains("[[buffer(16)]]") else { return nil }
        var head = slice(msl, 0, m.start) + "\nstruct main0_in\n{\n" + fields.map { "    \($0.0) \($0.1);" }.joined(separator: "\n") + "\n};"
        var body = dropEntries(slice(msl, m.end)), entries = Set<String>()
        for entry in ["main0", "main0_z"] {
            guard let h = match("\\nvertex main0_out \(entry)\\(main0_in in \\[\\[stage_in\\]\\]((?:, [^\\n]*)?)\\)\\n\\{\\n", body) else {
                if entry == "main0" { return nil }; continue
            }
            let rest = h[1]
            let vid = match(#"uint (\w+) \[\[vertex_id\]\]"#, rest)
            let iid = match(#"uint (\w+) \[\[instance_id\]\]"#, rest)
            let bi = match(#"uint (\w+) \[\[base_instance\]\]"#, rest)
            var params = rest.isEmpty ? [] : String(rest.dropFirst(2)).components(separatedBy: ", ")
            params.append("constant dm_vlayout& dm_lay [[buffer(16)]]")
            if vid == nil { params.append("uint dm_vid [[vertex_id]]") }
            if iid == nil { params.append("uint dm_iid [[instance_id]]") }
            if bi == nil { params.append("uint dm_bi [[base_instance]]") }
            let vn = vid?[1] ?? "dm_vid", inn = iid?[1] ?? "dm_iid", bn = bi?[1] ?? "dm_bi"
            let pro = "    main0_in in;\n" + fields.map { t,n,l in "    in.\(n) = as_type<\(t)>(dm_fetch(dm_lay, \(l)u, \(vn), \(inn), \(bn)));\n" }.joined()
            body = slice(body, 0, h.start) + "\nvertex main0_out \(entry)(" + params.joined(separator: ", ") + ")\n{\n" + pro + slice(body, h.end)
            entries.insert(entry)
        }
        guard let ui = find(head, "using namespace metal;") else { return nil }
        let end = ui + "using namespace metal;".count
        head = slice(head, 0, end) + "\n\n" + ShaderHelpers.pull + slice(head, end)
        var info: [String: Any] = ["entry": "main0", "slot": 16, "inputs": fields.map { $0.2 }]
        if entries.contains("main0_z") { info["entry_z"] = "main0_z" }
        return ShaderVariant(source: head + body, info: info)
    }
    static let fragment = #"(?m)^[^\n]*?\bfragment\s+\w+\s+(\w+)\s*\("#
    static func alpha(_ msl: String) -> ShaderVariant? {
        guard let st = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, msl),
              let col = match(#"(?m)^\s*(float4|half4)\s+(\w+)\s*\[\[color\(0\)\]\];"#, st[1]),
              msl.contains("main0_out out = {};"), !msl.contains("[[buffer(30)]]") else { return nil }
        let entries = matches(fragment, msl)
        guard entries.first?[1] == "main0" else { return nil }
        var src = "", at = 0
        for m in entries {
            guard let end = close(msl, m.end, 40, 41) else { return nil }
            src += slice(msl, at, end - 1) + (trim(slice(msl, m.end, end - 1)).isEmpty ? "" : ", ") + "constant dm_alpha_state& dm_alpha [[buffer(30)]]"
            at = end - 1
        }
        src += slice(msl, at)
        let first = entries[0].start
        let test = "if (!dm_alpha_pass(float(out.\(col[2]).w), dm_alpha)) discard_fragment();"
        let originalBody = slice(src, first)
        guard match(#"\breturn\b([^;]*);"#, originalBody) != nil else { return nil }
        let body = sub(#"\breturn\b([^;]*);"#, originalBody) { "{ " + test + " return" + $0[1] + "; }" }
        return ShaderVariant(source: slice(src, 0, first) + ShaderHelpers.alpha + body, info: matches(fragment, body).map { $0[1] })
    }
    static func logic(_ msl: String) -> ShaderVariant? {
        guard !msl.contains("[[buffer(29)]]"), match(#"\[\[color\(\d+\)\]\]\s*[,)]"#, msl) == nil else { return nil }
        var pieces = "", at = 0, names: [String] = [], targets = Set<Int>()
        for m in matches(#"(?m)^[^\n]*?\bfragment\s+(main0_out_[um])\s+(main0_[um])\s*\("#, msl) {
            let structure = m[1], name = m[2]
            guard let st = match("(?s)struct \(structure)\\s*\\{(.*?)\\};", msl) else { return nil }
            let ints = matches(#"(?m)^\s*(uint[234]?)\s+(\w+)\s*\[\[color\((\d+)\)\]\];"#, st[1])
            if ints.isEmpty { continue }
            guard let pend = close(msl, m.end, 40, 41), let start = find(msl, "{", pend), let end = close(msl, start + 1, 123, 125) else { return nil }
            let empty = trim(slice(msl, m.end, pend - 1)).isEmpty
            let fetch = ints.enumerated().map { i,f in "\(i > 0 || !empty ? ", " : "")uint4 dm_fb\(f[3]) [[color(\(f[3])), function_constant(dm_lfb\(f[3]))]]" }.joined()
            let params = fetch + ", constant dm_logic_state& dm_lop [[buffer(29)]]"
            let apply = ints.map { f -> String in
                let ty = f[1], nm = f[2], n = f[3]
                let pad = ["uint": ", 0u, 0u, 0u", "uint2": ", 0u, 0u", "uint3": ", 0u", "uint4": ""][ty]!
                let sw = ["uint": ".x", "uint2": ".xy", "uint3": ".xyz", "uint4": ""][ty]!
                return "if (dm_lfb\(n)) dm_r.\(nm) = \(ty)(dm_logic(uint4(dm_r.\(nm)\(pad)), dm_fb\(n), dm_lop, \(n)u)\(sw));"
            }.joined(separator: " ")
            let old = slice(msl, start, end)
            guard match(#"\breturn\s+([^;]+);"#, old) != nil else { return nil }
            let body = sub(#"\breturn\s+([^;]+);"#, old) { "{ \(structure) dm_r = \($0[1]); \(apply) return dm_r; }" }
            pieces += slice(msl, at, pend - 1) + params + slice(msl, pend - 1, start) + body
            at = end; names.append(name); targets.formUnion(ints.map { Int($0[3])! })
        }
        guard !names.isEmpty else { return nil }
        var src = pieces + slice(msl, at)
        for m in matches(#"(?m)^(?:\[\[[^\]]*\]\]\s*)?fragment\s+\w+\s+(\w+)\s*\("#, src).reversed() {
            if names.contains(m[1]) { continue }
            guard let pend = close(src, m.end, 40, 41), let start = find(src, "{", pend), let end = close(src, start + 1, 123, 125) else { return nil }
            src = slice(src, 0, m.start) + slice(src, end).drop(while: { $0 == "\n" })
        }
        guard let first = match(#"(?m)^(?:\[\[[^\]]*\]\]\s*)?(?:fragment|vertex|kernel)\s+\w+\s+\w+\s*\("#, src) else { return nil }
        let constants = targets.sorted().map { "constant bool dm_lfb\($0) [[function_constant(\(200 + $0))]];\n" }.joined()
        return ShaderVariant(source: slice(src, 0, first.start) + ShaderHelpers.logic + constants + slice(src, first.start), info: names)
    }
}
