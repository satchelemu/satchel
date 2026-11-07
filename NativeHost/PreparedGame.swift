import CryptoKit
import SwiftUI

private struct PreparedManifest: Decodable {
    struct FileRecord: Decodable { let bytes: UInt64; let sha256: String }
    let addOnContent: [PreparedAddOn]?
    let contentVersion: UInt32?
    let titleID: String
    let modules: [String]
    let files: [String: FileRecord]
}

/// Serialize publication with launch and package removal, including their asynchronous work.
enum PreparedPackageAccess {
    private static let lock = NSLock()
    private static var busy = false
    static func begin() throws {
        lock.lock(); defer { lock.unlock() }
        guard !busy else { throw NSError(domain: "PreparedPackage", code: 3, userInfo: [NSLocalizedDescriptionKey: "A prepared package is in use. Wait for the current operation to finish."]) }
        busy = true
    }
    static func end() { lock.lock(); busy = false; lock.unlock() }
}

enum PreparedPackage {
    /// The verify-once stamp for a package folder.
    static func stampURL(for root: URL) -> URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PreparedVerified-" + SHA256.hash(data: Data(root.standardizedFileURL.path.utf8)).map { String(format: "%02x", $0) }.joined() + ".json")
    }

    /// Removes one package folder from Documents/PreparedGames and its verify stamp. Saves live in
    /// Documents/NativeRuntime/<title> and are not touched.
    static func delete(named name: String) throws {
        let fm = FileManager.default
        func refuse(_ message: String) -> NSError {
            NSError(domain: "PreparedPackage", code: 2, userInfo: [NSLocalizedDescriptionKey: message])
        }
        guard !name.isEmpty, !name.contains("/"), name != ".", name != ".." else { throw refuse("Not a package name: \(name)") }
        try PreparedPackageAccess.begin(); defer { PreparedPackageAccess.end() }
        guard !SatchelNativeSessionStarted(), PreparedLaunch.current == nil else { throw refuse("A game is running. Close and reopen the app, then delete.") }
        let parent = fm.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames", isDirectory: true)
        let root = parent.appendingPathComponent(name, isDirectory: true)
        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: root.path, isDirectory: &isDirectory), isDirectory.boolValue else { throw refuse("No package named \(name).") }
        try fm.removeItem(at: root)
        try? fm.removeItem(at: stampURL(for: root))
    }

    /// After a successful Prepare: checks `root` with the launch verifier again, then removes
    /// every other package folder of the same title with `delete(named:)`. Folders whose
    /// manifest cannot be read and those in `keeping` (in use by a running job) are left alone.
    /// Throws, removing nothing, when `root` does not verify. Returns one line per folder that
    /// could not be removed.
    static func removeReplaced(by root: URL, keeping: Set<String> = [], profiles: [NativeGameProfile] = NativeGameProfile.all,
                               cancelled: () -> Bool = { false }) throws -> [String] {
        _ = try verify(root: root, profiles: profiles, cancelled: cancelled)
        let fm = FileManager.default
        let parent = fm.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames", isDirectory: true)
        func titleID(_ folder: URL) -> String? {
            let url = folder.appendingPathComponent("manifest.json")
            guard let size = (try? fm.attributesOfItem(atPath: url.path))?[.size] as? NSNumber, size.intValue <= 32 * 1024 * 1024,
                  let data = try? Data(contentsOf: url),
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let id = object["titleID"] as? String, !id.isEmpty else { return nil }
            return id.uppercased()
        }
        guard root.standardizedFileURL.deletingLastPathComponent().path == parent.standardizedFileURL.path,
              let title = titleID(root) else {
            throw NSError(domain: "PreparedPackage", code: 2, userInfo: [NSLocalizedDescriptionKey: "Not a package in Documents/PreparedGames."])
        }
        var failures: [String] = []
        let folders = try fm.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)
        for folder in folders.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
            let name = folder.lastPathComponent
            guard name != root.lastPathComponent, !keeping.contains(name), titleID(folder) == title else { continue }
            do { try delete(named: name) } catch { failures.append(name + ": " + error.localizedDescription) }
        }
        return failures
    }
}

/// Dev hook so the Mac can remove packages with no taps:
/// devicectl device process launch ... com.dell.satchel.dev -- -SatchelDeletePreparedGames "A,B"
/// The result goes to Documents/delete-result.txt.
enum PreparedGameCleanup {
    /// Removes one prepared package (the library's Remove from iPhone). Saves are kept.
    static func delete(named name: String) throws { try PreparedPackage.delete(named: name) }

    static func runRequested() {
        guard let list = UserDefaults.standard.string(forKey: "SatchelDeletePreparedGames"), !list.isEmpty else { return }
        let names = list.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        let lines = names.map { name -> String in
            do { try PreparedPackage.delete(named: name); return "deleted \(name)" }
            catch { return "failed \(name): \(error.localizedDescription)" }
        }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try? (lines.joined(separator: "\n") + "\n").write(to: documents.appendingPathComponent("delete-result.txt"), atomically: true, encoding: .utf8)
    }
}

