import GameController
import Security
import SwiftUI
import UIKit

// Simulator UI preview entry point. Seeds a library of fictional games with
// generated placeholder artwork (no game data, no real keys), then shows the
// real views. Launch arguments:
//   -SatchelPreviewDemo library|empty     seed the demo library or start empty
//   -SatchelPreviewKeys YES|NO            store dummy all-zero keys or remove them
//   -SatchelPreviewScreen home|detail|import|settings|menu|game|prepared|player|verifying|prepare
//                                        open one screen directly (default home); game and
//                                        the dev screens after it need build-preview.zsh game
//   -SatchelPreviewPrepared YES           seed fictional prepared-game manifests, one per status
//   -SatchelPreviewIdentity YES|expiring|expired   a fictional signing identity for Prepare
//   -SatchelPreviewFailure "text"         verifying: show this failure instead of progress
//   -SatchelPreviewPrepareEnd signed|unsigned|failed   how a preview Prepare run ends
//                                        (failed by default; signed writes a fictional manifest)
//   -SatchelPreviewPrepareSeconds N       length of a preview Prepare run (40 by default)
//   -SatchelPreviewPrepareStart YES       start that run at launch with the first base game
//   -SatchelPreviewMenu YES               game: take a view-shot, open the pause menu,
//                                        then take another (Documents/*-view.png)
//   -SatchelPreviewTouch YES              game: show the touch pad (the simulator's
//                                        virtual controller otherwise hides it)
//   -SatchelPreviewPosture half           game: the host's half-folded test layout
//   -SatchelPreviewHot YES                the session reports a heat frame-rate limit
//   -SatchelPreviewOrientation left|right ask for that landscape orientation (an
//                                        ordinary app request, not a device rotation)
//   -SatchelPreviewFold front|open|book|tall   emulate a screen: front is the real
//                                        simulator window; open and book draw
//                                        the inner screen's 951x669 points scaled
//                                        to fit, fully open or half folded; tall
//                                        is 669x951 half folded (fold across). The
//                                        simulator has no hinge control, so this
//                                        is layout emulation, not a fold event.
@main
struct SatchelPreviewApp: App {
    @UIApplicationDelegateAdaptor(SatchelAppDelegate.self) private var appDelegate
    init() {
        NativePlay.available = UserDefaults.standard.string(forKey: "SatchelPreviewNativePlay") != "NO"
        PreviewSeed.run()
#if PREVIEW_GAME
        if UserDefaults.standard.bool(forKey: "SatchelPreviewPrepared") { DevScreenFixture.seedPreparedGames() }
        NavigationFixFixture.seed()
        OnboardingDLCFixture.seed()
        SaveFixture.seed()
        DiagnosticsFixture.seed()
        SaveLifecycle.start()
#endif
        switch UserDefaults.standard.string(forKey: "SatchelPreviewFold") {
        case "open": ScreenTraits.override = ScreenTraits(posture: .flat)
        case "book", "tall": ScreenTraits.override = ScreenTraits(posture: .book)
        default: break
        }
    }
    var body: some Scene {
        WindowGroup { PreviewRoot().preferredColorScheme(.dark) }
    }
}

#if PREVIEW_GAME
/// Scale the production menu in the same viewport as the synthetic game surface.
struct PreviewMenuScreen: View {
    @ObservedObject var model: InGameMenuModel
    @State private var fold: String = SatchelPreviewCurrentFold()

    var body: some View {
        Group {
            if UserDefaults.standard.string(forKey: "SatchelPreviewFold") == nil {
                InGameMenuScreen(model: model)
            } else {
                GeometryReader { geometry in
                    let inner = fold != "front"
                    let size = inner ? (fold == "tall" ? CGSize(width: 669, height: 951) : CGSize(width: 951, height: 669)) : geometry.size
                    let scale = min(geometry.size.width / size.width, geometry.size.height / size.height)
                    InGameMenuScreen(model: model)
                        .frame(width: size.width, height: size.height)
                        .clipped().scaleEffect(scale)
                        .frame(width: geometry.size.width, height: geometry.size.height)
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: Notification.Name("SatchelPreviewFoldChanged"))) { note in
            if let value = note.object as? String { fold = value }
        }
    }
}
#endif

/// Shows the chosen screen, at the inner screen's size when emulating it.
private struct PreviewRoot: View {
    @StateObject private var library = HomebrewLibrary()
    @StateObject private var artwork = GameArtworkStore()
    private let screen = UserDefaults.standard.string(forKey: "SatchelPreviewScreen") ?? "home"
    @State private var fold = UserDefaults.standard.string(forKey: "SatchelPreviewFold") ?? "front"
    private var inner: Bool { ["open", "book", "tall"].contains(fold) }

