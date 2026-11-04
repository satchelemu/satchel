import SwiftUI

/// How the game picture fills the screen.
enum ScreenMode: String, Codable, CaseIterable, Identifiable {
    case fit, stretch
    var id: String { rawValue }
    var title: String { self == .fit ? "Fit" : "Stretch" }
    var detail: String { self == .fit ? "16:9, whole picture" : "Fills the screen" }
    var systemImage: String { self == .fit ? "rectangle.center.inset.filled" : "arrow.left.and.right.square.fill" }

    init(from decoder: Decoder) throws {
        // Older saves may hold modes that no longer exist; they fall back to Fit.
        self = ScreenMode(rawValue: (try? decoder.singleValueContainer().decode(String.self)) ?? "") ?? .fit
    }
}

/// Per-game play settings, saved on this iPhone.
struct InGameSettings: Codable, Equatable {
    var screenMode: ScreenMode = .fit
    var showFPS = false
    var touchOpacity = 0.6
    var touchSize = 1.0
    var haptics = true
    var volume = 0.8

    init() {}

    init(from decoder: Decoder) throws {
        // Every field is optional so settings saved by older builds still load; their play mode,
        // frame cap and render scale are ignored (each game runs as it expects, at full scale).
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let base = InGameSettings()
        screenMode = (try? c.decodeIfPresent(ScreenMode.self, forKey: .screenMode)) ?? base.screenMode
        showFPS = (try? c.decodeIfPresent(Bool.self, forKey: .showFPS)) ?? base.showFPS
        touchOpacity = (try? c.decodeIfPresent(Double.self, forKey: .touchOpacity)) ?? base.touchOpacity
        touchSize = (try? c.decodeIfPresent(Double.self, forKey: .touchSize)) ?? base.touchSize
        haptics = (try? c.decodeIfPresent(Bool.self, forKey: .haptics)) ?? base.haptics
        volume = (try? c.decodeIfPresent(Double.self, forKey: .volume)) ?? base.volume
    }

    /// Saved settings for a title, or the defaults on a first run.
    static func load(titleID: String) -> InGameSettings {
        if let data = UserDefaults.standard.data(forKey: key(titleID)),
           let settings = try? JSONDecoder().decode(InGameSettings.self, from: data) { return settings }
        return InGameSettings()
    }

    func save(titleID: String) {
        if let data = try? JSONEncoder().encode(self) { UserDefaults.standard.set(data, forKey: Self.key(titleID)) }
    }

    private static func key(_ titleID: String) -> String { "InGameSettings." + titleID.uppercased() }
}

/// What a running game offers the menu. The native host bridge conforms to
/// this; the menu never talks to the engine directly.
@MainActor protocol InGameSessionControl: AnyObject {
    var gameTitle: String { get }
    var titleID: String { get }
    /// The engine's lower frame cap while the phone is hot, or nil when not limited.
    var thermalFrameCap: Int? { get }
    /// Latest measured frame rate, or nil if the host does not report one.
    var framesPerSecond: Double? { get }
    /// Applies settings while the game is paused or running.
    func apply(_ settings: InGameSettings)
    /// Closes the menu and continues the game.
    func resume()
    /// Keeps the game paused in memory and shows the library.
    func pauseAndReturnToLibrary()
    /// Ends the game session and returns to the library.
    func quit()
}

@MainActor final class InGameMenuModel: ObservableObject {
    @Published var settings: InGameSettings {
        didSet {
            guard settings != oldValue else { return }
            session.apply(settings)
            settings.save(titleID: session.titleID)
        }
    }
    @Published private(set) var framesPerSecond: Double?
    @Published private(set) var thermalFrameCap: Int?
    let session: InGameSessionControl
    private var timer: Timer?

    init(session: InGameSessionControl) {
        self.session = session
        settings = InGameSettings.load(titleID: session.titleID)
        framesPerSecond = session.framesPerSecond
        thermalFrameCap = session.thermalFrameCap
    }

    func startUpdating() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.framesPerSecond = self.session.framesPerSecond
                self.thermalFrameCap = self.session.thermalFrameCap
            }
        }
    }

    func stopUpdating() { timer?.invalidate(); timer = nil }
}

/// Pause menu shown over the game. Present it over the game view with a clear
/// background; it dims the picture itself, except in the book posture where the
/// game stays visible on the left page.
/// The pause menu as a presented screen: a navigation stack whose close button is a system
/// bar item, so iOS 27.1 shows it in the vertical bar with the status bar, as in the app.
struct InGameMenuScreen: View {
    @ObservedObject var model: InGameMenuModel
    var body: some View {
        NavigationStack {
            InGameMenuView(model: model, barClose: true)
                .toolbar {
                    ToolbarItem(placement: .confirmationAction) {
                        Button {
                            Haptics.click()
                            model.session.resume()
                        } label: { Label("Resume", systemImage: "xmark") }
                        .satchelBarButton()
                        .accessibilityIdentifier("close-menu")
                    }
                }
                .satchelBars()
                .satchelMenuStatus()
        }
    }
}

