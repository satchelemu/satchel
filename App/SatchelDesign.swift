import SwiftUI
import UIKit

// Satchel design system. Original look: a dark "console shell" with two
// controller-inspired accents (Ember on the left, Tide on the right), chunky
// rounded type, springy press feedback and haptics. No third-party marks.
enum SatchelTheme {
    static let shell = Color(red: 0.067, green: 0.075, blue: 0.094)        // #111318
    static let surface = Color(red: 0.106, green: 0.118, blue: 0.141)      // #1B1E24
    static let surfaceHigh = Color(red: 0.149, green: 0.165, blue: 0.196)  // #262A32
    static let line = Color.white.opacity(0.08)
    static let text = Color(red: 0.96, green: 0.96, blue: 0.97)
    static let textDim = Color(red: 0.62, green: 0.65, blue: 0.70)
    static let ember = Color(red: 1.0, green: 0.29, blue: 0.33)            // left accent
    static let tide = Color(red: 0.13, green: 0.71, blue: 1.0)             // right accent
    static let spark = Color(red: 1.0, green: 0.82, blue: 0.25)            // needs attention
    static let leaf = Color(red: 0.26, green: 0.86, blue: 0.59)            // ready
    static let violet = Color(red: 0.62, green: 0.50, blue: 1.0)

    static let snap = Animation.spring(response: 0.28, dampingFraction: 0.68)
    static let glide = Animation.spring(response: 0.48, dampingFraction: 0.86)

    static func display(_ size: CGFloat, _ weight: Font.Weight = .heavy) -> Font {
        .system(size: size, weight: weight, design: .rounded)
    }
    static func label(_ size: CGFloat = 10) -> Font {
        .system(size: size, weight: .heavy, design: .rounded)
    }
    static func mono(_ size: CGFloat = 11, _ weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight, design: .monospaced)
    }

    // Stable fallback colour for a library entry without artwork.
    static func accent(for id: String) -> Color {
        let palette = [ember, tide, spark, leaf, violet, Color(red: 1.0, green: 0.55, blue: 0.25)]
        let seed = Int(id.prefix(6), radix: 16) ?? id.unicodeScalars.reduce(0) { $0 &+ Int($1.value) }
        return palette[abs(seed) % palette.count]
    }
}

/// The app's visible name. All user-facing copy reads it from here.
enum AppBrand {
    static let name = "Satchel"
}

// Earlier screens (player, diagnostics) use these names; they now follow the theme.
enum SatchelStyle {
    static let ink = SatchelTheme.shell
    static let panel = SatchelTheme.surface
    static let mint = SatchelTheme.tide
    static let secondary = SatchelTheme.textDim
    static let coral = SatchelTheme.ember
}

@MainActor
enum Haptics {
    private static let selection = UISelectionFeedbackGenerator()
    private static let light = UIImpactFeedbackGenerator(style: .light)
    private static let rigid = UIImpactFeedbackGenerator(style: .rigid)
    private static let notify = UINotificationFeedbackGenerator()
    static func tick() { selection.selectionChanged() }
    static func tap() { light.impactOccurred(intensity: 0.8) }
    static func click() { rigid.impactOccurred() }
    static func success() { notify.notificationOccurred(.success) }
    static func warning() { notify.notificationOccurred(.warning) }
}

/// Springy press: the control sinks, brightens a touch and clicks.
struct PressableStyle: ButtonStyle {
    var scale: CGFloat = 0.94
    var haptic = true
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? scale : 1)
            .brightness(configuration.isPressed ? 0.06 : 0)
            .animation(SatchelTheme.snap, value: configuration.isPressed)
            .onChange(of: configuration.isPressed) { _, pressed in if pressed && haptic { Haptics.tap() } }
            .satchelDimmedWhenDisabled()
    }
}

/// Dims and desaturates a control while it is disabled, so it never reads as broken.
/// Custom labels (capsules, cards, menus) do not dim on their own the way system ones do.
private struct DimmedWhenDisabled: ViewModifier {
    @Environment(\.isEnabled) private var enabled
    func body(content: Content) -> some View {
        content.opacity(enabled ? 1 : 0.42).saturation(enabled ? 1 : 0.3)
            .animation(SatchelTheme.snap, value: enabled)
    }
}

extension View {
    func satchelDimmedWhenDisabled() -> some View { modifier(DimmedWhenDisabled()) }
}

