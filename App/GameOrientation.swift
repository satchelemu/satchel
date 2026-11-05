import UIKit

@MainActor
enum GameOrientation {
    // Landscape on every screen, matching the launch-time Info.plist restriction, so a launch
    // never flashes upright. The one exception is the host's rule (NativeHost/DisplayControls.h):
    // a game on a half-folded Duo turns upright, picture on the top half and controls on the
    // bottom half. The delegates below answer at run time, so the Info.plist stays as it is.
    static var supportedMask: UIInterfaceOrientationMask {
        #if SATCHEL_UI_PREVIEW_MINIMAL
        return .landscape
        #else
        return SatchelGameOrientationMask()
        #endif
    }

    /// iOS 26 and later show a landscape-only app as a small window when the phone is turned
    /// upright (UIRequiresFullScreen is ignored). Satchel stays landscape instead: every view
    /// controller prefers the scene's orientation lock, which the system grants while the scene
    /// fills the screen. SwiftUI's own hosting controllers cannot override the property, so the
    /// base implementation answers for all of them.
    static func lockOrientation() {
        guard #available(iOS 26.0, *) else { return }
        let selector = #selector(getter: UIViewController.prefersInterfaceOrientationLocked)
        guard let method = class_getInstanceMethod(UIViewController.self, selector) else { return }
        // The lock pins the scene to the orientation it is in and refuses a turn, so it is
        // released while the scene follows the host's rule (follow) and taken again on arrival.
        let locked: @convention(block) (UIViewController) -> Bool = { _ in MainActor.assumeIsolated { !turning } }
        method_setImplementation(method, imp_implementationWithBlock(locked))
    }

    private static var turning = false

    private static func updateLock(_ scene: UIWindowScene) {
        guard #available(iOS 26.0, *) else { return }
        for window in scene.windows {
            var controller = window.rootViewController
            while let current = controller {
                current.setNeedsUpdateOfPrefersInterfaceOrientationLocked()
                controller = current.presentedViewController
            }
        }
    }

    /// Turns the scene to the supported orientation: landscape if it was opened while the phone
    /// was upright, and to or from the half-folded game's upright layout when the host's rule changes.
    static func follow(_ scene: UIWindowScene) {
        #if SATCHEL_UI_PREVIEW_MINIMAL
        updateLock(scene)
        guard #available(iOS 26.0, *), !scene.effectiveGeometry.interfaceOrientation.isLandscape else { return }
        scene.requestGeometryUpdate(.iOS(interfaceOrientations: supportedMask)) { _ in }
        #else
        turn(scene, tries: 10)
        #endif
    }

    #if !SATCHEL_UI_PREVIEW_MINIMAL
    private static func turn(_ scene: UIWindowScene, tries: Int) {
        let now = scene.effectiveGeometry.interfaceOrientation
        let arrived = supportedMask == .landscape ? now.isLandscape : now.isPortrait
        guard !arrived, tries > 0 else {
            turning = false
            updateLock(scene)
            return
        }
        turning = true
        updateLock(scene)
        SatchelFollowGameOrientation(scene)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { turn(scene, tries: tries - 1) }
    }
    #endif

    /// Follows the host's rule on every app scene as it changes.
    static func followGame() {
        #if !SATCHEL_UI_PREVIEW_MINIMAL
        NotificationCenter.default.addObserver(forName: .SatchelGameOrientationDidChange, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated {
                for case let scene as UIWindowScene in UIApplication.shared.connectedScenes where scene.session.role == .windowApplication {
                    follow(scene)
                }
            }
        }
        #endif
    }
}

final class SatchelAppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        MainActor.assumeIsolated { GameOrientation.lockOrientation(); GameOrientation.followGame() }
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
        MainActor.assumeIsolated { GameOrientation.follow(scene) }
    }

    @available(iOS 27.0, *)
    func supportedInterfaceOrientations(for windowScene: UIWindowScene) -> UIInterfaceOrientationMask {
        GameOrientation.supportedMask
    }
}
