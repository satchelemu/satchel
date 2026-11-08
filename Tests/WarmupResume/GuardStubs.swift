// Mac stand-ins for the iOS and app types WarmupJob.swift uses, so run.py compiles the
// production job unchanged (only its imports differ). Synthetic state only: no package,
// shader or game data. The scenarios set these fields the way the app's paths do.

/// `SatchelNativeSessionStarted()`: true once the session booted (front_ios_boot), which
/// waits for the app to be active.
var stubSessionStarted = false
func SatchelNativeSessionStarted() -> Bool { stubSessionStarted }

struct UIBackgroundTaskIdentifier: Equatable {
    let raw: Int
    static let invalid = UIBackgroundTaskIdentifier(raw: 0)
}

final class UIApplication {
    static let shared = UIApplication()
    enum State { case active, inactive, background }
    static let didBecomeActiveNotification = Notification.Name("StubDidBecomeActive")
    static let willResignActiveNotification = Notification.Name("StubWillResignActive")
    static let didReceiveMemoryWarningNotification = Notification.Name("StubMemoryWarning")
    var applicationState = State.active
    var isIdleTimerDisabled = false
    /// Expiration handlers of open background tasks: `expireAll` is iOS ending the time.
    private var handlers: [Int: () -> Void] = [:]
    private var next = 1
    func beginBackgroundTask(withName name: String?, expirationHandler: (() -> Void)?) -> UIBackgroundTaskIdentifier {
        next += 1; handlers[next] = expirationHandler ?? {}; return UIBackgroundTaskIdentifier(raw: next)
    }
    func endBackgroundTask(_ identifier: UIBackgroundTaskIdentifier) { handlers[identifier.raw] = nil }
    func expireAll() { let open = handlers.values; handlers = [:]; open.forEach { $0() } }
}

final class UIDevice {
    static let current = UIDevice()
    enum BatteryState { case unknown, unplugged, charging, full }
    static let batteryStateDidChangeNotification = Notification.Name("StubBatteryState")
    var batteryState = BatteryState.unplugged
    var isBatteryMonitoringEnabled = false
}

// BackgroundTasks: registration is refused, so `start` takes its foreground fallback.
class BGTask {
    var expirationHandler: (() -> Void)?
    func setTaskCompleted(success: Bool) {}
}
final class BGContinuedProcessingTask: BGTask {
    let progress = Progress()
    func updateTitle(_ title: String, subtitle: String) {}
}
final class BGContinuedProcessingTaskRequest {
    enum Strategy { case fail, queue }
    var strategy = Strategy.queue
    init(identifier: String, title: String, subtitle: String) {}
}
final class BGTaskScheduler {
    static let shared = BGTaskScheduler()
    func register(forTaskWithIdentifier identifier: String, using queue: DispatchQueue?, launchHandler: @escaping (BGTask) -> Void) -> Bool { false }
    func submitTaskRequest(_ request: BGContinuedProcessingTaskRequest, completion: @escaping (Error?) -> Void) { completion(CancellationError()) }
    func submit(_ request: BGContinuedProcessingTaskRequest) throws { throw CancellationError() }
}

struct SatchelWarmupPolicy: Equatable, Sendable {
    let lanes: Int
    let foreground: Bool
    static let conservative = SatchelWarmupPolicy(lanes: 2, foreground: false)
    static func select(foreground: Bool, charging: Bool, constrained: Bool) -> Self {
        Self(lanes: !constrained && (foreground || charging) ? 4 : 2, foreground: foreground && !constrained)
    }
}
struct SatchelWarmupTrial {
    let name: String? = nil
    let records: Int? = nil
    init(arguments: [String]) {}
}
struct NativePreparationRecipe {
    static func hex(_ value: String, count: Int) -> Bool {
        value.count == count && value.allSatisfy { $0.isHexDigit }
    }
}
enum PreparedPackage {
    static func verify(root: URL, cancelled: () -> Bool) throws -> URL {
        if cancelled() { throw CancellationError() }
        return root
    }
}

/// Synthetic shader work: one progress report every 10 ms until cancelled. `runs` counts
/// every run that began, from any thread.
enum WarmupRunner {
    private static let lock = NSLock()
    nonisolated(unsafe) private static var started = 0
    static var runs: Int { lock.lock(); defer { lock.unlock() }; return started }
    static func bounded(_ url: URL, maximum: Int) throws -> Data {
        let data = try Data(contentsOf: url)
        guard data.count <= maximum else { throw CancellationError() }
        return data
    }
    static func run(package: URL, runtime: URL, cancelled: @escaping () -> Bool, captureDirectory: URL? = nil,
                    execution: () -> SatchelWarmupPolicy = { .conservative }, expired: () -> Bool = { false },
                    progress: @escaping (WarmupProgress) -> Void) throws -> WarmupProgress {
        lock.lock(); started += 1; lock.unlock()
        var state = WarmupProgress(stage: "Synthetic shader work", completed: 0, total: 1_000_000)
        let deadline = Date().addingTimeInterval(30)
        while !cancelled() {
            guard Date() < deadline else { throw CancellationError() }
            state.completed += 1; state.unitsDone = state.completed
            progress(state)
            Thread.sleep(forTimeInterval: 0.01)
        }
        throw CancellationError()
    }
}

@MainActor final class PrepareJob {
    static let shared = PrepareJob()
    var running = false
    // The production line, staged by run.py from NativePrepare/PrepareJob.swift.
    __GAME_SESSION_OPEN__
}
@MainActor final class AddOnJob {
    static let shared = AddOnJob()
    var running = false
}
