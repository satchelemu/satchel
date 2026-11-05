import CryptoKit
import Foundation

struct PrepareFileRecord: Codable { let bytes: UInt64; let sha256: String }
struct PrepareShaderPack: Codable {
    let format: Int
    let titleID: String
    let version: String
    let contentVersion: UInt32
    let enginePin: String
    let forkPatchCount: Int
    let shaderChain: String
    let shaderInputs: String
    let modules: [String]
    let buildIDs: [String: String]
    let environment: [String: String]
    let files: [String: PrepareFileRecord]
}

/// A real signer must return only after signing and validating the complete file.
protocol PreparedGameSigner {
    func sign(_ dylib: URL, token: PrepareCancellation, progress: @escaping SwitchByteProgress) throws -> PrepareSigningState
}
enum PrepareSigningState: String { case needsSignature = "needs signature", signed }
extension PreparedGameSigner {
    func sign(_ dylib: URL, token: PrepareCancellation) throws -> PrepareSigningState {
        try sign(dylib, token: token, progress: { _, _ in })
    }
}
struct DevelopmentUnsignedSigner: PreparedGameSigner {
    func sign(_ dylib: URL, token: PrepareCancellation, progress: @escaping SwitchByteProgress) throws -> PrepareSigningState {
        try token.check()
        return .needsSignature
    }
}

