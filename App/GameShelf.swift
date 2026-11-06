import SwiftUI
import UIKit

/// Whether this build can launch prepared games. The normal app hosts the native player;
/// a minimal UI preview can omit the runtime.
enum NativePlay {
#if !SATCHEL_UI_PREVIEW_MINIMAL
    static var available = true
#else
    static var available = false
#endif
}

/// One tile on the shelf: a game (or homebrew file) plus any updates matched to it.
struct ShelfEntry: Identifiable {
    let item: HomebrewItem
    var updates: [HomebrewItem] = []
    var id: String { item.id }
}

/// Where a library entry is on the way from "imported" to "playing".
enum ShelfState: Equatable {
    case homebrew, ready, needsPreparing, readingInfo, needsKeys, imported, update(String)

    var label: String {
        switch self {
        case .homebrew: return "Ready to try"
        case .ready: return "Ready to play"
        case .needsPreparing: return "Needs preparing"
        case .readingInfo: return "Reading game info"
        case .needsKeys: return "Keys needed"
        case .imported: return "Imported"
        case .update(let version): return version.isEmpty ? "Update" : "Update " + version
        }
    }
    var tint: Color {
        switch self {
        case .homebrew, .ready: return SatchelTheme.leaf
        case .needsPreparing, .needsKeys: return SatchelTheme.spark
        case .readingInfo: return SatchelTheme.tide
        case .imported: return SatchelTheme.textDim
        case .update: return SatchelTheme.violet
        }
    }
    var systemImage: String {
        switch self {
        case .homebrew, .ready: return "play.fill"
        case .needsPreparing: return "hammer.fill"
        case .readingInfo: return "sparkle.magnifyingglass"
        case .needsKeys: return "key.fill"
        case .imported: return "checkmark"
        case .update: return "arrow.up.circle.fill"
        }
    }
}

extension GameArtworkStore {
    func state(_ item: HomebrewItem) -> ShelfState {
        // A prepared-only tile whose package an older engine made needs Prepare again.
        if Self.preparedFolder(item.id) != nil { return isPrepared(item) ? .ready : .needsPreparing }
        guard item.package != nil else { return .homebrew }
        if item.preparedSource?.isUpdate == true, info(item) == nil { return .update("") }
        if let info = info(item) {
            if info.kind == .update { return .update(info.displayVersion.isEmpty ? "" : "v" + info.displayVersion) }
            guard NativePlay.available else { return .imported }
            return isPrepared(item) ? .ready : .needsPreparing
        }
        switch status[item.id] {
        case .reading: return .readingInfo
        case .needsKeys: return keysAvailable ? .readingInfo : .needsKeys
        default: return keysAvailable ? .imported : .needsKeys
        }
    }

    func title(_ item: HomebrewItem) -> String {
        if let title = info(item)?.title, !title.isEmpty { return title }
        return item.name
    }

    func glow(_ item: HomebrewItem) -> Color {
        if let color = infos[item.id]?.accentColor { return color }
        if Self.preparedFolder(item.id) != nil, let id = newestPackage(for: item)?.applicationID,
           let color = titleInfo(id)?.accentColor { return color }
        return SatchelTheme.accent(for: item.id)
    }

    /// Groups updates under their base game when both are imported.
    func shelf(_ items: [HomebrewItem]) -> [ShelfEntry] {
        var entries: [ShelfEntry] = []
        let updates = items.filter { info($0)?.kind == .update || $0.preparedSource?.isUpdate == true }
        for item in items where info(item)?.kind != .addOnContent && info(item)?.kind != .update && item.preparedSource?.isUpdate != true {
            // Classify encrypted packages before assigning a tile. Updates and DLC never flash
            // onto the shelf while artwork is being read or keys are still missing.
            if item.package != nil && info(item) == nil && item.preparedSource == nil { continue }
            var entry = ShelfEntry(item: item)
            if let id = info(item)?.applicationID ?? item.preparedSource?.titleID ?? newestPackage(for: item)?.applicationID {
                entry.updates = updates.filter { (info($0)?.applicationID ?? $0.preparedSource?.titleID) == id }
                    .sorted { (infos[$0.id]?.version ?? 0) > (infos[$1.id]?.version ?? 0) }
            }
            entries.append(entry)
        }
        // Updates and DLC wait in the library until their base game is imported.
        return entries
    }

