import SwiftUI

/// The home screen's view of the import queue, built like the Prepare card: live progress
/// while a file imports, then its result until dismissed. It observes the library itself
/// so progress updates redraw only this card, not the shelf.
struct ImportJobCard: View {
    @ObservedObject var library: HomebrewLibrary
    @ObservedObject var artwork: GameArtworkStore
    /// Opens a library item's game page.
    let open: (String) -> Void
    /// Opens the import screen (live progress, or the full result with its tips).
    let details: () -> Void

    var body: some View {
        if library.importing {
            card(tint: SatchelTheme.tide) { progress }
        } else if let outcome = library.lastOutcome {
            card(tint: tint(outcome)) { result(outcome) }
        }
    }

    private func tint(_ outcome: ImportOutcome) -> Color {
        switch outcome {
        case .added: return SatchelTheme.leaf
        case .failed: return SatchelTheme.ember
        case .cancelled: return SatchelTheme.textDim
        }
    }

    private func card<Content: View>(tint: Color, @ViewBuilder _ content: () -> Content) -> some View {
        HStack(spacing: 10) { content() }
            .padding(.leading, 7).padding(.trailing, 10)
            .frame(height: 52)
            .frame(minWidth: 210, maxWidth: 360, alignment: .leading)
            .background(SatchelTheme.surface.opacity(0.94), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(tint.opacity(0.45)) }
            .transition(.move(edge: .top).combined(with: .opacity))
    }

    private func icon(_ systemImage: String, tint: Color, art: UIImage? = nil, badge: String? = nil) -> some View {
        ZStack(alignment: .bottomTrailing) {
            Group {
                if let art { Image(uiImage: art).resizable().scaledToFill() }
                else { Image(systemName: systemImage).font(.system(size: 15, weight: .bold)).foregroundStyle(tint)
                        .frame(maxWidth: .infinity, maxHeight: .infinity).background(tint.opacity(0.16)) }
            }
            .frame(width: 38, height: 38)
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            if let badge {
                Image(systemName: badge).font(.system(size: 11, weight: .black)).foregroundStyle(SatchelTheme.shell)
                    .frame(width: 18, height: 18).background(tint, in: Circle())
                    .overlay { Circle().strokeBorder(SatchelTheme.surface, lineWidth: 2) }
                    .offset(x: 5, y: 5)
            }
        }
        .accessibilityHidden(true)
    }

    // MARK: Running

    private var fraction: Double? { library.importProgress ?? library.downloadFraction }

    private var stage: String {
        if let progress = library.importProgress { return progress >= 1 ? "Checking the package" : "Copying to this iPhone" }
        if library.downloadFraction != nil { return "Downloading from iCloud" }
        return "Waiting for Files"
    }

    private var progress: some View {
        Button { Haptics.click(); details() } label: {
            HStack(spacing: 10) {
                icon("tray.and.arrow.down.fill", tint: SatchelTheme.tide)
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 6) {
                        Text(library.importName ?? "Importing").font(.system(size: 13, weight: .bold, design: .rounded))
                            .foregroundStyle(SatchelTheme.text).lineLimit(1)
                        Spacer(minLength: 4)
                        if let fraction {
                            Text("\(Int((fraction * 100).rounded(.down)))%").font(SatchelTheme.mono(12)).monospacedDigit()
                                .foregroundStyle(SatchelTheme.text)
                        }
                    }
                    HStack(spacing: 6) {
                        Text(stage).lineLimit(1)
                        Spacer(minLength: 4)
                        if library.queuedImports > 0 {
                            Text("\(library.queuedImports) more waiting").lineLimit(1).fixedSize()
                        }
                    }
                    .font(.system(size: 11, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                    SatchelProgressBar(value: fraction, tint: SatchelTheme.tide, height: 4)
                }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("Importing \(library.importName ?? "a file"), \(stage)" + (fraction.map { ", \(Int($0 * 100)) percent" } ?? ""))
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
        .accessibilityHint("Opens the import screen")
        .accessibilityIdentifier("import-job-card")
    }

    // MARK: Result

    @ViewBuilder private func result(_ outcome: ImportOutcome) -> some View {
        switch outcome {
        case .added(let id, let duplicate):
            let item = library.items.first { $0.id == id }
            let title = item.map(artwork.title) ?? "Your game"
            Button { Haptics.click(); if item != nil { open(id) } else { details() } } label: {
                HStack(spacing: 10) {
                    icon("checkmark", tint: SatchelTheme.leaf, art: item.flatMap(artwork.image), badge: "checkmark")
                    summary(duplicate ? "Already in your library" : "Added", detail: title, lines: 1)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
            .accessibilityHint("Opens the game page")
            .accessibilityIdentifier("import-job-card")
            if item != nil {
                Button { Haptics.click(); open(id) } label: {
                    SatchelButtonLabel(title: "Open", systemImage: "arrow.up.right", kind: .secondary, height: 32)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("import-job-open")
            }
            dismiss
        case .failed(let name, let message):
            Button { Haptics.click(); details() } label: {
                HStack(spacing: 10) {
                    icon("exclamationmark.triangle.fill", tint: SatchelTheme.ember, badge: "exclamationmark")
                    // A failure's reason gets two lines; Details shows it in full with tips.
                    summary("Couldn't add \(name)", detail: message, lines: 2)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressableStyle(scale: 0.98, haptic: false))
            .accessibilityHint("Opens the import screen with the full reason")
            .accessibilityIdentifier("import-job-card")
            Button { Haptics.click(); details() } label: {
                SatchelButtonLabel(title: "Details", kind: .secondary, height: 32)
            }
            .buttonStyle(PressableStyle())
            .accessibilityIdentifier("import-job-details")
            dismiss
        case .cancelled(let name):
            HStack(spacing: 10) {
                icon("xmark", tint: SatchelTheme.textDim, badge: "xmark")
                summary("Import stopped", detail: name + " · your original is unchanged", lines: 1)
            }
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("import-job-card")
            dismiss
        }
    }

    private func summary(_ heading: String, detail: String, lines: Int) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(heading).font(.system(size: 13, weight: .bold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                .lineLimit(1).minimumScaleFactor(0.85)
                .accessibilityIdentifier("import-job-heading")
            Text(detail).lineLimit(lines).truncationMode(.tail)
                .fixedSize(horizontal: false, vertical: true)
                .font(.system(size: lines > 1 ? 10 : 11, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }

    private var dismiss: some View {
        Button { withAnimation(SatchelTheme.glide) { library.clearOutcome() } } label: {
            Image(systemName: "xmark").font(.system(size: 12, weight: .bold)).foregroundStyle(SatchelTheme.textDim)
                .frame(width: 30, height: 30).background(Color.white.opacity(0.07), in: Circle())
        }
        .buttonStyle(PressableStyle(scale: 0.9))
        .accessibilityLabel("Dismiss")
        .accessibilityIdentifier("import-job-dismiss")
    }
}
