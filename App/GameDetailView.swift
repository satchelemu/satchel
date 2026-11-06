import SwiftUI

/// Full-screen game page: artwork, the path from import to play, and details.
struct GameDetailView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    let item: HomebrewItem
    var updates: [HomebrewItem] = []
    var onPlay: (() -> Void)?
    var onPrepare: (() -> Void)?
    var onOpenSettings: (() -> Void)?
#if !SATCHEL_UI_PREVIEW_MINIMAL
    @ObservedObject private var addOnJob = AddOnJob.shared
#endif
    @State private var playing = false
    @State private var showDiagnostics = false
    @State private var removeTarget: HomebrewItem?
    @Environment(\.dismiss) private var dismiss
    private var currentItem: HomebrewItem { library.items.first { $0.id == item.id } ?? item }
    private var originalRemoved: Bool { library.originalRemoved(currentItem) }
    private var run: HomebrewRun? { library.lastRun?.sha256 == item.id ? library.lastRun : nil }
    private var info: GameInfo? { artwork.info(currentItem) }
    private var state: ShelfState { artwork.state(currentItem) }
    private var title: String { artwork.title(item) }

    var body: some View {
        PanelChrome(identifier: "close-game-detail", close: { library.stop(); dismiss() }) {
            ScreenReader { cut, geometry in
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: artwork and Play on the first page, details on the second.
                        let page = fold.pages(in: geometry.size).first
                        FoldSplit(fold: fold, cut: cut) {
                            artColumn(side: max(120, min(page.width - cut.leading - 90, page.height - 150, 300)))
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                        } second: {
                            infoScroll(reserve: fold.isVertical ? reserve : 0)
                        }
                        .padding(.top, 10)
                    } else {
                        let artSide = max(120, min(geometry.size.height - 110, geometry.size.width * 0.30,
                                                   geometry.size.height > 560 ? 320 : 250))
                        HStack(alignment: .top, spacing: 28) {
                            artColumn(side: artSide).padding(.top, 8)
                            infoScroll(reserve: reserve)
                        }
                        .cutoutPadding(cut, leading: 24, trailing: 20).padding(.top, 10)
                    }
                }
            }
            .background { SatchelBackdrop(glow: artwork.glow(item), image: artwork.image(item)) }
            .tint(SatchelTheme.tide)
            .satchelMenuStatus()
            .fullScreenCover(isPresented: $playing) {
                HomebrewPlayerCover(url: library.localFile(item), name: item.name, sha256: item.id)
            }
            .confirmationDialog("Remove from this iPhone?", isPresented: Binding(get: { removeTarget != nil }, set: { if !$0 { removeTarget = nil } }),
                                titleVisibility: .visible, presenting: removeTarget) { target in
                Button("Remove \(artwork.title(target))", role: .destructive) {
                    if target.id == item.id { removeGame(); dismiss() }
                    else { artwork.forget(target); library.remove(target) }
                }
                Button("Keep", role: .cancel) {}
            } message: { target in
                if target.id == item.id {
                    Text("This removes the game, its updates and DLC, and its prepared copy from this iPhone. Saves are kept. The originals in Files or iCloud Drive are not touched, so you can import them again later.")
                } else {
                    Text("This deletes \(AppBrand.name)'s local copy. The original file in Files or iCloud Drive is not touched, so you can import it again later.")
                }
            }
        }
        .controllerNavigation(enabled: !playing && removeTarget == nil, back: { library.stop(); dismiss() })
    }

    /// Removes all of the game from this iPhone: its imported files, its updates and DLC, and
    /// every package prepared from it, so no tile is left behind. Saves stay in NativeRuntime.
    private func removeGame() {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        guard !AddOnJob.shared.running else { library.error = "Wait for Add DLC to finish before removing this game."; return }
#endif
        let game = currentItem
        let id = info?.applicationID ?? game.preparedSource?.titleID
        var targets = [game] + updates
        if let id {
            targets += library.items.filter { other in
                artwork.info(other)?.kind == .addOnContent && (artwork.info(other)?.addOns ?? []).contains { $0.applicationID == id }
            }
        }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        let packages = id.map { id in artwork.prepared.filter { $0.applicationID == id }.map(\.folder) } ?? []
#else
        let packages: [String] = []
#endif
        let library = library, artwork = artwork
        @MainActor func forgetFiles() { for target in targets { artwork.forget(target); library.remove(target) } }
        guard !packages.isEmpty else { forgetFiles(); return }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        // Packages go first: if one cannot be deleted (a game is still open), the game stays whole.
        Task {
            let failure = await Task.detached(priority: .userInitiated) { () -> String? in
                for name in packages {
                    do { try PreparedGameCleanup.delete(named: name) } catch { return error.localizedDescription }
                }
                return nil
            }.value
            artwork.refreshPrepared()
            if let failure { library.error = failure } else { forgetFiles() }
        }
#endif
    }

    private func artColumn(side: CGFloat) -> some View {
        VStack(spacing: 18) {
            GameTileArt(item: item, title: title, image: artwork.image(item), side: side)
                .clipShape(RoundedRectangle(cornerRadius: side * 0.12, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: side * 0.12, style: .continuous).strokeBorder(.white.opacity(0.12)) }
                .shadow(color: artwork.glow(item).opacity(0.55), radius: 28, y: 12)
            primaryAction.frame(width: side)
        }
    }

    private func infoScroll(reserve: CGFloat) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                header.padding(.trailing, reserve)
                if item.package == nil {
                    homebrewActions
                    if let run { runCards(run) }
                    compatibility
                } else {
                    journey
                    shadersPanel
                    if info?.kind == .update || info?.kind == .addOnContent { updateTarget }
                    if !updates.isEmpty || !extras.isEmpty { updatesPanel }
                    addOnPanel
                    savePanel
                    playerSettingsPanel
                    detailsPanel
                    if !originalRemoved { diagnostics }
                }
                removeButton
            }
            .padding(.bottom, 28).padding(.top, 8)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
        .accessibilityIdentifier("detail-scroll")
    }

    private var importedAddOns: [SwitchAddOnInfo] {
        guard let id = info?.applicationID ?? artwork.newestPackage(for: item)?.applicationID else { return [] }
        return GameInfo.addOns(for: id, in: library.items.filter(library.hasOriginal).compactMap { artwork.info($0) })
    }

    /// Shader warm-up for the package Play starts, so it stays reachable after the first play.
    @ViewBuilder private var shadersPanel: some View {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if info?.kind != .update, info?.kind != .addOnContent, let game = artwork.preparedPackage(for: currentItem) {
            WarmupPanel(package: game.folder, title: title)
        }
#endif
    }

    @ViewBuilder private var savePanel: some View {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if info?.kind != .update, info?.kind != .addOnContent,
           let id = info?.applicationID ?? artwork.newestPackage(for: item)?.applicationID {
            GameSavePanel(titleID: String(format: "%016llX", id), gameName: title)
        }
#endif
    }

    @ViewBuilder private var playerSettingsPanel: some View {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if info?.kind != .update, info?.kind != .addOnContent,
           let id = info?.applicationID ?? artwork.newestPackage(for: item)?.applicationID {
            GamePlayerSettingsPanel(titleID: String(format: "%016llX", id))
        }
#endif
    }

    @ViewBuilder private var addOnPanel: some View {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        let game = artwork.preparedPackage(for: item)
        let root = game.map { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PreparedGames/" + $0.folder) }
        let included = root.map { PreparedAddOn.read(from: $0) } ?? []
        let pending = importedAddOns.filter { imported in
            !included.contains { $0.titleID == String(format: "%016llX", imported.titleID) && $0.version >= imported.version }
        }
        if !importedAddOns.isEmpty || !included.isEmpty || addOnJob.package == game?.folder {
            SatchelPanel(title: "Add-on content") {
                ForEach(included, id: \.titleID) { dlc in
                    Label("DLC \(dlc.titleID) · version \(dlc.version) · Included", systemImage: "checkmark.circle.fill")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .accessibilityIdentifier("included-dlc-" + dlc.titleID)
                }
                ForEach(pending) { dlc in
                    Text("\(dlc.name) · version \(dlc.version) · Imported")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                }
                if let game, !pending.isEmpty {
                    Button {
                        let sources = library.items.filter { source in
                            library.hasOriginal(source) && (artwork.info(source)?.addOns ?? []).contains { candidate in
                                candidate.applicationID == game.applicationID && pending.contains(where: { $0.titleID == candidate.titleID })
                            }
                        }.map(library.localFile)
                        addOnJob.start(package: game.folder, sources: sources, artwork: artwork)
                    } label: {
                        SatchelButtonLabel(title: "Add DLC", systemImage: "plus.circle.fill", kind: .secondary, height: 38)
                    }
                    .buttonStyle(PressableStyle()).disabled(addOnJob.running)
                    .accessibilityIdentifier("add-dlc")
                    Text("Adds imported DLC without preparing the game again. Quit the game first. Keep Satchel open until it finishes.")
                        .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
                } else if game == nil, !pending.isEmpty {
                    Text("Prepare the base game to include this DLC.")
                        .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
                }
                if addOnJob.package == game?.folder {
                    Text(addOnJob.message).font(.system(size: 12)).accessibilityIdentifier("add-dlc-status")
                    if addOnJob.running {
                        ProgressView()
                        Button("Cancel Add DLC") { addOnJob.cancel() }
                            .disabled(addOnJob.cancelling).accessibilityIdentifier("cancel-add-dlc")
                    }
                }
            }
        }
#else
        if !importedAddOns.isEmpty {
            SatchelPanel(title: "Add-on content") {
                ForEach(importedAddOns) { dlc in Text(dlc.name) }
            }
        }
#endif
    }

    // MARK: Header

    private var header: some View {
        VStack(alignment: .leading, spacing: 8) {
            StateBadge(state: state)
            Text(title).font(SatchelTheme.display(30)).foregroundStyle(SatchelTheme.text)
                .fixedSize(horizontal: false, vertical: true).textSelection(.enabled)
                .accessibilityIdentifier("detail-game-title")
            Text(artwork.subtitle(item, updates: updates)).font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundStyle(SatchelTheme.textDim)
        }
    }

    // MARK: Primary action

    @ViewBuilder private var primaryAction: some View {
        if item.package == nil {
            if library.runningID != nil {
                Button(role: .destructive) { library.stop() } label: {
                    SatchelButtonLabel(title: "Stop", systemImage: "stop.fill", kind: .secondary, height: 52, fill: true)
                }.buttonStyle(PressableStyle())
            } else {
                Button { Haptics.click(); playing = true } label: {
                    SatchelButtonLabel(title: "Play", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 52, fill: true)
                }
                .buttonStyle(PressableStyle())
                .accessibilityLabel("Play homebrew")
                .accessibilityIdentifier("play-homebrew")
            }
        } else if state == .ready, let onPlay {
            Button { Haptics.click(); onPlay() } label: {
                SatchelButtonLabel(title: resumes ? "Resume" : "Play", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 52, fill: true)
            }.controllerAction("detail-play") { onPlay() }.buttonStyle(PressableStyle()).accessibilityIdentifier("play-game")
        } else if state == .needsKeys, let onOpenSettings {
            Button { onOpenSettings() } label: {
                SatchelButtonLabel(title: "Add keys", systemImage: "key.fill", kind: .accent(SatchelTheme.spark), height: 52, fill: true)
            }.controllerAction("detail-keys") { onOpenSettings() }.buttonStyle(PressableStyle()).accessibilityIdentifier("detail-add-keys")
        } else if !originalRemoved, info?.kind == .application, let onPrepare {
            Button { onPrepare() } label: {
                SatchelButtonLabel(title: stale ? "Prepare again" : "Prepare", systemImage: "hammer.fill",
                                  kind: .accent(SatchelTheme.tide), height: 52, fill: true)
            }.controllerAction("detail-prepare") { onPrepare() }.buttonStyle(PressableStyle()).accessibilityIdentifier("prepare-game")
        } else {
            SatchelButtonLabel(title: info?.kind == .update ? "Update stored" : "Can't play yet",
                              systemImage: info?.kind == .update ? "checkmark" : "lock.fill", kind: .quiet, height: 52, fill: true)
                .opacity(0.8)
                .accessibilityElement(children: .combine)
        }
    }

    /// Play returns to this game while it is paused in the background.
    private var resumes: Bool {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        guard let current = PreparedLaunch.current else { return false }
        return artwork.preparedPackage(for: currentItem)?.folder == current
#else
        return false
#endif
    }

    // MARK: Journey

    private enum StepState { case done, active, waiting, attention }

    private var journey: some View {
        SatchelPanel(title: "Path to play") {
            HStack(spacing: 0) {
                step("Imported", "tray.and.arrow.down.fill", .done)
                connector(infoStep == .done)
                step("Game info", "photo.fill", infoStep)
                connector(prepared)
                step("Prepared", "hammer.fill", prepared ? .done : stale ? .attention : (info?.kind == .application ? .active : .waiting))
                connector(prepared)
                step("Play", "play.fill", prepared ? .active : .waiting)
            }
            nextStep
        }
    }

    private var prepared: Bool { artwork.isPrepared(item) }
    /// Its only packages were made by an older engine: launch refuses them.
    private var stale: Bool { artwork.needsPrepareAgain(currentItem) }

    private var infoStep: StepState {
        if info != nil { return .done }
        switch artwork.status[item.id] {
        case .reading: return .active
        case .needsKeys, .failed: return .attention
        default: return artwork.keysAvailable ? .active : .attention
        }
    }

    private func step(_ title: String, _ icon: String, _ state: StepState) -> some View {
        let tint: Color = switch state {
        case .done: SatchelTheme.leaf
        case .active: SatchelTheme.tide
        case .attention: SatchelTheme.spark
        case .waiting: SatchelTheme.textDim.opacity(0.6)
        }
        return VStack(spacing: 6) {
            Image(systemName: state == .done ? "checkmark" : (state == .attention ? "exclamationmark" : icon))
                .font(.system(size: 14, weight: .heavy))
                .foregroundStyle(state == .waiting ? tint : SatchelTheme.shell)
                .frame(width: 34, height: 34)
                .background(state == .waiting ? AnyShapeStyle(Color.white.opacity(0.06)) : AnyShapeStyle(tint), in: Circle())
                .overlay { if state == .active { Circle().strokeBorder(tint.opacity(0.4), lineWidth: 5).padding(-5) } }
            Text(title).font(.system(size: 11, weight: .bold, design: .rounded))
                .foregroundStyle(state == .waiting ? SatchelTheme.textDim : SatchelTheme.text)
        }
        .frame(minWidth: 64)
        .accessibilityElement(children: .combine)
    }

    private func connector(_ lit: Bool) -> some View {
        Capsule().fill(lit ? SatchelTheme.leaf : Color.white.opacity(0.08)).frame(height: 4)
            .frame(maxWidth: .infinity).padding(.bottom, 20)
    }

    @ViewBuilder private var nextStep: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(nextText).font(.system(size: 14, weight: .medium, design: .rounded))
                .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("game-original-status")
            if !originalRemoved, case .failed = artwork.status[item.id] {
                Button { artwork.retry(item, url: library.localFile(item)) } label: {
                    SatchelButtonLabel(title: "Try again", systemImage: "arrow.clockwise", kind: .secondary, height: 38)
                }.buttonStyle(PressableStyle())
            }
            // A ready package can still be the wrong one (for example prepared without its update),
            // so a ready game keeps a quiet way back to Prepare. Play stays the primary action.
            if state == .ready, info?.kind == .application, !originalRemoved, let onPrepare {
                Button { onPrepare() } label: {
                    SatchelButtonLabel(title: "Prepare again", systemImage: "hammer", kind: .secondary, height: 38)
                }.controllerAction("detail-prepare-again") { onPrepare() }.buttonStyle(PressableStyle())
                .accessibilityIdentifier("detail-prepare-again")
            }
        }
        .padding(.top, 4)
    }

    private var nextText: String {
        if originalRemoved {
            if stale {
                return "\(AppBrand.name)'s engine changed, so this game has to be prepared again. Its original file was removed from this iPhone after the last Prepare: import the game again from Files or iCloud Drive, then press Prepare. Your saves are kept."
            }
            return "The original file was removed from this iPhone after the prepared package passed verification. To prepare this game again, import it again from Files or iCloud Drive first."
        }
        if case .failed(let reason) = artwork.status[item.id] { return "Couldn't read this game's info. " + reason }
        if info == nil {
            return artwork.keysAvailable
                ? "Reading the title and icon from the game itself. This takes a few seconds."
                : "Next: import your prod.keys in Settings. \(AppBrand.name) needs them to read the game's title, icon and code."
        }
        if info?.kind == .addOnContent { return "Open the matching game page and choose Add DLC if it is already prepared, or Prepare to include it for the first time." }
        if prepared { return "Everything is ready. Press Play to start the game." }
        if stale {
            return "\(AppBrand.name)'s engine changed since this game was prepared, so its prepared copy no longer starts. Press Prepare again to build a new one. Your saves are kept."
        }
        if NativePlay.available {
            return "Next: prepare the game on this iPhone. \(AppBrand.name) rewrites the game's code for iPhone, signs it and builds its shaders. It keeps going in the background if you switch apps."
        }
        return "Stored safely on this iPhone. Preparing games on the phone is still being built; once it lands, this game will be ready to prepare with one tap."
    }

    // MARK: Play mode

    // MARK: Updates

    private var updateTarget: some View {
        let base = library.items.first { other in
            artwork.info(other)?.kind == .application && artwork.info(other)?.applicationID == info?.applicationID
        }
        return SatchelPanel(title: info?.kind == .addOnContent ? "DLC" : "Update", tint: SatchelTheme.violet) {
            Text(base.map { "Applies to \(artwork.title($0)) when the game is prepared." }
                 ?? "Import the matching base game and this content will attach to it automatically.")
                .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// Other applications from the same cartridge, listed with the game rather than as tiles.
    private var extras: [GameInfo.Extra] { info?.extras ?? [] }

    private var updatesPanel: some View {
        SatchelPanel(title: "Loaded with this game", tint: SatchelTheme.violet) {
            ForEach(extras, id: \.titleID) { extra in
                HStack(spacing: 12) {
                    Image(systemName: "sparkles.rectangle.stack.fill").font(.system(size: 22)).foregroundStyle(SatchelTheme.violet)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(extra.title).font(.system(size: 15, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                            .lineLimit(2)
                        Text("Bonus app on the same cartridge  ·  not playable in \(AppBrand.name) yet")
                            .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim).lineLimit(2)
                    }
                    Spacer()
                }
            }
            ForEach(updates) { update in
                HStack(spacing: 12) {
                    Image(systemName: "arrow.up.circle.fill").font(.system(size: 24)).foregroundStyle(SatchelTheme.violet)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Version " + (artwork.info(update)?.displayVersion ?? "?"))
                            .font(.system(size: 15, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                        Text((library.originalRemoved(update) ? "Original removed" : satchelBytes(Int64(update.bytes))) + "  ·  " + update.name)
                            .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim).lineLimit(1)
                    }
                    Spacer()
                    Menu {
                        Button("Remove update", systemImage: "trash", role: .destructive) { removeTarget = update }
                    } label: {
                        Image(systemName: "ellipsis").font(.system(size: 15, weight: .bold)).frame(width: 44, height: 44)
                    }
                    .accessibilityLabel("Update options")
                }
            }
        }
    }

    // MARK: Details

    private var detailsPanel: some View {
        SatchelPanel(title: "Details") {
            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 9) {
                if let info {
                    row("Title ID", info.applicationID.titleIDString, mono: true)
                    row("Version", (info.displayVersion.isEmpty ? "" : info.displayVersion + "  ") + "(\(info.version))")
                    if info.bundlesUpdate { row("Contents", "Base game with a bundled update") }
                }
                if let package = item.package {
                    row("Package", "\(package.format.rawValue.uppercased()) with \(package.entryCount) \(package.entryCount == 1 ? "entry" : "entries")")
                }
                row("Original on iPhone", originalRemoved ? "Removed after verification" : satchelBytes(Int64(item.bytes)))
                row("Added", item.importedAt.formatted(date: .abbreviated, time: .shortened))
                row("File", item.name)
                row("Library ID", String(item.id.prefix(16)), mono: true)
            }
            Text(originalRemoved ? "Your game, artwork and saves are kept. The source in Files or iCloud Drive is unchanged." : "The source in Files or iCloud Drive is unchanged. The imported copy can be removed automatically after a verified Prepare.")
                .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
        }
    }

    private func row(_ label: String, _ value: String, mono: Bool = false) -> some View {
        GridRow {
            Text(label).font(.system(size: 12, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            Text(value).font(mono ? SatchelTheme.mono(13) : .system(size: 13, weight: .semibold, design: .rounded))
                .foregroundStyle(SatchelTheme.text).textSelection(.enabled).lineLimit(2)
        }
    }

    private var diagnostics: some View {
        VStack(alignment: .leading, spacing: 14) {
            Button { withAnimation(SatchelTheme.glide) { showDiagnostics.toggle() } } label: {
                HStack {
                    Image(systemName: "stethoscope").foregroundStyle(SatchelTheme.tide)
                    Text("Developer diagnostics").font(.system(size: 15, weight: .bold, design: .rounded))
                    Spacer()
                    Image(systemName: "chevron.down").rotationEffect(.degrees(showDiagnostics ? 180 : 0))
                }
                .foregroundStyle(SatchelTheme.text)
                .padding(.horizontal, 18).frame(minHeight: 52)
                .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(SatchelTheme.line) }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressableStyle(scale: 0.98))
            .accessibilityIdentifier("show-diagnostics")
            if showDiagnostics {
                PackageDiagnostics(library: library, item: item).transition(.opacity.combined(with: .move(edge: .top)))
            }
        }
    }

    /// Why Remove cannot be used right now, shown under it before any tap.
    private var removeBlocked: String? {
        if library.importing { return "Remove is available once the import finishes." }
        if library.runningID != nil { return "Remove is available once the game stops." }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        // The engine runs one game per app launch, so its prepared copy stays in use until then.
        if PrepareJob.gameSessionOpen, artwork.newestPackage(for: currentItem) != nil {
            return "A game was played since \(AppBrand.name) opened. Close and reopen the app, then remove this game."
        }
#endif
        return nil
    }

    @ViewBuilder private var removeButton: some View {
        Button { removeTarget = item } label: {
            Label("Remove from this iPhone", systemImage: "trash")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundStyle(SatchelTheme.ember)
                .frame(minHeight: 44)
        }
        .buttonStyle(PressableStyle(scale: 0.97))
        .disabled(removeBlocked != nil)
        .accessibilityIdentifier("detail-remove-import")
        .padding(.top, 4)
        if let removeBlocked {
            SatchelDisabledReason(text: removeBlocked).accessibilityIdentifier("detail-remove-reason")
        }
    }

    // MARK: Homebrew

    private var homebrewActions: some View {
        SatchelCard(title: "Ready to try") {
            Label("NRO structure validated", systemImage: "checkmark.seal.fill")
                .font(.system(size: 15, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.leaf)
            Text("Your file is stored locally. Press Play to run it with the built-in interpreter, or run a quick diagnostic.")
                .font(.subheadline).foregroundStyle(SatchelTheme.textDim)
                .lineSpacing(4).fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("detail-introduction")
            if library.runningID != nil {
                ProgressView("Interpreting homebrew…").tint(SatchelTheme.tide)
            } else {
                Button { library.run(item) } label: {
                    SatchelButtonLabel(title: "Run diagnostic", systemImage: "stethoscope", kind: .secondary, height: 44)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("run-homebrew")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("detail-actions-card")
    }

    private var compatibility: some View {
        SatchelCard(title: "Compatibility") {
            Text("Play uses experimental software-framebuffer and controller emulation. Compatibility varies by file; GPU rendering and audio are not implemented.")
                .font(.subheadline).foregroundStyle(SatchelTheme.textDim)
                .lineSpacing(4).fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("compatibility-copy")
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("compatibility-card")
    }

    @ViewBuilder private func runCards(_ run: HomebrewRun) -> some View {
        SatchelCard(title: "Last diagnostic") {
            Text(run.outcome).font(.headline)
                .foregroundStyle(run.exitCode == nil ? SatchelTheme.ember : SatchelTheme.leaf)
                .fixedSize(horizontal: false, vertical: true).textSelection(.enabled)
                .accessibilityIdentifier("run-outcome")
            Text("\(run.instructions.formatted()) instructions • \(String(format: "%.3f", run.seconds)) seconds")
                .font(.caption).foregroundStyle(SatchelTheme.textDim)
            if run.exitCode == nil {
                Text("The file imported correctly, but execution could not complete with the current interpreter.")
                    .font(.subheadline).foregroundStyle(SatchelTheme.textDim)
                    .lineSpacing(4).fixedSize(horizontal: false, vertical: true)
            }
        }
        SatchelCard(title: "Horizon debug output") {
            Text(run.output.isEmpty ? "No debug output was produced." : run.output)
                .font(.system(.footnote, design: .monospaced)).lineSpacing(4)
                .fixedSize(horizontal: false, vertical: true).textSelection(.enabled)
                .accessibilityIdentifier("homebrew-output")
            if let url = library.runReportURL {
                ShareLink(item: url) { Label("Share run report", systemImage: "square.and.arrow.up").frame(minHeight: 44) }
            }
        }
    }
}
