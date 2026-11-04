import Foundation

@main struct PrepareProgressTests {
    static func main() throws {
        var events: [PrepareProgressSnapshot] = []
        let progress = PrepareProgress { events.append($0) }
        precondition(PrepareStep.allCases.reduce(0) { $0 + $1.weight } == 100)
        for step in PrepareStep.allCases {
            progress.update(step, 0, 100, force: true)
            precondition(events.last!.steps.first { $0.step == step }!.state == "current")
            progress.update(step, 50, 100, force: true)
            precondition(events.last!.completedBytes == 50 && events.last!.totalBytes == 100)
            precondition(events.last!.stepFraction == 0.5)
            // A new analysis subpass can reset byte counts, but neither bar regresses.
            progress.update(step, 1, 200, fraction: 0.25, force: true)
            precondition(events.last!.stepFraction == 0.5)
            let count = events.count
            progress.update(step, 2, 200, fraction: 0.5)
            precondition(events.count == count, "Callbacks must be throttled")
            progress.finish(step)
            precondition(events.last!.steps.first { $0.step == step }!.state == "done")
        }
        precondition(events.last!.overallFraction == 1)
        precondition(zip(events, events.dropFirst()).allSatisfy { $0.overallFraction <= $1.overallFraction })
        precondition(events.allSatisfy { $0.overallFraction.isFinite && (0...1).contains($0.overallFraction) })
        let data = try JSONEncoder().encode(events.last!)
        let decoded = try JSONDecoder().decode(PrepareProgressSnapshot.self, from: data)
        precondition(decoded.overallFraction == 1 && decoded.steps.allSatisfy { $0.state == "done" })
        print("PASS: weighted progress, byte counters, monotonic subpasses, throttling and JSON round trip")
    }
}