/// Why the controls above are unavailable, shown under them while they are disabled.
struct SatchelDisabledReason: View {
    let text: String
    var systemImage = "lock.fill"
    var body: some View {
        Label(text, systemImage: systemImage)
            .font(.system(size: 11, weight: .semibold, design: .rounded))
            .foregroundStyle(SatchelTheme.textDim)
            .fixedSize(horizontal: false, vertical: true)
            .transition(.opacity)
    }
}

enum SatchelButtonKind { case primary, accent(Color), secondary, quiet, danger }

/// Capsule action label. Use inside a Button with PressableStyle.
struct SatchelButtonLabel: View {
    let title: String
    var systemImage: String?
    var kind: SatchelButtonKind = .primary
    var height: CGFloat = 48
    /// Stretch the capsule to the available width.
    var fill = false
    var body: some View {
        HStack(spacing: 8) {
            if let systemImage { Image(systemName: systemImage).font(.system(size: height * 0.34, weight: .bold)) }
            Text(title).font(SatchelTheme.display(height * 0.34, .bold)).lineLimit(1).minimumScaleFactor(0.8)
        }
        .padding(.horizontal, height * 0.42)
        .frame(maxWidth: fill ? .infinity : nil, minHeight: height)
        .foregroundStyle(foreground)
        .background(background, in: Capsule())
        .overlay { if case .secondary = kind { Capsule().strokeBorder(SatchelTheme.line) } }
        .contentShape(Capsule())
        // A capsule that does not fill keeps its natural width, so labels never truncate.
        .fixedSize(horizontal: !fill, vertical: false)
    }
    private var foreground: Color {
        switch kind {
        case .primary: return SatchelTheme.shell
        case .accent: return .white
        case .secondary: return SatchelTheme.text
        case .quiet: return SatchelTheme.text
        case .danger: return SatchelTheme.ember
        }
    }
    private var background: AnyShapeStyle {
        switch kind {
        case .primary: return AnyShapeStyle(SatchelTheme.text)
        case .accent(let color): return AnyShapeStyle(color.gradient)
        case .secondary: return AnyShapeStyle(SatchelTheme.surfaceHigh)
        case .quiet: return AnyShapeStyle(Color.white.opacity(0.06))
        case .danger: return AnyShapeStyle(SatchelTheme.ember.opacity(0.14))
        }
    }
}

/// Round icon button used in bars.
struct SatchelIconButton: View {
    let title: String
    let systemImage: String
    var tint: Color = SatchelTheme.text
    var fill: Color = SatchelTheme.surfaceHigh
    var identifier: String = ""
    let action: () -> Void
    var body: some View {
        Button(action: { TouchDiagnostics.note("icon button action: " + (identifier.isEmpty ? title : identifier)); action() }) {
            Image(systemName: systemImage).font(.system(size: 17, weight: .bold))
                .foregroundStyle(tint)
                .frame(width: 44, height: 44)
                .background(fill, in: Circle())
                .overlay { Circle().strokeBorder(SatchelTheme.line) }
                .contentShape(Circle())
        }
        .buttonStyle(PressableStyle(scale: 0.88))
        .accessibilityLabel(title)
        .accessibilityIdentifier(identifier.isEmpty ? title : identifier)
    }
}

/// The app mark: two tilted controller-like pills.
struct BrandMark: View {
    var size: CGFloat = 30
    var body: some View {
        HStack(spacing: size * 0.13) {
            RoundedRectangle(cornerRadius: size * 0.22, style: .continuous).fill(SatchelTheme.ember.gradient)
                .frame(width: size * 0.42, height: size)
            RoundedRectangle(cornerRadius: size * 0.22, style: .continuous).fill(SatchelTheme.tide.gradient)
                .frame(width: size * 0.42, height: size * 0.72)
        }
        .rotationEffect(.degrees(-12))
        .accessibilityHidden(true)
    }
}

/// A full-screen panel's chrome: a navigation stack whose close button is a system
/// toolbar item. iOS 27.1 draws it in the vertical bar where the device has one (the
/// Duo's trailing edge) and in a normal top bar elsewhere.
struct PanelChrome<Content: View>: View {
    var closeTitle = "Done"
    let identifier: String
    let close: () -> Void
    @ViewBuilder let content: Content
    var body: some View {
        NavigationStack {
            content
                .toolbar {
                    ToolbarItem(placement: .confirmationAction) {
                        Button {
                            TouchDiagnostics.note("panel close: " + identifier)
                            Haptics.tick()
                            close()
                        } label: { Label(closeTitle, systemImage: "xmark") }
                        .satchelBarButton()
                        .accessibilityIdentifier(identifier)
                    }
                }
                .satchelBars()
        }
    }
}

