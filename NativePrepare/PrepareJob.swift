import BackgroundTasks
import SwiftUI

/// The one Prepare job, owned by the app rather than by a screen. It is the only place that
/// starts, cancels and reports preparation, so closing the Prepare screen leaves the work
/// running, and the Prepare screen, the home card, the system's continued processing task
/// and `Documents/prepare-progress.json` all show the same state. One job runs at a time.
@MainActor final class PrepareJob: ObservableObject {
    static let shared = PrepareJob()

    /// How the last job ended. Kept (also across launches) until the home card is dismissed.
    struct Outcome: Codable, Equatable {
        enum Kind: String, Codable { case signed, needsSignature, failed, cancelled }
        let kind: Kind
        let package: String
        let title: String
        let message: String
        let baseID: String
        let updateID: String
        let finishedAt: Date
        var applicationID: UInt64?
    }

    @Published private(set) var running = false {
        // Cover the foreground fallback and the interval before iOS accepts a continued task.
        // Once accepted, the continued task owns expiration and background lifetime.
        didSet {
            guard running != oldValue else { return }
            if running {
                backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "Prepare game") { [weak self] in
                    self?.backgroundLifetime?.cancel(); self?.cancel(); self?.endBackgroundTask()
                }
            } else { endBackgroundTask() }
            // Keep the screen awake while preparing in the foreground; a game session holds it on its own.
            if running || (!WarmupJob.shared.playRequested && !SatchelNativeSessionStarted()) { UIApplication.shared.isIdleTimerDisabled = running }
        }
    }
    @Published private(set) var message = "Choose an imported base game and its matching update."
    @Published private(set) var fraction: Double?
    @Published private(set) var progressSnapshot: PrepareProgressSnapshot?
    @Published private(set) var startedAt: Date?
    /// The package folder being written, the game's display name and the library choices.
    @Published private(set) var package = ""
    @Published private(set) var title = ""
    @Published private(set) var baseID = ""
    @Published private(set) var updateID = ""
    @Published private(set) var outcome: Outcome? {
        didSet { Self.storeOutcome(outcome) }
    }
    @Published private(set) var cancelling = false
    @Published private(set) var finalizing = false
    /// Set when this job runs without the newest imported update of its game.
    @Published private(set) var updateWarning: String?
    /// `-SatchelAutoPrepare` starts one job per launch, even if the screen opens again.
    var autoPrepareUsed = false

    private var backgroundTask: UIBackgroundTaskIdentifier = .invalid
    private var backgroundLifetime: WarmupCancellation?
    private var cancellation: PrepareCancellation?
    private var selectedTitleID: UInt64?
    private static let outcomeKey = "PrepareJobOutcome"

    private init() {
        outcome = UserDefaults.standard.data(forKey: Self.outcomeKey).flatMap { try? JSONDecoder().decode(Outcome.self, from: $0) }
        if let outcome {
            package = outcome.package; title = outcome.title; message = outcome.message
            baseID = outcome.baseID; updateID = outcome.updateID
            selectedTitleID = outcome.applicationID
        }
    }

    private func endBackgroundTask() {
        if backgroundTask != .invalid { UIApplication.shared.endBackgroundTask(backgroundTask); backgroundTask = .invalid }
    }

    /// Starts preparing `base` with `update`. Ignored while a job runs.
    func start(base: HomebrewItem, update: HomebrewItem?, title: String, library: HomebrewLibrary,
               artwork: GameArtworkStore, applicationID: UInt64? = nil) {
        guard !running, !WarmupJob.shared.running, !AddOnJob.shared.running else { return }
        // A paused game holds gigabytes of memory and its package; Prepare is untested beside it.
        guard !Self.gameSessionOpen else { message = Self.sessionBlocked; return }
        baseID = base.id; updateID = update?.id ?? ""; self.title = title
        selectedTitleID = applicationID; updateWarning = nil
        guard artwork.keysAvailable else { message = FirstRunHelp.noKeys; return }
        guard library.hasOriginal(base), update.map(library.hasOriginal) ?? true else {
            message = "Import the original game and matching update again before preparing a new package."
            return
        }
        if let unreadable = library.items.first(where: { $0.package != nil && library.hasOriginal($0) && artwork.info($0) == nil }) {
            if case .failed(let reason) = artwork.status[unreadable.id] { message = reason }
            else { message = "Read game information for all imports before preparing. Open Workshop > Imported content to retry an unreadable file." }
            return
        }
        // Only remove updates covered by this Prepare, never a newer imported update.
        let info = artwork.info(base)
        let targetID = applicationID ?? info?.applicationID
        // A base prepared without its update can fail to start: say so when a newer matching
        // update is imported but not chosen (the Prepare screen shows the same caution).
        if let info, let targetID {
            let candidates = library.items.filter { $0.id != base.id && library.hasOriginal($0) }
                .compactMap { item in artwork.info(item).map { (id: item.id, info: $0) } }
            updateWarning = PrepareUpdateCaution.text(base: info, applicationID: targetID,
                                                      selected: update.flatMap { artwork.info($0) }, among: candidates)
        }
        let version = update.flatMap { artwork.info($0)?.version } ?? (info?.applicationID == targetID ? info?.version : nil) ?? 0
        var originals = [base] + library.items.filter {
            guard let other = artwork.info($0), let targetID else { return false }
            return other.kind == .update && other.applicationID == targetID && other.version <= version
        }
        if let update, !originals.contains(where: { $0.id == update.id }) { originals.append(update) }
        // Keep DLC originals: the cleanup index currently distinguishes only bases and updates.
        // The pipeline matches imported DLC to its authenticated application selection.
        let addOns = library.items.filter { item in
            artwork.info(item)?.addOns?.isEmpty == false
                && library.hasOriginal(item)
        }.map(library.localFile)
        start(base: library.localFile(base), update: update.map { library.localFile($0) },
              addOns: addOns, originals: originals, library: library, artwork: artwork)
    }

    /// Runs the last job again with the same library choices.
    func retry(library: HomebrewLibrary, artwork: GameArtworkStore, title: (HomebrewItem) -> String) {
        guard !running else { return }
        guard let base = library.items.first(where: { $0.id == baseID }) else {
            message = "The base game is no longer in the library. Choose it again on the Prepare screen."
            outcome = Outcome(kind: .failed, package: package, title: self.title, message: message,
                              baseID: baseID, updateID: updateID, finishedAt: Date())
            return
        }
        start(base: base, update: library.items.first { $0.id == updateID }, title: title(base), library: library,
              artwork: artwork, applicationID: selectedTitleID)
    }

    /// Prepare is refused while a game session exists, even paused in the background.
    static let sessionBlocked = "Quit the game before preparing another."
    static let verifiedMessage = "Ready to play. The complete package passed verification."
    static var gameSessionOpen: Bool { SatchelNativeSessionStarted() || PreparedLaunch.current != nil }

    /// A note on the Prepare screen when no job runs (for example why auto prepare did not start).
    func note(_ text: String) { if !running { message = text } }

    /// Clears the finished state shown on the home card.
    func dismissOutcome() { outcome = nil }

    func cancel() {
        if WarmupJob.shared.running { WarmupJob.shared.cancel(); return }
        guard running else { return }
        guard cancellation?.cancel() == true else { return }
        cancelling = true; message = "Cancelling preparation…"
    }

    private func start(base: URL, update: URL?, addOns: [URL], originals: [HomebrewItem], library: HomebrewLibrary, artwork: GameArtworkStore) {
        package = ""
        do {
            guard let keys = try SwitchKeys.load() else { throw PrepareFailure(message: FirstRunHelp.noKeys) }
            let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            let originalFiles = originals.map(library.localFile)
            let games = documents.appendingPathComponent("PreparedGames", isDirectory: true)
            // Metadata selection and hashing run on the worker, never the UI thread.
            let name = "Prepared-" + UUID().uuidString
            let destination = games.appendingPathComponent(name, isDirectory: true)
            let applicationID = selectedTitleID
            let token = PrepareCancellation(); cancellation = token
            let lifetime = WarmupCancellation(); backgroundLifetime = lifetime
            outcome = nil; package = name; cancelling = false; finalizing = false; running = true
            message = "Waiting for iOS to start preparation…"; fraction = 0; progressSnapshot = nil; startedAt = Date()
            Self.report(state: "waiting", message: message, snapshot: nil, package: name)
            // The work itself, run either inside a continued processing task (keeps going in the
            // background, with iOS showing its progress) or directly when the system declines one.
            let work: (BGTask?) -> Void = { task in
                var systemProgress: Progress?
                var retitle: ((PrepareProgressSnapshot) -> Void)?
                if #available(iOS 26.0, *), let continued = task as? BGContinuedProcessingTask {
                    systemProgress = continued.progress
                    retitle = { value in
                        continued.updateTitle("Preparing \(name) · \(Int(value.overallFraction * 100))%",
                            subtitle: "\(value.step.title) · \(Int(value.stepFraction * 100))%" +
                                (value.totalBytes > 0 ? " · \(ByteCountFormatter.string(fromByteCount: Int64(clamping: value.completedBytes), countStyle: .file))" : ""))
                    }
                    // The continued task owns background lifetime once accepted.
                    DispatchQueue.main.async { self.endBackgroundTask() }
                }
                systemProgress?.totalUnitCount = 1_000_000
                task?.expirationHandler = { lifetime.cancel(); token.cancel() }
                DispatchQueue.main.async {
                    self.startedAt = Date(); if !self.cancelling { self.message = "Opening signing identity…" }
                    Self.report(state: "starting", message: self.message, snapshot: nil, package: name)
                }
                var cleanupPlan: PreparedImportCleanup.Plan?
                var cleanupNote: String?
                var preparedTitleID: UInt64?
                let outcome: Result<PrepareSigningState, Error> = Result {
                    let selection = try PrepareDiscovery.select(base: base, update: update, keys: keys, titleID: applicationID, token: token)
                    NativeGameReport.record(package: name, titleID: String(format: "%016llX", selection.titleID), stage: "extract", result: "running", message: "Preparing " + selection.name)
                    let reviewed = NativeGameProfile.find(titleID: String(format: "%016llX", selection.titleID), version: selection.version)
                    let candidate = reviewed.map { documents.appendingPathComponent("ShaderPacks/" + $0.packageName) }
                    let pack = candidate.flatMap { FileManager.default.fileExists(atPath: $0.appendingPathComponent("shader-pack.json").path) ? $0 : nil }
                    let identity = try SatchelSigningIdentity.storedIdentity()
                    guard identity.expiry > Date() else { throw PrepareFailure(message: "Your signing certificate has expired. Import a renewed identity.") }
                    // Capture the inputs before Prepare reads them. Replacements made while
                    // it runs must not be deleted just because the older package verifies.
                    do { cleanupPlan = try PreparedImportCleanup.plan(documents: documents, originals: originalFiles, shaderPack: pack, cancelled: { token.cancelled }) }
                    catch { cleanupNote = "Originals kept: " + error.localizedDescription }
                    try token.check()
                    preparedTitleID = selection.titleID
                    DispatchQueue.main.async {
                        self.title = selection.name
                        self.selectedTitleID = selection.titleID
                    }
                    let state = try PreparePipeline.run(base: base, update: update, addOns: addOns, keys: keys, shaderPack: pack,
                        destination: destination, applicationID: selection.titleID, signer: PhonePreparedGameSigner(identity: identity), token: token,
                        details: { value in
                            systemProgress?.completedUnitCount = Int64(value.overallFraction * 1_000_000)
                            retitle?(value)
                            DispatchQueue.main.async {
                                if self.progressSnapshot == nil { self.startedAt = Date().addingTimeInterval(-value.elapsedSeconds) }
                                if !self.cancelling { self.message = value.message }
                                self.fraction = value.overallFraction
                                self.progressSnapshot = value
                                Self.report(state: "running", message: value.message, snapshot: value, package: name)
                            }
                        }) { _, _ in }
                    if state == .signed {
                        DispatchQueue.main.async {
                            self.message = "Verifying the complete prepared package before removing originals…"
                            self.fraction = nil
                        }
                        // Exactly the launch verifier, with its cache bypassed before destructive cleanup.
                        _ = try PreparedPackage.verify(root: destination, forceFull: true, cancelled: { token.cancelled })
                        try token.check()
                    }
                    try token.check()
                    return state
                }
                if case .success = outcome, let systemProgress { systemProgress.completedUnitCount = systemProgress.totalUnitCount }
                let plannedCleanup = cleanupPlan, planningNote = cleanupNote, cleanupTitleID = preparedTitleID
                DispatchQueue.main.async {
                    // A signed package that passed the full verifier replaces the title's older
                    // packages (a package from an earlier engine, or an earlier Prepare). Failed
                    // and cancelled runs never reach this, and a removal failure keeps success.
                    @MainActor func complete(_ result: Result<PrepareSigningState, Error>, note: String? = nil) {
                        guard case .success(.signed) = result else { proceed(result, note: note); return }
                        var keeping = Set<String>()
                        if WarmupJob.shared.running, let package = WarmupJob.shared.journal?.package { keeping.insert(package) }
                        if AddOnJob.shared.running { keeping.insert(AddOnJob.shared.package) }
                        DispatchQueue.global(qos: .utility).async {
                            let kept: String?
                            do {
                                let failures = try PreparedPackage.removeReplaced(by: destination, keeping: keeping)
                                kept = failures.isEmpty ? nil : "Older prepared copies were kept: " + failures.joined(separator: "; ")
                            } catch { kept = "Older prepared copies were kept: " + error.localizedDescription }
                            DispatchQueue.main.async {
                                proceed(result, note: kept.map { (note ?? Self.verifiedMessage) + " " + $0 } ?? note)
                            }
                        }
                    }
                    @MainActor func proceed(_ result: Result<PrepareSigningState, Error>, note: String? = nil) {
                        @MainActor func finish() {
                            task?.setTaskCompleted(success: { if case .success(.signed) = result { return true } else { return false } }())
                            self.finish(result, note: note)
                            artwork.refreshPrepared()
                        }
                        // Keep the verified cleanup contract. Optional work begins after its
                        // commit, so Play or Pause cannot turn off delete-after-Prepare.
                        artwork.refreshPrepared()
                        guard case .success(.signed) = result, !lifetime.cancelled,
                              let warmup = WarmupJob.shared.begin(package: destination, title: self.title, keepAlive: task == nil) else { finish(); return }
                        self.message = "Ready to play. Warming up known shaders…"
                        self.fraction = 1
                        // An expiration here is iOS stopping the warm-up: it resumes when the app is active again.
                        let warmupJob = WarmupJob.shared
                        task?.expirationHandler = { lifetime.cancel(); warmupJob.expire(warmup) }
                        // Expiration may race the handler replacement. Cleanup still commits,
                        // but an expired task must not admit optional work afterward.
                        if lifetime.cancelled { warmup.token.cancel() }
                        DispatchQueue.global(qos: .utility).async {
                            WarmupJob.perform(warmup, verified: true) { value in
                                systemProgress?.totalUnitCount = 2_000_000
                                systemProgress?.completedUnitCount = 1_000_000 + Int64(value.systemFraction * 1_000_000)
                                if let continued = task as? BGContinuedProcessingTask {
                                    continued.updateTitle("Warming up " + warmup.title, subtitle: value.stage)
                                }
                            }
                            DispatchQueue.main.async { finish() }
                        }
                    }
                    do { try token.check() } catch { complete(.failure(error)); return }
                    guard case .success(.signed) = outcome else { complete(outcome); return }
                    guard PreparedImportCleanup.enabled() else {
                        complete(outcome, note: "Ready to play. Original imports were kept by your storage setting."); return
                    }
                    guard let plannedCleanup else {
                        complete(outcome, note: planningNote ?? "Ready to play. Original imports were kept."); return
                    }
                    do { try artwork.preserveForCleanup(originals) } catch {
                        complete(outcome, note: "Ready to play. Originals kept: " + error.localizedDescription); return
                    }
                    // This is the commit point. Later expiration or Cancel cannot relabel a
                    // successful, verified cleanup as a cancelled preparation.
                    do { try token.beginFinalizing() } catch { complete(.failure(error)); return }
                    self.finalizing = true
                    self.message = "Ready to play. Removing original imports…"
                    guard let titleID = cleanupTitleID else {
                        complete(outcome, note: "Ready to play. Original imports were kept."); return
                    }
                    let sources = Dictionary(uniqueKeysWithValues: originals.map {
                        ($0.id, PreparedImportSource(titleID: titleID, package: name, isUpdate: $0.id != self.baseID))
                    })
                    library.reclaimPreparedOriginals(sources, plan: plannedCleanup) { note in
                        complete(outcome, note: note ?? "Ready to play. Original imports removed; your game, artwork and saves are kept.")
                    }
                }
            }
            let identifier = (Bundle.main.bundleIdentifier ?? "com.dell.satchel") + ".prepare." + UUID().uuidString
            let registered = BGTaskScheduler.shared.register(forTaskWithIdentifier: identifier, using: nil) { task in
                work(task)
            }
            if registered, #available(iOS 26.0, *) {
                let request = BGContinuedProcessingTaskRequest(identifier: identifier, title: "Preparing \(name)",
                                                               subtitle: "Satchel keeps preparing while you use other apps.")
                request.strategy = .fail
                // Declined (for example under heavy load): run in the foreground as before.
                let fallback = { DispatchQueue.global(qos: .utility).async { work(nil) } }
                if #available(iOS 27.0, *) {
                    BGTaskScheduler.shared.submitTaskRequest(request) { error in if error != nil { fallback() } }
                } else {
                    do { try BGTaskScheduler.shared.submit(request) } catch { fallback() }
                }
            } else {
                DispatchQueue.global(qos: .utility).async { work(nil) }
            }
        } catch {
            message = FirstRunHelp.message(error)
            outcome = Outcome(kind: .failed, package: package, title: title, message: message,
                              baseID: baseID, updateID: updateID, finishedAt: Date())
        }
    }

    private func finish(_ result: Result<PrepareSigningState, Error>, note: String? = nil) {
        running = false; cancellation = nil; cancelling = false; finalizing = false
        let kind: Outcome.Kind
        switch result {
        case .success(.signed):
            kind = .signed; message = note ?? Self.verifiedMessage
        case .success:
            kind = .needsSignature; message = "Prepared game needs signature. The development Mac signing tool must sign it before launch."
        case .failure(let error) where error is CancellationError:
            kind = .cancelled; message = "Preparation cancelled. Original imports kept. Any completed package is kept too."
        case .failure(let error):
            kind = .failed; message = FirstRunHelp.message(error)
        }
        if let updateWarning, kind != .cancelled { message += " " + updateWarning }
        outcome = Outcome(kind: kind, package: package, title: title, message: message,
                          baseID: baseID, updateID: updateID, finishedAt: Date(), applicationID: selectedTitleID)
        let state: String
        switch kind {
        case .signed: state = "signed"
        case .needsSignature: state = "unsigned"
        case .failed, .cancelled: state = "failed"
        }
        Self.report(state: state, message: message, snapshot: progressSnapshot, package: package)
    }

    private static func storeOutcome(_ outcome: Outcome?) {
        if let outcome, let data = try? JSONEncoder().encode(outcome) { UserDefaults.standard.set(data, forKey: outcomeKey) }
        else { UserDefaults.standard.removeObject(forKey: outcomeKey) }
    }

    /// Progress the Mac can read with devicectl (`Documents/prepare-progress.json`), since the
    /// phone's screen capture comes back black. Messages are fixed text with no key material.
    static func report(state: String, message: String, snapshot: PrepareProgressSnapshot?, package: String) {
        if !package.isEmpty {
            NativeGameReport.record(package: package, stage: snapshot?.step.rawValue ?? state, result: state, message: message)
        }
        var row: [String: Any] = ["state": state, "message": message, "package": package,
                                  "time": Date().timeIntervalSince1970]
        row["fraction"] = snapshot?.overallFraction ?? 0
        row["overallFraction"] = snapshot?.overallFraction ?? 0
        row["stepFraction"] = snapshot?.stepFraction ?? 0
        row["step"] = snapshot?.step.rawValue ?? state
        row["completedBytes"] = snapshot?.completedBytes ?? 0
        row["totalBytes"] = snapshot?.totalBytes ?? 0
        if let snapshot, let encoded = try? JSONEncoder().encode(snapshot),
           let fields = try? JSONSerialization.jsonObject(with: encoded) as? [String: Any] {
            fields.forEach { row[$0.key] = $0.value }
            row["message"] = message
        }
        let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("prepare-progress.json")
        if let data = try? JSONSerialization.data(withJSONObject: row) { try? data.write(to: url, options: .atomic) }
    }
}

