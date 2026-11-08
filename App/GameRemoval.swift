import Foundation

/// Deletes a whole game from this iPhone: its library records (the base, its updates and its
/// DLC) and their imported files, every package prepared for its title with their reports and
/// learned settings, its runtime folder (shader caches, logs, launch records), the art kept
/// for it, its per-title settings and its play history. The files in iCloud Drive or Files
/// are never touched. Saves stay unless the player chooses to delete them too, and even then
/// the current save is first kept in the game's save-backup list.
@MainActor
enum GameRemoval {
    /// A delete is running; nothing else may start one.
    private(set) static var inProgress = false

    /// The title a shelf item belongs to, if known.
    static func titleID(_ item: HomebrewItem, artwork: GameArtworkStore) -> UInt64? {
        artwork.info(item)?.applicationID ?? item.preparedSource?.titleID ?? artwork.newestPackage(for: item)?.applicationID
    }

    static func titleString(_ id: UInt64) -> String { String(format: "%016llX", id) }

    /// Why the game cannot be deleted right now, shown under Delete before any tap; nil when it can.
    static func blocker(library: HomebrewLibrary, item: HomebrewItem? = nil, artwork: GameArtworkStore? = nil) -> String? {
        if inProgress { return "Another game is being deleted." }
        if library.importing { return "Delete is available once the import finishes." }
        if library.runningID != nil { return "Delete is available once the game stops." }
        if library.reclaimingOriginals { return "Wait while \(AppBrand.name) tidies up imported files." }
#if !SATCHEL_UI_PREVIEW_MINIMAL
        if AddOnJob.shared.running { return "Wait for Add DLC to finish before deleting this game." }
        if PrepareJob.shared.running { return "Wait for the current preparation to finish." }
        if WarmupJob.shared.running { return "Pause the shader warm-up before deleting a game." }
        // The engine runs one game per app launch, so prepared copies stay in use until then.
        // A game with no prepared copy was never played here and can still go.
        if PrepareJob.gameSessionOpen, item.map({ artwork?.newestPackage(for: $0) != nil }) ?? true {
            return "A game was played since \(AppBrand.name) opened. Close and reopen the app, then delete this game."
        }
#endif
        return nil
    }

    /// Library records that belong to the game: the item itself (unless it is a prepared-only
    /// tile), its updates and its DLC. Another imported copy of the base game stays.
    static func records(for item: HomebrewItem, updates: [HomebrewItem], library: HomebrewLibrary,
                        artwork: GameArtworkStore) -> [HomebrewItem] {
        var targets = (GameArtworkStore.preparedFolder(item.id) == nil ? [item] : []) + updates
        guard let id = titleID(item, artwork: artwork) else { return targets }
        for other in library.items where !targets.contains(where: { $0.id == other.id }) {
            if let source = other.preparedSource, source.titleID == id, source.isUpdate { targets.append(other); continue }
            guard let info = artwork.info(other) else { continue }
            let belongs = info.kind == .addOnContent ? (info.addOns ?? []).contains { $0.applicationID == id }
                : info.kind == .update && info.applicationID == id
            if belongs { targets.append(other) }
        }
        return targets
    }

    /// What deleting this game removes, for the confirmation's wording.
    struct Summary {
        let records: Int
        let packages: Int
        let hasSave: Bool
    }

    static func summary(for item: HomebrewItem, updates: [HomebrewItem], library: HomebrewLibrary,
                        artwork: GameArtworkStore) -> Summary {
        let id = titleID(item, artwork: artwork)
        let packages = id.map { id in artwork.prepared.filter { $0.applicationID == id }.count } ?? 0
        let save = id.map { id in
            FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("NativeRuntime/" + titleString(id) + "/save").path
        }
        return Summary(records: records(for: item, updates: updates, library: library, artwork: artwork).count,
                       packages: packages, hasSave: save.map { FileManager.default.fileExists(atPath: $0) } ?? false)
    }

