import Foundation
import CryptoKit

/// Automatic boot feedback for games prepared on the starter profile (no reviewed settings).
///
/// After a launch with the package's own starter profile, the app reads the host log for the
/// three bounded memory signals of `NativePreparationRecipe.MemoryObservation` (a failed heap
/// request, whether a heap opened, the alias region high-water mark) and two graphics signals
/// (the game used conditional rendering, which the host drew through, and the host's count of
/// textures whose CPU-written content their GPU copy missed, which it never uploaded). When the
/// generic rules change the runtime profile, the next launch uses the regenerated profile. Nothing
/// else from the log is kept: no guest text, no game code or data, no save data and no keys.
///
/// Retry policy: memory settings come only from a launch that used the package's original
/// starter profile, bound to the package name, its profile digest and the rewriter revision, and
/// are fixed once derived. The one-way graphics settings (render conditions, texture uploads) may
/// also come from the first learned launch (a game that failed its heap request draws nothing
/// before it), in one step. So at most two corrections are made and a failing one cannot repeat
/// itself; "Forget learned settings" or
/// Prepare again starts over. Reviewed profiles, packages prepared with an observation and
/// packages from an earlier rewriter are left alone.
struct NativeBootFeedback: Codable {
    var schema = 1
    let package: String
    let titleID: String
    let version: String
    /// The package's own starter profile digest (manifest `files["profile.tsv"]`).
    let starterProfileSHA256: String
    let rewriter: String
    let observation: NativePreparationRecipe.MemoryObservation
    /// Set when a starter or the first learned launch showed conditional rendering or texture writes.
    var graphics: NativePreparationRecipe.GraphicsObservation? = nil
    /// The host's count of textures whose GPU copy missed CPU writes, when it taught the uploads.
    var textureWrites: UInt64? = nil
    /// When the observed starter launch began.
    let observedAt: Date
    /// The profile the next launch uses, or nil when no rule changes the starter profile.
    let learnedProfileSHA256: String?
    /// One line per changed profile key, "key old -> new".
    let changes: [String]
    var learnedLaunches = 0

    /// The launch profile the previous session used, written just before the host starts.
    struct LaunchRecord: Codable, Equatable {
        enum Kind: String, Codable { case starter, learned, other }
        let package: String
        let kind: Kind
        let profileSHA256: String
        let time: Double
    }

    /// The bounded facts a package exposes to feedback, read from its manifest.
    struct Binding {
        let package: String
        let titleID: String
        let version: String
        let recipe: NativePreparationRecipe
        let starterProfileSHA256: String
        let rewriter: String
    }

    static let profileName = "learned-profile.tsv"
    static let launchName = "launch-profile.json"

    // MARK: Locations

