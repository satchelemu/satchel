import SwiftUI
import GameController
import Combine

/// Polling leaves the engine's valueChangedHandler intact. Only edges are sent to UI scopes.
@MainActor final class ControllerInputMonitor: ObservableObject {
    static let shared = ControllerInputMonitor()
    @Published var values: [String: NSNumber] = [:]
    @Published var category = "Generic"
    @Published var connected = false
    let events = PassthroughSubject<String, Never>()
    private var timer: Timer?
    private var states: [ObjectIdentifier: NSMutableDictionary] = [:]
#if PREVIEW_GAME
    var synthetic: GCController?
#endif
    func start() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.poll() }
        }
    }
    private func poll() {
        guard UIApplication.shared.applicationState == .active else {
            states.removeAll(); values = [:]; return
        }
        var controllers = GCController.controllers().filter { $0.extendedGamepad != nil }
#if PREVIEW_GAME
        if let synthetic { controllers = [synthetic] }
#endif
        connected = !controllers.isEmpty
        let live = Set(controllers.map { ObjectIdentifier($0) })
        states = states.filter { live.contains($0.key) }
        if controllers.isEmpty { values = [:] }
        for (index, controller) in controllers.enumerated() {
            guard let pad = controller.extendedGamepad else { continue }
            let input = SatchelControllerValues(pad)
            if index == 0 { values = input; category = SatchelControllerCategory(controller) }
            let id = ObjectIdentifier(controller)
            let hadState = states[id] != nil
            let state = states[id] ?? NSMutableDictionary()
            states[id] = state
            let x = abs(pad.dpad.xAxis.value) > abs(pad.leftThumbstick.xAxis.value) ? pad.dpad.xAxis.value : pad.leftThumbstick.xAxis.value
            let y = abs(pad.dpad.yAxis.value) > abs(pad.leftThumbstick.yAxis.value) ? pad.dpad.yAxis.value : pad.leftThumbstick.yAxis.value
            // UI uses physical bottom/right positions, independent of gameplay remapping.
            let labelled = SatchelControllerCategory(controller) == "Pro Controller"
            let event = SatchelControllerEdge(state, x, y, labelled ? pad.buttonB.isPressed : pad.buttonA.isPressed,
                                             labelled ? pad.buttonA.isPressed : pad.buttonB.isPressed)
            if hadState && !event.isEmpty { events.send(event) }
        }
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