    /// Deletes the game. Returns nil when everything went, or what to tell the player.
    /// Order: the save backup (when saves go), then packages (a failure there leaves the game
    /// whole), then the runtime folder, then the library records and art.
    static func delete(_ item: HomebrewItem, updates: [HomebrewItem], keepSaves: Bool,
                       library: HomebrewLibrary, artwork: GameArtworkStore) async -> String? {
        if let reason = blocker(library: library, item: item, artwork: artwork) { return reason }
        inProgress = true; defer { inProgress = false }
        let id = titleID(item, artwork: artwork)
        let title = id.map(titleString)
        let targets = records(for: item, updates: updates, library: library, artwork: artwork)
        var partial: String?
#if !SATCHEL_UI_PREVIEW_MINIMAL
        // Packages from an older engine first, so a failure never leaves only an unplayable one.
        let packages = id.map { id in artwork.prepared.filter { $0.applicationID == id }.sorted { !$0.current && $1.current }.map(\.folder) } ?? []
        let result = await Task.detached(priority: .userInitiated) { () -> (fatal: String?, partial: String?) in
            if let title, !keepSaves {
                do { try SaveStore.native.backupIfChanged(title: title) }
                catch {
                    return ("Your save could not be backed up, so nothing was deleted. \(error.localizedDescription) Export the save from the game page first, or choose Delete, keep saves.", nil)
                }
            }
            for name in packages {
                do { try PreparedGameCleanup.delete(named: name) } catch { return (error.localizedDescription, nil) }
                try? FileManager.default.removeItem(at: NativeGameReport.url(package: name))
                try? FileManager.default.removeItem(at: NativeBootFeedback.storeURL(package: name))
            }
            guard let title else { return (nil, nil) }
            do { try SaveStore.native.purge(title: title, keepSaves: keepSaves); return (nil, nil) }
            catch { return (nil, "The game was deleted, but some of its cached files were not: \(error.localizedDescription)") }
        }.value
        artwork.refreshPrepared()
        if let fatal = result.fatal { return fatal }
        partial = result.partial
        if let journal = WarmupJob.shared.journal, packages.contains(journal.package) { WarmupJob.shared.dismiss() }
        if let outcome = PrepareJob.shared.outcome, !PrepareJob.shared.running,
           packages.contains(outcome.package) || (id != nil && outcome.applicationID == id) || targets.contains(where: { $0.id == outcome.baseID }) {
            PrepareJob.shared.dismissOutcome()
        }
#endif
        if let title {
            UserDefaults.standard.removeObject(forKey: "SatchelPlayerSettings." + title)
            if !keepSaves { UserDefaults.standard.removeObject(forKey: "SatchelSaveBackupError." + title) }
            PlayHistory.forget(title: title)
        }
        // An import could have started while files were being deleted; the library refuses
        // then, so say so instead of reporting success.
        guard library.remove(targets) else {
            return "The game's prepared copy was deleted, but its library entry was kept because the library was busy. Delete it again once the import finishes."
        }
        for target in targets { artwork.forget(target) }
        // Title art is shared with any other imported copy of the same game; keep it for that.
        if let id, !library.items.contains(where: { titleID($0, artwork: artwork) == id }) { artwork.forgetTitle(id) }
        Haptics.success()
        return partial
    }

    /// The confirmation for deleting a whole game: keep the saves (the default) or delete them too.
    static func confirmation(title: String, summary: Summary, delete: @escaping (Bool) -> Void) -> SatchelConfirmRequest {
        var parts = ["Removes \(title) from this iPhone"]
        parts[0] += summary.packages > 0 ? " with its prepared copy, updates and DLC," : " with its updates and DLC,"
        parts[0] += " and clears its artwork, settings, shader caches and logs."
        parts.append("Your files in iCloud Drive or Files stay, so you can add it again later.")
        if summary.hasSave {
            parts.append("Delete game and saves keeps a backup of the current save, which you can restore if you add the game again.")
        }
        var actions = [SatchelConfirmAction(id: "confirm-remove-prepared", title: summary.hasSave ? "Delete, keep saves" : "Delete game", role: .destructive,
                                            systemImage: "trash") { delete(true) }]
        if summary.hasSave {
            actions.append(SatchelConfirmAction(id: "confirm-delete-with-saves", title: "Delete game and saves", role: .destructive) { delete(false) })
        }
        actions.append(SatchelConfirmAction(id: "cancel-remove-prepared", title: "Cancel", role: .cancel) {})
        return SatchelConfirmRequest(title: "Delete \(title)?", message: parts.joined(separator: " "), actions: actions,
                                     identifier: "delete-game-confirm")
    }
}
