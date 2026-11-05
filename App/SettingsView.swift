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
    @State private var pickingKeys = false
    @State private var freeBytes: Int64?
    @State private var preparedBytes: [String: Int64] = [:]
    @State private var cacheBytes: Int64 = 0
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
                            sidebar.frame(width: compact ? 172 : 210)
                            pageScroll(compact: compact, reserve: reserve)
                        }
                        .cutoutPadding(cut, leading: compact ? 16 : 24, trailing: compact ? 16 : 24)
                    }
                }
            }
            .background { SatchelBackdrop(glow: page.tint) }
            .satchelMenuStatus()
        }
        .fileImporter(isPresented: $pickingKeys, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls): if let url = urls.first { keys.importKeys(url) }
            case .failure: keys.error = "The key file could not be opened."
            }
        }
        .task(id: page) { await measure() }
        .sheet(isPresented: $showingInstallGuide) {
            InstallGuideView { showingInstallGuide = false }
        }
    }

    private func pageScroll(compact: Bool, reserve: CGFloat) -> some View {
        ScrollView {
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
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
    }

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

    private var storagePage: some View {
        let used = library.libraryBytes
        let prepared = preparedBytes.values.reduce(0, +)
        let free = freeBytes ?? 0
        let total = max(1, used + prepared + cacheBytes + free)
        return VStack(alignment: .leading, spacing: 16) {
            SatchelPanel {
                GeometryReader { geometry in
                    HStack(spacing: 3) {
                        segment(SatchelTheme.ember, Double(used) / Double(total), geometry.size.width)
                        segment(SatchelTheme.leaf, Double(prepared) / Double(total), geometry.size.width)
                        segment(SatchelTheme.tide, Double(cacheBytes) / Double(total), geometry.size.width)
                        Capsule().fill(Color.white.opacity(0.08))
                    }
                }
                .frame(height: 18)
                LazyVGrid(columns: [GridItem(.flexible(), alignment: .leading), GridItem(.flexible(), alignment: .leading)],
                          alignment: .leading, spacing: 12) {
                    legend(SatchelTheme.ember, "Games", used)
                    legend(SatchelTheme.leaf, "Prepared", prepared)
                    legend(SatchelTheme.tide, "Artwork", cacheBytes)
                    legend(Color.white.opacity(0.3), "Free", free)
                }
            }
            SatchelPanel(title: "Original imports") {
                Toggle("Remove originals after Prepare", isOn: $removeOriginals)
                    .accessibilityIdentifier("remove-originals-after-prepare")
                Text("On by default. After full package verification, removes imported games, covered updates and the shader pack used for Prepare. Turn this off to keep originals. Your library, artwork and saves stay on this iPhone; files in iCloud Drive are unchanged.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            CloudSaveSyncPanel()
            SatchelPanel(title: "Library") {
                Text("\(library.items.count) library entries use \(satchelBytes(used)). Remove a game from its page to free space; the original in Files stays put.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            SatchelPanel(title: "Artwork cache") {
                Text("Titles and icons read from your games, \(satchelBytes(cacheBytes)). Refresh reads available originals again and keeps art for removed originals.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
                Button {
                    artwork.clearCache(library.items, fileURL: library.localFile)
                    Task { await measure() }
                } label: {
                    SatchelButtonLabel(title: "Refresh artwork", systemImage: "arrow.clockwise", kind: .secondary, height: 42)
                }.buttonStyle(PressableStyle())
            }
        }
    }

    private func segment(_ color: Color, _ fraction: Double, _ width: CGFloat) -> some View {
        Capsule().fill(color).frame(width: fraction > 0 ? max(6, width * fraction) : 0)
    }

    private func legend(_ color: Color, _ title: String, _ bytes: Int64) -> some View {
        HStack(spacing: 6) {
            Circle().fill(color).frame(width: 9, height: 9)
            Text(title).font(.system(size: 12, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            Text(satchelBytes(bytes)).font(.system(size: 13, weight: .bold, design: .rounded)).lineLimit(1)
        }
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
                ForEach(artwork.prepared, id: \.folder) { game in
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
                                Text(match.map { artwork.title($0) } ?? game.folder).font(.system(size: 16, weight: .bold, design: .rounded))
                                Text("Version \(game.version)  ·  \(game.applicationID.titleIDString)")
                                    .font(SatchelTheme.mono(11)).foregroundStyle(SatchelTheme.textDim)
                            }
                            Spacer()
                            Text(preparedBytes[game.folder].map(satchelBytes) ?? "…")
                                .font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                        }
                    }
                }
            }
            Button { artwork.refreshPrepared(); Task { await measure() } } label: {
                SatchelButtonLabel(title: "Check again", systemImage: "arrow.clockwise", kind: .secondary, height: 42)
            }.buttonStyle(PressableStyle())
        }
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
                bullet("doc.text", "Full notices ship inside the app.")
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
        let folders = artwork.prepared.map(\.folder)
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let (free, sizes) = await Task.detached(priority: .utility) { () -> (Int64?, [String: Int64]) in
            let values = try? documents.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
            var sizes: [String: Int64] = [:]
            for folder in folders {
                let root = documents.appendingPathComponent("PreparedGames/" + folder, isDirectory: true)
                var total: Int64 = 0
                if let enumerator = FileManager.default.enumerator(at: root, includingPropertiesForKeys: [.totalFileAllocatedSizeKey]) {
                    while let url = enumerator.nextObject() as? URL {
                        total += Int64((try? url.resourceValues(forKeys: [.totalFileAllocatedSizeKey]).totalFileAllocatedSize) ?? 0)
                    }
                }
                sizes[folder] = total
            }
            return (values?.volumeAvailableCapacityForImportantUsage, sizes)
        }.value
        freeBytes = free; preparedBytes = sizes; cacheBytes = artwork.cacheBytes
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