extension View {
    /// Bars float over Satchel's backdrop with no title row of their own.
    func satchelBars() -> some View {
        toolbarBackground(.hidden, for: .navigationBar)
            .navigationBarTitleDisplayMode(.inline)
    }

    /// A bar item in Satchel's colors. Plain items keep the system style (a custom
    /// style turns them into custom views, which the vertical bar leaves in a top bar);
    /// a tinted item is prominent glass on iOS 26 and later.
    @ViewBuilder func satchelBarButton(_ tint: Color? = nil) -> some View {
        if #available(iOS 26.0, *), let tint {
            buttonStyle(.glassProminent).tint(tint)
        } else {
            self.tint(tint ?? SatchelTheme.text)
        }
    }
}

/// Capsule segmented control in Satchel's colors (the pause menu's frame cap style).
struct SatchelSegmented<Value: Hashable>: View {
    let values: [Value]
    let selection: Value
    let label: (Value) -> String
    let choose: (Value) -> Void
    var body: some View {
        HStack(spacing: 4) {
            ForEach(values, id: \.self) { value in
                let selected = value == selection
                Button {
                    Haptics.tick()
                    withAnimation(SatchelTheme.snap) { choose(value) }
                } label: {
                    Text(label(value)).font(.system(size: 12, weight: .bold, design: .rounded)).lineLimit(1).fixedSize()
                        .foregroundStyle(selected ? SatchelTheme.shell : SatchelTheme.text)
                        .padding(.horizontal, 9).frame(minWidth: 44, minHeight: 34)
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
}

/// Small rounded status pill.
struct SatchelChip: View {
    let text: String
    var systemImage: String?
    var tint: Color = SatchelTheme.textDim
    var filled = false
    var body: some View {
        HStack(spacing: 5) {
            if let systemImage { Image(systemName: systemImage).font(.system(size: 10, weight: .bold)) }
            Text(text).font(SatchelTheme.label(11)).lineLimit(1)
        }
        .padding(.horizontal, 10).frame(height: 24)
        .foregroundStyle(filled ? SatchelTheme.shell : tint)
        .background(filled ? AnyShapeStyle(tint) : AnyShapeStyle(tint.opacity(0.14)), in: Capsule())
    }
}

/// Section label in small, tracked caps.
struct SatchelEyebrow: View {
    let text: String
    var tint: Color = SatchelTheme.textDim
    var body: some View {
        Text(text.uppercased()).font(SatchelTheme.label(10)).tracking(1.6).foregroundStyle(tint)
    }
}

/// Surface panel with optional eyebrow title.
struct SatchelPanel<Content: View>: View {
    var title: String = ""
    var tint: Color = SatchelTheme.textDim
    @ViewBuilder let content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if !title.isEmpty { SatchelEyebrow(text: title, tint: tint) }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay { RoundedRectangle(cornerRadius: 22, style: .continuous).strokeBorder(SatchelTheme.line) }
    }
}

// Explicit card padding keeps readable content independent of native bar/list insets.
struct SatchelCard<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            if !title.isEmpty {
                Text(title).font(SatchelTheme.display(15, .bold))
                    .foregroundStyle(SatchelTheme.textDim)
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(20)
        .background(SatchelTheme.surface, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay { RoundedRectangle(cornerRadius: 22, style: .continuous).strokeBorder(SatchelTheme.line) }
    }
}

/// Dark shell with a soft glow taken from the focused artwork and a faint dot grid.
struct SatchelBackdrop: View {
    var glow: Color = SatchelTheme.tide
    var image: UIImage?
    var body: some View {
        ZStack {
            SatchelTheme.shell
            if let image {
                // Overlay keeps the fill image from growing the layout.
                Color.clear.overlay {
                    Image(uiImage: image).resizable().scaledToFill()
                        .blur(radius: 70).opacity(0.32).saturation(1.3)
                }
                .clipped()
                .transition(.opacity)
                .id(image)
            }
            RadialGradient(colors: [glow.opacity(0.30), .clear], center: .topLeading, startRadius: 10, endRadius: 520)
            RadialGradient(colors: [SatchelTheme.ember.opacity(0.10), .clear], center: .bottomTrailing, startRadius: 10, endRadius: 420)
            Canvas { context, size in
                let step: CGFloat = 22
                var path = Path()
                for x in stride(from: step / 2, to: size.width, by: step) {
                    for y in stride(from: step / 2, to: size.height, by: step) {
                        path.addEllipse(in: CGRect(x: x - 0.8, y: y - 0.8, width: 1.6, height: 1.6))
                    }
                }
                context.fill(path, with: .color(.white.opacity(0.035)))
            }
            LinearGradient(colors: [.clear, SatchelTheme.shell.opacity(0.85)], startPoint: .center, endPoint: .bottom)
        }
        .animation(SatchelTheme.glide, value: image)
        .ignoresSafeArea()
        .accessibilityHidden(true)
    }
}

/// Thick animated progress capsule with moving stripes.
struct SatchelProgressBar: View {
    var value: Double?
    var tint: Color = SatchelTheme.tide
    var height: CGFloat = 14
    @State private var phase: CGFloat = 0
    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            ZStack(alignment: .leading) {
                Capsule().fill(Color.white.opacity(0.07))
                Capsule().fill(tint.gradient)
                    .frame(width: value.map { max(height, width * CGFloat(min(1, max(0, $0)))) } ?? width)
                    .opacity(value == nil ? 0.55 : 1)
                    .overlay {
                        Canvas { context, size in
                            var path = Path()
                            let gap: CGFloat = 14
                            for x in stride(from: -size.height + phase, to: size.width + size.height, by: gap) {
                                path.move(to: CGPoint(x: x, y: size.height))
                                path.addLine(to: CGPoint(x: x + size.height, y: 0))
                                path.addLine(to: CGPoint(x: x + size.height + 5, y: 0))
                                path.addLine(to: CGPoint(x: x + 5, y: size.height))
                                path.closeSubpath()
                            }
                            context.fill(path, with: .color(.white.opacity(0.18)))
                        }.clipShape(Capsule())
                    }
                    .animation(SatchelTheme.glide, value: value)
            }
        }
        .frame(height: height)
        .onAppear { withAnimation(.linear(duration: 0.9).repeatForever(autoreverses: false)) { phase = 14 } }
        .accessibilityElement()
        .accessibilityLabel("Progress")
        .accessibilityValue(value.map { "\(Int($0 * 100)) percent" } ?? "In progress")
    }
}

// Preserve native bar placement while giving icon labels a full touch target.
struct SatchelRailLabel: View {
    let title: String
    let systemImage: String
    init(_ title: String, systemImage: String) { self.title = title; self.systemImage = systemImage }
    var body: some View {
        ZStack {
            Color.clear
            Label(title, systemImage: systemImage).labelStyle(.iconOnly)
                .font(.system(size: 20, weight: .semibold))
        }.frame(width: 44, height: 44).contentShape(Rectangle())
            .accessibilityLabel(title)
    }
}

// Hosting the whole action preserves its touch rectangle in compact native bars.
struct SatchelRailButton: View {
    let title: String
    let systemImage: String
    let identifier: String
    let action: () -> Void
    init(_ title: String, systemImage: String, identifier: String = "", action: @escaping () -> Void) {
        self.title = title; self.systemImage = systemImage; self.identifier = identifier; self.action = action
    }
    var body: some View {
        HStack(spacing: 0) {
            Button(action: action) { SatchelRailLabel(title, systemImage: systemImage) }
                .buttonStyle(.plain).accessibilityIdentifier(identifier)
        }.fixedSize()
    }
}

extension View {
    @ViewBuilder func satchelMenuStatus() -> some View {
        if #available(iOS 27.0, *) {
            self.toolbarVisibility(.visible, for: .statusBar)
                .toolbarColorScheme(.dark, for: .statusBar)
        } else {
            self.statusBarHidden(false)
        }
    }
}

