import SwiftUI
import UniformTypeIdentifiers

private struct NRODocument: FileDocument {
    static let type = UTType(importedAs: "com.dell.satchel.nro", conformingTo: .data)
    static var readableContentTypes: [UTType] { [type] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

/// What the single full-screen cover shows. Changing the route while the cover is up
/// swaps its content in place, so no presentation is chained across a dismissal.
enum HomeRoute: Equatable {
    case detail(String)
    case importer
    case settings
    case demo
#if !SATCHEL_UI_PREVIEW_MINIMAL
    /// A prepared package (its folder name): its play screen, or the running game.
    case preparedPackage(String)
    case prepare
    case guidedPrepare(String)
#endif
}

private struct HomePresentation: Identifiable {
    let id = UUID()
    var route: HomeRoute
}

/// Home: a horizontal shelf of large game tiles over a glow from the focused game.
struct HomebrewView: View {
    @StateObject private var library = HomebrewLibrary()
    @StateObject private var artwork = GameArtworkStore()
    @AppStorage("SatchelDeveloperControls") private var developerControls = false
    @State private var signatureWarnings: [SignatureExpiry.Warning] = []
    @State private var signatureCheckDate = Date()
    @State private var reportPackage: String?
    @State private var presentation: HomePresentation?
    /// Bumped to rebuild the cover's presenter after its state fell out of step with UIKit.
    @State private var presenterEpoch = 0
    /// The system would draw a normal top bar here (no vertical bar); see `homeToolbar`.
    @State private var barOnTop = false
    @State private var exporting = false
    @State private var exportDocument: NRODocument?
    @State private var query = ""
    @State private var searching = false
    @State private var newestFirst = true
    @State private var focusedID: String?
#if !SATCHEL_UI_PREVIEW_MINIMAL
    /// A prepared package waiting for the Remove confirmation.
    @State private var pendingRemoval: PreparedGame?
    /// The package of a game paused in the background (`PreparedLaunch.current`), which
    /// is not observable, so it is read again whenever the home screen comes back.
    @State private var pausedPackage: String?
#endif
    @State private var scrolledID: String?
    @FocusState private var searchFocused: Bool
    @Environment(\.scenePhase) private var scenePhase
    private static let addTileID = "add-games-tile"
#if PREVIEW_GAME
    private static var previewImportUsed = false
#endif

    /// Imported games plus prepared games whose file is not in the library.
    private var shelfItems: [HomebrewItem] {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        library.items + artwork.preparedOnlyItems(library.items) { id in
            artwork.titleInfo(id)?.title ?? NativeGameProfile.find(titleID: String(format: "%016llX", id))?.name
        }
#else
        library.items
#endif
    }

    /// Tiles on the shelf: a game with its updates and extras counts once.
    private var shelfCount: Int { artwork.shelf(shelfItems).count }

    private var entries: [ShelfEntry] {
        artwork.shelf(shelfItems)
            .filter { query.isEmpty || artwork.title($0.item).localizedCaseInsensitiveContains(query) || $0.item.name.localizedCaseInsensitiveContains(query) }
            .sorted {
                newestFirst ? $0.item.importedAt > $1.item.importedAt
                    : artwork.title($0.item).localizedStandardCompare(artwork.title($1.item)) == .orderedAscending
            }
    }

    private var focusedEntry: ShelfEntry? {
        let list = entries
        if focusedID == Self.addTileID { return nil }
        return list.first { $0.id == focusedID } ?? list.first
    }

    /// Why library actions are unavailable right now, shown as the disabled item's subtitle.
    private var busyReason: String? {
        if library.runningID != nil { return "Unavailable while a game runs" }
        if library.reclaimingOriginals { return "Removing verified original imports" }
        if library.importing { return "Unavailable until the import finishes" }
        return nil
    }

    /// Prepare also waits for a game that is open, even paused in the background.
    private var prepareBusyReason: String? {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if busyReason == nil, PrepareJob.gameSessionOpen { return PrepareJob.sessionBlocked }
#endif
        return busyReason
    }

    private var workshopMenu: some View {
        Menu {
            let supplements = library.items.filter { $0.package != nil && artwork.info($0)?.kind != .application }
            if !supplements.isEmpty {
                Menu("Imported content") {
                    ForEach(supplements) { item in
                        Button(artwork.title(item)) { open(.detail(item.id)) }
                    }
                }
            }
            if developerControls {
                Button { if let url = library.sampleURL { library.importFile(url) } } label: {
                    Label("Load Horizon sample", systemImage: "sparkles")
                    if let busyReason { Text(busyReason) }
                }.disabled(busyReason != nil)
                .accessibilityIdentifier("workshop-load-sample")
                if let url = library.sampleURL {
                    Button { saveSample(url) } label: { Label("Save sample to Files…", systemImage: "square.and.arrow.up") }
                        .accessibilityIdentifier("workshop-save-sample")
                }
            }
#if !SATCHEL_UI_PREVIEW_MINIMAL
            Button { open(.prepare) } label: {
                Text("Prepare game")
                if let prepareBusyReason { Text(prepareBusyReason) }
            }
            .accessibilityIdentifier("workshop-prepare")
            .disabled(prepareBusyReason != nil)
#endif
            if developerControls {
                Button { open(.demo) } label: { Label("Open interpreter demo", systemImage: "arcade.stick.console") }
                    .accessibilityIdentifier("workshop-demo")
            }
        } label: {
            Label("Workshop", systemImage: "wrench.and.screwdriver.fill")
        }
        .accessibilityIdentifier("workshop")
    }

    var body: some View {
        NavigationStack {
            ScreenReader { cut, geometry in
                // Top bar, shelf chrome and info strip take about 230 points. The open
                // inner screen is taller, so tiles grow there.
                let tile = min(geometry.size.height > 560 ? 300 : 230, max(110, geometry.size.height - 230))
                let inset: CGFloat = 20
                let compact = geometry.size.width - cut.leading - cut.trailing < 760
                Group {
                    if let fold = cut.fold(in: geometry.size), !shelfItems.isEmpty {
                        bookHome(cut: cut, size: geometry.size, fold: fold)
                    } else {
                        VStack(spacing: 0) {
                            topBar(compact: compact, cut: cut).cutoutPadding(cut, leading: inset, trailing: inset).padding(.top, 8)
                            if shelfItems.isEmpty {
                                welcome(compact: compact).cutoutPadding(cut, leading: inset, trailing: inset)
                            } else {
                                Spacer(minLength: 0)
                                shelf(tile: tile, cut: cut, inset: inset, width: geometry.size.width)
                                focusedInfo.cutoutPadding(cut, leading: inset, trailing: inset).padding(.bottom, 8)
                            }
                        }
                    }
                }
                .frame(width: geometry.size.width, height: geometry.size.height)
                .onChange(of: cut.bar, initial: true) { _, bar in barOnTop = bar == .top }
            }
            .safeAreaInset(edge: .bottom, spacing: 4) {
                SignatureExpiryBanner(warnings: signatureWarnings, now: signatureCheckDate)
                    .padding(.horizontal, 32)
            }
            // Search covers the shelf instead of squeezing its header offscreen.
            .ignoresSafeArea(.keyboard)
            .background {
                SatchelBackdrop(glow: focusedEntry.map { artwork.glow($0.item) } ?? SatchelTheme.tide,
                               image: focusedEntry.flatMap { artwork.image($0.item) })
            }
            .toolbar { homeToolbar }
            .toolbar(barOnTop ? .hidden : .automatic, for: .navigationBar)
            .satchelBars()
            .satchelMenuStatus()
        }
        .controllerNavigation(enabled: presentation == nil && !exporting && library.error == nil)
        .background {
            // The one presenter. It sits on its own view so a reset can rebuild it
            // without touching the shelf.
            Color.clear
                .fullScreenCover(item: $presentation, onDismiss: presentationEnded) { shown in routeView(shown.route) }
                .id(presenterEpoch)
        }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        .modifier(NativeGameHooks(open: open))
#endif
        .fileExporter(isPresented: $exporting, document: exportDocument, contentType: NRODocument.type, defaultFilename: "hello-horizon.nro") { result in
            switch result {
            case .success: library.notice = "Sample saved. Import it from Files to try the round trip."
            case .failure(let error): library.error = error.localizedDescription
            }
        }
        .alert("Could not import or run", isPresented: Binding(get: { library.error != nil }, set: { if !$0 { library.error = nil } })) {
            Button("OK") { library.error = nil }
        } message: { Text(library.error ?? "") }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active { library.stop() } else {
                artwork.refreshPrepared()
                signatureCheckDate = Date()
#if !SATCHEL_UI_PREVIEW_MINIMAL
                refreshPaused()
                // A warm-up iOS stopped in the background picks up where it left off.
                WarmupJob.shared.resumeIfInterrupted()
#endif
                // An interruption (a call) during a cover transition can leave SwiftUI and
                // UIKit disagreeing about what is presented. Check once things settle.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { reconcilePresentation(reason: "scene active") }
            }
        }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        .onReceive(NotificationCenter.default.publisher(for: .SatchelNativeSessionDidEnd)) { _ in
            // The play screen clears the package in its own observer; read it after that.
            DispatchQueue.main.async { refreshPaused() }
        }
#endif
        .onAppear {
#if !SATCHEL_UI_PREVIEW_MINIMAL
            refreshPaused()
#endif
            artwork.sync(library.items, fileURL: library.localFile)
#if SATCHEL_UI_PREVIEW_MINIMAL || PREVIEW_GAME
            // Preview-only dates exercise the real home banner without a signing identity.
            if let days = UserDefaults.standard.string(forKey: "SatchelPreviewSignatureDays").flatMap(Double.init) {
                signatureWarnings = SignatureExpiry.warnings(app: signatureCheckDate.addingTimeInterval(days * 86400),
                    games: [], now: signatureCheckDate)
            }
#endif
#if PREVIEW_GAME
            // Preview-only: an import started from home, so the import card can be checked.
            if !Self.previewImportUsed, let mode = UserDefaults.standard.string(forKey: "SatchelPreviewHomeImport") {
                Self.previewImportUsed = true
                do { library.importFiles(try CompressedImportFixture.homeSources(mode)) }
                catch { preconditionFailure("Could not create the synthetic home import: \(error)") }
            }
#endif
            if developerControls { library.removeRequested() }
        }
#if !SATCHEL_UI_PREVIEW_MINIMAL && !PREVIEW_GAME
        .task(id: "\(signatureCheckDate)-" + artwork.prepared.map(\.folder).joined(separator: ",")) {
            let games = artwork.prepared.map { game in
                (folder: game.folder, name: artwork.titleInfo(game.applicationID)?.title ?? game.folder)
            }
            let now = signatureCheckDate
            let result = await Task.detached(priority: .utility) { SignatureExpiry.readWarnings(games: games, now: now) }.value
            if !Task.isCancelled { signatureWarnings = result }
        }
#endif
        .onChange(of: library.items.map { $0.id + ($0.preparedSource?.package ?? "") }) { _, _ in
            artwork.sync(library.items, fileURL: library.localFile)
            artwork.refreshPrepared()
        }
    }

    // MARK: Presentation

    private var route: HomeRoute? { presentation?.route }

    /// Shows a panel. With a panel already up, its content changes in place.
    private func open(_ next: HomeRoute) {
        reconcilePresentation(reason: "open")
        TouchDiagnostics.note("open \(next) (showing \(route.map { "\($0)" } ?? "nothing"))")
        if presentation != nil {
            withAnimation(SatchelTheme.glide) { presentation?.route = next }
        } else {
            presentation = HomePresentation(route: next)
        }
    }

    private func close() { presentation = nil }

    private func presentationEnded() {
        library.stop()
        artwork.refreshPrepared()
#if !SATCHEL_UI_PREVIEW_MINIMAL
        refreshPaused()
#endif
    }

#if !SATCHEL_UI_PREVIEW_MINIMAL
    private func refreshPaused() { pausedPackage = PreparedLaunch.current }

    /// True when Play for this item returns to the game paused in the background.
    private func resumes(_ item: HomebrewItem) -> Bool {
        guard let pausedPackage else { return false }
        return artwork.preparedPackage(for: item)?.folder == pausedPackage
    }
#endif

    /// Brings SwiftUI's idea of the cover back in line with what UIKit shows.
    private func reconcilePresentation(reason: String) {
        let shown = Self.presentedCover()
        if presentation != nil, shown == nil {
            TouchDiagnostics.note("reconcile (\(reason)): route \(route.map { "\($0)" } ?? "") has no cover; resetting")
            var transaction = Transaction(); transaction.disablesAnimations = true
            withTransaction(transaction) { presentation = nil; presenterEpoch += 1 }
        } else if presentation == nil, let shown, !shown.isBeingDismissed {
            TouchDiagnostics.note("reconcile (\(reason)): stray \(type(of: shown)) with no route; dismissing")
            shown.dismiss(animated: false)
        }
    }

    /// The full-screen controller presented over the app's root, if any. Alerts, menus
    /// and file pickers use other presentation styles and are left alone.
    private static func presentedCover() -> UIViewController? {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        for window in windows {
            guard let presented = window.rootViewController?.presentedViewController else { continue }
            if [.fullScreen, .overFullScreen].contains(presented.modalPresentationStyle) { return presented }
        }
        return nil
    }

    /// The cover's content for `route`. It takes the route the cover passes in, which is
    /// current even when this view's own copy of the state is not.
    @ViewBuilder private func routeView(_ route: HomeRoute) -> some View {
        Group {
            switch route {
            case .detail(let id):
                if let item = library.items.first(where: { $0.id == id }) {
                    let entry = artwork.shelf(library.items).first { $0.id == item.id }
                    GameDetailView(library: library, artwork: artwork, item: item, updates: entry?.updates ?? [],
                                   onPlay: playAction(for: item), onPrepare: prepareAction(for: item),
                                   onOpenSettings: { open(.settings) })
                } else {
                    // The game was removed while its page was up.
                    SatchelTheme.shell.ignoresSafeArea().onAppear { close() }
                }
            case .importer:
                ImportFlowView(library: library, artwork: artwork,
                               openItem: { item in open(.detail(item.id)) },
                               openSettings: { open(.settings) })
            case .settings:
                SettingsView(library: library, artwork: artwork)
            case .demo:
                InterpreterDemoView { close() }
#if !SATCHEL_UI_PREVIEW_MINIMAL
            case .preparedPackage(let name):
                PreparedGameView(packageURL: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                    .appendingPathComponent("PreparedGames", isDirectory: true).appendingPathComponent(name, isDirectory: true))
            case .guidedPrepare(let id):
                PrepareGameView(library: library, artwork: artwork, play: { open(.preparedPackage($0)) }, initialBaseID: id)
            case .prepare:
                PrepareGameView(library: library, artwork: artwork, play: { open(.preparedPackage($0)) })
#endif
            }
        }
        .transition(.opacity)
        .id("\(route)")
    }

    /// "Resume" when Play returns to this game paused in the background.
    private func playTitle(_ item: HomebrewItem) -> String {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if resumes(item) { return "Resume" }
#endif
        return "Play"
    }

    /// Play starts the newest package prepared for this game (Play shows only when one exists).
    private func playAction(for item: HomebrewItem) -> (() -> Void)? {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        guard let game = artwork.preparedPackage(for: item) else { return nil }
        return { open(.preparedPackage(game.folder)) }
#else
        return NativePlay.available ? {} : nil
#endif
    }

#if !SATCHEL_UI_PREVIEW_MINIMAL
    /// Removes every package of the game's title, packages from an older engine first, and
    /// stops at the first failure. So it can never delete the current package and leave only
    /// one that needs Prepare again.
    private func remove(_ game: PreparedGame) {
        let names = artwork.prepared.filter { $0.applicationID == game.applicationID }
            .sorted { !$0.current && $1.current }.map(\.folder)
        Task {
            let failure = await Task.detached(priority: .userInitiated) { () -> String? in
                for name in names {
                    do { try PreparedGameCleanup.delete(named: name) } catch { return error.localizedDescription }
                }
                return nil
            }.value
            artwork.refreshPrepared()
            if let failure { library.error = failure } else { Haptics.click() }
        }
    }
#endif

    /// A game page for imported games; a prepared game with no imported file starts straight away.
    private func openEntry(_ id: String) {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if let folder = GameArtworkStore.preparedFolder(id) {
            // A package from an older engine would only fail its launch check.
            guard artwork.prepared.first(where: { $0.folder == folder })?.current == true else {
                library.error = "\(AppBrand.name)'s engine changed, so this game has to be prepared again. Import the game from Files or iCloud Drive, then prepare it. Your saves are kept."
                return
            }
            open(.preparedPackage(folder)); return
        }
#endif
        open(.detail(id))
    }

    /// The game page's Prepare opens the Prepare screen with this game already chosen.
    private func prepareAction(for item: HomebrewItem) -> (() -> Void)? {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        return { open(.guidedPrepare(item.id)) }
#else
        return NativePlay.available ? {} : nil
#endif
    }

    // MARK: Bar

    /// Library actions as system bar items in the trailing vertical bar (the Duo, iOS 27.1).
    /// Where the system would draw a normal top bar instead, its full-width strip would
    /// sit over the header row and swallow its taps (keys badge, search field), so the
    /// home screen hides it and shows the same actions at the end of the header row.
    @ToolbarContentBuilder private var homeToolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .topBarTrailing) {
            if !shelfItems.isEmpty {
                searchButton.satchelBarButton()
                sortMenu.satchelBarButton()
            }
            workshopMenu.satchelBarButton()
            settingsButton.satchelBarButton()
        }
        ToolbarItem(placement: .primaryAction) {
            addButton.satchelBarButton(SatchelTheme.ember)
        }
    }

    /// The same actions in the header row, for screens without a vertical bar.
    private var inlineActions: some View {
        HStack(spacing: 8) {
            Group {
                if !shelfItems.isEmpty {
                    searchButton
                    sortMenu
                }
                workshopMenu
                settingsButton
            }
            .modifier(InlineBarStyle(prominent: false))
            addButton.modifier(InlineBarStyle(prominent: true))
        }
        .labelStyle(.iconOnly)
        .buttonBorderShape(.circle)
        .controlSize(.large)
        .fixedSize()
    }

    private var searchButton: some View {
        Button {
            withAnimation(SatchelTheme.snap) { searching.toggle() }
            searchFocused = searching
            if !searching { query = "" }
        } label: { Label("Search", systemImage: "magnifyingglass") }
        .accessibilityIdentifier("search-library")
    }

    private var sortMenu: some View {
        Menu {
            Picker("Sort library", selection: $newestFirst) {
                Label("Recently added", systemImage: "clock").tag(true)
                Label("Name", systemImage: "textformat").tag(false)
            }
        } label: { Label("Sort library", systemImage: "arrow.up.arrow.down") }
        .accessibilityIdentifier("sort-library")
    }

    private var settingsButton: some View {
        Button { open(.settings) } label: { Label("Settings", systemImage: "gearshape.fill") }
            .controllerAction("settings") { open(.settings) }
            .accessibilityIdentifier("open-settings")
    }

    private var addButton: some View {
        Button { Haptics.click(); openImporter() } label: { Label("Add games", systemImage: "plus") }
            .disabled(library.runningID != nil)
            .accessibilityIdentifier("import-homebrew")
    }

    // MARK: Header

    private func topBar(compact: Bool, cut: ScreenTraits) -> some View {
        HStack(spacing: compact ? 8 : 10) {
            brand.padding(.trailing, compact ? 0 : 6)
            if searching {
                searchField.transition(.move(edge: .leading).combined(with: .opacity))
            } else {
                statusChips(compact: compact).transition(.opacity)
            }
#if !SATCHEL_UI_PREVIEW_MINIMAL
            pausedGameCard
            prepareJobSlot
#endif
            if route != .importer { importJobCard }
            Spacer(minLength: 8)
            if cut.bar == .top { inlineActions }
        }
        .frame(height: 52)
    }