extension PreparedPackage {
    static func verify(root: URL, profiles: [NativeGameProfile] = NativeGameProfile.all,
                       forceFull: Bool = false, cancelled: () -> Bool = { false }) throws -> URL {
        func checkCancellation() throws { if cancelled() { throw CancellationError() } }
        try checkCancellation()
        let fm = FileManager.default
        let documents = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        func invalid(_ message: String) -> NSError {
            NSError(domain: "PreparedPackage", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
        }
        let manifestURL = root.appendingPathComponent("manifest.json")
        guard fm.fileExists(atPath: manifestURL.path) else {
            throw invalid("Prepared game not found. Transfer the complete package to Documents/PreparedGames, then try again.")
        }
        let attrs = try fm.attributesOfItem(atPath: manifestURL.path)
        guard let size = attrs[.size] as? NSNumber, size.intValue <= 32 * 1024 * 1024 else {
            throw invalid("Prepared manifest is too large.")
        }
        let manifestData = try Data(contentsOf: manifestURL)
        let manifest = try JSONDecoder().decode(PreparedManifest.self, from: manifestData)
        // Signing, recipe and engine identity: the same check the library uses to mark a
        // package made by another engine as needing Prepare again (PreparedIdentity).
        let profile = try JSONDecoder().decode(PreparedIdentity.self, from: manifestData).profile(in: profiles)
        guard manifest.files.count < 250_000 else {
            throw invalid("Prepared package does not match the reviewed profile for \(profile.name) \(profile.version).")
        }
        let nros = profile.nroBuildIDs ?? [:]
        if let inventory = profile.nroInventorySHA256 {
            guard !nros.isEmpty, manifest.files["dm/nro.tsv"]?.sha256 == inventory else {
                throw invalid("NRO inventory does not match the reviewed profile.")
            }
        } else if manifest.files["dm/nro.tsv"] != nil || !nros.isEmpty {
            throw invalid("This prepared package format does not support NRO modules without a reviewed inventory.")
        }
        let allModules = manifest.modules + nros.keys.sorted()
        let required = profile.requiredFiles
            + allModules.flatMap { ["dm/\($0).fixups.tsv", "dm/\($0).gates.tsv"] }
        guard required.allSatisfy({ manifest.files[$0] != nil }) else { throw invalid("Prepared package is missing a required binding.") }
        try PreparedAddOn.validate(manifest.addOnContent ?? [], titleID: manifest.titleID,
            contentVersion: manifest.contentVersion, root: root,
            files: manifest.files.mapValues { (bytes: $0.bytes, sha256: $0.sha256) })
        let canonicalRoot = root.resolvingSymlinksInPath().path + "/"
        // A full hash of about 20 GB takes minutes, so it runs once. Later launches only
        // check that every bound file still has the size and modification time recorded
        // after that successful pass, and the stamp is bound to this exact manifest.
        let manifestDigest = SHA256.hash(data: try Data(contentsOf: manifestURL)).map { String(format: "%02x", $0) }.joined()
        let stampURL = stampURL(for: root)
        func fingerprint(_ file: URL) throws -> [String] {
            let values = try file.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey])
            return [String(values.fileSize ?? -1), String(values.contentModificationDate?.timeIntervalSince1970 ?? -1)]
        }
        var cached = false
        if !forceFull, let stampData = try? Data(contentsOf: stampURL),
           let stamp = try? JSONDecoder().decode([String: [String]].self, from: stampData),
           stamp["manifest.json"] == [manifestDigest], stamp.count == manifest.files.count + 1,
           try manifest.files.keys.allSatisfy({ try stamp[$0] == fingerprint(root.appendingPathComponent($0)) }) {
            cached = true
        }
        if !cached { try? fm.removeItem(at: stampURL) }
        var verified = ["manifest.json": [manifestDigest]]
        // Progress the Mac can read with devicectl while the phone hashes the package.
        let progressURL = documents.appendingPathComponent("verify-progress.json")
        let totalBytes = try manifest.files.values.reduce(UInt64(0)) { total, record in
            let (sum, overflow) = total.addingReportingOverflow(record.bytes)
            guard !overflow else { throw invalid("Prepared package size exceeds limit.") }
            return sum
        }
        var doneBytes: UInt64 = 0, doneFiles = 0, lastReport = Date.distantPast
        let started = Date()
        func report(_ state: String) {
            let elapsed = Date().timeIntervalSince(started)
            let line = "{\"state\":\"\(state)\",\"files\":\(doneFiles),\"of\":\(manifest.files.count),\"bytes\":\(doneBytes),\"total\":\(totalBytes),\"seconds\":\(Int(elapsed))}"
            try? Data(line.utf8).write(to: progressURL, options: .atomic)
            lastReport = Date()
        }
        for (name, record) in manifest.files.sorted(by: { $0.key < $1.key }) {
            try checkCancellation()
            let parts = name.split(separator: "/", omittingEmptySubsequences: false)
            guard !name.hasPrefix("/"), !parts.contains(".."), !parts.contains("."), !parts.contains(""),
                  record.sha256.count == 64, record.sha256.allSatisfy({ "0123456789abcdef".contains($0) }),
                  record.bytes <= 128 * 1024 * 1024 * 1024 else { throw invalid("Invalid prepared package path or digest.") }
            let file = root.appendingPathComponent(name)
            guard file.resolvingSymlinksInPath().path.hasPrefix(canonicalRoot) else { throw invalid("Prepared package path escapes its directory.") }
            let values = try file.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
            guard values.isRegularFile == true, values.isSymbolicLink != true,
                  UInt64(values.fileSize ?? 0) == record.bytes else { throw invalid("Prepared file size mismatch: \(name)") }
            if cached { continue }
            let handle = try FileHandle(forReadingFrom: file)
            var hash = SHA256()
            do {
                // Each chunk is autoreleased; without a pool per chunk a 17 GB file kept every
                // chunk alive and the app reached its 8 GB memory limit (jetsam highwater).
                var done = false
                while !done {
                    try autoreleasepool {
                        try checkCancellation()
                        guard let chunk = try handle.read(upToCount: 4 * 1024 * 1024), !chunk.isEmpty else { done = true; return }
                        hash.update(data: chunk)
                    }
                }
                try handle.close()
            } catch { try? handle.close(); throw error }
            let actual = hash.finalize().map { String(format: "%02x", $0) }.joined()
            guard actual == record.sha256 else { throw invalid("Prepared file digest mismatch: \(name)") }
            verified[name] = try fingerprint(file)
            doneBytes += record.bytes; doneFiles += 1
            if Date().timeIntervalSince(lastReport) > 2 { report("hashing") }
        }
        // No unbound library or metadata may be discovered by the host's directory scans.
        // Path-based enumeration yields names relative to the package. URL enumeration can
        // return /private/var paths for a /var root, which garbled every relative name.
        report("scanning")
        guard let enumerator = fm.enumerator(atPath: root.path) else { throw invalid("Prepared package cannot be listed.") }
        while let name = enumerator.nextObject() as? String {
            try checkCancellation()
            let type = enumerator.fileAttributes?[.type] as? FileAttributeType
            guard type != .typeSymbolicLink else { throw invalid("Symlinks are not allowed in a prepared package.") }
            if type == .typeRegular {
                guard name == "manifest.json" || manifest.files[name] != nil else { throw invalid("Unbound prepared file: \(name)") }
            }
        }
        for module in allModules {
            let url = root.appendingPathComponent("dm/\(module).fixups.tsv")
            guard (try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? Int.max) < 64 * 1024 else {
                throw invalid("Module layout exceeds limit.")
            }
            let lines = try String(contentsOf: url, encoding: .utf8).split(separator: "\n")
            var builds: [String] = []
            for line in lines {
                let fields = line.split(whereSeparator: { $0.isWhitespace }).map(String.init)
                if fields.count == 3 && fields[0] == "#" && fields[1] == "BUILD_ID" { builds.append(fields[2]) }
            }
            guard builds.count == 1, builds[0].lowercased() == (profile.buildIDs[module] ?? nros[module])?.lowercased() else {
                throw invalid("Module build does not match the reviewed profile: \(module).")
            }
        }
        try checkCancellation()
        report("verified")
        try? fm.createDirectory(at: stampURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        if !cached { try? JSONEncoder().encode(verified).write(to: stampURL, options: .atomic) }
        return root
    }
}