extension ToolbarContent {
    @ToolbarContentBuilder func satchelVertical() -> some ToolbarContent {
        if #available(iOS 27.1, *) { self.axisBehavior(.verticalPreferred).contentMarginsRemoved() }
        else { self }
    }
}

extension UInt64 {
    var titleIDString: String { String(format: "%016llX", self) }
}

func satchelBytes(_ bytes: Int64) -> String { ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }

/// How a foldable is held. On the Duo the inner screen is two front-screen
/// halves, so in landscape the fold runs down the middle like a book.
enum DevicePosture: Equatable {
    case unknown   // not a foldable, or no hinge information
    case closed    // front screen
    case book      // inner screen, partly open
    case flat      // inner screen, fully open
}

/// Per-screen layout facts: room for the camera cutout (only on the side it is
/// on) and the fold posture.
/// Where the system draws navigation bar and toolbar items: a normal top bar, or the
/// iOS 27.1 vertical bar on the leading or trailing edge (the Duo's inner screen).
enum BarPlacement: Equatable { case top, leading, trailing }

struct ScreenTraits: Equatable {
    /// Room to leave on each side: the camera cutout and, on iOS 27.1, the vertical bar.
    var leading: CGFloat = 0
    var trailing: CGFloat = 0
    var posture: DevicePosture = .unknown
    /// The system's active division region (where content should split in two),
    /// in the reader's coordinates, when there is one.
    var divider: CGRect?
    var bar: BarPlacement = .top
    /// The window's symmetric horizontal safe area, which the reader ignores (the cutout
    /// is counted on its own side only). Any safe area beyond it, such as the vertical
    /// bar, is kept.
    var symmetric: CGFloat = 0
    /// The UI preview forces traits for emulated screens; nil means read the device.
    static var override: ScreenTraits?
    /// The last measurement, so a panel opens already laid out for the current screen.
    static var latest: ScreenTraits?

