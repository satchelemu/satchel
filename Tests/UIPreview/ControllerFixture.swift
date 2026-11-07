import SwiftUI
import GameController

/// Test-only input transport. Buttons pulse a GameController snapshot through production readers.
@MainActor enum ControllerFixture {
    static let controller = GCController.withExtendedGamepad()
    private static var panel: UIView?
    private static var lastPulse: UILabel?
    private static var timer: Timer?
    static func install() {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewControllerTests"), panel == nil,
              let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let window = scene.windows.first(where: { $0.isKeyWindow }) else { return }
        ControllerInputMonitor.shared.synthetic = controller
        ControllerInputMonitor.shared.start()
        let view = ControllerFixturePassthrough(frame: window.bounds)
        view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.backgroundColor = .clear
        let row = UIStackView(); row.axis = .horizontal; row.distribution = .fillEqually
        row.backgroundColor = UIColor.black.withAlphaComponent(0.8)
        for name in ["left", "right", "up", "down", "accept", "back", "home", "plus", "minus", "chord", "stick", "hold", "clear"] {
            let button = UIButton(type: .system)
            button.setTitle(name, for: .normal)
            button.accessibilityIdentifier = "fixture-pad-" + name
            button.addAction(UIAction { _ in pulse(name) }, for: .touchUpInside)
            row.addArrangedSubview(button)
        }
        view.addSubview(row); row.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 80),
            row.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -80),
            row.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -2),
            row.heightAnchor.constraint(equalToConstant: 32)])
        let state = UILabel(); state.font = .systemFont(ofSize: 1); state.textColor = .white
        state.accessibilityIdentifier = "fixture-controls-state"
        view.addSubview(state); state.frame = CGRect(x: 80, y: 2, width: 600, height: 4)
        let last = UILabel(); last.font = .systemFont(ofSize: 1); last.text = "none"
        last.accessibilityIdentifier = "fixture-last-pulse"; last.frame = CGRect(x: 80, y: 6, width: 600, height: 4)
        view.addSubview(last); lastPulse = last
        window.addSubview(view); panel = view
        // Tapping the transport is controller input, not touch use, so focus stays visible.
        ControllerInputMonitor.shared.ignoresTouch = { [weak view] touch in
            guard let view, let target = touch.view else { return false }
            return target.isDescendant(of: view)
        }
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { _ in
            MainActor.assumeIsolated {
                window.bringSubviewToFront(view)
                let menu = SatchelMenuState()
                state.text = String(format: "volume=%.2f opacity=%.2f size=%.2f buttons=%llu",
                    (menu["volume"] as? NSNumber)?.doubleValue ?? 0,
                    (menu["opacity"] as? NSNumber)?.doubleValue ?? 0,
                    (menu["size"] as? NSNumber)?.doubleValue ?? 0, SatchelPreviewGuestButtons())
            }
        }
        NotificationCenter.default.post(name: .GCControllerDidConnect, object: controller)
    }
    static func pulse(_ name: String) {
        lastPulse?.text = name
        guard let pad = controller.extendedGamepad else { return }
        switch name {
        case "clear": release(); SatchelPreviewResetGuestButtons(); return
        case "left": pad.dpad.setValueForXAxis(-1, yAxis: 0)
        case "right": pad.dpad.setValueForXAxis(1, yAxis: 0)
        case "up": pad.dpad.setValueForXAxis(0, yAxis: 1)
        case "down", "hold": pad.dpad.setValueForXAxis(0, yAxis: -1)
        case "accept": pad.buttonA.setValue(1)
        case "back": pad.buttonB.setValue(1)
        case "stick": pad.leftThumbstick.setValueForXAxis(0.8, yAxis: 0.4)
        case "plus": pad.buttonMenu.setValue(1)
        case "minus": pad.buttonOptions?.setValue(1)
        case "chord":
            let category = SatchelControllerCategory(controller)
            var profile = SatchelControllerProfile("", category) as! [String: Any]
            profile["shortcut"] = "menu-options"
            precondition(SatchelSaveControllerProfile("", category, profile))
            pad.buttonMenu.setValue(1); pad.buttonOptions?.setValue(1)
        case "home":
            if let home = pad.buttonHome { home.setValue(1) }
            else { pad.buttonMenu.setValue(1); pad.buttonOptions?.setValue(1) }
        default: break
        }
        // Snapshot mutation is not a hardware event. Deliver the same callback installed
        // by the native host, so tests cover its reader and guest publication as well.
        pad.valueChangedHandler?(pad, pad.buttonA)
        // Long observations use explicit release, so a slow accessibility snapshot cannot
        // outlast the input. Plus/Minus and navigation remain short physical pulses.
        if name == "stick" || name == "chord" || name == "hold" { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + (name == "home" ? 1.2 : 0.15)) { release() }
    }
    private static func release() {
        guard let pad = controller.extendedGamepad else { return }
        pad.dpad.setValueForXAxis(0, yAxis: 0); pad.leftThumbstick.setValueForXAxis(0, yAxis: 0)
        pad.buttonA.setValue(0); pad.buttonB.setValue(0); pad.buttonHome?.setValue(0)
        pad.buttonMenu.setValue(0); pad.buttonOptions?.setValue(0)
        pad.valueChangedHandler?(pad, pad.buttonA)
    }
}
private final class ControllerFixturePassthrough: UIView {
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        subviews.contains { !$0.isHidden && $0.frame.contains(point) }
    }
}
@_cdecl("SatchelPreviewControllers")
func previewControllers() -> NSArray {
    if UserDefaults.standard.bool(forKey: "SatchelPreviewControllerTests") {
        return MainActor.assumeIsolated { [ControllerFixture.controller] as NSArray }
    }
    return GCController.controllers() as NSArray
}
