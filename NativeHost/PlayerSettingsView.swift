import SwiftUI

/// Choices made before a prepared game starts, in the pause menu's look. The game runs at its
/// saved title settings; the screen mode can also change while playing.
struct PlayerSettingsView: View {
    let titleID: String
    /// The package folder being opened, shown so similar packages can be told apart.
    var package = ""
    var addOns: [PreparedAddOn] = []
    let play: () -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var mode = "fit"
    @State private var haptics = true

    private var profile: NativeGameProfile? { NativeGameProfile.find(titleID: titleID) }

    var body: some View {
        PanelChrome(closeTitle: "Back", identifier: "close-player-settings", close: { dismiss() }) {
            ScreenReader { cut, geometry in
                let reserve: CGFloat = cut.bar == .top ? 56 : 0
                ZStack {
                    if let fold = cut.fold(in: geometry.size) {
                        // Half folded: the game and Play on the first page, settings on the second.
                        FoldSplit(fold: fold, cut: cut) {
                            column { summary }
                        } second: {
                            column { panels }.padding(.trailing, fold.isVertical ? reserve : 0)
                        }
                    } else {
                        let compact = geometry.size.width - cut.leading - cut.trailing < 760
                        HStack(alignment: .top, spacing: compact ? 16 : 24) {
                            column { summary }.frame(width: compact ? 230 : 280)
                            column { panels }.padding(.trailing, reserve)
                        }
                        .cutoutPadding(cut, leading: 20, trailing: 20)
                    }
                }
            }
            .background { SatchelBackdrop(glow: SatchelTheme.ember) }
            .satchelMenuStatus()
        }
        .controllerNavigation(back: { dismiss() })
        .onAppear {
            let settings = SatchelPlayerSettings(titleID)
            mode = settings["DM_SCREEN_MODE"] ?? "fit"
            haptics = settings["DM_HAPTICS"] != "off"
        }
    }

    private func column<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) { content() }.padding(.vertical, 18)
        }
        .scrollIndicators(.hidden)
        .scrollClipDisabled() // scrolls through the outer margin to the screen edge
    }

    // MARK: Game and Play

    @ViewBuilder private var summary: some View {
        VStack(alignment: .leading, spacing: 6) {
            SatchelEyebrow(text: "Before you play", tint: SatchelTheme.ember)
            Text(profile?.name ?? (package.isEmpty ? "Prepared game" : package))
                .font(SatchelTheme.display(24)).lineLimit(3).minimumScaleFactor(0.8)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 6) {
                if let version = profile?.version { SatchelChip(text: "v" + version) }
                if !package.isEmpty { SatchelChip(text: package, systemImage: "shippingbox.fill") }
            }
        }
        Text("Uses this title's saved picture settings, \(mode == "stretch" ? "stretched" : "fit to the screen").")
            .font(.system(size: 13, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.top, 4)
        Button { Haptics.click(); save(); play() } label: {
            SatchelButtonLabel(title: "Play", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 52, fill: true)
        }
        .buttonStyle(PressableStyle())
        .accessibilityIdentifier("player-play")
        .controllerAction("player-play") { save(); play() }
        Label("While playing, tap the pause button or double tap with three fingers to open the menu. Controller: hold Home, or configure the shortcut in Settings, Controllers.",
              systemImage: "hand.tap.fill")
            .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
    }

    // MARK: Settings

    @ViewBuilder private var panels: some View {
        if !package.isEmpty { WarmupPanel(package: package, title: profile?.name ?? package) }
        if !addOns.isEmpty {
            SatchelPanel(title: "Loaded with this game") {
                ForEach(addOns, id: \.titleID) { row in
                    Text("DLC \(row.titleID) · \(satchelBytes(Int64(clamping: row.bytes)))")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                }
            }
        }
        SatchelPanel(title: "Vibration", tint: SatchelTheme.tide) {
            Toggle("Game vibration", isOn: $haptics)
                .accessibilityIdentifier("player-haptics")
            note("Use controller rumble, or the phone's haptics with touch controls. Saved for this game.")
        }
        SatchelPanel(title: "Picture", tint: SatchelTheme.tide) {
            HStack(spacing: 8) {
                ForEach(ScreenMode.allCases) { screenCard($0) }
            }
            note("Fit keeps the whole 16:9 picture. Stretch fills the screen. Double tap with two fingers to switch while playing.")
        }
    }

    private func screenCard(_ screen: ScreenMode) -> some View {
        let selected = mode == screen.rawValue
        return Button {
            Haptics.tick()
            withAnimation(SatchelTheme.snap) { mode = screen.rawValue }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                Image(systemName: screen.systemImage).font(.system(size: 18, weight: .bold))
                    .foregroundStyle(selected ? SatchelTheme.shell : SatchelTheme.tide)
                Text(screen.title).font(.system(size: 14, weight: .bold, design: .rounded))
                Text(screen.detail).font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundStyle(selected ? SatchelTheme.shell.opacity(0.75) : SatchelTheme.textDim)
                    .lineLimit(2).fixedSize(horizontal: false, vertical: true)
            }
            .foregroundStyle(selected ? SatchelTheme.shell : SatchelTheme.text)
            .padding(10)
            .frame(maxWidth: .infinity, minHeight: 92, alignment: .topLeading)
            .background(selected ? AnyShapeStyle(SatchelTheme.tide) : AnyShapeStyle(SatchelTheme.surfaceHigh),
                        in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        }
        .buttonStyle(PressableStyle(scale: 0.96, haptic: false))
        .accessibilityLabel("\(screen.title): \(screen.detail)")
        .accessibilityAddTraits(selected ? .isSelected : [])
        .accessibilityIdentifier("player-screen-" + screen.rawValue)
    }

    private func note(_ text: String) -> some View {
        Text(text).font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func save() {
        let current = SatchelSavedPlayerSettings(titleID)
        let selected = ["DM_SCREEN_MODE": mode, "DM_HAPTICS": haptics ? "on" : "off"]
        let changed = selected.filter { current[$0.key] != $0.value }
        if !changed.isEmpty { _ = SatchelSavePlayerSettings(titleID, changed) }
    }
}
