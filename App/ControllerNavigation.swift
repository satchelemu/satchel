import SwiftUI
import UIKit
import UIKit.UIGestureRecognizerSubclass
import GameController
import Combine

/// What iOS reports for one connected controller, for Settings > Controllers.
struct ControllerDiagnostic: Identifiable, Equatable {
    let id: ObjectIdentifier
    let name: String, productCategory: String, satchelCategory: String, profile: String
    let details: String, pressed: String, axes: String
    let current: Bool
    let changes: Int
}

/// Polling leaves the engine's valueChangedHandler intact. Only edges are sent to UI scopes.
/// It starts at launch, so controllers that were connected before the app opened are seen
/// as soon as GameController reports them, and connection events are kept for diagnosis.
@MainActor final class ControllerInputMonitor: ObservableObject {
    static let shared = ControllerInputMonitor()
    @Published var values: [String: NSNumber] = [:]
    @Published var category = "Generic"
    @Published var connected = false
    @Published private(set) var devices: [ControllerDiagnostic] = []
    @Published private(set) var otherInputs = ""
    @Published private(set) var history: [String] = []
    let events = PassthroughSubject<String, Never>()
    /// Any touch on the screen, so controller focus hides while touch is in use.
    let touches = PassthroughSubject<Void, Never>()
    private var timer: Timer?
    private var states: [ObjectIdentifier: NSMutableDictionary] = [:]
    private var changes: [ObjectIdentifier: Int] = [:]
    private var lastPressed: [ObjectIdentifier: String] = [:]
    private var diagnosticViewers = 0
#if PREVIEW_GAME
    var synthetic: GCController?
    /// The synthetic pad's own on-screen buttons are not touch use.
    var ignoresTouch: ((UITouch) -> Bool)?
#endif
    func start() {
        guard timer == nil else { return }
        let center = NotificationCenter.default
        for (name, label) in [(Notification.Name.GCControllerDidConnect, "connected"),
                              (.GCControllerDidDisconnect, "disconnected"),
                              (.GCControllerDidBecomeCurrent, "became current")] {
            center.addObserver(forName: name, object: nil, queue: .main) { note in
                let controller = note.object as? GCController
                MainActor.assumeIsolated {
                    ControllerInputMonitor.shared.note(controller.map { "\(Self.describe($0)) \(label)" } ?? "controller \(label)")
                }
            }
        }
        for controller in GCController.controllers() { note("\(Self.describe(controller)) already connected at start") }
        // Common modes keep polling while a scroll view tracks or decelerates.
        let timer = Timer(timeInterval: 1.0 / 30, repeats: true) { _ in
            MainActor.assumeIsolated { ControllerInputMonitor.shared.poll() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }
    /// Settings shows live per-controller state only while it is on screen.
    func showDiagnostics(_ on: Bool) { diagnosticViewers = max(0, diagnosticViewers + (on ? 1 : -1)); start() }

    static func describe(_ controller: GCController) -> String {
        "'\(controller.vendorName ?? "unnamed")' (\(controller.productCategory), \(profileName(controller)))"
    }
    static func profileName(_ controller: GCController) -> String {
        if controller.extendedGamepad != nil { return "extended gamepad" }
        if controller.microGamepad != nil { return "micro gamepad" }
        return "physical input only"
    }

    /// Kept in memory for Settings and in Documents/controller-log.txt for the Mac to read.
    private func note(_ text: String) {
        let line = Date().formatted(date: .omitted, time: .standard) + " " + text
        history.append(line)
        if history.count > 12 { history.removeFirst(history.count - 12) }
        ControllerLog.append(line)
    }

    private func poll() {
        guard UIApplication.shared.applicationState == .active else {
            states.removeAll(); values = [:]; return
        }
        var controllers = GCController.controllers()
#if PREVIEW_GAME
        if let synthetic { controllers = [synthetic] }
#endif
        // Any profile can navigate. The extended gamepad stays first, for the live mapping view.
        controllers.sort { ($0.extendedGamepad != nil ? 0 : 1) < ($1.extendedGamepad != nil ? 0 : 1) }
        connected = !controllers.isEmpty
        let live = Set(controllers.map { ObjectIdentifier($0) })
        states = states.filter { live.contains($0.key) }
        changes = changes.filter { live.contains($0.key) }
        lastPressed = lastPressed.filter { live.contains($0.key) }
        if controllers.isEmpty { values = [:] }
        for (index, controller) in controllers.enumerated() {
            let id = ObjectIdentifier(controller)
            if index == 0 {
                values = controller.extendedGamepad.map { SatchelControllerValues($0) } ?? [:]
                category = SatchelControllerCategory(controller)
                ControllerGlyphs.shared.update(controller)
            }
            let hadState = states[id] != nil
            let state = states[id] ?? NSMutableDictionary()
            states[id] = state
            let input = Self.navigation(controller)
            let edge = SatchelControllerEdge(state, input.x, input.y, input.accept, input.back)
            // A held D-pad or stick direction repeats after a short delay.
            let repeated = SatchelControllerRepeat(state, CACurrentMediaTime())
            let event = edge.isEmpty ? repeated : edge
            if hadState && !event.isEmpty { events.send(event) }
            let pressed = Self.pressedNames(controller)
            if pressed != lastPressed[id, default: ""] {
                if !pressed.isEmpty && changes[id, default: 0] == 0 { note("\(Self.describe(controller)) first input: \(pressed)") }
                changes[id, default: 0] += 1
                lastPressed[id] = pressed
            }
        }
        if diagnosticViewers > 0 { refreshDiagnostics(controllers) }
    }

    /// Navigation reads only elements every profile has: the Direction Pad, the left stick
    /// when present, and the two primary buttons by physical position.
    static func navigation(_ controller: GCController) -> (x: Float, y: Float, accept: Bool, back: Bool) {
        let labelled = SatchelControllerCategory(controller) == "Pro Controller"
        if let pad = controller.extendedGamepad {
            let x = abs(pad.dpad.xAxis.value) > abs(pad.leftThumbstick.xAxis.value) ? pad.dpad.xAxis.value : pad.leftThumbstick.xAxis.value
            let y = abs(pad.dpad.yAxis.value) > abs(pad.leftThumbstick.yAxis.value) ? pad.dpad.yAxis.value : pad.leftThumbstick.yAxis.value
            // UI uses physical bottom/right positions, independent of gameplay remapping.
            return (x, y, labelled ? pad.buttonB.isPressed : pad.buttonA.isPressed,
                    labelled ? pad.buttonA.isPressed : pad.buttonB.isPressed)
        }
        let profile = controller.physicalInputProfile
        let pad = profile.dpads[GCInputDirectionPad] ?? profile.dpads[GCInputLeftThumbstick] ?? profile.dpads.values.first
        let a = profile.buttons[GCInputButtonA]?.isPressed ?? false
        let back = (profile.buttons[GCInputButtonB] ?? profile.buttons[GCInputButtonX] ?? profile.buttons[GCInputButtonMenu])?.isPressed ?? false
        return (pad?.xAxis.value ?? 0, pad?.yAxis.value ?? 0, a, back)
    }
    static func pressedNames(_ controller: GCController) -> String {
        let profile = controller.physicalInputProfile
        var names = profile.buttons.filter { $0.value.isPressed }.map(\.key)
        for (name, pad) in profile.dpads where max(abs(pad.xAxis.value), abs(pad.yAxis.value)) >= 0.5 { names.append(name) }
        return names.sorted().joined(separator: ", ")
    }
    private func refreshDiagnostics(_ controllers: [GCController]) {
        let current = GCController.current
        devices = controllers.map { c in
            let profile = c.physicalInputProfile
            var details = ["player \(c.playerIndex == .indexUnset ? "unset" : String(c.playerIndex.rawValue + 1))"]
            for (label, present) in [("Home", profile.buttons[GCInputButtonHome] != nil), ("Options", profile.buttons[GCInputButtonOptions] != nil),
                                     ("stick buttons", profile.buttons[GCInputLeftThumbstickButton] != nil),
                                     ("motion", c.motion != nil), ("rumble", c.haptics != nil), ("light", c.light != nil)] {
                details.append((present ? "" : "no ") + label)
            }
            if let battery = c.battery { details.append("battery \(Int(battery.batteryLevel * 100))%") }
            if c.isAttachedToDevice { details.append("attached to the phone") }
            let axes = [GCInputLeftThumbstick, GCInputRightThumbstick, GCInputDirectionPad].compactMap { name in
                profile.dpads[name].map { String(format: "%@ %.2f, %.2f", name, $0.xAxis.value, $0.yAxis.value) }
            }.joined(separator: "   ")
            return ControllerDiagnostic(id: ObjectIdentifier(c), name: c.vendorName ?? "unnamed", productCategory: c.productCategory,
                satchelCategory: SatchelControllerCategory(c), profile: Self.profileName(c), details: details.joined(separator: ", "),
                pressed: Self.pressedNames(c), axes: axes, current: c === current, changes: changes[ObjectIdentifier(c), default: 0])
        }
        var other: [String] = []
        if GCKeyboard.coalesced != nil { other.append("a keyboard") }
        if !GCMouse.mice().isEmpty { other.append("\(GCMouse.mice().count) mouse") }
        otherInputs = other.isEmpty ? "" : "iOS also reports " + other.joined(separator: " and ") +
            ". A controller in keyboard or mouse mode appears here instead of as a controller."
    }
}

/// Connection events and each controller's first input, for reading from the Mac with
/// `devicectl ... copy from ... Documents/controller-log.txt`. Keeps the last 200 lines.
enum ControllerLog {
    private static let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("controller-log.txt")
    private static var lines: [String] = {
        var kept = ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
        kept.append("---- launch")
        return kept
    }()
    static func append(_ line: String) {
        lines.append(line)
        if lines.count > 200 { lines.removeFirst(lines.count - 200) }
        try? lines.joined(separator: "\n").appending("\n").write(to: url, atomically: true, encoding: .utf8)
        NSLog("Satchel controller: %@", line)
    }
}

/// The A and B glyphs of the controller in use, for the hint bar. GameController names the
/// symbol printed on each button, so a DualSense shows its cross and circle.
@MainActor final class ControllerGlyphs: ObservableObject {
    static let shared = ControllerGlyphs()
    @Published private(set) var accept = "a.circle.fill"
    @Published private(set) var back = "b.circle.fill"
    private var source: ObjectIdentifier?
    func update(_ controller: GCController) {
        guard source != ObjectIdentifier(controller) else { return }
        source = ObjectIdentifier(controller)
        // The same physical buttons as navigation reads (see `ControllerInputMonitor.navigation`).
        let labelled = SatchelControllerCategory(controller) == "Pro Controller"
        let profile = controller.physicalInputProfile
        let accept = controller.extendedGamepad.map { labelled ? $0.buttonB : $0.buttonA } ?? profile.buttons[GCInputButtonA]
        let back = controller.extendedGamepad.map { labelled ? $0.buttonA : $0.buttonB }
            ?? profile.buttons[GCInputButtonB] ?? profile.buttons[GCInputButtonX] ?? profile.buttons[GCInputButtonMenu]
        let a = Self.symbol(accept?.sfSymbolsName, fallback: "a.circle"), b = Self.symbol(back?.sfSymbolsName, fallback: "b.circle")
        if a != self.accept { self.accept = a }
        if b != self.back { self.back = b }
    }
    private static func symbol(_ name: String?, fallback: String) -> String {
        for candidate in [name.map { $0.hasSuffix(".fill") ? $0 : $0 + ".fill" }, name] {
            if let candidate, UIImage(systemName: candidate) != nil { return candidate }
        }
        return fallback + ".fill"
    }
}

/// Notes any touch in the window without taking part in it, so controller focus can hide
/// while the player uses the touch screen. It fails at once and never delays or cancels.
private final class ControllerTouchWatcher: UIGestureRecognizer, UIGestureRecognizerDelegate {
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        let monitor = ControllerInputMonitor.shared
#if PREVIEW_GAME
        if let ignores = monitor.ignoresTouch, touches.allSatisfy(ignores) { state = .failed; return }
#endif
        monitor.touches.send()
        state = .failed
    }
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }
}