    var body: some View {
        root.onAppear(perform: requestOrientation)
#if PREVIEW_GAME
            .onReceive(Timer.publish(every: 2, on: .main, in: .common).autoconnect()) { _ in
                capturePosture()
            }
#endif
            .onReceive(NotificationCenter.default.publisher(for: Notification.Name("SatchelPreviewFoldChanged"))) { note in
                guard let value = note.object as? String else { return }
                fold = value
                ScreenTraits.override = ScreenTraits(posture: ["book", "tall"].contains(value) ? .book : value == "open" ? .flat : .closed)
                // Report through the production reader without rebuilding its view tree.
                func report(_ view: UIView) {
                    (view as? ScreenProbe.ProbeView)?.report()
                    view.subviews.forEach(report)
                }
                for scene in UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }) {
                    scene.windows.forEach(report)
                }
            }
#if PREVIEW_GAME
            .overlay(alignment: .bottomLeading) {
                if UserDefaults.standard.bool(forKey: "SatchelPreviewSessionTests") {
                    TimelineView(.periodic(from: .now, by: 0.1)) { _ in
                        Text(SatchelPreviewSessionState()).font(.system(size: 1))
                            .accessibilityIdentifier("fixture-session-state")
                            .allowsHitTesting(false)
                    }
                }
            }
#endif
#if PREVIEW_GAME
            .onAppear(perform: startPrepare)
            .overlay(alignment: .bottomLeading) {
                if UserDefaults.standard.bool(forKey: "SatchelPreviewFixTests") {
                    TimelineView(.periodic(from: .now, by: 0.2)) { _ in
                        Text(NavigationFixFixture.state()).font(.system(size: 1))
                            .accessibilityIdentifier("fixture-removal-state").allowsHitTesting(false)
                    }
                }
                if UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupTests") {
                    NavigationWarmupControls()
                }
            }
            .overlay(alignment: .topLeading) {
                if UserDefaults.standard.bool(forKey: "SatchelPreviewSaveTests") {
                    TimelineView(.periodic(from: .now, by: 0.2)) { _ in
                        Text(SaveFixture.state()).font(.system(size: 1))
                            .accessibilityIdentifier("fixture-save-state").allowsHitTesting(false)
                    }
                }
            }
#endif
    }

