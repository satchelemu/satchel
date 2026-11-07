import SwiftUI
import UniformTypeIdentifiers
import UIKit

#if !SATCHEL_UI_PREVIEW_MINIMAL
/// Satchel's save manager and the engine's iCloud save sync (Settings > Storage, the engine's
/// save_sync.c through App/CloudSaveFolder.m) share one local save but never write it at once:
///
/// - Files: under Documents/NativeRuntime/<title ID>/, this panel and SaveStore own `save`
///   (export reads a clone; import and restore swap it), `SaveBackups/` and `.satchel-save-*`.
///   The sync owns `save.sync` (the hash last known to match the cloud), `save.sync-tmp/`
///   and `save-backups/` (the copies it keeps before replacing a save, newest 10 per side), and
///   the one packed file per game in the chosen folder. Neither touches the other's files.
/// - Time: the sync replaces `save` (and `save_device`) only at the game's first save mount, in
///   a running session. Export, import and restore are disabled while a session holds the
///   title (`running`), so the two never write `save` together. Between those points the sync
///   only reads committed clones and writes the cloud file and `save.sync`.
/// - Effect of an import or restore with sync on: the local save no longer matches `save.sync`,
///   so the next start sends it to the cloud. If another device sent a newer save in the
///   meantime, both changed and the newer file date wins: the imported save is then kept in
///   `save-backups/`, not lost. Automatic backups here keep running on quit and background.
struct GameSavePanel: View {
    let titleID: String
    let gameName: String
    @State private var summary: SaveStore.Summary?
    @State private var busy = false
    @State private var showBackups = false
    @State private var message: String?
    @State private var backupError: String?
    @State private var picker: SavePickerRequest?
    @State private var restoreBackup: SaveStore.Backup?
    @State private var exportDirectory: URL?
    @Environment(\.scenePhase) private var scenePhase

    private var running: Bool { SatchelNativeSaveTitle()?.caseInsensitiveCompare(titleID) == .orderedSame }

