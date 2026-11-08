import SwiftUI
import UniformTypeIdentifiers

/// Imports prod.keys into the app's device-only Keychain item (SwitchKeys).
final class KeyStore: ObservableObject {
    @Published private(set) var available = (try? SwitchKeys.load()) != nil
    @Published private(set) var keyCount = ((try? SwitchKeys.load()) ?? nil)?.count ?? 0
    @Published private(set) var busy = false
    @Published var message: String?
    @Published var error: String?
    private let queue = DispatchQueue(label: "com.dell.satchel.keys", qos: .userInitiated)

    func importKeys(_ url: URL) {
        guard !busy else { return }
        busy = true; error = nil; message = nil
        queue.async {
            let result: Result<Int, Error> = Result {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                var coordinationError: NSError?
                var inner: Result<Int, Error> = .failure(SwitchKeyFailure.invalid)
                NSFileCoordinator().coordinate(readingItemAt: url, options: .withoutChanges, error: &coordinationError) { readable in
                    inner = Result {
                        let file = try FileHandle(forReadingFrom: readable)
                        defer { try? file.close() }
                        let data = try file.read(upToCount: SwitchKeys.maximumFileSize + 1) ?? Data()
                        let keys = try SwitchKeys.parse(data)
                        try SwitchKeys.save(keys)
                        return keys.count
                    }
                }
                if let coordinationError { throw coordinationError }
                return try inner.get()
            }
            DispatchQueue.main.async {
                self.busy = false
                switch result {
                case .success(let count):
                    self.available = true; self.keyCount = count
                    self.message = "Keys saved. They stay on this iPhone, in its secure storage."
                    Haptics.success()
                    NotificationCenter.default.post(name: .satchelKeysChanged, object: nil)
                case .failure(let error):
                    self.error = (error as? SwitchKeyFailure)?.errorDescription
                        ?? "That file couldn't be opened. Make sure it is fully downloaded in Files and try again."
                    Haptics.warning()
                }
            }
        }
    }
}