extension ControllerInputMonitor {
    /// Watches every window of the app for touches (once per window).
    func watchTouches() {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        for window in windows where !(window.gestureRecognizers ?? []).contains(where: { $0 is ControllerTouchWatcher }) {
            let watcher = ControllerTouchWatcher(target: nil, action: nil)
            watcher.cancelsTouchesInView = false
            watcher.delaysTouchesBegan = false
            watcher.delaysTouchesEnded = false
            watcher.delegate = watcher
            window.addGestureRecognizer(watcher)
        }
    }
}

// MARK: Focus

/// The outline a focus highlight follows: the target's own corner radius or shape, so the
/// highlight sits on the target's edge.
enum ControllerFocusShape: Equatable {
    case rounded(CGFloat)
    case capsule
    case circle
}

/// How a controller target shows focus, chosen at the call site. `inset` moves the highlight
/// inside the target's frame (positive, when the target pads around its visible shape) or
/// past it (negative, for a bare control that needs room around it).
enum ControllerFocusStyle: Equatable {
    /// Cards and artwork tiles: lifts, with a soft shadow and a thin bright edge.
    case lift(ControllerFocusShape, inset: CGFloat = 0)
    /// List and settings rows: a filled highlight that slides from row to row.
    case row(ControllerFocusShape, inset: CGFloat = 0)
    /// Buttons and chips: brightens and grows slightly.
    case button(ControllerFocusShape, inset: CGFloat = 0)
    /// The target draws focus itself and is told through `onFocus` (the library's selected
    /// tile). Focus returns to the `selected` one; navigation scrolls only when `scrolls`.
    case own(selected: Bool, scrolls: Bool = false)
}

