import SwiftUI

// Small synthetic local files make removal and Prepare availability observable.
// The real library, cleanup, artwork, job and home views perform every mutation.
enum NavigationFixFixture {
    static let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    static let baseID = String(format: "%064x", 0xA11CE)
    static let updateID = String(format: "%064x", 0xA11CF)
    static let dlcIDs = [String(format: "%064x", 0xD1C), String(format: "%064x", 0xD1D)]
    static let packages = ["LanternDrift-1.2.0", "LanternDrift-1.2.0-second"]
    static var index: URL { documents.appendingPathComponent("Homebrew/library.json") }
    static var save: URL {
        documents.appendingPathComponent("NativeRuntime/" + UserDefaults.standard.string(forKey: "SatchelPreviewFixTitle")! + "/save/removal-check.txt")
    }

    static func seed() {
        let defaults = UserDefaults.standard
        defaults.removeObject(forKey: "SatchelWarmupJob")
        defaults.removeObject(forKey: "SatchelPreviewWarmupFinish")
        defaults.removeObject(forKey: "SatchelPreviewWarmupRelease")
        defaults.removeObject(forKey: "SatchelPreviewWarmupStarts")
        // A warm-up journal left by a failed package check, written as releases before the
        // packageFailed field did: "stale" and "missing" carry no field, "failed" carries it.
        if let mode = defaults.string(forKey: "SatchelPreviewStaleWarmup") {
            var progress = WarmupProgress(stage: "Package check failed. Prepare this game again.")
            progress.state = "failed"
            let journal = WarmupJob.Journal(package: mode == "missing" ? "LanternDrift-0.9.0" : "LanternDrift-1.2.0", title: "Lantern Drift",
                                            progress: progress, packageFailed: mode == "failed" ? true : nil)
            if let data = try? JSONEncoder().encode(journal) { defaults.set(data, forKey: "SatchelWarmupJob") }
        }
        // A warm-up saved by a release that started it after every Prepare, so it carries no
        // startedByUser field: "stopped" is a run iOS ended, "running" one the app ended.
        if let mode = defaults.string(forKey: "SatchelPreviewLegacyWarmup") {
            var progress = WarmupProgress(stage: mode == "stopped"
                ? "Stopped by iOS while \(AppBrand.name) was in the background. It resumes when you return." : "Synthetic shader work",
                completed: 1, total: 2)
            progress.state = mode == "stopped" ? "paused" : "running"
            let journal = WarmupJob.Journal(package: "LanternDrift-1.2.0", title: "Lantern Drift", progress: progress,
                                            interrupted: mode == "stopped" ? true : nil)
            if let data = try? JSONEncoder().encode(journal) { defaults.set(data, forKey: "SatchelWarmupJob") }
        }
        guard defaults.bool(forKey: "SatchelPreviewFixTests"), defaults.string(forKey: "SatchelPreviewDemo") == "library" else { return }
        do {
            let fm = FileManager.default
            var items = try JSONDecoder().decode([HomebrewItem].self, from: Data(contentsOf: index))
            let art = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("GameArt")
            let base = try JSONDecoder().decode(GameInfo.self, from: Data(contentsOf: art.appendingPathComponent(baseID + ".json")))
            defaults.set(base.applicationID.titleIDString, forKey: "SatchelPreviewFixTitle")
            let original = try requiredItem(items, id: dlcIDs[0])
            items.append(HomebrewItem(id: dlcIDs[1], name: "Lantern DLC Two", bytes: 1024, textBytes: 0,
                roBytes: 0, dataBytes: 0, bssBytes: 0, importedAt: original.importedAt, package: original.package))
            let row: [String: Any] = ["titleID": base.applicationID + 0x1002, "applicationID": base.applicationID,
                "version": 1, "requiredApplicationVersion": 0, "bytes": 1024]
            let dlc = try JSONDecoder().decode(SwitchAddOnInfo.self, from: JSONSerialization.data(withJSONObject: row))
            let info = GameInfo(applicationID: base.applicationID, kind: .addOnContent, version: 1,
                displayVersion: "1", title: "Lantern DLC Two", publisher: "Fixture", hasIcon: false, accent: nil, addOns: [dlc])
            try JSONEncoder().encode(info).write(to: art.appendingPathComponent(dlcIDs[1] + ".json"))
            for item in items {
                try Data("Synthetic local import".utf8).write(to: documents.appendingPathComponent("Homebrew/" + item.id + "." + item.fileExtension))
            }
            try JSONEncoder().encode(items).write(to: index)
            let second = documents.appendingPathComponent("PreparedGames/" + packages[1])
            try fm.createDirectory(at: second, withIntermediateDirectories: true)
            let manifest = try Data(contentsOf: documents.appendingPathComponent("PreparedGames/" + packages[0] + "/manifest.json"))
            try manifest.write(to: second.appendingPathComponent("manifest.json"))
            // Everything a whole-game delete clears or keeps: runtime caches and logs beside the
            // save and its sync state, per-title settings, play history, title art and reports.
            let title = base.applicationID.titleIDString
            let runtime = documents.appendingPathComponent("NativeRuntime/" + title)
            if fm.fileExists(atPath: runtime.path) { try fm.removeItem(at: runtime) }
            try fm.createDirectory(at: save.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data("Synthetic save retained".utf8).write(to: save)
            try Data("synthetic sync state".utf8).write(to: runtime.appendingPathComponent("save.sync"))
            for (path, text) in [("shader-capture/cache.bin", "synthetic cache"), ("session.json", "{}"), ("logs/host.log", "synthetic log")] {
                let url = runtime.appendingPathComponent(path)
                try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
                try Data(text.utf8).write(to: url)
            }
            defaults.set(["DM_SCREEN_MODE": "fit"], forKey: "SatchelPlayerSettings." + title)
            let played = [title: PlayHistory.Entry(lastPlayed: Date().addingTimeInterval(-86400 * 3), seconds: 5400)]
            defaults.set(try JSONEncoder().encode(played), forKey: PlayHistory.key)
            try JSONEncoder().encode(base).write(to: art.appendingPathComponent("title-" + title + ".json"))
            for package in packages {
                let report = NativeGameReport.url(package: package)
                try fm.createDirectory(at: report.deletingLastPathComponent(), withIntermediateDirectories: true)
                try Data("{}".utf8).write(to: report)
            }
            // A Prepare that was stopped before it finished left its staging folder.
            if defaults.bool(forKey: "SatchelPreviewLeftovers") {
                let stage = documents.appendingPathComponent("PreparedGames/.prepare-" + UUID().uuidString + "/rewrite")
                try fm.createDirectory(at: stage, withIntermediateDirectories: true)
                try Data(count: 3 * 1024 * 1024).write(to: stage.appendingPathComponent("main.partial"))
            }
        } catch { preconditionFailure("Synthetic removal setup failed: \(error)") }
    }

    /// What a whole-game delete left: art files, per-title settings, play history, runtime
    /// caches (non-save entries), sync state, the save, Satchel's save backups and reports.
    static func deleteState() -> String {
        let fm = FileManager.default
        let defaults = UserDefaults.standard
        guard let title = defaults.string(forKey: "SatchelPreviewFixTitle") else { return "fixture error: no title" }
        let art = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("GameArt")
        let artCount = [baseID + ".json", "title-" + title + ".json"].filter { fm.fileExists(atPath: art.appendingPathComponent($0).path) }.count
        let runtime = documents.appendingPathComponent("NativeRuntime/" + title)
        let entries = (try? fm.contentsOfDirectory(atPath: runtime.path)) ?? []
        let caches = entries.filter { !SaveStore.holdsSaves($0) && !$0.hasPrefix(".") }.count
        let backups = ((try? fm.contentsOfDirectory(atPath: runtime.appendingPathComponent("SaveBackups").path)) ?? [])
            .filter { $0.hasPrefix("satchel-") && $0.hasSuffix(".zip") }.count
        let reports = packages.filter { fm.fileExists(atPath: NativeGameReport.url(package: $0).path) }.count
        let saveKept = (try? String(contentsOf: save, encoding: .utf8)) == "Synthetic save retained"
        return "art=\(artCount) settings=\(defaults.object(forKey: "SatchelPlayerSettings." + title) == nil ? 0 : 1) history=\(PlayHistory.entry(title: title) == nil ? 0 : 1) caches=\(caches) sync=\(entries.contains("save.sync") ? 1 : 0) save=\(saveKept ? "kept" : "gone") backups=\(backups) reports=\(reports)"
    }

    private static func requiredItem(_ items: [HomebrewItem], id: String) throws -> HomebrewItem {
        guard let item = items.first(where: { $0.id == id }) else { throw PrepareFailure(message: "Missing synthetic DLC") }
        return item
    }

    static func state() -> String {
        do {
            let items = try JSONDecoder().decode([HomebrewItem].self, from: Data(contentsOf: index))
            let ids = Set(items.map(\.id)), targets = [baseID, updateID] + dlcIDs
            let fm = FileManager.default
            let files = targets.filter { id in
                fm.fileExists(atPath: documents.appendingPathComponent("Homebrew/" + id + (id == baseID ? ".xci" : ".nsp")).path)
            }.count
            let prepared = packages.filter { fm.fileExists(atPath: documents.appendingPathComponent("PreparedGames/" + $0).path) }.count
            let saveKept = (try? String(contentsOf: save, encoding: .utf8)) == "Synthetic save retained"
            return "base=\(ids.contains(baseID) ? 1 : 0) update=\(ids.contains(updateID) ? 1 : 0) dlc=\(dlcIDs.filter { ids.contains($0) }.count) files=\(files) packages=\(prepared) other=\(ids.subtracting(targets).count) save=\(saveKept ? "kept" : "missing")"
        } catch { return "fixture error: \(error.localizedDescription)" }
    }
}

struct NavigationWarmupControls: View {
    @ObservedObject private var job = WarmupJob.shared
    @State private var context: WarmupJob.Context?
    @State private var interrupted = false
    var body: some View {
        VStack {
            Button("Start synthetic warm-up") {
                let package = NavigationFixFixture.documents.appendingPathComponent("PreparedGames/LanternDrift-1.2.0")
                guard let context = job.begin(package: package, title: "Lantern Drift", keepAlive: false) else {
                    preconditionFailure("Synthetic warm-up could not begin")
                }
                self.context = context
                DispatchQueue.global(qos: .utility).async { WarmupJob.perform(context, verified: true) }
            }.accessibilityIdentifier("fixture-warmup-start").disabled(job.running)
            Button("Finish synthetic work") { UserDefaults.standard.set(true, forKey: "SatchelPreviewWarmupFinish") }
                .accessibilityIdentifier("fixture-warmup-finish")
            Text("\(job.running ? "running" : "idle"):\(job.journal?.progress.state ?? "none")")
                .accessibilityIdentifier("fixture-warmup-state")
            if UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupHoldCancellation") {
                Button("Release cancellation acknowledgement") { UserDefaults.standard.set(true, forKey: "SatchelPreviewWarmupRelease") }
                    .accessibilityIdentifier("fixture-warmup-release")
            }
            Text("starts=\(UserDefaults.standard.integer(forKey: "SatchelPreviewWarmupStarts")) interrupted=\(interrupted ? 1 : 0)")
                .accessibilityIdentifier("fixture-warmup-lifecycle")
        }.font(.system(size: 10))
        .onReceive(job.$journal) { value in if value?.interrupted == true { interrupted = true } }
        .onReceive(NotificationCenter.default.publisher(for: UIApplication.didEnterBackgroundNotification)) { _ in
            if UserDefaults.standard.bool(forKey: "SatchelPreviewWarmupExpire"), let context {
                job.expire(context)
            }
        }
    }
}