struct SettingsView: View {
    enum Page: String, CaseIterable, Identifiable {
        case keys = "Console keys", storage = "Storage", prepared = "Prepared games", device = "Device check", controllers = "Controllers", about = "About"
        var id: String { rawValue }
        var icon: String {
            switch self {
            case .keys: return "key.fill"
            case .storage: return "internaldrive.fill"
            case .prepared: return "hammer.fill"
            case .device: return "checkmark.shield.fill"
            case .controllers: return "gamecontroller.fill"
            case .about: return "info.circle.fill"
            }
        }
        var tint: Color {
            switch self {
            case .keys: return SatchelTheme.spark
            case .storage: return SatchelTheme.tide
            case .prepared: return SatchelTheme.leaf
            case .device: return SatchelTheme.tide
            case .controllers: return SatchelTheme.leaf
            case .about: return SatchelTheme.violet
            }
        }
    }

    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    @StateObject private var keys = KeyStore()
    @State var page: Page = .keys
    @AppStorage("SatchelDeveloperControls") private var developerControls = false
    @AppStorage(PreparedImportCleanup.settingKey) private var removeOriginals = true
    @State private var showDeveloperControls = false
    @State private var showingInstallGuide = false
    @State private var showingLicenses = false
    @State private var pickingKeys = false
    @State private var usage: StorageUsage?
    @State private var cacheBytes: Int64 = 0
    @State private var cleaningLeftovers = false
    @State private var storageMessage: String?
    @State private var pendingDelete: PreparedGame?
    @State private var deleting: String?
    @State private var deleteFailure: String?
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        PanelChrome(identifier: "close-settings", close: { dismiss() }) {
            ScreenReader { cut, geometry in
                let compact = geometry.size.width - cut.leading - cut.trailing < 760
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: sections on the first page, the chosen section on the second.
                        FoldSplit(fold: fold, cut: cut) {
                            sidebar.frame(maxWidth: 300, alignment: .leading).frame(maxWidth: .infinity, alignment: .leading)
                        } second: {
                            pageScroll(compact: true, reserve: fold.isVertical ? reserve : 0)
                        }
                    } else {
                        HStack(alignment: .top, spacing: compact ? 16 : 24) {
                            sidebar.frame(width: compact ? 188 : 210)
                            pageScroll(compact: compact, reserve: reserve)
                        }
                        .cutoutPadding(cut, leading: compact ? 16 : 24, trailing: compact ? 16 : 24)
                    }
                }
            }
            .background { SatchelBackdrop(glow: page.tint) }
            .satchelMenuStatus()
        }
        // A controller picks a section and B closes Settings, as on the other screens.
        .controllerNavigation(enabled: !pickingKeys && !showingInstallGuide && !showingLicenses && pendingDelete == nil, back: { dismiss() })
        .sheet(isPresented: $pickingKeys) {
            VStack(spacing: 0) {
                HStack {
                    Spacer()
                    Button("Cancel import") { pickingKeys = false }
                        .accessibilityIdentifier("cancel-keys-import")
                        .padding()
                }
                KeysDocumentPicker { url in
                    pickingKeys = false
                    if let url { keys.importKeys(url) }
                }
            }
        }
        .task(id: page) { await measure() }
        .sheet(isPresented: $showingInstallGuide) {
            InstallGuideView { showingInstallGuide = false }
        }
        .sheet(isPresented: $showingLicenses) {
            LicensesView { showingLicenses = false }
        }
    }

    private func pageScroll(compact: Bool, reserve: CGFloat) -> some View {
        ScrollViewReader { proxy in
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    Color.clear.frame(height: 0).id(Self.pageTop)
                    VStack(alignment: .leading, spacing: 16) {
                        Text(page.rawValue).font(SatchelTheme.display(compact ? 26 : 30)).padding(.trailing, reserve)
                            .accessibilityIdentifier("settings-page-" + page.rawValue)
                        switch page {
                        case .keys: keysPage
                        case .storage: storagePage
                        case .prepared: preparedPage
                        case .device: DeviceCheckView()
                        case .controllers: ControllersView()
                        case .about: aboutPage
                        }
                    }
                    .padding(.vertical, 18).padding(.trailing, 4)
                    .animation(SatchelTheme.glide, value: page)
                }
            }
            .scrollIndicators(.hidden)
            .scrollClipDisabled() // scrolls through the outer margin to the screen edge
            // Another section opens at its top, not where the last one was scrolled to.
            .onChange(of: page) { _, _ in proxy.scrollTo(Self.pageTop, anchor: .top) }
        }
    }

    private static let pageTop = "settings-page-top"

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                BrandMark(size: 24)
                Text("Settings").font(SatchelTheme.display(20))
            }
            .padding(.vertical, 18).padding(.leading, 6)
            ForEach(Page.allCases) { item in
                Button {
                    Haptics.tick()
                    page = item
                } label: {
                    HStack(spacing: 12) {
                        Image(systemName: item.icon).font(.system(size: 14, weight: .bold))
                            .foregroundStyle(page == item ? SatchelTheme.shell : item.tint)
                            .frame(width: 30, height: 30)
                            .background(page == item ? AnyShapeStyle(item.tint) : AnyShapeStyle(item.tint.opacity(0.15)),
                                        in: RoundedRectangle(cornerRadius: 9, style: .continuous))
                        Text(item.rawValue).font(.system(size: 15, weight: .bold, design: .rounded))
                            .lineLimit(1).minimumScaleFactor(0.75)
                        Spacer(minLength: 0)
                        if item == .keys && !keys.available {
                            Circle().fill(SatchelTheme.spark).frame(width: 8, height: 8)
                        }
                    }
                    .foregroundStyle(SatchelTheme.text)
                    .padding(.horizontal, 10).frame(minHeight: 48)
                    .background(page == item ? SatchelTheme.surfaceHigh : .clear, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
                    .contentShape(Rectangle())
                }
                .buttonStyle(PressableStyle(scale: 0.97, haptic: false))
                .accessibilityIdentifier("settings-" + item.rawValue)
                .controllerAction("settings-page-" + item.rawValue, style: .row(.rounded(14))) { Haptics.tick(); page = item }
            }
            Spacer()
        }
    }

    // MARK: Keys

    private var keysPage: some View {
        VStack(alignment: .leading, spacing: 16) {
            SatchelPanel {
                HStack(spacing: 14) {
                    Image(systemName: keys.available ? "checkmark.shield.fill" : "key.fill")
                        .font(.system(size: 26, weight: .bold))
                        .foregroundStyle(keys.available ? SatchelTheme.leaf : SatchelTheme.spark)
                        .frame(width: 54, height: 54)
                        .background((keys.available ? SatchelTheme.leaf : SatchelTheme.spark).opacity(0.14), in: Circle())
                    VStack(alignment: .leading, spacing: 4) {
                        Text(keys.available ? "Keys are set up" : "No keys yet").font(SatchelTheme.display(20, .bold))
                        Text(keys.available ? "\(keys.keyCount) keys stored in this iPhone's secure storage."
                                            : "\(AppBrand.name) needs your console's prod.keys to read your games.")
                            .font(.system(size: 13, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    }
                }
                if keys.busy {
                    SatchelProgressBar(value: nil, tint: SatchelTheme.spark, height: 10)
                } else {
                    Button { pickingKeys = true } label: {
                        SatchelButtonLabel(title: keys.available ? "Replace prod.keys" : "Import prod.keys", systemImage: "folder.fill",
                                          kind: keys.available ? .secondary : .accent(SatchelTheme.ember), height: 46)
                    }
                    .buttonStyle(PressableStyle())
                    .accessibilityIdentifier("import-keys")
                }
                if let message = keys.message {
                    Label(message, systemImage: "checkmark.circle.fill").font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(SatchelTheme.leaf)
                }
                if let error = keys.error {
                    Label(error, systemImage: "exclamationmark.triangle.fill").font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(SatchelTheme.ember).fixedSize(horizontal: false, vertical: true)
                }
            }
            SatchelPanel(title: "How it works") {
                bullet("1.circle.fill", "Dump prod.keys from your own console and save it to iCloud Drive or On My iPhone.")
                bullet("2.circle.fill", "Import it here. Only the keys \(AppBrand.name) uses are kept, in a Keychain item that never syncs or leaves this iPhone.")
                bullet("3.circle.fill", "Your games' titles and artwork appear in the library within seconds.")
            }
        }
    }

    private func bullet(_ icon: String, _ text: String) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: icon).font(.system(size: 18, weight: .bold)).foregroundStyle(SatchelTheme.tide)
                .frame(width: 26)
            Text(text).font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.text)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // MARK: Storage

    /// A Prepare or Add DLC is running, so its temporary folder is live.
    private var stagingBusy: String? {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if PrepareJob.shared.running { return "Wait for the current preparation to finish." }
        if AddOnJob.shared.running { return "Wait for Add DLC to finish." }
#endif
        return nil
    }

    /// "None" reads better than "Zero KB", and "…" shows while the first measurement runs.
    private func amount(_ bytes: Int64?) -> String {
        guard let bytes else { return "…" }
        return bytes <= 0 ? "None" : satchelBytes(bytes)
    }

    private func note(_ text: String) -> some View {
        Text(text).font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
    }

    private var storagePage: some View {
        let measured = usage != nil
        let games = usage?.imported ?? 0, prepared = usage?.prepared ?? 0
        let saves = usage?.saves ?? 0
        let caches = (usage?.caches ?? 0) + (usage?.shaderPacks ?? 0) + cacheBytes
        let leftovers = usage?.leftovers ?? 0
        let used = games + prepared + saves + caches + leftovers
        let total = max(1, used + (usage?.free ?? 0))
        return VStack(alignment: .leading, spacing: 16) {
            SatchelPanel {
                HStack(alignment: .firstTextBaseline) {
                    Text(measured ? satchelBytes(used) : "Measuring…").font(SatchelTheme.display(22))
                        .accessibilityIdentifier("storage-total")
                    Text("used by \(AppBrand.name)").font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(SatchelTheme.textDim)
                }
                if measured {
                    GeometryReader { geometry in
                        HStack(spacing: 3) {
                            segment(SatchelTheme.ember, Double(games) / Double(total), geometry.size.width)
                            segment(SatchelTheme.leaf, Double(prepared) / Double(total), geometry.size.width)
                            segment(SatchelTheme.violet, Double(saves) / Double(total), geometry.size.width)
                            segment(SatchelTheme.tide, Double(caches) / Double(total), geometry.size.width)
                            segment(SatchelTheme.spark, Double(leftovers) / Double(total), geometry.size.width)
                            Capsule().fill(Color.white.opacity(0.08))
                        }
                    }
                    .frame(height: 18)
                } else {
                    SatchelProgressBar(value: nil, tint: SatchelTheme.tide, height: 18)
                }
                LazyVGrid(columns: [GridItem(.flexible(), alignment: .leading), GridItem(.flexible(), alignment: .leading)],
                          alignment: .leading, spacing: 12) {
                    legend(SatchelTheme.ember, "Imported games", measured ? games : nil)
                    legend(SatchelTheme.leaf, "Prepared", measured ? prepared : nil)
                    legend(SatchelTheme.violet, "Saves", measured ? saves : nil)
                    legend(SatchelTheme.tide, "Caches and art", measured ? caches : nil)
                    if leftovers > 0 { legend(SatchelTheme.spark, "Leftovers", leftovers) }
                    legend(Color.white.opacity(0.3), "Free", measured ? usage?.free : nil)
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("storage-overview")
            if let usage, usage.leftovers > 0 { leftoversPanel(usage) }
            if let storageMessage {
                Label(storageMessage, systemImage: "info.circle.fill").font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
                    .accessibilityIdentifier("storage-message")
            }
            if let usage, !usage.games.isEmpty { gamesPanel(usage) }
            SatchelPanel(title: "Original imports") {
                Toggle("Remove originals after Prepare", isOn: $removeOriginals)
                    .accessibilityIdentifier("remove-originals-after-prepare")
                note("On by default. After full package verification, removes imported games, covered updates and the shader pack used for Prepare. Turn this off to keep originals. Your library, artwork and saves stay on this iPhone; files in iCloud Drive are unchanged.")
            }
            CloudSaveSyncPanel()
            SatchelPanel(title: "Library") {
                note("\(library.items.count) library entries use \(amount(measured ? games : nil)). Delete a game from its page to free its space; its files in iCloud Drive or Files stay put.")
            }
            SatchelPanel(title: "Artwork cache") {
                note("Titles and icons read from your games, \(satchelBytes(cacheBytes)). Refresh reads available originals again and keeps art for removed originals.")
                Button {
                    artwork.clearCache(library.items, fileURL: library.localFile)
                    Task { await measure() }
                } label: {
                    SatchelButtonLabel(title: "Refresh artwork", systemImage: "arrow.clockwise", kind: .secondary, height: 42)
                }.buttonStyle(PressableStyle())
            }
        }
    }

    /// Each game's share: its imported files, prepared copies, saves and caches.
    private func gamesPanel(_ usage: StorageUsage) -> some View {
        SatchelPanel(title: "By game") {
            ForEach(usage.games) { game in
                let parts = [("Imported", game.imported), ("Prepared", game.prepared), ("Saves", game.saves), ("Caches", game.caches)]
                    .filter { $0.1 > 0 }.map { "\($0.0) \(satchelBytes($0.1))" }
                HStack(alignment: .top, spacing: 12) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(gameName(game.id)).font(.system(size: 14, weight: .bold, design: .rounded))
                            .foregroundStyle(SatchelTheme.text).lineLimit(2)
                        Text(parts.joined(separator: "  ·  ")).font(.system(size: 12, weight: .medium, design: .rounded))
                            .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
                    }
                    Spacer(minLength: 8)
                    Text(satchelBytes(game.total)).font(.system(size: 13, weight: .bold, design: .rounded))
                        .foregroundStyle(SatchelTheme.text).fixedSize()
                }
                .accessibilityElement(children: .combine)
                .accessibilityIdentifier("storage-game-" + game.id.titleIDString)
            }
            note("Delete a game from its page to free all of this. You choose there whether its saves stay.")
        }
    }

    private func gameName(_ id: UInt64) -> String {
        if let item = library.items.first(where: { artwork.info($0)?.kind == .application && artwork.info($0)?.applicationID == id }) {
            return artwork.title(item)
        }
        if let title = artwork.titleInfo(id)?.title, !title.isEmpty { return title }
        return NativeGameProfile.find(titleID: id.titleIDString)?.name ?? "Game \(id.titleIDString)"
    }

    private func leftoversPanel(_ usage: StorageUsage) -> some View {
        let who = usage.leftoverCount == 1 ? "A preparation that was stopped before it finished" : "\(usage.leftoverCount) preparations that were stopped before they finished"
        return SatchelPanel(title: "Leftover files", tint: SatchelTheme.spark) {
            note(who + " left \(satchelBytes(usage.leftovers)) of temporary files. They are safe to delete; games, prepared games and saves are not touched.")
            if cleaningLeftovers {
                SatchelProgressBar(value: nil, tint: SatchelTheme.spark, height: 10)
            } else {
                SatchelConfirmReader { confirm in
                    let ask = {
                        guard stagingBusy == nil else { return }
                        confirm(SatchelConfirmRequest(title: "Delete leftover files?",
                            message: "Deletes \(satchelBytes(usage.leftovers)) of temporary files from preparations that did not finish. Games, prepared games and saves are not touched.",
                            actions: [SatchelConfirmAction(id: "confirm-clean-leftovers", title: "Delete leftover files", role: .destructive,
                                                           systemImage: "trash") { cleanLeftovers() }],
                            identifier: "leftovers-confirm"))
                    }
                    Button { ask() } label: {
                        SatchelButtonLabel(title: "Clean up leftovers", systemImage: "trash", kind: .secondary, height: 42)
                    }
                    .buttonStyle(PressableStyle())
                    .controllerAction("clean-up-leftovers", style: .button(.capsule)) { ask() }
                    .disabled(stagingBusy != nil)
                    .accessibilityIdentifier("clean-up-leftovers")
                }
                if let stagingBusy { SatchelDisabledReason(text: stagingBusy) }
            }
        }
    }

    private func cleanLeftovers() {
        guard stagingBusy == nil else { storageMessage = stagingBusy; return }
        cleaningLeftovers = true; storageMessage = nil
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        Task {
            let failures = await Task.detached(priority: .userInitiated) { StorageMeter.removeLeftovers(documents: documents) }.value
            cleaningLeftovers = false
            storageMessage = failures.isEmpty ? "Leftover files deleted." : "Some leftover files could not be deleted: " + failures.joined(separator: "; ")
            if failures.isEmpty { Haptics.success() } else { Haptics.warning() }
            await measure()
        }
    }

    private func segment(_ color: Color, _ fraction: Double, _ width: CGFloat) -> some View {
        Capsule().fill(color).frame(width: fraction > 0 ? max(6, width * fraction) : 0)
    }

    private func legend(_ color: Color, _ title: String, _ bytes: Int64?) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 6) {
                Circle().fill(color).frame(width: 9, height: 9)
                Text(title).font(.system(size: 12, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .lineLimit(1).minimumScaleFactor(0.85)
            }
            Text(amount(bytes)).font(.system(size: 14, weight: .bold, design: .rounded)).lineLimit(1).padding(.leading, 15)
        }
        .accessibilityElement(children: .combine)
    }

    // MARK: Prepared

    private var preparedPage: some View {
        VStack(alignment: .leading, spacing: 16) {
            if artwork.prepared.isEmpty {
                SatchelPanel {
                    Text("No prepared games yet.").font(SatchelTheme.display(18, .bold))
                    Text("Preparing turns an imported game into a version that runs natively on iPhone: its code is rewritten and signed, and its shaders are built. Prepared games show a green Play badge in the library.")
                        .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                        .fixedSize(horizontal: false, vertical: true)
                }
            } else {
                // Newest first; each row's date and tag tell copies of one game apart.
                ForEach(artwork.prepared.sorted { $0.date > $1.date }, id: \.folder) { game in
                    let match = library.items.first { artwork.info($0)?.kind == .application && artwork.info($0)?.applicationID == game.applicationID }
                    SatchelPanel {
                        HStack(spacing: 14) {
                            if let match {
                                GameTileArt(item: match, title: artwork.title(match), image: artwork.image(match), side: 56)
                                    .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                            } else {
                                Image(systemName: "shippingbox.fill").font(.system(size: 22)).foregroundStyle(SatchelTheme.leaf)
                                    .frame(width: 56, height: 56).background(SatchelTheme.leaf.opacity(0.14), in: RoundedRectangle(cornerRadius: 12))
                            }
                            VStack(alignment: .leading, spacing: 3) {
                                Text(preparedName(game)).font(.system(size: 16, weight: .bold, design: .rounded))
                                // One line where it fits; otherwise the title ID gets its own line
                                // rather than breaking in the middle.
                                ViewThatFits(in: .horizontal) {
                                    Text("Version \(game.version)  ·  \(game.applicationID.titleIDString)").lineLimit(1)
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text("Version \(game.version)")
                                        Text(game.applicationID.titleIDString).lineLimit(1).minimumScaleFactor(0.8)
                                    }
                                }
                                .font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim)
                                // The tag and date share a line where they fit; on the front
                                // screen the date goes below so the tag is never cut short.
                                ViewThatFits(in: .horizontal) {
                                    HStack(spacing: 8) { preparedTag(game); preparedDateText(game).fixedSize() }
                                    VStack(alignment: .leading, spacing: 4) { preparedTag(game); preparedDateText(game) }
                                }
                            }
                            Spacer()
                            Text(usage?.packages[game.folder].map(satchelBytes) ?? "…")
                                .font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
#if !SATCHEL_UI_PREVIEW_MINIMAL
                            Button { Haptics.tick(); pendingDelete = game } label: {
                                Image(systemName: "trash").font(.system(size: 15, weight: .bold)).foregroundStyle(SatchelTheme.ember)
                                    .frame(width: 44, height: 44)
                            }
                            .buttonStyle(PressableStyle(scale: 0.94))
                            .disabled(deleting != nil)
                            .accessibilityLabel("Delete prepared copy")
                            .accessibilityIdentifier("delete-prepared-" + game.folder)
#endif
                        }
                    }
                    .opacity(deleting == game.folder ? 0.5 : 1)
                }
            }
            if let deleteFailure {
                Label(deleteFailure, systemImage: "exclamationmark.triangle.fill").font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundStyle(SatchelTheme.ember).fixedSize(horizontal: false, vertical: true)
                    .accessibilityIdentifier("prepared-delete-failure")
            }
            Button { artwork.refreshPrepared(); Task { await measure() } } label: {
                SatchelButtonLabel(title: "Check again", systemImage: "arrow.clockwise", kind: .secondary, height: 42)
            }.buttonStyle(PressableStyle())
        }
        .alert("Delete this prepared copy?", isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
               presenting: pendingDelete) { game in
            Button("Delete", role: .destructive) { deletePrepared(game) }
                .accessibilityIdentifier("confirm-delete-prepared")
            Button("Cancel", role: .cancel) {}
                .accessibilityIdentifier("cancel-delete-prepared")
        } message: { game in
            Text("Deletes the copy of \(preparedName(game)) prepared \(preparedDate(game)) from this iPhone. Your saves are kept.")
        }
    }

    private func preparedTag(_ game: PreparedGame) -> some View {
        SatchelChip(text: game.current ? "Current" : "Needs Prepare again",
                   tint: game.current ? SatchelTheme.leaf : SatchelTheme.spark)
            .fixedSize()
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("prepared-tag-" + game.folder)
    }

    private func preparedDateText(_ game: PreparedGame) -> some View {
        Text(preparedDate(game)).font(.system(size: 12, weight: .semibold, design: .rounded))
            .foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
            .accessibilityIdentifier("prepared-date-" + game.folder)
    }

    private func preparedName(_ game: PreparedGame) -> String {
        let match = library.items.first { artwork.info($0)?.kind == .application && artwork.info($0)?.applicationID == game.applicationID }
        return match.map { artwork.title($0) } ?? artwork.titleInfo(game.applicationID)?.title ?? game.folder
    }

    private func preparedDate(_ game: PreparedGame) -> String {
        game.date.formatted(date: .abbreviated, time: .shortened)
    }

    /// Deletes one package folder (never saves) and refreshes this list and the library shelf.
    private func deletePrepared(_ game: PreparedGame) {
#if !SATCHEL_UI_PREVIEW_MINIMAL
        let name = game.folder
        deleting = name; deleteFailure = nil
        Task {
            let failure = await Task.detached(priority: .userInitiated) { () -> String? in
                do { try PreparedPackage.delete(named: name); return nil } catch { return error.localizedDescription }
            }.value
            deleting = nil
            artwork.refreshPrepared()
            if let failure { deleteFailure = failure } else { Haptics.click() }
            await measure()
        }
#endif
    }

    // MARK: About

    private var aboutPage: some View {
        VStack(alignment: .leading, spacing: 16) {
            SatchelPanel {
                HStack(spacing: 16) {
                    BrandMark(size: 44)
                    VStack(alignment: .leading, spacing: 3) {
                        Text(AppBrand.name).font(SatchelTheme.display(24))
                            .onTapGesture(count: 7) { showDeveloperControls = true }
                        if let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String {
                            Text("Version \(version) (\(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"))")
                                .font(SatchelTheme.mono(12)).foregroundStyle(SatchelTheme.textDim)
                        }
                    }
                }
                Text("Plays the games you own, entirely on this iPhone. Bring your own game dumps and keys; \(AppBrand.name) never downloads games or keys and never sends them anywhere.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Button { showingInstallGuide = true } label: {
                SatchelButtonLabel(title: "Install and refresh help", systemImage: "questionmark.circle.fill", kind: .secondary, height: 44)
            }.buttonStyle(PressableStyle()).accessibilityIdentifier("settings-install-help")
            if showDeveloperControls || developerControls {
                Toggle("Developer controls", isOn: $developerControls)
                    .accessibilityIdentifier("developer-controls")
            }
            SatchelPanel(title: "Built with") {
                bullet("cpu", "Native game execution and the Metal renderer build on NX2Apple (MIT).")
                bullet("paintbrush.pointed.fill", "Shader translation builds on Ryujinx (MIT).")
                bullet("doc.text", "Every open source license \(AppBrand.name) uses ships inside the app.")
                Button { showingLicenses = true } label: {
                    SatchelButtonLabel(title: "Licenses", systemImage: "doc.text.fill", kind: .secondary, height: 42)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("settings-licenses")
                .controllerAction("settings-licenses", style: .button(.capsule)) { showingLicenses = true }
            }
            SatchelPanel(title: "Not affiliated") {
                Text("\(AppBrand.name) is an independent project. It is not made, endorsed or supported by any console maker.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: Measuring

    private func measure() async {
        guard page == .storage || page == .prepared else { return }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let files = library.items.map { item in (url: library.localFile(item), title: GameRemoval.titleID(item, artwork: artwork)) }
        let packages = Dictionary(artwork.prepared.map { ($0.folder, $0.applicationID) }, uniquingKeysWith: { first, _ in first })
        cacheBytes = artwork.cacheBytes
        usage = await Task.detached(priority: .utility) { StorageMeter.measure(documents: documents, files: files, packages: packages) }.value
    }
}

/// The same guide as docs/install.md, bundled so help works without a hosted site.
private struct InstallGuideView: View {
    let close: () -> Void
    private let paragraphs: [String] = {
        guard let url = Bundle.main.url(forResource: "install", withExtension: "md"),
              let text = try? String(contentsOf: url, encoding: .utf8) else {
            return ["Install help is missing from this build. Ask the build owner for docs/install.md."]
        }
        return text.components(separatedBy: "\n\n").filter { !$0.isEmpty }
    }()

    var body: some View {
        PanelChrome(closeTitle: "Close help", identifier: "install-help-close", close: close) {
            ScreenReader { cut, _ in
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        ForEach(Array(paragraphs.enumerated()), id: \.offset) { _, paragraph in
                            if paragraph.hasPrefix("#") {
                                Text(paragraph.drop(while: { $0 == "#" || $0 == " " }))
                                    .font(SatchelTheme.display(22))
                            } else {
                                Text(.init(paragraph))
                                    .font(.system(size: 14, weight: .medium, design: .rounded))
                                    .textSelection(.enabled)
                            }
                        }
                    }
                    .frame(maxWidth: 680, alignment: .leading).frame(maxWidth: .infinity)
                    .padding(.vertical, 18).padding(.trailing, cut.bar == .top ? 56 : 0)
                    .cutoutPadding(cut, leading: 20, trailing: 20)
                }.accessibilityIdentifier("install-help-content")
            }
            .background { SatchelBackdrop(glow: SatchelTheme.tide) }.satchelMenuStatus()
        }
    }
}

/// An app-owned cancel action remains available if Files omits its toolbar.
private struct KeysDocumentPicker: UIViewControllerRepresentable {
    let completion: (URL?) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(completion: completion) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.data], asCopy: false)
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let completion: (URL?) -> Void
        init(completion: @escaping (URL?) -> Void) { self.completion = completion }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { completion(urls.first) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { completion(nil) }
    }
}
