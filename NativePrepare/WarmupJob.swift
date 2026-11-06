import BackgroundTasks
import CryptoKit
import SwiftUI

final class WarmupCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var stopped = false
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return stopped }
    func cancel() { lock.lock(); stopped = true; lock.unlock() }
}

final class WarmupScheduling: @unchecked Sendable {
    private let lock = NSLock()
    private var policy = SatchelWarmupPolicy.conservative
    func set(_ value: SatchelWarmupPolicy) { lock.lock(); policy = value; lock.unlock() }
    func get() -> SatchelWarmupPolicy { lock.lock(); defer { lock.unlock() }; return policy }
}

// Saved warm-up state. Pure, so Tests/WarmupResume compiles this part on the Mac.
/// The one warm-up's saved journal (`SatchelWarmupJob` in user defaults).
struct WarmupJournal: Codable {
    let package: String
    let title: String
    var progress: WarmupProgress
    /// True only when iOS ended the background time the run had (not Pause, not a
    /// memory warning).
    var interrupted: Bool? = nil
    /// True when the package itself failed its check: it cannot be played, only prepared again.
    var packageFailed: Bool? = nil
    /// True when the player started the run (Warm up, Resume). Earlier releases started it
    /// after every Prepare and wrote no such field: those runs never resume by themselves.
    var startedByUser: Bool? = nil
}

/// When a saved warm-up continues without being asked: only a run the player started that
/// iOS (or the app ending) stopped. Anything else waits on the page's Resume.
enum WarmupResume {
    static let interruptedStage = "Warm-up interrupted. Resume to reuse cached work."

    /// A journal read at launch. A run saved as running ended with the app. A stopped run
    /// the player did not start loses words that promise it resumes by itself.
    static func restored(_ saved: WarmupJournal) -> WarmupJournal {
        var value = saved
        if value.progress.state == "running" {
            value.progress.state = "paused"; value.progress.stage = interruptedStage
            value.interrupted = true
        }
        if value.interrupted == true, value.startedByUser != true, value.progress.state == "paused" {
            value.progress.stage = interruptedStage
        }
        return value
    }

    static func resumesByItself(_ journal: WarmupJournal) -> Bool {
        journal.startedByUser == true && journal.interrupted == true && journal.progress.state == "paused"
    }
}
// End of saved warm-up state.