    @ViewBuilder private func saveActions(_ summary: SaveStore.Summary) -> some View {
        Button { exportSave() } label: {
            Label("Export save", systemImage: "square.and.arrow.up").lineLimit(1)
        }.disabled(busy || running || summary.lastSaved == nil).accessibilityIdentifier("export-save")
        Button { picker = SavePickerRequest(export: nil) } label: {
            Label("Import save", systemImage: "square.and.arrow.down").lineLimit(1)
        }.disabled(busy || running).accessibilityIdentifier("import-save")
        if !summary.backups.isEmpty {
            Button("Backups (\(summary.backups.count))") { showBackups.toggle() }
                .lineLimit(1)
                .disabled(busy || running).accessibilityIdentifier("restore-save-backup")
        }
    }
    var body: some View {
        SatchelPanel(title: "Save data") {
            if let summary {
                if let date = summary.lastSaved {
                    Text("Last saved \(date.formatted(date: .abbreviated, time: .shortened)) · \(satchelBytes(summary.bytes))")
                        .accessibilityIdentifier("save-summary")
                } else { Text("No save yet. Save in the game before quitting.") }
                // One row where the actions fit; stacked on a narrow column (the front screen)
                // rather than each label wrapping onto two lines.
                ViewThatFits(in: .horizontal) {
                    HStack(spacing: 12) { saveActions(summary) }
                    VStack(alignment: .leading, spacing: 10) { saveActions(summary) }
                }
                if showBackups {
                    ForEach(Array(summary.backups.enumerated()), id: \.element.id) { index, backup in
                        HStack {
                            VStack(alignment: .leading, spacing: 3) {
                                Text(backup.date.formatted(date: .abbreviated, time: .standard))
                                Text(satchelBytes(backup.bytes)).foregroundStyle(SatchelTheme.textDim)
                            }
                            .accessibilityElement(children: .combine)
                            .accessibilityIdentifier("save-backup-info-\(index)")
                            Spacer()
                            Button("Restore") { restoreBackup = backup; showBackups = false }
                                .disabled(busy || running)
                                .accessibilityLabel("Restore \(backup.date.formatted(date: .abbreviated, time: .standard))")
                                .accessibilityIdentifier("save-backup-restore-\(index)")
                        }
                    }
                }
            } else { Text(message == nil ? "Reading save data…" : "Save data unavailable.") }
            if busy { ProgressView().controlSize(.small) }
            if running { Text("Quit this game to home before exporting or restoring. Pausing is not enough.") }
            Text("Up to 3 automatic backups after changed saves, on quit or background. Limit: 512 MiB per save, 1.6 GB of backups per game. Import accepts Satchel save ZIPs and backs up the current save first.")
                .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
            if SatchelCloudSaveFolderName() != nil {
                Text("iCloud save sync is on. An imported or restored save goes to iCloud the next time you play this game.")
                    .font(.system(size: 12)).foregroundStyle(SatchelTheme.textDim)
                    .accessibilityIdentifier("save-cloud-note")
            }
            if let backupError { Text(backupError).foregroundStyle(SatchelTheme.spark) }
            if let message { Text(message).foregroundStyle(SatchelTheme.textDim).accessibilityIdentifier("save-result") }
        }
        .font(.system(size: 13, weight: .medium, design: .rounded))
        .task(id: titleID) { await refresh() }
        .onChange(of: scenePhase) { _, phase in if phase == .active { Task { await refresh() } } }
        .onReceive(NotificationCenter.default.publisher(for: SaveLifecycle.changed)) { _ in Task { await refresh() } }
        .onReceive(NotificationCenter.default.publisher(for: .SatchelNativeSessionDidEnd)) { _ in Task { await refresh() } }
        .sheet(item: $picker, onDismiss: cleanupExport) { request in
            SaveFileSheet(export: request.export) { url in
                if request.export != nil { message = url == nil ? "Export cancelled." : "Save shared." }
                else if let url { restore(url) }
                picker = nil
            }
        }
        .confirmationDialog("Restore this backup?", isPresented: Binding(get: { restoreBackup != nil }, set: { if !$0 { restoreBackup = nil } }), titleVisibility: .visible) {
            if let backup = restoreBackup {
                Button("Restore backup") { restore(backup.url); restoreBackup = nil }
                Button("Cancel", role: .cancel) { restoreBackup = nil }
            }
        } message: {
            if let backup = restoreBackup {
                Text("Restore the backup from \(backup.date.formatted(date: .abbreviated, time: .standard)) (\(satchelBytes(backup.bytes)))? Your current save will be backed up before replacement. Only the latest 3 backups are kept.")
            }
        }
    }
    private func refresh() async {
        backupError = SaveLifecycle.error(title: titleID)
        do { summary = try await Task.detached(priority: .utility) { try SaveStore.native.summary(title: titleID) }.value }
        catch { message = error.localizedDescription }
    }
    private func exportSave() {
        busy = true; message = nil
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("SatchelSaveExport-" + UUID().uuidString)
        exportDirectory = directory
        Task {
            do {
                let url = try await Task.detached(priority: .userInitiated) { try SaveStore.native.export(title: titleID, name: gameName, to: directory) }.value
                picker = SavePickerRequest(export: url)
            } catch { message = error.localizedDescription; cleanupExport() }
            busy = false
        }
    }
    private func cleanupExport() {
        if let directory = exportDirectory { try? FileManager.default.removeItem(at: directory) }
        exportDirectory = nil
    }
    private func restore(_ url: URL) {
        busy = true; message = nil
        // Keep security scope alive through validation, backup and atomic restore.
        let scoped = url.startAccessingSecurityScopedResource()
        Task {
            do {
                try await Task.detached(priority: .userInitiated) {
                    // File coordination downloads provider content before the ZIP reader opens it.
                    var coordinationError: NSError?, operationError: Error?
                    NSFileCoordinator().coordinate(readingItemAt: url, options: [], error: &coordinationError) { readable in
                        do { try SaveStore.native.restore(title: titleID, from: readable) }
                        catch { operationError = error }
                    }
                    if let error = operationError ?? coordinationError { throw error }
                }.value
                message = "Save restored."
            } catch { message = error.localizedDescription }
            if scoped { url.stopAccessingSecurityScopedResource() }
            busy = false; await refresh()
        }
    }
}

private struct SavePickerRequest: Identifiable {
    let id = UUID()
    let export: URL?
}

private struct SaveFileSheet: View {
    let export: URL?
    let completion: (URL?) -> Void
    var body: some View {
        if let export { SaveShareSheet(url: export, completion: completion) }
        else {
            // Keep an exit even when the remote provider omits its own toolbar.
            VStack(spacing: 0) {
                HStack {
                    Spacer()
                    Button("Cancel import") { completion(nil) }
                        .accessibilityIdentifier("cancel-save-import")
                        .padding()
                }
                SaveDocumentPicker(completion: completion)
            }
        }
    }
}

private struct SaveShareSheet: UIViewControllerRepresentable {
    let url: URL
    let completion: (URL?) -> Void
    func makeUIViewController(context: Context) -> UIActivityViewController {
        let share = UIActivityViewController(activityItems: [url], applicationActivities: nil)
        share.completionWithItemsHandler = { _, completed, _, error in
            completion(completed && error == nil ? url : nil)
        }
        return share
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

private struct SaveDocumentPicker: UIViewControllerRepresentable {
    let completion: (URL?) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(completion: completion) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.zip], asCopy: false)
        picker.allowsMultipleSelection = false; picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let completion: (URL?) -> Void
        init(completion: @escaping (URL?) -> Void) { self.completion = completion }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { completion(urls.first) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { completion(nil) }
    }
}
#endif
