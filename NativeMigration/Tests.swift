import Darwin
import Foundation

@main struct MigrationTests {
    static let fm = FileManager.default
    static var checks = 0
    static func expect(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
        guard try condition() else { fatalError(message) }
        checks += 1
    }
    static func put(_ root: URL, _ path: String, _ text: String) throws {
        let url = root.appendingPathComponent(path)
        try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(text.utf8).write(to: url)
    }
    static func get(_ root: URL, _ path: String) throws -> Data { try Data(contentsOf: root.appendingPathComponent(path)) }
    static func rejected(_ body: () throws -> Void) throws {
        do { try body() } catch { checks += 1; return }
        fatalError("Expected rejection")
    }
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1]).resolvingSymlinksInPath()
        guard !fm.fileExists(atPath: root.path) else { fatalError("Use a fresh synthetic output directory") }
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        let dev = root.appendingPathComponent("Dev/Documents")
        let main = root.appendingPathComponent("Satchel/Documents")
        let group = root.appendingPathComponent("Group")
        let state = root.appendingPathComponent("Satchel/State")
        let a = String(repeating: "a", count: 64), b = String(repeating: "b", count: 64)
        try put(dev, "Homebrew/library.json", "[{\"id\":\"\(a)\",\"name\":\"Synthetic Dev\"}]")
        try put(dev, "Homebrew/\(a).nro", "synthetic import")
        for package in ["Package", "Package2", "Package3"] {
            try put(dev, "PreparedGames/\(package)/romfs.img", String(repeating: "synthetic payload ", count: 65536))
            try put(dev, "PreparedGames/\(package)/manifest.json", "synthetic manifest")
        }
        try put(dev, "ShaderPacks/Pack/shader", "synthetic shader")
        try put(dev, "NativeRuntime/TITLE/save/slot0/save.bin", "dev save bytes\0\u{1}\u{7}")
        try put(main, "Homebrew/library.json", "[{\"id\":\"\(b)\",\"name\":\"Synthetic Main\"}]")
        try put(main, "Homebrew/\(b).nro", "main import")
        try put(main, "PreparedGames/Package/romfs.img", "main package")
        try put(main, "ShaderPacks/MainOnly/shader", "main shader")
        try put(main, "NativeRuntime/TITLE/save/slot0/save.bin", "main save")
        try put(main, "NativeRuntime/TITLE/save/stale-slot/save.bin", "main extra slot")
        try put(main, "NativeRuntime/OTHER/save/save.bin", "other title save")
        let exporter = MigrationStore(shared: group, documents: dev, state: root.appendingPathComponent("Dev/State"))
        let originalDev = try exporter.inventory(dev)
        for point in ["export-inventory", "snapshot-file", "export-snapshot", "export-ready"] {
            exporter.checkpoint = { if $0 == point { throw MigrationFailure(message: "injected interruption") } }
            try rejected { try exporter.locked { try exporter.exportDocuments() } }
            try expect(exporter.inventory(dev) == originalDev, "Export changed original")
        }
        exporter.checkpoint = { _ in }
        try exporter.locked { try exporter.exportDocuments() }
        try put(dev, "changed-after-export", "synthetic change")
        try rejected { try exporter.exportDocuments() }
        try fm.removeItem(at: dev.appendingPathComponent("changed-after-export"))
        let adopter = MigrationStore(shared: group, documents: main, state: state)
        // Test both apps' lock against a second file descriptor.
        try exporter.locked { try rejected { try adopter.locked {} } }
        for point in ["adopt-plan", "adopt-backup", "adopt-publish", "adopt-verified"] {
            adopter.checkpoint = { if $0 == point { throw MigrationFailure(message: "injected interruption") } }
            try rejected { try adopter.locked { try adopter.adoptDocuments() } }
            try expect(!adopter.complete, "Interrupted migration marked complete")
        }
        adopter.checkpoint = { _ in }
        try adopter.locked { try adopter.adoptDocuments(); try adopter.finish() }
        try expect(adopter.complete, "Completion marker missing")
        try expect(get(main, "NativeRuntime/TITLE/save/slot0/save.bin") == get(dev, "NativeRuntime/TITLE/save/slot0/save.bin"), "Save bytes changed")
        try expect(!fm.fileExists(atPath: main.appendingPathComponent("NativeRuntime/TITLE/save/stale-slot").path), "Stale saves mixed")
        try expect(get(state, "Original/NativeRuntime/TITLE/save/slot0/save.bin") == Data("main save".utf8), "Main save backup lost")
        try expect(get(main, "NativeRuntime/OTHER/save/save.bin") == Data("other title save".utf8), "Other main title lost")
        try expect(get(main, "PreparedGames/Package/romfs.img") == Data("main package".utf8), "Main package overwritten")
        try expect(get(main, "PreparedGames/Package-from-dev-1/romfs.img") == get(dev, "PreparedGames/Package/romfs.img"), "Dev collision lost")
        let library = try JSONSerialization.jsonObject(with: get(main, "Homebrew/library.json")) as! [[String: Any]]
        try expect(Set(library.compactMap { $0["id"] as? String }) == Set([a, b]), "Library union failed")
        try expect(get(main, "ShaderPacks/MainOnly/shader") == Data("main shader".utf8), "Main shaders lost")
        try expect(exporter.inventory(dev) == originalDev, "Adoption changed originals")
        try put(main, "NativeRuntime/TITLE/save/slot0/save.bin", "new played save")
        try expect(get(dev, "NativeRuntime/TITLE/save/slot0/save.bin") != get(main, "NativeRuntime/TITLE/save/slot0/save.bin"), "Cloned saves share mutable inode")
        try expect(get(group, "Documents/NativeRuntime/TITLE/save/slot0/save.bin") == get(dev, "NativeRuntime/TITLE/save/slot0/save.bin"), "Rollback save changed")
        try rejected { _ = try adopter.child(main, "../escape") }
        try fm.createSymbolicLink(at: dev.appendingPathComponent("link"), withDestinationURL: main)
        try rejected { _ = try exporter.inventory(dev) }
        // This is synthetic task scratch only.
        try fm.removeItem(at: dev.appendingPathComponent("link"))
        let corruptURL = group.appendingPathComponent("Documents/PreparedGames/Package/romfs.img")
        var corrupt = try Data(contentsOf: corruptURL)
        corrupt[0] ^= 1
        try corrupt.write(to: corruptURL, options: .atomic)
        try rejected { try exporter.exportDocuments() }
        try expect(exporter.inventory(dev) == originalDev, "Corruption affected source")
        try put(root, "digest-known", "abc")
        try expect(adopter.digest(root.appendingPathComponent("digest-known")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known answer mismatch")
        // A sparse 48 GiB payload exercises 64-bit size and COW without allocating 48 GiB.
        let huge = root.appendingPathComponent("sparse48GiB")
        fm.createFile(atPath: huge.path, contents: Data())
        let file = try FileHandle(forWritingTo: huge)
        try file.truncate(atOffset: 48 * 1024 * 1024 * 1024)
        try file.seek(toOffset: 48 * 1024 * 1024 * 1024 - 4)
        try file.write(contentsOf: Data("END!".utf8)); try file.close()
        let cloned = root.appendingPathComponent("sparse48GiB-clone")
        try adopter.clone(huge, cloned)
        let attrs = try fm.attributesOfItem(atPath: cloned.path)
        try expect((attrs[.size] as! NSNumber).uint64Value == 48 * 1024 * 1024 * 1024, "Large size truncated")
        var originalStat = stat(), cloneStat = stat()
        lstat(huge.path, &originalStat); lstat(cloned.path, &cloneStat)
        try expect(originalStat.st_ino != cloneStat.st_ino && cloneStat.st_blocks < 1024, "Snapshot allocated full payload or used hardlinks")
        let reader = try FileHandle(forReadingFrom: cloned)
        try reader.seek(toOffset: 48 * 1024 * 1024 * 1024 - 4)
        try expect(reader.read(upToCount: 4) == Data("END!".utf8), "Large clone content mismatch")
        try reader.close()
        try expect(adopter.digest(huge) == adopter.digest(cloned), "Full 48 GiB snapshot hash mismatch")
        // Existing destinations are refused by both snapshot and exclusive publication.
        try rejected { try adopter.clone(huge, cloned) }
        try put(root, "move-source", "source")
        try put(root, "move-destination", "destination")
        try rejected { try adopter.move(root.appendingPathComponent("move-source"), root.appendingPathComponent("move-destination")) }
        try expect(get(root, "move-source") == Data("source".utf8) && get(root, "move-destination") == Data("destination".utf8), "Exclusive rename lost a file")
        print("PASS: \(checks) migration assertions, synthetic saves byte-identical, interruption recovery, conflicts, corruption, lock, traversal and 48 GiB COW snapshot")
    }
}
