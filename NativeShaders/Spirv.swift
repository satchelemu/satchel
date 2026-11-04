import Foundation

enum ShaderSpirv {
    static func words(_ data: Data) throws -> [UInt32] {
        guard data.count >= 20, data.count % 4 == 0 else { throw ShaderTransformError.unsupported("invalid SPIR-V framing") }
        let words = data.withUnsafeBytes { p in stride(from: 0, to: data.count, by: 4).map { UInt32(littleEndian: p.loadUnaligned(fromByteOffset: $0, as: UInt32.self)) } }
        guard words[0] == 0x07230203 else { throw ShaderTransformError.unsupported("invalid SPIR-V magic") }
        return words
    }
    static func singleton(_ data: Data) throws -> Data {
        let w = try words(data)
        var header = Array(w.prefix(5)), instructions: [[UInt32]] = [], i = 5
        while i < w.count {
            let n = Int(w[i] >> 16)
            guard n > 0 && n <= w.count - i else { throw ShaderTransformError.unsupported("invalid SPIR-V instruction") }
            instructions.append(Array(w[i..<i+n])); i += n
        }
        let operations: [UInt32: String] = [333:"true",334:"pred",335:"pred",336:"true",337:"value",338:"value",339:"ballot",345:"value",346:"value",347:"value",348:"value"]
        let builtins: [UInt32: String] = [36:"one",41:"zero",4416:"one4",4417:"one4",4418:"zero4",4419:"one4",4420:"zero4"]
        func op(_ x: [UInt32]) -> UInt32 { x[0] & 65535 }
        guard instructions.contains(where: { operations[op($0)] != nil || (op($0) == 71 && $0.count > 3 && $0[2] == 11 && builtins[$0[3]] != nil) }) else { return data }
        if instructions.contains(where: { (340...366).contains(op($0)) && operations[op($0)] == nil }) { return data }
        var variables: [UInt32: String] = [:], pointers: [UInt32: (String, UInt32)] = [:]
        for x in instructions where op(x) == 71 && x.count > 3 && x[2] == 11 { if let b = builtins[x[3]] { variables[x[1]] = b } }
        for x in instructions where [65,66].contains(op(x)) && x.count > 3 {
            if let b = variables[x[3]] { guard x.count == 5 && b.hasSuffix("4") else { return data }; pointers[x[2]] = (b, x[4]) }
        }
        if instructions.contains(where: { [65,66].contains(op($0)) && $0.count > 3 && pointers[$0[3]] != nil }) { return data }
        guard let uint = instructions.first(where: { op($0) == 21 && $0.count >= 4 && $0[2] == 32 && $0[3] == 0 })?[1],
              let bool = instructions.first(where: { op($0) == 20 && $0.count >= 2 })?[1],
              let vector = instructions.first(where: { op($0) == 23 && $0.count >= 4 && $0[2] == uint && $0[3] == 4 })?[1] else { return data }
        var bound = header[3], constants: [String: UInt32] = [:]
        guard bound <= UInt32.max - 5 else { throw ShaderTransformError.unsupported("SPIR-V id overflow") }
        for name in ["zero", "one", "zero4", "one4", "true"] { constants[name] = bound; bound += 1 }
        let z = constants["zero"]!, one = constants["one"]!, z4 = constants["zero4"]!, one4 = constants["one4"]!, t = constants["true"]!
        let declarations: [[UInt32]] = [[43 | 4 << 16,uint,z,0],[43 | 4 << 16,uint,one,1],[44 | 7 << 16,vector,z4,z,z,z,z],[44 | 7 << 16,vector,one4,one,z,z,z],[41 | 3 << 16,bool,t]]
        var out: [[UInt32]] = [], placed = false
        for original in instructions {
            var x = original; let o = op(x)
            if o == 17 && x.count > 1 && [61,62,63,64,65,66,67,68,4423,4431].contains(x[1]) { continue }
            if o == 10 {
                let bytes = x.dropFirst().flatMap { n in [UInt8(truncatingIfNeeded:n),UInt8(truncatingIfNeeded:n>>8),UInt8(truncatingIfNeeded:n>>16),UInt8(truncatingIfNeeded:n>>24)] }
                let text = String(decoding: bytes, as: UTF8.self)
                if text.contains("ballot") || text.contains("subgroup") { continue }
            }
            if [71,5].contains(o) && x.count > 1 && variables[x[1]] != nil { continue }
            if o == 59 && x.count > 2 && variables[x[2]] != nil { continue }
            if o == 15 {
                var k = 3
                while k < x.count { k += 1; if (0..<4).contains(where: { (x[k-1] >> (8 * $0)) & 255 == 0 }) { break } }
                x = Array(x.prefix(k)) + x.dropFirst(k).filter { variables[$0] == nil }; x[0] = UInt32(x.count) << 16 | o
            }
            if o == 54 && !placed { out += declarations; placed = true }
            if o == 61 && x.count > 3, let variable = variables[x[3]] { out.append([83 | 4 << 16,x[1],x[2],constants[variable]!]); continue }
            if [65,66].contains(o) && x.count > 2 && pointers[x[2]] != nil { continue }
            if o == 61 && x.count > 3, let pointer = pointers[x[3]] { out.append([77 | 5 << 16,x[1],x[2],constants[pointer.0]!,pointer.1]); continue }
            if let kind = operations[o] {
                guard x.count > (kind == "true" ? 2 : 4) else { throw ShaderTransformError.unsupported("invalid subgroup operation") }
                out.append(kind == "ballot" ? [169 | 6 << 16,x[1],x[2],x[4],one4,z4] : [83 | 4 << 16,x[1],x[2],kind == "true" ? t : x[4]])
                continue
            }
            out.append(x)
        }
        guard placed else { return data }
        header[3] = bound
        let flat = (header + out.flatMap { $0 }).map { $0.littleEndian }
        return flat.withUnsafeBytes { Data($0) }
    }
}
