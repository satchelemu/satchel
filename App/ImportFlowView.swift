import SwiftUI
import UniformTypeIdentifiers

/// "Add games" screen: what can be imported, live progress, and what happens next.
struct ImportFlowView: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    var openItem: (HomebrewItem) -> Void
    var openSettings: () -> Void
    @State private var picking = false
    @State private var pickerError: String?
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        PanelChrome(closeTitle: "Close", identifier: "close-import", close: { dismiss() }) {
            ScreenReader { cut, geometry in
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: the illustration on the first page, the flow on the second.
                        FoldSplit(fold: fold, cut: cut) {
                            illustration.frame(maxWidth: .infinity, maxHeight: .infinity)
                        } second: {
                            flowScroll(minHeight: fold.pages(in: geometry.size).second.height - 22, reserve: fold.isVertical ? reserve : 0)
                        }
                    } else {
                        HStack(alignment: .center, spacing: geometry.size.width < 760 ? 16 : 36) {
                            illustration
                                .scaleEffect(min(1, geometry.size.width / 950))
                                .frame(width: min(250, geometry.size.width * 0.28))
                            flowScroll(minHeight: geometry.size.height, reserve: reserve)
                        }
                        .cutoutPadding(cut, leading: 28, trailing: 28)
                    }
                }
            }
            .background { SatchelBackdrop(glow: SatchelTheme.tide) }
            .satchelMenuStatus()
        }
        // B closes this screen with a controller; the Files picker itself is system UI.
        .controllerNavigation(enabled: !picking, back: { dismiss() })
        // The + entry points clear the last result; the home card's Details opens this screen
        // with the result still showing.
        .onChange(of: library.lastOutcome) { _, outcome in
            switch outcome {
            case .added: Haptics.success()
            case .failed: Haptics.warning()
            default: break
            }
        }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.data, .folder], allowsMultipleSelection: true) { result in
            switch result {
            case .success(let urls): if !urls.isEmpty { library.importFiles(urls) }
            case .failure(let error): pickerError = error.localizedDescription
            }
        }
    }

    private func flowScroll(minHeight: CGFloat, reserve: CGFloat) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                content
            }
            .padding(.vertical, 24).padding(.trailing, reserve)
            .frame(minHeight: minHeight, alignment: .center)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
    }

    @ViewBuilder private var content: some View {
        if library.importing {
            progressCard
        } else if let outcome = library.lastOutcome {
            outcomeCard(outcome)
        } else {
            intro
        }
    }

    // MARK: Idle

    private var intro: some View {
        VStack(alignment: .leading, spacing: 16) {
            VStack(alignment: .leading, spacing: 6) {
                SatchelEyebrow(text: "Add games", tint: SatchelTheme.tide)
                Text("Bring your games over").font(SatchelTheme.display(26))
                Text("Pick files from iCloud Drive, On My iPhone or any Files location. Choose a split dump folder or select all of its parts together. You can choose several games at once.")
                    .font(.system(size: 14, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            VStack(spacing: 0) {
                kind("Base game", "XCI, NSP, XCZ or NSZ", "gamecontroller.fill", SatchelTheme.ember)
                Divider().overlay(SatchelTheme.line).padding(.leading, 62)
                kind("Update", "NSP or NSZ, attaches to its game automatically", "arrow.up.circle.fill", SatchelTheme.violet)
                Divider().overlay(SatchelTheme.line).padding(.leading, 62)
                kind("Homebrew", "NRO, plays right away", "sparkles", SatchelTheme.leaf)
            }
            .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(SatchelTheme.line) }
            if !artwork.keysAvailable {
                Button(action: openSettings) {
                    HStack(spacing: 12) {
                        Image(systemName: "key.fill").foregroundStyle(SatchelTheme.spark)
                        Text("Import your prod.keys too, so \(AppBrand.name) can show each game's title and artwork.")
                            .font(.system(size: 13, weight: .semibold, design: .rounded)).multilineTextAlignment(.leading)
                        Spacer(minLength: 0)
                        Image(systemName: "chevron.right").font(.system(size: 12, weight: .bold))
                    }
                    .foregroundStyle(SatchelTheme.text)
                    .padding(14)
                    .background(SatchelTheme.spark.opacity(0.12), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                }.buttonStyle(PressableStyle(scale: 0.98)).accessibilityIdentifier("import-open-settings")
            }
            VStack(alignment: .leading, spacing: 8) {
                Button { picking = true } label: {
                    SatchelButtonLabel(title: "Choose from Files", systemImage: "folder.fill", kind: .accent(SatchelTheme.ember), height: 52)
                        .fixedSize()
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("choose-files")
                .controllerAction("import-choose-files", style: .button(.capsule)) { picking = true }
                Text("Big games take a few minutes to copy. The import keeps going in the background if you switch apps.")
                    .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let pickerError {
                Text(pickerError).font(.footnote).foregroundStyle(SatchelTheme.ember)
            }
        }
    }

    private func kind(_ title: String, _ detail: String, _ icon: String, _ tint: Color) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon).font(.system(size: 17, weight: .bold)).foregroundStyle(tint)
                .frame(width: 36, height: 36).background(tint.opacity(0.15), in: RoundedRectangle(cornerRadius: 11, style: .continuous))
            VStack(alignment: .leading, spacing: 1) {
                Text(title).font(.system(size: 15, weight: .bold, design: .rounded))
                Text(detail).font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 14).padding(.vertical, 10)
    }

    // MARK: Progress

    private var stage: (title: String, detail: String) {
        guard let progress = library.importProgress else {
            if let download = library.downloadFraction {
                return ("Downloading from iCloud · \(Int((download * 100).rounded(.down)))%",
                        "iCloud Drive is downloading the file to this iPhone. \(AppBrand.name) copies it as soon as it arrives.")
            }
            return ("Waiting for Files", "Items stored only in iCloud download first, which can take a while on a slow connection.")
        }
        if progress >= 1 { return ("Checking the package", "Making sure the copy is complete and the package is intact.") }
        return ("Copying to this iPhone", "\(AppBrand.name) copies the game into its own library. Your original stays where it is.")
    }

    private var progressCard: some View {
        VStack(alignment: .leading, spacing: 16) {
            SatchelEyebrow(text: library.queuedImports > 0 ? "Importing · \(library.queuedImports) more waiting" : "Importing", tint: SatchelTheme.tide)
            Text(library.importName ?? "Your file").font(SatchelTheme.display(26)).lineLimit(2)
            VStack(alignment: .leading, spacing: 8) {
                SatchelProgressBar(value: library.importProgress ?? library.downloadFraction, tint: SatchelTheme.tide, height: 16)
                    .accessibilityIdentifier("import-progress")
                Text(stage.title).font(.system(size: 15, weight: .bold, design: .rounded))
                    .accessibilityIdentifier("import-stage")
                if let progress = library.importProgress {
                    Text(progressText(progress)).font(SatchelTheme.mono(12)).foregroundStyle(SatchelTheme.tide)
                } else if let started = library.importStartedAt {
                    // Elapsed time while Files works, so a stalled download is visible.
                    TimelineView(.periodic(from: .now, by: 1)) { context in
                        Text("\(importDuration(context.date.timeIntervalSince(started))) so far")
                            .font(SatchelTheme.mono(12)).monospacedDigit().foregroundStyle(SatchelTheme.tide)
                    }
                    .accessibilityIdentifier("import-elapsed")
                }
                Text(stage.detail).font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
            }
            .padding(18)
            .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: 20, style: .continuous).strokeBorder(SatchelTheme.line) }
            VStack(alignment: .leading, spacing: 8) {
                Button { library.cancelImport() } label: {
                    SatchelButtonLabel(title: "Cancel import", systemImage: "xmark", kind: .secondary, height: 44).fixedSize()
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("cancel-import")
                Text("You can close this screen or switch apps; the import keeps going in the background.")
                    .font(.system(size: 12, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            }
        }
    }

    private func importDuration(_ seconds: Double) -> String {
        let value = max(0, Int(seconds))
        return value < 60 ? "\(value)s" : "\(value / 60)m \(value % 60)s"
    }

    private func progressText(_ progress: Double) -> String {
        let percent = Int((progress * 100).rounded(.down))
        guard let total = library.importBytes else { return "\(percent)%" }
        return "\(satchelBytes(Int64(Double(total) * progress))) of \(satchelBytes(Int64(total)))  ·  \(percent)%"
    }

    // MARK: Results

    @ViewBuilder private func outcomeCard(_ outcome: ImportOutcome) -> some View {
        switch outcome {
        case .added(let id, let duplicate):
            if let item = library.items.first(where: { $0.id == id }) {
                addedCard(item, duplicate: duplicate)
            } else {
                intro
            }
        case .failed(let name, let message):
            problemCard(title: "Couldn't add \(name)", message: message, tint: SatchelTheme.ember,
                        tips: tips(for: message))
        case .cancelled(let name):
            problemCard(title: "Stopped importing \(name)", message: "Nothing was added and your original file is unchanged.",
                        tint: SatchelTheme.textDim, tips: [])
        }
    }

    private func addedCard(_ item: HomebrewItem, duplicate: Bool) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            SatchelEyebrow(text: duplicate ? "Already in your library" : "Added to your library", tint: SatchelTheme.leaf)
            HStack(alignment: .center, spacing: 16) {
                GameTileArt(item: item, title: artwork.title(item), image: artwork.image(item), side: 96)
                    .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
                    .shadow(color: artwork.glow(item).opacity(0.5), radius: 14, y: 6)
                VStack(alignment: .leading, spacing: 6) {
                    Text(artwork.title(item)).font(SatchelTheme.display(24)).lineLimit(2)
                    Text(artwork.subtitle(item)).font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(SatchelTheme.textDim).lineLimit(1)
                    StateBadge(state: artwork.state(item))
                }
            }
            Text(nextStep(item)).font(.system(size: 14, weight: .medium, design: .rounded))
                .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 12) {
                Button { openItem(item) } label: {
                    SatchelButtonLabel(title: "Open game", systemImage: "arrow.right", kind: .primary, height: 48)
                }.buttonStyle(PressableStyle()).accessibilityIdentifier("open-imported")
                Button { library.clearOutcome(); picking = true } label: {
                    SatchelButtonLabel(title: "Add more", systemImage: "plus", kind: .secondary, height: 48)
                }.buttonStyle(PressableStyle())
            }
        }
    }

    private func nextStep(_ item: HomebrewItem) -> String {
        if item.package == nil { return "Homebrew is ready right away. Open it and press Play." }
        switch artwork.state(item) {
        case .needsKeys: return "Next: import your prod.keys in Settings so \(AppBrand.name) can read this game's title, artwork and code."
        case .readingInfo: return "Reading the title and artwork from the game itself…"
        case .update: return "Updates attach to their base game automatically. Import the base game too if you haven't yet."
        case .ready: return "This game is prepared and ready to play."
        default:
            if NativePlay.available {
                return "Next: add its update if you have one, then prepare the game on this iPhone."
            }
            return "Your game is stored on this iPhone. Add its update now if you have one; preparing games on the phone arrives in a future update."
        }
    }

    private func problemCard(title: String, message: String, tint: Color, tips: [String]) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            Image(systemName: tint == SatchelTheme.ember ? "exclamationmark.triangle.fill" : "xmark.circle.fill")
                .font(.system(size: 30, weight: .bold)).foregroundStyle(tint)
            Text(title).font(SatchelTheme.display(24)).lineLimit(2)
            Text(message).font(.system(size: 14, weight: .medium, design: .rounded))
                .foregroundStyle(SatchelTheme.text).fixedSize(horizontal: false, vertical: true)
            ForEach(tips, id: \.self) { tip in
                Label(tip, systemImage: "lightbulb.fill").font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundStyle(SatchelTheme.textDim).fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 12) {
                Button { library.clearOutcome(); picking = true } label: {
                    SatchelButtonLabel(title: "Choose another file", systemImage: "folder.fill", kind: .primary, height: 48)
                }.buttonStyle(PressableStyle())
                Button { library.clearOutcome() } label: {
                    SatchelButtonLabel(title: "Back", kind: .secondary, height: 48)
                }.buttonStyle(PressableStyle())
            }
        }
    }

    private func tips(for message: String) -> [String] {
        let text = message.lowercased()
        if text.contains("free space") { return ["Remove games you no longer play from the library, or free space in iPhone Settings > General > iPhone Storage."] }
        if text.contains("not a supported") || text.contains("homebrew") || text.contains("format") {
            return ["\(AppBrand.name) accepts XCI, NSP, XCZ and NSZ game packages and NRO homebrew. Other formats, archives (ZIP, RAR) and save files can't be imported."]
        }
        if text.contains("download") || text.contains("provider") || text.contains("ended before") || text.contains("changed size") {
            return ["Open the file's folder in the Files app and wait for the download to finish, then try again."]
        }
        return ["If the file came from a dump tool, try dumping it again; the copy may be incomplete."]
    }

    // MARK: Illustration

    private var illustration: some View {
        ZStack {
            Circle().fill(SatchelTheme.tide.opacity(0.10)).frame(width: 230, height: 230)
            card(SatchelTheme.violet, "arrow.up.circle.fill").rotationEffect(.degrees(-16)).offset(x: -58, y: 14)
            card(SatchelTheme.leaf, "sparkles").rotationEffect(.degrees(14)).offset(x: 58, y: 18)
            card(SatchelTheme.ember, library.importing ? "arrow.down" : "plus").offset(y: -8)
                .scaleEffect(library.importing ? 1.06 : 1)
                .animation(library.importing ? .easeInOut(duration: 0.8).repeatForever(autoreverses: true) : SatchelTheme.snap,
                           value: library.importing)
        }
        .accessibilityHidden(true)
    }

    private func card(_ tint: Color, _ icon: String) -> some View {
        RoundedRectangle(cornerRadius: 22, style: .continuous).fill(tint.gradient)
            .frame(width: 112, height: 140)
            .overlay {
                VStack {
                    HStack { Capsule().fill(.white.opacity(0.35)).frame(width: 30, height: 6); Spacer() }
                    Spacer()
                    Image(systemName: icon).font(.system(size: 38, weight: .heavy)).foregroundStyle(.white)
                    Spacer()
                }.padding(14)
            }
            .shadow(color: tint.opacity(0.45), radius: 18, y: 8)
    }
}
