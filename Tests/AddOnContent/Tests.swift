import CryptoKit
import Foundation

@main @MainActor struct AddOnTests {
    static let title = ContentTests.title
    static let put = ExecutableTests.put
    static var assertions = 0
    static func check(_ okay: Bool) { precondition(okay); assertions += 1 }
    static func fails(_ body: () throws -> Void) { do { try body(); preconditionFailure("Expected rejection") } catch { assertions += 1 } }

    static func fixture(index: UInt64 = 1, version: UInt64 = 1, required: UInt64 = 2,
                        extended: Int = 16, corrupt: Bool = false, applicationID: UInt64? = nil) throws -> Data {
        let title = applicationID ?? Self.title
        let id = title + 0x1000 + index
        var rom = RomFSTests.fixture(); put(2, &rom.0, 0, 2); rom.0[3] = 3; rom.0[4] = 3
        var data = try ContentTests.nca([rom], contentType: 5, id: id)
        let start = 32 + extended
        var cnmt = Data(count: start + 56 + 32)
        put(id, &cnmt, 0, 8); put(version, &cnmt, 8, 4); cnmt[12] = 0x82
        put(UInt64(extended), &cnmt, 14, 2); put(1, &cnmt, 16, 2)
        put(title, &cnmt, 32, 8); put(required, &cnmt, 40, 4)
        let hash = Data(SHA256.hash(data: data))
        cnmt.replaceSubrange(start..<start + 32, with: hash)
        cnmt.replaceSubrange(start + 32..<start + 48, with: hash.prefix(16))
        put(UInt64(data.count), &cnmt, start + 48, 5); cnmt[start + 54] = 2
        let meta = try ContentTests.nca([ContentTests.pfsSection(ExecutableTests.pfs([("AddOnContent.cnmt",cnmt)]))], contentType: 1, id: id)
        if corrupt { data[data.count - 1] ^= 1 }
        return ExecutableTests.pfs([(hash.prefix(16).cpHex + ".nca", data),
            (Data(SHA256.hash(data: meta)).prefix(16).cpHex + ".cnmt.nca", meta)])
    }

    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        if CommandLine.arguments.count == 5, CommandLine.arguments[2] == "--interrupt-dlc" {
            _ = try AddOnUpdate.run(root: URL(fileURLWithPath: CommandLine.arguments[3]),
                sources: [URL(fileURLWithPath: CommandLine.arguments[4])], keys: ExecutableTests.keys,
                token: PrepareCancellation(), reserveBytes: 35 * 1024 * 1024 * 1024) { phase in
                    if phase == .publishing { exit(77) }
                }
            preconditionFailure("The interruption checkpoint was not reached")
        }
        if CommandLine.arguments.count == 5, CommandLine.arguments[2] == "--emit-dlc",
           let application = UInt64(CommandLine.arguments[3], radix: 16),
           let version = UInt32(CommandLine.arguments[4]) {
            try fixture(index: 1, required: UInt64(version), applicationID: application)
                .write(to: root.appendingPathComponent("synthetic-dlc-1.nsp"))
            try fixture(index: 2, required: UInt64(version), applicationID: application)
                .write(to: root.appendingPathComponent("synthetic-dlc-2.nsp"))
            try fixture(index: 1, version: 2, required: UInt64(version), applicationID: application)
                .write(to: root.appendingPathComponent("synthetic-dlc-1-v2.nsp"))
            try fixture(index: 3, required: UInt64(version) + 1, applicationID: application)
                .write(to: root.appendingPathComponent("synthetic-dlc-requires-update.nsp"))
            let syntheticKeys = ExecutableTests.keys.sorted { $0.key < $1.key }.map { $0.key + " = " + $0.value.cpHex }.joined(separator: "\n")
            try Data(syntheticKeys.utf8).write(to: root.appendingPathComponent("synthetic.keys"))
            print("PASS: emitted four synthetic DLC fixtures and synthetic keys for an isolated test app")
            return
        }
        if CommandLine.arguments.count > 2, CommandLine.arguments[2] == "--scan-fixtures" {
            var keys = ExecutableTests.keys; keys["titlekek_00"] = ExecutableTests.areaKey
            for path in CommandLine.arguments.dropFirst(3) {
                let package = try SwitchExecutableReader.ContentPackage(URL(fileURLWithPath: path), keys: keys, cancelled: { false })
                print("FIXTURE SCAN: \(path), AddOnContent=\(package.metadata.filter { $0.type == 0x82 }.count)")
                package.close()
            }
            return
        }
        let source = root.appendingPathComponent("dlc.nsp")
        try fixture().write(to: source)
        let imported = try SwitchPackageImport.copy(from: source, into: root, cancelled: { false }, progress: { _, _ in })
        let local = root.appendingPathComponent(imported.id + ".nsp")
        let (info, icon) = try GameInfoReader.read(local, keys: ExecutableTests.keys, cancelled: { false })
        check(info.kind == .addOnContent && info.applicationID == title && icon == nil)
        check(info.addOns?.count == 1 && info.addOns?[0].requiredApplicationVersion == 2)
        check(GameInfo.addOns(for: title, in: [info,info]).count == 1)
        check(GameInfo.addOns(for: title + 0x2000, in: [info]).isEmpty)
        let entries = try SwitchPackageReader.contentEntries(source)
        let bytes = try Data(contentsOf: source)
        let secure = PackageReaderTests.directory(entries.map(\.name), payloads: entries.map {
            bytes.subdata(in: Int($0.offset)..<Int($0.offset + $0.size))
        }, hashed: true)
        let cartridge = root.appendingPathComponent("dlc.xci")
        try PackageReaderTests.xci(secure).write(to: cartridge)
        let cartInfo = try GameInfoReader.read(cartridge, keys: ExecutableTests.keys, cancelled: { false }).0
        check(cartInfo == info)
        let artwork = GameArtworkStore()
        func item(_ id: String) -> HomebrewItem {
            HomebrewItem(id: id, name: id, bytes: 1, textBytes: 0, roBytes: 0, dataBytes: 0, bssBytes: 0, importedAt: Date())
        }
        let baseItem = item("base"), dlcItem = item("dlc"), updateItem = item("update")
        artwork.infos["base"] = GameInfo(applicationID: title, kind: .application, version: 0,
            displayVersion: "1", title: "Synthetic base", publisher: "", hasIcon: false, accent: nil)
        artwork.infos["dlc"] = info
        artwork.infos["update"] = GameInfo(applicationID: title, kind: .update, version: 2,
            displayVersion: "2", title: "Synthetic update", publisher: "", hasIcon: false, accent: nil)
        let shelf = artwork.shelf([dlcItem,updateItem,baseItem])
        check(shelf.count == 1 && shelf[0].item.id == "base" && shelf[0].updates.map(\.id) == ["update"])
        check(artwork.shelf([dlcItem,updateItem]).isEmpty)
        var pending = item("pending"); pending.package = imported.summary
        check(artwork.shelf([pending]).isEmpty)
        let reader = try SwitchExecutableReader.ContentPackage(source, keys: ExecutableTests.keys, cancelled: { false })
        let addon = reader.metadata[0]; reader.close()
        let program = try ContentTests.nca([ContentTests.pfsSection(ExecutableTests.pfs([("main", ExecutableTests.module(1))]))])
        let baseMeta = try SwitchContentMeta.parse(ContentTests.cnmt(program, type: 0x80, id: title, version: 0), metaTitleID: title)
        let patchMeta = try SwitchContentMeta.parse(ContentTests.cnmt(program, type: 0x81, id: title + 0x800, version: 2), metaTitleID: title + 0x800)
        let selection = try SwitchContentMeta.select(base: [baseMeta], update: [patchMeta,addon], applicationID: title)
        check(selection.1?.version == 2)
        let stage = root.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: stage, withIntermediateDirectories: true)
        let rows = try SwitchAddOnContent.write(sources: [source,local,cartridge], applicationID: title,
            applicationVersion: 2, keys: ExecutableTests.keys, to: stage, cancelled: { false })
        check(rows.count == 1 && rows[0].index == 1 && rows[0].path == "aoc/1.img")
        let image = try Data(contentsOf: stage.appendingPathComponent(rows[0].path))
        check(image.prefix(8) == Data("DMROMFS1".utf8) && image.count == rows[0].bytes)
        check(Data(SHA256.hash(data: image)).cpHex == rows[0].sha256)
        let bindings = Dictionary(uniqueKeysWithValues: rows.map { ($0.path, (bytes: $0.bytes, sha256: $0.sha256)) })
        func validate(_ values: [PreparedAddOn] = rows) throws {
            try PreparedAddOn.validate(values, titleID: String(format: "%016llX",title), contentVersion: 2, root: stage, files: bindings)
        }
        try validate(); assertions += 1
        // Exercise the production launch verifier, including its cached path, with a synthetic profile.
        let profileBytes = Data("synthetic DLC verifier profile".utf8)
        let profileHash = Data(SHA256.hash(data: profileBytes)).cpHex
        let profile = NativeGameProfile(titleID: String(format: "%016llX",title), version: "synthetic",
            name: "Synthetic DLC", packageName: "synthetic", phonePrepare: false,
            enginePin: "synthetic", shaderChain: "synthetic", shaderInputs: "synthetic",
            profileSHA256: profileHash, modules: ["main"], nroInventorySHA256: nil, nroBuildIDs: nil,
            buildIDs: ["main": "1234"], environment: [:], requiredFiles: ["profile.tsv"])
        try profileBytes.write(to: stage.appendingPathComponent("profile.tsv"))
        try FileManager.default.createDirectory(at: stage.appendingPathComponent("dm"), withIntermediateDirectories: true)
        let layout = Data("# BUILD_ID 1234\n".utf8)
        try layout.write(to: stage.appendingPathComponent("dm/main.fixups.tsv"))
        try Data().write(to: stage.appendingPathComponent("dm/main.gates.tsv"))
        var files: [String: Any] = [rows[0].path: ["bytes": rows[0].bytes,"sha256":rows[0].sha256]]
        for name in ["profile.tsv", "dm/main.fixups.tsv", "dm/main.gates.tsv"] {
            let data = try Data(contentsOf: stage.appendingPathComponent(name))
            files[name] = ["bytes": data.count, "sha256": Data(SHA256.hash(data: data)).cpHex]
        }
        let manifest: [String: Any] = ["format":1,"titleID":profile.titleID,"version":profile.version,
            "enginePin":profile.enginePin,"shaderChain":profile.shaderChain,"shaderInputs":profile.shaderInputs,
            "modules":profile.modules,"environment":profile.environment,"files":files,"contentVersion":2,
            "addOnContent":try JSONSerialization.jsonObject(with: JSONEncoder().encode(rows))]
        try JSONSerialization.data(withJSONObject: manifest).write(to: stage.appendingPathComponent("manifest.json"))
        try FileManager.default.createDirectory(at: root.appendingPathComponent("verification"), withIntermediateDirectories: true)
        _ = try PreparedPackage.verify(root: stage, profiles: [profile], forceFull: true); assertions += 1
        _ = try PreparedPackage.verify(root: stage, profiles: [profile]); assertions += 1
        var changed = image; changed[32] ^= 1
        try changed.write(to: stage.appendingPathComponent(rows[0].path))
        fails { _ = try PreparedPackage.verify(root: stage, profiles: [profile], forceFull: true) }
        try image.write(to: stage.appendingPathComponent(rows[0].path))
        fails { try validate([]) }
        fails { try validate(rows + rows) }
        let extra = stage.appendingPathComponent("aoc/2.img")
        try image.write(to: extra); fails { try validate() }
        fails { _ = try PreparedPackage.verify(root: stage, profiles: [profile]) }
        try FileManager.default.removeItem(at: extra)
        let original = stage.appendingPathComponent("aoc/1.img")
        try FileManager.default.moveItem(at: original, to: extra)
        try FileManager.default.createSymbolicLink(at: original, withDestinationURL: extra)
        fails { try validate() }
        try FileManager.default.removeItem(at: original); try FileManager.default.moveItem(at: extra, to: original)
        for ext in [16,24] {
            try fixture(extended: ext).write(to: source)
            let result = try GameInfoReader.read(source, keys: ExecutableTests.keys, cancelled: { false }).0
            check(result.addOns?[0].titleID == title + 0x1001)
        }
        try fixture(corrupt: true).write(to: source)
        fails { _ = try GameInfoReader.read(source, keys: ExecutableTests.keys, cancelled: { false }) }
        try fixture().write(to: source)
        let future = root.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: future, withIntermediateDirectories: true)
        fails { _ = try SwitchAddOnContent.write(sources: [source], applicationID: title,
            applicationVersion: 1, keys: ExecutableTests.keys, to: future, cancelled: { false }) }
        // One package can carry two versions of the same DLC. Reopen the selected version.
        let newer = root.appendingPathComponent("newer.nsp")
        try fixture(version: 2).write(to: newer)
        var combined: [String: Data] = [:]
        var order: [String] = []
        for input in [source,newer] {
            let bytes = try Data(contentsOf: input)
            for entry in try SwitchPackageReader.contentEntries(input) where combined[entry.name] == nil {
                order.append(entry.name)
                combined[entry.name] = bytes.subdata(in: Int(entry.offset)..<Int(entry.offset + entry.size))
            }
        }
        let bundle = root.appendingPathComponent("two-versions.nsp")
        try ExecutableTests.pfs(order.map { ($0,combined[$0]!) }).write(to: bundle)
        let versionStage = root.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: versionStage, withIntermediateDirectories: true)
        let newest = try SwitchAddOnContent.write(sources: [bundle], applicationID: title,
            applicationVersion: 2, keys: ExecutableTests.keys, to: versionStage, cancelled: { false })
        check(newest.count == 1 && newest[0].version == 2)
        fails { _ = try GameInfoReader.read(source, keys: ExecutableTests.keys, cancelled: { true }) }
        fails { _ = try SwitchAddOnContent.index(titleID: title + 0x1000, applicationID: title) }
        fails { _ = try SwitchAddOnContent.index(titleID: title + 0x1000 + 2001, applicationID: title) }
        try Data(stage.appendingPathComponent("aoc").path.utf8).write(to: root.appendingPathComponent("engine-aoc-path.txt"))
        print("PASS: \(assertions) DLC import, metadata, image and binding assertions")
        // The general Prepare path emits format 2. Its DLC uses the same verified image.
        let recipe = NativePreparationRecipe(schema: 1, titleID: profile.titleID, version: "synthetic",
            contentVersion: 2, name: "Synthetic DLC", modules: ["main"],
            buildIDs: ["main": String(repeating: "1", count: 32)], sdkVersions: [], systemResource: 0,
            usesHeap: false, largeBSS: 0, usedMemory: 0x4000000, nros: [], reviewedVersion: nil)
        let generated = try recipe.profile()
        var generatedFiles = files
        for name in generated.requiredFiles + ["dm/main.fixups.tsv"] {
            let bytes: Data
            if name == "profile.tsv" { bytes = try recipe.runtime() }
            else if name == "dm/main.fixups.tsv" { bytes = Data(("# BUILD_ID " + recipe.buildIDs["main"]! + "\n").utf8) }
            else { bytes = Data("Synthetic fixture".utf8) }
            let url = stage.appendingPathComponent(name)
            try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
            try bytes.write(to: url)
            generatedFiles[name] = ["bytes": bytes.count, "sha256": Data(SHA256.hash(data: bytes)).cpHex]
        }
        var generatedManifest = manifest
        generatedManifest["format"] = 2
        generatedManifest["signingStatus"] = "signed"
        generatedManifest["enginePin"] = generated.enginePin
        generatedManifest["shaderChain"] = generated.shaderChain
        generatedManifest["shaderInputs"] = generated.shaderInputs
        generatedManifest["environment"] = generated.environment
        generatedManifest["files"] = generatedFiles
        generatedManifest["recipe"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(recipe))
        generatedManifest["preparedProfile"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(generated))
        func writeGenerated() throws {
            try JSONSerialization.data(withJSONObject: generatedManifest).write(to: stage.appendingPathComponent("manifest.json"))
        }
        try writeGenerated()
        _ = try PreparedPackage.verify(root: stage, forceFull: true); assertions += 1
        _ = try PreparedPackage.verify(root: stage); assertions += 1
        generatedManifest["contentVersion"] = 3; try writeGenerated()
        fails { _ = try PreparedPackage.verify(root: stage) }
        generatedManifest["contentVersion"] = 2; try writeGenerated()
        try changed.write(to: stage.appendingPathComponent(rows[0].path))
        fails { _ = try PreparedPackage.verify(root: stage, forceFull: true) }
        try image.write(to: stage.appendingPathComponent(rows[0].path))
        print("PASS: 4 additional format 2 DLC acceptance and mutation assertions")
        try UpdateTests.run(root: root, prepared: stage, profile: generated)
        if CommandLine.arguments.count > 2 {
            let keys = try SwitchKeys.parse(Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2])))
            for path in CommandLine.arguments.dropFirst(3) {
                let url = URL(fileURLWithPath: path)
                let package = try SwitchExecutableReader.ContentPackage(url, keys: keys, cancelled: { false })
                let dlc = package.metadata.filter { $0.type == 0x82 }.count
                let main = package.metadata.first { $0.titleID == 0x0100BA4026E94000 && $0.type == 0x80 }
                let patches = package.metadata.filter { $0.applicationID == 0x0100BA4026E94000 && $0.type == 0x81 }
                package.close()
                print("SCAN: \(url.lastPathComponent), AddOnContent=\(dlc)")
                if main != nil {
                    let info = try GameInfoReader.read(url, keys: keys, cancelled: { false }).0
                    check(info.applicationID == 0x0100BA4026E94000 && info.kind == .application)
                    check(info.extras?.contains { $0.titleID == 0x01005F4027E94000 } == true)
                    check(info.bundlesUpdate == !patches.isEmpty)
                    check(info.version == (patches.map(\.version).max() ?? main!.version))
                    print("PASS: Castlevania main=\(String(format: "%016llX",info.applicationID)), bundledUpdate=\(info.bundlesUpdate), version=\(info.version), extras=\(info.extras!.map { String(format: "%016llX",$0.titleID) }.joined(separator: ","))")
                }
            }
        }
    }
}
