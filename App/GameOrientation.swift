import UIKit

@MainActor
enum GameOrientation {
    // Match the launch-time Info.plist restriction for every screen. Changing
    // orientation only when presenting a game causes a portrait launch flash
    // and a narrow compatibility window on the unfolded Duo.
    static let supportedMask: UIInterfaceOrientationMask = .landscape

    /// iOS 26 and later show a landscape-only app as a small window when the phone is turned
    /// upright (UIRequiresFullScreen is ignored). Satchel stays landscape instead: every view
    /// controller prefers the scene's orientation lock, which the system grants while the scene
    /// fills the screen. SwiftUI's own hosting controllers cannot override the property, so the
    /// base implementation answers for all of them.
    static func lockOrientation() {
        guard #available(iOS 26.0, *) else { return }
        let selector = #selector(getter: UIViewController.prefersInterfaceOrientationLocked)
        guard let method = class_getInstanceMethod(UIViewController.self, selector) else { return }
        let locked: @convention(block) (UIViewController) -> Bool = { _ in true }
        method_setImplementation(method, imp_implementationWithBlock(locked))
    }

    /// Turns the scene to landscape if it was opened while the phone was upright.
    static func requestLandscape(_ scene: UIWindowScene) {
        guard #available(iOS 26.0, *) else { return }
        scene.keyWindow?.rootViewController?.setNeedsUpdateOfPrefersInterfaceOrientationLocked()
        guard !scene.effectiveGeometry.interfaceOrientation.isLandscape else { return }
        scene.requestGeometryUpdate(.iOS(interfaceOrientations: supportedMask)) { _ in }
    }
}

final class SatchelAppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        MainActor.assumeIsolated { GameOrientation.lockOrientation() }
        // Touch recorder for device-only input problems (App/TouchDiagnostics.swift).
        NotificationCenter.default.addObserver(forName: UIWindow.didBecomeVisibleNotification, object: nil, queue: .main) { note in
            MainActor.assumeIsolated {
                if let scene = (note.object as? UIWindow)?.windowScene { TouchDiagnostics.attach(to: scene) }
            }
        }
        return true
    }

    func application(_ application: UIApplication, configurationForConnecting session: UISceneSession,
                     options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(name: nil, sessionRole: session.role)
        if session.role == .windowApplication { configuration.delegateClass = SatchelWindowSceneDelegate.self }
        // A TV by cable or AirPlay: the pinned engine's front end (front_ios.m DmTVSceneDelegate,
        // registered as an iOS 27 scene accessory once a game starts) moves the picture there and
        // keeps the controls on the phone. Unvalidated on hardware, so only with Developer controls.
        if session.role == .windowExternalDisplayNonInteractive,
           UserDefaults.standard.bool(forKey: "SatchelDeveloperControls"),
           let tv = NSClassFromString("DmTVSceneDelegate") {
            configuration.delegateClass = tv
        }
        return configuration
    }

    // Compatibility for iOS 17–26. iOS 27 uses the window-scene callback below.
    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        GameOrientation.supportedMask
    }
}

final class SatchelWindowSceneDelegate: NSObject, UIWindowSceneDelegate {
    func sceneDidBecomeActive(_ scene: UIScene) {
        guard let scene = scene as? UIWindowScene else { return }
        MainActor.assumeIsolated { GameOrientation.requestLandscape(scene) }
    }

    @available(iOS 27.0, *)
    func supportedInterfaceOrientations(for windowScene: UIWindowScene) -> UIInterfaceOrientationMask {
        GameOrientation.supportedMask
    }
}
