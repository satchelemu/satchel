// Synthetic data only. This is appended to the actual launch verifier by run.py.
let testRoot = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent(UUID().uuidString)
let fm = FileManager.default
try fm.createDirectory(at: testRoot, withIntermediateDirectories: true)
defer { try? fm.removeItem(at: testRoot) }
var checks = 0
func check(_ condition: @autoclosure () -> Bool, _ message: String) {
    precondition(condition(), message); checks += 1
}
func rejects(_ message: String, _ body: () throws -> Void) {
    do { try body(); fatalError("Accepted: " + message) }
    catch { checks += 1 }
}
func digest(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
func write(_ data: Data, _ url: URL) throws {
    try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try data.write(to: url)
}
func json(_ value: Any, _ url: URL) throws { try write(JSONSerialization.data(withJSONObject: value), url) }
let profileBytes = Data("Synthetic profile".utf8)
let profile = NativeGameProfile(titleID: "010000000000FFFF", version: "1", name: "Synthetic game",
    packageName: "Synthetic", phonePrepare: true, enginePin: "synthetic-engine", shaderChain: "synthetic-chain",
    shaderInputs: "synthetic-inputs", profileSHA256: digest(profileBytes), modules: ["main"],
    nroInventorySHA256: nil, nroBuildIDs: nil, buildIDs: ["main": "1234"], environment: [:],
    requiredFiles: ["profile.tsv", "game.dylib", "romfs.img"])
let package = testRoot.appendingPathComponent("Documents/PreparedGames/Synthetic")
let payloads = ["profile.tsv": profileBytes, "game.dylib": Data("synthetic signed payload".utf8),
                "romfs.img": Data(repeating: 7, count: 5 * 1024 * 1024),
                "dm/main.fixups.tsv": Data("# BUILD_ID 1234\n".utf8), "dm/main.gates.tsv": Data("synthetic gates".utf8)]
var records: [String: Any] = [:]
for (name, data) in payloads {
    try write(data, package.appendingPathComponent(name))
    records[name] = ["bytes": data.count, "sha256": digest(data)]
}
var manifest: [String: Any] = ["format": 1, "signingStatus": "signed", "titleID": profile.titleID,
    "version": profile.version, "enginePin": profile.enginePin, "shaderChain": profile.shaderChain,
    "shaderInputs": profile.shaderInputs, "modules": profile.modules, "environment": profile.environment,
    "files": records]
let manifestURL = package.appendingPathComponent("manifest.json")
try json(manifest, manifestURL)
let documents = testRoot.appendingPathComponent("Documents")
let base = documents.appendingPathComponent("Homebrew/" + String(repeating: "a", count: 64) + ".xci")
let update = documents.appendingPathComponent("Homebrew/" + String(repeating: "b", count: 64) + ".nsp")
let newer = documents.appendingPathComponent("Homebrew/" + String(repeating: "c", count: 64) + ".nsp")
let unrelated = documents.appendingPathComponent("Homebrew/" + String(repeating: "d", count: 64) + ".xci")
let art = testRoot.appendingPathComponent("Support/GameArt/title-010000000000FFFF.jpg")
let save = documents.appendingPathComponent("NativeRuntime/010000000000FFFF/save.dat")
let cloudSource = testRoot.appendingPathComponent("Files/source.xci")
let pack = documents.appendingPathComponent("ShaderPacks/Synthetic")
func originals() throws {
    for file in [base, update, newer, unrelated, art, save, cloudSource] { try write(Data("synthetic original".utf8), file) }
    try write(Data("synthetic shader".utf8), pack.appendingPathComponent("shaders/a.metallib"))
    try json(["files": ["shaders/a.metallib": ["bytes": 16]]], pack.appendingPathComponent("shader-pack.json"))
}
func remain() {
    for file in [base, update, pack, art, save, cloudSource] { check(fm.fileExists(atPath: file.path), "Must remain: " + file.lastPathComponent) }
}
try originals()
_ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true)
check(fm.fileExists(atPath: PreparedPackage.stampURL(for: package).path), "Full verifier wrote its stamp")
remain()
manifest["signingStatus"] = "needs signature"; try json(manifest, manifestURL)
rejects("unsigned package") { _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true) }
remain()
manifest["signingStatus"] = "signed"; manifest["enginePin"] = "old-engine"; try json(manifest, manifestURL)
rejects("engine change") { _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true) }
remain()
manifest["enginePin"] = profile.enginePin; try json(manifest, manifestURL)
try write(Data("unbound".utf8), package.appendingPathComponent("extra"))
rejects("unbound package file") { _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true) }
try fm.removeItem(at: package.appendingPathComponent("extra")); remain()
_ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true)
let rom = package.appendingPathComponent("romfs.img")
let timestamp = try fm.attributesOfItem(atPath: rom.path)[.modificationDate]!
try Data(repeating: 8, count: payloads["romfs.img"]!.count).write(to: rom)
try fm.setAttributes([.modificationDate: timestamp], ofItemAtPath: rom.path)
rejects("same-size corruption despite a prior verification stamp") {
    _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true)
}
remain()
try payloads["romfs.img"]!.write(to: rom)
let cancelled = PrepareCancellation(); cancelled.cancel()
rejects("cancel before verification") {
    _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true, cancelled: { cancelled.cancelled })
}
var calls = 0
rejects("cancel during verification") {
    _ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true, cancelled: { calls += 1; return calls > 4 })
}
remain()
rejects("cancel wins the commit race") { try cancelled.beginFinalizing() }
let committed = PrepareCancellation(); try committed.beginFinalizing()
check(!committed.cancel() && !committed.cancelled, "Late cancel cannot relabel committed cleanup")
let defaults = UserDefaults(suiteName: "cleanup-tests-" + UUID().uuidString)!
check(PreparedImportCleanup.enabled(in: defaults), "Automatic cleanup defaults on")
defaults.set(false, forKey: PreparedImportCleanup.settingKey)
check(!PreparedImportCleanup.enabled(in: defaults), "Keep-originals opt-out")
remain()
defaults.removeObject(forKey: PreparedImportCleanup.settingKey)
rejects("save directory never accepted as import") {
    _ = try PreparedImportCleanup.plan(documents: documents, originals: [save], shaderPack: pack)
}
for ext in ["nsz", "xcz"] {
    let compressed = documents.appendingPathComponent("Homebrew/" + String(repeating: "f", count: 64) + "." + ext)
    try write(Data("synthetic compressed import".utf8), compressed)
    let plan = try PreparedImportCleanup.plan(documents: documents, originals: [compressed], shaderPack: nil)
    check(plan.candidates.count == 1 && plan.remove().isEmpty, "Compressed import cleanup")
    check(!fm.fileExists(atPath: compressed.path), "Compressed original removed")
    try fm.createSymbolicLink(at: compressed, withDestinationURL: cloudSource)
    rejects("compressed symlink import") { _ = try PreparedImportCleanup.plan(documents: documents, originals: [compressed], shaderPack: nil) }
    try fm.removeItem(at: compressed)
}
let link = documents.appendingPathComponent("Homebrew/" + String(repeating: "e", count: 64) + ".xci")
try fm.createSymbolicLink(at: link, withDestinationURL: cloudSource)
rejects("symlink import") { _ = try PreparedImportCleanup.plan(documents: documents, originals: [link], shaderPack: pack) }
try fm.removeItem(at: link)
try write(Data("shared".utf8), pack.appendingPathComponent("keep.txt"))
let shared = try PreparedImportCleanup.plan(documents: documents, originals: [], shaderPack: pack)
check(shared.candidates.isEmpty, "Shared shader pack kept")
try fm.removeItem(at: pack.appendingPathComponent("keep.txt"))
let changed = try PreparedImportCleanup.plan(documents: documents, originals: [base], shaderPack: pack)
try write(Data("replacement import".utf8), base)
check(!changed.remove().isEmpty, "Changed original kept with a cleanup warning")
check(fm.fileExists(atPath: base.path), "Replacement survived")
try originals()
var item = HomebrewItem(id: String(repeating: "a", count: 64), name: "Synthetic", bytes: 18,
    textBytes: 0, roBytes: 0, dataBytes: 0, bssBytes: 0, importedAt: Date(),
    package: SwitchPackageSummary(format: .xci, entryCount: 1, contentBytes: 18))
