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