#if PREVIEW_GAME
    /// The Duo simulator's screen capture can crop the landscape framebuffer.
    /// Render the synthetic window at its actual bounds for layout review instead.
    private func capturePosture() {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewPostureTests") else { return }
        // A tapped preview tool can become key. Always sample the content window.
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows).filter { $0.windowLevel == .normal && !$0.isHidden }
        let gameWindow = SatchelNativeSessionStarted() ? SatchelNativeGameController().view.window : nil
        guard let window = gameWindow ?? windows.first(where: \.isKeyWindow) ?? windows.first else { return }
        let format = UIGraphicsImageRendererFormat()
        format.scale = 2
        let image = UIGraphicsImageRenderer(bounds: window.bounds, format: format).image { _ in
            window.drawHierarchy(in: window.bounds, afterScreenUpdates: true)
        }
        let prefix = UserDefaults.standard.bool(forKey: "SatchelPlainNativeMenu") ? "game-plain-" : "game-"
        let state = SatchelNativeSessionStarted() ? prefix + (SatchelMenuState()["state"] as? String ?? "unknown") : screen
        // Compare a clear patch of the fixture image with the composed window. A
        // transparent SwiftUI view can still have an opaque navigation container.
        func picture(in view: UIView) -> UIImageView? {
            if view.accessibilityIdentifier == "fixture-picture" { return view as? UIImageView }
            return view.subviews.lazy.compactMap { picture(in: $0) }.first
        }
        func pixel(_ image: UIImage, at point: CGPoint) -> [UInt8]? {
            guard let patch = image.cgImage?.cropping(to: CGRect(x: point.x * image.scale, y: point.y * image.scale, width: 1, height: 1)) else { return nil }
            var rgba = [UInt8](repeating: 0, count: 4)
            let rendered = rgba.withUnsafeMutableBytes { bytes -> Bool in
                guard let context = CGContext(data: bytes.baseAddress, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                    space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
                context.draw(patch, in: CGRect(x: 0, y: 0, width: 1, height: 1))
                return true
            }
            return rendered ? rgba : nil
        }
        let visibilityState = "\(fold):\(SatchelMenuState()["state"] as? String ?? "unknown")"
        SatchelPreviewSetPictureVisible(false, visibilityState)
        if let view = picture(in: window), let source = view.image {
            let point = view.convert(CGPoint(x: view.bounds.width / 4, y: view.bounds.height / 4), to: window)
            let expected = pixel(source, at: CGPoint(x: source.size.width / 4, y: source.size.height / 4))
            let actual = pixel(image, at: point)
            let visible = expected != nil && actual != nil && zip(expected!, actual!).allSatisfy { abs(Int($0) - Int($1)) <= 14 }
            SatchelPreviewSetPictureVisible(visible, visibilityState)
        }
        let directory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PostureSnapshots", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try? image.pngData()?.write(to: directory.appendingPathComponent("\(fold)-\(state).png"), options: .atomic)
    }

    /// -SatchelPreviewPrepareStart YES: start a preview Prepare run of the first base and its
    /// update at launch, as if chosen on the Prepare screen.
    private func startPrepare() {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewPrepareStart"),
              let base = library.items.first(where: { $0.package != nil && $0.fileExtension == "xci" }) else { return }
        artwork.sync(library.items, fileURL: library.localFile)
        let update = library.items.first { $0.fileExtension == "nsp" && $0.id != base.id && artwork.title($0) == artwork.title(base) }
        PrepareJob.shared.start(base: base, update: update, title: artwork.title(base), library: library, artwork: artwork)
    }