struct InGameMenuView: View {
    @ObservedObject var model: InGameMenuModel
    /// The close button is a bar item (InGameMenuScreen) rather than drawn here.
    var barClose = false
    @State private var confirmQuit = false

    /// Room on the trailing side for a close button over the content (44 points plus spacing).
    private func closeStrip(_ cut: ScreenTraits) -> CGFloat {
        barClose && cut.bar != .top ? 0 : 58
    }

    var body: some View {
        // The native bridge already sizes this view to the interactive bounds.
        ScreenReader(margin: false) { cut, geometry in
            if let fold = cut.fold(in: geometry.size) {
                // Half folded: the game stays visible on the first page.
                FoldSplit(fold: fold, cut: cut, secondFill: SatchelTheme.shell) {
                    VStack(alignment: .leading, spacing: 12) {
                        Spacer()
                        HStack(spacing: 10) {
                            pausedChip
                            resumeButton(height: 48)
                        }
                        .padding(14)
                        .background(.ultraThinMaterial, in: Capsule())
                    }
                    .padding(.bottom, 20)
                    .frame(maxWidth: .infinity, alignment: .leading)
                } second: {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 14) {
                            titleBlock
                            actionButtons(includeResume: false)
                            settingsPanels
                        }
                        .padding(.vertical, 18)
                    }
                    .scrollIndicators(.hidden)
                }
            } else {
                let leftWidth = min(240, geometry.size.width * 0.34)
                let rightWidth = max(200, geometry.size.width - cut.leading - cut.trailing - 44 - 22 - leftWidth - closeStrip(cut))
                HStack(alignment: .top, spacing: 22) {
                    VStack(alignment: .leading, spacing: 14) {
                        titleBlock
                        Spacer(minLength: 4)
                        actionButtons(includeResume: true)
                    }
                    .frame(width: leftWidth)
                    .padding(.vertical, 18)
                    ScrollView {
                        settingsPanels.padding(.vertical, 18).frame(width: rightWidth)
                    }
                    .scrollIndicators(.hidden)
                    .frame(width: rightWidth)
                }
                .cutoutPadding(cut, leading: 22, trailing: 22)
                .frame(width: geometry.size.width, height: geometry.size.height, alignment: .leading)
                .background {
                    ZStack {
                        Rectangle().fill(.ultraThinMaterial)
                        SatchelTheme.shell.opacity(0.72)
                    }
                    .ignoresSafeArea()
                }
            }
        }
        .overlay(alignment: .topTrailing) {
            if !barClose {
                ScreenReader(margin: false) { cut, _ in
                    SatchelIconButton(title: "Close menu", systemImage: "xmark", identifier: "close-menu") { model.session.resume() }
                        .padding(.top, 18).padding(.trailing, cut.trailing + 22)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                }
            }
        }
        .onAppear { model.startUpdating() }
        .onDisappear { model.stopUpdating() }
        .alert("Quit \(model.session.gameTitle)?", isPresented: $confirmQuit) {
            Button("Quit game", role: .destructive) { Haptics.click(); model.session.quit() }
            Button("Keep playing", role: .cancel) {}
        } message: {
            Text("Progress since your last save is lost. Quitting returns to your library.")
        }
        .preferredColorScheme(.dark)
    }

    // MARK: Pieces

    private var pausedChip: some View {
        SatchelChip(text: "Paused", systemImage: "pause.fill", tint: SatchelTheme.spark)
    }

    private var titleBlock: some View {
        VStack(alignment: .leading, spacing: 8) {
            pausedChip
            Text(model.session.gameTitle).font(SatchelTheme.display(24)).foregroundStyle(SatchelTheme.text)
                .lineLimit(2).minimumScaleFactor(0.75)
            Text(fpsText).font(SatchelTheme.mono(12)).foregroundStyle(SatchelTheme.textDim)
            if let cap = model.thermalFrameCap { thermalBanner(cap) }
        }
    }

    private var fpsText: String {
        guard let fps = model.framesPerSecond else { return "The game's own frame rate" }
        return String(format: "%.0f FPS", fps)
    }

    /// Plain notice while the engine holds the frame rate down to cool the phone.
    private func thermalBanner(_ cap: Int) -> some View {
        Label("Phone is warm: limited to \(cap) FPS to cool down.", systemImage: "thermometer.sun.fill")
            .font(.system(size: 12, weight: .bold, design: .rounded))
            .foregroundStyle(SatchelTheme.spark)
            .padding(.horizontal, 10).padding(.vertical, 8)
            .background(SatchelTheme.spark.opacity(0.14), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .fixedSize(horizontal: false, vertical: true)
            .accessibilityIdentifier("menu-thermal")
    }

    private func resumeButton(height: CGFloat) -> some View {
        Button { Haptics.click(); model.session.resume() } label: {
            SatchelButtonLabel(title: "Resume", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: height)
                .fixedSize()
        }
        .buttonStyle(PressableStyle())
        .accessibilityIdentifier("menu-resume")
    }

    private func actionButtons(includeResume: Bool) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            if includeResume {
                Button { Haptics.click(); model.session.resume() } label: {
                    SatchelButtonLabel(title: "Resume", systemImage: "play.fill", kind: .accent(SatchelTheme.ember), height: 50, fill: true)
                }
                .buttonStyle(PressableStyle())
                .accessibilityIdentifier("menu-resume")
            }
            Button { model.session.pauseAndReturnToLibrary() } label: {
                SatchelButtonLabel(title: "Library", systemImage: "square.grid.2x2.fill", kind: .secondary, height: 44, fill: includeResume)
            }
            .buttonStyle(PressableStyle())
            .accessibilityHint("The game stays paused")
            .accessibilityIdentifier("menu-library")
            Button { confirmQuit = true } label: {
                SatchelButtonLabel(title: "Quit game", systemImage: "xmark", kind: .danger, height: 40, fill: includeResume)
            }
            .buttonStyle(PressableStyle())
            .accessibilityIdentifier("menu-quit")
            Text("Library keeps the game paused. Quit ends it and returns to your library.")
                .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
            Label("Open this menu any time: three-finger double tap, or hold Options and Menu on a controller.",
                  systemImage: "hand.tap.fill")
                .font(.system(size: 11, weight: .medium, design: .rounded)).foregroundStyle(SatchelTheme.textDim)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var settingsPanels: some View {
        VStack(alignment: .leading, spacing: 14) {
            SatchelPanel(title: "Picture", tint: SatchelTheme.tide) {
                HStack(spacing: 8) {
                    ForEach(ScreenMode.allCases) { mode in
                        modeCard(mode)
                    }
                }
                toggleRow("FPS counter", isOn: $model.settings.showFPS)
            }
            SatchelPanel(title: "Touch controls", tint: SatchelTheme.leaf) {
                sliderRow("Opacity", value: $model.settings.touchOpacity, range: 0.2...1,
                          low: "circle.dotted", high: "circle.fill")
                sliderRow("Size", value: $model.settings.touchSize, range: 0.75...1.25,
                          low: "smallcircle.filled.circle", high: "largecircle.fill.circle")
                toggleRow("Haptics", isOn: $model.settings.haptics)
            }
            SatchelPanel(title: "Sound", tint: SatchelTheme.violet) {
                sliderRow("Volume", value: $model.settings.volume, range: 0...1,
                          low: "speaker.fill", high: "speaker.wave.3.fill")
            }
        }
    }

    private func modeCard(_ mode: ScreenMode) -> some View {
        let selected = model.settings.screenMode == mode
        return Button {
            Haptics.tick()
            withAnimation(SatchelTheme.snap) { model.settings.screenMode = mode }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                Image(systemName: mode.systemImage).font(.system(size: 18, weight: .bold))
                    .foregroundStyle(selected ? SatchelTheme.shell : SatchelTheme.tide)
                Text(mode.title).font(.system(size: 14, weight: .bold, design: .rounded))
                Text(mode.detail).font(.system(size: 10, weight: .medium, design: .rounded))
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
        .accessibilityLabel("\(mode.title): \(mode.detail)")
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func row<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 12) {
            Text(title).font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                .lineLimit(1).minimumScaleFactor(0.8)
            Spacer(minLength: 6)
            content()
        }
    }

    private func segmented<Value: Equatable>(_ values: [Value], selection: Value, label: @escaping (Value) -> String,
                                             choose: @escaping (Value) -> Void) -> some View {
        HStack(spacing: 4) {
            ForEach(values.indices, id: \.self) { index in
                let value = values[index], selected = value == selection
                Button {
                    Haptics.tick()
                    withAnimation(SatchelTheme.snap) { choose(value) }
                } label: {
                    Text(label(value)).font(.system(size: 12, weight: .bold, design: .rounded)).lineLimit(1).fixedSize()
                        .foregroundStyle(selected ? SatchelTheme.shell : SatchelTheme.text)
                        .padding(.horizontal, 7).frame(minWidth: 40, minHeight: 34)
                        .background(selected ? AnyShapeStyle(SatchelTheme.text) : AnyShapeStyle(Color.clear), in: Capsule())
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? .isSelected : [])
            }
        }
        .padding(3)
        .background(SatchelTheme.surfaceHigh, in: Capsule())
    }

    private func toggleRow(_ title: String, isOn: Binding<Bool>) -> some View {
        Toggle(isOn: isOn) {
            Text(title).font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.text)
        }
        .tint(SatchelTheme.leaf)
        .frame(minHeight: 36)
    }

    private func sliderRow(_ title: String, value: Binding<Double>, range: ClosedRange<Double>,
                           low: String, high: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title).font(.system(size: 14, weight: .semibold, design: .rounded)).foregroundStyle(SatchelTheme.text)
                Spacer()
                Text("\(Int(((value.wrappedValue - range.lowerBound) / (range.upperBound - range.lowerBound)) * 100))%")
                    .font(SatchelTheme.mono(12)).foregroundStyle(SatchelTheme.textDim)
            }
            HStack(spacing: 10) {
                Image(systemName: low).font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
                Slider(value: value, in: range).tint(SatchelTheme.tide)
                    .accessibilityLabel(title)
                Image(systemName: high).font(.system(size: 14)).foregroundStyle(SatchelTheme.textDim)
            }
        }
    }
}