enum PrepareIO {
    static func digest(_ url: URL, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws -> Data {
        let handle = try FileHandle(forReadingFrom: url); defer { try? handle.close() }
        let total = UInt64(try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
        var completed: UInt64 = 0
        var hash = SHA256()
        while try autoreleasepool(invoking: { () throws -> Bool in
            try token.check()
            guard let bytes = try handle.read(upToCount: 1024 * 1024), !bytes.isEmpty else { return false }
            hash.update(data: bytes); completed += UInt64(bytes.count); progress(completed, total); return true
        }) {}
        return Data(hash.finalize())
    }
    static func hex(_ bytes: Data) -> String { bytes.map { String(format: "%02x", $0) }.joined() }
    static func regular(_ root: URL, _ name: String) throws -> URL {
        let parts = name.split(separator: "/", omittingEmptySubsequences: false)
        guard !parts.isEmpty, !parts.contains(""), !parts.contains("."), !parts.contains(".."), !name.contains("\\") else {
            throw PrepareFailure(message: "Invalid shader pack path.")
        }
        var current = root
        for (i, part) in parts.enumerated() {
            current.appendPathComponent(String(part))
            let v = try current.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey, .isDirectoryKey])
            guard v.isSymbolicLink != true, i == parts.count - 1 ? v.isRegularFile == true : v.isDirectory == true else {
                throw PrepareFailure(message: "Shader pack contains an invalid file.")
            }
        }
        return current
    }
    static func record(_ file: URL, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws -> PrepareFileRecord {
        let size = try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
        return PrepareFileRecord(bytes: UInt64(size), sha256: hex(try digest(file, token: token, progress: progress)))
    }
    static func copy(_ source: URL, to destination: URL, expected: PrepareFileRecord, token: PrepareCancellation, progress: SwitchByteProgress = { _, _ in }) throws {
        guard try source.resourceValues(forKeys: [.fileSizeKey]).fileSize == Int(exactly: expected.bytes) else {
            throw PrepareFailure(message: "Shader pack file size mismatch.")
        }
        try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        guard FileManager.default.createFile(atPath: destination.path, contents: nil) else { throw PrepareFailure(message: "Cannot copy shader pack.") }
        let input = try FileHandle(forReadingFrom: source), output = try FileHandle(forWritingTo: destination)
        defer { try? input.close(); try? output.close() }
        var hash = SHA256(), count: UInt64 = 0
        while try autoreleasepool(invoking: { () throws -> Bool in
            try token.check()
            guard let bytes = try input.read(upToCount: 1024 * 1024), !bytes.isEmpty else { return false }
            count += UInt64(bytes.count)
            guard count <= expected.bytes else { throw PrepareFailure(message: "Shader pack changed during copy.") }
            hash.update(data: bytes); try output.write(contentsOf: bytes); progress(count, expected.bytes); return true
        }) {}
        guard count == expected.bytes, hex(Data(hash.finalize())) == expected.sha256 else {
            throw PrepareFailure(message: "Shader pack digest mismatch.")
        }
    }
}

enum PreparePipeline {
    static let title = "0100F2C0115B6000"
    static let moduleNames = ["main", "subsdk0", "sdk"]
    static func requireSupportedBase(_ base: URL, keys: [String: Data], cancelled: () -> Bool = { false }) throws {
        let programs = try SwitchExecutableReader.programs(base, keys: keys, cancelled: cancelled)
        try requireSupportedTitles(programs.map(\.titleID))
    }
    static func requireSupportedTitles(_ titles: [String]) throws {
        guard !titles.isEmpty else { throw SwitchExecutableError.noProgram }
    }
    static func run(base: URL, update: URL?, addOns: [URL] = [], keys: [String: Data], shaderPack: URL? = nil, destination: URL, applicationID: UInt64? = nil,
                    signer: PreparedGameSigner, token: PrepareCancellation, allowReviewedProfiles: Bool = true,
                    memoryObservation: NativePreparationRecipe.MemoryObservation? = nil,
                    details: ((PrepareProgressSnapshot) -> Void)? = nil,
                    progress: @escaping (String, Double?) -> Void) throws -> PrepareSigningState {
        let reporter = PrepareProgress { value in
            details?(value); progress(value.message, value.overallFraction)
        }
        guard allowReviewedProfiles || shaderPack == nil else {
            throw PrepareFailure(message: "Generic profile validation requires runtime shaders, without a reviewed shader pack.")
        }
        guard memoryObservation == nil || !allowReviewedProfiles else {
            throw PrepareFailure(message: "Memory observations require explicit generic profile validation.")
        }
        reporter.update(.planning, 0, 0, message: "Checking shader pack and destination…", force: true)
        let fm = FileManager.default
        guard !fm.fileExists(atPath: destination.path) else {
            throw PrepareFailure(message: "A prepared package already exists. Move it aside in Files before preparing again.")
        }
        let selection = try PrepareDiscovery.select(base: base, update: update, keys: keys, titleID: applicationID, token: token)
        let title = String(format: "%016llX", selection.titleID)
        let auxiliary = Set(["profile.tsv", "dm/shader_map.tsv", "dm/pso_manifest.tsv", "dm/c1_full.tsv", "dm/cb_extent.tsv", "dm/shader_pairs.tsv", "dm/pso_pairs.tsv"])
        var suppliedPack: PrepareShaderPack?
        if let shaderPack {
            let packFile = try PrepareIO.regular(shaderPack, "shader-pack.json")
            guard try packFile.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max < 32 * 1024 * 1024 else {
                throw PrepareFailure(message: "Shader pack manifest exceeds limit.")
            }
            let pack = try JSONDecoder().decode(PrepareShaderPack.self, from: Data(contentsOf: packFile))
            guard let reviewed = NativeGameProfile.find(titleID: title, version: pack.version),
                  pack.format == 1, pack.titleID == title, pack.enginePin == reviewed.enginePin,
                  pack.forkPatchCount == 34, pack.shaderChain == reviewed.shaderChain, pack.shaderInputs == reviewed.shaderInputs,
                  pack.modules == reviewed.modules, pack.environment == reviewed.environment, pack.buildIDs == reviewed.buildIDs,
                  pack.files["profile.tsv"]?.sha256 == reviewed.profileSHA256,
                  pack.files.count < 250_000,
                  auxiliary.subtracting(["dm/pso_pairs.tsv", "dm/cb_extent.tsv", "dm/shader_pairs.tsv"]).isSubset(of: Set(pack.files.keys)),
                  pack.files["msl/b-\(pack.shaderChain)/inputs.tsv"] != nil else {
                throw PrepareFailure(message: "Shader pack does not match the reviewed host profile.")
            }
            suppliedPack = pack
        }
        var packBytes: UInt64 = 0
        var checkedEntries: UInt64 = 0
        for (name, record) in suppliedPack?.files ?? [:] {
            let pack = suppliedPack!
            let shader = name.hasPrefix("msl/b-\(pack.shaderChain)/") && (name.hasSuffix(".metallib") || name.hasSuffix(".json") || name.hasSuffix("/inputs.tsv"))
            guard auxiliary.contains(name) || shader, record.bytes <= 256 * 1024 * 1024,
                  record.sha256.count == 64, record.sha256.allSatisfy({ "0123456789abcdef".contains($0) }) else {
                throw PrepareFailure(message: "Shader pack contains unsupported files.")
            }
            _ = try PrepareIO.regular(shaderPack!, name)
            packBytes += record.bytes
            checkedEntries += 1
            reporter.update(.planning, 0, 0, message: "Checking shader pack files…", fraction: Double(checkedEntries) / Double(pack.files.count))
        }
        reporter.finish(.planning)
        return try SwitchContentPipeline.withContent(base: base, update: selection.update, keys: keys,
            applicationID: selection.titleID, cancelled: { token.cancelled }, progress: { phase, done, total, fraction in
                switch phase {
                case .identification: reporter.update(.identification, done, total, message: "Identifying base and update…", fraction: fraction)
                case .authentication:
                    reporter.finishOnce(.identification)
                    reporter.update(.authentication, done, total, message: "Authenticating base and update…")
                case .composition:
                    reporter.finishOnce(.authentication)
                    reporter.update(.composition, done, total, message: "Composing and verifying game code…", fraction: fraction)
                }
            }, identified: { titles in try requireSupportedTitles(titles) }) { content in
            var recipe = try PrepareDiscovery.recipe(content, selection: selection, token: token)
            // Diagnostic callers can exercise exactly the unknown-game path while
            // keeping the bundled overrides and existing packages intact.
            if !allowReviewedProfiles { recipe.reviewedVersion = nil }
            recipe.memoryObservation = memoryObservation
            let moduleNames = recipe.modules
            if let pack = suppliedPack {
                guard recipe.contentVersion == pack.contentVersion, recipe.modules == pack.modules, recipe.buildIDs == pack.buildIDs else {
                    throw PrepareFailure(message: "Selected content does not match the shader pack.")
                }
            }
            reporter.finish(.composition, message: "Checking temporary storage…")
            let tree = try content.romFS.tree(cancelled: { token.cancelled })
            var imageBytes: UInt64 = 32
            for entry in tree { imageBytes += (entry.file?.size ?? 0) + 24 + UInt64(entry.name.count) }
            // Budget includes rewritten modules, final dylib, metadata, filesystem
            // overhead and a 1 GiB reserve. No duplicate extracted RomFS tree.
            let required = imageBytes + packBytes + 2 * 1024 * 1024 * 1024
            let parent = destination.deletingLastPathComponent()
            try fm.createDirectory(at: parent, withIntermediateDirectories: true)
            let free = (try fm.attributesOfFileSystem(forPath: parent.path)[.systemFreeSize] as? NSNumber)?.uint64Value ?? 0
            guard free >= required else {
                throw PrepareFailure(message: "Not enough free storage. Preparation needs \(ByteCountFormatter.string(fromByteCount: Int64(required), countStyle: .file)) available. Free space in iPhone Settings > General > iPhone Storage, then try again.")
            }
            let stage = parent.appendingPathComponent(".prepare-" + UUID().uuidString, isDirectory: true)
            try fm.createDirectory(at: stage, withIntermediateDirectories: false)
            var published = false
            defer { if !published { try? fm.removeItem(at: stage) } }
            let scratch = stage.appendingPathComponent("rewrite", isDirectory: true), tables = stage.appendingPathComponent("dm", isDirectory: true)
            try fm.createDirectory(at: scratch, withIntermediateDirectories: false)
            try fm.createDirectory(at: tables, withIntermediateDirectories: false)
            let rewriteTotal = content.program.modules.filter { moduleNames.contains($0.name) }.reduce(UInt64(0)) { $0 + UInt64($1.data.count) }
            var rewriteDone: UInt64 = 0
            for name in moduleNames {
                try token.check()
                let moduleSize = UInt64(content.program.modules.first { $0.name == name }!.data.count)
                reporter.update(.rewriting, 0, 0, message: "Rewriting \(name)…", fraction: Double(rewriteDone) / Double(rewriteTotal))
                try autoreleasepool {
                    let module = content.program.modules.first { $0.name == name }!
                    try PrepareRewrite.write(SwitchDecodedModule.decode(module), to: scratch, token: token) { fraction, done, total in
                        reporter.update(.rewriting, done, total, message: "Rewriting \(name)…",
                            fraction: (Double(rewriteDone) + Double(moduleSize) * fraction) / Double(rewriteTotal))
                    }
                }
                rewriteDone += moduleSize
                for suffix in ["fixups.tsv", "gates.tsv"] {
                    try fm.copyItem(at: scratch.appendingPathComponent(name + "." + suffix), to: tables.appendingPathComponent(name + "." + suffix))
                }
            }
            reporter.update(.rewriting, 0, 0, message: "Discovering and rewriting registered dynamic modules…", fraction: 0.99)
            recipe.nros = try PrepareDiscovery.nros(content.romFS, to: scratch, token: token)
            if let version = recipe.reviewedVersion,
               let reviewed = NativeGameProfile.find(titleID: recipe.titleID, version: version),
               (reviewed.nroBuildIDs ?? [:]) != Dictionary(uniqueKeysWithValues: recipe.nros.map { ($0.name, $0.buildID) }) {
                recipe.reviewedVersion = nil
            }
            for nro in recipe.nros {
                for suffix in ["fixups.tsv", "gates.tsv"] {
                    try fm.copyItem(at: scratch.appendingPathComponent(nro.name + "." + suffix), to: tables.appendingPathComponent(nro.name + "." + suffix))
                }
            }
            if !recipe.nros.isEmpty { try recipe.inventory.write(to: tables.appendingPathComponent("nro.tsv")) }
            var used: UInt64 = 0, bss: UInt64 = 0
            for module in moduleNames {
                let layout = try PrepareMachO.layout(scratch.appendingPathComponent(module + ".fixups.tsv"))
                func number(_ key: String) -> UInt64 { UInt64((layout[key] ?? "0").replacingOccurrences(of: "0x", with: ""), radix: 16) ?? 0 }
                if layout["PATCH_OFF"]?.hasPrefix("-") == true {
                    used += number("IMAGE_SIZE"); bss += number("IMAGE_SIZE") - number("FILE_IMAGE_LEN")
                } else {
                    used += UInt64(PrepareMachO.rounded(try scratch.appendingPathComponent(module + ".patched.bin").resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0))
                }
            }
            let needed = ((used + 0x1fffff) & ~UInt64(0x1fffff)) - bss
            recipe.usedMemory = needed > 0x4000000 ? needed + 0x400000 : 0x4000000
            let preparedProfile = try recipe.profile()
            reporter.finish(.rewriting)
            reporter.update(.container, 0, 0, message: "Writing native game container…", force: true)
            let dylib = stage.appendingPathComponent("game.dylib")
            let layout = try PrepareMachO.write(modules: moduleNames, nros: recipe.nros.map(\.name), from: scratch, to: dylib, token: token) { done, total in
                reporter.update(.container, done, total, message: "Writing and hashing native container…")
            }
            reporter.finish(.container)
            try fm.removeItem(at: scratch)
            reporter.update(.romfs, 0, imageBytes, message: "Writing composed RomFS…", force: true)
            let romResult = try SwitchRomFSImage.write(content.romFS, to: stage.appendingPathComponent("romfs.img"), cancelled: { token.cancelled }) { p in
                reporter.update(.romfs, p.imageCompletedBytes, p.imageTotalBytes, message: "Writing composed RomFS…")
            }
            let dlc = try SwitchAddOnContent.write(sources: [base] + (update.map { [$0] } ?? []) + addOns,
                applicationID: content.selection.applicationID,
                applicationVersion: content.selection.patchVersion ?? content.selection.baseVersion,
                keys: keys, to: stage, cancelled: { token.cancelled }, reserveBytes: packBytes + 1024 * 1024 * 1024) { done, total in
                    reporter.update(.romfs, done, total, message: "Preparing add-on content…")
                }
            reporter.finish(.romfs)
            var copiedBytes: UInt64 = 0
            var files: [String: PrepareFileRecord] = [:]
            reporter.update(.shaders, 0, packBytes, message: suppliedPack == nil ? "Preparing runtime shader capture…" : "Copying and verifying shader pack…", force: true)
            if let pack = suppliedPack, let shaderPack {
                for (name, record) in pack.files.sorted(by: { $0.key < $1.key }) {
                    try autoreleasepool {
                        let source = try PrepareIO.regular(shaderPack, name)
                        try PrepareIO.copy(source, to: stage.appendingPathComponent(name), expected: record, token: token) { done, _ in
                            reporter.update(.shaders, copiedBytes + done, packBytes, message: "Copying and verifying shader pack…")
                        }
                    }
                    copiedBytes += record.bytes
                }
                files = pack.files
            } else {
                let shaderRoot = stage.appendingPathComponent("msl/b-\(NativePreparationRecipe.shaderChain)")
                try fm.createDirectory(at: shaderRoot, withIntermediateDirectories: true)
                for name in auxiliary where name != "profile.tsv" {
                    try Data("# Runtime shader capture pending phone translator\n".utf8).write(to: stage.appendingPathComponent(name))
                }
                try Data("# No compiled game shaders. Runtime capture only.\n".utf8).write(to: shaderRoot.appendingPathComponent("inputs.tsv"))
                try recipe.runtime().write(to: stage.appendingPathComponent("profile.tsv"))
                for name in auxiliary.union(["msl/b-\(NativePreparationRecipe.shaderChain)/inputs.tsv"]) {
                    files[name] = try PrepareIO.record(stage.appendingPathComponent(name), token: token)
                }
            }
            reporter.finish(.shaders)
            reporter.update(.signing, 0, 0, message: "Signing game container…", force: true)
            let state = try signer.sign(dylib, token: token) { done, total in
                reporter.update(.signing, done, total, message: "Signing and verifying game container…")
            }
            reporter.finish(.signing)
            reporter.update(.binding, 0, 0, message: "Binding prepared files…", force: true)
            for row in dlc { files[row.path] = PrepareFileRecord(bytes: row.bytes, sha256: row.sha256) }
            // Bind the exact bytes hashed while writing RomFS. Its IVFC checks are
            // unchanged, and the host still verifies this digest before loading.
            files["romfs.img"] = PrepareFileRecord(bytes: romResult.imageBytes, sha256: PrepareIO.hex(romResult.sha256))
            let bindingNames = ["game.dylib"] + (recipe.nros.isEmpty ? [] : ["dm/nro.tsv"]) + (moduleNames + recipe.nros.map(\.name)).flatMap { ["dm/\($0).fixups.tsv", "dm/\($0).gates.tsv"] }
            let bindingBytes = try bindingNames.reduce(UInt64(0)) { total, name in
                total + UInt64(try stage.appendingPathComponent(name).resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
            }
            var boundBytes: UInt64 = 0
            for name in bindingNames {
                let record = try PrepareIO.record(stage.appendingPathComponent(name), token: token) { done, _ in
                    reporter.update(.binding, boundBytes + done, bindingBytes, message: "Binding prepared files…", fraction: 0.8 * Double(boundBytes + done) / Double(max(1, bindingBytes)))
                }
                files[name] = record; boundBytes += record.bytes
            }
            reporter.update(.binding, boundBytes, bindingBytes, message: "Writing package manifest…", fraction: 0.85, force: true)
            let fileJSON = try JSONSerialization.jsonObject(with: JSONEncoder().encode(files))
            let profile = suppliedPack == nil ? preparedProfile : NativeGameProfile.find(titleID: title, version: suppliedPack!.version)!
            var manifest: [String: Any] = ["format": suppliedPack == nil ? 2 : 1, "titleID": title, "version": profile.version,
                "enginePin": profile.enginePin, "forkPatchCount": 34, "shaderChain": profile.shaderChain,
                "shaderInputs": profile.shaderInputs, "modules": moduleNames, "environment": profile.environment,
                "addOnContent": try JSONSerialization.jsonObject(with: JSONEncoder().encode(dlc)),
                "layout": layout, "buildIDs": recipe.buildIDs, "contentVersion": recipe.contentVersion, "signingStatus": state.rawValue, "files": fileJSON]
            if suppliedPack == nil {
                manifest["recipe"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(recipe))
                manifest["preparedProfile"] = try JSONSerialization.jsonObject(with: JSONEncoder().encode(preparedProfile))
                manifest["shaderStatus"] = "runtime first-use translation required"
                manifest["experimental"] = recipe.reviewedVersion == nil
            } else if !recipe.nros.isEmpty {
                // A reviewed format 1 package retains its exact inventory binding.
                guard let expected = profile.nroInventorySHA256, files["dm/nro.tsv"]?.sha256 == expected else {
                    throw PrepareFailure(message: "The reviewed NRO inventory differs. Use native experimental preparation without a shader pack.")
                }
            }
            try JSONSerialization.data(withJSONObject: manifest, options: [.sortedKeys, .prettyPrinted]).write(to: stage.appendingPathComponent("manifest.json"), options: .atomic)
            try token.check()
            // moveItem refuses an existing destination. All original imports and
            // previous packages survive a failed or cancelled preparation.
            try fm.moveItem(at: stage, to: destination); published = true
            reporter.finish(.binding, message: state == .signed ? "Prepared game is signed." : "Prepared game needs signature. Development Mac signing is required.")
            return state
        }
    }
}

enum PreparedEnvironment {
    static let values = [
        "DM_LAUNCHER": "0", "DM_SAVED_CHOICES": "0", "DM_SAVE_SYNC": "0", "DM_FPS_TARGET": "30",
        "DM_RENDER_SCALE": "0.65", "DM_SHOW_FPS": "1", "DM_OPMODE": "1", "DM_PSO_PREPARE": "1",
        "DM_PSO_MTL4": "0", "DM_THERMAL_CAP": "1", "DM_BOOTPROF": "1", "DM_SLOWFRAME": "1",
        "DM_NVN_TSV": "zelda-frame-times.tsv", "DM_NVN_TSV_CALLS": "0", "DM_NVN_GPUPROF": "every:600", "DM_MEMSTAT": "zelda-memory.tsv"
    ]
}
