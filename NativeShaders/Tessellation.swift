import Foundation

// Tessellation control (hull) and evaluation (domain) shaders on Metal 4, which has no
// tessellation in its render encoder or pipelines. A program with these stages runs as:
//   1. the vertex shader's capture entry (main0_gs, the geometry route's capture) writes each
//      control point's outputs;
//   2. the hull, a compute kernel with one threadgroup per patch, reads them and writes one
//      control point record per output control point and one patch record per patch;
//   3. the tessellator kernels below turn each patch record's levels into domain points and
//      an index list (a compute tessellator, since there is no hardware one);
//   4. the domain shader, rewritten as a plain vertex function, reads its domain point by
//      vertex ID, its patch's records, and feeds the rasterizer as a vertex shader would.
// SPIRV-Cross (Apache 2.0, already pinned) writes the hull as a kernel and the domain as a
// post-tessellation vertex function; this file only changes their input and output plumbing.
// The records below are this file's ABI with the host, independent of each shader's own
// struct layout, so any hull pairs with any domain shader:
//   control point record, 576 bytes: the geometry capture's 144-word map (words 0-3 position,
//     4 point size, 5-12 clip distances, 13 layer, 14 viewport, 16 + 4 L + c user location L);
//   patch record, 528 bytes: outer levels (4 floats), inner levels (2), 2 unused, then
//     31 float4 per-patch user locations (Maxwell per-patch attributes 0x18 + 16 L).
// The tessellation mode (domain, spacing, winding) is the 3D engine's state, set by NVN from
// the program; the record has no decoded field for it yet, so request facts give it
// (tess_domain, tess_spacing, tess_cw), defaulting to triangles, equal spacing, counter-clockwise.
enum ShaderTessellation {
    static let controlPointBytes = 576, patchBytes = 528
    static let buffers: [String: [String: Int]] = [
        "hs": ["capture": 13, "input": 14, "patch": 26, "control_points": 28, "params": 29],
        "ds": ["points": 16, "patches": 17, "control_points": 18, "control_point_count": 19]]

    struct Interface {
        var location: [UInt32: Int] = [:], patch: Set<UInt32> = [], modes: [UInt32: [UInt32]] = [:]
    }
    static func interface(_ spirv: Data) throws -> Interface {
        let w = try ShaderSpirv.words(spirv)
        var io = Interface(), i = 5
        while i < w.count {
            let n = Int(w[i] >> 16), op = w[i] & 65535
            guard n > 0, n <= w.count - i else { throw ShaderTransformError.unsupported("invalid tessellation SPIR-V instruction") }
            if op == 71, n >= 3 {   // OpDecorate: Location 30, Patch 15
                if w[i + 2] == 30, n == 4 { io.location[w[i + 1]] = Int(w[i + 3]) }
                if w[i + 2] == 15 { io.patch.insert(w[i + 1]) }
            }
            if op == 16, n >= 3 { io.modes[w[i + 2]] = Array(w[(i + 3)..<(i + n)]) }   // OpExecutionMode
            i += n
        }
        return io
    }
    // SPIRV-Cross's MSL backend refuses isolines. The domain shader is rewritten into a
    // plain vertex function anyway, and an isoline's coordinate is two-dimensional like a
    // quad's, so the copy given to SPIRV-Cross says quads; reflection keeps isolines.
    static func isolinesAsQuads(_ spirv: Data) throws -> Data {
        var w = try ShaderSpirv.words(spirv), i = 5
        while i < w.count {
            let n = Int(w[i] >> 16)
            guard n > 0, n <= w.count - i else { throw ShaderTransformError.unsupported("invalid tessellation SPIR-V instruction") }
            if w[i] & 65535 == 16, n >= 3, w[i + 2] == 25 { w[i + 2] = 24 }
            i += n
        }
        return w.withUnsafeBytes { Data($0) }
    }