private struct ControllerFocusOutline: InsettableShape {
    var shape: ControllerFocusShape
    var amount: CGFloat = 0
    func path(in rect: CGRect) -> Path {
        let rect = rect.insetBy(dx: amount, dy: amount)
        switch shape {
        case .rounded(let radius): return RoundedRectangle(cornerRadius: max(0, radius - amount), style: .continuous).path(in: rect)
        case .capsule: return Capsule(style: .continuous).path(in: rect)
        case .circle: return Circle().path(in: rect)
        }
    }
    func inset(by amount: CGFloat) -> Self { var copy = self; copy.amount += amount; return copy }
}

private struct ControllerTarget: Equatable {
    let id: String
    let frame: CGRect
    /// The nearest scroll view's visible bounds, in the same global space as `frame`.
    let viewport: CGRect?
    /// For targets that draw their own focus: whether this is the current one.
    let selected: Bool?
    let scrolls: Bool
    let action: () -> Void
    let onFocus: (() -> Void)?
    static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.id == rhs.id && lhs.frame == rhs.frame && lhs.viewport == rhs.viewport
            && lhs.selected == rhs.selected && lhs.scrolls == rhs.scrolls
    }
}
private struct ControllerTargets: PreferenceKey {
    static var defaultValue: [ControllerTarget] = []
    static func reduce(value: inout [ControllerTarget], nextValue: () -> [ControllerTarget]) { value += nextValue() }
}
/// The focused target, and whether focus is drawn: only while the controller is in use.
/// A touch hides it; the next controller input shows it again on the same target.
@MainActor private final class ControllerFocus: ObservableObject {
    @Published var selected: String?
    @Published var shown = false
}
private struct ControllerFocusContext {
    let focus: ControllerFocus
    let namespace: Namespace.ID
}
private struct ControllerFocusKey: EnvironmentKey { static let defaultValue: ControllerFocusContext? = nil }
private extension EnvironmentValues {
    var controllerFocus: ControllerFocusContext? {
        get { self[ControllerFocusKey.self] }
        set { self[ControllerFocusKey.self] = newValue }
    }
}
private struct ControllerAction: ViewModifier {
    let id: String
    let style: ControllerFocusStyle
    let onFocus: (() -> Void)?
    let action: () -> Void
    @Environment(\.controllerFocus) private var context
    @Environment(\.isEnabled) private var enabled
    func body(content: Content) -> some View {
        Group {
            if let context {
                content.modifier(ControllerFocusLook(focus: context.focus, namespace: context.namespace, id: id, style: style))
            } else {
                content
            }
        }
        .id(id)
        // Measured outside the focus scale, so a lifted target reports its resting frame.
        .background {
            if enabled {
                GeometryReader { proxy in
                    Color.clear.preference(key: ControllerTargets.self, value: [target(proxy)])
                }
            }
        }
    }
    private func target(_ proxy: GeometryProxy) -> ControllerTarget {
        let frame = proxy.frame(in: .global)
        // Local bounds are unscaled; the global frame carries any scale above (the preview's
        // emulated inner screen), so convert with the ratio between them.
        let sx = proxy.size.width > 0 ? frame.width / proxy.size.width : 1
        let sy = proxy.size.height > 0 ? frame.height / proxy.size.height : 1
        let viewport = proxy.bounds(of: .scrollView).map {
            CGRect(x: frame.minX + $0.minX * sx, y: frame.minY + $0.minY * sy, width: $0.width * sx, height: $0.height * sy)
        }
        var selected: Bool?, scrolls = true
        if case .own(let current, let scrolling) = style { selected = current; scrolls = scrolling }
        return ControllerTarget(id: id, frame: frame, viewport: viewport, selected: selected, scrolls: scrolls,
                                action: action, onFocus: onFocus)
    }
}