#endif

    private func requestOrientation() {
        guard let wanted = UserDefaults.standard.string(forKey: "SatchelPreviewOrientation"),
              let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene else { return }
        let mask: UIInterfaceOrientationMask = wanted == "left" ? .landscapeLeft : .landscapeRight
        scene.requestGeometryUpdate(.iOS(interfaceOrientations: mask)) { _ in }
    }

    @ViewBuilder private var root: some View {
        if UserDefaults.standard.string(forKey: "SatchelPreviewFold") == nil {
            content
        } else {
            GeometryReader { geometry in
                let size = inner ? (fold == "tall" ? CGSize(width: 669, height: 951) : CGSize(width: 951, height: 669)) : geometry.size
                let scale = min(geometry.size.width / size.width, geometry.size.height / size.height)
                content.frame(width: size.width, height: size.height)
                    .clipped()
                    .scaleEffect(scale)
                    .frame(width: geometry.size.width, height: geometry.size.height)
            }
            .background(Color.black.ignoresSafeArea())
        }
    }

    @ViewBuilder private var content: some View {
        switch screen {
        case "detail":
            let item = library.items.first { $0.fileExtension == "xci" && $0.name.hasPrefix("Lantern") } ?? library.items.first!
            let entry = artwork.shelf(library.items).first { $0.id == item.id }
            GameDetailView(library: library, artwork: artwork, item: item, updates: entry?.updates ?? [],
                           onPlay: {}, onPrepare: {}, onOpenSettings: {})
                .onAppear { artwork.sync(library.items, fileURL: library.localFile) }
        case "import":
            ImportFlowView(library: library, artwork: artwork, openItem: { _ in }, openSettings: {})
                .task { if UserDefaults.standard.bool(forKey: "SatchelPreviewImport") { startSyntheticImport() } }
        case "settings":
            SettingsView(library: library, artwork: artwork)
#if PREVIEW_GAME
        case "compressed-import":
            ImportFlowView(library: library, artwork: artwork, openItem: { _ in }, openSettings: {})
                .overlay(alignment: .bottomLeading) {
                    VStack {
                        Button("Import synthetic package") {
                            let format = UserDefaults.standard.string(forKey: "SatchelPreviewCompressedFormat") ?? "nsz"
                            do {
                                let sources = try CompressedImportFixture.sources(format: format)
                                try ImportCancellationFixture.selected(sources)
                                library.importFiles(sources)
                            }
                            catch { preconditionFailure("Could not create synthetic container: \(error)") }
                        }.accessibilityIdentifier("fixture-import-package")
                        if UserDefaults.standard.bool(forKey: "SatchelPreviewImportCancellation") {
                            Text(ImportCancellationFixture.state(library))
                                .accessibilityIdentifier("fixture-import-cancellation")
                        }
                        Text(CompressedImportFixture.integrity(library))
                            .accessibilityIdentifier("fixture-import-integrity")
                        Text(library.items.map { $0.fileExtension }.joined(separator: ","))
                            .accessibilityIdentifier("fixture-imported-formats")
                    }
                }
        case "game":
            Color.black.ignoresSafeArea()
                .fullScreenCover(isPresented: .constant(true)) { PreviewGameSurface().ignoresSafeArea() }
        case "prepared":
            PreparedGamesView(library: library, artwork: artwork)
                .onAppear { artwork.sync(library.items, fileURL: library.localFile) }
        case "player":
            PlayerSettingsView(titleID: "0100F2C0115B6000", package: "TOTK-1.4.3-2") {}
        case "verifying":
            PreparedVerifyingView(titleID: "0100F2C0115B6000", package: "TOTK-1.4.3-2",
                                  failure: UserDefaults.standard.string(forKey: "SatchelPreviewFailure")) {}
        case "prepare":
            PrepareGameView(library: library, artwork: artwork, play: { _ in })
                .onAppear { artwork.sync(library.items, fileURL: library.localFile) }
#endif
        case "menu":
            ZStack {
                PreviewGameFrame()
                InGameMenuView(model: InGameMenuModel(session: PreviewSession.shared))
            }
        default:
            HomebrewView()
        }
    }

    /// Imports a 3 GB synthetic NSP (valid container header, zero payload, no
    /// game data) so the progress screen can be captured.
    private func startSyntheticImport() {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("Starfall Atlas.nsp")
        try? FileManager.default.removeItem(at: url)
        var header = Data("PFS0".utf8)
        let name = Data("00000000000000000000000000000000.nca".utf8) + Data([0])
        let strings = name + Data(count: (16 - name.count % 16) % 16)
        func le<T: FixedWidthInteger>(_ value: T) -> Data { withUnsafeBytes(of: value.littleEndian) { Data($0) } }
        let size: UInt64 = 3 * 1024 * 1024 * 1024
        header += le(UInt32(1)) + le(UInt32(strings.count)) + le(UInt32(0))
        header += le(UInt64(0)) + le(size) + le(UInt32(0)) + le(UInt32(0)) + strings
        guard FileManager.default.createFile(atPath: url.path, contents: header),
              let handle = try? FileHandle(forWritingTo: url) else { return }
        try? handle.truncate(atOffset: UInt64(header.count) + size)
        try? handle.close()
        library.importFiles([url])
    }
}

/// Fictional paused "game" picture behind the menu, drawn in code.
private struct PreviewGameFrame: View {
    var body: some View {
        ZStack {
            LinearGradient(colors: [Color(red: 0.07, green: 0.10, blue: 0.30), Color(red: 1.0, green: 0.55, blue: 0.2)],
                           startPoint: .top, endPoint: .bottom)
            Circle().fill(.white.opacity(0.85)).frame(width: 120).offset(x: 160, y: -110)
            ForEach(0..<9) { index in
                RoundedRectangle(cornerRadius: 10).fill(Color.orange)
                    .frame(width: 26, height: 34)
                    .offset(x: CGFloat(index * 70 - 280), y: CGFloat((index * 37) % 120))
            }
            Rectangle().fill(Color(red: 0.05, green: 0.08, blue: 0.12)).frame(height: 90).frame(maxHeight: .infinity, alignment: .bottom)
        }
        .ignoresSafeArea()
    }
}

@MainActor final class PreviewSession: InGameSessionControl {
    static let shared = PreviewSession()
    let gameTitle = "Lantern Drift"
    let titleID = "0100ABCD00000000"
    var thermalFrameCap: Int? { UserDefaults.standard.bool(forKey: "SatchelPreviewHot") ? 30 : nil }
    var framesPerSecond: Double? { 29.6 }
    func apply(_ settings: InGameSettings) {}
    func resume() {}
    func pauseAndReturnToLibrary() {}
    func quit() {}
}