    struct Member { let type: String, name: String, attribute: Int? }
    static func members(_ name: String, _ source: String) -> (ShaderMatch, [Member])? {
        guard let st = match("(?s)struct \(name)\\n\\{\\n(.*?)\\n\\};", source) else { return nil }
        let list = st[1].components(separatedBy: "\n").map(trim).filter { !$0.isEmpty }.map { line -> Member in
            if let m = match(#"^(.+?) (\w+(?:\[\d+\])?)(?: \[\[attribute\((\d+)\)\]\])?;$"#, line) {
                return Member(type: m[1], name: m[2], attribute: m[3].isEmpty ? nil : Int(m[3]))
            }
            return Member(type: "?", name: line, attribute: nil)
        }
        return (st, list)
    }
    static func userLocation(_ member: Member, _ io: Interface, patch: Bool) throws -> Int {
        guard let id = match(#"^m_(\d+)$"#, member.name).flatMap({ UInt32($0[1]) }), let location = io.location[id],
              io.patch.contains(id) == patch, (0..<(patch ? 31 : 32)).contains(location),
              ["float4", "int4", "uint4"].contains(member.type), member.attribute == nil || member.attribute == location else {
            throw ShaderTransformError.unsupported("unsupported tessellation interface member")
        }
        return location
    }
    // The canonical control point record, with the shader's own member names where it has them.
    static func controlPointStruct(_ name: String, _ list: [Member], _ io: Interface, output: Bool) throws -> (String, [Int: Member], String?) {
        var user: [Int: Member] = [:], position: String?, size: String?, clip: Member?, layer: String?, viewport: String?
        for member in list {
            switch (member.type, member.name) {
            case ("float4", "gl_Position"): position = member.name
            case ("float", "gl_PointSize") where output: size = member.name
            case ("uint", "gl_Layer") where output: layer = member.name
            case ("uint", "gl_ViewportIndex") where output: viewport = member.name
            case (_, let n) where n.hasPrefix("gl_ClipDistance") && output:
                guard clip == nil, member.type == "float" || member.type.hasPrefix("spvUnsafeArray<float, ") else {
                    throw ShaderTransformError.unsupported("unsupported tessellation clip distance")
                }
                clip = member
            default:
                let location = try userLocation(member, io, patch: false)
                guard user[location] == nil else { throw ShaderTransformError.unsupported("duplicate tessellation location") }
                user[location] = member
            }
        }
        var clipText = "    float dm_clip[8];\n"
        if let clip {
            let count: Int
            if let m = match(#"^spvUnsafeArray<float, (\d+)>$"#, clip.type) { count = Int(m[1])! }
            else if let m = match(#"^gl_ClipDistance\[(\d+)\]$"#, clip.name) { count = Int(m[1])! } else { count = 0 }
            guard (1...8).contains(count) else { throw ShaderTransformError.unsupported("unsupported tessellation clip distance") }
            clipText = "    \(clip.type) \(clip.name);\n" + (count < 8 ? "    float dm_clip_pad[\(8 - count)];\n" : "")
        }
        var text = "struct \(name)\n{\n    float4 \(position ?? "dm_position");\n    float \(size ?? "dm_point_size");\n" + clipText
            + "    uint \(layer ?? "dm_layer");\n    uint \(viewport ?? "dm_viewport");\n    uint dm_pad0;\n"
        for location in 0..<32 {
            text += user[location].map { "    \($0.type) \($0.name);\n" } ?? "    float4 dm_u\(location);\n"
        }
        return (text + "};", user, position)
    }
    static func patchStruct(_ name: String, _ list: [Member], _ io: Interface) throws -> (String, [Int: Member]) {
        var user: [Int: Member] = [:]
        for member in list {
            let location = try userLocation(member, io, patch: true)
            guard user[location] == nil else { throw ShaderTransformError.unsupported("duplicate tessellation patch location") }
            user[location] = member
        }
        var text = "struct \(name)\n{\n    float edgeTessellationFactor[4];\n    float insideTessellationFactor[2];\n    float dm_pad[2];\n"
        for location in 0..<31 {
            text += user[location].map { "    \($0.type) \($0.name);\n" } ?? "    float4 dm_p\(location);\n"
        }
        return (text + "};", user)
    }
    static func replace(_ source: String, _ m: ShaderMatch, _ text: String) -> String {
        slice(source, 0, m.start) + text + slice(source, m.end)
    }
    static let captureStruct = "struct dm_geometry_capture { device uint* dst; uint4 info; uint stride; short slot[144]; };\n"
        + "struct dm_tess_input { const device uint* index; uint first; uint pad; };\n"
        + "static inline uint dm_tess_ld(constant dm_geometry_capture& g, const device uint* p, uint k, uint d) { short s = g.slot[k]; return s >= 0 ? p[s] : d; }\n"

    // Hull: SPIRV-Cross's kernel takes its control points through [[stage_in]], which needs a
    // stage input descriptor Metal 4 compute pipelines do not have; it reads them from the
    // vertex capture instead. Its outputs go to the canonical records.
    static func hull(_ input: String, _ io: Interface) throws -> (String, [String: Any]) {
        var src = input
        guard let entry = match(#"(?s)\bkernel void main0\((.*?)\)\n\{\n"#, src) else { throw ShaderTransformError.unsupported("missing hull kernel") }
        var params = entry[1]
        guard let (outMatch, outList) = members("main0_out", src) else { throw ShaderTransformError.unsupported("hull without control point outputs") }
        let (outText, _, _) = try controlPointStruct("main0_out", outList, io, output: true)
        // The input struct: position and user locations only; others are explicit failures.
        var loads: [String] = [], words: [Int] = []
        var inText: String?, inRange: ShaderMatch?, inputs = 0
        if let (m, list) = members("main0_in", src) {
            inRange = m; inputs = list.count
            var lines: [String] = []
            for member in list {
                let base: Int
                if member.type == "float4", member.name == "gl_Position" { base = 0 }
                else { base = 16 + 4 * (try userLocation(member, io, patch: false)) }
                lines.append("    \(member.type) \(member.name);")
                let defaults = (0..<4).map { base + $0 == 3 || base + $0 >= 19 && (base + $0) % 4 == 3 ? "0x3f800000u" : "0u" }
                let parts = (0..<4).map { "dm_tess_ld(dm_gs, dm_p, \(base + $0)u, \(defaults[$0]))" }.joined(separator: ", ")
                loads.append("        in.\(member.name) = as_type<\(member.type)>(uint4(\(parts)));")
                words += (base..<base + 4)
            }
            inText = "struct main0_in\n{\n" + lines.joined(separator: "\n") + "\n};"
        }
        var patchList: [Member] = [], patchRange: ShaderMatch?
        if let (m, list) = members("main0_patchOut", src) { patchRange = m; patchList = list }
        let (patchText, _) = try patchStruct("main0_patchOut", patchList, io)
        // Replace from the end so earlier ranges stay valid.
        var edits: [(ShaderMatch, String)] = [(outMatch, outText)]
        if let inRange, let inText { edits.append((inRange, inText)) }
        if let patchRange { edits.append((patchRange, patchText)) }
        for (m, text) in edits.sorted(by: { $0.0.start > $1.0.start }) { src = replace(src, m, text) }
        if patchRange == nil {
            guard let at = find(src, "kernel void main0(") else { throw ShaderTransformError.unsupported("missing hull kernel") }
            src = slice(src, 0, at) + patchText + "\n\n" + slice(src, at)
        }
        // Parameters.
        let stageIn = "main0_in in [[stage_in]], "
        let hasInput = params.contains(stageIn)
        guard hasInput == (inText != nil) else { throw ShaderTransformError.unsupported("unexpected hull stage input") }
        params = params.replacingOccurrences(of: stageIn, with: "")
        for factors in ["MTLQuadTessellationFactorsHalf", "MTLTriangleTessellationFactorsHalf"] {
            params = params.replacingOccurrences(of: "device \(factors)* spvTessLevel [[buffer(26)]]", with: "device main0_patchOut* spvTessLevel [[buffer(26)]]")
        }
        if !params.contains("spvTessLevel [[buffer(26)]]") { params += ", device main0_patchOut* spvTessLevel [[buffer(26)]]" }
        let patchOut = params.contains("device main0_patchOut* spvPatchOut [[buffer(27)]]")
        params = params.replacingOccurrences(of: ", device main0_patchOut* spvPatchOut [[buffer(27)]]", with: "")
        params += ", constant dm_geometry_capture& dm_gs [[buffer(13)]], constant dm_tess_input& dm_ti [[buffer(14)]]"
        guard params.contains("device main0_out* spvOut [[buffer(28)]]"), params.contains("constant uint* spvIndirectParams [[buffer(29)]]"),
              params.contains("uint gl_InvocationID [[thread_index_in_threadgroup]]"), params.contains("uint gl_PrimitiveID [[threadgroup_position_in_grid]]"),
              !params.contains("[[stage_in]]"), !params.contains("spvPatchOut"), match(#"\[\[buffer\((1[3-4])\)\]\]"#, entry[1]) == nil else {
            throw ShaderTransformError.unsupported("unexpected hull kernel parameters")
        }
        var prologue = patchOut ? "    device main0_patchOut* spvPatchOut = spvTessLevel;\n" : ""
        if hasInput {
            prologue += "    main0_in in = {};\n    if (gl_InvocationID < spvIndirectParams[0])\n    {\n"
                + "        uint dm_c = gl_PrimitiveID * spvIndirectParams[0] + gl_InvocationID;\n"
                + "        uint dm_r = (dm_ti.index ? dm_ti.index[dm_c] : dm_c) + dm_ti.first;\n"
                + "        const device uint* dm_p = dm_gs.dst + ulong(dm_r) * dm_gs.stride;\n"
                + loads.joined(separator: "\n") + "\n    }\n"
        }
        guard let head = match(#"(?s)\bkernel void main0\((.*?)\)\n\{\n"#, src) else { throw ShaderTransformError.unsupported("missing hull kernel") }
        src = slice(src, 0, head.start) + "kernel void main0(" + params + ")\n{\n" + prologue + slice(src, head.end)
        // Levels are kept in full precision, not as the half factors Metal's tessellator takes.
        src = sub(#"(?m)^(\s*spvTessLevel\[[^\]\n]+\]\.(?:edge|inside)TessellationFactor\[\d\] = )half\((.*)\);$"#, src) { "\($0[1])\($0[2]);" }
        guard !src.contains("TessellationFactorsHalf"), !src.contains("[[attribute("), !src.contains("[[stage_in]]"),
              match(#"spvTessLevel\[[^\]\n]+\]\.(?:edge|inside)TessellationFactor\[\d\] = half\("#, src) == nil else {
            throw ShaderTransformError.unsupported("unconverted hull interface")
        }
        src = replacingFirst(src, "using namespace metal;\n", "using namespace metal;\n\n" + captureStruct)
        src += "\nstatic_assert(sizeof(main0_out) == \(controlPointBytes) && sizeof(main0_patchOut) == \(patchBytes), \"tessellation record layout\");\n"
        guard let vertices = io.modes[26]?.first, (1...32).contains(vertices) else {
            throw ShaderTransformError.unsupported("hull without output vertex count")
        }
        let info: [String: Any] = ["role": "control", "output_vertices": Int(vertices), "control_point_bytes": controlPointBytes,
            "patch_bytes": patchBytes, "input_bytes_per_vertex": 16 * inputs, "capture_words": words.sorted(), "buffers": buffers["hs"]!]
        return (src, info)
    }

    static func mode(_ io: Interface) throws -> (domain: String, spacing: String, points: Bool) {
        let domains = [22: "triangles", 24: "quads", 25: "isolines"].filter { io.modes[UInt32($0.key)] != nil }
        let spacings = [1: "equal", 2: "fractional_even", 3: "fractional_odd"].filter { io.modes[UInt32($0.key)] != nil }
        guard domains.count == 1, spacings.count == 1 else { throw ShaderTransformError.unsupported("domain shader without one tessellation mode") }
        return (domains.first!.value, spacings.first!.value, io.modes[10] != nil)
    }

    // Domain: [[patch]] vertex functions need a hardware tessellator. The rewritten function
    // takes [[vertex_id]] into the tessellator's point list, and its patch and control points
    // from the canonical records.
    static func domain(_ input: String, _ io: Interface) throws -> String {
        var src = input
        guard let entry = match(#"\[\[ patch\((triangle|quad), \d+\) \]\] vertex main0_out main0\((.*?)\)\n\{\n"#, src) else {
            throw ShaderTransformError.unsupported("missing domain entry")
        }
        var params = entry[2], prologue = "    float4 dm_pt = dm_tess_points[dm_vid];\n    uint dm_patch = as_type<uint>(dm_pt.w);\n"
        if params.contains("float3 gl_TessCoord [[position_in_patch]]") {
            params = params.replacingOccurrences(of: "float3 gl_TessCoord [[position_in_patch]]", with: "")
            prologue += "    float3 gl_TessCoord = dm_pt.xyz;\n"
        } else if params.contains("float2 gl_TessCoordIn [[position_in_patch]]") {
            params = params.replacingOccurrences(of: "float2 gl_TessCoordIn [[position_in_patch]]", with: "")
            prologue += "    float2 gl_TessCoordIn = dm_pt.xy;\n"
        }
        if params.contains("uint gl_PrimitiveID [[patch_id]]") {
            params = params.replacingOccurrences(of: "uint gl_PrimitiveID [[patch_id]]", with: "")
            prologue += "    uint gl_PrimitiveID = dm_patch;\n"
        }
        let stageIn = params.contains("main0_patchIn patchIn [[stage_in]]")
        params = params.replacingOccurrences(of: "main0_patchIn patchIn [[stage_in]]", with: "")
        params = params.components(separatedBy: ", ").map(trim).filter { !$0.isEmpty }.joined(separator: ", ")
        guard !params.contains("[[position_in_patch]]"), !params.contains("[[patch_id]]"), !params.contains("[[stage_in]]"),
              !params.contains("[[vertex_id]]"), !params.contains("[[instance_id]]"), match(#"\[\[buffer\((1[6-9])\)\]\]"#, params) == nil else {
            throw ShaderTransformError.unsupported("unexpected domain parameters")
        }
        params = "uint dm_vid [[vertex_id]], const device float4* dm_tess_points [[buffer(16)]], const device dm_tess_patch* dm_tess_patches [[buffer(17)]], "
            + "const device main0_in* dm_tess_cp [[buffer(18)]], constant uint& dm_tess_cp_count [[buffer(19)]]" + (params.isEmpty ? "" : ", " + params)
        var edits: [(ShaderMatch, String)] = []
        if stageIn {
            guard let (pm, plist) = members("main0_patchIn", src) else { throw ShaderTransformError.unsupported("domain without patch input") }
            var lines: [String] = []
            prologue += "    const device dm_tess_patch& dm_pr = dm_tess_patches[dm_patch];\n    main0_patchIn patchIn;\n"
            for member in plist {
                switch (member.type, member.name) {
                case ("patch_control_point<main0_in>", "gl_in"):
                    lines.append("    const device main0_in* gl_in;")
                    prologue += "    patchIn.gl_in = dm_tess_cp + ulong(dm_patch) * dm_tess_cp_count;\n"
                case ("float4", "gl_TessLevel"):   // triangles: outer 0-2, inner 0
                    lines.append("    float4 gl_TessLevel;")
                    prologue += "    patchIn.gl_TessLevel = float4(dm_pr.outer[0], dm_pr.outer[1], dm_pr.outer[2], dm_pr.inner[0]);\n"
                case ("float4", "gl_TessLevelOuter"):
                    lines.append("    float4 gl_TessLevelOuter;")
                    prologue += "    patchIn.gl_TessLevelOuter = float4(dm_pr.outer[0], dm_pr.outer[1], dm_pr.outer[2], dm_pr.outer[3]);\n"
                case ("float2", "gl_TessLevelInner"):
                    lines.append("    float2 gl_TessLevelInner;")
                    prologue += "    patchIn.gl_TessLevelInner = float2(dm_pr.inner[0], dm_pr.inner[1]);\n"
                default:
                    let location = try userLocation(member, io, patch: true)
                    lines.append("    \(member.type) \(member.name);")
                    prologue += "    patchIn.\(member.name) = as_type<\(member.type)>(dm_pr.user[\(location)]);\n"
                }
            }
            edits.append((pm, "struct main0_patchIn\n{\n" + lines.joined(separator: "\n") + "\n};"))
        }
        if let (im, ilist) = members("main0_in", src) {
            // Only position and user locations reach a domain shader from the records.
            for member in ilist where !(member.type == "float4" && member.name == "gl_Position") { _ = try userLocation(member, io, patch: false) }
            edits.append((im, try controlPointStruct("main0_in", ilist, io, output: false).0))
        } else if src.contains("patch_control_point<") {
            throw ShaderTransformError.unsupported("domain control points without an input struct")
        } else {
            guard let at = find(src, "[[ patch(") else { throw ShaderTransformError.unsupported("missing domain entry") }
            src = slice(src, 0, at) + (try controlPointStruct("main0_in", [], io, output: false).0) + "\n\n" + slice(src, at)
        }
        for (m, text) in edits.sorted(by: { $0.0.start > $1.0.start }) { src = replace(src, m, text) }
        guard let head = match(#"\[\[ patch\((triangle|quad), \d+\) \]\] vertex main0_out main0\((.*?)\)\n\{\n"#, src) else {
            throw ShaderTransformError.unsupported("missing domain entry")
        }
        src = slice(src, 0, head.start) + "vertex main0_out main0(" + params + ")\n{\n" + prologue + slice(src, head.end)
        src = src.replacingOccurrences(of: "patchIn.gl_in.size()", with: "dm_tess_cp_count")
        guard !src.contains("patch_control_point"), !src.contains("[[attribute("), !src.contains("[[ patch("), !src.contains(".size()") else {
            throw ShaderTransformError.unsupported("unconverted domain interface")
        }
        src += "\nstatic_assert(sizeof(main0_in) == \(controlPointBytes) && sizeof(dm_tess_patch) == \(patchBytes), \"tessellation record layout\");\n"
        return replacingFirst(src, "using namespace metal;\n", "using namespace metal;\n\nstruct dm_tess_patch { float outer[4]; float inner[2]; float pad[2]; float4 user[31]; };\n")
    }

    static func base(_ raw: String) throws -> String {
        try ShaderTransforms.checkAtomics(raw)
        let source = ShaderTransforms.precise(raw)
        try ShaderTransforms.checkTensor(source)
        return try ShaderTransforms.border(ShaderTransforms.wideLoads(ShaderTransforms.pools(source)))
    }

    static func prepare(code: Data, job: [String: Any], cancelled: () -> Bool = { false }) throws -> PreparedShaderSources {
        guard let key = job["key"] as? String, let stage = job["stage"] as? String, ["hs", "ds"].contains(stage) else {
            throw ShaderTransformError.unsupported("invalid tessellation request")
        }
        if cancelled() { throw ShaderTransformError.unsupported("tessellation preparation cancelled") }
        let translated = try NativeShaderTranslation.translate(code: code, job: job)
        let io = try interface(translated.spirv)
        var source: String, tessellation: [String: Any], analysis: String, entries: [String: Any] = [:]
        if stage == "hs" {
            let raw = try NativeShaderTranslation.cross(translated.spirv)
            (source, tessellation) = try hull(try base(raw), io)
            analysis = source
        } else {
            let (domainKind, spacing, points) = try mode(io)
            let spirv = try ShaderSpirv.singleton(domainKind == "isolines" ? try isolinesAsQuads(translated.spirv) : translated.spirv)
            let vertex = try ShaderTransforms.initializeVertex(domain(try base(try NativeShaderTranslation.cross(spirv)), io))
            (analysis, source) = try ShaderTransforms.depth(vertex)
            entries = ["entry_z": "main0_z"]
            tessellation = ["role": "evaluation", "domain": domainKind, "spacing": spacing, "point_mode": points,
                            "winding": job["tess_cw"] as? Bool == true ? "cw" : "ccw", "control_point_bytes": controlPointBytes,
                            "patch_bytes": patchBytes, "buffers": buffers["ds"]!]
        }
        let reads = ShaderReflection.driverReads(analysis)
        var notes = translated.resources["notes"] as? [String] ?? []
        if job["no_facts"] as? Bool == true { notes.append("not in the answer table, queries use defaults") }
        var reflection: [String: Any] = ["key": key + "_b", "route": "B", "chain": "26dc2fa9477c", "dm_ctl": key,
            "source": job["source"] ?? "native", "pc_paired": job["pc_paired"] ?? "", "stage": stage, "entry": "main0",
            "msl_entry_kind": stage == "hs" ? "kernel" : "vertex", "threadgroup_size": NSNull(), "c1": job["c1"] ?? "", "notes": notes,
            "drv_reads": reads, "drv_unfillable": ShaderReflection.unfillable(reads, stage: stage),
            "bindings": try ShaderReflection.bindings(stage: stage, source: analysis, resources: translated.resources),
            "vertex_inputs": [Any](), "raster": [String: Any](), "tessellation": tessellation]
        reflection.merge(entries) { _, new in new }
        return PreparedShaderSources(sources: ["b": source], reflection: reflection)
    }
}
