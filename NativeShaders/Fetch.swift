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
        var calls: [[String: Int]] = [], parts = "", pos = 0
        for m in matches(pattern, msl) {
            let member = m[2], variable = m[4]
            var end = m.end
            if !m[1].isEmpty {
                let suffix = ", spvDescriptorSet0.dm_border, \\d+u, spvDescriptorSet0.\(escaped(member)), spvDescriptorSet0.\(escaped(m[3]))Smplr_w, select\\(\(escaped(variable)), float2\\(0\\.0\\), isnan\\(\(escaped(variable))\\)\\)\\)"
                guard let tail = match("^" + suffix, slice(msl, end)) else { continue }
                end += tail.end
            }
            guard textures.contains(member), member == m[3], let reg = registers[member],
                  let d = match("float2 \(escaped(variable)) = float2\\((_\\d+), (_\\d+)\\);", msl),
                  let a = match("float \(escaped(d[1])) = in\\.(m_\\d+)\\.x;", msl),
                  let b = match("float \(escaped(d[2])) = in\\.(m_\\d+)\\.y;", msl),
                  a[1] == b[1], let loc = locations[a[1]] else { continue }
            let i = calls.count
            if i >= 32 { break }
            let select = (0..<8).map { "FX_\(i) == \($0) ? fb\($0) : " }.joined()
            parts += slice(msl, pos, m.start) + "(" + select + slice(msl, m.start, end) + ")"
            pos = end; calls.append(["fc": i, "tex_reg": reg, "varying_locn": loc])
        }
        if calls.isEmpty && !mappingOnly { return nil }
        var converted: [Int: Int] = [:]
        for call in calls { converted[call["tex_reg"]!, default: 0] += 1 }
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
        let constants = calls.map { "constant int FX_\($0["fc"]!) [[function_constant(\($0["fc"]!))]];\n" }.joined()
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
}