/// Draws focus on one target. Every focus change runs in a theme spring (or a short fade
/// with Reduce Motion, which also drops the scale and the sliding row highlight).
private struct ControllerFocusLook: ViewModifier {
    @ObservedObject var focus: ControllerFocus
    let namespace: Namespace.ID
    let id: String
    let style: ControllerFocusStyle
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @ViewBuilder func body(content: Content) -> some View {
        let on = focus.shown && focus.selected == id
        switch style {
        case .own:
            content
        case .lift(let shape, let inset):
            let outline = ControllerFocusOutline(shape: shape)
            content
                .background {
                    if on {
                        outline.fill(SatchelTheme.shell)
                            .shadow(color: .black.opacity(0.6), radius: 18, y: 12)
                            .padding(inset).transition(.opacity)
                    }
                }
                .overlay {
                    if on {
                        ZStack {
                            outline.stroke(SatchelTheme.tide.opacity(0.5), lineWidth: 6).blur(radius: 7)
                            outline.strokeBorder(LinearGradient(colors: [.white, .white.opacity(0.55)],
                                                                startPoint: .top, endPoint: .bottom), lineWidth: 2)
                        }
                        .padding(inset).transition(.opacity)
                    }
                }
                .scaleEffect(on && !reduceMotion ? 1.05 : 1)
                .zIndex(on ? 1 : 0)
        case .row(let shape, let inset):
            let outline = ControllerFocusOutline(shape: shape)
            content
                .background {
                    if on {
                        outline.fill(Color.white.opacity(0.10))
                            .overlay { outline.strokeBorder(Color.white.opacity(0.10)) }
                            .padding(inset)
                            .modifier(SlidingFocus(namespace: namespace, active: !reduceMotion))
                            .transition(.opacity)
                    }
                }
                .overlay {
                    // Lightens a row that has its own fill (the selected Settings section).
                    if on { outline.fill(Color.white.opacity(0.05)).blendMode(.plusLighter).padding(inset).transition(.opacity) }
                }
        case .button(let shape, let inset):
            let outline = ControllerFocusOutline(shape: shape)
            content
                .background {
                    if on {
                        outline.fill(Color.white.opacity(0.12))
                            .background { outline.fill(Color.black.opacity(0.45)).blur(radius: 10).offset(y: 6) }
                            .padding(inset).transition(.opacity)
                    }
                }
                .overlay {
                    if on {
                        ZStack {
                            outline.fill(Color.white.opacity(0.14)).blendMode(.plusLighter)
                            outline.strokeBorder(LinearGradient(colors: [.white.opacity(0.8), .white.opacity(0.15)],
                                                                startPoint: .top, endPoint: .bottom), lineWidth: 1.5)
                        }
                        .padding(inset).transition(.opacity)
                    }
                }
                .scaleEffect(on && !reduceMotion ? 1.05 : 1)
                .zIndex(on ? 1 : 0)
        }
    }
}
/// One row highlight per scope that slides between rows.
private struct SlidingFocus: ViewModifier {
    let namespace: Namespace.ID
    let active: Bool
    @ViewBuilder func body(content: Content) -> some View {
        if active { content.matchedGeometryEffect(id: "controller-focus-row", in: namespace) } else { content }
    }
}