/// Plain words for a Prepare that leaves out the base's newest imported update, matched as the
/// shelf groups updates (`GameInfo.newestUpdate`), or nil. Prepare is never blocked by it. A
/// chosen update of another game counts as none; a cartridge's bundled update counts through the
/// base's own version. Pure, so the Prepare screen, the job and `Tests/PrepareUpdate` share it.
enum PrepareUpdateCaution {
    static func text(base: GameInfo, applicationID: UInt64, selected: GameInfo?, among candidates: [(id: String, info: GameInfo)]) -> String? {
        let chosen = selected.flatMap { $0.kind == .update && $0.applicationID & ~0x800 == applicationID & ~0x800 ? $0 : nil }
        guard let id = GameInfo.newestUpdate(for: applicationID, baseVersion: max(base.version, chosen?.version ?? 0), in: candidates),
              let newest = candidates.first(where: { $0.id == id })?.info else { return nil }
        if let chosen {
            let versions = chosen.displayVersion.isEmpty || newest.displayVersion.isEmpty ? "" : " (v\(newest.displayVersion); v\(chosen.displayVersion) is selected)"
            return "A newer imported update is not selected" + versions + ". Choose it under Update to prepare the game with it."
        }
        let versions = base.displayVersion.isEmpty || newest.displayVersion.isEmpty ? "" : " (base v\(base.displayVersion); update v\(newest.displayVersion) is left out)"
        return "This game will be prepared without its imported update" + versions + "."
    }
}
