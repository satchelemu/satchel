import SwiftUI
import UIKit

/// Connects Satchel's SwiftUI pause menu (`App/InGameMenuView.swift`) to the native menu
/// bridge (`NativeHost/display/MENU.md`). The native host keeps owning pause, persistence and
/// the session; this file only translates between the two contracts.
@MainActor
final class NativeSessionControl: InGameSessionControl {
    let gameTitle: String
    let titleID: String
    private let bridge = NativeMenuBridge()
    private var applied: InGameSettings?

    init(gameTitle: String, titleID: String) {
        self.gameTitle = gameTitle
        self.titleID = titleID
    }

    // The engine's heat cap is 30 FPS (InGameMenu.m thermalMessage).
    var thermalFrameCap: Int? { (bridge.snapshot["thermalLimited"] as? Bool) == true ? 30 : nil }
    // The host shows its own FPS counter but does not publish a measured value.
    var framesPerSecond: Double? { nil }

    var currentSettings: InGameSettings {
        let state = SatchelMenuState()
        var settings = InGameSettings.load(titleID: titleID)
        settings.screenMode = SatchelScreenMode() == 1 ? .stretch : .fit
        settings.showFPS = (state["fps"] as? Bool) ?? settings.showFPS
        settings.haptics = (state["haptics"] as? Bool) ?? settings.haptics
        settings.touchOpacity = (state["opacity"] as? Double) ?? settings.touchOpacity
        settings.touchSize = (state["size"] as? Double) ?? settings.touchSize
        settings.volume = (state["volume"] as? Double) ?? settings.volume
        return settings
    }

    func apply(_ settings: InGameSettings) {
        let old = applied
        applied = settings
        func changed<T: Equatable>(_ key: KeyPath<InGameSettings, T>) -> Bool { old?[keyPath: key] != settings[keyPath: key] }
        if changed(\.screenMode) { bridge.perform("screen", value: settings.screenMode.rawValue) }
        if changed(\.showFPS) { bridge.perform("fps", value: settings.showFPS ? "on" : "off") }
        if changed(\.haptics) { bridge.perform("haptics", value: settings.haptics ? "on" : "off") }
        if changed(\.touchOpacity) { bridge.perform("opacity", value: Self.decimal(settings.touchOpacity)) }
        if changed(\.touchSize) { bridge.perform("size", value: Self.decimal(settings.touchSize)) }
        if changed(\.volume) { bridge.perform("volume", value: Self.decimal(settings.volume)) }
    }

    func resume() { bridge.perform("resume") }
    func pauseAndReturnToLibrary() { bridge.perform("library") }
    // The menu already confirmed; the bridge only quits from its confirm state.
    func quit() { bridge.perform("quit"); bridge.perform("confirm-quit") }

    private static func decimal(_ value: Double) -> String { String(format: "%.2f", value) }
}

/// Shows Satchel's SwiftUI menu while the bridge says the game is paused. The menu is a
/// presented controller (like the app's panels), so iOS gives it the status bar and the
/// vertical bar with its close button. A transparent placeholder takes the bridge's
/// custom-view slot so the plain menu stays hidden; if the presentation fails, the
/// placeholder is removed and the bridge's plain menu shows, so a paused game is never
/// left behind nothing.
@MainActor
enum SatchelMenu {
    private static var model: InGameMenuModel?
    private static var presented: UIViewController?
    private static var observer: NSObjectProtocol?
    private static let placeholder: UIView = {
        let view = UIView()
        view.backgroundColor = .clear
        view.isUserInteractionEnabled = false
        view.accessibilityIdentifier = "menu.presented-placeholder"
        return view
    }()

    static func install(titleID: String) {
        // Device validation can exercise the same native fallback without causing
        // an artificial presentation failure. Normal users keep the SwiftUI menu.
        if UserDefaults.standard.bool(forKey: "SatchelDeveloperControls") &&
            UserDefaults.standard.bool(forKey: "SatchelPlainNativeMenu") {
            uninstall(); return
        }
        guard model == nil, !titleID.isEmpty else { return }
        let name = NativeGameProfile.find(titleID: titleID)?.name ?? titleID
        model = InGameMenuModel(session: NativeSessionControl(gameTitle: name, titleID: titleID))
        SatchelMenuSetCustomView(placeholder)
        observer = NotificationCenter.default.addObserver(forName: .SatchelMenuDidChange, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { sync() }
        }
        sync()
    }

    static func uninstall() {
        dismiss()
        if let observer { NotificationCenter.default.removeObserver(observer) }
        observer = nil; model = nil
        SatchelMenuSetCustomView(nil)
    }

    private static var menuState: String { SatchelMenuState()["state"] as? String ?? "running" }
    private static var wantsMenu: Bool { ["menu", "confirmQuit"].contains(menuState) }

    private static func sync() {
        if wantsMenu { present() }
        else if menuState != "opening" {
            dismiss()
            if ["quitting", "stopFailed"].contains(menuState) { SatchelMenuSetCustomView(nil) }
        }
    }

    private static func present(attempt: Int = 0) {
        guard presented == nil, let model else { return }
        // Back in the slot after an earlier fallback.
        if placeholder.superview == nil { SatchelMenuSetCustomView(placeholder) }
        guard let presenter = topController() else {
            // Reopening a paused game attaches the surface before its controller reaches a
            // window. Wait up to a second for the window before showing the plain menu.
            guard attempt < 20 else { return fallBack("no controller to present from") }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
                if wantsMenu { present(attempt: attempt + 1) }
            }
            return
        }
        let controller = MenuHostingController(rootView: InGameMenuScreen(model: model))
        controller.modalPresentationStyle = .overFullScreen
        controller.modalTransitionStyle = .crossDissolve
        controller.modalPresentationCapturesStatusBarAppearance = true
        controller.view.backgroundColor = .clear
        presented = controller
        fputs("Satchel menu: presenting Satchel menu from \(type(of: presenter))\n", stderr)
        presenter.present(controller, animated: true) {
            if !wantsMenu { dismiss() }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            guard presented === controller, controller.view.window == nil else { return }
            fallBack("the presented menu did not reach a window")
        }
    }

    private static func dismiss() {
        guard let controller = presented else { return }
        presented = nil
        if controller.presentingViewController != nil { controller.dismiss(animated: menuState == "running") }
    }

    private static func fallBack(_ reason: String) {
        fputs("Satchel menu: Satchel menu unavailable (\(reason)); showing the plain menu\n", stderr)
        presented = nil
        SatchelMenuSetCustomView(nil)
    }

    /// The front-most controller in the game's window.
    private static func topController() -> UIViewController? {
        var top = SatchelNativeGameController().view.window?.rootViewController
        while let next = top?.presentedViewController, !next.isBeingDismissed { top = next }
        return top
    }
}

/// Hosts the pause menu with the status bar shown over the game.
final class MenuHostingController: UIHostingController<InGameMenuScreen> {
    override var prefersStatusBarHidden: Bool { false }
    override var preferredStatusBarStyle: UIStatusBarStyle { .lightContent }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }
}
