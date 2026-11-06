// Synthetic packages only. run.py appends this to the real verifier, library scan and shelf state.
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
func write(_ data: Data, _ url: URL) throws {
    try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try data.write(to: url)
}
func digest(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
func object<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }

let games = testRoot.appendingPathComponent("Documents/PreparedGames")
let titleA = "010000000000AAA0", titleB = "010000000000BBB0"
let save = testRoot.appendingPathComponent("Documents/NativeRuntime/\(titleA)/save/progress.dat")
try write(Data("synthetic save".utf8), save)

/// A complete synthetic phone package: a fictional recipe, the profile it generates and every
/// file the launch verifier requires. `enginePin` stands in for a package an older engine made.
@discardableResult
func makePackage(_ name: String, title: String, enginePin: String? = nil, age: TimeInterval = 0,
                 rewriter: String? = NativePreparationRecipe.rewriterRevision, build: String = String(repeating: "a", count: 32)) throws -> URL {
    let recipe = NativePreparationRecipe(schema: 1, titleID: title, version: "1.0.0", contentVersion: 0, name: "Synthetic " + title,
        modules: ["main"], buildIDs: ["main": build], sdkVersions: [], systemResource: 0,
        usesHeap: false, largeBSS: 0, usedMemory: 0x4000000, nros: [], reviewedVersion: nil)
    let profile = try recipe.profile(profiles: [])
    var payloads = ["profile.tsv": try recipe.runtime(profiles: []),
                    "dm/main.fixups.tsv": Data("# BUILD_ID \(build)\n".utf8)]
    for file in profile.requiredFiles + ["dm/main.gates.tsv"] where payloads[file] == nil { payloads[file] = Data("synthetic \(file)".utf8) }
    let package = games.appendingPathComponent(name)
    var records: [String: Any] = [:]
    for (file, data) in payloads {
        try write(data, package.appendingPathComponent(file))
        records[file] = ["bytes": data.count, "sha256": digest(data)]
    }
    var manifest: [String: Any] = ["format": 2, "signingStatus": "signed", "contentVersion": 0, "titleID": title,
        "version": recipe.version, "recipe": try object(recipe), "preparedProfile": try object(profile),
        "enginePin": enginePin ?? profile.enginePin, "shaderChain": profile.shaderChain, "shaderInputs": profile.shaderInputs,
        "modules": profile.modules, "environment": profile.environment, "files": records]
    if let rewriter { manifest["rewriter"] = rewriter }
    let url = package.appendingPathComponent("manifest.json")
    try write(JSONSerialization.data(withJSONObject: manifest), url)
    try fm.setAttributes([.modificationDate: Date().addingTimeInterval(-age)], ofItemAtPath: url.path)
    return package
}
func folders(_ title: String) -> [String] {
    PreparedGame.scan().filter { $0.applicationID == UInt64(title, radix: 16)! }.map(\.folder).sorted()
}
func stamp(_ package: URL) -> Bool { fm.fileExists(atPath: PreparedPackage.stampURL(for: package).path) }

// 1. A package from another engine is not current, needs Prepare and is never Play's choice.
let stale = try makePackage("Prepared-stale", title: titleA, enginePin: "previous-engine", age: 60)
rejects("launch verifier refuses another engine") { _ = try PreparedPackage.verify(root: stale, profiles: [], forceFull: true) }
let staleManifest = try Data(contentsOf: stale.appendingPathComponent("manifest.json"))
check(!PreparedIdentity.isCurrent(staleManifest, profiles: []), "Another engine pin is not current")
let store = GameArtworkStore()
store.prepared = PreparedGame.scan()
check(store.prepared.count == 1 && store.prepared[0].current == false, "Scan marks the old-engine package stale")
let base = HomebrewItem(id: String(repeating: "1", count: 64), name: "Synthetic A", bytes: 18, textBytes: 0, roBytes: 0,
    dataBytes: 0, bssBytes: 0, importedAt: Date(), package: SwitchPackageSummary(format: .xci, entryCount: 1, contentBytes: 18))
store.infos[base.id] = GameInfo(applicationID: UInt64(titleA, radix: 16)!, kind: .application)
check(store.state(base) == .needsPreparing, "An imported game with only a stale package needs preparing")
check(store.preparedPackage(for: base) == nil && !store.isPrepared(base), "Play never starts a stale package")
check(store.needsPrepareAgain(base) && store.newestPackage(for: base)?.folder == "Prepared-stale", "The stale package is still known")
var orphan = store.preparedOnlyItems([], name: { _ in "Synthetic A" })
check(orphan.count == 1 && orphan[0].id == "prepared:Prepared-stale", "A prepared-only title keeps its tile")
check(store.state(orphan[0]) == .needsPreparing && store.preparedPackage(for: orphan[0]) == nil, "A stale prepared-only tile needs preparing")
var removed = base; removed.preparedSource = PreparedImportSource(titleID: UInt64(titleA, radix: 16)!, package: "Prepared-stale", isUpdate: false)
check(store.state(removed) == .needsPreparing && store.preparedPackage(for: removed) == nil, "A retained record whose original was removed needs preparing")
// The same check as launch: a current manifest passes both, and is the one Play starts.
let earlier = try makePackage("Prepared-earlier", title: titleA, age: 120)
_ = try PreparedPackage.verify(root: earlier, profiles: [], forceFull: true)
check(stamp(earlier), "The current package verified and has its stamp")
store.refreshPrepared()
check(store.prepared.first { $0.folder == "Prepared-earlier" }?.current == true, "Scan marks the current package current")
check(store.state(base) == .ready && store.preparedPackage(for: base)?.folder == "Prepared-earlier", "A current package is ready, even when older than a stale one")
check(!store.needsPrepareAgain(base) && store.newestPackage(for: base)?.folder == "Prepared-stale", "Newest-any and Play choice differ")
orphan = store.preparedOnlyItems([], name: { _ in "Synthetic A" })
check(orphan.count == 1 && orphan[0].id == "prepared:Prepared-earlier" && store.state(orphan[0]) == .ready, "A prepared-only tile prefers the current package")
for (key, value) in [("shaderChain", "previous-chain"), ("shaderInputs", "previous-inputs")] {
    let url = earlier.appendingPathComponent("manifest.json")
    var manifest = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [String: Any]
    manifest[key] = value
    let changed = try JSONSerialization.data(withJSONObject: manifest)
    check(!PreparedIdentity.isCurrent(changed, profiles: []), "Another \(key) is not current")
}

