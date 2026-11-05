import Foundation

// Adapted from pinned NX2Apple dmsh_geometry.py and geometry_finish (MIT).
// The host ABI is vertex capture, compute emission, then ordered raster replay.
// Logical resource stage remains geometry even though Metal runs a kernel.
enum ShaderGeometry {
    static func outputLayout(_ code: Data, topology: Int) throws -> [String: Int] {
        guard code.count >= 20, (0..<5).contains(topology) else {
            throw ShaderTransformError.unsupported("invalid geometry header or topology")
        }
        let w0 = try ShaderRecord.word(code, 0), w2 = try ShaderRecord.word(code, 8)
        let w3 = try ShaderRecord.word(code, 12), w4 = try ShaderRecord.word(code, 16)
        var vertices = Int(w4 & 4095), output = Int((w3 >> 24) & 15)
        let invocations = Int(w2 >> 24)
        if w0 & (1 << 24) != 0 {
            vertices = [1, 2, 2, 3, 3][topology]
            output = [1, 6, 6, 7, 7][topology]
        }
        guard [1, 6, 7].contains(output), vertices > 0, invocations > 0 else {
            throw ShaderTransformError.unsupported("invalid geometry output declaration")
        }
        return ["vertices": vertices, "invocations": invocations, "output": output, "index_stride": vertices * 2]
    }

    static func flattenSets(_ data: Data) throws -> Data {
        var words = try ShaderSpirv.words(data), sets: [UInt32: (UInt32, Int)] = [:], bindings: [UInt32: Int] = [:]
        var i = 5
        while i < words.count {
            let size = Int(words[i] >> 16), op = words[i] & 65535
            guard size > 0, size <= words.count - i else { throw ShaderTransformError.unsupported("invalid geometry SPIR-V instruction") }
            if op == 71, size == 4 {
                if words[i + 2] == 34 { sets[words[i + 1]] = (words[i + 3], i + 3) }
                if words[i + 2] == 33 { bindings[words[i + 1]] = i + 3 }
            }
            i += size
        }
        for (id, (set, at)) in sets where set != 0 {
            guard [1, 2].contains(set), let binding = bindings[id], words[binding] <= UInt32.max - 11000 else {
                throw ShaderTransformError.unsupported("unknown geometry auxiliary descriptor set")
            }
            words[binding] += set == 1 ? 10000 : 11000
            words[at] = 0
        }
        return words.withUnsafeBytes { Data($0) }
    }

    static func topologyPointer(_ input: String) throws -> String {
        guard matches(#"texture_buffer<float> trb_data"#, input).count == 1 else {
            throw ShaderTransformError.unsupported("missing geometry topology buffer")
        }
        var source = input.replacingOccurrences(of: "texture_buffer<float> trb_data", with: "device const uint* trb_data")
        let prefix = "spvDescriptorSet0.trb_data.read("
        while let begin = find(source, prefix) {
            let at = begin + prefix.utf16.count
            guard let end = close(source, at, 40, 41), slice(source, end).hasPrefix(".x") else {
                throw ShaderTransformError.unsupported("unexpected geometry topology read")
            }
            source = slice(source, 0, begin) + "as_type<float>(spvDescriptorSet0.trb_data[" + slice(source, at, end - 1) + "])" + slice(source, end + 2)
        }
        guard !source.contains("trb_data.read") else { throw ShaderTransformError.unsupported("unconverted geometry topology read") }
        return source
    }

    static func captureInput(_ input: String) throws -> String {
        if !input.contains("vertex_output") { return input }
        let pattern = #"\bdevice (\w+)\* vertex_output\b"#
        guard matches(pattern, input).count == 1 else { throw ShaderTransformError.unsupported("unknown geometry capture input") }
        return sub(pattern, input) { "device volatile \($0[1])* vertex_output" }
    }

