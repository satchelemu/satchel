import Foundation
import CoreFoundation

// Preserve the pinned Python writer's whitespace and field order for the
// deterministic semantic contract. Runtime compiler provenance stays separate.
enum ShaderReflectionJSON {
    static func encode(_ reflection: [String: Any]) throws -> Data {
        Data(try value(reflection, path: [], depth: 0).utf8)
    }
    static func quote(_ text: String) -> String {
        var out = "\""
        for scalar in text.unicodeScalars {
            switch scalar.value {
            case 34: out += "\\\""
            case 92: out += "\\\\"
            case 8: out += "\\b"
            case 12: out += "\\f"
            case 10: out += "\\n"
            case 13: out += "\\r"
            case 9: out += "\\t"
            case 0..<32: out += String(format: "\\u%04x", scalar.value)
            default: out.unicodeScalars.append(scalar)
            }
        }
        return out + "\""
    }
    static func order(_ path: [String]) -> [String] {
        if path.isEmpty { return ["key","route","chain","dm_ctl","source","pc_paired","stage","entry","msl_entry_kind","entry_z","entry_geometry_capture","entry_discard","entry_tf","tf_strides","entry_layer","entry_layer_z","layer_from","entry_u","entry_m","uint_mask_m","entry_d","first_store","tg_spill","threadgroup_size","c1","notes","drv_reads","drv_unfillable","bindings","vertex_inputs","raster","vs_pad","vin","pull","alpha","logic","fetch","chain_metal","chain_compiler"] }
        if path.first == "bindings" { return ["msl_name","msl_type","arg_id","border_bit","kind","index","reg","bank","off","soff","sreg","elem","atomic","fetch1d","fetch1d_size","rw"] }
        if path.first == "vertex_inputs" { return ["location","msl_type"] }
        if path.first == "raster" {
            if path.contains("uv_affine") { return ["attr","sx","ox","sy","oy"] }
            return ["discard","sample_mask","primitive_id","colors","pos_attr","pos_w","uv_from_attr","uv_affine","screen"]
        }
        if path.first == "drv_reads" { return ["0","2","vec4_offsets","dynamic"] }
        if path.first == "vin" { return ["entry","entry_z","shadow","location","float","sint","uint"] }
        if path.first == "pull" { return ["entry","entry_z","slot","inputs"] }
        if path.first == "fetch" { return ["calls","mixed_regs","outputs","fc","tex_reg","varying_locn","proj","frag_pos","color","type"] }
        if path.first == "layer_from" { return ["loc","comp"] }
        if path.first == "first_store" { return ["reg","word","value"] }
        return []
    }
    static func value(_ object: Any, path: [String], depth: Int) throws -> String {
        if object is NSNull { return "null" }
        if let string = object as? String { return quote(string) }
        if let number = object as? NSNumber {
            if CFGetTypeID(number) == CFBooleanGetTypeID() { return number.boolValue ? "true" : "false" }
            return number.stringValue
        }
        let indent = String(repeating: " ", count: depth), child = indent + " "
        if let array = object as? [Any] {
            if array.isEmpty { return "[]" }
            return "[\n" + (try array.map { child + (try value($0, path: path, depth: depth + 1)) }).joined(separator: ",\n") + "\n" + indent + "]"
        }
        if let dictionary = object as? [String: Any] {
            if dictionary.isEmpty { return "{}" }
            var sequence = order(path)
            if path.isEmpty, dictionary["geometry"] != nil {
                sequence = ["key","route","chain","dm_ctl","source","pc_paired","stage","entry","msl_entry_kind","threadgroup_size","geometry","c1","notes","drv_reads","drv_unfillable","bindings","vertex_inputs","raster"]
            } else if path.isEmpty, dictionary["geometry_reader"] as? Bool == true {
                sequence = ["key","route","chain","dm_ctl","stage","entry","entry_z","msl_entry_kind","geometry_reader","geometry_output","c1","notes","drv_reads","drv_unfillable","bindings","vertex_inputs","raster","entry_tf","tf_strides"]
            } else if path == ["geometry"] {
                sequence = ["vertices","invocations","output","index_stride","stride","lean","capture_reads"]
            }
            let ranks = Dictionary(uniqueKeysWithValues: sequence.enumerated().map { ($0.element, $0.offset) })
            let keys = dictionary.keys.sorted { a,b in
                let ar = ranks[a] ?? 999, br = ranks[b] ?? 999
                if ar != br { return ar < br }
                if let ai = Int(a), let bi = Int(b) { return ai < bi }
                return a < b
            }
            return "{\n" + (try keys.map { key in child + quote(key) + ": " + (try value(dictionary[key]!, path: path + [key], depth: depth + 1)) }).joined(separator: ",\n") + "\n" + indent + "}"
        }
        throw ShaderTransformError.unsupported("unsupported reflection JSON value")
    }
}
