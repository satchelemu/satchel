import Foundation

extension ShaderReflection {
    static func raster(stage: String, source: String) -> [String: Any] {
        let output = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, source)?[1] ?? ""
        if stage == "ps" {
            return ["discard": source.contains("discard_fragment") ? 1 : 0,
                    "sample_mask": output.contains("sample_mask") ? 1 : 0,
                    "primitive_id": source.contains("[[primitive_id]]") ? 1 : 0,
                    "colors": Set(matches(#"\[\[color\((\d+)\)\]\]"#, output).map { Int($0[1])! }).sorted()]
        }
        if stage != "vs" { return [:] }
        let screen = screenVaryings(source)
        guard let body = match(#"(?s)\bvertex\s+\w+\s+\w+\s*\(.*?\)\s*\{(.*)\}\s*$"#, source),
              match(#"\b(if|for|while|switch|do)\b|clip_distance|ClipDistance"#, body[1]) == nil else { return ["pos_attr": -1, "screen": screen] }
        let b = body[1]
        var attr: [String: Int] = [:], variables: [String: [String]] = [:], last: [String: String] = [:]
        for m in ShaderTransforms.attributes(source) { attr[m[2]] = Int(m[3])! }
        for m in matches(#"float\s+(_\w+)\s*=\s*in\.(\w+)\.([xyzw]);"#, b) { variables[m[1]] = [m[2], m[3]] }
        for m in matches(#"out\.gl_Position\.([xyzw])\s*=\s*([^;]+);"#, b) { last[m[1]] = trim(m[2]) }
        guard let x = last["x"].flatMap({ variables[$0] }), let location = attr[x[0]],
              ["x", "y", "z"].allSatisfy({ last[$0].flatMap { variables[$0] } == [x[0], $0] }) else { return ["pos_attr": -1, "screen": screen] }
        let w: String
        if last["w"] == "1.0" { w = "one" }
        else if last["w"].flatMap({ variables[$0] }) == [x[0], "w"] { w = "attr" }
        else { return ["pos_attr": -1, "screen": screen] }
        var locations: [String: Int] = [:], outputs: [String: String] = [:]
        for m in matches(#"(m_\d+) \[\[user\(locn(\d+)\)\]\]"#, output) { locations[m[1]] = Int(m[2])! }
        for m in matches(#"out\.(m_\d+)\.([xyzw])\s*=\s*([^;]+);"#, b) { outputs[m[1] + "." + m[2]] = trim(m[3]) }
        var uv: [String: Int] = [:], affine: [String: Any] = [:]
        func fm(_ value: String) -> ([String], [Int], [Int])? {
            let p = "float \(escaped(value)) = fma\\((_\\w+), \\(\\*spvDescriptorSet0\\.vp_c(\\d+)\\)\\._m0\\[(\\d+)\\]\\.([xyzw]), \\(\\*spvDescriptorSet0\\.vp_c(\\d+)\\)\\._m0\\[(\\d+)\\]\\.([xyzw])\\);"
            guard let m = match(p, b), let input = variables[m[1]] else { return nil }
            let components = ["x": 0, "y": 1, "z": 2, "w": 3]
            return (input, [Int(m[2])! - 3, Int(m[3])!, components[m[4]]!], [Int(m[5])! - 3, Int(m[6])!, components[m[7]]!])
        }
        for (member, loc) in locations {
            let sx = variables[outputs[member + ".x"] ?? ""], sy = variables[outputs[member + ".y"] ?? ""]
            if let sx, let sy, sx[0] == sy[0], let a = attr[sx[0]], sx[1] == "x", sy[1] == "y" { uv[String(loc)] = a; continue }
            if let fx = fm(outputs[member + ".x"] ?? ""), let fy = fm(outputs[member + ".y"] ?? ""),
               fx.0[0] == fy.0[0], let a = attr[fx.0[0]], fx.0[1] == "x", fy.0[1] == "y",
               min(fx.1[0], fx.2[0], fy.1[0], fy.2[0]) >= 0 {
                affine[String(loc)] = ["attr": a, "sx": fx.1, "ox": fx.2, "sy": fy.1, "oy": fy.2]
            }
        }
        return ["pos_attr": location, "pos_w": w, "uv_from_attr": uv, "uv_affine": affine, "screen": screen]
    }
}

// Port of pinned nxnative dmsh_fetch.screen_varyings (f8a5618d): the interpolated outputs that are the clip to screen
// varying in every vertex entry point that returns the output struct. See Notices.
extension ShaderReflection {
    typealias Linear = [String: Double]
    private struct LinearError: Error {}
    private final class LinearParser {
        let tokens: [String], env: (String) -> Linear?
        var i = 0
        init(_ tokens: [String], _ env: @escaping (String) -> Linear?) { self.tokens = tokens; self.env = env }
        func add(_ a: Linear, _ b: Linear, _ s: Double = 1.0) -> Linear {
            var r = a
            for (k, v) in b { r[k] = (r[k] ?? 0.0) + s * v }
            return r.filter { $0.value != 0 }
        }
        func constant(_ a: Linear) -> Bool { a.keys.allSatisfy { $0 == "1" } }
        func scale(_ a: Linear, _ c: Double) -> Linear {
            var r: Linear = [:]
            for (k, v) in a where v * c != 0 { r[k] = v * c }
            return r
        }
        func peek() -> String? { i < tokens.count ? tokens[i] : nil }
        func take(_ t: String? = nil) throws -> String {
            guard let x = peek(), t == nil || x == t else { throw LinearError() }
            i += 1; return x
        }
        func mul(_ a: Linear, _ b: Linear) throws -> Linear {
            if constant(a) { return scale(b, a["1"] ?? 0.0) }
            if constant(b) { return scale(a, b["1"] ?? 0.0) }
            throw LinearError()
        }
        func atom() throws -> Linear {
            let t = try take()
            if t == "-" { return scale(try atom(), -1.0) }
            if t == "(" { let v = try sum(); _ = try take(")"); return v }
            if t == "fma" {
                _ = try take("("); let a = try sum(); _ = try take(","); let b = try sum(); _ = try take(","); let c = try sum(); _ = try take(")")
                return add(try mul(a, b), c)
            }
            if t.hasPrefix("#") {
                var digits = String(t.dropFirst())
                if digits.hasSuffix(".") { digits += "0" }
                digits = digits.replacingOccurrences(of: ".e", with: ".0e")
                guard let value = Double(digits) else { throw LinearError() }
                return value != 0 ? ["1": value] : [:]
            }
            guard let v = env(t) else { throw LinearError() }
            return v
        }
        func product() throws -> Linear {
            var v = try atom()
            while let op = peek(), op == "*" || op == "/" {
                _ = try take(); let w = try atom()
                if op == "*" { v = try mul(v, w) }
                else if constant(w), let d = w["1"], d != 0 { v = scale(v, 1.0 / d) }
                else { throw LinearError() }
            }
            return v
        }
        func sum() throws -> Linear {
            var v = try product()
            while let op = peek(), op == "+" || op == "-" {
                _ = try take(); v = add(v, try product(), op == "+" ? 1.0 : -1.0)
            }
            return v
        }
    }
    static func linear(_ expr: String, _ env: @escaping (String) -> Linear?) -> Linear? {
        let pattern = #"\s*(?:(out\.gl_Position\.[xyzw])|(_\d+)|(\d+\.\d*(?:e[+-]?\d+)?|\d+)f?|(fma)|([-+*/(),]))"#
        var tokens: [String] = [], pos = 0
        let length = (expr as NSString).length
        while pos < length {
            guard let m = match(pattern, expr, at: pos), m.end != pos else { return nil }
            if m.range(1).location != NSNotFound { tokens.append(m[1]) }
            else if m.range(2).location != NSNotFound { tokens.append(m[2]) }
            else if m.range(3).location != NSNotFound { tokens.append("#" + m[3]) }
            else if m.range(4).location != NSNotFound { tokens.append(m[4]) }
            else if m.range(5).location != NSNotFound { tokens.append(m[5]) }
            pos = m.end
        }
        let parser = LinearParser(tokens.filter { !$0.isEmpty }, env)
        guard let v = try? parser.sum(), parser.peek() == nil else { return nil }
        return v
    }
    static func entryScreen(_ body: String, _ locations: [String: Int]) -> Set<Int> {
        let tail = body + "\n"
        guard matches(#"\breturn\b"#, body).count == 1, tail.hasSuffix("\n    return out;\n") || tail.hasSuffix("\n    return out;\n\n") else { return [] }
        let lines = body.components(separatedBy: "\n").map(trim)
        for (n, s) in lines.enumerated() where match(#"(if|else|for|while|do|switch)\b"#, s, at: 0) != nil {
            if n + 1 >= lines.count || lines[n + 1] != "{" { return [] }
        }
        var depth = 0, statements: [(Int, String)] = []
        for s in lines {
            if s == "{" { depth += 1; continue }
            if s == "}" { depth -= 1; continue }
            if !s.isEmpty { statements.append((depth, s)) }
        }
        var declared: [String: (Int, Int, String)] = [:], last: [String: (Int, Int, String?)] = [:]
        var assigned: [String: Int] = [:]
        for m in matches(#"(?<![\w.])(_\d+) = "#, body) { assigned[m[1], default: 0] += 1 }
        for (n, (dp, s)) in statements.enumerated() {
            if let m = fullMatch(#"(?:float|int|uint|bool|half) (_\d+) = (.+);"#, s) { declared[m[1]] = (n, dp, m[2]); continue }
            if let m = fullMatch(#"out\.(gl_Position|m_\d+)\.([xyzw]) = (.+);"#, s) { last[m[1] + "." + m[2]] = (n, dp, m[3]); continue }
            if let m = fullMatch(#"out\.(gl_Position|m_\d+) = float4\((.+)\);"#, s) {
                let parts = m[2].components(separatedBy: ",").map(trim)
                for (k, c) in ["x", "y", "z", "w"].enumerated() { last[m[1] + "." + c] = (n, dp, parts.count == 4 ? parts[k] : nil) }
            }
        }
        var position: [String: (Int, Int, String?)] = [:]
        for c in ["x", "y", "w"] {
            guard let p = last["gl_Position." + c], p.1 == 0, p.2 != nil else { return [] }
            position[c] = p
        }
        var symbols: [String: String] = [:]
        for c in ["x", "y", "w"] { if let v = position[c]!.2, fullMatch(#"_\d+"#, v) != nil { symbols[v] = "P" + c } }
        func environment(_ at: Int) -> (String) -> Linear? {
            var memo: [String: Linear?] = [:]
            func env(_ name: String) -> Linear? {
                if name.hasPrefix("out.gl_Position.") {
                    let c = String(name.suffix(1))
                    guard let p = position[c], p.0 < at else { return nil }
                    return ["P" + c: 1.0]
                }
                if let symbol = symbols[name] { return [symbol: 1.0] }
                if let known = memo[name] { return known }
                guard let d = declared[name], d.0 < at, assigned[name, default: 0] == 1 else { return nil }
                memo[name] = .some(nil)
                let value = linear(d.2, environment(d.0))
                memo[name] = .some(value)
                return value
            }
            return env
        }
        let want: [String: Linear] = ["x": ["Px": 0.5, "Pw": 0.5], "y": ["Py": -0.5, "Pw": 0.5], "w": ["Pw": 1.0]]
        var got = Set<Int>()
        for (member, k) in locations {
            var ok = true
            for c in ["x", "y", "w"] {
                guard let a = last[member + "." + c], a.1 == 0, let e = a.2, linear(e, environment(a.0)) == want[c]! else { ok = false; break }
            }
            if ok { got.insert(k) }
        }
        return got
    }
    static func screenVaryings(_ msl: String) -> [Int] {
        let output = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, msl)?[1] ?? ""
        var locations: [String: Int] = [:]
        for m in matches(#"float4 (m_\d+) \[\[user\(locn(\d+)\)\]\]"#, output) { locations[m[1]] = Int(m[2])! }
        let entries = matches(#"(?s)\nvertex main0_out \w+\(.*?\)\n\{\n(.*?)\n\}\n"#, msl)
        if locations.isEmpty || entries.isEmpty { return [] }
        var got = Set(locations.values)
        for e in entries { got.formIntersection(entryScreen(e[1], locations)) }
        return got.sorted()
    }
}
