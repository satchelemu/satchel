import Foundation
import CryptoKit

struct NativeGameProfile: Codable, Sendable, Equatable {
    let titleID: String
    let version: String
    let name: String
    let packageName: String
    let phonePrepare: Bool
    let enginePin: String
    let shaderChain: String
    let shaderInputs: String
    let profileSHA256: String
    let modules: [String]
    let nroInventorySHA256: String?
    let nroBuildIDs: [String: String]?
    let buildIDs: [String: String]
    let environment: [String: String]
    let requiredFiles: [String]

    static let unsupportedMessage = "This package has no valid preparation recipe. Prepare the imported game in Satchel."
    static let all: [NativeGameProfile] = {
        guard let url = Bundle.main.url(forResource: "NativeGameProfiles", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let profiles = try? JSONDecoder().decode([NativeGameProfile].self, from: data) else { return [] }
        return profiles
    }()

    static func find(titleID: String, version: String? = nil, in profiles: [NativeGameProfile] = all) -> NativeGameProfile? {
        profiles.first { $0.titleID == titleID.uppercased() && (version == nil || $0.version == version) }
    }

    static func supportMessage(titleID: String) -> String {
        if let profile = find(titleID: titleID) {
            return "Reviewed settings available for \(profile.name). Open Prepare game to continue."
        }
        return "Satchel will generate experimental settings from this game's metadata. Boot and rendering are not guaranteed."
    }
}

/// The manifest fields that bind a prepared package to this build: signing, the recipe or
/// reviewed profile, the engine pin, shader chain and inputs, modules, environment and runtime
/// profile. `PreparedPackage.verify` checks them before reading any package file, and the
/// library uses the same check to tell a current package from one an older engine made.
struct PreparedIdentity: Decodable {
    struct FileRecord: Decodable { let sha256: String }
    let format: Int
    let signingStatus: String?
    let contentVersion: UInt32?
    let recipe: NativePreparationRecipe?
    let preparedProfile: NativeGameProfile?
    let titleID: String
    let version: String
    let enginePin: String
    let shaderChain: String
    let shaderInputs: String
    let modules: [String]
    let environment: [String: String]
    let files: [String: FileRecord]

    /// The profile this package was made for, or the launch verifier's refusal.
    func profile(in profiles: [NativeGameProfile] = NativeGameProfile.all) throws -> NativeGameProfile {
        func invalid(_ message: String) -> NSError {
            NSError(domain: "PreparedPackage", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
        }
        guard signingStatus == nil || signingStatus == "signed" else {
            throw invalid("Prepared game needs signature. Sign the container before launching.")
        }
        let profile: NativeGameProfile
        if format == 2 {
            guard signingStatus == "signed", let recipe else { throw invalid("Native package requires a signed container and a preparation recipe.") }
            guard contentVersion == recipe.contentVersion else { throw invalid("DLC content version differs from the preparation recipe.") }
            profile = try recipe.profile(profiles: profiles)
            guard preparedProfile == profile else { throw invalid("Prepared profile differs from its bounded recipe.") }
        } else {
            guard format == 1, recipe == nil,
                  let reviewed = NativeGameProfile.find(titleID: titleID, version: version, in: profiles) else {
                throw invalid(NativeGameProfile.unsupportedMessage)
            }
            profile = reviewed
        }
        guard version == profile.version, titleID == profile.titleID, enginePin == profile.enginePin,
              shaderChain == profile.shaderChain, shaderInputs == profile.shaderInputs,
              modules == profile.modules, environment == profile.environment,
              files["profile.tsv"]?.sha256 == profile.profileSHA256 else {
            throw invalid("Prepared package does not match the reviewed profile for \(profile.name) \(profile.version).")
        }
        return profile
    }

    /// True when a manifest passes `profile(in:)`, so Play can start the package without
    /// preparing it again. The package's files are checked at launch, not here.
    static func isCurrent(_ manifest: Data, profiles: [NativeGameProfile] = NativeGameProfile.all) -> Bool {
        guard let identity = try? JSONDecoder().decode(Self.self, from: manifest) else { return false }
        return (try? identity.profile(in: profiles)) != nil
    }
}

/// Versioned, bounded facts, never arbitrary environment or runtime profile text.
/// Defaults follow the pinned MIT nxnative game.py and profile_guess.py.
struct NativePreparationRecipe: Codable, Sendable {
    /// Numeric host observations from a boot with this recipe's original layout.
    /// No guest text, arbitrary profile keys or title-specific lookup is accepted.
    struct MemoryObservation: Codable, Sendable {
        let largestFailedHeap: UInt64
        let heapOpened: Bool
        let aliasMappedHighWater: UInt64
    }
    struct NRO: Codable, Sendable {
        let name: String
        let sha256: String
        let bytes: UInt64
        let buildID: String
    }
    let schema: Int
    let titleID: String
    let version: String
    let contentVersion: UInt32
    let name: String
    let modules: [String]
    let buildIDs: [String: String]
    let sdkVersions: [String]
    let systemResource: UInt64
    let usesHeap: Bool
    let largeBSS: UInt64
    var usedMemory: UInt64
    var nros: [NRO]
    var reviewedVersion: String?
    var memoryObservation: MemoryObservation? = nil