#if !SATCHEL_UI_PREVIEW_MINIMAL
    /// The Prepare job's card: live progress while it runs, then its result until dismissed.
    private var prepareJobSlot: some View {
        PrepareJobSlot(library: library, artwork: artwork, open: { open(.prepare) }, play: { open(.preparedPackage($0)) },
                       openWarmup: openWarmup)
    }

    /// A warm-up card opens the game page of its package, or its play screen when the
    /// package has no imported game here.
    private func openWarmup(_ package: String) {
        if let item = library.items.first(where: { artwork.preparedPackage(for: $0)?.folder == package }) {
            open(.detail(item.id))
        } else {
            open(.preparedPackage(package))
        }
    }

    /// "Paused · <game> · Resume" while a game waits in the background and no cover is up.
    @ViewBuilder private var pausedGameCard: some View {
        if let package = pausedPackage, presentation == nil {
            let game = artwork.prepared.first { $0.folder == package }
            let name = game.flatMap { artwork.titleInfo($0.applicationID)?.title }
                ?? game.flatMap { NativeGameProfile.find(titleID: String(format: "%016llX", $0.applicationID))?.name } ?? package
            Button { Haptics.click(); open(.preparedPackage(package)) } label: {
                HStack(spacing: 10) {
                    Image(systemName: "pause.fill").font(.system(size: 14, weight: .black)).foregroundStyle(SatchelTheme.shell)
                        .frame(width: 32, height: 32).background(SatchelTheme.ember, in: Circle())
                    VStack(alignment: .leading, spacing: 1) {
                        Text("Paused").font(SatchelTheme.label(10)).tracking(1.2).foregroundStyle(SatchelTheme.textDim)
                        Text(name).font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                            .lineLimit(1)
                    }
                    SatchelButtonLabel(title: "Resume", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 32)
                }
                .padding(.leading, 9).padding(.trailing, 10)
                .frame(height: 52)
                .frame(maxWidth: 300, alignment: .leading)
                .background(SatchelTheme.surface.opacity(0.94), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                .overlay { RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(SatchelTheme.ember.opacity(0.45)) }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
            .accessibilityLabel("Paused, \(name), Resume")
            .accessibilityIdentifier("paused-game-card")
            .transition(.move(edge: .top).combined(with: .opacity))
        }
    }
#endif

    private var brand: some View {
        HStack(spacing: 10) {
            BrandMark(size: 26)
            Text(AppBrand.name).font(SatchelTheme.display(21)).foregroundStyle(SatchelTheme.text)
                .lineLimit(1).fixedSize()
        }
    }

    private var searchField: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass").foregroundStyle(SatchelTheme.textDim)
            TextField("Find a game", text: $query)
                .font(.system(size: 15, weight: .semibold, design: .rounded))
                .autocorrectionDisabled().textInputAutocapitalization(.never)
                .focused($searchFocused).submitLabel(.search).onSubmit { searchFocused = false }
                .accessibilityIdentifier("library-search")
            Button { query = ""; withAnimation(SatchelTheme.snap) { searching = false }; searchFocused = false } label: {
                Image(systemName: "xmark.circle.fill").foregroundStyle(SatchelTheme.textDim).frame(width: 36, height: 36)
            }.accessibilityLabel("Close search").accessibilityIdentifier("close-search")
        }
        .padding(.leading, 14).frame(height: 44).frame(maxWidth: 320)
        .background(SatchelTheme.surfaceHigh, in: Capsule())
    }

    // MARK: Book posture (half folded): focused game on the left page, library on the right

    private func bookHome(cut: ScreenTraits, size: CGSize, fold: FoldLine) -> some View {
        let pages = fold.pages(in: size)
        let sideInsets = fold.isVertical ? cut.trailing + 42 : cut.leading + cut.trailing + 40
        return FoldSplit(fold: fold, cut: cut) {
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 8) { brand; Spacer(minLength: 4); statusChips(compact: true) }.frame(height: 52)
#if !SATCHEL_UI_PREVIEW_MINIMAL
                pausedGameCard
                prepareJobSlot
#endif
                Spacer(minLength: 0)
                if let entry = focusedEntry {
                    let tile = { (side: CGFloat) in
                        GameTile(entry: entry, title: artwork.title(entry.item), image: artwork.image(entry.item),
                                 state: artwork.state(entry.item), glow: artwork.glow(entry.item), focused: true, side: max(110, side))
                    }
                    if fold.isVertical {
                        tile(min(pages.first.width - cut.leading - 60, pages.first.height - 300, 260))
                        focusedDetails(entry, stacked: true)
                    } else {
                        // Top page of a tall window: tile and details side by side.
                        HStack(alignment: .center, spacing: 18) {
                            tile(min(pages.first.height - 110, 260))
                            focusedDetails(entry, stacked: true)
                        }
                    }
                } else {
                    welcome(compact: true)
                }
                Spacer(minLength: 0)
            }
            .padding(.vertical, 8)
        } second: {
            VStack(spacing: 12) {
                HStack(spacing: 8) {
                    if searching { searchField } else { Spacer(minLength: 0) }
                    if route != .importer { importJobCard }
                    if cut.bar == .top { inlineActions }
                }
                .frame(height: 52)
                if !shelfItems.isEmpty { bookGrid(width: pages.second.width - sideInsets) }
            }
            .padding(.vertical, 8)
        }
    }

    private func bookGrid(width: CGFloat) -> some View {
        let columns = width > 420 ? 3 : 2
        let side = (width - CGFloat(columns - 1) * 8) / CGFloat(columns) - 14
        let focused = focusedEntry?.id
        return ScrollView {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: columns), spacing: 8) {
                ForEach(entries) { entry in
                    Button { tapped(entry.id) } label: {
                        GameTile(entry: entry, title: artwork.title(entry.item), image: artwork.image(entry.item),
                                 state: artwork.state(entry.item), glow: artwork.glow(entry.item),
                                 focused: focused == entry.id, side: max(60, side))
                    }
                    .buttonStyle(PressableStyle(scale: 0.96, haptic: false))
                    .accessibilityIdentifier("homebrew-\(entry.item.name)")
                    .controllerAction("game-" + entry.id) { openEntry(entry.item.id) }
                    .accessibilityLabel("\(artwork.title(entry.item)), \(artwork.state(entry.item).label)")
                }
                Button { openImporter() } label: { AddGameTile(side: max(60, side), focused: false) }
                    .buttonStyle(PressableStyle(scale: 0.96, haptic: false))
                    .accessibilityLabel("Add games").accessibilityIdentifier("add-games-tile")
                    .controllerAction("add-games") { openImporter() }
            }
            .padding(.vertical, 6)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
        .accessibilityIdentifier("collection-panel")
    }

    private func statusChips(compact: Bool) -> some View {
        HStack(spacing: 8) {
            Button { open(.settings) } label: {
                SatchelChip(text: artwork.keysAvailable ? "Keys ready" : "Keys needed",
                           systemImage: artwork.keysAvailable ? "checkmark.shield.fill" : "key.fill",
                           tint: artwork.keysAvailable ? SatchelTheme.leaf : SatchelTheme.spark)
            }
            .buttonStyle(PressableStyle(scale: 0.94))
            .accessibilityHint(artwork.keysAvailable ? "Opens Console keys in Settings" : "Opens Console keys in Settings to import prod.keys")
            .accessibilityIdentifier("keys-status")
            if !shelfItems.isEmpty && !compact {
                SatchelChip(text: shelfCount == 1 ? "1 game" : "\(shelfCount) games", systemImage: "square.stack.fill")
            }
        }
        .fixedSize()
    }

    /// The import's card: live progress while it runs, then its result until dismissed.
    private var importJobCard: some View {
        ImportJobCard(library: library, artwork: artwork, open: { open(.detail($0)) }, details: { open(.importer) })
    }

    /// Every + entry point: a fresh import screen, so the last result is cleared first.
    private func openImporter() {
        if !library.importing { library.clearOutcome() }
        open(.importer)
    }

    // MARK: Shelf

    private func shelf(tile: CGFloat, cut: ScreenTraits, inset: CGFloat, width: CGFloat) -> some View {
        let list = entries
        let focused = focusedEntry?.id ?? (focusedID == Self.addTileID ? Self.addTileID : nil)
        return ScrollView(.horizontal) {
            LazyHStack(alignment: .bottom, spacing: tile * 0.04) {
                ForEach(list) { entry in
                    Button { tapped(entry.id) } label: {
                        GameTile(entry: entry, title: artwork.title(entry.item), image: artwork.image(entry.item),
                                 state: artwork.state(entry.item), glow: artwork.glow(entry.item),
                                 focused: focused == entry.id, side: tile)
                    }
                    .buttonStyle(PressableStyle(scale: 0.96, haptic: false))
                    .id(entry.id)
                    .accessibilityIdentifier("homebrew-\(entry.item.name)")
                    .controllerAction("game-" + entry.id) { openEntry(entry.item.id) }
                    .accessibilityLabel("\(artwork.title(entry.item)), \(artwork.state(entry.item).label)")
                    .accessibilityHint(focused == entry.id ? "Opens the game page" : "Selects this game")
                    .contextMenu {
                        Button("Open", systemImage: "arrow.up.right") { openEntry(entry.item.id) }
                            .accessibilityIdentifier("library-context-open")
#if !SATCHEL_UI_PREVIEW_MINIMAL
                        if let game = artwork.preparedPackage(for: entry.item) ?? artwork.newestPackage(for: entry.item) {
                            Button("View game report", systemImage: "doc.text.magnifyingglass") { reportPackage = game.folder }
                            Button("Remove prepared game", systemImage: "trash", role: .destructive) { pendingRemoval = game }
                                .accessibilityIdentifier("library-context-remove-prepared")
                        }
#endif
                    }
                }
                Button { tapped(Self.addTileID) } label: {
                    AddGameTile(side: tile, focused: focused == Self.addTileID)
                }
                .buttonStyle(PressableStyle(scale: 0.96, haptic: false))
                .id(Self.addTileID)
                .accessibilityLabel("Add games").accessibilityIdentifier("add-games-tile")
                    .controllerAction("add-games") { openImporter() }
            }
            .scrollTargetLayout()
            .padding(.vertical, 14)
        }
        .scrollIndicators(.hidden)
        // The focused tile's glow reaches past the shelf; let it bleed behind the title
        // below instead of ending in a hard edge at the scroll view's bounds.
        .scrollClipDisabled()
#if !SATCHEL_UI_PREVIEW_MINIMAL
        // On the shelf, not the root: a confirmationDialog beside the root's alert left the
        // NavigationStack without its content on iOS 27.1 (a black home screen).
        .alert("Remove this prepared game?", isPresented: Binding(get: { pendingRemoval != nil }, set: { if !$0 { pendingRemoval = nil } }),
               presenting: pendingRemoval) { game in
            Button("Remove", role: .destructive) { remove(game) }
                .accessibilityIdentifier("confirm-remove-prepared")
            Button("Cancel", role: .cancel) {}
                .accessibilityIdentifier("cancel-remove-prepared")
        } message: { game in
            let count = artwork.prepared.filter { $0.applicationID == game.applicationID }.count
            Text(count > 1 ? "Deletes all \(count) prepared copies of this game from this iPhone to free their space. Your saves are kept. Prepare the game again to play it."
                           : "Deletes \(game.folder) from this iPhone to free its space. Your saves are kept. Prepare the game again to play it.")
        }
#endif
        .scrollTargetBehavior(.viewAligned)
        .scrollPosition(id: $scrolledID, anchor: .leading)
        .contentMargins(.leading, cut.leading + inset - 7, for: .scrollContent)
        .contentMargins(.trailing, max(cut.trailing + inset, width - tile - cut.leading - inset - 14), for: .scrollContent)
        .frame(height: tile + 42)
        .onChange(of: scrolledID) { _, id in
            guard let id, id != focusedID else { return }
            withAnimation(SatchelTheme.snap) { focusedID = id }
            Haptics.tick()
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("collection-panel")
#if !SATCHEL_UI_PREVIEW_MINIMAL
        .sheet(isPresented: Binding(get: { reportPackage != nil }, set: { if !$0 { reportPackage = nil } })) {
            if let reportPackage { NativeGameReportView(package: reportPackage, titleID: "") }
        }
#endif

    }

    private func tapped(_ id: String) {
        let current = focusedEntry?.id ?? (focusedID == Self.addTileID ? Self.addTileID : nil)
        if id == current {
            Haptics.click()
            if id == Self.addTileID { openImporter() }
            else if shelfItems.contains(where: { $0.id == id }) { openEntry(id) }
        } else {
            Haptics.tick()
            withAnimation(SatchelTheme.snap) { focusedID = id; scrolledID = id }
        }
    }

    // MARK: Focused game

    @ViewBuilder private var focusedInfo: some View {
        if let entry = focusedEntry {
            focusedDetails(entry, stacked: false).frame(height: 76)
        } else if focusedID == Self.addTileID {
            HStack {
                VStack(alignment: .leading, spacing: 5) {
                    Text("Add games").font(SatchelTheme.display(26))
                    Text("XCI, NSP, XCZ and NSZ games, updates and NRO homebrew from Files or iCloud Drive.")
                        .font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                }
                Spacer()
                Button { openImporter() } label: {
                    SatchelButtonLabel(title: "Choose files", systemImage: "plus", kind: .accent(SatchelTheme.ember), height: 50)
                }.buttonStyle(PressableStyle()).accessibilityIdentifier("choose-focused-files")
            }
            .frame(height: 76)
        } else {
            Text("No games match \u{201C}\(query)\u{201D}.").font(SatchelTheme.display(18, .bold)).foregroundStyle(SatchelTheme.textDim)
                .frame(maxWidth: .infinity, minHeight: 76, alignment: .leading)
        }
    }

    /// Badge, title, subtitle and actions for the focused game. Side by side
    /// under the shelf; stacked on the left page in the book posture.
    @ViewBuilder private func focusedDetails(_ entry: ShelfEntry, stacked: Bool) -> some View {
        let state = artwork.state(entry.item)
        let text = VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 8) {
                StateBadge(state: state)
                if let update = entry.updates.first, let version = artwork.info(update)?.displayVersion {
                    SatchelChip(text: "Update v" + version, systemImage: "arrow.up.circle.fill", tint: SatchelTheme.violet)
                }
            }
            Text(artwork.title(entry.item)).font(SatchelTheme.display(26)).foregroundStyle(SatchelTheme.text)
                .accessibilityIdentifier("focused-item-" + entry.id)
                .lineLimit(stacked ? 2 : 1).minimumScaleFactor(0.7)
            Text(artwork.subtitle(entry.item, updates: entry.updates))
                .font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .lineLimit(1).minimumScaleFactor(0.8)
        }
        .id(entry.id)
        .transition(.asymmetric(insertion: .move(edge: .bottom).combined(with: .opacity), removal: .opacity))
        let buttons = HStack(spacing: 12) {
            if state == .ready, let play = playAction(for: entry.item) {
                Button { Haptics.click(); play() } label: {
                    SatchelButtonLabel(title: playTitle(entry.item), systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 50).fixedSize()
                }.buttonStyle(PressableStyle()).accessibilityIdentifier("play-focused")
            }
            // A prepared game without an imported file has no game page: Play is its only action.
            if GameArtworkStore.preparedFolder(entry.item.id) == nil {
                Button { Haptics.click(); openEntry(entry.item.id) } label: {
                    SatchelButtonLabel(title: state == .homebrew ? "Open and play" : "Open", systemImage: "arrow.up.right",
                                      kind: state == .ready ? .secondary : .primary, height: 50).fixedSize()
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("open-focused")
            }
        }
        Group {
            if stacked {
                VStack(alignment: .leading, spacing: 14) { text; buttons }
            } else {
                HStack(alignment: .center, spacing: 18) { text; Spacer(minLength: 12); buttons }
            }
        }
        .animation(SatchelTheme.snap, value: entry.id)
    }

    // MARK: Welcome

    private func welcome(compact: Bool) -> some View {
        HStack(alignment: .center, spacing: compact ? 18 : 32) {
            VStack(alignment: .leading, spacing: 10) {
                SatchelEyebrow(text: "Welcome", tint: SatchelTheme.ember)
                Text("Your games,\npacked to go.").font(SatchelTheme.display(compact ? 28 : 36, .black)).foregroundStyle(SatchelTheme.text)
                    .fixedSize(horizontal: false, vertical: true)
                Text("Three steps and you're playing. Everything happens on this iPhone.")
                    .font(.system(size: 15, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: compact ? 200 : 300, alignment: .leading)
            HStack(spacing: compact ? 10 : 14) {
                welcomeStep(compact, 1, "Add keys", "Import prod.keys from your console.", "key.fill", SatchelTheme.spark,
                            done: artwork.keysAvailable, button: artwork.keysAvailable ? "Keys ready" : "Import keys") { open(.settings) }
                welcomeStep(compact, 2, "Add a game", "XCI, NSP, XCZ or NSZ, plus its update.", "plus", SatchelTheme.ember,
                            done: false, button: "Choose files") { openImporter() }
                welcomeStep(compact, 3, "Prepare and play", "\(AppBrand.name) readies it for iPhone.", "play.fill", SatchelTheme.leaf,
                            done: false, button: nil) {}
            }
        }
        .frame(maxHeight: .infinity)
    }

    private func welcomeStep(_ compact: Bool, _ number: Int, _ title: String, _ detail: String, _ icon: String, _ tint: Color,
                             done: Bool, button: String?, action: @escaping () -> Void) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("\(number)").font(SatchelTheme.display(15, .black)).foregroundStyle(SatchelTheme.shell)
                    .frame(width: 30, height: 30).background(tint, in: Circle())
                Spacer()
                Image(systemName: done ? "checkmark.circle.fill" : icon).font(.system(size: 20, weight: .bold))
                    .foregroundStyle(done ? SatchelTheme.leaf : tint)
            }
            Spacer(minLength: 0)
            Text(title).font(SatchelTheme.display(18, .bold)).foregroundStyle(SatchelTheme.text)
                .lineLimit(2).minimumScaleFactor(0.85).fixedSize(horizontal: false, vertical: true)
            Text(detail).font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
            if let button {
                Button(action: action) {
                    SatchelButtonLabel(title: button, kind: done ? .quiet : .accent(tint == SatchelTheme.spark ? SatchelTheme.ember : tint), height: 38, fill: true)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier(number == 2 ? "hero-import-homebrew" : "welcome-step-\(number)")
            } else {
                Spacer().frame(height: 38)
            }
        }
        .padding(16)
        .frame(width: compact ? 132 : 168, height: compact ? 220 : 210)
        .background(SatchelTheme.surface.opacity(0.9), in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 24, style: .continuous)
                .strokeBorder(done ? SatchelTheme.leaf.opacity(0.5) : SatchelTheme.line, lineWidth: done ? 2 : 1)
        }
    }

    private func saveSample(_ url: URL) {
        do { exportDocument = NRODocument(data: try Data(contentsOf: url)); exporting = true }
        catch { library.error = error.localizedDescription }
    }
}

