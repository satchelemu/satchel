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
        // Baseline main 01eb22d, including the complete legacy runtime text, plus the fps_max key the
        // f8a5618d engine pin (39476a8) added to every starter profile; nothing else changed.
        try check(NativePreparationRecipe.hash(Data(runtime.utf8)) == "70b9f7e1197a3636d21691abd4274549f4e543b76fd2a83b1b1e1e5beca1f13d", "legacy starter runtime digest")
        try check(runtime.contains("mem_alias\t0x118000000\n"), "documented virtual alias default")
        try check(runtime.contains("display_version\t" + recipe.buildIDs["main"]! + "\t1.0\n"), "display version keyed by main build")
        try check(runtime.contains("render_cond\t0\n") && runtime.contains("device_api\t0.0\n") && runtime.contains("game_env\t\n"), "no Zelda default leakage")
        // A texture-write observation turns on both upload switches and nothing else; without one the
        // starter text above is unchanged (an earlier observation decodes with uploads unset).
        var uploads = recipe; uploads.graphicsObservation = .init(renderConditions: false, textureUploads: true)
        let learned = String(decoding: try uploads.runtime(profiles: []), as: UTF8.self)
        let differing = zip(runtime.split(separator: "\n"), learned.split(separator: "\n")).filter { $0 != $1 }.map { String($1) }
        try check(differing == ["tex_dirty_upload\t1", "tex_flush_upload\t1"], "texture uploads change exactly the two switches")
        let earlier = try JSONDecoder().decode(NativePreparationRecipe.GraphicsObservation.self, from: Data(#"{"renderConditions":true}"#.utf8))
        var conditions = recipe; conditions.graphicsObservation = earlier
        try check(earlier.textureUploads == nil && String(decoding: try conditions.runtime(profiles: []), as: UTF8.self).contains("tex_flush_upload\t0\n"), "an earlier observation keeps the uploads off")
        func memory(_ recipe: NativePreparationRecipe) throws -> [String: UInt64] {
            let text = String(decoding: try recipe.runtime(profiles: []), as: UTF8.self)
            return Dictionary(uniqueKeysWithValues: text.split(separator: "\n").compactMap { row in
                let columns = row.split(separator: "\t")
                guard columns.count == 2, columns[1].hasPrefix("0x"), let value = UInt64(columns[1].dropFirst(2), radix: 16) else { return nil }
                return (String(columns[0]), value)
            })
        }
        var observed = recipe
        observed.memoryObservation = .init(largestFailedHeap: 0x86000000, heapOpened: false, aliasMappedHighWater: 0)
        let sized = try memory(observed)
        try check(sized["mem_alias"] == 0x98000000 && sized["mem_reserve"] == 0x140000000, "observed heap fits inside unchanged reservation")
        try check(sized["app_pool"] == 0xcd500000, "heap correction preserves application pool")
        observed.memoryObservation = .init(largestFailedHeap: 0, heapOpened: false, aliasMappedHighWater: 0x110000000)
        try check(try memory(observed)["app_pool"] == 0x40000000, "alias pressure without heap selects bounded pool")
        observed.memoryObservation = .init(largestFailedHeap: 0, heapOpened: true, aliasMappedHighWater: 0x110000000)
        try check(try memory(observed)["app_pool"] == 0xcd500000, "opened heap forbids alias-only pool reduction")
        observed.memoryObservation = .init(largestFailedHeap: 0, heapOpened: false, aliasMappedHighWater: 0x108000000)
        try check(try memory(observed)["app_pool"] == 0xcd500000, "alias pressure threshold is strict")
        for observation in [
            NativePreparationRecipe.MemoryObservation(largestFailedHeap: UInt64.max, heapOpened: false, aliasMappedHighWater: 0),
            .init(largestFailedHeap: 1, heapOpened: false, aliasMappedHighWater: 0),
            .init(largestFailedHeap: 0, heapOpened: false, aliasMappedHighWater: 0x118000001)
        ] {
            observed.memoryObservation = observation
            try reject("invalid memory observation") { _ = try observed.profile(profiles: []) }
        }
        try check(try JSONDecoder().decode(NativePreparationRecipe.self, from: JSONEncoder().encode(recipe)).runtime(profiles: []) == recipe.runtime(profiles: []), "legacy recipe without observations retains its runtime bytes")
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
            var r = NativePreparationRecipe(schema: 1, titleID: reviewed.titleID, version: reviewed.version, contentVersion: 0, name: reviewed.name,
                modules: reviewed.modules, buildIDs: reviewed.buildIDs, sdkVersions: [], systemResource: 0, usesHeap: false,
                largeBSS: 0, usedMemory: 0x4000000, nros: nros, reviewedVersion: reviewed.version)
            try check(try NativePreparationRecipe.hash(r.runtime()) == reviewed.profileSHA256, "reviewed runtime digest")
            r.memoryObservation = .init(largestFailedHeap: 0x86000000, heapOpened: false, aliasMappedHighWater: 0)
            try check(try NativePreparationRecipe.hash(r.runtime()) == reviewed.profileSHA256, "reviewed overrides retain priority over memory observations")
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
