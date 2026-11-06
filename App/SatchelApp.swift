import SwiftUI

@main
struct SatchelApp: App {
    @UIApplicationDelegateAdaptor(SatchelAppDelegate.self) private var appDelegate
    init() {
        TouchDiagnostics.migrationSuspended = SatchelMigration.bridge
        // Controllers that were connected before launch are reported asynchronously;
        // observe from launch so every screen and the diagnostics see them.
        ControllerInputMonitor.shared.start()
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if !SatchelMigration.bridge { SaveLifecycle.start() }
#endif
    }
    var body: some Scene {
        WindowGroup { SatchelRootView().preferredColorScheme(.dark) }
    }
}