// 2. Replacement after Prepare. A stale package keeps the stamp it had under its own engine.
try write(Data("{}".utf8), PreparedPackage.stampURL(for: stale))
let other = try makePackage("Prepared-other", title: titleB, age: 30)
_ = try PreparedPackage.verify(root: other, profiles: [], forceFull: true)
let unreadable = games.appendingPathComponent("Prepared-unreadable")
try write(Data("not a manifest".utf8), unreadable.appendingPathComponent("manifest.json"))
// A failed Prepare (its package does not verify) removes nothing.
let new = try makePackage("Prepared-new", title: titleA)
let rom = new.appendingPathComponent("romfs.img")
let romData = try Data(contentsOf: rom)
try Data("damaged".utf8).write(to: rom)
rejects("a failed Prepare replaces nothing") { _ = try PreparedPackage.removeReplaced(by: new, profiles: []) }
check(folders(titleA) == ["Prepared-earlier", "Prepared-new", "Prepared-stale"] && stamp(stale) && stamp(earlier), "Failed Prepare keeps older packages")
try romData.write(to: rom)
// A cancelled Prepare removes nothing.
rejects("a cancelled Prepare replaces nothing") { _ = try PreparedPackage.removeReplaced(by: new, profiles: [], cancelled: { true }) }
check(folders(titleA) == ["Prepared-earlier", "Prepared-new", "Prepared-stale"] && stamp(stale) && stamp(earlier), "Cancelled Prepare keeps older packages")
// A removal refused (a game session is open) keeps the package and reports it; the new one stays.
sessionStarted = true
var failures = try PreparedPackage.removeReplaced(by: new, profiles: [])
sessionStarted = false
check(failures.count == 2 && failures.allSatisfy { $0.contains("A game is running") }, "Refused removals are reported")
check(folders(titleA) == ["Prepared-earlier", "Prepared-new", "Prepared-stale"], "Refused removals keep every package")
// A package a running job uses is kept.
failures = try PreparedPackage.removeReplaced(by: new, keeping: ["Prepared-earlier"], profiles: [])
check(failures.isEmpty && folders(titleA) == ["Prepared-earlier", "Prepared-new"] && !stamp(stale), "A package in use is kept")
// Success: exactly one package folder for the title; stamps, saves and other titles are untouched.
failures = try PreparedPackage.removeReplaced(by: new, profiles: [])
check(failures.isEmpty && folders(titleA) == ["Prepared-new"], "Successful Prepare leaves exactly one package for its title")
check(!stamp(earlier) && !stamp(stale) && stamp(new), "Older verify stamps removed, the new stamp kept")
check(fm.fileExists(atPath: other.path) && stamp(other) && fm.fileExists(atPath: unreadable.path), "Other titles and unreadable folders kept")
check((try? String(contentsOf: save, encoding: .utf8)) == "synthetic save", "Saves untouched")
store.refreshPrepared()
check(store.state(base) == .ready && store.preparedPackage(for: base)?.folder == "Prepared-new", "The new package is Play's choice")
rejects("a package outside PreparedGames") { _ = try PreparedPackage.removeReplaced(by: testRoot.appendingPathComponent("Elsewhere"), profiles: []) }

// 3. The rewriter reference. A package written before Prepare recorded it was rewritten by the
// bbc3b737 reference: current only when every module build is one proven unchanged since.
let titleC = "010000000000CCC0"
let unchanged = try makePackage("Prepared-rewriter-unchanged", title: titleC, rewriter: nil, build: "e7caa6ab07f133269a2705ec9340ac58")
let unchangedManifest = try Data(contentsOf: unchanged.appendingPathComponent("manifest.json"))
check(PreparedIdentity.isCurrent(unchangedManifest, profiles: []), "An earlier package of a build proven unchanged stays current")
_ = try PreparedPackage.verify(root: unchanged, profiles: [], forceFull: true)
let changedBuild = try makePackage("Prepared-rewriter-changed", title: titleC, rewriter: nil)
let changedManifest = try Data(contentsOf: changedBuild.appendingPathComponent("manifest.json"))
check(!PreparedIdentity.isCurrent(changedManifest, profiles: []), "An earlier package of any other build needs Prepare again")
rejects("launch verifier refuses an unproven earlier rewrite") { _ = try PreparedPackage.verify(root: changedBuild, profiles: [], forceFull: true) }
let otherRewriter = try makePackage("Prepared-rewriter-other", title: titleC, rewriter: "another-rewriter", build: "e7caa6ab07f133269a2705ec9340ac58")
let otherManifest = try Data(contentsOf: otherRewriter.appendingPathComponent("manifest.json"))
check(!PreparedIdentity.isCurrent(otherManifest, profiles: []), "Another recorded rewriter is not current, whatever its builds")
print("PASS: \(checks) stale and replaced package assertions")
