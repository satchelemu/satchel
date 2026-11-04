import Foundation
import CryptoKit
import Darwin

let fm = FileManager.default
let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try fm.createDirectory(at: root, withIntermediateDirectories: true)
let title = "0100000000000001", other = "0100000000000002"
let runtime = root.appendingPathComponent("NativeRuntime")
let save = runtime.appendingPathComponent(title + "/save")
SaveTestGate()
let store = SaveStore(root: runtime, beginAccess: { id, allow in
    if !SatchelBeginSaveAccess(id, allow) { throw SaveFailure(message: "Quit this game before restoring.") }
}, endAccess: { SatchelEndSaveAccess() })
var checks = 0
func check(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
    guard try condition() else { fatalError("FAIL: " + message) }; checks += 1
}
func rejects(_ label: String, _ body: () throws -> Void) throws {
    do { try body(); fatalError("FAIL: accepted " + label) }
    catch { print("PASS: rejected " + label + " (" + error.localizedDescription + ")"); checks += 1 }
}
func write(_ text: String, at path: String = "slot/value.dat") throws {
    let url = save.appendingPathComponent(path)
    try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data(text.utf8).write(to: url)
}
func value(_ directory: URL = save) throws -> String { try String(contentsOf: directory.appendingPathComponent("slot/value.dat"), encoding: .utf8) }
func export(_ name: String) throws -> URL {
    try store.export(title: title, name: "Synthetic / Game", to: root.appendingPathComponent(name))
}

try write("synthetic original")
try fm.createDirectory(at: save.appendingPathComponent("empty"), withIntermediateDirectories: true)
try Data().write(to: save.appendingPathComponent("zero.dat"))
try write("case alias", at: "slot/.dm-case-1")
let aliasName = Array("Value.dat".utf8)
try check(setxattr(save.appendingPathComponent("slot/.dm-case-1").path, SaveStore.guestAttribute, aliasName, aliasName.count, 0, XATTR_NOFOLLOW) == 0, "fixture xattr")
let original = try export("exports")
let initial = try store.summary(title: title)
try check(initial.lastSaved != nil && initial.bytes == 28, "visible last saved and logical byte size")
try write("synthetic replacement")
try store.restore(title: title, from: original)
try check(value() == "synthetic original", "round trip data")
let restoredTime = try store.summary(title: title).lastSaved!
fputs("Synthetic timestamp diagnostic: initial=\(initial.lastSaved!.timeIntervalSince1970) restored=\(restoredTime.timeIntervalSince1970)\n", stderr)
try check(abs(restoredTime.timeIntervalSince(initial.lastSaved!)) < 0.001, "last-saved time preserved on restore")
try check(fm.fileExists(atPath: save.appendingPathComponent("empty").path), "empty directory round trip")
var attr = [UInt8](repeating: 0, count: 50)
let attrCount = getxattr(save.appendingPathComponent("slot/.dm-case-1").path, SaveStore.guestAttribute, &attr, attr.count, 0, XATTR_NOFOLLOW)
try check(attrCount == aliasName.count && Array(attr.prefix(attrCount)) == aliasName, "guest filename mapping round trip")
let firstBackups = try store.summary(title: title).backups
try check(firstBackups.count == 1, "automatic pre-import backup")
try store.restore(title: title, from: firstBackups[0].url)
try check(value() == "synthetic replacement", "restore automatic backup")
print("PASS: round trip, metadata, empty files/directories, guest-name xattr and restore pre-import backup")

try rejects("wrong title") { try store.restore(title: other, from: original) }
let truncated = root.appendingPathComponent("truncated.zip")
var zipData = try Data(contentsOf: original); zipData.removeLast(10); try zipData.write(to: truncated)
try rejects("truncated ZIP") { try store.restore(title: title, from: truncated) }
let damaged = root.appendingPathComponent("damaged.zip")
zipData = try Data(contentsOf: original)
let reader = try SaveArchive.Reader(url: original)
let payload = reader.members.first { $0.name == "save/slot/value.dat" }!
zipData[payload.offset] ^= 0xff; try zipData.write(to: damaged)
try rejects("corrupted payload") { try store.restore(title: title, from: damaged) }
try check(value() == "synthetic replacement", "rejected archives leave current save unchanged")
SaveTestSetTitle(title)
try rejects("import while running") { try store.restore(title: title, from: original) }
try rejects("export while running") { _ = try export("running-export") }
_ = try store.backupIfChanged(title: title)
try check(value() == "synthetic replacement", "backup while running snapshots only")
SaveTestSetTitle(nil)

for index in 0..<5 {
    try write("rotation-\(index)")
    _ = try store.backupIfChanged(title: title)
}
let rotated = try store.summary(title: title).backups
try check(rotated.count == 3, "rotation keeps three backups")
var retained: [String] = []
for backup in rotated {
    let archive = try SaveArchive.Reader(url: backup.url)
    let member = archive.members.first { $0.name == "save/slot/value.dat" }!
    retained.append(String(data: try archive.data(member), encoding: .utf8)!)
}
try check(retained == ["rotation-4", "rotation-3", "rotation-2"], "oldest backups removed first")
let before = Set(rotated.map(\.id))
_ = try store.backupIfChanged(title: title)
try check(Set(store.summary(title: title).backups.map(\.id)) == before, "unchanged save does not rotate backups")
print("PASS: backup rotation, newest ordering and unchanged-save deduplication")

