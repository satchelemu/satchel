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
    static func layer(_ input: String) throws -> String {
        guard let i = find(input, "struct main0_out\n{\n"), let j = find(input, "\n};", i),
              slice(input, i, j).contains("[[render_target_array_index]]"), slice(input, i, j).contains("[[point_size]]") else { return input }
        let src = slice(input, 0, i) + sub(#"\n    float gl_PointSize \[\[point_size\]\];"#, slice(input, i, j)) { _ in "" } + slice(input, j)
        return try dropPointSize(src)
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
    static func uintFragment(_ src: String) throws -> (String?, Int) {
        guard let m = match(#"(?m)^.*\bfragment\s+main0_out\s+main0\s*\("#, src),
              let st = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, src) else { return (nil, 0) }
        let fields = matches(#"(?m)^\s*(\w+)\s+(\w+)\s*(\[\[[^\]]*\]\])?;"#, st[1])
        let colors = fields.filter { $0[3].contains("color(") && $0[1].hasPrefix("float") }
        let names = Set(colors.map { $0[2] })
        // One pass over the stores to colour outputs (pinned fs_uint), with no word character before out.
        let stores = matches(#"out\.(\w+)(?:\.\w+)? = ([^;]+);"#, src).filter { names.contains($0[1]) && !inWord(src, $0.start) }
        let fromInt = Set(stores.filter { match(#"^as_type<float\d?>\("#, $0[2]) != nil }.map { $0[1] })
        let writes = stores.map { $0[2] }
        let flat: Set<String> = src.contains(", flat]]") ? Set(matches(#"\b(\w+) \[\[user\(locn\d+\), flat\]\]"#, src).map { $0[1] }) : []
        func passed(_ value: String) -> Bool {
            let w = trim(value)
            let d = fullMatch(#"\w+"#, w) != nil ? match("\\bfloat\\d? \(w) = (in\\.\\w+(?:\\.\\w+)?);", src) : nil
            guard let i = fullMatch(#"in\.(\w+)(?:\.\w+)?"#, d?[1] ?? w) else { return false }
            return flat.contains(i[1])
        }
        var body = slice(src, m.start)
        if let r = find(body, "return out;") { body = slice(body, 0, r + 1) }
        let ints = try integerOutputs(body, colors: colors.map { $0[2] })
        let constant = !writes.isEmpty && (writes.allSatisfy({ fullMatch(#"-?[0-9.]+f?|float\d\([-0-9., ]*\)"#, trim($0)) != nil }) || writes.allSatisfy(passed))
        if fromInt.isEmpty && ints.isEmpty && !constant { return (nil, 0) }
        func utype(_ t: String) -> String { "u" + t.replacingOccurrences(of: "float", with: "int") }
        func entry(_ suffix: String, _ uints: Set<String>) -> String {
            let structure = "struct main0_out_\(suffix)\n{\n" + fields.map { "    \(uints.contains($0[2]) ? utype($0[1]) : $0[1]) \($0[2]) \($0[3]);\n" }.joined() + "};\n\n"
            let ret = "return main0_out_\(suffix){" + fields.map { uints.contains($0[2]) ? "as_type<\(utype($0[1]))>(out.\($0[2]))" : "out.\($0[2])" }.joined(separator: ", ") + "};"
            var body = slice(src, m.start)
            if let e = match(#"\bfragment\s+main0_out\s+main0\s*\("#, body) { body = slice(body, 0, e.start) + "fragment main0_out_\(suffix) main0_\(suffix)(" + slice(body, e.end) }
            return "\n" + structure + body.replacingOccurrences(of: "return out;", with: ret)
        }
        var out = src + entry("u", names), mask = 0
        let asInt = ints.isEmpty ? fromInt : ints
        if !asInt.isEmpty && !names.subtracting(asInt).isEmpty {
            out += entry("m", asInt)
            for f in fields where asInt.contains(f[2]) { mask += 1 << Int(match(#"color\((\d+)\)"#, f[3])![1])! }
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

// Ports of the transformations nxnative f8a5618d added to dmsh_build (see Notices).
extension ShaderTransforms {
    // Python's modulo: the result has the divisor's sign.
    static func pyMod(_ a: Int, _ b: Int) -> Int { ((a % b) + b) % b }

    // Ampere HMMA (dmsh_build hmma_msl) needs NVN2 code, which ShaderRecord refuses; fail closed if it appears.
    static func checkTensor(_ src: String) throws {
        if match(#"\n(\w+) dmsh_hmma16816_f16\("#, src) != nil { throw ShaderTransformError.unsupported("Ampere HMMA outside the Switch translation contract") }
    }

    // dmsh_wide.wide_loads: storage words the translator split, read whole again.
    static func wideLoads(_ msl: String) -> String {
        if !msl.contains(" >> uint(2)") { return msl }
        let ident = #"_[0-9A-Za-z_]+"#, buffer = #"[a-z]p_s\d+"#
        let readOnly = Set(matches(#"(?m)^\s*const device "# + ident + #"\* ("# + buffer + #") \[\[id\(\d+\)\]\];"#, msl).map { $0[1] })
        if readOnly.isEmpty { return msl }
        var lines = msl.components(separatedBy: "\n")
        let addPattern = #"^\s*int ("# + ident + #") = ("# + ident + #") \+ (-?\d+);\s*$"#
        let indexPattern = #"^\s*uint ("# + ident + #") = uint\(int\(uint\(("# + ident + #")\) >> uint\(2\)\)\);\s*$"#
        let loadPattern = #"\(\*spvDescriptorSet0\.("# + buffer + #")\)\._m0\[int\(("# + ident + #")\)\]"#
        let op = #"(?:[-+*/%|&^]|<<|>>)?=(?!=)"#
        let writePattern = #"atomic_|_barrier\(|\.write\(|\]\s*"# + op + #"|^\s*\*|^\s*"# + ident + #"\s*"# + op
        var adds: [String: (String, Int)] = [:], index: [String: String] = [:]
        for line in lines {
            if let m = match(addPattern, line, at: 0) { adds[m[1]] = (m[2], Int(m[3])!); continue }
            if let m = match(indexPattern, line, at: 0) { index[m[1]] = m[2] }
        }
        func root(_ start: String) -> (String, Int) {
            var v = start, off = 0
            for _ in 0..<64 {
                guard let next = adds[v] else { break }
                v = next.0; off += next.1
            }
            return (v, off)
        }
        struct Word { let line: Int; let start: Int; let end: Int; let index: String }
        var order: [String] = [], words: [String: (String, [Int: Word])] = [:], block = 0
        for (i, line) in lines.enumerated() {
            if line.contains("{") || line.contains("}") || match(writePattern, line) != nil { block += 1; continue }
            let found = matches(loadPattern, line)
            guard found.count == 1, readOnly.contains(found[0][1]), let iv = index[found[0][2]] else { continue }
            let (v, off) = root(iv)
            guard pyMod(off, 4) == 0 else { continue }
            let key = "\(block)\u{0}\(found[0][1])\u{0}\(v)"
            if words[key] == nil { order.append(key); words[key] = (found[0][1], [:]) }
            if words[key]!.1[off] == nil { words[key]!.1[off] = Word(line: i, start: found[0].start, end: found[0].end, index: found[0][2]) }
        }
        var edits: [Int: (Int, Int, String, String?)] = [:], n = 0
        let vectors = [2: "packed_uint2", 3: "packed_uint3", 4: "packed_uint4"]
        for key in order {
            let (buf, byOffset) = words[key]!
            for base in Set(byOffset.keys.map { $0 - pyMod($0, 16) }).sorted() {
                var run: [Word] = []
                while run.count < 4, let w = byOffset[base + 4 * run.count] { run.append(w) }
                if run.count < 2 || run.contains(where: { $0.line < run[0].line }) { continue }
                let name = "_dmw\(n)", vec = vectors[run.count]!; n += 1
                let decl = "\(vec) \(name) = *(const device \(vec)*)&(*spvDescriptorSet0.\(buf))._m0[int(\(run[0].index))];"
                for (w, word) in run.enumerated() { edits[word.line] = (word.start, word.end, "\(name).\(["x", "y", "z", "w"][w])", w == 0 ? decl : nil) }
            }
        }
        for (i, e) in edits {
            let original = lines[i]
            let edited = slice(original, 0, e.0) + e.2 + slice(original, e.1)
            let indent = String(original.prefix(while: { $0 == " " || $0 == "\t" }))
            lines[i] = (e.3.map { indent + $0 + "\n" } ?? "") + edited
        }
        return lines.joined(separator: "\n")
    }

    // dmsh_build first_store: a store only the first invocation of a dispatch makes leaves the shader for the host.
    static let firstStore1D = #"float (\w+) = as_type<float>\(gl_WorkGroupID\.x\);\s*float (\w+) = as_type<float>\(gl_LocalInvocationID\.x\);\s*int (\w+) = as_type<int>\(\1\) << (\d+);\s*"#
        + #"int (\w+) = ~as_type<int>\(\2\);\s*int (\w+) = \5 \+ 1;\s*bool (\w+) = \6 != \3;\s*"#
    static let firstStore2D = #"float (\w+) = as_type<float>\(gl_WorkGroupID\.y\);\s*float (\w+) = as_type<float>\(gl_LocalInvocationID\.y\);\s*float (\w+) = as_type<float>\(gl_LocalInvocationID\.x\);\s*"#
        + #"float (\w+) = as_type<float>\(gl_WorkGroupID\.x\);\s*float (\w+) = as_type<float>\(gl_WorkGroupID\.z\);\s*"#
        + #"int (\w+) = as_type<int>\(\1\) << (\d+);\s*int (\w+) = ~as_type<int>\(\2\);\s*int (\w+) = \8 \+ 1;\s*bool (\w+) = \9 != \6;\s*"#
        + #"int (\w+) = as_type<int>\(\4\) << (\d+);\s*int (\w+) = ~as_type<int>\(\3\);\s*int (\w+) = \13 \+ 1;\s*bool (\w+) = \14 == \11;\s*"#
        + #"bool (\w+) = !\10;\s*bool (\w+) = \15 && \16;\s*bool (\w+) = (?:as_type<int>\(\5\) == 0|0 == as_type<int>\(\5\));\s*bool (\w+) = \18 && \17;\s*(?:bool (\w+) = !\19;\s*)?"#
    static func firstStore(_ msl: String, local: [Int]?) -> (String, [(String, Int, Int)]) {
        guard let local, local.count == 3, local[2] == 1 else { return (msl, []) }
        let m: ShaderMatch, guardPattern: String
        if let one = match(firstStore1D, msl) {
            guard local[1] == 1, let shift = Int(one[4]), shift < 63, 1 << shift == local[0] else { return (msl, []) }
            m = one; guardPattern = "!" + escaped(one[7])
        } else {
            guard let two = match(firstStore2D, msl), let sy = Int(two[7]), let sx = Int(two[12]), sy < 63, sx < 63,
                  1 << sy == local[1], 1 << sx == local[0] else { return (msl, []) }
            m = two; guardPattern = two.range(20).location != NSNotFound ? "!" + escaped(two[20]) : escaped(two[19])
        }
        let held = #"int (\w+) = \w+;\s*if \("# + guardPattern + #"\)\s*\{\s*\1 = (\d+);\s*\}\s*int (\w+) = \1;\s*"#
        let blockPattern = #"if \("# + guardPattern + #"\)\s*\{\s*((?:\(\*\w+\.\w+\)\._m0\[\d+\] = (?:\d+u|uint\(\w+\));\s*)+)\}\s*"#
        let plain = #"int \w+ = [^;{}]*;\s*"#
        var at = m.end, out = slice(msl, 0, m.end), stores: [(String, Int, Int)] = [], values: [String: Int] = [:]
        while true {
            if let h = match(held, msl, at: at) {
                guard let value = Int(h[2]) else { return (msl, []) }
                values[h[3]] = value; out += slice(msl, at, h.end); at = h.end; continue
            }
            if let b = match(blockPattern, msl, at: at) {
                for s in matches(#"\(\*\w+\.(\w+)\)\._m0\[(\d+)\] = (\d+u|uint\(\w+\));"#, b[1]) {
                    guard let word = Int(s[2]) else { return (msl, []) }
                    if s[3].hasSuffix("u") { guard let v = Int(s[3].dropLast()) else { return (msl, []) }; stores.append((s[1], word, v)) }
                    else if let v = values[String(s[3].dropFirst(5).dropLast())] { stores.append((s[1], word, v)) }
                    else { return (msl, []) }
                }
                at = b.end; continue
            }
            guard let p = match(plain, msl, at: at), match(held, msl, at: p.start) == nil else { break }
            out += slice(msl, at, p.end); at = p.end
        }
        let rest = slice(msl, at)
        if stores.isEmpty || stores.count > 4 || match(#"if \("# + guardPattern + #"\)"#, rest) != nil { return (msl, []) }
        return (out + rest, stores)
    }

    // dmsh_build tg_spill: compute shared memory past Metal's 32 KB in a device buffer (DMSH_TG_SLOT 30, DMSH_TG_MAX 0x8000, DMSH_TG_GROUPS 512).
    static func spillShared(_ msl: String) throws -> (String, Int) {
        guard let m = match(#"\bkernel\s+void\s+main0\s*\("#, msl) else { return (msl, 0) }
        let bytes: [String: Int] = ["uint": 4, "int": 4, "float": 4, "uint2": 8, "int2": 8, "float2": 8, "uint3": 16, "int3": 16, "float3": 16, "uint4": 16, "int4": 16, "float4": 16,
                                    "half": 2, "short": 2, "ushort": 2, "half2": 4, "half4": 8, "char": 1, "uchar": 1]
        var decls: [(ShaderMatch, Int)] = [], total = 0
        for d in matches(#"(?m)^([ \t]*)threadgroup (spvUnsafeArray<(\w+), (\d+)>|\w+) (\w+)(?:\[(\d+)\])?;[ \t]*$"#, msl) where d.start >= m.end {
            let elem = d.range(3).location != NSNotFound ? d[3] : d[2]
            let count = Int(d.range(4).location != NSNotFound ? d[4] : (d.range(6).location != NSNotFound ? d[6] : "1")) ?? 0
            guard let size = bytes[elem] else { throw ShaderTransformError.unsupported("threadgroup array of an unknown element") }
            decls.append((d, total)); total += (size * count + 15) & ~15
        }
        if total <= 0x8000 { return (msl, 0) }
        let text = msl as NSString
        var depth = 1, i = m.end
        while depth > 0 {
            guard i < text.length else { throw ShaderTransformError.unsupported("unbalanced kernel parameters") }
            let c = text.character(at: i)
            if c == 40 { depth += 1 } else if c == 41 { depth -= 1 }
            i += 1
        }
        let params = slice(msl, m.end, i - 1)
        let g = match(#"uint3 (\w+) \[\[threadgroup_position_in_grid\]\]"#, params)
        let gid = g?[1] ?? "dm_tgid"
        let add = (g == nil ? ", uint3 dm_tgid [[threadgroup_position_in_grid]]" : "") + ", uint3 dm_ntg [[threadgroups_per_grid]], device uchar* dm_tg [[buffer(30)]]"
        var out = "", at = 0
        for (k, (d, off)) in decls.enumerated() {
            out += slice(msl, at, d.start)
            let hasCount = d.range(6).location != NSNotFound
            let type = d[2] + (hasCount ? "[\(d[6])]" : "")
            if k == 0 { out += "\(d[1])ulong dm_tgbase = ulong(((\(gid).z * dm_ntg.y + \(gid).y) * dm_ntg.x + \(gid).x) % 512u) * \(total)ul;\n" }
            if hasCount { out += "\(d[1])device \(d[2]) (&\(d[5]))[\(d[6])] = *(device \(d[2]) (*)[\(d[6])])(dm_tg + dm_tgbase + \(off));" }
            else { out += "\(d[1])device \(type)& \(d[5]) = *(device \(type)*)(dm_tg + dm_tgbase + \(off));" }
            at = d.end
        }
        var src = out + slice(msl, at)
        guard let p = find(src, params, m.start) else { throw ShaderTransformError.unsupported("kernel parameters moved") }
        let e = p + (params as NSString).length
        src = slice(src, 0, e) + add + slice(src, e)
        let lines = src.components(separatedBy: "\n").map { line -> String in
            if line.contains("operator [] (size_t pos)") { return line }
            let device = sub(#"\bthreadgroup\b(?=\s)"#, line) { _ in "device" }
            return sub(#"(?<!mem_device \| )mem_flags::mem_threadgroup\b"#, device) { _ in "mem_flags::mem_device | mem_flags::mem_threadgroup" }
        }
        return (lines.joined(separator: "\n"), total)
    }

    // dmsh_build drop_point_size: the point size output leaves vertex functions whose output struct lost it.
    static func dropPointSize(_ input: String) throws -> String {
        var src = sub(#"\n    out\.gl_PointSize = [^;\n]*;"#, input) { _ in "" }
        while let use = find(src, "out.gl_PointSize") {
            guard let a = matches(#"\bvertex\s+\w+\s+\w+\s*\("#, src).last(where: { $0.start < use }),
                  let b = find(src, "{", a.end) else { throw ShaderTransformError.unsupported("a point size output used outside a vertex function") }
            let text = src as NSString
            var depth = 0, e = b
            while e < text.length {
                let c = text.character(at: e)
                if c == 123 { depth += 1 } else if c == 125 { depth -= 1 }
                if depth == 0 { break }
                e += 1
            }
            let body = slice(src, b + 1, min(e, text.length))
            guard body.contains("out.gl_PointSize") else { throw ShaderTransformError.unsupported("a point size output outside its vertex function") }
            src = slice(src, 0, b + 1) + "\n    float dmsh_point_size = 1.0;" + body.replacingOccurrences(of: "out.gl_PointSize", with: "dmsh_point_size") + slice(src, min(e, text.length))
        }
        return src
    }

    // dmsh_build vs_writes: storage or image writes keep a rasterizer-discard entry point.
    static func vertexWrites(_ src: String, bindings: [[String: Any]]) -> Bool {
        bindings.contains { $0["rw"] as? Bool == true } || match(#"access::(read_)?write\b"#, src) != nil
    }

    // dmsh_build attributes: the vertex attribute declarations, searched from the first to the last declaring line.
    static func attributes(_ src: String) -> [ShaderMatch] {
        guard let i = find(src, "[[attribute(") else { return [] }
        let text = src as NSString
        let j = text.range(of: "[[attribute(", options: .backwards).location
        let lineStart = text.range(of: "\n", options: .backwards, range: NSRange(location: 0, length: i)).location
        let from = lineStart == NSNotFound ? 0 : lineStart + 1
        let to = find(src, "\n", j) ?? text.length
        return matches(#"(\w+)\s+(\w+)\s*\[\[attribute\((\d+)\)\]\]"#, slice(src, from, to))
    }

    // dmsh_build int_outputs: the colour outputs that hold integers, not float bits.
    static func integerOutputs(_ body: String, colors: [String]) throws -> Set<String> {
        var types: [String: String] = [:], definitions: [String: [String]] = [:]
        for m in matches(#"(?m)^\s*(u?int\d?|float\d?|bool)\s+(_\d+)\b"#, body) { types[m[2]] = m[1] }
        for m in matches(#"(?m)^\s*(?:(?:u?int\d?|float\d?|bool)\s+)?(_\d+)\s*=\s*([^;]+);"#, body) { definitions[m[1], default: []].append(trim(m[2])) }
        func kinds(_ e: String, _ seen: inout Set<String>) -> Set<String> {
            if fullMatch(#"_\d+"#, e) != nil {
                if (types[e] ?? "").hasPrefix("float") { return ["float"] }
                if seen.contains(e) { return [] }
                seen.insert(e)
                var result = Set<String>()
                for x in definitions[e] ?? [] { result.formUnion(kinds(x, &seen)) }
                return result
            }
            if fullMatch(#"-?\d+u?"#, e) != nil {
                let digits = e.hasSuffix("u") ? String(e.dropLast()) : e
                guard let value = Int(digits) else { return [] }
                return 0 < abs(value) && abs(value) < 0x800000 ? ["int"] : []
            }
            if let t = fullMatch(#"[^?]+\?\s*([^:]+?)\s*:\s*(.+)"#, e) {
                let first = kinds(t[1], &seen)
                return first.union(kinds(t[2], &seen))
            }
            if let a = fullMatch(#"as_type<u?int\d?>\((.*)\)"#, e) {
                return match(#"fp_c\d|\bin\.|^-?[0-9.]"#, a[1]) != nil ? [] : ["float"]
            }
            return e.hasPrefix("(*spvDescriptorSet0") || e.hasPrefix("spvDescriptorSet0") ? [] : ["int"]
        }
        var found: [(String, Set<String>)] = []
        for name in colors {
            var k = Set<String>()
            for w in matches("\\bout\\.\(name)(?:\\.\\w+)? = ([^;]+);", body) {
                let write = trim(w[1])
                if let a = fullMatch(#"as_type<float\d?>\((.*)\)"#, write) { var seen = Set<String>(); k.formUnion(kinds(trim(a[1]), &seen)) }
                else if fullMatch(#"-?[0-9.]+(?:e[-+]?\d+)?f?"#, write) != nil {
                    guard let value = Double(write.hasSuffix("f") ? String(write.dropLast()) : write) else { throw ShaderTransformError.unsupported("unreadable colour constant") }
                    if 0 < abs(value) && abs(value) < 1.17549e-38 { k.insert("int") }
                } else { k.insert("float") }
            }
            found.append((name, k))
        }
        let clean = Set(found.filter { $0.1 == ["int"] }.map { $0.0 })
        return clean.isEmpty ? Set(found.filter { $0.1.contains("int") }.map { $0.0 }) : clean
    }
}