    static var documents: URL { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0] }
    static func storeURL(package: String) -> URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("LearnedSettings", isDirectory: true)
            .appendingPathComponent(NativePreparationRecipe.hash(Data(package.utf8)) + ".json")
    }
    static func runtimeURL(titleID: String) -> URL { documents.appendingPathComponent("NativeRuntime/" + titleID, isDirectory: true) }
    static func packageURL(package: String) -> URL? {
        guard !package.isEmpty, !package.contains("/"), package != ".", package != ".." else { return nil }
        return documents.appendingPathComponent("PreparedGames/" + package, isDirectory: true)
    }

    // MARK: Binding

    /// Only a current-rewriter format 2 package whose own profile is the generated starter
    /// profile, with no reviewed version and no observation already in its recipe.
    static func binding(packageRoot: URL) -> Binding? {
        let manifest = packageRoot.appendingPathComponent("manifest.json")
        guard let size = try? manifest.resourceValues(forKeys: [.fileSizeKey]).fileSize, size < 32 * 1024 * 1024,
              let data = try? Data(contentsOf: manifest),
              let identity = try? JSONDecoder().decode(PreparedIdentity.self, from: data),
              identity.format == 2, let recipe = identity.recipe, recipe.reviewedVersion == nil, recipe.memoryObservation == nil,
              recipe.graphicsObservation == nil,
              let rewriter = identity.rewriter, rewriter == NativePreparationRecipe.rewriterRevision,
              recipe.titleID == identity.titleID, recipe.version == identity.version,
              let runtime = try? recipe.runtime(profiles: []),
              let recorded = identity.files["profile.tsv"]?.sha256,
              NativePreparationRecipe.hash(runtime) == recorded,
              identity.preparedProfile?.profileSHA256 == recorded else { return nil }
        return Binding(package: packageRoot.lastPathComponent, titleID: identity.titleID, version: identity.version,
                       recipe: recipe, starterProfileSHA256: recorded, rewriter: rewriter)
    }

    func matches(_ binding: Binding) -> Bool {
        package == binding.package && titleID == binding.titleID && version == binding.version &&
            starterProfileSHA256 == binding.starterProfileSHA256 && rewriter == binding.rewriter
    }

    // MARK: Observation

    /// Bounded numeric memory signals from a host log (hle_svc.c in the pinned engine). The
    /// line formats follow the MIT nxnative dm/games/profile_guess.py heap_signals reader.
    struct LogSignals: Equatable {
        var largestFailedHeap: UInt64 = 0
        var heapOpened = false
        var aliasMappedHighWater: UInt64 = 0
        var renderConditions = false
        /// The largest count of the host's texture-write watch (hle_nvn.c, texture-writes.patch).
        var textureWritesMissed: UInt64 = 0
        mutating func ingest(_ line: Substring) {
            if line.contains("SetRenderEnableConditional: the host draws anyway") || line.contains("SetRenderEnable(false): the host draws anyway") {
                renderConditions = true
            } else if let range = line.range(of: "[nvn] texture writes the GPU copy misses: ") {
                let digits = line[range.upperBound...].prefix { $0.isNumber }
                if digits.count <= 9, line[range.upperBound...].dropFirst(digits.count).hasPrefix(" textures"), let value = UInt64(digits) {
                    textureWritesMissed = max(textureWritesMissed, value)
                }
            } else if line.contains("SetHeapSize OOM: req 0x"), let value = Self.hex(after: "SetHeapSize OOM: req 0x", in: line) {
                largestFailedHeap = max(largestFailedHeap, value)
            } else if line.contains("[mem] heap "), line.contains(" GB (heap region ") {
                heapOpened = true
            } else if let range = line.range(of: "[mem] alias region mapped up to +") {
                let digits = line[range.upperBound...].prefix { $0.isNumber || $0 == "." }
                if line[range.upperBound...].dropFirst(digits.count).hasPrefix(" GB"), digits.count <= 8,
                   let gigabytes = Double(digits), gigabytes.isFinite, gigabytes >= 0, gigabytes < 64 {
                    aliasMappedHighWater = max(aliasMappedHighWater, UInt64(gigabytes * 1_073_741_824))
                }
            }
        }
        static func hex(after marker: String, in line: Substring) -> UInt64? {
            guard let range = line.range(of: marker) else { return nil }
            let digits = line[range.upperBound...].prefix { $0.isHexDigit }
            guard !digits.isEmpty, digits.count <= 16 else { return nil }
            return UInt64(digits, radix: 16)
        }
    }

    /// Streams at most `limit` bytes of the log, line by line, keeping only the memory signals.
    static func signals(log url: URL, limit: Int = 256 * 1024 * 1024) -> LogSignals? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        var signals = LogSignals(), pending = Data(), read = 0
        while read < limit {
            let more: Data? = autoreleasepool { try? handle.read(upToCount: min(1024 * 1024, limit - read)) }
            guard let chunk = more, !chunk.isEmpty else { break }
            read += chunk.count
            pending.append(chunk)
            guard let last = pending.lastIndex(of: 0x0a) else {
                if pending.count > 64 * 1024 { pending.removeAll(keepingCapacity: true) }
                continue
            }
            let text = String(decoding: pending[pending.startIndex..<last], as: UTF8.self)
            for line in text.split(separator: "\n") where line.utf8.count <= 4096 { signals.ingest(line) }
            pending = Data(pending[pending.index(after: last)...])
        }
        if !pending.isEmpty, pending.count <= 4096 {
            for line in String(decoding: pending, as: UTF8.self).split(separator: "\n") { signals.ingest(line) }
        }
        return signals
    }

    /// The recipe's bounded observation for these signals, or nil when it would be invalid.
    static func observation(_ signals: LogSignals, recipe: NativePreparationRecipe) -> NativePreparationRecipe.MemoryObservation? {
        let aliasLimit: UInt64 = recipe.systemResource > 0 ? 0x118000000 : 0x10000000
        let heap = signals.largestFailedHeap > 0xcd500000 ? 0 : (signals.largestFailedHeap + 0x1fffff) / 0x200000 * 0x200000
        let observation = NativePreparationRecipe.MemoryObservation(
            largestFailedHeap: min(heap, 0xcd400000), heapOpened: signals.heapOpened,
            aliasMappedHighWater: min(signals.aliasMappedHighWater, aliasLimit))
        var observed = recipe
        observed.memoryObservation = observation
        return (try? observed.validate()) == nil ? nil : observation
    }

    /// The profile keys an observation changes, as "key old -> new" lines.
    static func changes(from old: Data, to new: Data) -> [String] {
        func values(_ data: Data) -> [String: String] {
            var rows: [String: String] = [:]
            for line in String(decoding: data, as: UTF8.self).split(separator: "\n") where !line.hasPrefix("#") {
                let parts = line.split(separator: "\t", maxSplits: 1, omittingEmptySubsequences: false)
                if parts.count == 2 { rows[String(parts[0])] = String(parts[1]) }
            }
            return rows
        }
        let a = values(old), b = values(new)
        return Set(a.keys).union(b.keys).sorted().compactMap { key in
            a[key] == b[key] ? nil : "\(key) \(a[key] ?? "unset") -> \(b[key] ?? "unset")"
        }
    }

    /// Textures counted before the uploads are learned. A write to a page a texture shares with
    /// other data counts too (as the engine's own dirty-page poll), so one or two are not enough.
    /// Measured on the Mac (docs/generic-profiles.md).
    static let textureWriteThreshold: UInt64 = 8

    /// The graphics settings these signals ask for, or nil when they ask for none.
    static func graphics(_ signals: LogSignals) -> NativePreparationRecipe.GraphicsObservation? {
        let textures = signals.textureWritesMissed >= textureWriteThreshold
        guard signals.renderConditions || textures else { return nil }
        return .init(renderConditions: signals.renderConditions, textureUploads: textures ? true : nil)
    }

    static func make(binding: Binding, observation: NativePreparationRecipe.MemoryObservation,
                     graphics: NativePreparationRecipe.GraphicsObservation?, at date: Date, launches: Int = 0) -> NativeBootFeedback? {
        var observed = binding.recipe
        observed.memoryObservation = observation
        observed.graphicsObservation = graphics
        guard let starter = try? binding.recipe.runtime(profiles: []), let learned = try? observed.runtime(profiles: []) else { return nil }
        var feedback = NativeBootFeedback(package: binding.package, titleID: binding.titleID, version: binding.version,
                                          starterProfileSHA256: binding.starterProfileSHA256, rewriter: binding.rewriter,
                                          observation: observation, graphics: graphics, observedAt: date,
                                          learnedProfileSHA256: learned != starter ? NativePreparationRecipe.hash(learned) : nil,
                                          changes: changes(from: starter, to: learned))
        feedback.learnedLaunches = launches
        return feedback
    }

    /// Derives feedback from one starter boot's signals.
    static func learn(_ signals: LogSignals, binding: Binding, at date: Date = Date()) -> NativeBootFeedback? {
        guard let observation = observation(signals, recipe: binding.recipe) else { return nil }
        let graphics = graphics(signals)
        var feedback = make(binding: binding, observation: observation, graphics: graphics, at: date)
        if graphics?.textureUploads == true { feedback?.textureWrites = signals.textureWritesMissed }
        return feedback
    }

    /// Whether a graphics setting is still off that a later signal could turn on.
    var graphicsOpen: Bool { graphics?.renderConditions != true || graphics?.textureUploads != true }

    /// The first learned launch may add the graphics settings it shows that are still off, in one
    /// step (collect reads only that launch); memory stays fixed and nothing is turned off again.
    func adding(_ signals: LogSignals, binding: Binding) -> NativeBootFeedback? {
        guard matches(binding), let seen = Self.graphics(signals) else { return nil }
        let render = graphics?.renderConditions == true || seen.renderConditions
        let textures = graphics?.textureUploads == true || seen.textureUploads == true
        let merged = NativePreparationRecipe.GraphicsObservation(renderConditions: render, textureUploads: textures ? true : nil)
        guard merged != graphics else { return nil }
        var feedback = Self.make(binding: binding, observation: observation, graphics: merged, at: observedAt, launches: learnedLaunches)
        feedback?.textureWrites = graphics?.textureUploads == true ? textureWrites : (textures ? signals.textureWritesMissed : nil)
        return feedback
    }

    /// The regenerated learned profile, checked against its recorded digest.
    func learnedProfile(binding: Binding) -> Data? {
        guard matches(binding), let digest = learnedProfileSHA256 else { return nil }
        var observed = binding.recipe
        observed.memoryObservation = observation
        observed.graphicsObservation = graphics
        guard let data = try? observed.runtime(profiles: []), NativePreparationRecipe.hash(data) == digest else { return nil }
        return data
    }

    // MARK: Store

    static func load(package: String) -> NativeBootFeedback? {
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .iso8601
        guard let data = try? Data(contentsOf: storeURL(package: package)), data.count < 64 * 1024,
              let value = try? decoder.decode(Self.self, from: data), value.schema == 1, value.package == package else { return nil }
        return value
    }
    func save() throws {
        let url = Self.storeURL(package: package)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys, .prettyPrinted]; encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(self).write(to: url, options: .atomic)
    }
    /// Forget what was learned for a package. The next launch uses its starter profile again and
    /// is observed afresh; the launch that taught the old settings is not read again.
    static func reset(package: String) {
        let feedback = load(package: package)
        try? FileManager.default.removeItem(at: storeURL(package: package))
        guard let feedback else { return }
        let runtime = runtimeURL(titleID: feedback.titleID)
        try? FileManager.default.removeItem(at: runtime.appendingPathComponent(profileName))
        if launchRecord(titleID: feedback.titleID)?.package == package { try? FileManager.default.removeItem(at: runtime.appendingPathComponent(launchName)) }
    }

    static func launchRecord(titleID: String) -> LaunchRecord? {
        guard let data = try? Data(contentsOf: runtimeURL(titleID: titleID).appendingPathComponent(launchName)), data.count < 4096 else { return nil }
        return try? JSONDecoder().decode(LaunchRecord.self, from: data)
    }

    // MARK: Collection and launch

    /// Reads the previous starter launch of this package, if its log is still the latest one,
    /// and stores what it showed. Returns the package's feedback, new or existing.
    @discardableResult
    static func collect(packageRoot: URL) -> NativeBootFeedback? {
        guard let binding = binding(packageRoot: packageRoot) else { return nil }
        let existing = load(package: binding.package).flatMap { $0.matches(binding) ? $0 : nil }
        let runtime = runtimeURL(titleID: binding.titleID)
        guard let record = launchRecord(titleID: binding.titleID), record.package == binding.package else { return existing }
        // Memory settings are fixed once derived; only the first learned launch may still add the
        // graphics settings that are off. Without a change, the latest starter launch is read again:
        // its log may have grown since, or a later launch may show what it did not.
        if let existing, let learned = existing.learnedProfileSHA256 {
            guard existing.graphicsOpen, record.kind == .learned, record.profileSHA256 == learned, existing.learnedLaunches <= 1 else { return existing }
        } else {
            guard record.kind == .starter, record.profileSHA256 == binding.starterProfileSHA256,
                  existing.map({ record.time >= $0.observedAt.timeIntervalSince1970 }) ?? true else { return existing }
        }
        guard let session = try? Data(contentsOf: runtime.appendingPathComponent("session.json")), session.count < 4096,
              let state = try? JSONSerialization.jsonObject(with: session) as? [String: Any],
              state["package"] as? String == binding.package,
              let time = state["time"] as? Double, time >= record.time,
              let signals = signals(log: runtime.appendingPathComponent("host.log")) else { return existing }
        let feedback: NativeBootFeedback?
        if let existing, existing.learnedProfileSHA256 != nil { feedback = existing.adding(signals, binding: binding) }
        else { feedback = learn(signals, binding: binding, at: Date(timeIntervalSince1970: record.time)) }
        guard let feedback else { return existing }
        try? feedback.save()
        return feedback
    }

    /// Called after the package verified and before the host starts. Records which profile this
    /// launch uses and returns the learned profile's path when one applies.
    static func prepareLaunch(packageRoot: URL) -> URL? {
        guard let binding = binding(packageRoot: packageRoot) else {
            if let titleID = try? manifestTitleID(packageRoot) {
                writeLaunch(LaunchRecord(package: packageRoot.lastPathComponent, kind: .other, profileSHA256: "", time: Date().timeIntervalSince1970), titleID: titleID)
            }
            return nil
        }
        let runtime = runtimeURL(titleID: binding.titleID)
        try? FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
        if var feedback = collect(packageRoot: packageRoot), let learned = feedback.learnedProfile(binding: binding) {
            let url = runtime.appendingPathComponent(profileName)
            if (try? learned.write(to: url, options: .atomic)) != nil {
                feedback.learnedLaunches += 1
                try? feedback.save()
                writeLaunch(LaunchRecord(package: binding.package, kind: .learned, profileSHA256: feedback.learnedProfileSHA256!, time: Date().timeIntervalSince1970), titleID: binding.titleID)
                return url
            }
        }
        try? FileManager.default.removeItem(at: runtime.appendingPathComponent(profileName))
        writeLaunch(LaunchRecord(package: binding.package, kind: .starter, profileSHA256: binding.starterProfileSHA256, time: Date().timeIntervalSince1970), titleID: binding.titleID)
        return nil
    }

    private static func manifestTitleID(_ root: URL) throws -> String? {
        let data = try Data(contentsOf: root.appendingPathComponent("manifest.json"))
        let title = (try JSONSerialization.jsonObject(with: data) as? [String: Any])?["titleID"] as? String
        return title.flatMap { NativePreparationRecipe.hex($0, count: 16) ? $0 : nil }
    }
    private static func writeLaunch(_ record: LaunchRecord, titleID: String) {
        let runtime = runtimeURL(titleID: titleID)
        try? FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(record) { try? data.write(to: runtime.appendingPathComponent(launchName), options: .atomic) }
    }

    // MARK: Report

    /// Plain-language lines for the game report.
    var summary: [String] {
        func gib(_ bytes: UInt64) -> String { String(format: "%.2f GiB", Double(bytes) / 1_073_741_824) }
        var lines = ["Starter launch of \(ISO8601DateFormatter().string(from: observedAt)): " +
                     (observation.largestFailedHeap > 0 ? "largest failed heap request \(gib(observation.largestFailedHeap)), " : "no failed heap request, ") +
                     (observation.heapOpened ? "heap opened, " : "no heap opened, ") +
                     "alias region mapped up to \(gib(observation.aliasMappedHighWater))"]
        if graphics?.renderConditions == true { lines.append("The game uses conditional rendering; Satchel evaluates it as the console does.") }
        if graphics?.textureUploads == true {
            lines.append("The game wrote \(textureWrites.map(String.init) ?? "several") textures from the CPU that the GPU copy did not show; Satchel now uploads them from memory and again when it changes.")
        }
        if learnedProfileSHA256 == nil {
            lines.append("No generic rule applies; launches keep the starter profile.")
        } else {
            lines.append("Learned settings: " + changes.joined(separator: "; "))
            lines.append(learnedLaunches == 0 ? "They apply on the next launch." : "Applied on \(learnedLaunches) launch\(learnedLaunches == 1 ? "" : "es").")
        }
        return lines
    }
}

