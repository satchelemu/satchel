import CryptoKit
import Foundation

/// Holds the first synthetic copy after writing a chunk. Cancellation, cleanup and
/// publication still run in SwitchPackageImport and HomebrewLibrary.
enum ImportCancellationFixture {
    private static let lock = NSLock()
    private static var held = false
    private static var original: URL?
    private static var digest: SHA256.Digest?

    static func selected(_ sources: [URL]) throws {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewImportCancellation"), original == nil else { return }
        let source = sources[0]
        digest = SHA256.hash(data: try Data(contentsOf: source))
        original = source
    }

    static func afterChunk(cancelled: () -> Bool) throws {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewImportCancellation") else { return }
        lock.lock()
        let shouldHold = !held
        held = true
        lock.unlock()
        guard shouldHold else { return }
        let deadline = Date().addingTimeInterval(60)
        while !cancelled() {
            guard Date() < deadline else { throw SwitchImportFailure(message: "Synthetic import cancellation timed out.") }
            Thread.sleep(forTimeInterval: 0.02)
        }
    }

    static func state(_ library: HomebrewLibrary) -> String {
        let folder = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Homebrew")
        let partials = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil))?
            .filter { $0.lastPathComponent.hasPrefix(".import-") }.count ?? -1
        let unchanged = original.flatMap { try? Data(contentsOf: $0) }.map { SHA256.hash(data: $0) == digest } ?? false
        return "items=\(library.items.count) partial=\(partials) source=\(unchanged ? "unchanged" : "missing or changed")"
    }
}
