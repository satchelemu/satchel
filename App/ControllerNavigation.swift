import SwiftUI
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
    private var timer: Timer?
    private var states: [ObjectIdentifier: NSMutableDictionary] = [:]
    private var changes: [ObjectIdentifier: Int] = [:]
    private var lastPressed: [ObjectIdentifier: String] = [:]
    private var diagnosticViewers = 0
#if PREVIEW_GAME
    var synthetic: GCController?
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
            }
            let hadState = states[id] != nil
            let state = states[id] ?? NSMutableDictionary()
            states[id] = state
            let input = Self.navigation(controller)
            let event = SatchelControllerEdge(state, input.x, input.y, input.accept, input.back)
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

private struct ControllerTarget: Equatable {
    let id: String
    let frame: CGRect
    let action: () -> Void
    static func == (lhs: Self, rhs: Self) -> Bool { lhs.id == rhs.id && lhs.frame == rhs.frame }
}
private struct ControllerTargets: PreferenceKey {
    static var defaultValue: [ControllerTarget] = []
    static func reduce(value: inout [ControllerTarget], nextValue: () -> [ControllerTarget]) { value += nextValue() }
}
@MainActor private final class ControllerFocus: ObservableObject { @Published var selected: String? }
private struct ControllerFocusKey: EnvironmentKey { static let defaultValue: ControllerFocus? = nil }
private extension EnvironmentValues {
    var controllerFocus: ControllerFocus? {
        get { self[ControllerFocusKey.self] }
        set { self[ControllerFocusKey.self] = newValue }
    }
}
private struct ControllerAction: ViewModifier {
    let id: String
    let action: () -> Void
    @Environment(\.controllerFocus) private var focus
    @Environment(\.isEnabled) private var enabled
    func body(content: Content) -> some View {
        content.id(id).background {
            if enabled {
                GeometryReader { proxy in
                    Color.clear.preference(key: ControllerTargets.self,
                        value: [.init(id: id, frame: proxy.frame(in: .global), action: action)])
                }
            }
        }
        .overlay { if let focus { ControllerFocusRing(focus: focus, id: id) } }
    }
}
private struct ControllerFocusRing: View {
    @ObservedObject var focus: ControllerFocus
    let id: String
    var body: some View {
        RoundedRectangle(cornerRadius: 12).stroke(focus.selected == id ? SatchelTheme.spark : .clear, lineWidth: 3)
            .allowsHitTesting(false).accessibilityHidden(true)
    }
}
private struct ControllerNavigation: ViewModifier {
    var enabled: Bool
    var back: () -> Void
    @StateObject private var focus = ControllerFocus()
    @State private var targets: [ControllerTarget] = []
    @State private var visible = false
    func body(content: Content) -> some View {
        ScrollViewReader { proxy in
        content.environment(\.controllerFocus, focus)
#if PREVIEW_GAME
            .overlay(alignment: .topLeading) {
                if enabled && UserDefaults.standard.bool(forKey: "SatchelPreviewControllerTests") {
                    Text(focus.selected ?? "none").font(.system(size: 1)).allowsHitTesting(false)
                        .accessibilityIdentifier("controller-focus")
                }
            }
#endif
            .onPreferenceChange(ControllerTargets.self) { targets = $0 }
            .onAppear { visible = true; ControllerInputMonitor.shared.start() }
            .onDisappear { visible = false; focus.selected = nil }
            .onChange(of: focus.selected) { _, id in
                if let id { withAnimation { proxy.scrollTo(id, anchor: .center) } }
            }
            .onChange(of: enabled) { _, _ in focus.selected = nil }
            .onReceive(ControllerInputMonitor.shared.events) { event in
                guard enabled && visible else { return }
                if event == "back" { back(); return }
                let ordered = targets.sorted {
                    if abs($0.frame.midY - $1.frame.midY) > 30 { return $0.frame.midY < $1.frame.midY }
                    return $0.frame.midX < $1.frame.midX
                }
                guard !ordered.isEmpty else { return }
                guard let current = ordered.first(where: { $0.id == focus.selected }) else {
                    focus.selected = ordered.first(where: { ["menu-resume", "detail-play", "player-play", "quit-cancel"].contains($0.id) || $0.id.hasPrefix("game-") })?.id ?? ordered.first?.id
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
                if let next { focus.selected = next.id }
            }
            // Targets belong only to this scope, never to a presenting library underneath it.
            .preference(key: ControllerTargets.self, value: [])
        }
    }
}
extension View {
    func controllerAction(_ id: String, action: @escaping () -> Void) -> some View {
        modifier(ControllerAction(id: id, action: action))
    }
    func controllerNavigation(enabled: Bool = true, back: @escaping () -> Void = {}) -> some View {
        modifier(ControllerNavigation(enabled: enabled, back: back))
    }
}