    /// "Publisher · v1.2.0 · 14.3 GB" style subtitle.
    func subtitle(_ item: HomebrewItem, updates: [HomebrewItem] = []) -> String {
        if let game = preparedPackage(for: item) ?? newestPackage(for: item), Self.preparedFolder(item.id) != nil || item.preparedSource != nil {
            let origin = !game.current ? "Prepared by an older engine" : game.preparedOnPhone ? "Prepared on this iPhone" : "Prepared on a Mac"
            return [origin, game.version.isEmpty ? nil : "v" + game.version].compactMap { $0 }.joined(separator: "  ·  ")
        }
        var parts: [String] = []
        let info = infos[item.id]
        if let publisher = info?.publisher, !publisher.isEmpty { parts.append(publisher) }
        else if item.package == nil { parts.append("Homebrew") }
        let newest = updates.first.flatMap { infos[$0.id] } ?? info
        if let version = newest?.displayVersion, !version.isEmpty { parts.append("v" + version) }
        parts.append(item.formatLabel)
        parts.append(satchelBytes(Int64(item.bytes + updates.reduce(0) { $0 + $1.bytes })))
        return parts.joined(separator: "  ·  ")
    }
}

/// Square game art: the package's own icon, or a generated tile in its place.
struct GameTileArt: View {
    let item: HomebrewItem
    var title: String
    var image: UIImage?
    var side: CGFloat
    var body: some View {
        if let image {
            Image(uiImage: image).resizable().interpolation(.high).scaledToFill()
                .frame(width: side, height: side).clipped()
                .accessibilityHidden(true)
        } else {
            GeneratedTileArt(item: item, title: title, side: side)
        }
    }
}

/// Original generated cover: bold colour, rings and the title's initials.
struct GeneratedTileArt: View {
    let item: HomebrewItem
    let title: String
    let side: CGFloat
    private var accent: Color { SatchelTheme.accent(for: item.id) }
    private var initials: String {
        let words = title.split { !$0.isLetter && !$0.isNumber }.prefix(2)
        let letters = words.count >= 2 ? words.compactMap(\.first).map(String.init).joined() : String(title.prefix(2))
        return letters.uppercased()
    }
    var body: some View {
        ZStack(alignment: .topLeading) {
            accent
            LinearGradient(colors: [.white.opacity(0.18), .clear, .black.opacity(0.35)], startPoint: .topLeading, endPoint: .bottomTrailing)
            Circle().stroke(.white.opacity(0.14), lineWidth: side * 0.08)
                .frame(width: side * 0.95, height: side * 0.95).offset(x: side * 0.45, y: -side * 0.30)
            Circle().stroke(.white.opacity(0.10), lineWidth: side * 0.04)
                .frame(width: side * 0.5, height: side * 0.5).offset(x: side * 0.68, y: -side * 0.06)
            RoundedRectangle(cornerRadius: side * 0.08, style: .continuous).fill(.black.opacity(0.12))
                .frame(width: side * 0.5, height: side * 0.62).rotationEffect(.degrees(-18))
                .offset(x: -side * 0.12, y: side * 0.62)
            VStack(alignment: .leading, spacing: side * 0.02) {
                HStack {
                    Text(item.package == nil ? "HOMEBREW" : item.formatLabel)
                        .font(.system(size: max(8, side * 0.055), weight: .heavy, design: .rounded)).tracking(1.5)
                    Spacer()
                    Image(systemName: item.package == nil ? "sparkles" : "gamecontroller.fill")
                        .font(.system(size: max(10, side * 0.08), weight: .bold))
                }
                Spacer(minLength: 0)
                Text(initials).font(.system(size: side * 0.32, weight: .black, design: .rounded)).tracking(-side * 0.012)
                    .lineLimit(1).minimumScaleFactor(0.5)
                Text(title).font(.system(size: max(9, side * 0.07), weight: .bold, design: .rounded))
                    .lineLimit(2).opacity(0.85)
            }
            .padding(side * 0.09)
            .foregroundStyle(.white)
            .frame(width: side, height: side, alignment: .topLeading)
        }
        .frame(width: side, height: side)
        .clipped()
        .accessibilityHidden(true)
    }
}

