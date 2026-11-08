import Foundation

// Stand-ins for NativePrepare's device-only pieces (the rewriter pipeline and the OpenSSL
// signer), so the game preview (build-preview.zsh game) shows the real Prepare, signing and
// prepared-game screens. Nothing here reads keys, games or certificates.

struct PrepareFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

final class PrepareCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    private var finalizing = false
    @discardableResult func cancel() -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard !finalizing else { return false }
        value = true; return true
    }
    /// Cancellation and the final cleanup commit have one ordering, including task expiration.
    func beginFinalizing() throws {
        lock.lock(); defer { lock.unlock() }
        if value { throw CancellationError() }
        finalizing = true
    }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func check() throws { if cancelled { throw CancellationError() } }
}

enum PrepareSigningState: String { case needsSignature = "needs signature", signed }

protocol PreparedGameSigner {
    func sign(_ dylib: URL, token: PrepareCancellation) throws -> PrepareSigningState
}

struct PhonePreparedGameSigner: PreparedGameSigner {
    let identity: SatchelSigningIdentity
    func sign(_ dylib: URL, token: PrepareCancellation) throws -> PrepareSigningState { .signed }
}

enum PreparePipeline {
    static let title = "0100F2C0115B6000"
    static func requireSupportedBase(_ base: URL, keys: [String: Data], cancelled: () -> Bool = { false }) throws {}

    /// Walks the real steps with the real progress model and no work, so the progress screens
    /// can be captured. -SatchelPreviewPrepareEnd signed|unsigned|failed picks the ending
    /// (failed by default); -SatchelPreviewPrepareSeconds sets the length (40 by default). A
    /// signed or unsigned ending writes a fictional manifest, with no game files. Without reviewed
    /// settings (the developer test package) it writes a current starter manifest for the chosen
    /// title, marked as a test package as the real pipeline marks it.
    static func run(base: URL, update: URL?, addOns: [URL] = [], keys: [String: Data], shaderPack: URL? = nil,
                    destination: URL, applicationID: UInt64? = nil, signer: PreparedGameSigner, token: PrepareCancellation,
                    allowReviewedProfiles: Bool = true, details: ((PrepareProgressSnapshot) -> Void)? = nil,
                    progress: @escaping (String, Double?) -> Void) throws -> PrepareSigningState {
        let reporter = PrepareProgress { value in details?(value); progress(value.message, value.overallFraction) }
        let defaults = UserDefaults.standard
        let seconds = defaults.double(forKey: "SatchelPreviewPrepareSeconds") > 0 ? defaults.double(forKey: "SatchelPreviewPrepareSeconds") : 40
        let ending = defaults.string(forKey: "SatchelPreviewPrepareEnd") ?? "failed"
        let ticks = 20
        for step in PrepareStep.allCases {
            let total: UInt64 = [PrepareStep.romfs, .shaders, .rewriting].contains(step) ? 2_400_000_000 : 0
            for tick in 0...ticks {
                try token.check()
                if ending == "failed" && step == .shaders && tick == ticks / 2 {
                    throw PrepareFailure(message: "Preview: the shader pack does not match this game version.")
                }
                let done = UInt64(Double(total) * Double(tick) / Double(ticks))
                reporter.update(step, done, total, fraction: Double(tick) / Double(ticks))
                Thread.sleep(forTimeInterval: seconds * step.weight / 100 / Double(ticks))
            }
        }
        let fm = FileManager.default
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        var manifest: [String: Any] = ["format": 1, "titleID": "0100F2C0115B6000", "version": "1.4.3",
                                       "signingStatus": ending == "signed" ? "signed" : "needs signature",
                                       "contentVersion": 0, "buildIDs": [String: String]()]
        if !allowReviewedProfiles {
            guard shaderPack == nil else { throw PrepareFailure(message: "Preview: a test package takes no shader pack.") }
            manifest = PreviewManifest.make(titleID: applicationID ?? 0x0100F2C0115B6000, version: "1.2.0", name: "Synthetic test package")
            manifest["testPackage"] = true
            if ending != "signed" { manifest["signingStatus"] = "needs signature" }
        }
        try JSONSerialization.data(withJSONObject: manifest).write(to: destination.appendingPathComponent("manifest.json"))
        return ending == "signed" ? .signed : .needsSignature
    }
}

/// A fictional identity chosen by -SatchelPreviewIdentity YES|expiring|expired (none otherwise).
final class SatchelSigningIdentity: NSObject {
    let certificateName: String
    let team: String
    let expiry: Date
    private static var stored: SatchelSigningIdentity? = {
        let days: Double
        switch UserDefaults.standard.string(forKey: "SatchelPreviewIdentity") {
        case "YES": days = 214
        case "expiring": days = 12
        case "expired": days = -3
        default: return nil
        }
        return SatchelSigningIdentity(name: "Apple Development: Preview Developer (PREVIEW01)", expiry: Date().addingTimeInterval(days * 86400))
    }()

    private init(name: String, expiry: Date) {
        certificateName = name; team = "PREVIEWTM0"; self.expiry = expiry
    }

    static var applicationTeam: String? { "PREVIEWTM0" }

    init(pkcs12 data: Data, passphrase: String, team: String) throws {
        throw PrepareFailure(message: "Preview: identities cannot be imported here.")
    }

    static func storedIdentity() throws -> SatchelSigningIdentity {
        guard let stored else { throw PrepareFailure(message: "No signing identity is stored on this device.") }
        return stored
    }

    func store() throws { Self.stored = self }
    static func removeStoredIdentity() throws { stored = nil }
}