/// "A Select  B Back", in the bottom corner for a few seconds each time controller focus
/// appears, so it teaches the buttons without covering content while the player navigates.
private struct ControllerHints: View {
    let back: Bool
    @ObservedObject private var glyphs = ControllerGlyphs.shared
    var body: some View {
        HStack(spacing: 14) {
            hint(glyphs.accept, "Select")
            if back { hint(glyphs.back, "Back") }
        }
        .font(.system(size: 12, weight: .bold, design: .rounded))
        .foregroundStyle(SatchelTheme.text)
        .padding(.horizontal, 11).frame(height: 28)
        .background(SatchelTheme.surfaceHigh, in: Capsule())
        .overlay { Capsule().strokeBorder(SatchelTheme.line) }
        .shadow(color: .black.opacity(0.35), radius: 10, y: 4)
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
    private func hint(_ symbol: String, _ text: String) -> some View {
        HStack(spacing: 5) {
            Image(systemName: symbol).font(.system(size: 16, weight: .semibold))
            Text(text)
        }
    }
}

private struct ControllerNavigation: ViewModifier {
    var enabled: Bool
    var back: (() -> Void)?
    @StateObject private var focus = ControllerFocus()
    @Namespace private var namespace
    @State private var targets: [ControllerTarget] = []
    @State private var onScreen = false
    @State private var hinting = false
    @State private var hintRound = 0
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    private var motion: Animation { reduceMotion ? .easeInOut(duration: 0.16) : SatchelTheme.snap }

