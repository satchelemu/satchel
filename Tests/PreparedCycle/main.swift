import Foundation

@MainActor
func run() throws {
    let args = ["app", "-SatchelCyclePreparedGames", "A,B,C", "-SatchelCycleSeconds", "5", "-SatchelCycleRounds", "2"]
    let disabled = try PreparedGameCycle.Configuration.parse(arguments: args, developer: false)
    assert(disabled == nil)
    let config = try PreparedGameCycle.Configuration.parse(arguments: args, developer: true)!
    for (index, bad) in [(2, ""), (2, "A,,B"), (2, "../A"), (2, "A\tB"), (4, "nan"), (4, "inf"), (4, "0"), (4, "86401"), (6, "0"), (6, "101"), (6, "2.5")] {
        var input = args; input[index] = bad
        do { _ = try PreparedGameCycle.Configuration.parse(arguments: input, developer: true); fatalError("accepted invalid configuration") }
        catch PreparedGameCycle.ConfigurationError.invalid {}
    }
    var log = "", launches: [String] = [], quits = 0
    let date = Date(timeIntervalSince1970: 0)
    let cycle = PreparedGameCycle(configuration: config, write: { log = $0 }, launch: { launches.append($0) }, quit: { quits += 1; return true })
    cycle.begin(now: 0, date: date)
    cycle.begin(now: 0, date: date)
    for visit in 0..<6 {
        let t = Double(visit * 10), package = ["A", "B", "C"][visit % 3]
        cycle.tick(now: t + 1, date: date, activePackage: package, metrics: .init(frames: 1, footprint: 100), menuState: "running")
        cycle.tick(now: t + 6, date: date, activePackage: package, metrics: .init(frames: 300, footprint: 123456), menuState: "running")
        assert(quits == visit + 1 && cycle.phase == .stopping)
        cycle.tick(now: t + 7, date: date, activePackage: nil, metrics: nil, menuState: "ended")
        assert(launches.count == visit + 1, "must wait for end notification")
        cycle.sessionEnded(package: "unrelated", now: t + 8)
        assert(cycle.phase == .stopping)
        cycle.sessionEnded(package: package, now: t + 8)
        cycle.sessionEnded(package: package, now: t + 8)
        cycle.tick(now: t + 10, date: date, activePackage: nil, metrics: nil, menuState: "ended")
    }
    assert(cycle.phase == .finished && cycle.failure == nil)
    assert(launches == ["A", "B", "C", "A", "B", "C"])
    let rows = log.split(separator: "\n"); assert(rows.count == 7)
    for row in rows.dropFirst() { assert(row.hasSuffix("\t5.000\t300\t123456\t2.000\tok")) }
    print("PASS: developer gate, 11 malformed configurations, six ordered visits, notification wait, exact telemetry and duplicate end")

    func fixture() -> PreparedGameCycle {
        PreparedGameCycle(configuration: config, write: { log = $0 }, launch: { _ in }, quit: { quits += 1; return true })
    }
    let failed = fixture(); failed.begin(now: 0, date: date)
    failed.launchFailed(package: "A", reason: "package-not-found", now: 1)
    failed.tick(now: 1000, date: date, activePackage: nil, metrics: nil, menuState: "ended")
    assert(failed.phase == .finished && log.contains("package-not-found"))
    let launchTimeout = fixture(); launchTimeout.begin(now: 0, date: date)
    launchTimeout.tick(now: 900, date: date, activePackage: nil, metrics: nil, menuState: "running")
    assert(launchTimeout.failure == "launch-timeout")
    let noFrame = fixture(); noFrame.begin(now: 0, date: date)
    noFrame.tick(now: 1, date: date, activePackage: "A", metrics: .init(frames: 0, footprint: nil), menuState: "running")
    noFrame.tick(now: 91, date: date, activePackage: "A", metrics: .init(frames: 0, footprint: nil), menuState: "running")
    assert(noFrame.failure == "first-frame-timeout" && log.contains("unknown"))
    noFrame.sessionEnded(package: "A", now: 92); assert(noFrame.phase == .finished)
    for reason in ["stop-failed", "stop-timeout", "frame-progress-timeout", "app-backgrounded", "unexpected-session-end"] {
        let f = fixture(); f.begin(now: 0, date: date)
        f.tick(now: 1, date: date, activePackage: "A", metrics: .init(frames: 1, footprint: 100), menuState: "running")
        if reason.hasPrefix("stop-") {
            f.tick(now: 6, date: date, activePackage: "A", metrics: .init(frames: 300, footprint: 100), menuState: "running")
            f.tick(now: 126, date: date, activePackage: "A", metrics: nil, menuState: reason == "stop-failed" ? "stopFailed" : "quitting")
        } else if reason == "frame-progress-timeout" {
            f.tick(now: 61, date: date, activePackage: "A", metrics: .init(frames: 1, footprint: 100), menuState: "running")
        } else if reason == "app-backgrounded" { f.interrupted(now: 2, metrics: .init(frames: 1, footprint: 100)) }
        else { f.sessionEnded(package: "A", now: 2) }
        assert(f.failure == reason && log.contains(reason))
        if f.phase == .stopping { f.sessionEnded(package: "A", now: 127) }
        assert(f.phase == .finished)
    }
    for afterEnd in [false, true] {
        let f = fixture(); f.begin(now: 0, date: date)
        f.tick(now: 1, date: date, activePackage: "A", metrics: .init(frames: 1, footprint: 100), menuState: "running")
        f.tick(now: 6, date: date, activePackage: "A", metrics: .init(frames: 300, footprint: 100), menuState: "running")
        if afterEnd { f.sessionEnded(package: "A", now: 7) }
        f.interrupted(now: 8, metrics: nil)
        if !afterEnd { f.sessionEnded(package: "A", now: 9) }
        f.tick(now: 10, date: date, activePackage: nil, metrics: nil, menuState: "ended")
        assert(f.phase == .finished && f.failure?.hasPrefix("app-backgrounded") == true)
        if afterEnd { assert(log.contains("\t1.000\tapp-backgrounded-after-stop")) }
    }
    let rejected = PreparedGameCycle(configuration: config, write: { log = $0 }, launch: { _ in }, quit: { false })
    rejected.begin(now: 0, date: date); rejected.interrupted(now: 1, metrics: nil)
    assert(rejected.phase == .finished && rejected.failure == "app-backgrounded")
    enum DiskError: Error { case full }
    var launched = false
    let disk = PreparedGameCycle(configuration: config, write: { _ in throw DiskError.full }, launch: { _ in launched = true }, quit: { true })
    disk.begin(now: 0, date: date)
    assert(!launched && disk.failure == "log-write-failed" && disk.phase == .finished)
    print("PASS: missing package, launch/first-frame/progress/stop timeouts, stop failure, play/stop/settle interruptions, unexpected end, rejected quit and log failure")
}
try MainActor.assumeIsolated { try run() }