/// Optional work on an already usable package. Failure never changes its playability.
@MainActor final class WarmupJob: ObservableObject {
    static let shared = WarmupJob()
    struct Context: @unchecked Sendable {
        let package: URL
        let runtime: URL
        let capture: URL
        let title: String
        let token: WarmupCancellation
        let scheduling = WarmupScheduling()
        /// Cancelled with `token` only when iOS ended the run's background time.
        let expiry = WarmupCancellation()
    }
    typealias Journal = WarmupJournal
    /// A package's last known warm-up, from the live journal or its own `progress.json`.
    struct Status {
        var progress: WarmupProgress
        var interrupted = false
        var live = false
    }
    @Published private(set) var running = false
    @Published private(set) var playRequested = false
    @Published private(set) var journal: Journal?
    private var context: Context?
    private var background: UIBackgroundTaskIdentifier = .invalid
    private var memoryWarning: NSObjectProtocol?
    private var schedulingObservers: [NSObjectProtocol] = []
    /// On-disk status per package folder, keyed by the manifest's modification date so a
    /// package prepared again is read again. Hashing runs off the main thread.
    @Published private var stored: [String: Status] = [:]
    private var storedStamp: [String: Date] = [:]
    private var loading: Set<String> = []
    private init() {
        for name in [UIApplication.didBecomeActiveNotification, UIApplication.willResignActiveNotification,
                     UIDevice.batteryStateDidChangeNotification, ProcessInfo.thermalStateDidChangeNotification,
                     Notification.Name.NSProcessInfoPowerStateDidChange] {
            schedulingObservers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                let resigning = note.name == UIApplication.willResignActiveNotification
                Task { @MainActor in self?.refreshScheduling(resigning: resigning) }
            })
        }
        memoryWarning = NotificationCenter.default.addObserver(forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.cancel() }
        }
        if let data = UserDefaults.standard.data(forKey: "SatchelWarmupJob"), let saved = try? JSONDecoder().decode(Journal.self, from: data) {
            // A run saved as running ended with the app, the same as iOS stopping it. Only a
            // run the player started resumes by itself (`resumeIfInterrupted`).
            journal = WarmupResume.restored(saved)
        }
    }
    private func refreshScheduling(resigning: Bool = false) {
        let battery = UIDevice.current.batteryState
        let process = ProcessInfo.processInfo
        context?.scheduling.set(.select(foreground: !resigning && UIApplication.shared.applicationState == .active,
            charging: battery == .charging || battery == .full,
            constrained: process.isLowPowerModeEnabled || process.thermalState.rawValue >= ProcessInfo.ThermalState.serious.rawValue))
    }
    func cancel() { context?.token.cancel() }

    /// The package's warm-up as the game page shows it: the live journal when it is about
    /// this package, else the last run recorded on disk (nil until read, or never run).
    func status(for package: String) -> Status? {
        if let journal, journal.package == package {
            return Status(progress: journal.progress, interrupted: WarmupResume.resumesByItself(journal), live: running)
        }
        loadStatus(package)
        return stored[package]
    }

    /// Reads `Documents/NativeRuntime/<titleID>/warmup/<sha256 of manifest.json>/progress.json`
    /// on a utility queue, once per manifest version.
    private func loadStatus(_ package: String) {
        guard !package.contains("/"), package != ".", package != "..", !loading.contains(package) else { return }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let manifest = documents.appendingPathComponent("PreparedGames/" + package + "/manifest.json")
        let stamp = (try? manifest.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate ?? .distantPast
        guard storedStamp[package] != stamp else { return }
        loading.insert(package)
        DispatchQueue.global(qos: .utility).async {
            let status = Self.readStatus(manifest: manifest, documents: documents)
            DispatchQueue.main.async {
                self.loading.remove(package)
                self.storedStamp[package] = stamp
                self.stored[package] = status
            }
        }
    }

    nonisolated private static func readStatus(manifest: URL, documents: URL) -> Status? {
        guard let data = try? WarmupRunner.bounded(manifest, maximum: 32 * 1024 * 1024),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let id = object["titleID"] as? String, NativePreparationRecipe.hex(id, count: 16) else { return nil }
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        let url = documents.appendingPathComponent("NativeRuntime/" + id + "/warmup/" + digest + "/progress.json")
        guard let saved = try? WarmupRunner.bounded(url, maximum: 1024 * 1024),
              var progress = try? JSONDecoder().decode(WarmupProgress.self, from: saved) else { return nil }
        // A stored "running" means the app was ended during the run. Without the journal
        // nothing resumes it by itself: the page offers Resume.
        if progress.state == "running" {
            progress.state = "paused"; progress.stage = WarmupResume.interruptedStage
        }
        return Status(progress: progress)
    }

    /// Restarts a run the player started that iOS stopped in the background, when nothing
    /// blocks it. A run an earlier release started after Prepare waits for Resume.
    func resumeIfInterrupted() {
        guard let journal, WarmupResume.resumesByItself(journal), !running, !playRequested,
              !PrepareJob.shared.running, !AddOnJob.shared.running, !SatchelNativeSessionStarted() else { return }
        start(package: journal.package, title: journal.title)
    }
    func prepareForPlay() { playRequested = true; cancel() }
    // The request gate covers verification and launch only. A live session has
    // its own gate, which clears on Quit without leaving warm-up blocked.
    func playRequestFinished() { playRequested = false }
    func dismiss() { guard !running else { return }; journal = nil; UserDefaults.standard.removeObject(forKey: "SatchelWarmupJob") }
    /// Only a player's action reaches this: Warm up or Resume on a page, or the automatic
    /// return of a run the player started. Prepare never starts a warm-up.
    func begin(package: URL, title: String, keepAlive: Bool = true) -> Context? {
        guard !running, !playRequested, !AddOnJob.shared.running, !SatchelNativeSessionStarted() else { return nil }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        guard package.standardizedFileURL.deletingLastPathComponent() == documents.appendingPathComponent("PreparedGames").standardizedFileURL,
              let data = try? WarmupRunner.bounded(package.appendingPathComponent("manifest.json"), maximum: 32 * 1024 * 1024),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let id = object["titleID"] as? String, NativePreparationRecipe.hex(id, count: 16) else { return nil }
        let recipeName = ((object["recipe"] as? [String: Any])?["name"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
        let label = recipeName.flatMap { !$0.isEmpty && $0.utf8.count <= 512 ? $0 : nil } ?? title
        let trial = SatchelWarmupTrial(arguments: ProcessInfo.processInfo.arguments)
        var runtime = documents.appendingPathComponent("NativeRuntime/" + id)
        let capture = runtime.appendingPathComponent("shader-capture")
        if let name = trial.name { runtime.appendPathComponent("WarmupTrials/" + name) }
        let value = Context(package: package, runtime: runtime, capture: capture, title: label, token: WarmupCancellation())
        context = value; running = true
        UIDevice.current.isBatteryMonitoringEnabled = true
        refreshScheduling()
        journal = Journal(package: package.lastPathComponent, title: value.title, progress: WarmupProgress(stage: "Checking prepared package"),
                          startedByUser: true)
        UIApplication.shared.isIdleTimerDisabled = true
        if keepAlive { background = UIApplication.shared.beginBackgroundTask(withName: "Warm up game") { value.expiry.cancel(); value.token.cancel() } }
        save(); return value
    }
    private func save() {
        if let journal, let data = try? JSONEncoder().encode(journal) { UserDefaults.standard.set(data, forKey: "SatchelWarmupJob") }
    }
    private func receive(_ progress: WarmupProgress, for value: Context, interrupted: Bool = false, packageFailed: Bool = false) {
        guard context?.token === value.token else { return }
        journal = Journal(package: value.package.lastPathComponent, title: value.title, progress: progress,
                          interrupted: interrupted ? true : nil, packageFailed: packageFailed ? true : nil, startedByUser: true); save()
    }

    /// Marks the running warm-up as stopped by iOS; called by the expiration handlers.
    nonisolated func expire(_ value: Context) { value.expiry.cancel(); value.token.cancel() }
    private func finish(_ value: Context, error: Error?, verified: Bool) {
        guard context?.token === value.token else { return }
        let byIOS = value.expiry.cancelled && value.token.cancelled
        if let error {
            var state = journal?.progress ?? WarmupProgress()
            state.state = value.token.cancelled ? "paused" : "failed"
            if verified {
                state.stage = byIOS ? "Stopped by iOS while \(AppBrand.name) was in the background. It resumes when you return."
                    : value.token.cancelled ? "Paused. Play is ready; resume to reuse cached work." : "Warm-up stopped. Play is still ready. " + error.localizedDescription
            } else {
                state.stage = byIOS ? "Package check stopped by iOS in the background. It resumes when you return."
                    : value.token.cancelled ? "Package check paused. Resume to check it before warm-up." : "Package check failed. Prepare this game again."
            }
            receive(state, for: value, interrupted: byIOS, packageFailed: !verified && !value.token.cancelled)
        }
        // The run rewrote this package's progress.json; read it again next time.
        storedStamp[value.package.lastPathComponent] = nil
        running = false; context = nil
        if !playRequested, !SatchelNativeSessionStarted(), !PrepareJob.shared.running { UIApplication.shared.isIdleTimerDisabled = false }
        endBackgroundTask()
        // Cancellation may finish after the scene's active callback already ran.
        // Retry here too so a system-expired run cannot remain stranded on return.
        if UIApplication.shared.applicationState == .active { resumeIfInterrupted() }
    }
    private func endBackgroundTask() {
        if background != .invalid { UIApplication.shared.endBackgroundTask(background); background = .invalid }
    }
    /// The work of one run, inside the continued-processing task `start` asks for (or the
    /// foreground fallback). `verified` skips the package check when the caller just did it.
    @discardableResult nonisolated static func perform(_ context: Context, verified: Bool,
                                    system: ((WarmupProgress) -> Void)? = nil) -> Bool {
        var failure: Error?, checked = verified
        do {
            if !verified { _ = try PreparedPackage.verify(root: context.package, cancelled: { context.token.cancelled }); checked = true }
            let trial = SatchelWarmupTrial(arguments: ProcessInfo.processInfo.arguments)
            _ = try WarmupRunner.run(package: context.package, runtime: context.runtime,
                cancelled: { context.token.cancelled }, captureDirectory: context.capture, execution: { context.scheduling.get() }) { progress in
                    if let limit = trial.records, progress.phase == 1, progress.completed >= limit { context.token.cancel() }
                    system?(progress)
                    DispatchQueue.main.async { shared.receive(progress, for: context) }
                }
        } catch { failure = error }
        let result = failure, packageChecked = checked
        DispatchQueue.main.async { shared.finish(context, error: result, verified: packageChecked) }
        return failure == nil
    }
    func start(package: String, title: String) {
        guard !PrepareJob.shared.running, !package.contains("/"), package != ".", package != ".." else { return }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        guard let value = begin(package: documents.appendingPathComponent("PreparedGames/" + package), title: title) else { return }
        let work: (BGTask?) -> Void = { task in
            if task != nil { DispatchQueue.main.async { self.endBackgroundTask() } }
            task?.expirationHandler = { self.expire(value) }
            if let continued = task as? BGContinuedProcessingTask { continued.progress.totalUnitCount = 1_000_000 }
            let success = Self.perform(value, verified: false) { progress in
                if let continued = task as? BGContinuedProcessingTask {
                    continued.progress.completedUnitCount = Int64(progress.systemFraction * 1_000_000)
                    continued.updateTitle("Warming up " + value.title, subtitle: progress.stage + " · \(progress.completed) of \(progress.total)")
                }
            }
            task?.setTaskCompleted(success: success)
        }
        let identifier = (Bundle.main.bundleIdentifier ?? "com.dell.satchel") + ".prepare." + UUID().uuidString
        let registered = BGTaskScheduler.shared.register(forTaskWithIdentifier: identifier, using: nil) { work($0) }
        let fallback = { DispatchQueue.global(qos: .utility).async { work(nil) } }
        if registered {
            let request = BGContinuedProcessingTaskRequest(identifier: identifier, title: "Warming up " + value.title, subtitle: "Play is ready while shaders warm up.")
            request.strategy = .fail
            if #available(iOS 27.0, *) { BGTaskScheduler.shared.submitTaskRequest(request) { if $0 != nil { fallback() } } }
            else { do { try BGTaskScheduler.shared.submit(request) } catch { fallback() } }
        } else { fallback() }
    }
}
