import Foundation

struct PreparedShaderSources {
    let sources: [String: String]
    let reflection: [String: Any]
    var variantReflections: [String: [String: Any]] = [:]
}

enum ShaderSourcePipeline {
    static func prepare(source input: String, resources: [String: Any], job: [String: Any],
                        wanted: Set<Int>, mappingOnly: Bool, dual: Bool) throws -> PreparedShaderSources {
        guard let key = job["key"] as? String, let stage = job["stage"] as? String else {
            throw ShaderTransformError.unsupported("invalid source job")
        }
        var source = input
        try ShaderTransforms.checkAtomics(source)
        source = ShaderTransforms.precise(source)
        try ShaderTransforms.checkTensor(source)
        source = try ShaderTransforms.border(ShaderTransforms.wideLoads(ShaderTransforms.pools(source)))
        var firstStores: [(String, Int, Int)] = [], spill = 0
        if stage == "cs" {
            (source, firstStores) = ShaderTransforms.firstStore(source, local: job["local"] as? [Int])
            (source, spill) = try ShaderTransforms.spillShared(source)
        }
        var padded: [Int] = [], layer: [String: Any]?, discard = false
        var full = source
        if stage == "vs" {
            source = ShaderTransforms.baseInstance(try ShaderTransforms.layer(ShaderTransforms.initializeVertex(source)))
            (source, padded) = ShaderTransforms.pad(source, wanted: wanted)
            let capture = try ShaderTransforms.capture(source)
            (source, full) = try ShaderTransforms.depth(source)
            full += capture
            discard = ShaderTransforms.vertexWrites(source, bindings: try ShaderReflection.bindings(stage: stage, source: source, resources: resources))
            if discard { full += try ShaderTransforms.discard(source) }
            if let tf = job["tf"] as? [Any] { full += try ShaderTransforms.feedback(source, layout: tf) }
            (full, layer) = try ShaderTransforms.layerEntries(full)
        }
        let (uint, mask) = stage == "ps" ? try ShaderTransforms.uintFragment(source) : (nil, 0)
        let dualEntry = stage == "ps" && dual ? ShaderTransforms.dualFragment(source) : nil
        if uint != nil || dualEntry != nil { full = (uint ?? source) + (dualEntry ?? "") }
        let entry = match(#"(vertex|fragment|kernel)\s+\S+\s+(\w+)\s*\("#, source)
        let reads = ShaderReflection.driverReads(source)
        let bindings = try ShaderReflection.bindings(stage: stage, source: source, resources: resources)
        var notes = resources["notes"] as? [String] ?? []
        if job["no_facts"] as? Bool == true { notes.append("not in the answer table, queries use defaults") }
        var reflection: [String: Any] = ["key": key + "_b", "route": "B", "chain": "26dc2fa9477c", "dm_ctl": key,
            "source": job["source"] ?? "native", "pc_paired": job["pc_paired"] ?? "", "stage": stage,
            "entry": entry?[2] ?? "main0", "msl_entry_kind": entry.map { $0[1] as Any } ?? NSNull(),
            "threadgroup_size": job["local"] ?? NSNull(), "c1": job["c1"] ?? "", "notes": notes,
            "drv_reads": reads, "drv_unfillable": ShaderReflection.unfillable(reads, stage: stage), "bindings": bindings,
            "vertex_inputs": ShaderTransforms.attributes(source).map { ["location": Int($0[3])!, "msl_type": $0[1]] as [String: Any] },
            "raster": ShaderReflection.raster(stage: stage, source: source), "vs_pad": padded]
        if stage == "vs" { reflection["entry_z"] = "main0_z"; reflection["entry_geometry_capture"] = "main0_gs" }
        if discard { reflection["entry_discard"] = "main0_discard" }
        if let tf = job["tf"] as? [Any] {
            reflection["entry_tf"] = "main0_tf"
            reflection["tf_strides"] = tf.map { ($0 as? [String: Any])?["stride"] as? Int ?? 0 }
        }
        if let layer { reflection["entry_layer"] = "main0_l"; reflection["entry_layer_z"] = "main0_lz"; reflection["layer_from"] = layer }
        if uint != nil { reflection["entry_u"] = "main0_u"; if mask != 0 { reflection["entry_m"] = "main0_m"; reflection["uint_mask_m"] = mask } }
        if dualEntry != nil { reflection["entry_d"] = "main0_d" }
        if !firstStores.isEmpty {
            reflection["first_store"] = try firstStores.map { store -> [String: Any] in
                guard let binding = bindings.first(where: { $0["msl_name"] as? String == store.0 }), let reg = binding["reg"] else {
                    throw ShaderTransformError.unsupported("first store without a storage binding")
                }
                return ["reg": reg, "word": store.1, "value": store.2]
            }
        }
        if spill != 0 { reflection["tg_spill"] = spill }
        var sources = ["b": full]
        if stage == "vs" {
            if let v = ShaderVariants.vin(full) { sources["bv"] = v.source; reflection["vin"] = ["entry": "main0", "entry_z": "main0_z", "shadow": v.info] }
            if let v = ShaderVariants.pull(full) { sources["bp"] = v.source; reflection["pull"] = v.info }
        }
        if stage == "ps" {
            // One side library for the alpha test and logic operation entry points (<entry>_a, <entry>_l), as the pinned host loads it.
            if let v = ShaderVariants.side(full), let info = v.info as? [String: [String]] {
                sources["bx"] = v.source
                if let names = info["alpha"], !names.isEmpty { reflection["alpha"] = ["entries": names] }
                if let names = info["logic"], !names.isEmpty { reflection["logic"] = ["entries": names] }
            }
            // The retained Mac bf corpus was generated from the final base text.
            // Truncating its integer entry preserves one final newline, as in
            // NativeShaders/package.py's proven source recovery.
            if let v = ShaderVariants.fetch(full, bindings: bindings, mappingOnly: mappingOnly) { sources["bf"] = v.source; reflection["fetch"] = v.info }
        }
        // Compiler provenance is assigned only after actual runtime compilation.
        // The semantic chain identifies this parity candidate, not a passed gate.
        return PreparedShaderSources(sources: sources, reflection: reflection)
    }
}