/// The package currently owned by the native session coordinator.
enum PreparedLaunch {
    static var current: String?

    // Automation is a request for this process only. Persisted developer
    // defaults must not reopen a game after iOS terminates the app.
    static func launchFlag(_ name: String, arguments: [String] = ProcessInfo.processInfo.arguments) -> Bool {
        guard let index = arguments.lastIndex(of: "-" + name), index + 1 < arguments.count else { return false }
        return ["yes", "true", "1"].contains(arguments[index + 1].lowercased())
    }
}

/// The background time around the package check, ended once. Main thread only.
private final class CheckKeepalive: @unchecked Sendable {
    var identifier: UIBackgroundTaskIdentifier = .invalid
    func end() {
        if identifier != .invalid { UIApplication.shared.endBackgroundTask(identifier); identifier = .invalid }
    }
}

struct PreparedGameView: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var scenePhase
    let packageURL: URL
    @State private var launchRequested = UserDefaults.standard.bool(forKey: "SatchelDeveloperControls") && PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame")
    @State private var ready: Bool
    @State private var failure: String?

    init(packageURL: URL) {
        self.packageURL = packageURL
        if NativePreparedCycle.shouldLaunch(packageURL.lastPathComponent) { _launchRequested = State(initialValue: true) }
        _ready = State(initialValue: SatchelNativeSessionStarted() && PreparedLaunch.current == packageURL.lastPathComponent)

    }

    // Player settings are kept per title, so read it from the package being opened.
    private var titleID: String {
        let manifest = packageURL.appendingPathComponent("manifest.json")
        guard let data = try? Data(contentsOf: manifest),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let id = object["titleID"] as? String else { return "" }
        return id
    }

    var body: some View {
        Group {
            if failure != nil && !ready {
                PreparedVerifyingView(titleID: titleID, package: packageURL.lastPathComponent, failure: failure) { dismiss() }
            } else if !launchRequested && !ready {
                PlayerSettingsView(titleID: titleID, package: packageURL.lastPathComponent, addOns: PreparedAddOn.read(from: packageURL)) { launchRequested = true }
            } else if ready {
                NativeGameSurface(titleID: titleID).ignoresSafeArea()
            } else {
                PreparedVerifyingView(titleID: titleID, package: packageURL.lastPathComponent, failure: failure) { dismiss() }
            }
        }
        .interactiveDismissDisabled()
        .onReceive(NotificationCenter.default.publisher(for: .SatchelMenuDidChange)) { _ in
            if SatchelMenuState()["state"] as? String == "library" { dismiss() }
        }
        .onReceive(NotificationCenter.default.publisher(for: .SatchelNativeSessionDidEnd)) { notification in
            let ended = notification.userInfo?["package"] as? String
            if PreparedLaunch.current == ended { PreparedLaunch.current = nil }
            SatchelMenu.uninstall()
            if ended == packageURL.lastPathComponent { UIApplication.shared.isIdleTimerDisabled = false; dismiss() }
        }
        .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
        .task(id: launchRequested) {
            guard launchRequested, !ready, failure == nil else { return }
            // The first check reads the whole package and can take minutes; keep it running
            // for the time iOS gives a backgrounded app instead of suspending at once.
            let keepalive = CheckKeepalive()
            keepalive.identifier = UIApplication.shared.beginBackgroundTask(withName: "Check prepared game") { keepalive.end() }
            defer { keepalive.end() }
            do {
                guard !AddOnJob.shared.running else { throw PrepareFailure(message: "Wait for Add DLC to finish, or cancel it before playing.") }
                try PreparedPackageAccess.begin(); defer { PreparedPackageAccess.end() }
                WarmupJob.shared.prepareForPlay()
                defer { WarmupJob.shared.playRequestFinished() }
                if SatchelNativeSessionStarted() && PreparedLaunch.current != packageURL.lastPathComponent {
                    try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                        SatchelStopNativeSession { error in
                            if let error { continuation.resume(throwing: error) }
                            else { continuation.resume() }
                        }
                    }
                }
                NativeGameReport.record(package: packageURL.lastPathComponent, titleID: titleID, stage: "verify", result: "running", message: "Verifying all package bindings.")
                let package = try await Task.detached(priority: .userInitiated) { try PreparedPackage.verify(root: packageURL) }.value
                // A starter profile package reads its previous launch's memory signals here, and
                // uses learned settings once a generic rule changes its profile (NativeBootFeedback).
                let learned = await Task.detached(priority: .userInitiated) { NativeBootFeedback.prepareLaunch(packageRoot: package) }.value
                NativeGameReport.record(package: packageURL.lastPathComponent, titleID: titleID, stage: "boot", result: "starting",
                                        message: learned == nil ? "Loading the verified signed native container." : "Loading the verified signed native container with learned memory settings.")
                try await Task.detached(priority: .userInitiated) {
                    var error: NSError?
                    guard SatchelPrepareNativeSessionWithProfile(package.path, learned?.path, &error) else {
                        throw error ?? NSError(domain: "SatchelNativeHost", code: 1,
                                               userInfo: [NSLocalizedDescriptionKey: "Native session preparation failed."])
                    }
                }.value
                PreparedLaunch.current = packageURL.lastPathComponent
                SatchelMenuBeginSession()
                ready = true
            } catch {
                failure = error.localizedDescription
                NativePreparedCycle.launchFailed(packageURL.lastPathComponent, error: error)
                var report = NativeGameReport.load(package: packageURL.lastPathComponent, titleID: titleID)
                report.result = "failed"; report.message = error.localizedDescription; try? report.save()
                // The Mac reads this file with devicectl, since screen capture can come back black.
                let progressURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                    .appendingPathComponent("verify-progress.json")
                if let data = try? JSONSerialization.data(withJSONObject: ["state": "failed", "message": error.localizedDescription]) {
                    try? data.write(to: progressURL, options: .atomic)
                }
            }
        }
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active: SatchelNativeFocusEvent(2); SatchelNativeFocusEvent(3)
            case .inactive: SatchelNativeFocusEvent(0)
            case .background: SatchelNativeFocusEvent(1)
            @unknown default: break
            }
        }
    }
}