    func body(content: Content) -> some View {
        ScrollViewReader { proxy in
        content.environment(\.controllerFocus, ControllerFocusContext(focus: focus, namespace: namespace))
#if PREVIEW_GAME
            .overlay(alignment: .topLeading) {
                if enabled && UserDefaults.standard.bool(forKey: "SatchelPreviewControllerTests") {
                    Text(focus.selected ?? "none").font(.system(size: 1)).allowsHitTesting(false)
                        .accessibilityIdentifier("controller-focus")
                    Text(focus.shown ? "shown" : "hidden").font(.system(size: 1)).allowsHitTesting(false)
                        .accessibilityIdentifier("controller-focus-shown")
                }
            }
#endif
            .overlay(alignment: .bottomTrailing) {
                if enabled && focus.shown && hinting {
                    ControllerHints(back: back != nil)
                        .padding(.trailing, 14).padding(.bottom, 6)
                        .transition(.opacity)
                }
            }
            .onPreferenceChange(ControllerTargets.self) { targets = $0 }
            .onAppear {
                onScreen = true
                ControllerInputMonitor.shared.start()
                ControllerInputMonitor.shared.watchTouches()
            }
            .onDisappear { onScreen = false; focus.selected = nil; focus.shown = false }
            // Covered by a sheet or dialog, focus hides and comes back where it was.
            .onChange(of: enabled) { _, _ in hide() }
            .onReceive(ControllerInputMonitor.shared.touches) { _ in hide() }
            .onReceive(ControllerInputMonitor.shared.events) { event in handle(event, proxy) }
            // Targets belong only to this scope, never to a presenting library underneath it.
            .preference(key: ControllerTargets.self, value: [])
        }
    }

    private func hide() {
        guard focus.shown else { return }
        withAnimation(.easeOut(duration: 0.18)) { focus.shown = false }
    }

    private func handle(_ event: String, _ proxy: ScrollViewProxy) {
        guard enabled && onScreen else { return }
        if event == "back" { back?(); return }
        let ordered = targets.sorted {
            if abs($0.frame.midY - $1.frame.midY) > 30 { return $0.frame.midY < $1.frame.midY }
            return $0.frame.midX < $1.frame.midX
        }
        guard !ordered.isEmpty else { return }
        // The first input (ever, or after a touch) shows focus where it was, and does nothing else.
        guard focus.shown, let current = ordered.first(where: { $0.id == focus.selected }) else {
            show(start(ordered), proxy)
            showHint()
            return
        }
        if event == "accept" { current.action(); return }
        let horizontal = event == "left" || event == "right"
        let positive = event == "right" || event == "down"
        let candidates = ordered.filter {
            let delta = horizontal ? $0.frame.midX - current.frame.midX : $0.frame.midY - current.frame.midY
            return $0.id != current.id && (positive ? delta > 10 : delta < -10)
        }
        let next = candidates.min {
            func score(_ target: ControllerTarget) -> CGFloat {
                let dx = abs(target.frame.midX - current.frame.midX), dy = abs(target.frame.midY - current.frame.midY)
                return horizontal ? dx + dy * 3 : dy + dx * 3
            }
            return score($0) < score($1)
        }
        if let next {
            Haptics.tick()
            show(next, proxy)
        }
    }

