import UIKit

/// Records where taps land so device-only touch problems can be diagnosed from the Mac
/// (`devicectl ... copy from ... Documents/touch-log.txt`). Keeps the last 300 lines.
/// It observes touches without taking them: the recognizer never begins, cancels or delays.
enum TouchDiagnostics {
    static var migrationSuspended = false
    private static let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("touch-log.txt")
    // Earlier launches are kept so an interruption that ends in a relaunch is still visible.
    private static var lines: [String] = {
        var kept = ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
        kept.append("---- launch")
        return kept
    }()
    private static var installed = Set<ObjectIdentifier>()
    private static var observingLifecycle = false

    static func note(_ text: String) {
        guard !migrationSuspended, UserDefaults.standard.bool(forKey: "SatchelDeveloperControls") else { return }
        let stamp = String(format: "%.3f", Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 100_000))
        lines.append(stamp + " " + text)
        if lines.count > 300 { lines.removeFirst(lines.count - 300) }
        try? lines.joined(separator: "\n").appending("\n").write(to: url, atomically: true, encoding: .utf8)
    }

    /// Called whenever a scene's windows may have changed; attaches one observer per window.
    static func attach(to scene: UIWindowScene) {
        guard !migrationSuspended, UserDefaults.standard.bool(forKey: "SatchelDeveloperControls") else { return }
        if !observingLifecycle {
            observingLifecycle = true
            let center = NotificationCenter.default
            for (name, label) in [(UIScene.willDeactivateNotification, "scene will deactivate"),
                                  (UIScene.didActivateNotification, "scene did activate"),
                                  (UIScene.didEnterBackgroundNotification, "scene did enter background"),
                                  (UIScene.willEnterForegroundNotification, "scene will enter foreground")] {
                center.addObserver(forName: name, object: nil, queue: .main) { _ in
                    MainActor.assumeIsolated { TouchDiagnostics.note(label + " " + TouchDiagnostics.presentationState()) }
                }
            }
        }
        for window in scene.windows where !installed.contains(ObjectIdentifier(window)) {
            installed.insert(ObjectIdentifier(window))
            window.addGestureRecognizer(Observer(window: window))
            note("window \(type(of: window)) bounds=\(window.bounds.size) safe=\(window.safeAreaInsets)")
        }
    }

    /// The top presented controller of every window, to spot a presentation that SwiftUI
    /// believes is open while UIKit has none (or the reverse).
    static func presentationState() -> String {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        return windows.map { window -> String in
            var top = window.rootViewController
            while let presented = top?.presentedViewController { top = presented }
            return "[\(type(of: window)) key=\(window.isKeyWindow) top=\(top.map { String(describing: type(of: $0)) } ?? "nil")]"
        }.joined(separator: " ")
    }

    private final class Observer: UIGestureRecognizer, UIGestureRecognizerDelegate {
        weak var window: UIWindow?
        init(window: UIWindow) {
            self.window = window
            super.init(target: nil, action: nil)
            cancelsTouchesInView = false
            delaysTouchesBegan = false
            delaysTouchesEnded = false
            delegate = self
        }
        func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }
        override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
            guard let window, let touch = touches.first else { return }
            let point = touch.location(in: window)
            var chain: [String] = []
            var view = window.hitTest(point, with: event)
            while let current = view, chain.count < 6 { chain.append(String(describing: type(of: current))); view = current.superview }
            var top = window.rootViewController
            while let presented = top?.presentedViewController { top = presented }
            let screen = window.windowScene?.screen.bounds.size ?? .zero
            TouchDiagnostics.note("tap x=\(Int(point.x)) y=\(Int(point.y)) window=\(window.bounds.size) screen=\(screen) safe=\(window.safeAreaInsets) top=\(top.map { String(describing: type(of: $0)) } ?? "nil") hit=\(chain.joined(separator: "<"))")
            state = .failed
        }
    }
}
