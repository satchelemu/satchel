import Foundation

// Native ports of pinned NX2Apple dmsh_vin/pull/fetch and of nxnative dmsh_side/alpha/logic (f8a5618d). See Notices.
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
    // Native port of pinned nxnative dmsh_side.unit with dmsh_alpha.entry and dmsh_logic.entry (f8a5618d).
    // One source holds each fragment body once, as dm_body_<entry>, with the plain entry points and
    // the <entry>_a (alpha test) and <entry>_l (logic operation) entry points around it. The host takes
    // the side entry points from <key>_bx. The text is the tool's compile unit, byte for byte.
    struct FragmentEntry {
        let attr: String, ret: String, name: String, params: [String], decl: String, args: String
        let start: Int, head: Int, end: Int
    }
    static func parameters(_ list: String) -> [String] {
        var out: [String] = [], piece = "", depth = 0
        for ch in list {
            if "<([".contains(ch) { depth += 1 }
            if ">)]".contains(ch) { depth -= 1 }
            if ch == "," && depth == 0 { out.append(trim(piece)); piece = "" } else { piece.append(ch) }
        }
        if !trim(piece).isEmpty { out.append(trim(piece)) }
        return out
    }
    static func fragmentEntries(_ msl: String) -> [FragmentEntry]? {
        var out: [FragmentEntry] = []
        for m in matches(#"(?m)^((?:\[\[[^\]]*\]\]\s*)?)fragment\s+(\w+)\s+(\w+)\s*\("#, msl) {
            guard let head = close(msl, m.end, 40, 41), let body = find(msl, "{", head),
                  let end = close(msl, body + 1, 123, 125) else { return nil }
            let params = parameters(slice(msl, m.end, head - 1))
            var decl: [String] = [], args: [String] = []
            for p in params {
                let d = sub(#"\s*\[\[.*\]\]$"#, p) { _ in "" }
                guard let a = match(#"(\w+)$"#, d), !p.contains("function_constant") else { return nil }
                decl.append(d); args.append(a[1])
            }
            out.append(FragmentEntry(attr: m[1], ret: m[2], name: m[3], params: params, decl: decl.joined(separator: ", "),
                                     args: args.joined(separator: ", "), start: m.start, head: head, end: end))
        }
        return out
    }
    static func colour0(_ msl: String, _ structure: String) -> (String, String)? {
        guard let st = match("(?s)struct \(structure)\\s*\\{(.*?)\\};", msl),
              let col = match(#"(?m)^\s*(float4|half4|uint4)\s+(\w+)\s*\[\[color\(0\)(?:, index\(0\))?\]\];"#, st[1]) else { return nil }
        return (col[1], col[2])
    }
    static func alphaRefused(_ msl: String) -> Bool {
        guard match(#"(?s)struct main0_out\s*\{(.*?)\};"#, msl) != nil, let colour = colour0(msl, "main0_out"), colour.0 != "uint4" else { return true }
        return msl.contains("[[buffer(30)]]")
    }
    static func alphaEntry(_ msl: String, _ e: FragmentEntry) -> String? {
        guard let (type, name) = colour0(msl, e.ret) else { return nil }
        // main0_u and main0_m write colour 0 as the integer vector of the float's bits: the test reads the float
        let alpha = type == "uint4" ? "as_type<float>(dm_r.\(name).w)" : "float(dm_r.\(name).w)"
        let params = (e.params + ["constant dm_alpha_state& dm_alpha [[buffer(30)]]"]).joined(separator: ", ")
        return "\(e.attr)fragment \(e.ret) \(e.name)_a(\(params))\n{\n    \(e.ret) dm_r = dm_body_\(e.name)(\(e.args));\n"
            + "    if (!dm_alpha_pass(\(alpha), dm_alpha)) discard_fragment();\n    return dm_r;\n}\n"
    }
    static func logicRefused(_ msl: String) -> Bool {
        msl.contains("[[buffer(29)]]") || match(#"\[\[color\(\d+\)\]\]\s*[,)]"#, msl) != nil
    }
    static func logicEntry(_ msl: String, _ e: FragmentEntry) -> (String, Set<Int>)? {
        guard ["main0", "main0_u", "main0_m"].contains(e.name), let st = match("(?s)struct \(e.ret)\\s*\\{(.*?)\\};", msl) else { return nil }
        let outs = matches(#"(?m)^\s*((?:uint|float)[234]?)\s+(\w+)\s*\[\[color\((\d+)\)\]\];"#, st[1]).map { ($0[1], $0[2], Int($0[3])!) }
        guard !outs.isEmpty else { return nil }
        func scalar(_ type: String) -> String { type.hasPrefix("uint") ? "uint" : "float" }
        let fetch = outs.map { type, _, n in "\(scalar(type))4 dm_fb\(n) [[color(\(n)), function_constant(dm_lfb\(n))]]" }
        let apply = outs.map { type, name, n -> String in
            let s = scalar(type), width = Int(type.dropFirst(s.count)) ?? 1, zero = s == "uint" ? "0u" : "0.0f"
            let pad = String(repeating: ", \(zero)", count: 4 - width), sw = width < 4 ? "." + String("xyzw".prefix(width)) : ""
            return "if (dm_lfb\(n)) dm_r.\(name) = \(type)(\(s == "uint" ? "dm_logic" : "dm_logic_norm")(\(s)4(dm_r.\(name)\(pad)), dm_fb\(n), dm_lop, \(n)u)\(sw));"
        }
        let params = (e.params + fetch + ["constant dm_logic_state& dm_lop [[buffer(29)]]"]).joined(separator: ", ")
        return ("\(e.attr)fragment \(e.ret) \(e.name)_l(\(params))\n{\n    \(e.ret) dm_r = dm_body_\(e.name)(\(e.args));\n    \(apply.joined(separator: " "))\n    return dm_r;\n}\n",
                Set(outs.map { $0.2 }))
    }
    // info: ["alpha": entry point names, "logic": entry point names], either possibly empty.
    static func side(_ msl: String) -> ShaderVariant? {
        guard let entries = fragmentEntries(msl), entries.first?.name == "main0" else { return nil }
        var alpha: [(String, String)] = []
        if !alphaRefused(msl) {
            let made = entries.map { ($0.name + "_a", alphaEntry(msl, $0)) }
            // the host needs one for each of the shader's own (at_fn)
            if made.allSatisfy({ $0.1 != nil }) { alpha = made.map { ($0.0, $0.1!) } }
        }
        var logic: [(String, String)] = [], targets = Set<Int>()
        if !logicRefused(msl) {
            for e in entries { if let made = logicEntry(msl, e) { logic.append((e.name + "_l", made.0)); targets.formUnion(made.1) } }
        }
        guard !alpha.isEmpty || !logic.isEmpty else { return nil }
        var bodies = "", at = entries[0].start
        for e in entries {
            bodies += slice(msl, at, e.start) + "static \(e.ret) dm_body_\(e.name)(\(e.decl))" + slice(msl, e.head, e.end); at = e.end
        }
        let plain = entries.map { "\($0.attr)fragment \($0.ret) \($0.name)(\($0.params.joined(separator: ", ")))\n{\n    return dm_body_\($0.name)(\($0.args));\n}\n" }.joined()
        let constants = targets.sorted().map { "constant bool dm_lfb\($0) [[function_constant(\(200 + $0))]];\n" }.joined()
        let source = slice(msl, 0, entries[0].start) + ShaderHelpers.alpha + ShaderHelpers.logic + constants + bodies + slice(msl, at) + "\n"
            + plain + alpha.map { $0.1 }.joined() + logic.map { $0.1 }.joined()
        return ShaderVariant(source: source, info: ["alpha": alpha.map { $0.0 }, "logic": logic.map { $0.0 }])
    }
}
