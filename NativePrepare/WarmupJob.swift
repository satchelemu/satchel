import BackgroundTasks
import SwiftUI

final class WarmupCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var stopped = false
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return stopped }
    func cancel() { lock.lock(); stopped = true; lock.unlock() }
}

/// Optional work on an already usable package. Failure never changes its playability.
@MainActor final class WarmupJob: ObservableObject {
    static let shared = WarmupJob()
    struct Context: @unchecked Sendable {
        let package: URL
        let runtime: URL
        let title: String
        let token: WarmupCancellation
    }
    struct Journal: Codable {
        let package: String
        let title: String
        var progress: WarmupProgress
    }
    @Published private(set) var running = false
    @Published private(set) var playRequested = false
    @Published private(set) var journal: Journal?
    private var context: Context?
    private var background: UIBackgroundTaskIdentifier = .invalid
    private var memoryWarning: NSObjectProtocol?
    private init() {
        memoryWarning = NotificationCenter.default.addObserver(forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.cancel() }
        }
        if let data = UserDefaults.standard.data(forKey: "SatchelWarmupJob"), var saved = try? JSONDecoder().decode(Journal.self, from: data) {
            if saved.progress.state == "running" { saved.progress.state = "paused"; saved.progress.stage = "Warm-up interrupted. Resume to reuse cached work." }
            journal = saved
        }
    }
    func cancel() { context?.token.cancel() }
    func prepareForPlay() { playRequested = true; cancel() }
    func playRequestFailed() { if !SatchelNativeSessionStarted() { playRequested = false } }
    func dismiss() { guard !running else { return }; journal = nil; UserDefaults.standard.removeObject(forKey: "SatchelWarmupJob") }
    func begin(package: URL, title: String, keepAlive: Bool = true) -> Context? {
        guard !running, !playRequested, !AddOnJob.shared.running, !SatchelNativeSessionStarted() else { return nil }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        guard package.standardizedFileURL.deletingLastPathComponent() == documents.appendingPathComponent("PreparedGames").standardizedFileURL,
              let data = try? WarmupRunner.bounded(package.appendingPathComponent("manifest.json"), maximum: 32 * 1024 * 1024),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let id = object["titleID"] as? String, NativePreparationRecipe.hex(id, count: 16) else { return nil }
        let recipeName = ((object["recipe"] as? [String: Any])?["name"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines)
        let label = recipeName.flatMap { !$0.isEmpty && $0.utf8.count <= 512 ? $0 : nil } ?? title
        let value = Context(package: package, runtime: documents.appendingPathComponent("NativeRuntime/" + id), title: label, token: WarmupCancellation())
        context = value; running = true
        journal = Journal(package: package.lastPathComponent, title: value.title, progress: WarmupProgress(stage: "Checking prepared package"))
        UIApplication.shared.isIdleTimerDisabled = true
        if keepAlive { background = UIApplication.shared.beginBackgroundTask(withName: "Warm up game") { value.token.cancel() } }
        save(); return value
    }
    private func save() {
        if let journal, let data = try? JSONEncoder().encode(journal) { UserDefaults.standard.set(data, forKey: "SatchelWarmupJob") }
    }
    private func receive(_ progress: WarmupProgress, for value: Context) {
        guard context?.token === value.token else { return }
        journal = Journal(package: value.package.lastPathComponent, title: value.title, progress: progress); save()
    }
    private func finish(_ value: Context, error: Error?, verified: Bool) {
        guard context?.token === value.token else { return }
        if let error {
            var state = journal?.progress ?? WarmupProgress()
            state.state = value.token.cancelled ? "paused" : "failed"
            if verified {
                state.stage = value.token.cancelled ? "Warm-up paused. Play is ready; resume to reuse cached work." : "Warm-up stopped. Play is still ready. " + error.localizedDescription
            } else {
                state.stage = value.token.cancelled ? "Package check paused. Resume to check it before warm-up." : "Package check failed. Prepare this game again."
            }
            receive(state, for: value)
        }
        running = false; context = nil
        if !playRequested, !SatchelNativeSessionStarted(), !PrepareJob.shared.running { UIApplication.shared.isIdleTimerDisabled = false }
        endBackgroundTask()
    }
    private func endBackgroundTask() {
        if background != .invalid { UIApplication.shared.endBackgroundTask(background); background = .invalid }
    }
    /// The Prepare worker calls this within its existing continued-processing task.
    @discardableResult nonisolated static func perform(_ context: Context, verified: Bool,
                                    system: ((WarmupProgress) -> Void)? = nil) -> Bool {
        var failure: Error?, checked = verified
        do {
            if !verified { _ = try PreparedPackage.verify(root: context.package, cancelled: { context.token.cancelled }); checked = true }
            _ = try WarmupRunner.run(package: context.package, runtime: context.runtime,
                cancelled: { context.token.cancelled }) { progress in
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
            task?.expirationHandler = { value.token.cancel() }
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
