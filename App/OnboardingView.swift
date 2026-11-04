import SwiftUI
import UniformTypeIdentifiers

/// Uses the same secure key import and game importer as Settings and the library.
struct OnboardingView: View {
    static let finishedKey = "SatchelOnboardingFinished"
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    let finish: () -> Void
    let prepare: ((String) -> Void)?
    @StateObject private var keys = KeyStore()
    @State private var step = 0
    @State private var pickingKeys = false
    @State private var pickingGame = false
    @State private var pickerError: String?
    @State private var importedID: String?
    @State private var showingInstallGuide = false

    private var game: HomebrewItem? {
        let candidates = library.items.filter { $0.package != nil && library.hasOriginal($0) && artwork.info($0)?.kind != .update && artwork.info($0)?.kind != .addOnContent }
        return candidates.first { $0.id == importedID } ?? candidates.first
    }

    var body: some View {
        PanelChrome(closeTitle: "Skip for now", identifier: "onboarding-skip", close: finish) {
            ScreenReader { cut, _ in
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        HStack(spacing: 12) {
                            BrandMark(size: 32)
                            SatchelEyebrow(text: "Getting started · \(step + 1) of 4", tint: SatchelTheme.tide)
                        }
                        Text(["Welcome to Satchel", "Bring your console keys", "Import your first game", "Prepare it on this iPhone"][step])
                            .font(SatchelTheme.display(28)).accessibilityIdentifier("onboarding-title")
                        SatchelPanel {
                            content
                        }
                        HStack(spacing: 12) {
                            if step > 0 {
                                Button { step -= 1 } label: { SatchelButtonLabel(title: "Back", kind: .secondary, height: 42) }
                                    .accessibilityIdentifier("onboarding-back")
                            }
                            if step < 3 {
                                Button { step += 1 } label: { SatchelButtonLabel(title: "Continue", systemImage: "arrow.right", height: 42) }
                                    .disabled((step == 1 && !keys.available) || (step == 2 && (game == nil || game.flatMap(artwork.info)?.kind != .application)) || keys.busy || library.importing)
                                    .accessibilityIdentifier("onboarding-next")
                            }
                        }.buttonStyle(PressableStyle())
                        Text("You can return to this guide in Settings > About.")
                            .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                        Button("Install and refresh help") { showingInstallGuide = true }
                            .accessibilityIdentifier("onboarding-install-help")
                    }
                    .frame(maxWidth: 680, alignment: .leading).frame(maxWidth: .infinity)
                    .padding(.vertical, 18).padding(.trailing, cut.bar == .top ? 56 : 0)
                    .cutoutPadding(cut, leading: 20, trailing: 20)
                }.scrollIndicators(.hidden)
            }
            .background { SatchelBackdrop(glow: SatchelTheme.tide) }.satchelMenuStatus()
        }
        .onAppear { library.importScreenVisible = true; library.clearOutcome() }
        .sheet(isPresented: $showingInstallGuide) {
            InstallGuideView { showingInstallGuide = false }
        }
        .onDisappear { library.importScreenVisible = false }
        .onChange(of: library.lastOutcome) { _, result in
            if case .added(let id, _) = result { importedID = id }
        }
        .fileImporter(isPresented: $pickingKeys, allowedContentTypes: [.data]) { result in
            switch result {
            case .success(let url): keys.importKeys(url)
            case .failure: keys.error = "The key file could not be opened. Download it fully in Files and try again."
            }
        }
        .fileImporter(isPresented: $pickingGame, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls): if let url = urls.first { library.importFile(url) }
            case .failure: pickerError = "The game could not be opened. Download it fully in Files and try again."
            }
        }
    }

    @ViewBuilder private var content: some View {
        switch step {
        case 0:
            copy("Satchel prepares and plays your own Switch game dumps entirely on this iPhone. Bring your own prod.keys and games from Files or iCloud Drive.")
            copy("First, add your keys. Then import a game and prepare it. Some games may still need features Satchel does not support.")
        case 1:
            copy(keys.available ? "Your keys are set up. You can continue or replace them." : "Choose prod.keys exported from your own console. Satchel validates its format and keeps only the keys it needs in this iPhone's secure storage. The file is never copied into the library or included in reports.")
            copy("A valid file may still be missing keys for a newer game. Satchel checks those when it reads the game.")
            Button { pickingKeys = true } label: {
                SatchelButtonLabel(title: keys.available ? "Replace prod.keys" : "Import prod.keys", systemImage: "key.fill", kind: .accent(SatchelTheme.ember), height: 44)
            }.buttonStyle(PressableStyle()).disabled(keys.busy).accessibilityIdentifier("onboarding-import-keys")
            if keys.busy { ProgressView("Checking keys…") }
            if let text = keys.message { copy(text) }
            if let text = keys.error { problem(text) }
        case 2:
            copy("Choose your base game: XCI, NSP, XCZ or NSZ. It is copied into Satchel; your original in Files stays unchanged. You can add updates later from the library.")
            if library.importing {
                SatchelProgressBar(value: library.importProgress, tint: SatchelTheme.tide, height: 10)
                copy(library.notice ?? "Importing your game…")
                Button("Cancel import") { library.cancelImport() }.accessibilityIdentifier("onboarding-cancel-import")
            } else {
                Button { pickerError = nil; library.clearOutcome(); pickingGame = true } label: {
                    SatchelButtonLabel(title: "Choose game", systemImage: "folder.fill", kind: .accent(SatchelTheme.ember), height: 44)
                }.buttonStyle(PressableStyle()).accessibilityIdentifier("onboarding-import-game")
            }
            if let game {
                copy("In your library: " + artwork.title(game))
                if case .failed(let reason) = artwork.status[game.id] { problem(reason) }
                else if artwork.info(game) == nil { copy("Reading the game information. If keys are needed, use Back to replace prod.keys.") }
            }
            if case .failed(_, let message) = library.lastOutcome { problem(message) }
            if case .added(let id, _) = library.lastOutcome, let item = library.items.first(where: { $0.id == id }), game == nil {
                copy(item.package == nil ? "Homebrew is ready from the library. For this guide, choose a base Switch game." : "This is extra content. Choose its base game to prepare it.")
            }
            if let pickerError { problem(pickerError) }
        default:
            copy("Prepare turns your imported game into a signed version that can run on this iPhone. In the next screen, add your Apple Development signing identity and select your game. Enter its passphrase only on the phone.")
            copy("Preparation needs extra free space and can take a while. You can close the Prepare screen while it works. When verification finishes, Play appears in your library.")
            if let game, let prepare {
                Button { prepare(game.id) } label: {
                    SatchelButtonLabel(title: "Open Prepare", systemImage: "hammer.fill", kind: .accent(SatchelTheme.ember), height: 44)
                }.buttonStyle(PressableStyle()).accessibilityIdentifier("onboarding-prepare")
            } else {
                copy("Import a base game first, then open Prepare from Workshop in the library.")
            }
            Button("Return to library", action: finish).accessibilityIdentifier("onboarding-finish")
        }
    }

    private func copy(_ text: String) -> some View {
        Text(text).font(.system(size: 14, weight: .medium, design: .rounded))
            .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
    }
    private func problem(_ text: String) -> some View {
        Label(text, systemImage: "exclamationmark.triangle.fill")
            .font(.system(size: 14, weight: .semibold, design: .rounded))
            .foregroundStyle(SatchelTheme.spark).fixedSize(horizontal: false, vertical: true)
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