    /// Outer margin on top of the safe area: above and below the content, and on each
    /// side that has no vertical bar (the bar already keeps content off its edge).
    static let edgeMargin: CGFloat = 16
    static let verticalMargin: CGFloat = 14

    /// The traits plus the system's safe area beyond the symmetric part (vertical bar),
    /// and the outer margin when `margin` is set.
    func adding(safe insets: EdgeInsets, margin: Bool) -> ScreenTraits {
        var traits = self
        let edge = margin ? Self.edgeMargin : 0
        traits.leading = max(leading, insets.leading - symmetric) + (bar == .leading ? 0 : edge)
        traits.trailing = max(trailing, insets.trailing - symmetric) + (bar == .trailing ? 0 : edge)
        return traits
    }

    var isBook: Bool { posture == .book }
    /// The fold line when partly open. It crosses the middle of the longer side:
    /// left and right pages in a wide window, top and bottom in a tall one
    /// (the native host puts the game on top and controls below in that case).
    func fold(in size: CGSize) -> FoldLine? {
        // Prefer the system's division region; otherwise assume the middle.
        if let divider, divider.width > 0 || divider.height > 0 {
            return divider.height >= divider.width ? .vertical(divider.midX) : .horizontal(divider.midY)
        }
        guard isBook else { return nil }
        return size.width > size.height ? .vertical(size.width / 2) : .horizontal(size.height / 2)
    }
}

enum FoldLine: Equatable {
    case vertical(CGFloat)    // x of the fold
    case horizontal(CGFloat)  // y of the fold

    var isVertical: Bool { if case .vertical = self { return true } else { return false } }

    /// Sizes of the first (left or top) and second (right or bottom) pages.
    func pages(in size: CGSize) -> (first: CGSize, second: CGSize) {
        switch self {
        case .vertical(let x): return (CGSize(width: x, height: size.height), CGSize(width: size.width - x, height: size.height))
        case .horizontal(let y): return (CGSize(width: size.width, height: y), CGSize(width: size.width, height: size.height - y))
        }
    }
}

