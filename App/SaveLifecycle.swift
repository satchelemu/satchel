import Foundation
import UIKit

#if !SATCHEL_UI_PREVIEW_MINIMAL
extension SaveStore {
    static let native = SaveStore(
        root: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("NativeRuntime"),
        beginAccess: { title, allowRunning in
            guard SatchelBeginSaveAccess(title, allowRunning) else {
                throw SaveFailure(message: "Quit this game to home before exporting or restoring its save. A paused game is still running.")
            }
        }, endAccess: { SatchelEndSaveAccess() }, clone: { source, destination in
            var error: NSError?
            guard SatchelCloneCommittedSave(source.path, destination.path, &error) else {
                if let error { throw error }
                throw SaveFailure(message: "Could not snapshot the committed save.")
            }
        })
}

@MainActor
enum SaveLifecycle {
    static let changed = Notification.Name("SatchelSaveBackupsChanged")
    private static var observers: [NSObjectProtocol] = []
    private static var pending = Set<String>()
    private static var repeatRequested = Set<String>()
    @MainActor private final class BackgroundWork {
        var task: Task<String?, Never>?
        var identifier: UIBackgroundTaskIdentifier = .invalid
        func end() {
            if identifier != .invalid { UIApplication.shared.endBackgroundTask(identifier); identifier = .invalid }
        }
    }
    static func error(title: String) -> String? { UserDefaults.standard.string(forKey: "SatchelSaveBackupError." + title) }

    static func start() {
        guard observers.isEmpty else { return }
        // These files are transient picker exports from an earlier process.
        let abandoned = ((try? FileManager.default.contentsOfDirectory(at: FileManager.default.temporaryDirectory,
            includingPropertiesForKeys: nil)) ?? []).filter { $0.lastPathComponent.hasPrefix("SatchelSaveExport-") }
        Task.detached(priority: .utility) {
            for url in abandoned { try? FileManager.default.removeItem(at: url) }
        }
        observers.append(NotificationCenter.default.addObserver(forName: .SatchelNativeSessionDidEnd, object: nil, queue: .main) { notification in
            guard let title = notification.userInfo?["titleID"] as? String, !title.isEmpty else { return }
            MainActor.assumeIsolated { schedule(title) }
        })
        for name in [UIApplication.didEnterBackgroundNotification, UIScene.didEnterBackgroundNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { _ in
                guard let title = SatchelNativeSaveTitle() else { return }
                MainActor.assumeIsolated { schedule(title) }
            })
        }
    }

    private static func schedule(_ title: String) {
        guard pending.insert(title).inserted else { repeatRequested.insert(title); return }
        let work = BackgroundWork()
        work.identifier = UIApplication.shared.beginBackgroundTask(withName: "Save backup") {
            work.task?.cancel(); work.end()
        }
        work.task = Task.detached(priority: .utility) {
            do { _ = try SaveStore.native.backupIfChanged(title: title); return nil }
            catch is CancellationError { return "Automatic backup was interrupted. Quit the game or background Satchel again to retry." }
            catch { return "Automatic backup failed: " + error.localizedDescription }
        }
        Task {
            let message = await work.task?.value
            UserDefaults.standard.set(message ?? nil, forKey: "SatchelSaveBackupError." + title)
            pending.remove(title)
            work.end()
            NotificationCenter.default.post(name: changed, object: title)
            if repeatRequested.remove(title) != nil { schedule(title) }
        }
    }
}
#endif