let legacy = try JSONEncoder().encode([item])
let decoded = try JSONDecoder().decode([HomebrewItem].self, from: legacy)
check(decoded[0].preparedSource == nil, "Legacy index loads")
_ = try PreparedPackage.verify(root: package, profiles: [profile], forceFull: true)
let plan = try PreparedImportCleanup.plan(documents: documents, originals: [base, update], shaderPack: pack)
item.preparedSource = PreparedImportSource(titleID: 0x010000000000FFFF, package: "Synthetic", isUpdate: false)
let index = documents.appendingPathComponent("Homebrew/library.json")
try JSONEncoder().encode([item]).write(to: index, options: .atomic)
check(plan.remove().isEmpty, "Verified cleanup succeeded")
for file in [base, update, pack] { check(!fm.fileExists(atPath: file.path), "Duplicate removed") }
for file in [newer, unrelated, save, art, cloudSource, manifestURL] { check(fm.fileExists(atPath: file.path), "Noncandidate preserved") }
let reloaded = try JSONDecoder().decode([HomebrewItem].self, from: Data(contentsOf: index))
check(reloaded.count == 1 && reloaded[0].id == item.id, "Library entry survives relaunch")
check(reloaded[0].preparedSource == item.preparedSource, "Title and package association survive relaunch")
_ = try PreparedPackage.verify(root: package, profiles: [profile])
check(true, "Prepared package verifies after originals are removed")
print("PASS: \(checks) synthetic cleanup and verifier assertions")