/// Stand-in for the engine-bound runtime check (device-only libraries).
struct PackageDiagnostics: View {
    @ObservedObject var library: HomebrewLibrary
    let item: HomebrewItem
    var body: some View {
        SatchelCard(title: "On-phone executable check") {
            Text("Engine diagnostics run on the device build only.")
                .font(.subheadline).foregroundStyle(SatchelStyle.secondary)
        }
    }
}

struct HomebrewPlayerCover: View {
    let url: URL
    let name: String
    let sha256: String
    var body: some View { HomebrewPlayer(url: url, name: name, expectedSHA256: sha256) }
}

struct InterpreterDemoView: View {
    let close: () -> Void
    var body: some View {
        NavigationStack {
            LabView().toolbar { ToolbarItem(placement: .topBarTrailing) { Button("Library") { close() } } }
        }
    }
}

private enum PreviewSeed {
    struct Game {
        let name: String, title: String, publisher: String, version: String, kind: GameInfo.Kind
        let format: SwitchPackageFormat?, gigabytes: Double, hasIcon: Bool, colors: (UIColor, UIColor), motif: Int
        var prepared = false
    }

    static let titleBase: UInt64 = 0x0100_ABCD_0000_0000
    static let games: [Game] = [
        Game(name: "Lantern Drift [Base]", title: "Lantern Drift", publisher: "Hollow Pine Games", version: "1.0.0", kind: .application,
             format: .xci, gigabytes: 11.4, hasIcon: true, colors: (UIColor(red: 0.07, green: 0.10, blue: 0.30, alpha: 1), UIColor(red: 1.0, green: 0.55, blue: 0.20, alpha: 1)), motif: 0, prepared: true),
        Game(name: "Lantern Drift [Update]", title: "Lantern Drift", publisher: "Hollow Pine Games", version: "1.2.0", kind: .update,
             format: .nsp, gigabytes: 1.3, hasIcon: true, colors: (UIColor(red: 0.07, green: 0.10, blue: 0.30, alpha: 1), UIColor(red: 1.0, green: 0.55, blue: 0.20, alpha: 1)), motif: 0),
        Game(name: "Tidebreaker", title: "Tidebreaker", publisher: "Saltwork Studio", version: "2.0.1", kind: .application,
             format: .nsp, gigabytes: 6.8, hasIcon: true, colors: (UIColor(red: 0.0, green: 0.42, blue: 0.62, alpha: 1), UIColor(red: 0.55, green: 0.95, blue: 0.90, alpha: 1)), motif: 1),
        Game(name: "Paper Knights", title: "Paper Knights", publisher: "Foldline", version: "1.0.3", kind: .application,
             format: .xci, gigabytes: 3.2, hasIcon: true, colors: (UIColor(red: 0.95, green: 0.90, blue: 0.80, alpha: 1), UIColor(red: 0.85, green: 0.20, blue: 0.25, alpha: 1)), motif: 2),
        Game(name: "Neon Pastry Panic", title: "Neon Pastry Panic", publisher: "Sugar Rush Labs", version: "1.1.0", kind: .application,
             format: .nsp, gigabytes: 1.9, hasIcon: true, colors: (UIColor(red: 0.35, green: 0.05, blue: 0.45, alpha: 1), UIColor(red: 1.0, green: 0.35, blue: 0.70, alpha: 1)), motif: 3),
        Game(name: "Quiet Orbit", title: "Quiet Orbit", publisher: "Small Moon Co.", version: "1.0.0", kind: .application,
             format: .nsp, gigabytes: 0.8, hasIcon: false, colors: (.black, .white), motif: 0),
        Game(name: "hello-horizon", title: "", publisher: "", version: "", kind: .application,
             format: nil, gigabytes: 0.0001, hasIcon: false, colors: (.black, .white), motif: 0),
    ]