// Damage the current matching backup without changing its manifest. A matching
// inventory must not let a corrupt copy stand in for the pre-import backup.
let damagedBackup = rotated[0].url
var backupData = try Data(contentsOf: damagedBackup)
let backupReader = try SaveArchive.Reader(url: damagedBackup)
let backupPayload = backupReader.members.first { $0.name == "save/slot/value.dat" }!
backupData[backupPayload.offset] ^= 1; try backupData.write(to: damagedBackup)
_ = try store.backupIfChanged(title: title)
let repaired = try store.summary(title: title).backups
try check(repaired.first?.url != damagedBackup, "corrupt matching backup replaced")
let repairedReader = try SaveArchive.Reader(url: repaired[0].url)
try check(String(data: repairedReader.data(repairedReader.members.first { $0.name == "save/slot/value.dat" }!), encoding: .utf8) == "rotation-4", "replacement backup has verified data")

// Rewrite only the inventory using our ZIP writer to exercise validation past
// ZIP framing. All bytes are synthetic; no game packages or saves are inputs.
func malicious(_ label: String, mutate: (inout [String: Any]) -> Void) throws -> URL {
    let input = try SaveArchive.Reader(url: original)
    let url = root.appendingPathComponent(label + ".zip"), output = try SaveArchive.Writer(url: root.appendingPathComponent(label + ".zip"))
    var metadata = try JSONSerialization.jsonObject(with: input.data(input.members[0])) as! [String: Any]
    mutate(&metadata)
    try output.add(name: "satchel-save.json", data: JSONSerialization.data(withJSONObject: metadata, options: [.sortedKeys]))
    for member in input.members.dropFirst() { try output.add(name: member.name, data: input.data(member)) }
    try output.finish(); return url
}
let traversal = try malicious("traversal") { object in
    var entries = object["entries"] as! [[String: Any]]; entries[0]["path"] = "../outside"; object["entries"] = entries
}
try rejects("path traversal") { try store.restore(title: title, from: traversal) }
let tooLarge = try malicious("oversized") { object in
    var entries = object["entries"] as! [[String: Any]]; entries[0]["size"] = SaveArchive.maxBytes + 1; object["entries"] = entries
}
try rejects("oversized inventory") { try store.restore(title: title, from: tooLarge) }
let mismatch = try malicious("hash-mismatch") { object in
    var entries = object["entries"] as! [[String: Any]]
    let index = entries.firstIndex { $0["path"] as? String == "slot/value.dat" }!
    entries[index]["sha256"] = String(repeating: "0", count: 64); object["entries"] = entries
}
try rejects("SHA-256 mismatch") { try store.restore(title: title, from: mismatch) }
try check(value() == "rotation-4", "malformed imports preserve save")

try fm.createSymbolicLink(atPath: save.appendingPathComponent("link").path, withDestinationPath: "slot/value.dat")
try rejects("symlink in source save") { _ = try export("symlink-export") }
try fm.removeItem(at: save.appendingPathComponent("link"))
let tiny = SaveStore(root: runtime, backupBytes: 10)
try write("must survive backup failure")
try rejects("backup failure before restore") { try tiny.restore(title: title, from: original) }
try check(value() == "must survive backup failure", "backup failure preserves original")

let limitedRoot = root.appendingPathComponent("limited")
let limitedSave = limitedRoot.appendingPathComponent(title + "/save")
try fm.createDirectory(at: limitedSave, withIntermediateDirectories: true)
let limited = SaveStore(root: limitedRoot, backupBytes: 2500)
for index in 0..<4 {
    try Data(repeating: UInt8(index), count: 1000).write(to: limitedSave.appendingPathComponent("data"))
    _ = try limited.backupIfChanged(title: title)
}
let bounded = try limited.summary(title: title).backups
try check(bounded.count < 3 && bounded.reduce(Int64(0), { $0 + $1.bytes }) <= 2500, "byte bound removes oldest even below count limit")
print("PASS: size bounds, unsafe paths, symlinks, hashes and failed-backup preservation")

// Interrupted restore scratch is not a live save and is removed on the next access.
let orphan = runtime.appendingPathComponent(title + "/.satchel-save-interrupted")
try fm.createDirectory(at: orphan, withIntermediateDirectories: true)
try Data("unfinished".utf8).write(to: orphan.appendingPathComponent("partial"))
_ = try store.summary(title: title)
try check(!fm.fileExists(atPath: orphan.path), "interrupted operation scratch cleanup")
try check(value() == "must survive backup failure", "scratch cleanup does not touch save")

// The engine's dirty work tree is deliberately outside the archive contract.
let dirty = runtime.appendingPathComponent(title + "/save.work")
try fm.createDirectory(at: dirty, withIntermediateDirectories: true)
try Data("uncommitted".utf8).write(to: dirty.appendingPathComponent("dirty.dat"))
let committedOnly = try export("committed-only")
try check(!SaveArchive.Reader(url: committedOnly).members.contains { $0.name.contains("dirty.dat") || $0.name.contains("save.work") }, "uncommitted work excluded")

let streamingRoot = root.appendingPathComponent("streaming")
let streamingSave = streamingRoot.appendingPathComponent(title + "/save")
try fm.createDirectory(at: streamingSave, withIntermediateDirectories: true)
let payloadURL = streamingSave.appendingPathComponent("large.bin")
let bytes = Data((0..<(3 * SaveArchive.chunkSize + 17)).map { UInt8(truncatingIfNeeded: $0) })
try bytes.write(to: payloadURL)
let streaming = SaveStore(root: streamingRoot)
let streamed = try streaming.export(title: title, name: "Synthetic streaming", to: root.appendingPathComponent("streamed-export"))
try Data("replacement".utf8).write(to: payloadURL)
try streaming.restore(title: title, from: streamed)
try check(Data(contentsOf: payloadURL) == bytes, "streaming restore crosses chunk boundaries")
print("PASS: \(checks) synthetic save checks. No real saves or game inputs used.")