    static let enginePin = "f8a5618d5f505a4c0a0bcac7c413872d3a53c009"
    static let shaderChain = "26dc2fa9477c"
    // The integration patches applied to the pinned engine (scripts/deps/upstream/nxnative-queue/series).
    static let forkPatchCount = 29
    // The pinned loader sizes its tables from the inventory, including titles
    // with 1,432 NROs. Keep a bounded recipe without the former 128-module limit.
    static let maximumNROs = 4096
    static let environment = [
        "DM_LAUNCHER": "0", "DM_SAVED_CHOICES": "0", "DM_SAVE_SYNC": "0", "DM_FPS_TARGET": "0",
        "DM_RENDER_SCALE": "1", "DM_SHOW_FPS": "1", "DM_OPMODE": "1", "DM_PSO_PREPARE": "0",
        "DM_PSO_MTL4": "0", "DM_THERMAL_CAP": "1", "DM_BOOTPROF": "1", "DM_SLOWFRAME": "1",
        "DM_NVN_TSV": "frame-times.tsv", "DM_NVN_TSV_CALLS": "0", "DM_NVN_GPUPROF": "every:600",
        "DM_MEMSTAT": "memory.tsv"
    ]
    static func hex(_ value: String, count: Int) -> Bool {
        value.count == count && value.allSatisfy { "0123456789abcdefABCDEF".contains($0) }
    }
    static func hash(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    func validate() throws {
        func text(_ s: String, _ max: Int) -> Bool {
            !s.isEmpty && s.utf8.count <= max && !s.unicodeScalars.contains { CharacterSet.controlCharacters.contains($0) }
        }
        guard schema == 1, Self.hex(titleID, count: 16), titleID == titleID.uppercased(),
              UInt64(titleID, radix: 16) != 0, text(name, 512), text(version, 32),
              modules.first == "main", modules.count <= 8, Set(modules).count == modules.count,
              modules.allSatisfy({ $0 == "main" || $0 == "sdk" || ($0.hasPrefix("subsdk") && Int($0.dropFirst(6)).map { (0...9).contains($0) } == true) }),
              Set(buildIDs.keys) == Set(modules), buildIDs.values.allSatisfy({ Self.hex($0, count: 32) }),
              systemResource <= 0x10000000, systemResource % 0x1000 == 0,
              largeBSS <= 0xcd500000, usedMemory >= 0x4000000, usedMemory <= 0xcd500000,
              sdkVersions.count <= 32, sdkVersions.allSatisfy({ text($0, 64) && $0.allSatisfy { $0.isNumber || $0 == "_" } }),
              nros.count <= Self.maximumNROs, Set(nros.map(\.sha256)).count == nros.count,
              nros.enumerated().allSatisfy({ i, n in n.name == "nro\(i)" && Self.hex(n.sha256, count: 64) && Self.hex(n.buildID, count: 32) && n.bytes >= 0x80 && n.bytes <= 512 * 1024 * 1024 }) else {
            throw NSError(domain: "NativeRecipe", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid or unsupported native preparation recipe."])
        }
        if let observation = memoryObservation {
            guard observation.largestFailedHeap <= 0xcd500000,
                  observation.largestFailedHeap % 0x200000 == 0,
                  observation.aliasMappedHighWater <= (systemResource > 0 ? 0x118000000 : 0x10000000) else {
                throw NSError(domain: "NativeRecipe", code: 4, userInfo: [NSLocalizedDescriptionKey: "Invalid native memory observation."])
            }
        }
    }
    func reviewed(in profiles: [NativeGameProfile] = NativeGameProfile.all) throws -> NativeGameProfile? {
        try validate()
        guard let reviewedVersion else { return nil }
        guard let p = NativeGameProfile.find(titleID: titleID, version: reviewedVersion, in: profiles),
              p.modules == modules, p.buildIDs == buildIDs,
              (p.nroBuildIDs ?? [:]) == Dictionary(uniqueKeysWithValues: nros.map { ($0.name, $0.buildID) }) else {
            throw NSError(domain: "NativeRecipe", code: 2, userInfo: [NSLocalizedDescriptionKey: "Reviewed settings do not match these module builds."])
        }
        return p
    }
    var inventory: Data {
        Data(("# name\tsha256\tsize\n" + nros.map { "\($0.name)\t\($0.sha256)\t\($0.bytes)" }.joined(separator: "\n") + "\n").utf8)
    }
    func runtime(profiles: [NativeGameProfile] = NativeGameProfile.all) throws -> Data {
        if let p = try reviewed(in: profiles) {
            guard let url = Bundle.main.url(forResource: "NativeRuntimeProfiles", withExtension: "json"),
                  let values = try? JSONDecoder().decode([String: String].self, from: Data(contentsOf: url)),
                  let value = values[p.titleID + ":" + p.version], Self.hash(Data(value.utf8)) == p.profileSHA256 else {
                throw NSError(domain: "NativeRecipe", code: 3, userInfo: [NSLocalizedDescriptionKey: "The bundled reviewed runtime settings are missing or altered."])
            }
            return Data(value.utf8)
        }
        let pool: UInt64 = 0xcd500000
        let heap = usesHeap ? max(0x10000000, ((pool - min(pool, 0x4000000 + largeBSS) + 0xfffffff) / 0x10000000) * 0x10000000) : 0x10000000
        var reserve: UInt64 = systemResource > 0 ? 0x140000000 : 0x30000000 + heap
        var alias: UInt64 = systemResource > 0 ? 0x118000000 : 0x10000000
        var applicationPool = pool
        // Adapted from NX2Apple dm/games/profile_guess.py heap_signals (MIT).
        // A failed heap request sizes the next boot's partition before any guest
        // address is exposed. Never move a live heap or alias mapping.
        if let observation = memoryObservation {
            let heapCapacity = reserve - 0x20000000 - alias
            if observation.largestFailedHeap > heapCapacity {
                let requested = ((observation.largestFailedHeap + 0x7ffffff) / 0x8000000) * 0x8000000
                if reserve >= 0x60000000 + requested {
                    alias = reserve - 0x20000000 - requested
                } else {
                    alias = 0x40000000
                    reserve = 0x20000000 + alias + requested
                }
            } else if observation.largestFailedHeap == 0, !observation.heapOpened, systemResource > 0,
                      observation.aliasMappedHighWater > alias - 0x10000000,
                      usedMemory + largeBSS < 0x40000000 {
                // A virtual allocator consumes the advertised pool and also
                // reserves separate regions. Leave space for those regions.
                applicationPool = 0x40000000
            }
        }
        var values = ["title_id": titleID, "program_id": "0x" + titleID, "name": name,
            "romfs_marker": "", "modules": modules.joined(separator: " "),
            "mem_reserve": String(format: "0x%llx", reserve),
            "mem_alias": String(format: "0x%llx", alias), "mem_stack": "0x10000000",
            "app_pool": String(format: "0x%llx", applicationPool), "mem_used": String(format: "0x%llx", usedMemory),
            "sys_resource": String(format: "0x%llx", systemResource), "mem_map_limit": "0",
            "present_interval": "0", "core_preempt": "0", "alloc_big_align": "0", "fps_target": "0", "fps_max": "60",
            "fps_hold": "1", "present_snap": "0", "pso_mtl4": "0", "shared_font": "auto", "pad_layout": "0",
            "op_mode": "1", "perf_mode": "0", "feedback_snap": "", "tex_flush_upload": "0", "render_cond": "0",
            "tex_dirty_upload": "0", "tex_dyn_slots": "0", "gpu_timestamps": "0", "vconv_range": "0",
            "stdalloc_cap": "0", "tex_bc_cpu": "0", "ue_texture_pool": "0", "enc_thread": "0",
            "game_args": "", "game_env": "", "device_api": "0.0", "shader_key_skip": "0x0+0x0"]
        values["display_version"] = buildIDs["main"]! + "\t" + version
        return Data(("# Experimental Satchel profile, nxnative defaults, recipe 1\n" + values.keys.sorted().map { "\($0)\t\(values[$0]!)" }.joined(separator: "\n") + "\n").utf8)
    }
    func profile(profiles: [NativeGameProfile] = NativeGameProfile.all) throws -> NativeGameProfile {
        let override = try reviewed(in: profiles)
        let runtime = try runtime(profiles: profiles)
        return NativeGameProfile(titleID: titleID, version: version, name: name, packageName: titleID + "-" + String(contentVersion),
            phonePrepare: true, enginePin: Self.enginePin, shaderChain: Self.shaderChain, shaderInputs: "runtime-capture-v1",
            profileSHA256: Self.hash(runtime), modules: modules,
            nroInventorySHA256: nros.isEmpty ? nil : Self.hash(inventory),
            nroBuildIDs: Dictionary(uniqueKeysWithValues: nros.map { ($0.name, $0.buildID) }), buildIDs: buildIDs,
            environment: override?.environment ?? Self.environment,
            requiredFiles: ["game.dylib", "profile.tsv", "romfs.img", "dm/shader_map.tsv", "dm/pso_manifest.tsv", "dm/c1_full.tsv", "msl/b-\(Self.shaderChain)/inputs.tsv"])
    }
}
