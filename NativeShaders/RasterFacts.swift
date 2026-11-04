import Foundation

extension ShaderReflection {
    static func raster(stage: String, source: String) -> [String: Any] {
        let output = match(#"(?s)struct main0_out\s*\{(.*?)\};"#, source)?[1] ?? ""
        if stage == "ps" {
            return ["discard": source.contains("discard_fragment") ? 1 : 0,
                    "sample_mask": output.contains("sample_mask") ? 1 : 0,
                    "colors": Set(matches(#"\[\[color\((\d+)\)\]\]"#, output).map { Int($0[1])! }).sorted()]
        }
        if stage != "vs" { return [:] }
        guard let body = match(#"(?s)\bvertex\s+\w+\s+\w+\s*\(.*?\)\s*\{(.*)\}\s*$"#, source),
              match(#"\b(if|for|while|switch|do)\b|clip_distance|ClipDistance"#, body[1]) == nil else { return ["pos_attr": -1] }
        let b = body[1]
        var attr: [String: Int] = [:], variables: [String: [String]] = [:], last: [String: String] = [:]
        for m in matches(#"(\w+)\s+(\w+)\s*\[\[attribute\((\d+)\)\]\]"#, source) { attr[m[2]] = Int(m[3])! }
        for m in matches(#"float\s+(_\w+)\s*=\s*in\.(\w+)\.([xyzw]);"#, b) { variables[m[1]] = [m[2], m[3]] }
        for m in matches(#"out\.gl_Position\.([xyzw])\s*=\s*([^;]+);"#, b) { last[m[1]] = trim(m[2]) }
        guard let x = last["x"].flatMap({ variables[$0] }), let location = attr[x[0]],
              ["x", "y", "z"].allSatisfy({ last[$0].flatMap { variables[$0] } == [x[0], $0] }) else { return ["pos_attr": -1] }
        let w: String
        if last["w"] == "1.0" { w = "one" }
        else if last["w"].flatMap({ variables[$0] }) == [x[0], "w"] { w = "attr" }
        else { return ["pos_attr": -1] }
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
        return ["pos_attr": location, "pos_w": w, "uv_from_attr": uv, "uv_affine": affine]
    }
}