/// A GeometryReader that lays out edge to edge horizontally and passes the
/// screen traits, so the side without a cutout keeps only a normal margin.
/// Updates on rotation, on moving between screens and on hinge changes.
struct ScreenReader<Content: View>: View {
    @State private var traits = ScreenTraits.override ?? ScreenTraits.latest ?? ScreenProbe.measureKeyWindow() ?? ScreenTraits()
    @State private var measured = false
    private let margin: Bool
    private let content: (ScreenTraits, GeometryProxy) -> Content
    /// `margin` adds Satchel's outer margin (see `ScreenTraits.edgeMargin`). Backgrounds
    /// that must reach the screen edges go in `.background` outside the reader.
    init(margin: Bool = true, @ViewBuilder content: @escaping (ScreenTraits, GeometryProxy) -> Content) {
        self.margin = margin
        self.content = content
    }
    var body: some View {
        // The outer reader sees the system safe area (including a vertical bar); the inner
        // one spans the full width, and content pads by the traits instead.
        GeometryReader { outer in
            let current = traits.adding(safe: outer.safeAreaInsets, margin: margin)
            GeometryReader { geometry in content(current, geometry) }
                .padding(.vertical, margin ? ScreenTraits.verticalMargin : 0)
                .ignoresSafeArea(.container, edges: current.bar == .top ? [.horizontal, .top] : .horizontal)
        }
        .background {
            ScreenProbe { value in
                // The first measurement lands without animation so nothing slides in.
                if !measured { measured = true; traits = value }
                else if value != traits { withAnimation(SatchelTheme.glide) { traits = value } }
            }
            .allowsHitTesting(false).accessibilityHidden(true)
        }
    }
}

struct ScreenProbe: UIViewRepresentable {
    let changed: (ScreenTraits) -> Void

    /// A synchronous first guess from the key window, so the first frame already has
    /// the cutout and bar room (the probe confirms it once it is in the window).
    static func measureKeyWindow() -> ScreenTraits? {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        guard let window = windows.first(where: \.isKeyWindow) ?? windows.first else { return nil }
        return ProbeView.measure(window, in: window, posture: .unknown)
    }
    func makeUIView(context: Context) -> ProbeView { ProbeView() }
    func updateUIView(_ view: ProbeView, context: Context) { view.changed = changed; view.report() }

    final class ProbeView: UIView {
        var changed: ((ScreenTraits) -> Void)?
        private var observer: NSObjectProtocol?
        private var last: ScreenTraits?
        private var hingePosture: DevicePosture = .unknown
        private var watchingHinge = false
        private var geometryObservation: NSKeyValueObservation?

        override init(frame: CGRect) {
            super.init(frame: frame)
            isUserInteractionEnabled = false
            // A half turn (landscape left to right) keeps the same size, so no layout pass.
            observer = NotificationCenter.default.addObserver(forName: UIDevice.orientationDidChangeNotification, object: nil, queue: .main) { [weak self] _ in
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { self?.report() }
            }
            if #available(iOS 27.1, *) {
                registerForTraitChanges(UITraitCollection.systemTraitsAffectingVerticalBarEdge) { (view: ProbeView, _: UITraitCollection) in
                    view.report()
                }
            }
        }
        required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
        deinit {
            if let observer { NotificationCenter.default.removeObserver(observer) }
            geometryObservation?.invalidate()
        }

        override func didMoveToWindow() {
            super.didMoveToWindow()
            watchHinge()
            // A landscape-to-landscape turn or a move to the other screen can keep
            // the same size, so no layout pass happens; the scene geometry changes.
            geometryObservation = window?.windowScene?.observe(\.effectiveGeometry, options: [.new]) { [weak self] _, _ in
                DispatchQueue.main.async {
                    self?.report()
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { self?.report() }
                }
            }
            report()
        }
        override func layoutSubviews() { super.layoutSubviews(); report() }
        override func safeAreaInsetsDidChange() { super.safeAreaInsetsDidChange(); report() }