    static func run() {
        let defaults = UserDefaults.standard
        let fm = FileManager.default
        let documents = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let support = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        // A freshly seeded library gets a fresh key prerequisite unless a test
        // explicitly asks for synthetic keys. Relaunches without reseeding keep it.
        let fixtureKeys = defaults.string(forKey: "SatchelPreviewKeys")
            ?? (defaults.string(forKey: "SatchelPreviewDemo") == nil ? nil : "NO")
        if let keys = fixtureKeys {
            let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                        kSecAttrService as String: "com.dell.satchel.console-keys",
                                        kSecAttrAccount as String: "nca-loader-v1"]
            SecItemDelete(query as CFDictionary)
            if keys == "YES" {
                // Dummy all-zero values: enough for the UI to show "Keys ready".
                try? SwitchKeys.save(["header_key": Data(count: 32), "key_area_key_application_00": Data(count: 16),
                                      "titlekek_00": Data(count: 16)])
            }
        }
        guard let mode = defaults.string(forKey: "SatchelPreviewDemo") else { return }
        // The old job belongs to the library being replaced. Do not carry a
        // synthetic failure and its base selection into another fresh journey.
        defaults.removeObject(forKey: "PrepareJobOutcome")
        let library = documents.appendingPathComponent("Homebrew", isDirectory: true)
        let art = support.appendingPathComponent("GameArt", isDirectory: true)
        let prepared = documents.appendingPathComponent("PreparedGames", isDirectory: true)
        for folder in [library, art, prepared] { try? fm.removeItem(at: folder) }
        try? fm.createDirectory(at: library, withIntermediateDirectories: true)
        try? fm.createDirectory(at: art, withIntermediateDirectories: true)
        guard ["library", "prepared-only"].contains(mode) else { return }
        var items: [HomebrewItem] = []
        let now = Date()
        for (index, game) in games.enumerated() {
            let id = String(format: "%064x", index + 0xA11CE)
            let bytes = Int(game.gigabytes * 1_000_000_000)
            let package = game.format.map { SwitchPackageSummary(format: $0, entryCount: 6 + index, contentBytes: UInt64(bytes)) }
            items.append(HomebrewItem(id: id, name: game.name, bytes: bytes, textBytes: 0, roBytes: 0, dataBytes: 0, bssBytes: 0,
                                      importedAt: now.addingTimeInterval(-Double(index) * 3600), package: package))
            guard game.format != nil else { continue }
            let applicationID = titleBase + UInt64(game.title.unicodeScalars.reduce(0) { $0 &* 31 &+ Int($1.value) } & 0xFFFF) << 16
            var accent: [Double]?
            if game.hasIcon, let jpeg = placeholderArt(game).jpegData(compressionQuality: 0.9) {
                try? jpeg.write(to: art.appendingPathComponent(id + ".jpg"))
                accent = GameInfoReader.accent(of: jpeg)
            }
            var info = GameInfo(applicationID: applicationID, kind: game.kind, version: game.kind == .update ? 0x20000 : 0,
                                displayVersion: game.version, title: game.title, publisher: game.publisher,
                                hasIcon: game.hasIcon, accent: accent, addOns: [])
            if game.prepared {
                info.extras = [GameInfo.Extra(titleID: applicationID + 0x2000, title: "Lantern Sound Gallery")]
            }
            try? JSONEncoder().encode(info).write(to: art.appendingPathComponent(id + ".json"))
            if game.prepared {
                if defaults.bool(forKey: "SatchelPreviewOriginalRemoved") {
                    items[items.count - 1].preparedSource = PreparedImportSource(titleID: applicationID,
                        package: "LanternDrift-1.2.0", isUpdate: false)
                }
                let folder = prepared.appendingPathComponent("LanternDrift-1.2.0", isDirectory: true)
                try? fm.createDirectory(at: folder, withIntermediateDirectories: true)
                let manifest: [String: Any] = ["titleID": applicationID.titleIDString, "version": "1.2.0",
                    "addOnContent": [["titleID": (applicationID + 0x1001).titleIDString, "applicationID": applicationID.titleIDString,
                        "version": 1, "requiredApplicationVersion": 0, "index": 1, "path": "aoc/1.img",
                        "bytes": 1024, "sha256": String(repeating: "0", count: 64)]]]
                try? JSONSerialization.data(withJSONObject: manifest).write(to: folder.appendingPathComponent("manifest.json"))
            }
        }
        // Navigation fixture: a prepared package with no imported library record and
        // artwork cached only by title ID. Manifests and artwork are entirely synthetic.
        if defaults.bool(forKey: "SatchelPreviewPreparedOnlyTile") || mode == "prepared-only" {
            let game = games[0]
            let titleID: UInt64 = 0x0100ABCD12340000
            let folder = prepared.appendingPathComponent("Navigation-Orphan", isDirectory: true)
            try? fm.createDirectory(at: folder, withIntermediateDirectories: true)
            let manifest = ["titleID": titleID.titleIDString, "version": "1.0.0"]
            try? JSONSerialization.data(withJSONObject: manifest).write(to: folder.appendingPathComponent("manifest.json"))
            let info = GameInfo(applicationID: titleID, kind: .application, version: 0,
                                displayVersion: "1.0.0", title: "Navigation Meadow", publisher: "Fixture",
                                hasIcon: true, accent: nil, addOns: [])
            let name = "title-" + titleID.titleIDString
            try? JSONEncoder().encode(info).write(to: art.appendingPathComponent(name + ".json"))
            try? placeholderArt(game).jpegData(compressionQuality: 0.9)?.write(to: art.appendingPathComponent(name + ".jpg"))
        }
        if mode == "library" {
            let id = String(format: "%064x", 0xD1C)
            items.append(HomebrewItem(id: id, name: "Lantern DLC", bytes: 1024, textBytes: 0, roBytes: 0,
                dataBytes: 0, bssBytes: 0, importedAt: now,
                package: SwitchPackageSummary(format: .nsp, entryCount: 1, contentBytes: 1024)))
            let base = try! JSONDecoder().decode(GameInfo.self,
                from: Data(contentsOf: art.appendingPathComponent(String(format: "%064x", 0xA11CE) + ".json")))
            let row: [String: Any] = ["titleID": base.applicationID + 0x1001, "applicationID": base.applicationID,
                "version": 1, "requiredApplicationVersion": 0, "bytes": 1024]
            let dlc = try! JSONDecoder().decode(SwitchAddOnInfo.self, from: JSONSerialization.data(withJSONObject: row))
            let info = GameInfo(applicationID: base.applicationID, kind: .addOnContent, version: 1,
                displayVersion: "1", title: "Lantern DLC", publisher: "Fixture", hasIcon: false, accent: nil,
                addOns: [dlc])
            try? JSONEncoder().encode(info).write(to: art.appendingPathComponent(id + ".json"))
        }
        if mode == "prepared-only" { items = [] }
        try? JSONEncoder().encode(items).write(to: library.appendingPathComponent("library.json"))
    }

    /// Abstract placeholder cover drawn in code. Not derived from any game.
    static func placeholderArt(_ game: Game) -> UIImage {
        let size = CGSize(width: 256, height: 256)
        return UIGraphicsImageRenderer(size: size).image { context in
            let cg = context.cgContext
            let colors = [game.colors.0.cgColor, game.colors.1.cgColor] as CFArray
            let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 1])!
            cg.drawLinearGradient(gradient, start: .zero, end: CGPoint(x: 256, y: 256), options: [])
            UIColor.white.withAlphaComponent(0.85).setFill()
            switch game.motif {
            case 0: // moon and floating lanterns
                UIBezierPath(ovalIn: CGRect(x: 150, y: 28, width: 70, height: 70)).fill()
                for (x, y, s) in [(40.0, 150.0, 34.0), (96.0, 120.0, 26.0), (150.0, 170.0, 30.0), (200.0, 135.0, 20.0)] {
                    game.colors.1.withAlphaComponent(0.95).setFill()
                    UIBezierPath(roundedRect: CGRect(x: x, y: y, width: s, height: s * 1.3), cornerRadius: s * 0.35).fill()
                }
            case 1: // waves
                for row in 0..<4 {
                    let path = UIBezierPath()
                    let base = 120 + Double(row) * 32
                    path.move(to: CGPoint(x: 0, y: base))
                    for x in stride(from: 0.0, through: 256, by: 32) {
                        path.addQuadCurve(to: CGPoint(x: x + 32, y: base), controlPoint: CGPoint(x: x + 16, y: base - 22))
                    }
                    path.addLine(to: CGPoint(x: 256, y: 256)); path.addLine(to: CGPoint(x: 0, y: 256)); path.close()
                    UIColor.white.withAlphaComponent(0.18 + Double(row) * 0.1).setFill(); path.fill()
                }
            case 2: // folded paper shield
                let shield = UIBezierPath()
                shield.move(to: CGPoint(x: 128, y: 40)); shield.addLine(to: CGPoint(x: 205, y: 70))
                shield.addLine(to: CGPoint(x: 190, y: 170)); shield.addLine(to: CGPoint(x: 128, y: 220))
                shield.addLine(to: CGPoint(x: 66, y: 170)); shield.addLine(to: CGPoint(x: 51, y: 70)); shield.close()
                game.colors.1.setFill(); shield.fill()
                UIColor.white.withAlphaComponent(0.35).setFill()
                let fold = UIBezierPath(); fold.move(to: CGPoint(x: 128, y: 40)); fold.addLine(to: CGPoint(x: 128, y: 220))
                fold.addLine(to: CGPoint(x: 66, y: 170)); fold.addLine(to: CGPoint(x: 51, y: 70)); fold.close(); fold.fill()
            default: // sprinkles and a ring
                UIColor.white.withAlphaComponent(0.9).setStroke()
                let ring = UIBezierPath(ovalIn: CGRect(x: 58, y: 58, width: 140, height: 140)); ring.lineWidth = 34; ring.stroke()
                for i in 0..<18 {
                    let angle = Double(i) / 18 * .pi * 2
                    [UIColor.yellow, .cyan, .white][i % 3].setFill()
                    UIBezierPath(roundedRect: CGRect(x: 128 + cos(angle) * 70 - 3, y: 128 + sin(angle) * 70 - 8, width: 6, height: 16), cornerRadius: 3).fill()
                }
            }
            let style = NSMutableParagraphStyle(); style.alignment = .center
            let text = NSAttributedString(string: game.title.uppercased(), attributes: [
                .font: UIFont.systemFont(ofSize: 22, weight: .black), .foregroundColor: UIColor.white,
                .paragraphStyle: style, .kern: 1.5])
            text.draw(in: CGRect(x: 10, y: 206, width: 236, height: 40))
        }
    }
}