/// The wait between Play and the game: the package check, then session setup. A failure
/// gets a close button in the bar; while checking, the screen cannot be left.
struct PreparedVerifyingView: View {
    let titleID: String
    let package: String
    let failure: String?
    let close: () -> Void
    @State private var started = Date()
    @State private var fraction: Double?

    private var gameName: String { NativeGameProfile.find(titleID: titleID)?.name ?? package }

    var body: some View {
        NavigationStack {
            ScreenReader { cut, _ in
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        VStack(alignment: .leading, spacing: 6) {
                            SatchelEyebrow(text: failure == nil ? "Getting ready" : "Could not start",
                                          tint: failure == nil ? SatchelTheme.tide : SatchelTheme.ember)
                            Text(failure == nil ? "Verifying prepared game" : "This game did not start")
                                .font(SatchelTheme.display(26))
                            HStack(spacing: 8) {
                                Text(gameName).font(.system(size: 15, weight: .bold, design: .rounded)).lineLimit(2)
                                SatchelChip(text: package, systemImage: "shippingbox.fill")
                            }
                        }
                        if let failure {
                            SatchelPanel(title: "What happened", tint: SatchelTheme.ember) {
                                Text(failure).font(.system(size: 14, weight: .semibold, design: .rounded))
                                    .fixedSize(horizontal: false, vertical: true)
                                    .accessibilityIdentifier("verify-failure")
                                Button(action: close) {
                                    SatchelButtonLabel(title: "Back to prepared games", systemImage: "chevron.left", kind: .secondary, height: 42)
                                }
                                .buttonStyle(PressableStyle())
                            }
                        } else {
                            SatchelPanel {
                                SatchelProgressBar(value: fraction, tint: SatchelTheme.tide)
                                Text(fraction.map { "Checking files, \(Int($0 * 100))%" } ?? "Checking the game and its shaders")
                                    .font(.system(size: 14, weight: .semibold, design: .rounded))
                                    .accessibilityIdentifier("verify-status")
                                Text("The first check reads the whole package and can take several minutes. Later launches are quick.")
                                    .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                    }
                    .frame(maxWidth: 560, alignment: .leading)
                    .padding(.vertical, 24)
                    .frame(maxWidth: .infinity)
                }
                .scrollIndicators(.hidden)
                .scrollClipDisabled()
                .cutoutPadding(cut, leading: 24, trailing: 24)
            }
            .background { SatchelBackdrop(glow: failure == nil ? SatchelTheme.tide : SatchelTheme.ember) }
            .toolbar {
                if failure != nil {
                    ToolbarItem(placement: .confirmationAction) {
                        Button { Haptics.tick(); close() } label: { Label("Back", systemImage: "xmark") }
                            .satchelBarButton()
                            .accessibilityIdentifier("close-verify")
                    }
                }
            }
            .satchelBars()
            .satchelMenuStatus()
        }
        .task(id: failure == nil) {
            // The verifier reports hashing progress in Documents/verify-progress.json (for the
            // Mac); a file older than this screen is from an earlier launch and is ignored.
            let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("verify-progress.json")
            while failure == nil && !Task.isCancelled {
                if let date = try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate,
                   date >= started, let data = try? Data(contentsOf: url),
                   let row = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                   let done = row["bytes"] as? Double, let total = row["total"] as? Double, total > 0 {
                    withAnimation(SatchelTheme.glide) { fraction = min(1, done / total) }
                }
                try? await Task.sleep(for: .seconds(1))
            }
        }
    }
}

