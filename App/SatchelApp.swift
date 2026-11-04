import SwiftUI

@main
struct SatchelApp: App {
    @UIApplicationDelegateAdaptor(SatchelAppDelegate.self) private var appDelegate
    init() {
        TouchDiagnostics.migrationSuspended = SatchelMigration.bridge
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if !SatchelMigration.bridge { SaveLifecycle.start() }
#endif
    }
    var body: some Scene {
        WindowGroup { SatchelRootView().preferredColorScheme(.dark) }
    }
}
