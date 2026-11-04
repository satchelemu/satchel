import Foundation

enum ShaderTransformError: Error { case unsupported(String) }

// Native ports of dmsh_build's source transformations. Order is part of the ABI.
enum ShaderTransforms {
    static func precise(_ src: String) -> String {
        sub(#"\bfast::(min|max|clamp)\("#, src) { "precise::\($0[1])(" }
    }
    static func checkAtomics(_ src: String) throws {
        if match(#"\bspvImage2DAtomicCoord\b|device\s+atomic_u?int\s*\*\s*\w+_atomic\b"#, src) != nil {
            throw ShaderTransformError.unsupported("image atomics emulated through a raw pointer")
        }
    }
    static let memberPattern = #"(?m)^\s*(.+?)\s+(\w+)\s*\[\[id\((\d+)\)(?:\s*,\s*\w+\(\d+\))*\]\];"#
    static func border(_ input: String) throws -> String {
        guard let body = match(#"(?s)struct spvDescriptorSetBuffer0\s*\{(.*?)\};"#, input) else { return input }
        let fields = matches(memberPattern, body[1])
        let samplers = fields.filter { trim($0[1]) == "sampler" }.map { $0[2] }
        let textures = Set(fields.filter { match(#"^(texture|depth)\w*<float(,.*)?>$"#, trim($0[1])) != nil }.map { $0[2] })
        let pattern = #"spvDescriptorSet0\.(\w+)\.sample\(spvDescriptorSet0\.(\w+), "#
        let used = Set(matches(pattern, input).filter { textures.contains($0[1]) }.map { $0[2] })
        let ordered = Array(samplers.filter { used.contains($0) }.prefix(32))
        guard !ordered.isEmpty else { return input }
        let bits = Dictionary(uniqueKeysWithValues: ordered.enumerated().map { ($0.element, $0.offset) })
        let nid = fields.map { f -> Int in
            let width = match(#"^array<.*,\s*(\d+)>$"#, trim(f[1])).flatMap { Int($0[1]) } ?? 1
            return Int(f[3])! + width
        }.max()!
        let add = ordered.enumerated().map { "    sampler \($0.element)_w [[id(\(nid + $0.offset))]];\n" }.joined()
            + "    constant dm_border_t* dm_border [[id(\(nid + ordered.count))]];\n"
        let end = NSMaxRange(body.range(1))
        var src = slice(input, 0, end) + ((input as NSString).character(at: end - 1) == 10 ? "" : "\n") + add + slice(input, end)
        for m in matches(pattern, src).filter({ bits[$0[2]] != nil && textures.contains($0[1]) }).reversed() {
            guard let end = close(src, m.end, 40, 41) else { throw ShaderTransformError.unsupported("unbalanced sample") }
            let args = slice(src, m.end, end - 1)
            let value = "dm_bs(\(slice(src, m.start, end)), spvDescriptorSet0.dm_border, \(bits[m[2]]!)u, spvDescriptorSet0.\(m[1]), spvDescriptorSet0.\(m[2])_w, \(args))"
            src = slice(src, 0, m.start) + value + slice(src, end)
        }
        return replacingFirst(src, "using namespace metal;\n", "using namespace metal;\n" + ShaderHelpers.border)
    }
    static func pools(_ input: String) throws -> String {
        guard let body = match(#"(?s)struct spvDescriptorSetBuffer0\s*\{(.*?)\};"#, input) else { return input }
        var pools: [(String, String)] = []
        // sub visits from the end. Collect in member order before replacing.
        for m in matches(#"(?m)^(\s*)array<(.+), (\d+)> (\w+) \[\[id\("#, body[1]) where m[4].hasSuffix("_cb0_40000000") && Int(m[3])! >= 33 {
            pools.append((m[4], m[2]))
        }
        guard !pools.isEmpty else { return input }
        let names = Set(pools.map { $0.0 })
        var members = sub(#"(?m)^(\s*)array<(.+), (\d+)> (\w+) \[\[id\("#, body[1]) { m in
            names.contains(m[4]) ? "\(m[1])constant spvPool_\(m[4])* \(m[4]) [[id(" : m[0]
        }
        let declarations = matches(#"(?m)^(\s*)(.+?)\s+(\w+)\s*\[\[id\((\d+)\)((?:\s*,\s*\w+\(\d+\))*)\]\];"#, members)
        var ids: [Int: Int] = [:], next = 0
        for m in declarations {
            ids[m.start] = next
            next += match(#"^array<.+, (\d+)>$"#, trim(m[2])).flatMap { Int($0[1]) } ?? 1
        }
        for m in declarations.reversed() {
            members = slice(members, 0, m.start) + "\(m[1])\(m[2]) \(m[3]) [[id(\(ids[m.start]!))\(m[5])]];" + slice(members, m.end)
        }
        let structs = pools.map { "struct spvPool_\($0.0)\n{\n    uint n;\n    array<\($0.1), 1> e;\n};\n\n" }.joined()
        var src = slice(input, 0, body.start) + structs + slice(input, body.start, body.range(1).location) + members + slice(input, NSMaxRange(body.range(1)))
        for (name, _) in pools {
            var out = "", pos = 0
            for m in matches("([\\w.]+)\\.\(escaped(name))\\[", src) {
                if m.start < pos { continue }
                guard let end = close(src, m.end, 91, 93) else { throw ShaderTransformError.unsupported("unbalanced pool index") }
                let base = "\(m[1]).\(name)"
                out += slice(src, pos, m.start) + "(&\(base)->e[0])[min(uint(\(slice(src, m.end, end - 1))), \(base)->n)]"
                pos = end
            }
            src = out + slice(src, pos)
            let left = matches("\\b\(escaped(name))\\b", src).count - 2 * matches("\\.\(escaped(name))->e\\[0\\]\\)\\[", src).count - 1
            if left != 0 { throw ShaderTransformError.unsupported("uncovered pool use") }
        }
        return src
    }
    static func initializeVertex(_ src: String) -> String {
        guard let i = find(src, "struct main0_out\n{\n"), let j = find(src, "\n};", i), let k = find(src, "main0_out out = {};") else { return src }
        let outputs = matches(#"\n    float4 (\w+) \[\[user\(locn\d+\)\]\];"#, slice(src, i, j))
        let end = k + "main0_out out = {};".count
        return slice(src, 0, end) + outputs.map { "\n    out.\($0[1]) = float4(0.0, 0.0, 0.0, 1.0);" }.joined() + slice(src, end)
    }
    static func layer(_ input: String) -> String {
        guard let i = find(input, "struct main0_out\n{\n"), let j = find(input, "\n};", i),
              slice(input, i, j).contains("[[render_target_array_index]]"), slice(input, i, j).contains("[[point_size]]") else { return input }
        let src = slice(input, 0, i) + sub(#"\n    float gl_PointSize \[\[point_size\]\];"#, slice(input, i, j)) { _ in "" } + slice(input, j)
        return sub(#"\n    out\.gl_PointSize = [^;\n]*;"#, src) { _ in "" }
    }
    static func baseInstance(_ src: String) -> String {
        let word = "(*spvDescriptorSet0.vp_c0)._m0[100].y"
        if !src.contains(word) { return src }
        return sub(#"\bvertex\s+\w+\s+main0\s*\((?:[^()]|\([^()]*\))*\)"#, src.replacingOccurrences(of: word, with: "as_type<float>(gl_BaseInstance)")) { m in
            m[0].contains("[[base_instance]]") ? m[0] : String(m[0].dropLast()) + ", uint gl_BaseInstance [[base_instance]])"
        }
    }
    static func pad(_ input: String, wanted: Set<Int>) -> (String, [Int]) {
        let have = Set(matches(#"\[\[user\(locn(\d+)\)"#, input).map { Int($0[1])! })
        let missing = wanted.subtracting(have).sorted()
        guard !missing.isEmpty, let i = find(input, "struct main0_out\n{\n"), let j = find(input, "\n};", i), input.contains("main0_out out = {};") else { return (input, []) }
        var src = slice(input, 0, j) + missing.map { "\n    float4 dm_pad\($0) [[user(locn\($0))]];" }.joined() + slice(input, j)
        let k = find(src, "main0_out out = {};")! + "main0_out out = {};".count
        src = slice(src, 0, k) + missing.map { "\n    out.dm_pad\($0) = float4(0.0, 0.0, 0.0, 1.0);" }.joined() + slice(src, k)
        return (src, missing)
    }
    static func depth(_ src: String) throws -> (String, String) {
        guard let m = match(#"\bvertex\s+main0_out\s+main0\s*\("#, src) else { throw ShaderTransformError.unsupported("missing vertex main0") }
        let body = slice(src, m.start)
        let gl = body.replacingOccurrences(of: "return out;", with: "out.gl_Position.z = fma(out.gl_Position.z, 0.5, out.gl_Position.w * 0.5);\n    return out;")
        let single = slice(src, 0, m.start) + gl
        guard let entry = match(#"\bmain0\s*\("#, body) else { throw ShaderTransformError.unsupported("missing vertex entry") }
        let z = slice(body, 0, entry.start) + "main0_z(" + slice(body, entry.end)
        return (single, single + "\n" + z)
    }
    static func discard(_ src: String) throws -> String {
        guard let m = match(#"(?s)\bvertex\s+main0_out\s+main0\s*\((.*?)\)\s*\{"#, src) else { throw ShaderTransformError.unsupported("missing vertex main0") }
        return "\nvertex void main0_discard(" + m[1] + ")\n{" + slice(src, m.end).replacingOccurrences(of: "return out;", with: "return;")
    }
    static func uintFragment(_ src: String) -> (String?, Int) {
        guard let m = match(#"(?m)^.*\bfragment\s+main0_out\s+main0\s*\("#, src),
              let st = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, src) else { return (nil, 0) }
        let fields = matches(#"(?m)^\s*(\w+)\s+(\w+)\s*(\[\[[^\]]*\]\])?;"#, st[1])
        let colors = fields.filter { $0[3].contains("color(") && $0[1].hasPrefix("float") }
        let names = Set(colors.map { $0[2] })
        let fromInt = Set(colors.filter { match("\\bout\\.\(escaped($0[2]))(?:\\.\\w+)? = as_type<float\\d?>\\(", src) != nil }.map { $0[2] })
        let writes = colors.flatMap { matches("\\bout\\.\(escaped($0[2]))(?:\\.\\w+)? = ([^;]+);", src).map { $0[1] } }
        if fromInt.isEmpty && (writes.isEmpty || !writes.allSatisfy({ match(#"^-?[0-9.]+f?$|^float\d\([-0-9., ]*\)$"#, trim($0)) != nil })) { return (nil, 0) }
        func utype(_ t: String) -> String { "u" + t.replacingOccurrences(of: "float", with: "int") }
        func entry(_ suffix: String, _ uints: Set<String>) -> String {
            let structure = "struct main0_out_\(suffix)\n{\n" + fields.map { "    \(uints.contains($0[2]) ? utype($0[1]) : $0[1]) \($0[2]) \($0[3]);\n" }.joined() + "};\n\n"
            let ret = "return main0_out_\(suffix){" + fields.map { uints.contains($0[2]) ? "as_type<\(utype($0[1]))>(out.\($0[2]))" : "out.\($0[2])" }.joined(separator: ", ") + "};"
            var body = slice(src, m.start)
            if let e = match(#"\bfragment\s+main0_out\s+main0\s*\("#, body) { body = slice(body, 0, e.start) + "fragment main0_out_\(suffix) main0_\(suffix)(" + slice(body, e.end) }
            return "\n" + structure + body.replacingOccurrences(of: "return out;", with: ret)
        }
        var out = src + entry("u", names), mask = 0
        if !fromInt.isEmpty && !names.subtracting(fromInt).isEmpty {
            out += entry("m", fromInt)
            for f in fields where fromInt.contains(f[2]) { mask += 1 << Int(match(#"color\((\d+)\)"#, f[3])![1])! }
        }
        return (out, mask)
    }
    static func dualFragment(_ src: String) -> String? {
        guard let m = match(#"(?m)^.*\bfragment\s+main0_out\s+main0\s*\("#, src), let st = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, src) else { return nil }
        let fields = matches(#"(?m)^\s*(\w+)\s+(\w+)\s*(\[\[[^\]]*\]\])?;"#, st[1])
        guard fields.map({ $0[3] }).filter({ $0.contains("color(") }).sorted() == ["[[color(0)]]", "[[color(1)]]"] else { return nil }
        let structure = "struct main0_out_d\n{\n" + fields.map { "    \($0[1]) \($0[2]) \($0[3] == "[[color(1)]]" ? "[[color(0), index(1)]]" : $0[3]);\n" }.joined() + "};\n\n"
        let ret = "return main0_out_d{" + fields.map { "out.\($0[2])" }.joined(separator: ", ") + "};"
        var body = slice(src, m.start)
        if let e = match(#"\bfragment\s+main0_out\s+main0\s*\("#, body) { body = slice(body, 0, e.start) + "fragment main0_out_d main0_d(" + slice(body, e.end) }
        return "\n" + structure + body.replacingOccurrences(of: "return out;", with: ret)
    }
}