private struct NativeGameSurface: UIViewControllerRepresentable {
    let titleID: String
    func makeUIViewController(context: Context) -> UIViewController {
        let controller = SatchelNativeGameController()
        SatchelMenuSurfaceAttached(true)
        SatchelMenu.install(titleID: titleID) // Satchel's SwiftUI pause menu replaces the plain one
        return controller
    }
    func updateUIViewController(_ controller: UIViewController, context: Context) {}
    static func dismantleUIViewController(_ controller: UIViewController, coordinator: ()) { SatchelMenuSurfaceAttached(false) }
}


private struct PreparedGameChoice: Identifiable, Sendable {
    enum Status: Sendable { case ready, needsSignature, incomplete, unsupported }
    let url: URL
    let name: String
    let titleID: UInt64?
    let version: String?
    let detail: String
    let status: Status
    /// Nil when the manifest could not be read.
    let preparedOnPhone: Bool?
    /// Ships Metal shader source instead of prebuilt libraries (a shader test package).
    let shaderSource: Bool
    let date: Date?
    var supported: Bool { status == .ready }
    var id: String { url.lastPathComponent }
}

struct PreparedGamesView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    /// A package (folder name) to open as soon as the list loads, such as one just prepared.
    var launch: String? = nil
    @Environment(\.dismiss) private var dismiss
    @State private var games: [PreparedGameChoice] = []
    @State private var selected: PreparedGameChoice?
    @State private var loading = true
    @State private var failure: String?
    @State private var pendingDelete: PreparedGameChoice?
    @State private var reportGame: PreparedGameChoice?
    @State private var deleting: String?

    var body: some View {
        PanelChrome(closeTitle: "Library", identifier: "close-prepared-games", close: { dismiss() }) {
            ScreenReader { cut, geometry in
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: the heading on the first page, the packages on the second.
                        FoldSplit(fold: fold, cut: cut) {
                            header(reserve: 0).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
                        } second: {
                            listScroll(columns: 1, reserve: fold.isVertical ? reserve : 0, showHeader: false)
                        }
                    } else {
                        let width = geometry.size.width - cut.leading - cut.trailing - 40
                        listScroll(columns: width > 820 ? 2 : 1, reserve: reserve, showHeader: true)
                            .cutoutPadding(cut, leading: 20, trailing: 20)
                    }
                }
            }
            .background { SatchelBackdrop(glow: SatchelTheme.leaf) }
            .satchelMenuStatus()
        }
        .fullScreenCover(item: $selected) { PreparedGameView(packageURL: $0.url) }
        .sheet(item: $reportGame) { NativeGameReportView(package: $0.id, titleID: $0.titleID.map { String(format: "%016llX", $0) } ?? "") }
        .confirmationDialog(pendingDelete.map { "Delete \(displayName($0))?" } ?? "",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            titleVisibility: .visible, presenting: pendingDelete) { game in
            Button("Delete \(game.id)", role: .destructive) { delete(game) }
                .accessibilityIdentifier("confirm-delete-" + game.id)
            Button("Cancel", role: .cancel) {}
        } message: { game in
            Text("Removes the prepared package \(game.id) from this iPhone. Your saves are kept. Prepare the game again to get it back.")
        }
        .task {
            guard loading else { return }
            do {
                games = try await Task.detached(priority: .utility) {
                    try Self.discover()
                }.value
                let defaults = UserDefaults.standard
                if PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame") {
                    let name = defaults.string(forKey: "SatchelPreparedGame")
                    if let game = games.first(where: { $0.supported && (name == nil || $0.id == name) }) { selected = game }
                    else { failure = "The requested prepared game was not found or has no supported profile." }
                } else if let launch, let game = games.first(where: { $0.id == launch && $0.supported }) {
                    selected = game
                }
            } catch { failure = error.localizedDescription }
            loading = false
        }
    }

    private func header(reserve: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            SatchelEyebrow(text: "\(AppBrand.name) Dev", tint: SatchelTheme.leaf)
            Text("Prepared games").font(SatchelTheme.display(28))
            Text("Packages in Documents/PreparedGames. Pick one to check it and play.")
                .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(.trailing, reserve)
    }

    private func listScroll(columns: Int, reserve: CGFloat, showHeader: Bool) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if showHeader { header(reserve: reserve) }
                if loading {
                    SatchelPanel {
                        HStack(spacing: 12) {
                            ProgressView().tint(SatchelTheme.leaf)
                            Text("Finding prepared games…").font(.system(size: 14, weight: .semibold, design: .rounded))
                        }
                    }
                }
                if let failure {
                    SatchelPanel(title: "Not available", tint: SatchelTheme.ember) {
                        Text(failure).font(.system(size: 14, weight: .semibold, design: .rounded))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                if !loading && games.isEmpty {
                    SatchelPanel {
                        Text("No prepared games yet.").font(SatchelTheme.display(18, .bold))
                        Text("Prepare a game from the Workshop menu, or place a complete package in Documents/PreparedGames.")
                            .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 14, alignment: .top), count: columns),
                          alignment: .leading, spacing: 14) {
                    ForEach(games) { game in card(game) }
                }
            }
            .padding(.vertical, 18)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
    }

    private func card(_ game: PreparedGameChoice) -> some View {
        Button { Haptics.click(); selected = game } label: {
            HStack(alignment: .top, spacing: 14) {
                art(game)
                VStack(alignment: .leading, spacing: 7) {
                    Text(displayName(game)).font(.system(size: 16, weight: .bold, design: .rounded))
                        .foregroundStyle(SatchelTheme.text).multilineTextAlignment(.leading)
                        .lineLimit(2).fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 6) {
                        Image(systemName: "shippingbox.fill").font(.system(size: 10, weight: .bold))
                        Text(game.id).lineLimit(1).truncationMode(.middle).layoutPriority(2)
                        if let version = game.version { Text("·  v\(version)").lineLimit(1).layoutPriority(1) }
                        if let date = game.date {
                            Text("·  " + date.formatted(.dateTime.month(.abbreviated).day().hour().minute())).lineLimit(1)
                        }
                    }
                    .font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim)
                    chips(game)
                    Text(game.detail).font(.system(size: 12, weight: .medium, design: .rounded))
                        .foregroundStyle(game.supported ? SatchelTheme.textDim : tint(game.status))
                        .multilineTextAlignment(.leading).fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
                if game.supported {
                    Image(systemName: "play.fill").font(.system(size: 15, weight: .bold))
                        .foregroundStyle(SatchelTheme.shell)
                        .frame(width: 40, height: 40).background(SatchelTheme.leaf, in: Circle())
                }
            }
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 22, style: .continuous)
                    .strokeBorder(game.supported ? SatchelTheme.leaf.opacity(0.35) : SatchelTheme.line)
            }
            .contentShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
        }
        .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
        .disabled(!game.supported || deleting != nil)
        .opacity(deleting == game.id ? 0.5 : 1)
        .accessibilityIdentifier("prepared-" + game.id)
        // A disabled button gets no context menu, so the menu sits on a wrapper.
        .contentShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
        .contextMenu {
            Button("View game report", systemImage: "doc.text.magnifyingglass") { reportGame = game }
            Button(role: .destructive) { pendingDelete = game } label: { Label("Delete package", systemImage: "trash") }
                .accessibilityIdentifier("delete-" + game.id)
        }
    }

    private func delete(_ game: PreparedGameChoice) {
        deleting = game.id
        Task {
            let name = game.id
            let result = await Task.detached(priority: .userInitiated) { () -> String? in
                do { try PreparedPackage.delete(named: name); return nil } catch { return error.localizedDescription }
            }.value
            if let result { failure = result } else { games.removeAll { $0.id == name }; Haptics.click() }
            deleting = nil
        }
    }

    /// The imported base game with this package's title ID, for its icon and title.
    private func match(_ game: PreparedGameChoice) -> HomebrewItem? {
        game.titleID.flatMap { id in
            library.items.first { artwork.info($0)?.kind == .application && artwork.info($0)?.applicationID == id }
        }
    }

    private func displayName(_ game: PreparedGameChoice) -> String {
        game.status == .unsupported ? match(game).map { artwork.title($0) } ?? game.name : game.name
    }

    @ViewBuilder private func art(_ game: PreparedGameChoice) -> some View {
        let match = match(game)
        Group {
            if let match {
                GameTileArt(item: match, title: artwork.title(match), image: artwork.image(match), side: 72)
            } else {
                let accent = SatchelTheme.accent(for: game.titleID.map(\.titleIDString) ?? game.id)
                ZStack {
                    accent
                    LinearGradient(colors: [.white.opacity(0.18), .clear, .black.opacity(0.35)],
                                   startPoint: .topLeading, endPoint: .bottomTrailing)
                    Image(systemName: game.status == .incomplete ? "shippingbox" : "gamecontroller.fill")
                        .font(.system(size: 24, weight: .bold)).foregroundStyle(.white.opacity(0.9))
                }
                .frame(width: 72, height: 72)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .saturation(game.supported ? 1 : 0.4)
        .accessibilityHidden(true)
    }

    private func chips(_ game: PreparedGameChoice) -> some View {
        ChipFlow(spacing: 6) {
            SatchelChip(text: label(game.status), systemImage: symbol(game.status), tint: tint(game.status), filled: game.supported)
            if let phone = game.preparedOnPhone {
                SatchelChip(text: phone ? "Prepared on this iPhone" : "Prepared on Mac",
                           systemImage: phone ? "iphone" : "desktopcomputer", tint: SatchelTheme.tide)
            }
            if game.shaderSource {
                SatchelChip(text: "Shader source test", systemImage: "testtube.2", tint: SatchelTheme.violet)
            }
        }
    }

    private func label(_ status: PreparedGameChoice.Status) -> String {
        switch status {
        case .ready: return "Ready"
        case .needsSignature: return "Needs signature"
        case .incomplete: return "Incomplete"
        case .unsupported: return "Not supported"
        }
    }

    private func symbol(_ status: PreparedGameChoice.Status) -> String {
        switch status {
        case .ready: return "checkmark.circle.fill"
        case .needsSignature: return "signature"
        case .incomplete: return "exclamationmark.triangle.fill"
        case .unsupported: return "questionmark.circle.fill"
        }
    }

    private func tint(_ status: PreparedGameChoice.Status) -> Color {
        switch status {
        case .ready: return SatchelTheme.leaf
        case .needsSignature: return SatchelTheme.spark
        case .incomplete: return SatchelTheme.ember
        case .unsupported: return SatchelTheme.textDim
        }
    }

    private nonisolated static func discover() throws -> [PreparedGameChoice] {
        let fm = FileManager.default
        let root = fm.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames")
        guard fm.fileExists(atPath: root.path) else { return [] }
        return try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey], options: .skipsHiddenFiles)
            .sorted { $0.lastPathComponent < $1.lastPathComponent }.compactMap { url in
                let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                guard values.isDirectory == true, values.isSymbolicLink != true else { return nil }
                do {
                    let file = url.appendingPathComponent("manifest.json")
                    let attrs = try file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey, .contentModificationDateKey])
                    guard attrs.isRegularFile == true, attrs.isSymbolicLink != true,
                          (attrs.fileSize ?? Int.max) <= 32 * 1024 * 1024 else {
                        throw NSError(domain: "PreparedGames", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid prepared manifest."])
                    }
                    let manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [String: Any]
                    let titleID = manifest?["titleID"] as? String ?? "", version = manifest?["version"] as? String
                    var profile = NativeGameProfile.find(titleID: titleID, version: version ?? "")
                    if manifest?["format"] as? Int == 2 {
                        guard let recipeObject = manifest?["recipe"], let profileObject = manifest?["preparedProfile"] else { throw NSError(domain: "PreparedGames", code: 2) }
                        let recipe = try JSONDecoder().decode(NativePreparationRecipe.self, from: JSONSerialization.data(withJSONObject: recipeObject))
                        let declared = try JSONDecoder().decode(NativeGameProfile.self, from: JSONSerialization.data(withJSONObject: profileObject))
                        let generated = try recipe.profile()
                        guard generated == declared else { throw NSError(domain: "PreparedGames", code: 3) }
                        profile = generated
                    }
                    let signed = manifest?["signingStatus"] == nil || manifest?["signingStatus"] as? String == "signed"
                    let status: PreparedGameChoice.Status = profile == nil ? .unsupported : signed ? .ready : .needsSignature
                    let shaderSource = manifest?["shaderSource"] != nil
                    let detail: String
                    switch status {
                    case .unsupported: detail = NativeGameProfile.unsupportedMessage
                    case .needsSignature: detail = "Prepared game needs signature."
                    default: detail = manifest?["format"] as? Int == 2 ? "Experimental boot. Shader capture is available; phone translation is not linked yet." : shaderSource ? "Ships Metal shader source; the phone compiles it at launch. Verify and launch."
                                                   : "Verify and launch."
                    }
                    // The phone's preparation pipeline records the content version and module
                    // build IDs; packages built by the Mac tools have neither.
                    let phone = manifest?["contentVersion"] != nil || manifest?["buildIDs"] != nil
                    return PreparedGameChoice(url: url, name: profile?.name ?? url.lastPathComponent,
                        titleID: UInt64(titleID, radix: 16), version: profile?.version ?? version, detail: detail,
                        status: status, preparedOnPhone: phone, shaderSource: shaderSource, date: attrs.contentModificationDate)
                } catch {
                    return PreparedGameChoice(url: url, name: url.lastPathComponent, titleID: nil, version: nil,
                        detail: "Package incomplete: \(error.localizedDescription)", status: .incomplete,
                        preparedOnPhone: nil, shaderSource: false, date: nil)
                }
            }
    }
}

