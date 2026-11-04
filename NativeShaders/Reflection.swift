import Foundation

enum ShaderReflection {
    struct Handle {
        let textureBank: Int, samplerBank: Int, word0: Int, word1: Int, type: Int
    }
    static func handle(_ name: String, textureOnly: Bool = false) -> Handle? {
        let pattern = textureOnly ? #"^[a-z]+_t\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)"# : #"^[a-z]+_[ti]\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)"#
        guard let m = match(pattern, name), let h = Int(m[2], radix: 16) else { return nil }
        let slots = Int(m[1]) ?? -1
        let tb = slots >= 0 ? slots & 0xffff : 2
        let sb = slots >= 0 && slots >> 16 != 0 ? (slots >> 16) - 1 : tb
        return Handle(textureBank: tb, samplerBank: sb, word0: h & 0x3fff, word1: (h >> 14) & 0x3fff, type: h >> 28)
    }
    static func decode(_ name: String) -> (Int?, Int?, Int)? {
        guard let h = handle(name) else { return nil }
        let words = [h.word0] + (h.type != 0 ? [h.word1] : [])
        func slot(_ word: Int, _ base: Int, _ bank: Int) -> Int? {
            bank == 2 && word * 4 >= base && (word * 4 - base) % 8 == 0 ? (word * 4 - base) / 8 : nil
        }
        if match(#"^[a-z]+_i"#, name) != nil {
            let image = words.filter { 0x120 <= $0 * 4 && $0 * 4 < 0x160 }.compactMap { slot($0, 0x120, h.textureBank) }.first
            return (image, nil, h.type)
        }
        let tw = h.type == 3 ? [h.word0] : words
        var texture = tw.filter { $0 * 4 < 0x168 }.compactMap { slot($0, 0x20, h.textureBank) }.first
        if texture == nil { texture = tw.filter { $0 * 4 < 0x568 }.compactMap { slot($0, 0x168, h.textureBank).map { 128 + $0 } }.first }
        let sampler = h.type == 3 ? nil : words.filter { $0 * 4 >= 0x568 }.compactMap { slot($0, 0x568, h.samplerBank) }.first
        return (texture, sampler, h.type)
    }
    static func ubo(_ name: String) -> (Int, Int, String, Int, Int)? {
        guard match(#"^[a-z]+_t\w*?_cb\d+_"#, name) != nil, let h = handle(name, textureOnly: true),
              (3...16).contains(h.textureBank), [0,1,3].contains(h.type), h.type != 1 || (3...16).contains(h.samplerBank) else { return nil }
        if h.type == 3 { return (h.textureBank - 3, h.word0 * 4, "id", h.word1, 0) }
        return (h.textureBank - 3, h.word0 * 4, "ubo", h.samplerBank - 3, (h.type == 1 ? h.word1 : h.word0) * 4)
    }
    static func c2Array(_ name: String, _ type: String) -> (Int, Int?)? {
        let n = name.hasSuffix("Smplr") ? String(name.dropLast(5)) : name
        guard type.hasPrefix("array<"), match(#"^[a-z]+_t\w*?_cb\d+_[0-9A-F]+$"#, n) != nil, let h = handle(n, textureOnly: true) else { return nil }
        if h.type == 0 && h.textureBank == 2 && h.samplerBank == 2 { return (h.word0 * 4, nil) }
        if h.type == 1 && h.textureBank == 2 && h.samplerBank == 2 { return (h.word0 * 4, h.word1 * 4) }
        return nil
    }
    static func bindings(stage: String, source: String, resources: [String: Any]) throws -> [[String: Any]] {
        let body = match(#"(?s)struct spvDescriptorSetBuffer0\s*\{(.*?)\};"#, source)?[1] ?? ""
        let members = matches(ShaderTransforms.memberPattern, body)
        guard members.count == body.components(separatedBy: "[[id(").count - 1 else { throw ShaderTransformError.unsupported("unreadable argument member") }
        let sbufs = (resources["sbufs"] as? [[String: Any]] ?? []).sorted { ($0["binding"] as! Int) < ($1["binding"] as! Int) }
        let helpers = ["vertex_output": 0, "geometry_vb_output": 1, "geometry_ib_output": 2, "vb_info": 3, "trb_data": 4, "vb_input": 1]
        var binds: [[String: Any]] = []
        for m in members {
            let ty = trim(m[1]); var nm = m[2]
            var e: [String: Any] = ["msl_name": nm, "msl_type": ty, "arg_id": Int(m[3])!]
            let pool = ty == "constant spvPool_\(nm)*" ? match("struct spvPool_\(escaped(nm))\\s*\\{\\s*uint n;\\s*array<(.+), \\d+> e;", source) : nil
            if ty == "sampler" && nm.hasSuffix("Smplr_w") { nm = String(nm.dropLast(2)); e["border_bit"] = binds.filter { $0["border_bit"] != nil }.count }
            if nm == "dm_border" { e["kind"] = "border" }
            else if let reg = helpers[nm] { e["kind"] = "geometry"; e["reg"] = reg }
            else if let pool { e["kind"] = pool[1] == "sampler" ? "samp_pool" : "tex_pool"; e["elem"] = pool[1] }
            else if nm == "support_buffer" { e["kind"] = "support" }
            else if let cb = match(#"^[a-z]+_c(\d+)$"#, nm) {
                let n = Int(cb[1])!
                if (3...16).contains(n) { e["kind"] = "cbuf"; e["reg"] = n - 3 }
                else { e["kind"] = n == 1 ? "c1" : "drvcb"; e["bank"] = n }
            } else if let sb = match(#"^[a-z]+_s(\d+)$"#, nm) { e["kind"] = "sbuf"; e["index"] = Int(sb[1])! }
            else if let u = ubo(nm.hasSuffix("Smplr") ? String(nm.dropLast(5)) : nm) {
                if nm.hasSuffix("Smplr") { e["kind"] = u.2 == "id" ? "samp_id" : "samp_ubo"; e["reg"] = u.3; if u.2 != "id" { e["off"] = u.4 } }
                else if ty.contains("texture_buffer") { e["kind"] = "?" }
                else { e["kind"] = "tex_ubo"; e["reg"] = u.0; e["off"] = u.1 }
            } else if let array = c2Array(nm, ty) {
                e["kind"] = nm.hasSuffix("Smplr") ? "samp_c2a" : "tex_c2a"; e["off"] = array.0
                if let off = array.1 { e["soff"] = off }
            } else if nm.hasSuffix("Smplr") {
                let base = String(nm.dropLast(5))
                guard let d = decode(base) else { throw ShaderTransformError.unsupported("unknown sampler") }
                if d.2 == 3 { e["kind"] = "samp_id"; e["reg"] = handle(base, textureOnly: true)?.word1 ?? 0 }
                else { e["kind"] = d.2 != 0 ? "samp" : "samp_tex"; e["reg"] = (d.2 != 0 ? d.1 : d.0).map { $0 as Any } ?? NSNull() }
            } else {
                if let d = decode(nm) {
                    e["kind"] = match(#"^[a-z]+_i"#, nm) != nil ? "img" : (ty.contains("texture_buffer") ? "tbuf" : "tex")
                    e["reg"] = d.0.map { $0 as Any } ?? NSNull()
                } else { e["kind"] = "?" }
                if e["kind"] as? String == "img" && match("\\b\(escaped(nm))\\.atomic_", source) != nil { e["atomic"] = true }
                if e["kind"] as? String == "tex", let reg = e["reg"] as? Int, (resources["fetch1d"] as? [Int] ?? []).contains(reg) {
                    e["fetch1d"] = true
                    if match("\\b\(escaped(nm))\\.get_width\\(", source) != nil { e["fetch1d_size"] = true }
                }
            }
            if e["kind"] as? String == "sbuf" {
                let index = e["index"] as! Int
                let b = index < sbufs.count ? sbufs[index] : [:]
                let bank = b["cb_slot"] as? Int, cbOffset = b["cb_offset"] as? Int
                let off = bank == 0 ? cbOffset.map { $0 * 4 } : nil
                if stage == "cs", let off, (0x210..<0x310).contains(off), (off - 0x210) % 16 == 0 { e["kind"] = "ubo_ptr"; e["reg"] = (off - 0x210) / 16 }
                else if let bank, bank >= 3, let cbOffset { e["kind"] = "sbuf_ubo"; e["reg"] = bank - 3; e["off"] = cbOffset * 4 }
                else if let off, let base = ["vs": 0x110, "cs": 0x310, "gs": 0x410, "ps": 0x510][stage], (0..<256).contains(off - base), (off - base) % 16 == 0 { e["reg"] = (off - base) / 16 }
                else { e["reg"] = NSNull() }
                e["rw"] = (b["flags"] as? Int ?? 0) & 1 != 0
            }
            binds.append(e)
        }
        return binds
    }
    static func driverReads(_ source: String) -> [String: Any] {
        var out: [String: Any] = [:]
        for bank in [0, 2] {
            var offsets = Set<Int>(), dynamic = false
            for m in matches("fp_c\(bank)\\)\\._m0\\[([^\\]]+)\\]", source) {
                let index = m[1].replacingOccurrences(of: #"u+$"#, with: "", options: .regularExpression)
                if let n = Int(index), match(#"^\d+$"#, index) != nil { offsets.insert(n * 16) }
                else { dynamic = true }
            }
            if !offsets.isEmpty || dynamic { out[String(bank)] = ["vec4_offsets": offsets.sorted(), "dynamic": dynamic] }
        }
        return out
    }
    static func unfillable(_ reads: [String: Any]) -> [String] {
        let filled = ["0": [(0x110,0x210),(0x310,0x410),(0x410,0x510),(0x510,0x610)], "2": [(0x20,0x168),(0x120,0x160),(0x168,0x568),(0x568,0x968)]]
        var bad: [String] = []
        for bank in ["0", "2"] {
            guard let read = reads[bank] as? [String: Any] else { continue }
            if bank == "0" && read["dynamic"] as? Bool == true { bad.append("c0 dynamic index") }
            for off in read["vec4_offsets"] as? [Int] ?? [] where !filled[bank]!.contains(where: { off < $0.1 && $0.0 < off + 16 }) { bad.append("c\(bank)+0x\(String(off, radix: 16))") }
        }
        return bad
    }
}
