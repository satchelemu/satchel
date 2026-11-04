#if os(iOS)
import BackgroundTasks
import UIKit

/// Keeps an import going when the user leaves the app, the way Prepare does
/// (`NativePrepare/PrepareJob.swift`): a UIKit background task covers the start, then a
/// continued processing task owns the background lifetime and shows its progress in the
/// system. When iOS declines one, the import runs on its queue as before.
enum ImportLifetime {
    /// Feeds the system's progress display. Safe to call from the import worker.
    final class Reporter: @unchecked Sendable {
        private let lock = NSLock()
        private let name: String
        private var task: BGTask?
        private var shown = ""

        fileprivate init(name: String) { self.name = name }
        fileprivate func attach(_ task: BGTask) { lock.lock(); self.task = task; lock.unlock() }

        func update(fraction: Double?, stage: String) {
            guard #available(iOS 26.0, *) else { return }
            lock.lock(); defer { lock.unlock() }
            guard let continued = task as? BGContinuedProcessingTask else { return }
            if let fraction { continued.progress.completedUnitCount = Int64(min(1, max(0, fraction)) * 1_000_000) }
            let subtitle = fraction.map { "\(stage) · \(Int($0 * 100))%" } ?? stage
            guard subtitle != shown else { return }
            shown = subtitle
            continued.updateTitle("Importing \(name)", subtitle: subtitle)
        }
    }

    /// The UIKit background task, ended once by whichever path finishes first. Main thread only.
    private final class Keepalive: @unchecked Sendable {
        var identifier: UIBackgroundTaskIdentifier = .invalid
        func end() {
            if identifier != .invalid { UIApplication.shared.endBackgroundTask(identifier); identifier = .invalid }
        }
    }

    /// Runs `work` (which returns whether the import succeeded) with a background lifetime.
    /// `stop` cancels the import; it runs when iOS ends the time it gave. Call on the main thread.
    static func run(name: String, queue: DispatchQueue, stop: @escaping () -> Void,
                               work: @escaping (Reporter) -> Bool) {
        let keepalive = Keepalive()
        let reporter = Reporter(name: name)
        keepalive.identifier = UIApplication.shared.beginBackgroundTask(withName: "Import game") {
            stop(); keepalive.end()
        }
        let foreground = {
            queue.async {
                _ = work(reporter)
                DispatchQueue.main.async { keepalive.end() }
            }
        }
        guard #available(iOS 26.0, *) else { foreground(); return }
        // The `.prepare.` prefix is the identifier family Info.plist already permits.
        let identifier = (Bundle.main.bundleIdentifier ?? "com.dell.satchel") + ".prepare.import-" + UUID().uuidString
        let registered = BGTaskScheduler.shared.register(forTaskWithIdentifier: identifier, using: nil) { task in
            // The continued task owns background lifetime once accepted.
            DispatchQueue.main.async { keepalive.end() }
            reporter.attach(task)
            if let continued = task as? BGContinuedProcessingTask { continued.progress.totalUnitCount = 1_000_000 }
            task.expirationHandler = { stop() }
            let success = work(reporter)
            if success, let continued = task as? BGContinuedProcessingTask {
                continued.progress.completedUnitCount = continued.progress.totalUnitCount
            }
            task.setTaskCompleted(success: success)
        }
        guard registered else { foreground(); return }
        let request = BGContinuedProcessingTaskRequest(identifier: identifier, title: "Importing \(name)",
                                                       subtitle: "Satchel keeps importing while you use other apps.")
        request.strategy = .fail
        // Declined (for example under heavy load): run in the foreground as before.
        if #available(iOS 27.0, *) {
            BGTaskScheduler.shared.submitTaskRequest(request) { error in
                if error != nil { DispatchQueue.main.async { foreground() } }
            }
        } else {
            do { try BGTaskScheduler.shared.submit(request) } catch { foreground() }
        }
    }
}

/// iCloud download progress for the selected files while Files fetches them, from
/// Spotlight's ubiquitous item metadata. Providers other than iCloud Drive report nothing,
/// and the caller then shows "Waiting for Files". Create and stop it on the main thread.
final class ImportDownloadWatch {
    private let query = NSMetadataQuery()
    private let names: [String]
    private let paths: Set<String>
    private let update: (Double?) -> Void
    private var observers: [NSObjectProtocol] = []

    init(urls: [URL], update: @escaping (Double?) -> Void) {
        names = Array(Set(urls.map(\.lastPathComponent)))
        paths = Set(urls.map(Self.key))
        self.update = update
        query.searchScopes = [NSMetadataQueryUbiquitousDocumentsScope, NSMetadataQueryAccessibleUbiquitousExternalDocumentsScope]
        query.predicate = NSPredicate(format: "%K IN %@", NSMetadataItemFSNameKey, names)
        query.notificationBatchingInterval = 0.5
        for name in [Notification.Name.NSMetadataQueryDidFinishGathering, .NSMetadataQueryDidUpdate] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: query, queue: .main) { [weak self] _ in
                self?.publish()
            })
        }
        query.start()
    }

    func stop() {
        query.stop()
        observers.forEach(NotificationCenter.default.removeObserver)
        observers = []
    }

    deinit { stop() }

    private static func key(_ url: URL) -> String {
        let path = url.standardizedFileURL.resolvingSymlinksInPath().path
        return path.hasPrefix("/private/") ? String(path.dropFirst("/private".count)) : path
    }

    private func publish() {
        query.disableUpdates(); defer { query.enableUpdates() }
        var byPath: [String: Double] = [:]
        var byName: [String: [Double]] = [:]
        for index in 0..<query.resultCount {
            guard let item = query.result(at: index) as? NSMetadataItem,
                  let name = item.value(forAttribute: NSMetadataItemFSNameKey) as? String else { continue }
            let status = item.value(forAttribute: NSMetadataUbiquitousItemDownloadingStatusKey) as? String
            let reported = (item.value(forAttribute: NSMetadataUbiquitousItemPercentDownloadedKey) as? NSNumber)?.doubleValue
            // Folders and items with no download state say nothing about progress.
            guard status != nil || reported != nil else { continue }
            let percent = status == NSMetadataUbiquitousItemDownloadingStatusCurrent ? 100 : reported ?? 0
            if let url = item.value(forAttribute: NSMetadataItemURLKey) as? URL { byPath[Self.key(url)] = percent }
            byName[name, default: []].append(percent)
        }
        // Prefer the selected files themselves; fall back to names that match only one item
        // (split parts named 00, 01 can exist in many folders).
        var parts = paths.compactMap { byPath[$0] }
        if parts.isEmpty { parts = names.compactMap { byName[$0]?.count == 1 ? byName[$0]?.first : nil } }
        guard !parts.isEmpty else { update(nil); return }
        let selected = max(parts.count, paths.count)
        update(min(1, parts.reduce(0, +) / Double(selected) / 100))
    }
}
#endif