/// Lays chips out left to right, wrapping onto new lines.
private struct ChipFlow: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = arrange(proposal.width ?? .infinity, subviews)
        return CGSize(width: rows.map(\.width).max() ?? 0, height: rows.last.map { $0.y + $0.height } ?? 0)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        for row in arrange(bounds.width, subviews) {
            var x = bounds.minX
            for index in row.items {
                let size = subviews[index].sizeThatFits(.unspecified)
                subviews[index].place(at: CGPoint(x: x, y: bounds.minY + row.y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
        }
    }

    private func arrange(_ width: CGFloat, _ subviews: Subviews) -> [(items: [Int], y: CGFloat, width: CGFloat, height: CGFloat)] {
        var rows: [(items: [Int], y: CGFloat, width: CGFloat, height: CGFloat)] = []
        for index in subviews.indices {
            let size = subviews[index].sizeThatFits(.unspecified)
            if var last = rows.last, last.width + spacing + size.width <= width {
                last.items.append(index); last.width += spacing + size.width; last.height = max(last.height, size.height)
                rows[rows.count - 1] = last
            } else {
                let y = rows.last.map { $0.y + $0.height + spacing } ?? 0
                rows.append(([index], y, size.width, size.height))
            }
        }
        return rows
    }
}


struct NativeGameReportView: View {
    let package: String
    let titleID: String
    @Environment(\.dismiss) private var dismiss
    @State private var report: NativeGameReport?
    @State private var feedback: NativeBootFeedback?
    @State private var failure: String?
    @State private var copied = false
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    if let report {
                        Text("Stage: " + report.stage).font(.headline)
                        Text("Result: " + report.result)
                        Text(report.message)
                        Text("First frame: " + report.firstFrame + "\nGameplay: " + report.gameplay)
                        if let crash = report.crash { Text(crash).font(.system(.body, design: .monospaced)) }
                        Text("Copy diagnostics puts command names, counts, error codes and revisions on the clipboard, never game data, saves or keys. The copy stays on this device and expires after five minutes.")
                            .font(.caption).foregroundStyle(.secondary)
                        HStack(spacing: 20) {
                            Button {
                                let info = Bundle.main.infoDictionary
                                let app = "\(info?["CFBundleShortVersionString"] as? String ?? "unknown") (\(info?["CFBundleVersion"] as? String ?? "unknown"))"
                                var system = utsname(); uname(&system)
                                let model = withUnsafeBytes(of: &system.machine) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
                                let text = report.diagnosticText(packageRoot: NativeBootFeedback.packageURL(package: package), feedback: feedback,
                                                                 appVersion: app, device: model + ", iOS " + UIDevice.current.systemVersion)
                                UIPasteboard.general.setItems([["public.utf8-plain-text": text]],
                                                              options: [.localOnly: true, .expirationDate: Date().addingTimeInterval(300)])
                                copied = true
                            } label: { Label(copied ? "Copied" : "Copy diagnostics", systemImage: "doc.on.doc") }
                                .accessibilityIdentifier("game-report-copy")
                            ShareLink(item: NativeGameReport.url(package: package)) { Label("Export report", systemImage: "square.and.arrow.up") }
                        }
#if PREVIEW_GAME
                        GameReportClipboardFixture()
#endif
                        if let feedback {
                            Text("Automatic settings").font(.headline)
                            ForEach(feedback.summary, id: \.self) { Text($0).accessibilityIdentifier("game-report-feedback") }
                            if feedback.learnedProfileSHA256 != nil {
                                Button("Forget learned settings") {
                                    NativeBootFeedback.reset(package: package)
                                    self.feedback = nil
                                    self.report?.bootFeedback = nil
                                    try? self.report?.save()
                                    copied = false
                                }.accessibilityIdentifier("game-report-forget-learned")
                            }
                        }
                        ForEach(report.missingServices + report.missingSVCs + report.shaderFailures, id: \.self) { Text($0) }
                        Text(report.lastLogLines.joined(separator: "\n")).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                    }
                    if let failure { Text(failure) }
                }.frame(maxWidth: .infinity, alignment: .leading).padding(24)
            }
            .navigationTitle("Game report")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .task {
                let active = SatchelNativeSessionStarted()
                let (value, learned) = await Task.detached(priority: .utility) { () -> (NativeGameReport, NativeBootFeedback?) in
                    var value = NativeGameReport.load(package: package, titleID: titleID, sessionActive: active)
                    let learned = NativeBootFeedback.packageURL(package: package).flatMap { NativeBootFeedback.collect(packageRoot: $0) }
                    value.bootFeedback = learned?.summary
                    return (value, learned)
                }.value
                do { try value.save(); report = value; feedback = learned } catch { failure = error.localizedDescription }
            }
        }
    }
}