#if !SATCHEL_UI_PREVIEW_MINIMAL
/// Native player hooks on the home screen; the prepared-game and prepare screens are routes
/// of the single cover.
private struct NativeGameHooks: ViewModifier {
    let open: (HomeRoute) -> Void
    private static var thenUsed = false

    func body(content: Content) -> some View {
        content
            .onReceive(NotificationCenter.default.publisher(for: .SatchelMenuReturnToGame)) { _ in
                if let running = PreparedLaunch.current { open(.preparedPackage(running)) }
            }
            // Second-launch check: after the first game quits, start another in this process.
            // devicectl ... -- -SatchelAutoLaunchPreparedGame YES -SatchelPreparedGame A -SatchelThenPreparedGame B
            .onReceive(NotificationCenter.default.publisher(for: .SatchelNativeSessionDidEnd)) { _ in
                guard !NativePreparedCycle.requested, UserDefaults.standard.bool(forKey: "SatchelDeveloperControls"), !Self.thenUsed,
                      let next = UserDefaults.standard.string(forKey: "SatchelThenPreparedGame"),
                      PreparedGame.scan().contains(where: { $0.folder == next }) else { return }
                Self.thenUsed = true
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { open(.preparedPackage(next)) }
            }
            // Lets the Mac start the prepared game with no taps:
            // devicectl device process launch ... com.dell.satchel -- -SatchelAutoLaunchPreparedGame YES
            .onAppear {
                let developer = UserDefaults.standard.bool(forKey: "SatchelDeveloperControls")
                if NativePreparedCycle.requested {
                    NativePreparedCycle.start { open(.preparedPackage($0)) }
                    return
                }
                if developer { PreparedGameCleanup.runRequested() }
                if developer && PreparedLaunch.launchFlag("SatchelAutoPrepare") && !SatchelNativeSessionStarted() { open(.prepare) }
                else if developer && PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame") && !SatchelNativeSessionStarted() {
                    let games = PreparedGame.scan()
                    let name = UserDefaults.standard.string(forKey: "SatchelPreparedGame") ?? games.max { $0.date < $1.date }?.folder
                    if let name, games.contains(where: { $0.folder == name }) { open(.preparedPackage(name)) }
                }
            }
    }
}
#endif

/// Satchel's bar item look for the home actions when they sit in the header row.
private struct InlineBarStyle: ViewModifier {
    let prominent: Bool
    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            if prominent { content.buttonStyle(.glassProminent).tint(SatchelTheme.ember) }
            else { content.buttonStyle(.glass).tint(SatchelTheme.text) }
        } else {
            content.buttonStyle(.bordered).tint(prominent ? SatchelTheme.ember : SatchelTheme.text)
        }
    }
}
