import SwiftUI

// Simulator document-provider boundary. All save bytes are synthetic. The panel,
// lifecycle, archive checks and atomic restore are production implementations.
enum SaveFixture {
    static let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    static let workspace = documents.appendingPathComponent("NavigationSaveFixture")
    static var title: String {
        let manifest = documents.appendingPathComponent("PreparedGames/LanternDrift-1.2.0/manifest.json")
        let data = try! Data(contentsOf: manifest)
        return (try! JSONSerialization.jsonObject(with: data) as! [String: Any])["titleID"] as! String
    }
    static var save: URL { documents.appendingPathComponent("NativeRuntime/\(title)/save/progress.txt") }
    static var imported: URL { workspace.appendingPathComponent("Import/Synthetic.zip") }
    static var wrongTitle: URL { workspace.appendingPathComponent("Wrong/Synthetic.zip") }
    static var corrupt: URL { workspace.appendingPathComponent("Corrupt.zip") }
    static var exportVerified = false

    static func seed() {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewSaveTests") else { return }
        do {
            let fm = FileManager.default
            let runtime = documents.appendingPathComponent("NativeRuntime/" + title)
            for url in [workspace, runtime] where fm.fileExists(atPath: url.path) { try fm.removeItem(at: url) }
            try fm.createDirectory(at: workspace, withIntermediateDirectories: true)
            try fm.createDirectory(at: save.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data("checkpoint-old".utf8).write(to: save)
            let archive = try SaveStore.native.export(title: title, name: "Synthetic", to: imported.deletingLastPathComponent())
            if archive != imported { try fm.moveItem(at: archive, to: imported) }
            _ = try SaveStore.native.backupIfChanged(title: title)
            try Data("checkpoint-current".utf8).write(to: save)
            let other = "0100ABCDFFFF0000"
            let otherRoot = workspace.appendingPathComponent("Other")
            let otherSave = otherRoot.appendingPathComponent(other + "/save")
            try fm.createDirectory(at: otherSave, withIntermediateDirectories: true)
            try Data("unrelated-synthetic-title".utf8).write(to: otherSave.appendingPathComponent("progress.txt"))
            let otherArchive = try SaveStore(root: otherRoot).export(title: other, name: "Synthetic", to: wrongTitle.deletingLastPathComponent())
            if otherArchive != wrongTitle { try fm.moveItem(at: otherArchive, to: wrongTitle) }
            try Data("not a ZIP".utf8).write(to: corrupt)
        } catch { preconditionFailure("Synthetic save setup failed: \(error)") }
    }

    static func state() -> String {
        do {
            let payload = try String(contentsOf: save, encoding: .utf8)
            let count = try SaveStore.native.summary(title: title).backups.count
            return "payload=\(payload) backups=\(count) export=\(exportVerified ? "verified" : "none")"
        } catch { return "fixture error: \(error.localizedDescription)" }
    }

    static func verifyExport(_ url: URL) throws -> URL {
        let fm = FileManager.default
        let copy = workspace.appendingPathComponent("Export-" + UUID().uuidString + ".zip")
        try fm.copyItem(at: url, to: copy)
        let root = workspace.appendingPathComponent("ExportCheck-" + UUID().uuidString)
        try SaveStore(root: root).restore(title: title, from: copy)
        let restored = root.appendingPathComponent(title + "/save/progress.txt")
        guard try Data(contentsOf: restored) == Data(contentsOf: save) else {
            throw SaveFailure(message: "The exported synthetic bytes changed.")
        }
        exportVerified = true
        return copy
    }
}

struct PreviewSaveDocumentPicker: View {
    let export: URL?
    let completion: (URL?) -> Void
    @State private var failure: String?

    var body: some View {
        if UserDefaults.standard.bool(forKey: "SatchelPreviewSaveProvider") {
            VStack(spacing: 20) {
                Text("Synthetic document provider")
                if let export {
                    Button("Save synthetic export") {
                        do { completion(try SaveFixture.verifyExport(export)) }
                        catch { failure = error.localizedDescription }
                    }.accessibilityIdentifier("fixture-save-export")
                } else {
                    Button("Choose matching save") { completion(SaveFixture.imported) }
                        .accessibilityIdentifier("fixture-save-import")
                    Button("Choose another title") { completion(SaveFixture.wrongTitle) }
                        .accessibilityIdentifier("fixture-save-wrong-title")
                    Button("Choose corrupt archive") { completion(SaveFixture.corrupt) }
                        .accessibilityIdentifier("fixture-save-corrupt")
                }
                Button("Cancel") { completion(nil) }.accessibilityIdentifier("fixture-save-cancel")
                if let failure { Text(failure).accessibilityIdentifier("fixture-save-error") }
            }
        } else {
            SaveDocumentPicker(export: export, completion: completion)
        }
    }
}
