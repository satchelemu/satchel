import SwiftUI

/// Simulator-only foreground peer for lifecycle tests. No storage or networking.
@main
struct NavigationBackgroundApp: App {
    @UIApplicationDelegateAdaptor(SatchelAppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            Color.black.overlay {
                Text("Navigation background fixture")
                    .foregroundStyle(.white)
                    .accessibilityIdentifier("navigation-background")
            }.ignoresSafeArea()
        }
    }
}

// The shared orientation delegate references diagnostics; this peer has no recorder.
enum TouchDiagnostics {
    static func attach(to scene: UIWindowScene) {}
}

// The shared orientation delegate follows the host's rule (NativeHost/DisplayControls.h).
// This peer has no game, so the rule always answers landscape.
func SatchelGameOrientationMask() -> UIInterfaceOrientationMask { .landscape }
func SatchelFollowGameOrientation(_ scene: UIWindowScene) {
    guard #available(iOS 26.0, *), !scene.effectiveGeometry.interfaceOrientation.isLandscape else { return }
    scene.requestGeometryUpdate(.iOS(interfaceOrientations: .landscape)) { _ in }
}
extension Notification.Name {
    static let SatchelGameOrientationDidChange = Notification.Name("SatchelGameOrientationDidChange")
}