/// Spinning two-colour ring around the focused tile.
struct FocusRing: View {
    var cornerRadius: CGFloat
    var lineWidth: CGFloat = 4
    @State private var spin = 0.0
    var body: some View {
        RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
            .strokeBorder(AngularGradient(colors: [SatchelTheme.ember, SatchelTheme.spark, SatchelTheme.tide, SatchelTheme.leaf, SatchelTheme.ember],
                                          center: .center, angle: .degrees(spin)), lineWidth: lineWidth)
            .onAppear { withAnimation(.linear(duration: 6).repeatForever(autoreverses: false)) { spin = 360 } }
            .accessibilityHidden(true)
    }
}

/// Small status pill used on tiles and detail.
struct StateBadge: View {
    let state: ShelfState
    var body: some View {
        SatchelChip(text: state.label, systemImage: state.systemImage, tint: state.tint)
    }
}

/// A shelf tile: art, focus ring, lift and a corner state dot.
struct GameTile: View {
    let entry: ShelfEntry
    let title: String
    let image: UIImage?
    let state: ShelfState
    let glow: Color
    let focused: Bool
    let side: CGFloat

    var body: some View {
        let radius = side * 0.13
        GameTileArt(item: entry.item, title: title, image: image, side: side)
            .clipShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
            .overlay { RoundedRectangle(cornerRadius: radius, style: .continuous).strokeBorder(.white.opacity(0.10)) }
            .overlay(alignment: .topLeading) {
                if !entry.updates.isEmpty {
                    Text("+UPDATE").font(SatchelTheme.label(max(8, side * 0.05))).tracking(1)
                        .padding(.horizontal, 7).padding(.vertical, 4)
                        .foregroundStyle(.white).background(SatchelTheme.violet, in: Capsule())
                        .padding(side * 0.06)
                }
            }
            .overlay(alignment: .topTrailing) {
                if state == .ready || state == .homebrew {
                    Image(systemName: "play.fill").font(.system(size: side * 0.07, weight: .black))
                        .foregroundStyle(SatchelTheme.shell)
                        .frame(width: side * 0.17, height: side * 0.17)
                        .background(SatchelTheme.leaf, in: Circle())
                        .overlay { Circle().strokeBorder(SatchelTheme.shell, lineWidth: 2) }
                        .padding(side * 0.05)
                } else if state == .readingInfo {
                    ProgressView().tint(.white).padding(side * 0.07)
                }
            }
            .padding(7)
            .overlay { if focused { FocusRing(cornerRadius: radius + 7) } }
            .shadow(color: glow.opacity(focused ? 0.55 : 0), radius: focused ? 22 : 0, y: focused ? 10 : 0)
            .scaleEffect(focused ? 1 : 0.84, anchor: .bottom)
            .opacity(focused ? 1 : 0.78)
            .animation(SatchelTheme.snap, value: focused)
    }
}

/// Dashed "add a game" tile at the end of the shelf.
struct AddGameTile: View {
    let side: CGFloat
    let focused: Bool
    var body: some View {
        let radius = side * 0.13
        VStack(spacing: side * 0.06) {
            Image(systemName: "plus").font(.system(size: side * 0.2, weight: .heavy))
                .foregroundStyle(SatchelTheme.shell)
                .frame(width: side * 0.36, height: side * 0.36)
                .background(SatchelTheme.text, in: Circle())
            Text("Add games").font(SatchelTheme.display(max(12, side * 0.085), .bold))
            Text("XCI · NSP · XCZ · NSZ · NRO").font(SatchelTheme.label(max(8, side * 0.05))).tracking(1).foregroundStyle(SatchelTheme.textDim)
        }
        .foregroundStyle(SatchelTheme.text)
        .frame(width: side, height: side)
        .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: radius, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: radius, style: .continuous)
                .strokeBorder(SatchelTheme.textDim.opacity(0.5), style: StrokeStyle(lineWidth: 2.5, dash: [8, 7]))
        }
        .padding(7)
        .overlay { if focused { FocusRing(cornerRadius: radius + 7) } }
        .scaleEffect(focused ? 1 : 0.84, anchor: .bottom)
        .opacity(focused ? 1 : 0.78)
        .animation(SatchelTheme.snap, value: focused)
    }
}