#if !SATCHEL_UI_PREVIEW_MINIMAL && !PREVIEW_GAME
// Compile check for the dev hooks in HomebrewView (build-preview.zsh dev). The game preview
// compiles the real views instead (DevScreenFixture.swift stubs their device libraries).
struct PreparedGamesView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    var launch: String? = nil
    var body: some View { Text("Prepared games (dev stub)") }
}

struct PrepareJobSlot: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    let open: () -> Void
    let play: (String) -> Void
    let openWarmup: (String) -> Void
    var body: some View { EmptyView() }
}

func SatchelNativeSessionStarted() -> Bool { false }

extension Notification.Name {
    static let SatchelMenuReturnToGame = Notification.Name("SatchelMenuReturnToGame")
}

struct PrepareGameView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    let play: (String) -> Void
    var body: some View { Text("Prepare game (dev stub)") }
}
#endif

#if PREVIEW_GAME
/// The game screen as the dev app shows it: the native game controller inside a full-screen
/// cover, with Satchel's menu installed (NativeHost/PreparedGame.swift NativeGameSurface).
private struct PreviewGameSurface: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> UIViewController {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        FileManager.default.changeCurrentDirectoryPath(documents.path) // view-shots land here
        let controller = SatchelNativeGameController()
        SatchelMenuSurfaceAttached(true)
        SatchelMenu.install(titleID: "0100F2C0115B6000")
        let defaults = UserDefaults.standard
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            if defaults.bool(forKey: "SatchelPreviewTouch") {
                for pad in GCController.controllers() {
                    NotificationCenter.default.post(name: .GCControllerDidDisconnect, object: pad)
                }
            }
            if defaults.string(forKey: "SatchelPreviewPosture") == "half" { satchel_display_command("posture half") }
        }
        if defaults.bool(forKey: "SatchelPreviewMenu") {
            // view-shot runs on the next main-queue turn, so open the menu after it.
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { satchel_display_command("view-shot game-view.png") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { SatchelMenuPerform("open", nil) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { satchel_display_command("view-shot menu-view.png") }
        }
        return controller
    }
    func updateUIViewController(_ controller: UIViewController, context: Context) {}
}
#endif