        private func watchHinge() {
            guard !watchingHinge else { return }
            if #available(iOS 27.1, *) {
                watchingHinge = true
                addInteraction(UIHingeInteraction { [weak self] _, update in
                    guard let self else { return }
                    switch update.hinge?.status {
                    case .closed: self.hingePosture = .closed
                    case .partiallyOpen: self.hingePosture = .book
                    case .fullyOpen: self.hingePosture = .flat
                    default: self.hingePosture = .unknown
                    }
                    self.report()
                })
            }
        }

        func report() {
            guard let window else { return }
            if let value = ScreenTraits.override { publish(value); return }
            publish(Self.measure(self, in: window, posture: hingePosture))
        }

        /// Cutout sides, fold and bar placement for `view`, in its own coordinates.
        static func measure(_ view: UIView, in window: UIWindow, posture: DevicePosture) -> ScreenTraits {
            let ltr = view.effectiveUserInterfaceLayoutDirection == .leftToRight
            let symmetric = min(window.safeAreaInsets.left, window.safeAreaInsets.right)
            var bar = BarPlacement.top
            if #available(iOS 27.1, *) {
                switch view.traitCollection.verticalBarEdge {
                case .leading: bar = .leading
                case .trailing: bar = .trailing
                default: bar = .top
                }
                // The system reports occlusions (the camera cutout, margins included)
                // and divisions (the fold) in this view's own coordinates.
                var leftInset: CGFloat = 0, rightInset: CGFloat = 0
                let bounds = view.bounds
                for region in view.reservedRegions(kind: .occlusion) where region.isActive {
                    let frame = region.frame
                    if frame.minX <= bounds.width * 0.25 { leftInset = max(leftInset, frame.maxX) }
                    else if frame.maxX >= bounds.width * 0.75 { rightInset = max(rightInset, bounds.width - frame.minX) }
                }
                let divider = view.reservedRegions(kind: .division).first(where: \.isActive)?.frame
                let reported = !view.reservedRegions(kind: .occlusion).isEmpty
                if reported || divider != nil {
                    return ScreenTraits(leading: ltr ? leftInset : rightInset, trailing: ltr ? rightInset : leftInset,
                                        posture: posture, divider: divider, bar: bar, symmetric: symmetric)
                }
            }
            let left = window.safeAreaInsets.left, right = window.safeAreaInsets.right
            var cutoutLeft = false, cutoutRight = false
            if abs(left - right) > 1 {
                // The system already says which side is covered.
                cutoutLeft = left > right; cutoutRight = right > left
            } else if left > 0 {
                // Symmetric insets: the cutout sits at the top of the device.
                switch window.windowScene?.interfaceOrientation {
                case .landscapeRight: cutoutLeft = true
                case .landscapeLeft: cutoutRight = true
                default: cutoutLeft = true; cutoutRight = true
                }
            }
            let leftInset = cutoutLeft ? left : 0, rightInset = cutoutRight ? right : 0
            return ScreenTraits(leading: ltr ? leftInset : rightInset, trailing: ltr ? rightInset : leftInset,
                                posture: posture, bar: bar, symmetric: symmetric)
        }

        private func publish(_ value: ScreenTraits) {
            guard value != last else { return }
            last = value
            ScreenTraits.latest = value
            DispatchQueue.main.async { [changed] in changed?(value) }
        }
    }
}

extension View {
    /// Normal margin on both sides, plus the cutout's room on its side only.
    func cutoutPadding(_ cut: ScreenTraits, leading: CGFloat, trailing: CGFloat) -> some View {
        padding(.leading, cut.leading + leading).padding(.trailing, cut.trailing + trailing)
    }
}

/// Two pages split at the fold, with a soft crease, for the half-folded posture.
/// Pages get the normal margin plus the cutout's room on its side.
struct FoldSplit<First: View, Second: View>: View {
    let fold: FoldLine
    let cut: ScreenTraits
    var gutter: CGFloat = 22
    var margin: CGFloat = 20
    /// Optional solid fill behind the whole second page.
    var secondFill: Color?
    @ViewBuilder let first: First
    @ViewBuilder let second: Second

    var body: some View {
        switch fold {
        case .vertical(let x):
            HStack(spacing: 0) {
                first.padding(.leading, cut.leading + margin).padding(.trailing, gutter).frame(width: x)
                second.padding(.leading, gutter).padding(.trailing, cut.trailing + margin)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background { secondFill?.ignoresSafeArea() }
            }
            .background(alignment: .leading) { crease(width: 18, height: nil).offset(x: x - 9) }
        case .horizontal(let y):
            VStack(spacing: 0) {
                first.cutoutPadding(cut, leading: margin, trailing: margin).padding(.bottom, gutter).frame(height: y)
                second.cutoutPadding(cut, leading: margin, trailing: margin).padding(.top, gutter)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background { secondFill?.ignoresSafeArea() }
            }
            .background(alignment: .top) { crease(width: nil, height: 18).offset(y: y - 9) }
        }
    }

    private func crease(width: CGFloat?, height: CGFloat?) -> some View {
        LinearGradient(colors: [.clear, .black.opacity(0.35), .clear],
                       startPoint: width == nil ? .top : .leading, endPoint: width == nil ? .bottom : .trailing)
            .frame(width: width, height: height)
            .allowsHitTesting(false).accessibilityHidden(true)
    }
}