extension NativeGameReport {
    /// Shader failures without their shader keys, counted by kind.
    var shaderFailureKinds: [String] {
        var counts: [String: Int] = [:]
        for failure in shaderFailures {
            let kind = failure.replacingOccurrences(of: #"[0-9a-fA-F]{16}"#, with: "<key>", options: .regularExpression)
            counts[kind, default: 0] += 1
        }
        return counts.keys.sorted().map { "\(counts[$0]!) x \($0)" }
    }

    /// The text "Copy diagnostics" puts on the clipboard: fixed fields, names of missing
    /// commands and their counts, error codes and revisions. No keys, guest log text, shader
    /// keys, game code, game data or save data.
    func diagnosticText(packageRoot: URL?, feedback: NativeBootFeedback?, appVersion: String, device: String) -> String {
        var settings = "not recorded", rewriter = "not recorded", version = "unknown"
        if let root = packageRoot,
           let data = try? Data(contentsOf: root.appendingPathComponent("manifest.json")), data.count < 32 * 1024 * 1024,
           let identity = try? JSONDecoder().decode(PreparedIdentity.self, from: data) {
            version = identity.version
            rewriter = identity.rewriter ?? "not recorded"
            if identity.format != 2 { settings = "reviewed package (format \(identity.format))" }
            else if let reviewed = identity.recipe?.reviewedVersion { settings = "reviewed settings for version \(reviewed)" }
            else if identity.recipe?.memoryObservation != nil { settings = "starter profile with a prepared memory observation" }
            else if feedback?.learnedProfileSHA256 != nil { settings = "starter profile with learned memory settings" }
            else { settings = "starter profile (generated from the game's metadata)" }
        }
        var lines = ["Satchel game diagnostics", "App: " + appVersion, "Device: " + device,
                     "Title: \(titleID.isEmpty ? "unknown" : titleID) version \(version)", "Package: " + package,
                     "Settings: " + settings, "Rewriter revision: " + rewriter,
                     "Engine pin: " + NativePreparationRecipe.enginePin + ", shader chain " + NativePreparationRecipe.shaderChain,
                     "Stage: \(stage), result: \(result)", "Message: " + String(message.prefix(300)),
                     "First frame: \(firstFrame), gameplay: \(gameplay)"]
        if let crash { lines.append("Crash: " + crash) }
        lines.append("Missing services (\(missingServices.count))" + (missingServices.isEmpty ? ": none" : ":"))
        lines += missingServices.map { "  " + $0 }
        lines.append("Unimplemented SVCs (\(missingSVCs.count))" + (missingSVCs.isEmpty ? ": none" : ": " + missingSVCs.joined(separator: ", ")))
        let kinds = shaderFailureKinds
        lines.append("Shader failures (\(shaderFailures.count))" + (kinds.isEmpty ? ": none" : ":"))
        lines += kinds.map { "  " + $0 }
        if let feedback { lines += feedback.summary.map { "Memory: " + $0 } } else { lines.append("Memory: no starter boot observation") }
        return lines.joined(separator: "\n") + "\n"
    }
}
