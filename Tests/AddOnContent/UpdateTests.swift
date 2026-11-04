import CryptoKit
import Foundation

@MainActor enum UpdateTests {
    static func run(root: URL, prepared: URL, profile: NativeGameProfile) throws {
        let fm = FileManager.default
        let countBefore = AddOnTests.assertions
        let check = AddOnTests.check
        let fails = AddOnTests.fails
        let game = root.appendingPathComponent("update-package")
        try fm.copyItem(at: prepared, to: game)
        let source = root.appendingPathComponent("add-after-prepare.nsp")
        let original = try inventory(game)
        let manifest = game.appendingPathComponent("manifest.json")
        let reserve: UInt64 = 35 * 1024 * 1024 * 1024
        func update(_ token: PrepareCancellation = PrepareCancellation(), progress: (AddOnUpdate.Phase) throws -> Void = { _ in }) throws -> Int {
            try AddOnUpdate.run(root: game, sources: [source], keys: ExecutableTests.keys, token: token,
                reserveBytes: reserve, progress: progress)
        }
        try AddOnTests.fixture(index: 2).write(to: source)
        // No base, update, shader pack or signing identity is passed to this API.
        let committed = PrepareCancellation()
        check(try update(committed) == 1)
        check(!committed.cancel())
        check(PreparedAddOn.read(from: game).map(\.index) == [1, 2])
        let updated = try inventory(game)
        check(original.allSatisfy { $0.key == "manifest.json" || updated[$0.key] == $0.value })
        _ = try PreparedPackage.verify(root: game, forceFull: true); check(true)
        _ = try PreparedPackage.verify(root: game); check(true)
        check(try update() == 0)
        check(try inventory(game) == updated)
        try AddOnTests.fixture(index: 2, version: 2).write(to: source)
        check(try update() == 1)
        check(PreparedAddOn.read(from: game).first { $0.index == 2 }?.version == 2)
        let upgraded = try inventory(game)
        try AddOnTests.fixture(index: 2, version: 1).write(to: source)
        check(try update() == 0)
        check(try inventory(game) == upgraded)
        // Reject same-version conflicting metadata, a newer required base, and corrupt NCA bytes.
        for bytes in [try AddOnTests.fixture(index: 2, version: 2, required: 1),
                      try AddOnTests.fixture(index: 3, required: 3),
                      try AddOnTests.fixture(index: 3, corrupt: true)] {
            try bytes.write(to: source)
            fails { _ = try update() }
            check(try inventory(game) == upgraded)
        }
        try AddOnTests.fixture(index: 3, applicationID: AddOnTests.title + 0x2000).write(to: source)
        fails { _ = try update() }
        check(try inventory(game) == upgraded)
        PreparedLaunch.current = "synthetic-running"
        fails { _ = try update() }
        PreparedLaunch.current = nil
        check(try inventory(game) == upgraded)
        // Cancellation and injected failures at every boundary leave the old package exact.
        try AddOnTests.fixture(index: 3).write(to: source)
        for phase in [AddOnUpdate.Phase.checking, .cloning, .extracting, .binding, .verifying, .publishing] {
            let token = PrepareCancellation()
            fails { _ = try update(token) { if $0 == phase { token.cancel() } } }
            check(try inventory(game) == upgraded)
            fails { _ = try update() { if $0 == phase { throw CocoaError(.fileWriteOutOfSpace) } } }
            check(try inventory(game) == upgraded)
        }
        fails { _ = try AddOnUpdate.run(root: game, sources: [source], keys: ExecutableTests.keys,
            token: PrepareCancellation(), reserveBytes: UInt64.max) }
        check(try inventory(game) == upgraded)
        try PreparedPackageAccess.begin()
        fails { _ = try update() }
        PreparedPackageAccess.end()
        check(try inventory(game) == upgraded)
        // A staged digest failure must never become the live manifest.
        fails { _ = try update() { phase in
            if phase == .verifying {
                let scratch = try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil).first { $0.lastPathComponent.hasPrefix(".satchel-dlc-") }!
                try Data("corrupt".utf8).write(to: scratch.appendingPathComponent("package/aoc/3.img"))
            }
        } }
        check(try inventory(game) == upgraded)
        // Exit a child after verification but before publication, bypassing all defers.
        let child = Process()
        child.executableURL = URL(fileURLWithPath: CommandLine.arguments[0])
        child.arguments = [root.path, "--interrupt-dlc", game.path, source.path]
        try child.run(); child.waitUntilExit()
        check(child.terminationStatus == 77)
        check(try inventory(game) == upgraded)
        _ = try PreparedPackage.verify(root: game, forceFull: true); check(true)
        let abandoned = try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
            .first { $0.lastPathComponent.hasPrefix(".satchel-dlc-") }!
        check(!fm.fileExists(atPath: abandoned.appendingPathComponent("manifest.json").path))
        check(try update() == 1)
        check(!fm.fileExists(atPath: abandoned.path))
        check(PreparedAddOn.read(from: game).map(\.index) == [1, 2, 3])
        _ = try PreparedPackage.verify(root: game, forceFull: true); check(true)
        // Add the first DLC to a package that was prepared with no aoc directory.
        let empty = root.appendingPathComponent("initially-no-dlc")
        try fm.copyItem(at: prepared, to: empty)
        try fm.removeItem(at: empty.appendingPathComponent("aoc"))
        let emptyManifest = empty.appendingPathComponent("manifest.json")
        var noDLC = try JSONSerialization.jsonObject(with: Data(contentsOf: emptyManifest)) as! [String: Any]
        var noDLCFiles = noDLC["files"] as! [String: Any]
        noDLCFiles.removeValue(forKey: "aoc/1.img")
        noDLC["files"] = noDLCFiles; noDLC.removeValue(forKey: "addOnContent")
        try JSONSerialization.data(withJSONObject: noDLC).write(to: emptyManifest)
        _ = try PreparedPackage.verify(root: empty, forceFull: true); check(true)
        check(try AddOnUpdate.run(root: empty, sources: [source], keys: ExecutableTests.keys,
            token: PrepareCancellation(), reserveBytes: reserve) == 1)
        check(PreparedAddOn.read(from: empty).map(\.index) == [3])
        _ = try PreparedPackage.verify(root: empty, forceFull: true); check(true)
        // Format 1 uses its reviewed profile, and preserves unknown manifest fields.
        var object = try JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as! [String: Any]
        object["format"] = 1; object.removeValue(forKey: "recipe"); object.removeValue(forKey: "preparedProfile")
        object["futureMetadata"] = ["keep": "exact"]
        try JSONSerialization.data(withJSONObject: object).write(to: manifest)
        try AddOnTests.fixture(index: 4).write(to: source)
        check(try AddOnUpdate.run(root: game, sources: [source], keys: ExecutableTests.keys,
            token: PrepareCancellation(), profiles: [profile], reserveBytes: reserve) == 1)
        let result = try JSONSerialization.jsonObject(with: Data(contentsOf: manifest)) as! [String: Any]
        check((result["futureMetadata"] as? [String: String]) == ["keep": "exact"])
        check((result["signingStatus"] as? String) == "signed")
        _ = try PreparedPackage.verify(root: game, profiles: [profile], forceFull: true); check(true)
        check(try Data(contentsOf: game.appendingPathComponent("game.dylib")) == Data(contentsOf: prepared.appendingPathComponent("game.dylib")))
        object = result
        object.removeValue(forKey: "contentVersion")
        // A legacy package with no DLC verifies, but cannot safely infer a numeric base version.
        object["addOnContent"] = []
        var legacyFiles = object["files"] as! [String: Any]
        legacyFiles = legacyFiles.filter { !$0.key.hasPrefix("aoc/") }
        object["files"] = legacyFiles
        try fm.removeItem(at: game.appendingPathComponent("aoc"))
        try JSONSerialization.data(withJSONObject: object).write(to: manifest)
        let legacy = try inventory(game)
        _ = try PreparedPackage.verify(root: game, profiles: [profile], forceFull: true); check(true)
        fails { _ = try AddOnUpdate.run(root: game, sources: [source], keys: ExecutableTests.keys,
            token: PrepareCancellation(), profiles: [profile], reserveBytes: reserve) }
        check(try inventory(game) == legacy)
        check(!(try fm.contentsOfDirectory(atPath: root.path)).contains { $0.hasPrefix(".satchel-dlc-") })
        print("PASS: \(AddOnTests.assertions - countBefore) incremental DLC transaction assertions")
    }

    static func inventory(_ root: URL) throws -> [String: String] {
        var result: [String: String] = [:]
        let iterator = FileManager.default.enumerator(atPath: root.path)!
        for case let name as String in iterator where iterator.fileAttributes?[.type] as? FileAttributeType == .typeRegular {
            result[name] = Data(SHA256.hash(data: try Data(contentsOf: root.appendingPathComponent(name)))).cpHex
        }
        return result
    }
}
