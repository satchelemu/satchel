// A warm-up never starts, resumes or keeps running while a game is open or on its way, on
// every path that opens a game. run.py compiles this with the production WarmupJob.swift,
// PreparedLaunch and NativePreparedCycle.requested, and runs each scenario in its own
// process with that path's launch arguments. Usage: warmup-guard-tests <scenario>.
// Each helper below does what the named app code does, in the same order.

let domain = ProcessInfo.processInfo.processName
UserDefaults.standard.removePersistentDomain(forName: domain)
let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
precondition(documents.path.hasPrefix(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] ?? "/missing"),
             "Run through run.py: the synthetic Documents folder must be private to the test")
let package = "Prepared-Synthetic", otherGame = "Prepared-Other"
for name in [package, otherGame] {
    let folder = documents.appendingPathComponent("PreparedGames/" + name)
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    try Data(#"{"titleID":"0100000000000001"}"#.utf8).write(to: folder.appendingPathComponent("manifest.json"))
}

var failures: [String] = [], checks = 0
func expect(_ condition: Bool, _ message: String) {
    checks += 1
    if !condition { failures.append(message); print("FAIL: " + message) }
}
@MainActor func pump(_ seconds: Double = 0.15) { RunLoop.main.run(until: Date().addingTimeInterval(seconds)) }
@MainActor func eventually(_ seconds: Double = 3, _ condition: () -> Bool) -> Bool {
    let deadline = Date().addingTimeInterval(seconds)
    while !condition() { if Date() > deadline { return false }; pump(0.01) }
    return true
}

/// The player's own run that iOS stopped in the background: it promises to resume on return.
func seedStoppedByIOS() {
    var progress = WarmupProgress(stage: WarmupStopWords.stoppedByIOS, completed: 1_856, total: 146_652)
    progress.state = "paused"
    let journal = WarmupJournal(package: package, title: "Synthetic game", progress: progress, interrupted: true, startedByUser: true)
    UserDefaults.standard.set(try! JSONEncoder().encode(journal), forKey: "SatchelWarmupJob")
}
/// A run an earlier release started after Prepare that iOS stopped: it waits for Resume.
func seedLegacy() {
    var progress = WarmupProgress(stage: WarmupStopWords.stoppedByIOS, completed: 3, total: 9)
    progress.state = "paused"
    let journal = WarmupJournal(package: package, title: "Synthetic game", progress: progress, interrupted: true)
    UserDefaults.standard.set(try! JSONEncoder().encode(journal), forKey: "SatchelWarmupJob")
}

@MainActor var job: WarmupJob { WarmupJob.shared }
/// HomebrewView's scene-phase handler (the app becomes active), after UIKit's notification.
@MainActor func activate() {
    UIApplication.shared.applicationState = .active
    NotificationCenter.default.post(name: UIApplication.didBecomeActiveNotification, object: nil)
    job.resumeIfInterrupted(); pump()
}
@MainActor func background() {
    UIApplication.shared.applicationState = .background
    NotificationCenter.default.post(name: UIApplication.willResignActiveNotification, object: nil); pump()
}
/// PreparedGameView's launch task: Play's request, then the package check.
@MainActor func playRequest() { job.prepareForPlay(); pump() }
/// The same task once the session is prepared: the package is owned, the request ends.
/// The session boots only when the app is active (front_ios_boot).
@MainActor func sessionPrepared(_ name: String = package) { PreparedLaunch.current = name; job.playRequestFinished(); pump() }
@MainActor func boot() { stubSessionStarted = true; pump() }
/// SatchelStopNativeSession's end, then PreparedGameView's observer clearing the package.
@MainActor func sessionEnded() { stubSessionStarted = false; PreparedLaunch.current = nil; pump() }
/// The game page's Warm up or Resume (WarmupView: job.start).
@MainActor func warmUp() { job.start(package: package, title: "Synthetic game"); pump() }
@MainActor var noRun: Bool { !job.running && WarmupRunner.runs == 0 }
@MainActor var waitsForReturn: Bool { job.journal.map(WarmupResume.resumesByItself) ?? false }
@MainActor var pausedForPlay: Bool {
    !job.running && job.journal?.progress.state == "paused" && job.journal?.progress.stage == WarmupStopWords.paused && !waitsForReturn
}
@MainActor func stopRun() { job.cancel(); _ = eventually { !job.running } }

let scenario = CommandLine.arguments.dropFirst().first ?? ""
@MainActor func run(_ scenario: String) {
switch scenario {
case "automatic-launch":
    // devicectl ... -SatchelDeveloperControls YES -SatchelAutoLaunchPreparedGame YES -SatchelPreparedGame <game>
    seedStoppedByIOS()
    UIApplication.shared.applicationState = .inactive
    activate()
    expect(noRun, "Automatic launch: the saved warm-up resumed as the app opened to start a game")
    playRequest()
    UIApplication.shared.applicationState = .inactive
    sessionPrepared()
    activate()
    expect(noRun, "Automatic launch: the saved warm-up resumed beside the prepared game before it booted")
    boot(); background(); activate()
    expect(noRun, "Automatic launch: the saved warm-up resumed on return to the running game")
    sessionEnded(); activate()
    expect(noRun, "Automatic launch: the saved warm-up resumed in a process launched to play")
    expect(waitsForReturn, "Automatic launch: the saved run lost its promise to resume on an ordinary return")

case "play":
    // The game page's Play, with the session prepared while the app was not active.
    seedStoppedByIOS()
    playRequest(); activate()
    expect(noRun, "Play: the saved warm-up resumed during the package check")
    UIApplication.shared.applicationState = .inactive
    sessionPrepared(); activate()
    expect(noRun, "Play: the saved warm-up resumed between session setup and the game's boot")
    boot(); activate()
    expect(noRun, "Play: the saved warm-up resumed beside the running game")
    sessionEnded(); activate()
    expect(eventually { job.running } && WarmupRunner.runs == 1, "Play: a run iOS stopped must still resume on return once the game ended")
    stopRun()

case "play-pauses-running":
    warmUp()
    expect(eventually { job.running && WarmupRunner.runs == 1 }, "Warm up must start a run with no game open")
    playRequest()
    expect(eventually { pausedForPlay }, "Play must pause a running warm-up with the Play is ready words, waiting for Resume")
    UIApplication.shared.applicationState = .inactive
    sessionPrepared(); activate(); boot(); background(); activate()
    expect(!job.running && WarmupRunner.runs == 1, "Play's paused warm-up started again beside the game")
    sessionEnded(); activate()
    expect(!job.running && WarmupRunner.runs == 1, "Play's paused warm-up resumed by itself after the game")
    warmUp()
    expect(eventually { job.running && WarmupRunner.runs == 2 }, "Resume must work again once the game ended")
    stopRun()

case "session-without-request":
    // Any route that opens a session without Play's request still pauses a running warm-up.
    warmUp()
    expect(eventually { job.running && WarmupRunner.runs == 1 }, "Warm up must start a run with no game open")
    PreparedLaunch.current = package
    expect(eventually { pausedForPlay }, "A game session opened beside a running warm-up and the warm-up kept running")
    warmUp()
    expect(!job.running && WarmupRunner.runs == 1, "Warm up started beside a prepared session that had not booted yet")
    boot(); warmUp()
    expect(!job.running && WarmupRunner.runs == 1, "Warm up started beside a running game")
    sessionEnded()

case "pause-menu-library":
    // Game A runs; its pause menu's Library leaves it running and opens another game.
    seedStoppedByIOS()
    playRequest(); sessionPrepared(otherGame); boot(); activate()
    warmUp()
    expect(noRun, "Warm up started while a game stays open behind the library")
    playRequest()
    sessionEnded()   // PreparedGameView stops game A before checking game B
    activate()
    expect(noRun, "Switching games: the saved warm-up resumed between the two sessions")
    UIApplication.shared.applicationState = .inactive
    sessionPrepared(); activate()
    expect(noRun, "Switching games: the saved warm-up resumed before the second game booted")
    boot(); activate()
    expect(noRun, "Switching games: the saved warm-up resumed beside the second game")
    sessionEnded(); activate()
    expect(eventually { job.running }, "Switching games: a run iOS stopped must still resume once no game is open")
    stopRun()

case "developer-cycle":
    // -SatchelDeveloperControls YES -SatchelCyclePreparedGames A,B: each visit is Play's route.
    seedStoppedByIOS()
    activate()
    for name in [package, otherGame] {
        expect(noRun, "Cycle: the saved warm-up resumed before visiting " + name)
        playRequest(); sessionPrepared(name); activate(); boot(); activate()
        expect(noRun, "Cycle: the saved warm-up resumed during the visit to " + name)
        sessionEnded(); activate()
        expect(noRun, "Cycle: the saved warm-up resumed in the gap after " + name)
    }
    expect(waitsForReturn, "Cycle: the saved run lost its promise to resume on an ordinary return")

case "return-while-starting":
    // The player leaves during the package check; it finishes in the background.
    seedStoppedByIOS()
    playRequest(); background()
    sessionPrepared(); activate()
    expect(noRun, "Return: the saved warm-up resumed as the app returned to a game waiting to boot")
    boot(); background(); activate()
    expect(noRun, "Return: the saved warm-up resumed on return to a running game")
    sessionEnded()

case "ios-stop-while-game-opens":
    // iOS ends the run's time just as a session opens: the stop keeps its promise, and the
    // job's own retry after the stop (finish) does not restart it beside the game.
    warmUp()
    expect(eventually { job.running && WarmupRunner.runs == 1 }, "Warm up must start a run with no game open")
    PreparedLaunch.current = package
    UIApplication.shared.expireAll()
    expect(eventually { !job.running }, "A run iOS stopped kept running")
    pump(0.3)
    expect(!job.running && WarmupRunner.runs == 1, "The job's retry after an iOS stop restarted the run beside the game")
    expect(waitsForReturn, "An iOS stop must still promise to resume on return")
    boot(); activate()
    expect(WarmupRunner.runs == 1, "The run iOS stopped resumed beside the running game")
    sessionEnded(); activate()
    expect(eventually { job.running } && WarmupRunner.runs == 2, "The run iOS stopped must resume on return once the game ended")
    stopRun()

case "opt-in":
    // Nothing starts a warm-up the player did not start, on any of these paths.
    seedLegacy()
    activate(); playRequest(); sessionPrepared(); activate(); boot(); sessionEnded(); activate()
    UserDefaults.standard.removeObject(forKey: "SatchelWarmupJob")
    activate()
    expect(WarmupRunner.runs == 0, "A warm-up started without the player")

default:
    print("Unknown scenario: " + scenario); exit(2)
}
}
// Top-level code runs on the main thread, the job's actor.
MainActor.assumeIsolated { run(scenario) }

UserDefaults.standard.removePersistentDomain(forName: domain)
if failures.isEmpty { print("Warm-up guard \(scenario): \(checks) checks passed") } else { exit(1) }
