import Foundation

/// Simulator only: stands in for the Files folder picker when `-SatchelPreviewCloudFolder YES`
/// is set, with a synthetic folder in the app's own container. Nothing contacts iCloud.
enum CloudSaveFixture {
    static func folder() -> URL? {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewCloudFolder") else { return nil }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("Satchel Saves", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
}
