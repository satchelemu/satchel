import Foundation

extension ShaderVariants {
    static func fetch(_ input: String, bindings: [[String: Any]], mappingOnly: Bool) -> ShaderVariant? {
        var msl = input
        if let cut = find(msl, "\nstruct main0_out_u\n") { msl = slice(msl, 0, cut + 1) }
        let textures = Set(matches(#"texture2d<float> (\w+) \[\[id\(\d+\)\]\];"#, msl).map { $0[1] })
        let ins = msl.components(separatedBy: "struct main0_in").last!.components(separatedBy: "};")[0]
        var locations: [String: Int] = [:], registers: [String: Int] = [:]
        for m in matches(#"float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];"#, ins) { locations[m[1]] = Int(m[2])! }
        for b in bindings where b["kind"] as? String == "tex" {
            if let name = b["msl_name"] as? String, let reg = b["reg"] as? Int { registers[name] = reg }
        }
        // ICU lacks Python's conditional group. Parse the optional correction
        // suffix explicitly, preserving the same complete sample match.
        let pattern = #"(dm_bs\()?spvDescriptorSet0\.(\w+)\.sample\(spvDescriptorSet0\.(\w+)Smplr, select\((_\d+), float2\(0\.0\), isnan\(\4\)\)\)"#
        var calls: [[String: Any]] = [], parts = "", pos = 0
        for m in matches(pattern, msl) {
            let member = m[2], variable = m[4]
            var end = m.end
            if !m[1].isEmpty {
                let suffix = ", spvDescriptorSet0.dm_border, \\d+u, spvDescriptorSet0.\(escaped(member)), spvDescriptorSet0.\(escaped(m[3]))Smplr_w, select\\(\(escaped(variable)), float2\\(0\\.0\\), isnan\\(\(escaped(variable))\\)\\)\\)"
                guard let tail = match("^" + suffix, slice(msl, end)) else { continue }
                end += tail.end
            }
            guard textures.contains(member), member == m[3], let reg = registers[member] else { continue }
            let d = match("float2 \(escaped(variable)) = float2\\((_\\d+), (_\\d+)\\);", msl)
            let a = d.flatMap { match("float \(escaped($0[1])) = in\\.(m_\\d+)\\.x;", msl) }
            let b = d.flatMap { match("float \(escaped($0[2])) = in\\.(m_\\d+)\\.y;", msl) }
            // A varying unchanged, the screen position of a projective sample, or the pixel position times uniforms (pinned dmsh_fetch).
            var how: [String: Any]
            if let a, let b, a[1] == b[1], let loc = locations[a[1]] { how = ["varying_locn": loc] }
            else {
                let pa = d.flatMap { projective(msl, $0[1], "x", locations) }, pb = d.flatMap { projective(msl, $0[2], "y", locations) }
                let fx = d.flatMap { fragmentPosition(msl, $0[1], "x") }, fy = d.flatMap { fragmentPosition(msl, $0[2], "y") }
                if let pa, pa == pb, let loc = locations[pa] { how = ["varying_locn": loc, "proj": 1] }
                else if let fx, let fy { how = ["frag_pos": [fx, fy]] }
                else { continue }
            }
            let i = calls.count
            if i >= 32 { break }
            let select = (0..<8).map { "FX_\(i) == \($0) ? fb\($0) : " }.joined()
            parts += slice(msl, pos, m.start) + "(" + select + slice(msl, m.start, end) + ")"
            pos = end; calls.append(["fc": i, "tex_reg": reg].merging(how) { old, _ in old })
        }
        if calls.isEmpty && !mappingOnly { return nil }
        var converted: [Int: Int] = [:]
        for call in calls { converted[call["tex_reg"] as! Int, default: 0] += 1 }
        let mixed = Set(registers.compactMap { name, reg -> Int? in
            let count = msl.components(separatedBy: "spvDescriptorSet0.\(name).").count - 1
            return textures.contains(name) && count > converted[reg, default: 0] ? reg : nil
        }).sorted()
        var body = parts + slice(msl, pos)
        guard let om = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, body) else { return nil }
        let outs = matches(#"(\w+) (m_\d+) \[\[color\((\d+)\)\]\];"#, om[1])
        guard !outs.isEmpty, outs.count <= 4,
              trim(sub(#"\w+ m_\d+ \[\[color\(\d+\)\]\];"#, om[1]) { _ in "" }).isEmpty else { return nil }
        let members = outs.enumerated().map { j, m in (0..<8).map { "    \(m[1]) \(m[2])_at\($0) [[color(\($0)), function_constant(OM_\(j)_\($0))]];\n" }.joined() }.joined()
        body = slice(body, 0, om.range(1).location) + "\n" + members + slice(body, NSMaxRange(om.range(1)))
        let constants = calls.map { "constant int FX_\($0["fc"] as! Int) [[function_constant(\($0["fc"] as! Int))]];\n" }.joined()
            + (0..<8).map { "constant bool FB_\($0) [[function_constant(\(32 + $0))]];\n" }.joined()
            + outs.indices.map { j in (0..<8).map { "constant bool OM_\(j)_\($0) [[function_constant(\(64 + 8 * j + $0))]];\n" }.joined() }.joined()
        body = replacingFirst(body, "using namespace metal;\n", "using namespace metal;\n\n" + constants)
        guard let i0 = find(body, "fragment main0_out main0("), let i1 = find(body, ")\n{", i0) else { return nil }
        var fbs = (0..<8).map { ", float4 fb\($0) [[color(\($0)), function_constant(FB_\($0))]]" }.joined()
        if (body as NSString).character(at: i1 - 1) == 40 { fbs = String(fbs.dropFirst(2)) }
        body = slice(body, 0, i1) + fbs + slice(body, i1)
        let decl = outs.map { "    \($0[1]) spv_o_\($0[2]) = \($0[1])(0);\n" }.joined()
        body = replacingFirst(body, "    main0_out out = {};\n", "    main0_out out = {};\n" + decl)
        for m in outs { body = body.replacingOccurrences(of: "out.\(m[2])", with: "spv_o_\(m[2])") }
        let put = outs.enumerated().map { j,m in (0..<8).map { "    if (OM_\(j)_\($0)) out.\(m[2])_at\($0) = spv_o_\(m[2]);\n" }.joined() }.joined()
        guard body.contains("    return out;\n") else { return nil }
        body = body.replacingOccurrences(of: "    return out;\n", with: put + "    return out;\n")
        let info: [String: Any] = ["calls": calls, "mixed_regs": mixed, "outputs": outs.map { ["color": Int($0[3])!, "type": $0[1]] as [String: Any] }]
        return ShaderVariant(source: body, info: info)
    }
    // dmsh_fetch _once: name is assigned exactly once in the source.
    static func assignedOnce(_ msl: String, _ name: String) -> Bool {
        let text = name + " = ", source = msl as NSString
        var n = 0, from = 0
        while let i = find(msl, text, from) {
            if i == 0 { n += 1 } else {
                let c = source.character(at: i - 1)
                if !(c == 46 || c == 95 || (48...57).contains(c) || (65...90).contains(c) || (97...122).contains(c)) { n += 1 }
            }
            from = i + (text as NSString).length
        }
        return n == 1
    }
    // dmsh_fetch _proj: var = in.m_X.<comp> / in.m_X.w, as a product with 1.0 / w or as a quotient.
    static func projective(_ msl: String, _ variable: String, _ comp: String, _ locations: [String: Int]) -> String? {
        func source(_ name: String, _ c: String) -> String? {
            guard let m = match("float \(escaped(name)) = in\\.(m_\\d+)\\.\(c);", msl), assignedOnce(msl, name), locations[m[1]] != nil else { return nil }
            return m[1]
        }
        if !assignedOnce(msl, variable) { return nil }
        if let m = match("float \(escaped(variable)) = (_\\d+) \\* (_\\d+);", msl) {
            for (x, r) in [(m[1], m[2]), (m[2], m[1])] {
                if let d = match("float \(escaped(r)) = 1\\.0 / (_\\d+);", msl), assignedOnce(msl, r), let s = source(x, comp), s == source(d[1], "w") { return s }
            }
            return nil
        }
        guard let m = match("float \(escaped(variable)) = (_\\d+) / (_\\d+);", msl) else { return nil }
        let s = source(m[1], comp)
        return s == source(m[2], "w") ? s : nil
    }
    // dmsh_fetch _frag_pos: var = gl_FragCoord.<comp> [/ render scale] * U, U a fragment uniform component.
    static func fragmentPosition(_ msl: String, _ variable: String, _ comp: String) -> [Int]? {
        func one(_ name: String, _ pattern: String) -> ShaderMatch? {
            guard let m = match("float \(escaped(name)) = \(pattern);", msl), assignedOnce(msl, name) else { return nil }
            return m
        }
        func pixel(_ name: String) -> Bool {
            if one(name, "gl_FragCoord\\.\(comp)") != nil { return true }
            guard let q = one(name, "(_\\d+) / (_\\d+)") else { return false }
            return one(q[1], "gl_FragCoord\\.\(comp)") != nil && one(q[2], escaped("(*spvDescriptorSet0.support_buffer)._m5[0].x")) != nil
        }
        guard let m = one(variable, "(\\S+) \\* (\\S+)") else { return nil }
        for (a, u) in [(m[1], m[2]), (m[2], m[1])] {
            if let k = fullMatch(#"\(\*spvDescriptorSet0\.fp_c(\d+)\)\._m0\[(\d+)\]\.([xyzw])"#, u), let slot = Int(k[1]), slot >= 3,
               fullMatch(#"_\d+"#, a) != nil, pixel(a), let vector = Int(k[2]) {
                return [slot - 3, vector, ["x": 0, "y": 1, "z": 2, "w": 3][k[3]]!]
            }
        }
        return nil
    }
}