    static func reader(_ input: String) throws -> String {
        let builtin = "gl_BuiltIn_5253"
        if !input.contains(builtin) { return input }
        let pattern = #"\bspvArrayCopyFromStackToStack\(\s*gl_BuiltIn_5253\s*,\s*as_type<int>\((\w+)\)\.elements\s*\);"#
        let count = matches(pattern, input).count
        let lowered = sub(pattern, input) { "dm_gs_viewport_mask = as_type<uint>(\($0[1]));" }
        guard count > 0, !lowered.contains(builtin), !input.contains("dm_gs_viewport_mask"),
              let output = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, lowered),
              let position = match(#"\bfloat4 (\w+) \[\[position(?:, invariant)?\]\];"#, output[1]),
              let entry = match(#"\bvertex\s+main0_out\s+main0\s*\([^{}]*\)\s*\{"#, lowered),
              let end = close(lowered, entry.end, 123, 125) else {
            throw ShaderTransformError.unsupported("unsupported geometry viewport mask reader")
        }
        var body = slice(lowered, entry.end, end - 1)
        guard body.components(separatedBy: "dm_gs_viewport_mask").count - 1 == count, body.contains("return out;") else {
            throw ShaderTransformError.unsupported("geometry viewport mask outside reader")
        }
        body = body.replacingOccurrences(of: "return out;", with: "{ if ((dm_gs_viewport_mask & 1u) == 0u) out.\(position[1]) = float4(2.0, 0.0, 0.0, 1.0); return out; }")
        return slice(lowered, 0, entry.end) + "\n    uint dm_gs_viewport_mask = 1u;" + body + slice(lowered, end - 1)
    }

    static func prepare(code: Data, job: [String: Any], cancelled: () -> Bool = { false }) throws -> PreparedShaderSources {
        guard let key = job["key"] as? String, job["stage"] as? String == "gs" else {
            throw ShaderTransformError.unsupported("invalid geometry request")
        }
        _ = try outputLayout(code, topology: 0)
        var sources: [String: String] = [:], reflections: [String: [String: Any]] = [:]
        for topology in 0..<5 {
            if cancelled() { throw ShaderTransformError.unsupported("geometry preparation cancelled") }
            var query = job; query["geometry_topology"] = topology
            let translated = try NativeShaderTranslation.translate(code: code, job: query)
            let raw = try NativeShaderTranslation.cross(flattenSets(translated.spirv))
            try ShaderTransforms.checkAtomics(raw)
            let plain = try topologyPointer(ShaderTransforms.pools(ShaderTransforms.precise(raw)))
            let source = try captureInput(plain)
            let layout = try outputLayout(code, topology: topology)
            guard let stride = translated.resources["geometry_stride"] as? Int, stride > 0, stride <= 144 else {
                throw ShaderTransformError.unsupported("invalid geometry output stride")
            }
            var geometry = layout; geometry["stride"] = stride
            let suffix = "bg\(topology)"
            var reflection: [String: Any] = ["key": key + "_" + suffix, "route": "B", "chain": "26dc2fa9477c", "dm_ctl": key,
                "source": job["source"] ?? "native", "pc_paired": job["pc_paired"] ?? "", "stage": "gs", "entry": "main0",
                "msl_entry_kind": "kernel", "threadgroup_size": [32, 1, 1], "geometry": geometry,
                "c1": job["c1"] ?? "", "notes": translated.resources["notes"] ?? [], "drv_reads": ShaderReflection.driverReads(source),
                "drv_unfillable": [String](), "bindings": try ShaderReflection.bindings(stage: "gs", source: source, resources: translated.resources),
                "vertex_inputs": [Any](), "raster": [String: Any]()]
            sources[suffix] = source; reflections[suffix] = reflection
            if topology == 0 {
                reflection["key"] = key + "_b"; sources["b"] = source; reflections["b"] = reflection
                let rawReader = try NativeShaderTranslation.cross(flattenSets(translated.reader))
                try ShaderTransforms.checkAtomics(rawReader)
                let vertex = try ShaderTransforms.layer(ShaderTransforms.initializeVertex(reader(ShaderTransforms.pools(ShaderTransforms.precise(rawReader)))))
                let (analysis, full) = try ShaderTransforms.depth(vertex)
                var readerSource = full
                var readerReflection: [String: Any] = ["key": key + "_bgv", "route": "B", "chain": "26dc2fa9477c", "dm_ctl": key,
                    "stage": "vs", "entry": "main0", "entry_z": "main0_z", "msl_entry_kind": "vertex",
                    "geometry_reader": true, "geometry_output": layout["output"]!, "c1": "", "notes": [String](),
                    "drv_reads": [String: Any](), "drv_unfillable": [String](),
                    "bindings": try ShaderReflection.bindings(stage: "vs", source: analysis, resources: ["sbufs": [[String: Any]]()]),
                    "vertex_inputs": [Any](), "raster": [String: Any]()]
                if let tf = job["tf"] as? [Any] {
                    readerSource += try ShaderTransforms.feedback(analysis, layout: tf, geometry: true)
                    readerReflection["entry_tf"] = "main0_tf"
                    readerReflection["tf_strides"] = tf.map { ($0 as? [String: Any])?["stride"] as? Int ?? 0 }
                }
                sources["bgv"] = readerSource; reflections["bgv"] = readerReflection
            }
        }
        return PreparedShaderSources(sources: sources, reflection: reflections["b"]!, variantReflections: reflections)
    }
}
