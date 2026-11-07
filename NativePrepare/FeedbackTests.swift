import Foundation

// Synthetic checks for NativeBootFeedback (App/NativeBootFeedback.swift). Built and run by
// NativePrepare/check_feedback.py with a private home folder; no game files are read.
@main struct FeedbackTests {
    static var checks = 0
    static func check(_ condition: Bool, _ message: String) {
        guard condition else { fatalError("FAIL: " + message) }
        checks += 1
    }
    static func recipe(system: UInt64, bss: UInt64 = 0, used: UInt64 = 0x5a00000, title: String = "010000000000F00D") -> NativePreparationRecipe {
        NativePreparationRecipe(schema: 1, titleID: title, version: "1.0.0", contentVersion: 0, name: "Synthetic", modules: ["main", "sdk"],
                                buildIDs: ["main": String(repeating: "1", count: 32), "sdk": String(repeating: "2", count: 32)],
                                sdkVersions: ["17_5_0"], systemResource: system, usesHeap: true, largeBSS: bss, usedMemory: used,
                                nros: [], reviewedVersion: nil)
    }
    static func signals(_ text: String) -> NativeBootFeedback.LogSignals {
        var s = NativeBootFeedback.LogSignals()
        for line in text.split(separator: "\n") { s.ingest(line) }
        return s
    }
    static func binding(_ recipe: NativePreparationRecipe, package: String = "Synthetic-1") throws -> NativeBootFeedback.Binding {
        NativeBootFeedback.Binding(package: package, titleID: recipe.titleID, version: recipe.version, recipe: recipe,
                                   starterProfileSHA256: NativePreparationRecipe.hash(try recipe.runtime(profiles: [])),
                                   rewriter: NativePreparationRecipe.rewriterRevision)
    }
    /// A format 2 manifest with the fields PreparedIdentity binds.
    static func writePackage(_ recipe: NativePreparationRecipe, at root: URL, rewriter: String = NativePreparationRecipe.rewriterRevision,
                             profileDigest: String? = nil) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let runtime = try recipe.runtime(profiles: [])
        let profile = try recipe.profile(profiles: [])
        func object<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
        let manifest: [String: Any] = [
            "format": 2, "signingStatus": "signed", "contentVersion": recipe.contentVersion, "recipe": try object(recipe),
            "preparedProfile": try object(profile), "titleID": recipe.titleID, "version": recipe.version,
            "enginePin": profile.enginePin, "shaderChain": profile.shaderChain, "shaderInputs": profile.shaderInputs,
            "modules": profile.modules, "environment": profile.environment, "rewriter": rewriter,
            "files": ["profile.tsv": ["bytes": runtime.count, "sha256": profileDigest ?? NativePreparationRecipe.hash(runtime)]]]
        try JSONSerialization.data(withJSONObject: manifest).write(to: root.appendingPathComponent("manifest.json"))
        try runtime.write(to: root.appendingPathComponent("profile.tsv"))
    }

    static func main() throws {
        let home = URL(fileURLWithPath: NSHomeDirectory())
        guard let expected = ProcessInfo.processInfo.environment["FEEDBACK_TEST_HOME"],
              home.standardizedFileURL.path == URL(fileURLWithPath: expected).standardizedFileURL.path,
              NativeBootFeedback.documents.path.hasPrefix(home.path) else { fatalError("Run through check_feedback.py with a private home folder") }

        // Signals: the pinned engine's hle_svc.c lines, with guest text and malformed values ignored.
        let zelda = signals("[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n[guest] RAW_GUEST_TEXT 0xffffffff\n[dm] SetHeapSize OOM: req 0x80000000 > cap 0x8000000\n")
        check(zelda == NativeBootFeedback.LogSignals(largestFailedHeap: 0x84600000, heapOpened: false, aliasMappedHighWater: 0), "largest failed heap request")
        let unity = signals("[dm] [mem] alias region mapped up to +4.00 GB (alias region 4.38 GB)\n[dm] [mem] alias region mapped up to +4.25 GB (alias region 4.38 GB)\n")
        check(unity.aliasMappedHighWater == 4_563_402_752 && !unity.heapOpened && unity.largestFailedHeap == 0, "alias high-water mark")
        let opened = signals("[dm] [mem] heap 1.50 GB (heap region 2.00 GB)\n[dm] [mem] alias region mapped up to +abc GB (alias region 4.38 GB)\n[dm] SetHeapSize OOM: req 0xzz > cap 0x1\n")
        check(opened == NativeBootFeedback.LogSignals(largestFailedHeap: 0, heapOpened: true, aliasMappedHighWater: 0), "heap opened, malformed values ignored")

        // Rule 1: a failed heap request sizes the heap partition (same value as the reviewed Zelda profile).
        let heapRecipe = recipe(system: 0x2000000)
        let heapBinding = try binding(heapRecipe)
        guard let heap = NativeBootFeedback.learn(zelda, binding: try binding(heapRecipe)) else { fatalError("FAIL: heap feedback") }
        check(heap.changes == ["mem_alias 0x118000000 -> 0x98000000"], "heap rule changes only the alias split: \(heap.changes)")
        check(heap.learnedProfileSHA256 != nil && heap.learnedProfile(binding: heapBinding) != nil, "learned heap profile regenerates")
        check(heap.observation.largestFailedHeap == 0x84600000, "aligned request kept")
        // Rule 2: alias pressure without a heap selects a 1 GiB pool (same value as the reviewed Castlevania profile).
        let aliasRecipe = recipe(system: 0x1000000, bss: 0x39af80, used: 0x8864000, title: "010000000000BEEF")
        guard let alias = NativeBootFeedback.learn(unity, binding: try binding(aliasRecipe)) else { fatalError("FAIL: alias feedback") }
        check(alias.changes == ["app_pool 0xcd500000 -> 0x40000000"], "alias rule changes only the pool: \(alias.changes)")
        // A heap that opened, or no signal, keeps the starter profile.
        let both = signals("[dm] [mem] heap 1.00 GB (heap region 1.00 GB)\n[dm] [mem] alias region mapped up to +4.25 GB (alias region 4.38 GB)\n")
        check(NativeBootFeedback.learn(both, binding: try binding(aliasRecipe))?.learnedProfileSHA256 == nil, "opened heap blocks the pool rule")
        let quiet = NativeBootFeedback.learn(NativeBootFeedback.LogSignals(), binding: try binding(heapRecipe))
        check(quiet != nil && quiet!.learnedProfileSHA256 == nil && quiet!.changes.isEmpty && quiet!.learnedProfile(binding: heapBinding) == nil, "no signal, no change")
        // Bounds: an unaligned request rounds up to 2 MiB, one beyond the pool is not a heap rule,
        // and an alias mark is clamped to the original region.
        let odd = NativeBootFeedback.observation(signals("SetHeapSize OOM: req 0x84600001 > cap 0x8000000"), recipe: heapRecipe)
        check(odd?.largestFailedHeap == 0x84800000, "unaligned request rounds up")
        let huge = NativeBootFeedback.observation(signals("SetHeapSize OOM: req 0xffffffffffff > cap 0x8000000"), recipe: heapRecipe)
        check(huge?.largestFailedHeap == 0, "request beyond the pool ignored")
        let far = NativeBootFeedback.observation(signals("[mem] alias region mapped up to +9.50 GB (alias region 4.38 GB)"), recipe: heapRecipe)
        check(far?.aliasMappedHighWater == 0x118000000, "alias mark clamped to the region")
        // A learned record from another package, profile or rewriter never applies.
        let other = try binding(heapRecipe, package: "Synthetic-2")
        check(heap.learnedProfile(binding: other) == nil, "package binding")
        var changed = heapRecipe; changed.usedMemory = 0x6000000
        let changedBinding = try binding(changed)
        check(heap.learnedProfile(binding: changedBinding) == nil, "profile digest binding")

        // Streaming: a signal line across the 1 MiB read boundary, and a log without a final newline.
        let temporary = home.appendingPathComponent("logs", isDirectory: true)
        try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: true)
        let long = String(repeating: "a", count: 1024 * 1024 - 20) + "\n[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n[dm] [mem] alias region mapped up to +1.25 GB (alias region 4.38 GB)"
        try Data(long.utf8).write(to: temporary.appendingPathComponent("host.log"))
        let streamed = NativeBootFeedback.signals(log: temporary.appendingPathComponent("host.log"))
        check(streamed?.largestFailedHeap == 0x84600000 && streamed?.aliasMappedHighWater == 1_342_177_280, "streamed signals across chunks")
        check(NativeBootFeedback.signals(log: temporary.appendingPathComponent("missing.log")) == nil, "missing log")

        // Package binding: only a current-rewriter starter package with its own profile.
        let games = NativeBootFeedback.documents.appendingPathComponent("PreparedGames", isDirectory: true)
        let root = games.appendingPathComponent("Synthetic-1", isDirectory: true)
        try writePackage(heapRecipe, at: root)
        check(NativeBootFeedback.binding(packageRoot: root) != nil, "starter package binds")
        var reviewed = heapRecipe; reviewed.reviewedVersion = "1.0.0"
        try writePackage(heapRecipe, at: games.appendingPathComponent("Old"), rewriter: "bbc3b73784751d2a706932dc69fd67627e3445d4")
        check(NativeBootFeedback.binding(packageRoot: games.appendingPathComponent("Old")) == nil, "earlier rewriter refused")
        var observed = heapRecipe; observed.memoryObservation = .init(largestFailedHeap: 0x84600000, heapOpened: false, aliasMappedHighWater: 0)
        try writePackage(observed, at: games.appendingPathComponent("Observed"))
        check(NativeBootFeedback.binding(packageRoot: games.appendingPathComponent("Observed")) == nil, "already corrected recipe refused")
        try writePackage(heapRecipe, at: games.appendingPathComponent("Altered"), profileDigest: String(repeating: "0", count: 64))
        check(NativeBootFeedback.binding(packageRoot: games.appendingPathComponent("Altered")) == nil, "altered profile refused")

        // Launch flow: first launch starter, the log teaches, the next launch is learned.
        let runtime = NativeBootFeedback.runtimeURL(titleID: heapRecipe.titleID)
        check(NativeBootFeedback.prepareLaunch(packageRoot: root) == nil, "first launch uses the starter profile")
        guard let first = NativeBootFeedback.launchRecord(titleID: heapRecipe.titleID) else { fatalError("FAIL: launch record") }
        check(first.kind == .starter && first.package == "Synthetic-1", "starter launch recorded")
        func session(_ package: String, _ time: Double) throws {
            try JSONSerialization.data(withJSONObject: ["package": package, "stage": "boot", "result": "returned 0", "time": time])
                .write(to: runtime.appendingPathComponent("session.json"))
        }
        try Data("[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n".utf8).write(to: runtime.appendingPathComponent("host.log"))
        try session("Synthetic-1", first.time - 10)
        check(NativeBootFeedback.collect(packageRoot: root) == nil, "a session older than the launch is not read")
        try session("Synthetic-2", first.time + 1)
        check(NativeBootFeedback.collect(packageRoot: root) == nil, "another package's session is not read")
        try session("Synthetic-1", first.time + 1)
        guard let learned = NativeBootFeedback.prepareLaunch(packageRoot: root) else { fatalError("FAIL: learned launch") }
        check(learned.lastPathComponent == NativeBootFeedback.profileName && learned.deletingLastPathComponent().standardizedFileURL == runtime.standardizedFileURL, "learned profile in the runtime folder")
        var corrected = heapRecipe; corrected.memoryObservation = heap.observation
        check(try Data(contentsOf: learned) == corrected.runtime(profiles: []), "learned profile equals the observed recipe's profile")
        check(NativeBootFeedback.launchRecord(titleID: heapRecipe.titleID)?.kind == .learned, "learned launch recorded")
        // The learned launch's own log never adjusts the settings again.
        try Data("[dm] [mem] alias region mapped up to +0.75 GB (alias region 0.98 GB)\n[dm] SetHeapSize OOM: req 0xa0000000 > cap 0x98000000\n".utf8).write(to: runtime.appendingPathComponent("host.log"))
        try session("Synthetic-1", Date().timeIntervalSince1970 + 5)
        _ = NativeBootFeedback.prepareLaunch(packageRoot: root)
        let stored = NativeBootFeedback.load(package: "Synthetic-1")
        check(stored?.observation.largestFailedHeap == 0x84600000 && stored?.learnedLaunches == 2, "learned settings fixed after derivation")
        check(stored?.graphics == nil, "no render condition signal, none added")
        // Render conditions: from a starter launch together with memory, or added once by the first
        // learned launch when the starter launch failed before drawing; never later.
        let drawn = signals("[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n[dm] [RED] SetRenderEnableConditional: the host draws anyway for now\n")
        check(drawn.renderConditions && signals("[dm] [RED] SetRenderEnable(false): the host draws anyway for now").renderConditions, "render condition signals")
        let both2 = NativeBootFeedback.learn(drawn, binding: heapBinding)
        check(both2?.changes == ["mem_alias 0x118000000 -> 0x98000000", "render_cond 0 -> 1"] && both2?.learnedProfile(binding: heapBinding) != nil, "starter launch teaches both")
        check(NativeBootFeedback.learn(signals("[dm] [RED] SetRenderEnableConditional: the host draws anyway for now"), binding: heapBinding)?.changes == ["render_cond 0 -> 1"], "render conditions alone")
        let added = heap.adding(drawn, binding: heapBinding)
        check(added?.changes == ["mem_alias 0x118000000 -> 0x98000000", "render_cond 0 -> 1"] && added?.observation.largestFailedHeap == 0x84600000, "learned launch adds render conditions")
        check(added?.adding(drawn, binding: heapBinding) == nil && heap.adding(zelda, binding: heapBinding) == nil, "render conditions added at most once, only with the signal")
        // Through the store: the first learned launch adds it, the next launch uses it, later launches change nothing.
        try Data("[dm] [RED] SetRenderEnableConditional: the host draws anyway for now\n".utf8).write(to: runtime.appendingPathComponent("host.log"))
        let current = NativeBootFeedback.load(package: "Synthetic-1")!
        try JSONEncoder().encode(NativeBootFeedback.LaunchRecord(package: "Synthetic-1", kind: .learned, profileSHA256: current.learnedProfileSHA256!, time: Date().timeIntervalSince1970)).write(to: runtime.appendingPathComponent(NativeBootFeedback.launchName))
        try session("Synthetic-1", Date().timeIntervalSince1970 + 20)
        check(NativeBootFeedback.collect(packageRoot: root)?.graphics == nil, "a learned launch after the first is not read")
        var first2 = current; first2.learnedLaunches = 1; try first2.save()
        guard let withGraphics = NativeBootFeedback.collect(packageRoot: root), withGraphics.graphics?.renderConditions == true else { fatalError("FAIL: first learned launch adds render conditions") }
        guard let next = NativeBootFeedback.prepareLaunch(packageRoot: root), String(decoding: try Data(contentsOf: next), as: UTF8.self).contains("render_cond\t1") else { fatalError("FAIL: render conditions applied") }
        check(NativeBootFeedback.load(package: "Synthetic-1")?.learnedLaunches == 2, "launch count kept")
        try session("Synthetic-1", Date().timeIntervalSince1970 + 30)
        check(NativeBootFeedback.collect(packageRoot: root)?.learnedProfileSHA256 == withGraphics.learnedProfileSHA256, "no further change")
        // Forget: the next launch is a starter launch and the learned file is gone.
        NativeBootFeedback.reset(package: "Synthetic-1")
        check(NativeBootFeedback.load(package: "Synthetic-1") == nil && !FileManager.default.fileExists(atPath: learned.path), "reset removes learned settings")
        check(NativeBootFeedback.launchRecord(titleID: heapRecipe.titleID) == nil && NativeBootFeedback.collect(packageRoot: root) == nil, "the teaching launch is not read again")
        check(NativeBootFeedback.prepareLaunch(packageRoot: root) == nil && NativeBootFeedback.launchRecord(titleID: heapRecipe.titleID)?.kind == .starter, "starter again after reset")
        // A launch with no rule to apply is read again later, so a longer boot can still teach.
        try Data("[dm] frame 300\n".utf8).write(to: runtime.appendingPathComponent("host.log"))
        try session("Synthetic-1", Date().timeIntervalSince1970 + 10)
        check(NativeBootFeedback.collect(packageRoot: root)?.learnedProfileSHA256 == nil, "quiet boot stored without change")
        try Data("[dm] frame 300\n[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n".utf8).write(to: runtime.appendingPathComponent("host.log"))
        check(NativeBootFeedback.collect(packageRoot: root)?.learnedProfileSHA256 != nil, "the same launch's longer log teaches")
        // A reviewed recipe or a format 1 package records an other launch and gets nothing.
        check((try? reviewed.runtime(profiles: [])) == nil, "a reviewed recipe never yields a starter profile")
        let packaged = games.appendingPathComponent("Reviewed", isDirectory: true)
        try FileManager.default.createDirectory(at: packaged, withIntermediateDirectories: true)
        try JSONSerialization.data(withJSONObject: ["format": 1, "titleID": "010000000000CAFE", "version": "1.0.0"]).write(to: packaged.appendingPathComponent("manifest.json"))
        check(NativeBootFeedback.prepareLaunch(packageRoot: packaged) == nil && NativeBootFeedback.launchRecord(titleID: "010000000000CAFE")?.kind == .other, "reviewed package untouched")

        // Copied diagnostics: fixed fields, no shader keys, guest text or raw log lines.
        var report = NativeGameReport(package: "Synthetic-1", titleID: heapRecipe.titleID, stage: "boot", result: "returned 0", message: "Synthetic")
        report.ingest("[dm] [RED] PhoneShader key=0123456789abcdef stage=1 prepare failed status=-4\n[dm] PhoneShader key=fedcba9876543210 variant=b compile failed code=3 private compiler text\n[guest] RAW_GUEST_TEXT\n[dm] ServiceIPC service=acc:u0 command=10 status=unknown caller=main+0x1234\n[dm] SVC 0x52 x0=0x1 (placeholder returns success)\n[dm] CRASH sig=11 addr=0x10 pc=main+0x20 lr=main+0x24 sp=0x999\n")
        check(report.shaderFailures.count == 2, "variant compile failure is reported: \(report.shaderFailures)")
        let text = report.diagnosticText(packageRoot: root, feedback: NativeBootFeedback.load(package: "Synthetic-1"), appVersion: "1.0 (1)", device: "Synthetic, iOS 27.1")
        for line in ["Satchel game diagnostics", "Title: 010000000000F00D version 1.0.0", "Settings: starter profile with learned memory settings",
                     "Rewriter revision: " + NativePreparationRecipe.rewriterRevision, "Crash: CRASH sig=11 addr=0x10 pc=main+0x20 lr=main+0x24",
                     "  ServiceIPC service=acc:u0 command=10 status=unknown caller=main+0x1234", "Unimplemented SVCs (1): SVC 0x52",
                     "  1 x PhoneShader key=<key> stage=1 prepare failed status=-4: native translation failed",
                     "  1 x PhoneShader key=<key> variant=b compile failed code=3",
                     "Memory: Learned settings: mem_alias 0x118000000 -> 0x98000000"] {
            check(text.contains(line), "diagnostics contain \(line)\n\(text)")
        }
        for secret in ["0123456789abcdef", "fedcba9876543210", "RAW_GUEST_TEXT", "private compiler", "sp=0x999"] {
            check(!text.contains(secret), "diagnostics omit \(secret)")
        }
        print("PASS: \(checks) boot feedback, binding, launch, retry and diagnostics assertions")
    }
}
