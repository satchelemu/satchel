import SwiftUI

/// Shader warm-up for one prepared package (the game page, the play screen), or for the
/// journal's package when `package` is nil (the Prepare screen).
struct WarmupPanel: View {
    var package: String? = nil
    var title = "Prepared game"
    var play: ((String) -> Void)? = nil
    @ObservedObject private var job = WarmupJob.shared
    @ObservedObject private var prepare = PrepareJob.shared
    @ObservedObject private var addOns = AddOnJob.shared

    var body: some View {
        if let package {
            SatchelPanel(title: "Shaders", tint: SatchelTheme.tide) { content(package, title: title) }
        } else if let journal = job.journal {
            SatchelPanel(title: "Warm up " + journal.title, tint: SatchelTheme.tide) { content(journal.package, title: journal.title) }
        }
    }

    private enum Phase { case notStarted, running, paused, complete, partial, failed }

    private func currentPhase(_ status: WarmupJob.Status?, package: String) -> Phase {
        if job.running && job.journal?.package == package { return .running }
        switch status?.progress.state {
        case "paused", "running": return .paused
        case "complete": return .complete
        case "partial": return .partial
        case "failed": return .failed
        default: return .notStarted
        }
    }

    /// Why the button cannot be used right now, in words.
    private func blocked(_ package: String) -> String? {
        if job.running && job.journal?.package == package { return nil }
        if SatchelNativeSessionStarted() || job.playRequested { return "Quit the game to warm up shaders" }
        if prepare.running { return "Available after preparation finishes" }
        if job.running { return "Another warm-up is running" }
        if addOns.running { return "Available after Add DLC finishes" }
        return nil
    }

