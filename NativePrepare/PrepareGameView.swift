import SwiftUI

struct PrepareGameView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    /// Opens a finished package (its folder name) in the prepared games list.
    let play: (String) -> Void
    var initialBaseID: String? = nil
    /// The app's one job: it keeps running when this screen closes.
    @ObservedObject private var job = PrepareJob.shared
    @ObservedObject private var warmup = WarmupJob.shared
    @ObservedObject private var addOnJob = AddOnJob.shared
    @State private var showGameReport = false
    @State private var baseID = ""
    @State private var updateID = ""
    /// The base the update choice belongs to: a new base gets its own newest update preselected.
    @State private var updateBaseID = ""
    @State private var applicationID = ""
    @Environment(\.dismiss) private var dismiss
    private var bases: [HomebrewItem] { library.items.filter { $0.package != nil && artwork.info($0)?.kind != .addOnContent && library.hasOriginal($0) } }
    private var updates: [HomebrewItem] { library.items.filter { ["nsp", "nsz"].contains($0.fileExtension) && artwork.info($0)?.kind != .addOnContent && library.hasOriginal($0) } }
    /// Imported DLC that Prepare will include for the chosen game (PrepareJob passes every
    /// imported DLC; the pipeline keeps the ones whose application matches).
    private var addOns: [SwitchAddOnInfo] {
        guard let base = bases.first(where: { $0.id == baseID }) else { return [] }
        let target = applicationID.isEmpty ? artwork.info(base)?.applicationID : UInt64(applicationID, radix: 16)
        guard let target else { return [] }
        let infos = library.items.filter { library.hasOriginal($0) }.compactMap { artwork.info($0) }
        return GameInfo.addOns(for: target, in: infos)
    }
    /// The newest imported update for the chosen base, matched as the shelf groups updates.
    private var matchingUpdate: HomebrewItem? {
        guard let base = bases.first(where: { $0.id == baseID }), let info = artwork.info(base),
              let target = applicationID.isEmpty ? info.applicationID : UInt64(applicationID, radix: 16) else { return nil }
        let candidates = updates.compactMap { item in artwork.info(item).map { (id: item.id, info: $0) } }
        guard let id = GameInfo.newestUpdate(for: target, baseVersion: info.version, in: candidates) else { return nil }
        return updates.first { $0.id == id }
    }
    /// When the base changes, choose its newest imported update (or none when it has none).
    private func preselectUpdate() {
        guard !job.running, updateBaseID != baseID else { return }
        updateBaseID = baseID
        updateID = matchingUpdate?.id ?? ""
    }
    /// A plain note when a matching update is imported but set aside, or a newer one than the
    /// chosen update is. Prepare still works.
    private var updateCaution: String? {
        guard let base = bases.first(where: { $0.id == baseID }), let info = artwork.info(base),
              let target = applicationID.isEmpty ? info.applicationID : UInt64(applicationID, radix: 16) else { return nil }
        let candidates = updates.compactMap { item in artwork.info(item).map { (id: item.id, info: $0) } }
        return PrepareUpdateCaution.text(base: info, applicationID: target,
                                         selected: updates.first { $0.id == updateID }.flatMap { artwork.info($0) }, among: candidates)
    }
    /// Play is offered only for a package this build can start.
    private var warmupPlayable: Bool {
        guard let package = warmup.journal?.package else { return false }
        return warmup.journal?.packageFailed != true && artwork.prepared.contains { $0.folder == package && $0.current }
    }
    private var lockedReason: String { "Locked while \(job.title.isEmpty ? "a game" : job.title) prepares. Closing this screen keeps it going." }

    var body: some View {
        // Close always works: the job belongs to the app, so closing returns to the library
        // and preparation continues (the home screen shows its progress).
        PanelChrome(closeTitle: "Library", identifier: "close-prepare", close: { dismiss() }) {
            ScreenReader { cut, geometry in
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: the signing identity on the first page, preparation on the second.
                        FoldSplit(fold: fold, cut: cut) {
                            column { identity }
                        } second: {
                            column { preparation }.padding(.trailing, fold.isVertical ? reserve : 0)
                        }
                    } else {
                        let compact = geometry.size.width - cut.leading - cut.trailing < 760
                        HStack(alignment: .top, spacing: compact ? 16 : 24) {
                            column { identity }.frame(width: compact ? 280 : 340)
                            column { preparation }.padding(.trailing, reserve)
                        }
                        .cutoutPadding(cut, leading: 20, trailing: 20)
                    }
                }
            }
            .background { SatchelBackdrop(glow: SatchelTheme.leaf) }
            .satchelMenuStatus()
        }
        .sheet(isPresented: $showGameReport) { NativeGameReportView(package: job.package, titleID: "") }
        .onAppear {
            if !job.running, let initialBaseID, baseID.isEmpty { baseID = initialBaseID; preselectUpdate() }
            // Show the running or last job's choices.
            if !job.baseID.isEmpty, baseID.isEmpty { baseID = job.baseID; updateID = job.updateID; updateBaseID = job.baseID }
            // Developer controls: `-SatchelAutoPrepare YES` lets the Mac start Prepare with no taps, using the imported
            // Zelda base and update (names matched case-insensitively). Once per launch.
            guard UserDefaults.standard.bool(forKey: "SatchelDeveloperControls"), UserDefaults.standard.bool(forKey: "SatchelAutoPrepare"), !job.running, !job.autoPrepareUsed else { return }
            job.autoPrepareUsed = true
            let match = { (item: HomebrewItem) in item.name.lowercased().contains("zelda") }
            guard let base = bases.first(where: { match($0) && $0.fileExtension == "xci" }) ?? bases.first(where: match) else {
                job.note("Auto prepare: no imported Zelda base found."); return
            }
            baseID = base.id; updateBaseID = base.id
            let update = updates.first(where: { match($0) && $0.id != base.id })
            updateID = update?.id ?? ""
            job.start(base: base, update: update, title: artwork.title(base), library: library, artwork: artwork,
                      applicationID: applicationID.isEmpty ? artwork.info(base)?.applicationID : UInt64(applicationID, radix: 16))
        }
        .onChange(of: baseID) { preselectUpdate() }
    }

    private func column<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) { content() }.padding(.vertical, 18)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
    }

    @ViewBuilder private var identity: some View {
        VStack(alignment: .leading, spacing: 6) {
            SatchelEyebrow(text: "\(AppBrand.name) Dev", tint: SatchelTheme.leaf)
            Text("Prepare game").font(SatchelTheme.display(28))
            Text("Prepares the game's code and files on this iPhone, then signs them with your identity.")
                .font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
        }
        SigningIdentityView().disabled(job.running)
        if job.running { SatchelDisabledReason(text: "The signing identity is in use until preparation ends.") }
    }

    /// Why Prepare cannot start yet, or nil when it can.
    private var startBlocked: String? {
        if job.running { return lockedReason }
        if PrepareJob.gameSessionOpen { return PrepareJob.sessionBlocked }
        if warmup.running { return "Pause shader warm-up before preparing another game." }
        // PrepareJob.start ignores a tap while Add DLC runs: say why instead.
        if addOnJob.running { return "Wait for Add DLC to finish, or cancel it before preparing." }
        if !artwork.keysAvailable { return FirstRunHelp.noKeys }
        if bases.isEmpty { return "Import a base game (XCI or NSP) first." }
        if !bases.contains(where: { $0.id == baseID }) { return "Choose a base game to prepare." }
        if case .failed(let reason) = artwork.status[baseID] { return reason }
        if !applicationID.isEmpty && !NativePreparationRecipe.hex(applicationID, count: 16) { return "Enter a 16-digit hexadecimal title ID, or leave it empty for automatic selection." }
        return nil
    }

    /// Live progress or the last result first, so reopening the screen shows it straight away.
    @ViewBuilder private var preparation: some View {
        if job.running || job.outcome != nil || warmup.journal != nil {
            statusPanel
            gamePanel
        } else {
            gamePanel
            statusPanel
        }
    }

    private var gamePanel: some View {
        SatchelPanel(title: "Game", tint: SatchelTheme.ember) {
            Group {
                picker("Base game", detail: "An imported XCI or NSP", systemImage: "gamecontroller.fill", tint: SatchelTheme.ember,
                       items: bases, none: "Select base", selection: $baseID, identifier: "prepare-base")
                picker("Update", detail: "The matching update NSP", systemImage: "arrow.up.circle.fill", tint: SatchelTheme.violet,
                       items: updates, none: "No update selected", selection: $updateID, identifier: "prepare-update")
                if let updateCaution {
                    SatchelDisabledReason(text: updateCaution, systemImage: "exclamationmark.triangle.fill").accessibilityIdentifier("prepare-update-caution")
                }
                addOnRow
            }
            .disabled(job.running)
            TextField("Title ID (optional for multi-title dumps)", text: $applicationID)
                .textInputAutocapitalization(.characters).autocorrectionDisabled().disabled(job.running)
                .accessibilityIdentifier("prepare-title-id")
            Text("Preparation keeps going when you close this screen or switch apps. Known shaders warm up afterwards; the game page shows their progress. New titles use experimental settings, explained in the game report.")
                .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
            Button {
                guard let base = bases.first(where: { $0.id == baseID }) else { return }
                job.start(base: base, update: updates.first { $0.id == updateID }, title: artwork.title(base), library: library,
                          artwork: artwork, applicationID: applicationID.isEmpty ? artwork.info(base)?.applicationID : UInt64(applicationID, radix: 16))
            } label: {
                SatchelButtonLabel(title: "Prepare game", systemImage: "hammer.fill", kind: .accent(SatchelTheme.ember), height: 48)
            }
            .buttonStyle(PressableStyle())
            .disabled(startBlocked != nil)
            .accessibilityHint(startBlocked ?? "")
            .accessibilityIdentifier("prepare-start")
            if let startBlocked { SatchelDisabledReason(text: startBlocked).accessibilityIdentifier("prepare-start-reason") }
        }
    }

    private var statusPanel: some View {
        VStack(spacing: 14) {
            WarmupPanel(play: warmupPlayable ? play : nil)
            SatchelPanel(title: job.running ? "Preparing" : "Status", tint: job.running ? SatchelTheme.tide : statusTint) {
                Text(job.message).font(.system(size: 14, weight: .semibold, design: .rounded))
                    .fixedSize(horizontal: false, vertical: true)
                    .accessibilityIdentifier("prepare-status")
                // While it runs the message follows the steps; the finished message includes this.
                if job.running, let warning = job.updateWarning {
                    SatchelDisabledReason(text: warning, systemImage: "exclamationmark.triangle.fill").accessibilityIdentifier("prepare-update-warning")
                }
                if !job.package.isEmpty {
                    Button("Game diagnostics") { showGameReport = true }.accessibilityIdentifier("prepare-game-diagnostics")
                    ShareLink(item: NativeGameReport.url(package: job.package)) { Label("Export preparation report", systemImage: "square.and.arrow.up") }
                }
                if job.running || job.progressSnapshot != nil {
                    progressDetails
                }
                if !job.running, let outcome = job.outcome { finishedActions(outcome) }
            }
        }
    }

    private var statusTint: Color {
        switch job.outcome?.kind {
        case .signed: return SatchelTheme.leaf
        case .needsSignature: return SatchelTheme.spark
        case .failed, .cancelled: return SatchelTheme.ember
        case nil: return SatchelTheme.textDim
        }
    }

    /// Play for a ready package, Retry for one that failed.
    @ViewBuilder private func finishedActions(_ outcome: PrepareJob.Outcome) -> some View {
        HStack(spacing: 10) {
            Spacer()
            switch outcome.kind {
            case .signed:
                Button { Haptics.click(); play(outcome.package) } label: {
                    SatchelButtonLabel(title: "Play \(outcome.package)", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 40)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("prepare-play")
            case .failed, .cancelled:
                Button { Haptics.click(); job.retry(library: library, artwork: artwork, title: artwork.title) } label: {
                    SatchelButtonLabel(title: "Retry", systemImage: "arrow.clockwise", kind: .secondary, height: 40)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("prepare-retry")
            case .needsSignature:
                EmptyView()
            }
        }
    }

    private var progressDetails: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Overall").font(SatchelTheme.label(11)).foregroundStyle(SatchelTheme.textDim)
                Spacer()
                Text("\(Int((job.fraction ?? 0) * 100))%")
                    .font(SatchelTheme.mono(13)).monospacedDigit()
            }
            SatchelProgressBar(value: job.fraction ?? 0, tint: SatchelTheme.tide)
                .accessibilityIdentifier("prepare-overall-progress")
            TimelineView(.periodic(from: .now, by: 1)) { context in
                let elapsed = job.running ? context.date.timeIntervalSince(job.startedAt ?? context.date) : job.progressSnapshot?.elapsedSeconds ?? 0
                HStack(spacing: 8) {
                    if job.running {
                        Image(systemName: "arrow.triangle.2.circlepath")
                            .rotationEffect(.degrees(context.date.timeIntervalSince1970.truncatingRemainder(dividingBy: 6) * 60))
                            .animation(.linear(duration: 1), value: context.date)
                            .foregroundStyle(SatchelTheme.tide)
                            .accessibilityLabel("Preparation is active")
                    }
                    Text("\(prepareDuration(elapsed)) elapsed").monospacedDigit()
                    Spacer()
                    if job.running, let remaining = job.progressSnapshot?.estimatedRemainingSeconds {
                        Text("About \(prepareDuration(remaining)) left")
                    } else if job.running { Text("Estimating time…") }
                }
                .font(SatchelTheme.mono(10)).foregroundStyle(SatchelTheme.textDim)
            }
            ForEach(PrepareStep.allCases, id: \.self) { step in
                let row = job.progressSnapshot?.steps.first { $0.step == step }
                let current = job.progressSnapshot?.step == step && row?.state != "done"
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: row?.state == "done" ? "checkmark.circle.fill" : current ? "circle.inset.filled" : "circle")
                        .foregroundStyle(row?.state == "done" ? SatchelTheme.leaf : current ? SatchelTheme.tide : SatchelTheme.textDim)
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Text(step.title).font(.system(size: 12, weight: current ? .bold : .medium, design: .rounded))
                            Spacer()
                            if current { Text("\(Int((job.progressSnapshot?.stepFraction ?? 0) * 100))%") .font(SatchelTheme.mono(10)) }
                        }
                        if current, let value = job.progressSnapshot {
                            SatchelProgressBar(value: value.stepFraction, tint: SatchelTheme.tide)
                            if value.totalBytes > 0 {
                                Text("\(bytes(value.completedBytes)) of \(bytes(value.totalBytes))")
                                    .font(SatchelTheme.mono(10)).foregroundStyle(SatchelTheme.textDim)
                                    .accessibilityIdentifier("prepare-step-bytes")
                            }
                        }
                    }
                }
                .foregroundStyle(current ? SatchelTheme.text : SatchelTheme.textDim)
            }
            if job.running {
                HStack {
                    Spacer()
                    Button { job.cancel() } label: {
                        SatchelButtonLabel(title: job.cancelling ? "Cancelling…" : "Cancel preparation", systemImage: "xmark", kind: .danger, height: 38)
                    }
                    .buttonStyle(PressableStyle())
                    .disabled(job.cancelling || job.finalizing)
                    .accessibilityHint(job.cancelling ? "Stopping at the next safe point and removing partial outputs." : "")
                    .accessibilityIdentifier("prepare-cancel")
                }
            }
        }
    }
    private func bytes(_ count: UInt64) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: count), countStyle: .file)
    }

    /// A choice from the library as a menu, styled as one of Satchel's rows.
    /// DLC is not chosen by hand: every imported DLC for this game is included.
    private var addOnRow: some View {
        let tint = SatchelTheme.leaf
        let list = addOns
        return HStack(alignment: .top, spacing: 12) {
            Image(systemName: "puzzlepiece.extension.fill").font(.system(size: 17, weight: .bold)).foregroundStyle(tint)
                .frame(width: 40, height: 40).background(tint.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            VStack(alignment: .leading, spacing: 2) {
                Text("DLC").font(SatchelTheme.label(10)).tracking(1.2).foregroundStyle(SatchelTheme.textDim)
                if list.isEmpty {
                    Text(baseID.isEmpty ? "Choose a base game first" : "None imported")
                        .font(.system(size: 15, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                } else {
                    Text(list.count == 1 ? "1 DLC included" : "\(list.count) DLC included")
                        .font(.system(size: 15, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                    ForEach(list) { row in
                        Text("\(row.name) · \(satchelBytes(Int64(clamping: row.bytes)))")
                            .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                            .lineLimit(1).minimumScaleFactor(0.8)
                    }
                }
                Text("Import DLC files with + on the home screen, like a game. Every imported DLC for this game is included when it prepares.")
                    .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(10)
        .background(SatchelTheme.surfaceHigh, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("prepare-dlc")
    }

    private func picker(_ title: String, detail: String, systemImage: String, tint: Color, items: [HomebrewItem],
                        none: String, selection: Binding<String>, identifier: String) -> some View {
        let chosen = items.first { $0.id == selection.wrappedValue }
        return Menu {
            Picker(title, selection: selection) {
                Text(none).tag("")
                ForEach(items) { Text($0.name).tag($0.id) }
            }
        } label: {
            HStack(spacing: 12) {
                Image(systemName: systemImage).font(.system(size: 17, weight: .bold)).foregroundStyle(tint)
                    .frame(width: 40, height: 40).background(tint.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(title.uppercased()).font(SatchelTheme.label(10)).tracking(1.2).foregroundStyle(SatchelTheme.textDim)
                        .lineLimit(1).minimumScaleFactor(0.8)
                    Text(chosen?.name ?? none).font(.system(size: 15, weight: .bold, design: .rounded))
                        .foregroundStyle(chosen == nil ? SatchelTheme.textDim : SatchelTheme.text)
                        .lineLimit(2).minimumScaleFactor(0.8)
                }
                .multilineTextAlignment(.leading)
                .frame(maxWidth: .infinity, alignment: .leading)
                Spacer(minLength: 6)
                Image(systemName: "chevron.up.chevron.down").font(.system(size: 12, weight: .bold)).foregroundStyle(SatchelTheme.textDim)
            }
            .padding(10)
            .background(SatchelTheme.surfaceHigh, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        }
        .accessibilityLabel(title)
        .accessibilityValue(chosen?.name ?? none)
        .accessibilityHint(detail)
        .accessibilityIdentifier(identifier)
        .satchelDimmedWhenDisabled()
    }
}

private func prepareDuration(_ seconds: Double) -> String {
    let value = max(0, Int(seconds))
    return value < 60 ? "\(value)s" : "\(value / 60)m \(value % 60)s"
}

/// The home screen's view of the Prepare job: nothing when there is no job, live progress
/// while one runs, then its result until dismissed. It observes the job itself so progress
/// updates redraw only this card, not the shelf.
struct PrepareJobSlot: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    /// Reopens the Prepare screen.
    let open: () -> Void
    /// Opens a finished package (its folder name) in the prepared games list.
    let play: (String) -> Void
    /// Opens the game page (or play screen) of the package a warm-up is for.
    let openWarmup: (String) -> Void
    @ObservedObject private var job = PrepareJob.shared
    @ObservedObject private var warmup = WarmupJob.shared

    var body: some View {
        // A finished warm-up leaves the header: only running, paused or failed work stays, and
        // only while its package is still there (a newer Prepare replaces the old one).
        let package = warmup.journal.flatMap { journal in artwork.prepared.first { $0.folder == journal.package } }
        if let journal = warmup.journal, warmup.running || (!job.running && package != nil && ["paused", "failed"].contains(journal.progress.state)) {
            // A package an older engine made, or one that failed its check, is never played from here.
            WarmupHomeCard(open: openWarmup, play: play, playable: package?.current == true && journal.packageFailed != true)
        } else if job.running || job.outcome != nil {
            PrepareJobCard(job: job, art: library.items.first { $0.id == job.baseID }.flatMap(artwork.image),
                           retry: { job.retry(library: library, artwork: artwork, title: artwork.title) }, open: open, play: play)
                .transition(.move(edge: .top).combined(with: .opacity))
        }
    }
}

/// A compact card in Satchel's style, sized for the home screen's header row (52 points).
private struct PrepareJobCard: View {
    @ObservedObject var job: PrepareJob
    let art: UIImage?
    let retry: () -> Void
    let open: () -> Void
    let play: (String) -> Void

    private var tint: Color {
        switch job.running ? nil : job.outcome?.kind {
        case nil: return SatchelTheme.tide
        case .signed: return SatchelTheme.leaf
        case .needsSignature: return SatchelTheme.spark
        case .failed, .cancelled: return SatchelTheme.ember
        }
    }

    var body: some View {
        HStack(spacing: 10) {
            Button { Haptics.click(); open() } label: {
                HStack(spacing: 10) {
                    icon
                    if job.running { progress } else if let outcome = job.outcome { result(outcome) }
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
            .accessibilityHint("Opens the Prepare screen")
            .accessibilityIdentifier("prepare-job-card")
            if !job.running, let outcome = job.outcome { actions(outcome) }
        }
        .padding(.leading, 7).padding(.trailing, 10)
        .frame(height: 52)
        .frame(minWidth: 230, maxWidth: 400, alignment: .leading)
        .background(SatchelTheme.surface.opacity(0.94), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay { RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(tint.opacity(0.45)) }
    }

    private var icon: some View {
        ZStack(alignment: .bottomTrailing) {
            Group {
                if let art { Image(uiImage: art).resizable().scaledToFill() }
                else { Image(systemName: "hammer.fill").font(.system(size: 15, weight: .bold)).foregroundStyle(tint)
                        .frame(maxWidth: .infinity, maxHeight: .infinity).background(tint.opacity(0.16)) }
            }
            .frame(width: 38, height: 38)
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            if !job.running {
                Image(systemName: badge).font(.system(size: 11, weight: .black)).foregroundStyle(SatchelTheme.shell)
                    .frame(width: 18, height: 18).background(tint, in: Circle())
                    .overlay { Circle().strokeBorder(SatchelTheme.surface, lineWidth: 2) }
                    .offset(x: 5, y: 5)
            }
        }
        .accessibilityHidden(true)
    }

    private var badge: String {
        switch job.outcome?.kind {
        case .signed: return "checkmark"
        case .needsSignature: return "signature"
        case .cancelled: return "xmark"
        default: return "exclamationmark"
        }
    }

    private var progress: some View {
        TimelineView(.periodic(from: .now, by: 1)) { context in
            let elapsed = context.date.timeIntervalSince(job.startedAt ?? context.date)
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 6) {
                    Text(job.title.isEmpty ? "Preparing" : job.title).font(.system(size: 13, weight: .bold, design: .rounded))
                        .foregroundStyle(SatchelTheme.text).lineLimit(1)
                    Spacer(minLength: 4)
                    Text("\(Int((job.fraction ?? 0) * 100))%").font(SatchelTheme.mono(12)).monospacedDigit()
                        .foregroundStyle(SatchelTheme.text)
                }
                HStack(spacing: 6) {
                    Text(job.cancelling ? "Cancelling…" : job.progressSnapshot?.step.title ?? "Waiting for iOS to start")
                        .lineLimit(1)
                    Spacer(minLength: 4)
                    Group {
                        if let remaining = job.progressSnapshot?.estimatedRemainingSeconds {
                            Text("\(prepareDuration(elapsed)) · \(prepareDuration(remaining)) left")
                        } else {
                            Text(prepareDuration(elapsed))
                        }
                    }
                    .monospacedDigit().lineLimit(1).fixedSize()
                }
                .font(.system(size: 11, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                SatchelProgressBar(value: job.fraction ?? 0, tint: SatchelTheme.tide, height: 4)
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Preparing \(job.title), \(Int((job.fraction ?? 0) * 100)) percent, \(job.progressSnapshot?.step.title ?? "starting")")
        }
    }

    private func result(_ outcome: PrepareJob.Outcome) -> some View {
        let heading: String
        // The package folder leads where there is one, so similar packages stay distinct.
        let package: String?
        let detail: String
        switch outcome.kind {
        case .signed: heading = "Ready to play"; package = outcome.package; detail = outcome.title
        case .needsSignature: heading = "Needs signature"; package = outcome.package; detail = "sign it on the Mac before playing"
        case .cancelled: heading = "Prepare cancelled"; package = nil; detail = outcome.title
        case .failed: heading = "Prepare failed"; package = nil; detail = outcome.message
        }
        return VStack(alignment: .leading, spacing: 2) {
            Text(heading).font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                .lineLimit(1).minimumScaleFactor(0.85)
            HStack(spacing: 4) {
                if let package {
                    Text(package).font(SatchelTheme.mono(11)).lineLimit(1).truncationMode(.middle).layoutPriority(1)
                    Text("·")
                }
                // A failure's reason gets two lines; the Prepare screen shows it in full.
                Text(detail).lineLimit(outcome.kind == .failed ? 2 : 1).truncationMode(.tail)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .font(.system(size: outcome.kind == .failed ? 10 : 11, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }

    @ViewBuilder private func actions(_ outcome: PrepareJob.Outcome) -> some View {
        switch outcome.kind {
        case .signed:
            Button { Haptics.click(); play(outcome.package) } label: {
                SatchelButtonLabel(title: "Play", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 32)
            }
            .buttonStyle(PressableStyle())
            .accessibilityIdentifier("prepare-job-play")
        case .failed, .cancelled:
            Button { Haptics.click(); retry() } label: {
                SatchelButtonLabel(title: "Retry", systemImage: "arrow.clockwise", kind: .secondary, height: 32)
            }
            .buttonStyle(PressableStyle())
            .accessibilityIdentifier("prepare-job-retry")
        case .needsSignature:
            EmptyView()
        }
        Button { withAnimation(SatchelTheme.glide) { job.dismissOutcome() } } label: {
            Image(systemName: "xmark").font(.system(size: 12, weight: .bold)).foregroundStyle(SatchelTheme.textDim)
                .frame(width: 30, height: 30).background(Color.white.opacity(0.07), in: Circle())
        }
        .buttonStyle(PressableStyle(scale: 0.9))
        .accessibilityLabel("Dismiss")
        .accessibilityIdentifier("prepare-job-dismiss")
    }
}
