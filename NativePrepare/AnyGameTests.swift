import Foundation

@main struct AnyGameTests {
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        var assertions = 0
        func check(_ condition: Bool, _ label: String) throws {
            guard condition else { throw PrepareFailure(message: "Test failed: " + label) }; assertions += 1
        }
        func reject(_ label: String, _ operation: () throws -> Void) throws {
            do { try operation() } catch { assertions += 1; return }
            throw PrepareFailure(message: "Unexpected acceptance: " + label)
        }
        let recipe = NativePreparationRecipe(schema: 1, titleID: "0100000000000001", version: "1.0", contentVersion: 0,
            name: "Synthetic game", modules: ["main", "sdk"], buildIDs: ["main": String(repeating: "1", count: 32), "sdk": String(repeating: "2", count: 32)],
            sdkVersions: ["19_3_16"], systemResource: 0x1000000, usesHeap: false, largeBSS: 0, usedMemory: 0x4000000, nros: [], reviewedVersion: nil)
        let profile = try recipe.profile(profiles: [])
        try check(profile.shaderInputs == "runtime-capture-v1", "experimental capture identity")
        let runtime = String(decoding: try recipe.runtime(profiles: []), as: UTF8.self)
        try check(runtime.contains("mem_alias\t0x118000000\n"), "documented virtual alias default")
        try check(runtime.contains("display_version\t" + recipe.buildIDs["main"]! + "\t1.0\n"), "display version keyed by main build")
        try check(runtime.contains("render_cond\t0\n") && runtime.contains("device_api\t0.0\n") && runtime.contains("game_env\t\n"), "no Zelda default leakage")
        var manyModules = recipe
        manyModules.nros = (0..<1432).map { .init(name: "nro\($0)", sha256: String(format: "%064x", $0), bytes: 4096, buildID: String(repeating: "a", count: 32)) }
        try check(try manyModules.profile(profiles: []).nroBuildIDs?.count == 1432, "dynamic loader inventory beyond 128 modules")
        let encoded = try JSONEncoder().encode(recipe)
        for (key, value) in [("modules", ["main", "../sdk"] as Any), ("systemResource", UInt64.max), ("name", "injected\ngame_env\tBAD=1"), ("usedMemory", 0), ("titleID", "../x"), ("schema", 2), ("reviewedVersion", "1.4.3")] {
            var object = try JSONSerialization.jsonObject(with: encoded) as! [String: Any]; object[key] = value
            try reject("recipe " + key) {
                let changed = try JSONDecoder().decode(NativePreparationRecipe.self, from: JSONSerialization.data(withJSONObject: object))
                _ = try changed.profile(profiles: [])
            }
        }
        for reviewed in NativeGameProfile.all {
            let nros = (reviewed.nroBuildIDs ?? [:]).keys.sorted().enumerated().map { i, name in
                NativePreparationRecipe.NRO(name: name, sha256: String(repeating: String(i + 1), count: 64), bytes: 4096, buildID: reviewed.nroBuildIDs![name]!)
            }
            let r = NativePreparationRecipe(schema: 1, titleID: reviewed.titleID, version: reviewed.version, contentVersion: 0, name: reviewed.name,
                modules: reviewed.modules, buildIDs: reviewed.buildIDs, sdkVersions: [], systemResource: 0, usesHeap: false,
                largeBSS: 0, usedMemory: 0x4000000, nros: nros, reviewedVersion: reviewed.version)
            try check(try NativePreparationRecipe.hash(r.runtime()) == reviewed.profileSHA256, "reviewed runtime digest")
        }
        var nro = Data(repeating: 0, count: 0x3000)
        func word(_ at: Int, _ value: UInt32) { for i in 0..<4 { nro[at + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) } }
        nro.replaceSubrange(0x10..<0x14, with: Data("NRO0".utf8)); word(0x18, 0x3000)
        for i in 0..<3 { word(0x20 + i * 8, UInt32(i * 0x1000)); word(0x24 + i * 8, 0x1000) }
        try check(try PrepareDiscovery.decodeNRO(nro, name: "nro0").segments.count == 3, "NRO segment decode")
        word(0x28, 0); try reject("overlapping NRO segments") { _ = try PrepareDiscovery.decodeNRO(nro, name: "nro0") }
        word(0x28, 0x1000); word(0x1c, 2); try reject("compressed NRO") { _ = try PrepareDiscovery.decodeNRO(nro, name: "nro0") }
        for (i, name) in ["main", "nro0", "nro1"].enumerated() {
            try Data(repeating: UInt8(i + 1), count: 0x8100).write(to: root.appendingPathComponent(name + ".patched.bin"))
            try Data("# TEXT_END 0x4000\n# PATCH_OFF 0x8000\n# KEY_PAGE_OFF 0x4000\n# KEY_PAGE_SIZE 0x4000\n".utf8).write(to: root.appendingPathComponent(name + ".fixups.tsv"))
        }
        let layout = try PrepareMachO.write(modules: ["main"], nros: ["nro0", "nro1"], from: root, to: root.appendingPathComponent("nros.dylib"), token: PrepareCancellation())
        try check(layout.count == 3, "startup and dynamic layouts retained")
        let image = try Data(contentsOf: root.appendingPathComponent("nros.dylib"))
        try check(image.range(of: Data("__dm_nro_tab".utf8)) != nil, "packed NRO table")
        try check(image.range(of: Data("__DMnro0W".utf8)) == nil, "dynamic modules use packed sections")
        print("PASS: \(assertions) native recipe, override, NRO and container assertions")
    }
}