enum DevScreenFixture {
    /// Fictional manifests (no game files) covering each status the prepared-games list shows.
    static func seedPreparedGames() {
        let fm = FileManager.default
        let root = fm.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames", isDirectory: true)
        let base: [String: Any] = ["format": 1, "titleID": "0100F2C0115B6000", "version": "1.4.3"]
        let packages: [String: [String: Any]?] = [
            "TOTK-1.4.3": ["signingStatus": "signed"],
            "TOTK-1.4.3-2": ["signingStatus": "signed", "contentVersion": 0, "buildIDs": [String: String]()],
            "TOTK-1.4.3-3": ["signingStatus": "needs signature", "contentVersion": 0, "buildIDs": [String: String]()],
            "TOTK-1.4.3-src": ["signingStatus": "signed", "shaderSource": ["format": 1]],
            "TOTK-copy-partial": nil,
        ]
        for (name, extra) in packages {
            let folder = root.appendingPathComponent(name, isDirectory: true)
            try? fm.removeItem(at: folder)
            try? fm.createDirectory(at: folder, withIntermediateDirectories: true)
            guard let extra else { continue }
            let manifest = base.merging(extra) { $1 }
            try? JSONSerialization.data(withJSONObject: manifest).write(to: folder.appendingPathComponent("manifest.json"))
        }
    }
}


enum PreviewPackage {
    static func verifyWarmup(root: URL, cancelled: () -> Bool) throws -> URL {
        if cancelled() { throw CancellationError() }
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupTests"),
              root.lastPathComponent == "LanternDrift-1.2.0",
              root.deletingLastPathComponent() == NavigationFixFixture.documents.appendingPathComponent("PreparedGames") else {
            throw PrepareFailure(message: "Only the named synthetic warm-up package can be checked in this fixture.")
        }
        return root
    }

    /// The full check Prepare runs before its cleanup, for the manifest-only package the
    /// preview pipeline just wrote. Enabled only by -SatchelPreviewPrepareTests.
    static func verifyPrepared(root: URL, cancelled: () -> Bool) throws -> URL {
        if cancelled() { throw CancellationError() }
        let games = NavigationFixFixture.documents.appendingPathComponent("PreparedGames").standardizedFileURL.path
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewPrepareTests"), root.lastPathComponent.hasPrefix("Prepared-"),
              root.standardizedFileURL.deletingLastPathComponent().path == games,
              FileManager.default.fileExists(atPath: root.appendingPathComponent("manifest.json").path) else {
            throw PrepareFailure(message: "Only an explicitly enabled synthetic Prepare can pass this fixture's check.")
        }
        return root
    }

    static func verify(root: URL) throws -> URL {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewSessionTests"),
              ["LanternDrift-1.2.0", "Navigation-Orphan"].contains(root.lastPathComponent),
              root.deletingLastPathComponent().lastPathComponent == "PreparedGames" else {
            throw PrepareFailure(message: "Only the two synthetic session packages can launch in this fixture.")
        }
        return root
    }
}


enum PrepareDiscovery {
    struct Selection {
        let titleID: UInt64
        let name: String
        let version: String
        let update: URL?
    }
    static func select(base: URL, update: URL?, keys: [String: Data], titleID: UInt64?, token: PrepareCancellation) throws -> Selection {
        try token.check()
        try OnboardingDLCFixture.prepareError()
        return Selection(titleID: titleID ?? 0x0100F2C0115B6000, name: "Synthetic preparation", version: "1.0", update: update)
    }
}

// Navigation drives the production job with synthetic progress, never shaders.
enum WarmupRunner {
    static func bounded(_ url: URL, maximum: Int) throws -> Data {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let data = try handle.read(upToCount: maximum + 1) ?? Data()
        guard data.count <= maximum else { throw PrepareFailure(message: "Synthetic warm-up metadata is oversized.") }
        return data
    }
    static func run(package: URL, runtime: URL, cancelled: @escaping () -> Bool, captureDirectory: URL? = nil,
                    execution: () -> SatchelWarmupPolicy = { .conservative }, expired: () -> Bool = { false },
                    progress: (WarmupProgress) -> Void) throws -> WarmupProgress {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupTests") else {
            throw PrepareFailure(message: "Shader warm-up cannot run in the navigation fixture.")
        }
        let defaults = UserDefaults.standard
        defaults.set(defaults.integer(forKey: "SatchelPreviewWarmupStarts") + 1, forKey: "SatchelPreviewWarmupStarts")
        var state = WarmupProgress(stage: "Synthetic shader work", completed: 1, total: 2)
        progress(state)
        let deadline = Date().addingTimeInterval(60)
        while !UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupFinish") {
            if cancelled() {
                while defaults.bool(forKey: "SatchelPreviewWarmupHoldCancellation") && !defaults.bool(forKey: "SatchelPreviewWarmupRelease") {
                    guard Date() < deadline else { throw PrepareFailure(message: "Synthetic cancellation acknowledgement timed out.") }
                    Thread.sleep(forTimeInterval: 0.02)
                }
                throw CancellationError()
            }
            guard Date() < deadline else { throw PrepareFailure(message: "Synthetic warm-up timed out.") }
            Thread.sleep(forTimeInterval: 0.05)
        }
        if UserDefaults.standard.string(forKey: "SatchelPreviewWarmupEnd") == "failed" {
            throw PrepareFailure(message: "Synthetic shader failure.")
        }
        state.completed = 2; state.state = "complete"; state.phase = 3
        state.stage = "Known shaders warmed. New shaders still build while playing."
        progress(state)
        return state
    }
}
