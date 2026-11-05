import Foundation

/// Developer automation policy. The app adapter supplies the real menu and session events;
/// host tests supply a clock and synthetic sessions. No engine stop operation lives here.
@MainActor
final class PreparedGameCycle {
    struct Configuration {
        let packages: [String]
        let seconds: Double
        let rounds: Int

        static func parse(arguments: [String], developer: Bool) throws -> Configuration? {
            guard developer else { return nil }
            func value(_ key: String) -> String? {
                guard let i = arguments.lastIndex(of: "-" + key), i + 1 < arguments.count else { return nil }
                return arguments[i + 1]
            }
            guard arguments.contains("-SatchelCyclePreparedGames") else { return nil }
            let packages = (value("SatchelCyclePreparedGames") ?? "").components(separatedBy: ",")
            guard !packages.isEmpty, packages.count <= 64, packages.allSatisfy({
                !$0.isEmpty && $0 != "." && $0 != ".." && $0.utf8.count <= 255 &&
                !$0.contains("/") && !$0.contains("\\") && !$0.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) })
            }), let seconds = Double(value("SatchelCycleSeconds") ?? ""), seconds.isFinite,
                  seconds >= 1, seconds <= 86400,
                  let rounds = Int(value("SatchelCycleRounds") ?? ""), (1...100).contains(rounds) else {
                throw ConfigurationError.invalid
            }
            return Configuration(packages: packages, seconds: seconds, rounds: rounds)
        }
    }
    enum ConfigurationError: Error { case invalid }
    struct Metrics {
        let frames: UInt64
        let footprint: UInt64?
    }
    enum Phase { case idle, launching, playing, stopping, settling, finished }
    private(set) var phase = Phase.idle
    private(set) var failure: String?
    private(set) var package: String?
    private let configuration: Configuration
    private let write: (String) throws -> Void
    private let launch: (String) -> Void
    private let quit: () -> Bool
    private var rows: [String] = []
    private var visit = 0
    private var entered = 0.0, playStart: Double?, stopStart: Double?, lastFrameTime = 0.0
    private var start = "", frames: UInt64 = 0, footprint: UInt64?
    private var played = 0.0
    private var stopEnd: Double?
    private var ended = false
    static let header = "package\tstart\tseconds_played\tframes\tfootprint_at_quit_bytes\tstop_seconds\tresult\n"

    init(configuration: Configuration, write: @escaping (String) throws -> Void,
         launch: @escaping (String) -> Void, quit: @escaping () -> Bool) {
        self.configuration = configuration; self.write = write; self.launch = launch; self.quit = quit
    }

    func begin(now: Double, date: Date) { guard phase == .idle else { return }; next(now: now, date: date) }

    private func next(now: Double, date: Date) {
        guard visit < configuration.packages.count * configuration.rounds else { phase = .finished; return }
        package = configuration.packages[visit % configuration.packages.count]
        phase = .launching; entered = now; playStart = nil; stopStart = nil; stopEnd = nil; ended = false
        frames = 0; footprint = nil; played = 0; failure = nil
        start = ISO8601DateFormatter().string(from: date)
        rows.append("")
        if record("launching", now: now) { launch(package!) }
    }

    /// Called on the main thread. Frames come from the native guest-present counter,
    /// never from a display timer or an assumed frame rate.
    func tick(now: Double, date: Date, activePackage: String?, metrics: Metrics?, menuState: String) {
        guard phase != .idle, phase != .finished else { return }
        if phase == .settling {
            if now - entered >= 2 { visit += 1; next(now: now, date: date) }
            return
        }
        if phase == .stopping {
            if menuState == "stopFailed" { fail("stop-failed", now: now, metrics: nil, stop: false) }
            else if now - (stopStart ?? now) >= 120 { fail("stop-timeout", now: now, metrics: nil, stop: false) }
            return
        }
        if let activePackage, activePackage != package {
            fail("unexpected-package", now: now, metrics: metrics, stop: true); return
        }
        if let metrics, activePackage == package {
            if metrics.frames > frames { lastFrameTime = now }
            frames = metrics.frames
            if phase == .launching && frames > 0 {
                phase = .playing; playStart = now; stopStart = nil; start = ISO8601DateFormatter().string(from: date)
                if !record("playing", now: now) { _ = quit(); return }
            }
        }
        if phase == .launching {
            // Verification may read a large package before the native session exists.
            let deadline = activePackage == package ? 90.0 : 900.0
            if activePackage == package && metrics != nil && playStart == nil {
                if stopStart == nil { stopStart = now }
                if now - (stopStart ?? now) >= deadline { fail("first-frame-timeout", now: now, metrics: metrics, stop: true) }
            } else if now - entered >= deadline { fail("launch-timeout", now: now, metrics: metrics, stop: true) }
        } else if phase == .playing {
            played = now - (playStart ?? now)
            if now - lastFrameTime >= 60 { fail("frame-progress-timeout", now: now, metrics: metrics, stop: true) }
            else if played >= configuration.seconds { requestQuit(now: now, metrics: metrics) }
        }
    }

    private func requestQuit(now: Double, metrics: Metrics?) {
        if let metrics { frames = metrics.frames }
        footprint = metrics?.footprint; stopStart = now; phase = .stopping
        guard record(failure ?? "stopping", now: now) else { _ = quit(); return }
        if !quit() { fail("quit-rejected", now: now, metrics: nil, stop: false) }
    }

    func launchFailed(package: String, reason: String, now: Double) {
        guard self.package == package, phase == .launching else { return }
        fail(reason, now: now, metrics: nil, stop: false)
    }

    func interrupted(now: Double, metrics: Metrics?) {
        guard phase != .idle, phase != .finished else { return }
        if phase == .stopping {
            failure = failure ?? "app-backgrounded"
            _ = record(failure!, now: now)
        } else if phase == .settling {
            fail("app-backgrounded-after-stop", now: now, metrics: nil, stop: false)
        } else { fail("app-backgrounded", now: now, metrics: metrics, stop: true) }
    }

    func sessionEnded(package: String, now: Double) {
        guard self.package == package, !ended, phase != .settling else { return }
        ended = true; stopEnd = now
        if phase != .stopping {
            if phase != .finished { fail("unexpected-session-end", now: now, metrics: nil, stop: false) }
            return
        }
        guard record(failure ?? "ok", now: now) else { return }
        if failure != nil { phase = .finished }
        else { phase = .settling; entered = now }
    }

    private func fail(_ reason: String, now: Double, metrics: Metrics?, stop: Bool) {
        failure = failure ?? reason
        if phase == .playing, let playStart { played = now - playStart }
        if stop { requestQuit(now: now, metrics: metrics) }
        else { phase = .finished; _ = record(failure!, now: now) }
    }

    @discardableResult
    private func record(_ result: String, now: Double) -> Bool {
        let seconds = String(format: "%.3f", locale: Locale(identifier: "en_US_POSIX"), played)
        let stop = stopStart.map { String(format: "%.3f", locale: Locale(identifier: "en_US_POSIX"), max(0, (stopEnd ?? now) - $0)) } ?? "0.000"
        rows[rows.count - 1] = [package ?? "", start, seconds, String(frames), footprint.map(String.init) ?? "unknown", stop, result].joined(separator: "\t")
        do { try write(Self.header + rows.joined(separator: "\n") + "\n"); return true }
        catch { failure = "log-write-failed"; phase = .finished; return false }
    }
}