    /// Where focus appears: the last focused target, or the library's current tile when the
    /// last one was a tile (touch may have chosen another since), or the screen's main action.
    private func start(_ ordered: [ControllerTarget]) -> ControllerTarget {
        if let last = ordered.first(where: { $0.id == focus.selected }) {
            if last.selected != nil, let current = ordered.first(where: { $0.selected == true }) { return current }
            return last
        }
        return ordered.first(where: { $0.selected == true })
            ?? ordered.first(where: { ["menu-resume", "detail-play", "player-play", "quit-cancel"].contains($0.id) || $0.id.hasPrefix("game-") })
            ?? ordered[0]
    }

    private func showHint() {
        hintRound += 1
        let round = hintRound
        withAnimation(.easeOut(duration: 0.2)) { hinting = true }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
            if hintRound == round { withAnimation(.easeIn(duration: 0.3)) { hinting = false } }
        }
    }

    private func show(_ target: ControllerTarget, _ proxy: ScrollViewProxy) {
        withAnimation(motion) {
            focus.selected = target.id
            focus.shown = true
        }
        target.onFocus?()
        scrollIntoView(target, proxy)
    }

    /// Scrolls only when the target is not fully in view, and then just far enough to leave
    /// a margin for its lift and shadow, so focus moves inside the view never scroll it.
    private func scrollIntoView(_ target: ControllerTarget, _ proxy: ScrollViewProxy) {
        guard target.scrolls, let viewport = target.viewport else { return }
        let margin: CGFloat = 20
        // scrollTo(anchor:) lines up the same unit point of target and viewport; solve for
        // the point that puts the target `margin` inside the edge it crosses.
        func needed(_ low: CGFloat, _ high: CGFloat, _ start: CGFloat, _ end: CGFloat) -> CGFloat? {
            if low >= start + margin && high <= end - margin { return nil }
            let room = (end - start) - (high - low)
            guard room > 2 * margin else { return 0.5 }
            return low < start + margin ? margin / room : (room - margin) / room
        }
        func kept(_ low: CGFloat, _ high: CGFloat, _ start: CGFloat, _ end: CGFloat) -> CGFloat {
            let room = (end - start) - (high - low)
            return room > 0 ? min(1, max(0, (low - start) / room)) : 0.5
        }
        let frame = target.frame
        let x = needed(frame.minX, frame.maxX, viewport.minX, viewport.maxX)
        let y = needed(frame.minY, frame.maxY, viewport.minY, viewport.maxY)
        guard x != nil || y != nil else { return }
        let anchor = UnitPoint(x: x ?? kept(frame.minX, frame.maxX, viewport.minX, viewport.maxX),
                               y: y ?? kept(frame.minY, frame.maxY, viewport.minY, viewport.maxY))
        withAnimation(reduceMotion ? .easeInOut(duration: 0.2) : SatchelTheme.glide) { proxy.scrollTo(target.id, anchor: anchor) }
    }
}
extension View {
    /// A controller target. `style` picks how focus shows (see `ControllerFocusStyle`);
    /// `onFocus` runs when controller focus arrives; `action` runs on A.
    func controllerAction(_ id: String, style: ControllerFocusStyle = .row(.rounded(12)),
                          onFocus: (() -> Void)? = nil, action: @escaping () -> Void) -> some View {
        modifier(ControllerAction(id: id, style: style, onFocus: onFocus, action: action))
    }
    func controllerNavigation(enabled: Bool = true, back: (() -> Void)? = nil) -> some View {
        modifier(ControllerNavigation(enabled: enabled, back: back))
    }
}
