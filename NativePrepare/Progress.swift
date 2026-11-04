import Foundation

enum PrepareStep: String, CaseIterable, Codable, Sendable {
    case planning, identification, authentication, composition, rewriting, container, romfs, shaders, signing, binding
    var title: String {
        switch self {
        case .identification: return "Identify game"
        case .authentication: return "Authenticate content"
        case .composition: return "Compose base and update"
        case .planning: return "Check files and storage"
        case .rewriting: return "Rewrite game code"
        case .container: return "Build native container"
        case .romfs: return "Write game data"
        case .shaders: return "Copy and verify shaders"
        case .signing: return "Sign and verify container"
        case .binding: return "Bind and publish package"
        }
    }
    // Expected cost weights, not a claim that all devices take the same time.
    var weight: Double {
        switch self {
        case .identification: return 1
        case .authentication: return 7
        case .composition: return 2
        case .planning: return 6
        case .rewriting: return 22
        case .container: return 1
        case .romfs: return 28
        case .shaders: return 30
        case .signing: return 1
        case .binding: return 2
        }
    }
}

struct PrepareProgressSnapshot: Codable, Sendable {
    struct Row: Codable, Sendable {
        let step: PrepareStep
        let title: String
        let state: String
        let fraction: Double
    }
    let step: PrepareStep
    let message: String
    let stepFraction: Double
    let overallFraction: Double
    let completedBytes: UInt64
    let totalBytes: UInt64
    let elapsedSeconds: Double
    let estimatedRemainingSeconds: Double?
    let steps: [Row]
}

/// One synchronous reporter drives the UI, system activity, JSON and harness.
/// Fractions advance only on work callbacks. Animation never invents completed work.
final class PrepareProgress {
    private let started = ProcessInfo.processInfo.systemUptime
    private var current: PrepareStep = .planning
    private var values: [PrepareStep: Double] = [:]
    private var lastEmit = -Double.infinity
    private let sink: (PrepareProgressSnapshot) -> Void
    init(_ sink: @escaping (PrepareProgressSnapshot) -> Void) { self.sink = sink }
    func update(_ step: PrepareStep, _ bytes: UInt64, _ total: UInt64, message: String? = nil,
                fraction: Double? = nil, force: Bool = false) {
        let changed = step != current
        current = step
        let value = min(1, max(0, fraction ?? (total == 0 ? 0 : Double(bytes) / Double(total))))
        values[step] = max(values[step] ?? 0, value)
        let now = ProcessInfo.processInfo.systemUptime
        guard force || changed || value == 1 || now - lastEmit >= 0.2 else { return }
        lastEmit = now
        let overall = PrepareStep.allCases.reduce(0.0) { $0 + $1.weight * (values[$1] ?? 0) } / 100
        let elapsed = now - started
        let remaining = elapsed >= 5 && overall >= 0.02 && overall < 1 ? elapsed * (1 - overall) / overall : nil
        sink(PrepareProgressSnapshot(step: step, message: message ?? step.title, stepFraction: values[step] ?? 0,
            overallFraction: overall, completedBytes: bytes, totalBytes: total, elapsedSeconds: elapsed,
            estimatedRemainingSeconds: remaining, steps: PrepareStep.allCases.map {
                let f = values[$0] ?? 0
                return .init(step: $0, title: $0.title, state: f == 1 ? "done" : $0 == step ? "current" : "pending", fraction: f)
            }))
    }
    func finishOnce(_ step: PrepareStep) { if values[step] != 1 { finish(step) } }
    func finish(_ step: PrepareStep, message: String? = nil) { update(step, 0, 0, message: message, fraction: 1, force: true) }
}