    @ViewBuilder private func content(_ package: String, title: String) -> some View {
        let status = job.status(for: package)
        let phase = currentPhase(status, package: package)
        let progress = status?.progress
        VStack(alignment: .leading, spacing: 8) {
            switch phase {
            case .notStarted:
                line("Optional. Builds this game's known shaders ahead of time, which can take hours for a large game. The game plays without it; new areas may stutter the first time.")
                hint(WarmupWords.keepOpen)
            case .running:
                if let progress {
                    line(progress.stage)
                    if let fraction = progress.fraction { SatchelProgressBar(value: fraction, tint: SatchelTheme.tide, height: 8) }
                    Text("\(progress.completed) of \(progress.total) · \(warmupDuration(progress.seconds))")
                        .font(SatchelTheme.mono(11)).monospacedDigit().accessibilityIdentifier("warmup-progress")
                }
                hint(WarmupWords.keepOpen)
            case .paused:
                if status?.interrupted == true {
                    line(WarmupStopWords.stoppedByIOS)
                    hint(WarmupWords.furthest)
                } else {
                    line("Paused. Resume reuses the work already cached.")
                }
                if let progress, progress.total > 0 {
                    Text("\(progress.completed) of \(progress.total) done").font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim)
                }
            case .complete:
                Label("Known shaders are warmed up", systemImage: "checkmark.circle.fill")
                    .font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.leaf)
                if let progress { counts(progress) }
                line("New shaders still build the first time they appear in play.")
            case .partial:
                line("Finished with some shaders left to build while playing.")
                if let progress { counts(progress) }
            case .failed:
                line(progress.map { $0.firstFailure.map { "Warm-up stopped: \($0)" } ?? $0.stage } ?? "Warm-up stopped.")
                line("Play is still ready.")
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("warmup-status")
        let reason = blocked(package)
        HStack(spacing: 10) {
            if phase == .running {
                Button { Haptics.click(); job.cancel() } label: {
                    SatchelButtonLabel(title: "Pause", systemImage: "pause.fill", kind: .secondary, height: 38)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("warmup-pause")
            } else {
                Button { Haptics.click(); job.start(package: package, title: title) } label: {
                    SatchelButtonLabel(title: startTitle(phase), systemImage: phase == .complete ? nil : "flame.fill",
                                      kind: phase == .complete ? .quiet : .accent(SatchelTheme.tide), height: 38)
                }
                .buttonStyle(PressableStyle())
                .disabled(reason != nil)
                .satchelDimmedWhenDisabled()
                .accessibilityHint(reason ?? "")
                .accessibilityIdentifier("warmup-start")
            }
            if let play {
                Button { job.cancel(); play(package) } label: {
                    SatchelButtonLabel(title: "Play now", systemImage: "play.fill", kind: .secondary, height: 38)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("warmup-play")
            }
            if self.package == nil && !job.running {
                Button("Dismiss") { job.dismiss() }.font(.system(size: 13, weight: .semibold))
            }
        }
        if phase != .running, let reason { SatchelDisabledReason(text: reason).accessibilityIdentifier("warmup-start-reason") }
    }

    private func startTitle(_ phase: Phase) -> String {
        switch phase {
        case .notStarted, .running: return "Warm up"
        case .paused: return "Resume"
        case .complete, .partial: return "Warm up again"
        case .failed: return "Try again"
        }
    }

    private func line(_ text: String) -> some View {
        Text(text).font(.system(size: 13, weight: .semibold, design: .rounded))
            .foregroundStyle(SatchelTheme.text).fixedSize(horizontal: false, vertical: true)
    }

    private func hint(_ text: String) -> some View {
        Text(text).font(.system(size: 11, weight: .medium, design: .rounded))
            .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
    }

    private func counts(_ progress: WarmupProgress) -> some View {
        var parts = ["\(progress.shaders) shaders", "\(progress.pipelines + progress.archiveHits) pipelines"]
        if progress.deferred > 0 { parts.append("\(progress.deferred) left to gameplay") }
        if progress.failed > 0 { parts.append("\(progress.failed) failed") }
        parts.append(warmupDuration(progress.seconds))
        return Text(parts.joined(separator: " · ")).font(SatchelTheme.mono(10)).foregroundStyle(SatchelTheme.textDim)
    }
}

/// What a long warm-up needs, said plainly: iOS gives a background task little time and
/// may stop it, so a large game only gets far in the foreground on power.
enum WarmupWords {
    static let keepOpen = "A large game only gets far with \(AppBrand.name) open, the screen on and the phone charging. In the background iOS may stop it; it resumes when you return."
    static let furthest = "It gets furthest with \(AppBrand.name) open, the screen on and the phone charging."
    static let card = "Keep \(AppBrand.name) open and the phone charging"
}

private func warmupDuration(_ seconds: Double) -> String {
    let value = max(0, Int(seconds))
    return value < 60 ? "\(value)s" : "\(value / 60)m \(value % 60)s"
}

struct WarmupHomeCard: View {
    /// Opens the game page for the warming package (or its play screen).
    let open: (String) -> Void
    let play: (String) -> Void
    /// False for a package this build cannot start (an older engine made it, or it failed its
    /// check): the card offers Prepare again, through the game page, and never Play.
    var playable = true
    @ObservedObject private var job = WarmupJob.shared
    var body: some View {
        if let journal = job.journal {
            HStack(spacing: 12) {
                Button { open(journal.package) } label: {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Warm up " + journal.title).font(.system(size: 13, weight: .bold))
                        Text(job.running ? "\(journal.progress.stage) · \(journal.progress.completed)/\(journal.progress.total)"
                             : playable ? journal.progress.stage : "Needs Prepare again before it can be played.")
                            .font(.system(size: 11)).lineLimit(2).fixedSize(horizontal: false, vertical: true)
                        if job.running {
                            // Two lines on a narrow screen rather than a cut-off sentence.
                            Text(WarmupWords.card).font(.system(size: 10)).foregroundStyle(SatchelTheme.textDim)
                                .lineLimit(2).fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }.accessibilityIdentifier("warmup-home-card")
                Spacer()
                if playable {
                    Button("Play now", systemImage: "play.fill") { job.cancel(); play(journal.package) }
                        .accessibilityIdentifier("warmup-home-play")
                } else if !job.running {
                    Button("Prepare again", systemImage: "hammer.fill") { open(journal.package) }
                        .accessibilityIdentifier("warmup-home-prepare")
                }
                if !job.running {
                    Button("Dismiss", systemImage: "xmark") { job.dismiss() }
                        .labelStyle(.iconOnly).accessibilityIdentifier("warmup-home-dismiss")
                }
            }.padding(12).background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 14))
        }
    }
}
