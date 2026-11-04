import SwiftUI

struct WarmupPanel: View {
    var package: String? = nil
    var title = "Prepared game"
    var play: ((String) -> Void)? = nil
    @ObservedObject private var job = WarmupJob.shared
    @ObservedObject private var prepare = PrepareJob.shared
    var body: some View {
        if let package {
            SatchelPanel(title: "Warm up shaders", tint: SatchelTheme.tide) { content(package, title: title) }
        } else if let journal = job.journal {
            SatchelPanel(title: "Warm up " + journal.title, tint: SatchelTheme.tide) { content(journal.package, title: journal.title) }
        }
    }
    @ViewBuilder private func content(_ package: String, title: String) -> some View {
        let journal = job.journal?.package == package ? job.journal : nil
        if let state = journal?.progress {
            Text(state.stage).font(.system(size: 13, weight: .semibold, design: .rounded))
            Text("\(state.shaders) shaders · \(state.pipelines + state.archiveHits) pipelines · \(state.failed) failed · \(state.deferred) deferred")
                .font(SatchelTheme.mono(10)).foregroundStyle(SatchelTheme.textDim)
            if job.running {
                if let fraction = state.fraction { SatchelProgressBar(value: fraction, tint: SatchelTheme.tide) }
                Text("\(state.completed) of \(state.total) · \(Int(state.seconds)) seconds")
                    .font(SatchelTheme.mono(11)).accessibilityIdentifier("warmup-progress")
            }
        } else {
            Text("Reduce shader pauses by warming known shaders for this device. You can play at any time; new shaders build as needed.")
                .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
        }
        HStack {
            if job.running && journal != nil {
                Button("Pause warm-up") { job.cancel() }.accessibilityIdentifier("warmup-pause")
            } else {
                Button(journal?.progress.state == "paused" ? "Resume warm-up" : "Warm up") { job.start(package: package, title: title) }
                    .disabled(job.running || prepare.running || job.playRequested || SatchelNativeSessionStarted()).accessibilityIdentifier("warmup-start")
            }
            if let play { Button("Play now", systemImage: "play.fill") { job.cancel(); play(package) }.accessibilityIdentifier("warmup-play") }
            if self.package == nil && !job.running { Button("Dismiss") { job.dismiss() } }
        }.font(.system(size: 13, weight: .semibold))
    }
}

struct WarmupHomeCard: View {
    let open: () -> Void
    let play: (String) -> Void
    @ObservedObject private var job = WarmupJob.shared
    var body: some View {
        if let journal = job.journal {
            HStack(spacing: 12) {
                Button(action: open) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Warm up " + journal.title).font(.system(size: 13, weight: .bold))
                        Text(job.running ? "\(journal.progress.stage) · \(journal.progress.completed)/\(journal.progress.total)" : journal.progress.stage)
                            .font(.system(size: 11)).lineLimit(1)
                    }
                }.accessibilityIdentifier("warmup-home-card")
                Spacer()
                Button("Play now", systemImage: "play.fill") { job.cancel(); play(journal.package) }
                if !job.running {
                    Button("Dismiss", systemImage: "xmark") { job.dismiss() }
                        .labelStyle(.iconOnly).accessibilityIdentifier("warmup-home-dismiss")
                }
            }.padding(12).background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 14))
        }
    }
}
