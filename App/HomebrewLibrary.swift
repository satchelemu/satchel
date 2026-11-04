import CryptoKit
import Foundation

struct HomebrewRun: Codable {
    let name: String
    let sha256: String
    let outcome: String
    let output: String
    let instructions: UInt64
    let pc: String
    let exitCode: UInt64?
    let seconds: Double
    let timestamp: Date
}

enum ImportOutcome: Equatable {
    case added(id: String, duplicate: Bool)
    case failed(name: String, message: String)
    case cancelled(name: String)
}

private struct LibraryFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

private final class Cancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    func cancel() { lock.lock(); value = true; lock.unlock() }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
}

final class HomebrewLibrary: ObservableObject {
    @Published private(set) var items: [HomebrewItem] = []
    @Published private(set) var importing = false
    @Published private(set) var reclaimingOriginals = false
    @Published private(set) var importProgress: Double?
    /// iCloud download progress (0 to 1) while Files fetches the selection, before the copy
    /// starts; nil when the provider reports none.
    @Published private(set) var downloadFraction: Double?
    @Published private(set) var importStartedAt: Date?
    @Published private(set) var runningID: String?
    @Published private(set) var lastRun: HomebrewRun?
    @Published var error: String?
    @Published var notice: String?
    @Published private(set) var sampleURL: URL?
    @Published private(set) var runReportURL: URL?
    @Published private(set) var importName: String?
    @Published private(set) var importBytes: UInt64?
    @Published private(set) var queuedImports = 0
    /// How the last import ended. Kept until dismissed (the home card or the import screen)
    /// or until the next import starts.
    @Published private(set) var lastOutcome: ImportOutcome?
    private var importQueue: [SwitchDumpSelection] = []
    private let queue = DispatchQueue(label: "com.dell.satchel.homebrew", qos: .userInitiated)
    private var cancellation: Cancellation?
    private var importCancellation: Cancellation?
    private var importCoordinator: NSFileCoordinator?
#if os(iOS)
    private var downloadWatch: ImportDownloadWatch?
#endif
    private let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    private var directory: URL { documents.appendingPathComponent("Homebrew", isDirectory: true) }
    private var indexURL: URL { directory.appendingPathComponent("library.json") }

    init() {
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            // Failure to reclaim a partial copy must not hide an existing library.
            try? SwitchPackageImport.removeInterruptedCopies(in: directory)
            if FileManager.default.fileExists(atPath: indexURL.path) {
                let data = try Data(contentsOf: indexURL)
                items = try JSONDecoder().decode([HomebrewItem].self, from: data).filter {
                    $0.id.count == 64 && $0.id.allSatisfy { "0123456789abcdef".contains($0) }
                }
            }
            // A visible copy lets users put this original sample in iCloud Drive
            // using Files or Share > Save to Files; no iCloud credentials needed.
            if UserDefaults.standard.bool(forKey: "SatchelDeveloperControls"),
               let bundled = Bundle.main.url(forResource: "hello-horizon", withExtension: "nro") {
                let folder = documents.appendingPathComponent("Samples", isDirectory: true)
                try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                let target = folder.appendingPathComponent("hello-horizon.nro")
                if !FileManager.default.fileExists(atPath: target.path) { try FileManager.default.copyItem(at: bundled, to: target) }
                sampleURL = target
            }
        } catch { self.error = "Could not open the local library: \(error.localizedDescription)" }
    }

    /// Imports one after another; the first starts now.
    func importFiles(_ urls: [URL]) {
        importQueue += SwitchDumpSelection.groups(urls)
        queuedImports = max(0, importQueue.count - (importing ? 0 : 1))
        if !importing, !reclaimingOriginals, !importQueue.isEmpty { importFile(importQueue.removeFirst()) }
    }

    func clearOutcome() { lastOutcome = nil }

    func importFile(_ url: URL) { importFiles([url]) }

    private func importFile(_ selection: SwitchDumpSelection) {
        guard !selection.urls.isEmpty else { return }
        guard !importing else { return }
        guard !reclaimingOriginals else { importQueue.append(selection); queuedImports = importQueue.count; return }
        let displayName = String(selection.name.prefix(100))
        importing = true; importProgress = nil; downloadFraction = nil; importStartedAt = Date(); error = nil
        notice = "Waiting for Files to download and open the selected file…"
        importName = displayName; importBytes = nil; lastOutcome = nil; queuedImports = importQueue.count
        let token = Cancellation(); importCancellation = token
        let coordinator = NSFileCoordinator(); importCoordinator = coordinator
        let folder = directory
#if os(iOS)
        downloadWatch = ImportDownloadWatch(urls: selection.urls) { [weak self] fraction in
            guard let self, self.importing, self.importProgress == nil else { return }
            self.downloadFraction = fraction
        }
#endif
        // Reports to the system's progress display when the import runs as a continued task.
        let work = { (report: @escaping (Double?, String) -> Void) -> Bool in
            report(nil, "Waiting for Files")
            let scoped = selection.urls.filter { $0.startAccessingSecurityScopedResource() }
            defer { scoped.forEach { $0.stopAccessingSecurityScopedResource() } }
            let result: Result<HomebrewItem, Error> = Result {
                try selection.coordinated(using: coordinator) { parts in
                    if token.cancelled { throw CancellationError() }
                    let stream = try SwitchPackageStream(urls: parts)
                    let prefix = try stream.read(at: 0, count: min(stream.size, UInt64(SATCHEL_HOMEBREW_PROBE_SIZE)), cancelled: { token.cancelled })
                    let format = prefix.withUnsafeBytes { buffer in
                        satchel_homebrew_format(buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count)
                    }
                    if format == SATCHEL_FORMAT_XCI || format == SATCHEL_FORMAT_PFS0 {
                        var lastProgress = 0.0
                        let imported = try SwitchPackageImport.copy(parts: parts, into: folder, cancelled: { token.cancelled }) { copied, total in
                            let now = ProcessInfo.processInfo.systemUptime
                            if now - lastProgress >= 0.1 || copied == total {
                                lastProgress = now
                                report(Double(copied) / Double(total), copied == total ? "Verifying" : "Copying")
                                DispatchQueue.main.async {
                                    self.stopDownloadWatch()
                                    self.importProgress = Double(copied) / Double(total)
                                    self.importBytes = total
                                    self.notice = copied == total ? "Verifying the local package…" : "Copying to this iPhone…"
                                }
                            }
                        }
                        return HomebrewItem(id: imported.id, name: displayName, bytes: Int(imported.bytes),
                                            textBytes: 0, roBytes: 0, dataBytes: 0, bssBytes: 0, importedAt: Date(), package: imported.summary)
                    }
                    guard format == SATCHEL_FORMAT_NRO else {
                        throw LibraryFailure(message: FirstRunHelp.unsupported)
                    }
                    guard parts.count == 1, SwitchDumpSelection.part(parts[0]) == nil else {
                        throw LibraryFailure(message: "Split imports must contain an XCI or NSP game dump.")
                    }
                    let file = try FileHandle(forReadingFrom: parts[0])
                    defer { try? file.close() }
                    let size = try file.seekToEnd()
                    guard size <= UInt64(SATCHEL_NRO_FILE_LIMIT) else { throw LibraryFailure(message: "This prototype accepts NRO files up to 64 MiB.") }
                    try file.seek(toOffset: 0)
                    let data = try file.read(upToCount: Int(SATCHEL_NRO_FILE_LIMIT) + 1) ?? Data()
                    guard data.count <= Int(SATCHEL_NRO_FILE_LIMIT) else { throw LibraryFailure(message: "The selected file exceeds 64 MiB.") }
                    var info = SatchelNROInfo()
                    var message = [CChar](repeating: 0, count: 256)
                    let valid = data.withUnsafeBytes { buffer in
                        satchel_nro_inspect(buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count, &info, &message, message.count)
                    }
                    guard valid else { throw LibraryFailure(message: String(cString: message)) }
                    let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
                    let destination = folder.appendingPathComponent(digest + ".nro")
                    // Reimport also repairs a modified cache copy. The selected
                    // original remains untouched; library identity stays deduplicated.
                    if token.cancelled { throw CancellationError() }
                    try data.write(to: destination, options: .atomic)
                    return HomebrewItem(id: digest, name: displayName, bytes: data.count,
                                        textBytes: info.text_size, roBytes: info.ro_size, dataBytes: info.data_size,
                                        bssBytes: info.bss_size, importedAt: Date())
                }
            }
            let succeeded: Bool
            if case .success = result { succeeded = true } else { succeeded = false }
            DispatchQueue.main.async {
                self.stopDownloadWatch()
                self.importing = false; self.importProgress = nil; self.importCancellation = nil; self.importCoordinator = nil
                self.importName = nil; self.downloadFraction = nil; self.importStartedAt = nil
                defer {
                    if !self.importQueue.isEmpty { self.importFile(self.importQueue.removeFirst()) }
                    else { self.queuedImports = 0 }
                }
                switch result {
                case .success(let item):
                    let duplicate = self.items.first { $0.id == item.id }
                    // Reimport restores the original without adding another shelf entry.
                    let updated = duplicate == nil ? self.items + [item] : self.items.map { $0.id == item.id ? item : $0 }
                    do {
                        try JSONEncoder().encode(updated).write(to: self.indexURL, options: .atomic)
                        self.items = updated
                        // The result shows on the home card and the import screen; nothing opens by itself.
                        self.lastOutcome = .added(id: item.id, duplicate: duplicate != nil)
                        if item.package != nil {
                            self.notice = duplicate == nil ? "Added to your library." : "Already in your library."
                        } else {
                            self.notice = duplicate == nil ? "Imported a local copy. Ready to try." : "Already in your library."
                        }
                    } catch {
                        let message = "The file was copied, but the library index could not be saved: \(error.localizedDescription)"
                        self.lastOutcome = .failed(name: displayName, message: message)
                        self.notice = nil
                    }
                case .failure(let error):
                    if token.cancelled || error is CancellationError {
                        self.notice = "Import cancelled. Your original file is unchanged."
                        self.lastOutcome = .cancelled(name: displayName)
                        self.importQueue.removeAll()
                    } else {
                        self.lastOutcome = .failed(name: displayName, message: FirstRunHelp.message(error))
                        self.notice = nil
                    }
                }
            }
            return succeeded
        }
        let stop = { token.cancel(); coordinator.cancel() }
#if os(iOS)
        ImportLifetime.run(name: displayName, queue: queue, stop: stop) { reporter in
            work { fraction, stage in reporter.update(fraction: fraction, stage: stage) }
        }
#else
        queue.async { _ = work { _, _ in } }
#endif
    }

    private func stopDownloadWatch() {
#if os(iOS)
        downloadWatch?.stop(); downloadWatch = nil
#endif
    }

    func cancelImport() {
        importCancellation?.cancel()
        importCoordinator?.cancel()
    }

    func localFile(_ item: HomebrewItem) -> URL { directory.appendingPathComponent(item.id + "." + item.fileExtension) }

    /// Developer hook so the Mac can tidy the library with no taps:
    /// devicectl device process launch ... com.dell.satchel -- -SatchelDeveloperControls YES
    ///   -SatchelRemoveLibraryItems "<id>,<id>" -SatchelRemoveDocuments "<name>,<name>"
    /// Items go through remove(_:); documents are top-level names only, never the library,
    /// prepared games, saves or shader packs. The result goes to Documents/remove-result.txt.
    func removeRequested() {
        let defaults = UserDefaults.standard
        var lines: [String] = []
        for id in (defaults.string(forKey: "SatchelRemoveLibraryItems") ?? "").split(separator: ",").map(String.init) {
            if let item = items.first(where: { $0.id == id }) { remove(item); lines.append("removed item \(item.name)") }
            else { lines.append("no item \(id)") }
        }
        let protected: Set<String> = ["Homebrew", "NativeRuntime", "PreparedGames", "ShaderPacks"]
        for name in (defaults.string(forKey: "SatchelRemoveDocuments") ?? "").split(separator: ",").map(String.init) {
            guard !name.isEmpty, !name.contains("/"), name != ".", name != "..", !protected.contains(name) else {
                lines.append("refused \(name)"); continue
            }
            do { try FileManager.default.removeItem(at: documents.appendingPathComponent(name)); lines.append("removed document \(name)") }
            catch { lines.append("failed \(name): \((error as NSError).code)") }
        }
        guard !lines.isEmpty else { return }
        try? (lines.joined(separator: "\n") + "\n").write(to: documents.appendingPathComponent("remove-result.txt"), atomically: true, encoding: .utf8)
    }

    /// Deletes the local copy and its library entry. The original in Files is untouched.
    func remove(_ item: HomebrewItem) {
        guard runningID == nil, !importing, !reclaimingOriginals else { return }
        let updated = items.filter { $0.id != item.id }
        do {
            try JSONEncoder().encode(updated).write(to: indexURL, options: .atomic)
            items = updated
            try? FileManager.default.removeItem(at: localFile(item))
            notice = "Removed \(item.name) from this iPhone."
        } catch { self.error = "Could not update the library: \(error.localizedDescription)" }
    }

    func originalRemoved(_ item: HomebrewItem) -> Bool {
        item.preparedSource != nil && !FileManager.default.fileExists(atPath: localFile(item).path)
    }

    func hasOriginal(_ item: HomebrewItem) -> Bool {
        FileManager.default.fileExists(atPath: localFile(item).path)
    }

    var libraryBytes: Int64 {
        items.reduce(0) { $0 + Int64((try? localFile($1).resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0) }
    }

    /// Called only after the full launch verifier passes. Persist the title association first,
    /// so interruption during deletion still leaves a playable library entry on next launch.
    @MainActor func reclaimPreparedOriginals(_ sources: [String: PreparedImportSource], plan: PreparedImportCleanup.Plan,
                                  completion: @escaping @MainActor (String?) -> Void) {
        guard !importing else {
            completion("Originals kept because an import is running. Prepare again to remove them."); return
        }
        reclaimingOriginals = true
        @MainActor func finish(_ note: String?) {
            self.reclaimingOriginals = false
            self.objectWillChange.send()
            completion(note)
            if !self.importQueue.isEmpty { self.importFile(self.importQueue.removeFirst()) }
        }
        Task {
            do {
                var updated = items
                for i in updated.indices {
                    if let source = sources[updated[i].id] { updated[i].preparedSource = source }
                }
                try JSONEncoder().encode(updated).write(to: indexURL, options: .atomic)
                items = updated
                let failures = await Task.detached(priority: .utility) { plan.remove() }.value
                finish(failures.isEmpty ? nil : "Ready to play. Some originals could not be removed: " + failures.joined(separator: "; "))
            } catch { finish("Originals kept: " + error.localizedDescription) }
        }
    }

    func run(_ item: HomebrewItem) {
        guard item.package == nil else {
            error = "This game package is stored locally, but XCI/NSP execution is not implemented in this app yet."
            return
        }
        guard runningID == nil else { return }
        runningID = item.id; lastRun = nil; runReportURL = nil; error = nil
        let token = Cancellation(); cancellation = token
        let path = directory.appendingPathComponent(item.id + ".nro")
        queue.async {
            let start = ProcessInfo.processInfo.systemUptime
            var report: HomebrewRun
            do {
                let attributes = try FileManager.default.attributesOfItem(atPath: path.path)
                guard let size = attributes[.size] as? NSNumber, size.uint64Value <= UInt64(SATCHEL_NRO_FILE_LIMIT) else { throw LibraryFailure(message: "The local file exceeds the size limit.") }
                let file = try FileHandle(forReadingFrom: path)
                defer { try? file.close() }
                let data = try file.read(upToCount: Int(SATCHEL_NRO_FILE_LIMIT) + 1) ?? Data()
                guard data.count <= Int(SATCHEL_NRO_FILE_LIMIT) else { throw LibraryFailure(message: "The local file exceeds the size limit.") }
                let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
                guard digest == item.id else { throw LibraryFailure(message: "The local NRO changed since import. Import it again to create a new library entry.") }
                var message = [CChar](repeating: 0, count: 256)
                let vm = data.withUnsafeBytes { buffer in
                    satchel_create_nro(buffer.bindMemory(to: UInt8.self).baseAddress, buffer.count, &message, message.count)
                }
                guard let vm else { throw LibraryFailure(message: String(cString: message)) }
                defer { satchel_destroy(vm) }
                var outcome = "Stopped at the prototype execution limit (2 seconds or 20 million instructions)."
                var exit: UInt64?
                while !token.cancelled && ProcessInfo.processInfo.systemUptime - start < 2 && satchel_instructions(vm) < 20_000_000 {
                    let result = satchel_frame(vm, 0, 0, 50_000)
                    if result == SATCHEL_WAIT { Thread.sleep(forTimeInterval: 0.001) }
                    if result == SATCHEL_EXITED {
                        exit = satchel_exit_code(vm)
                        outcome = exit == 0 ? "Completed" : "Exited with code \(exit!)"
                        break
                    }
                    if result == SATCHEL_FAULT { outcome = String(cString: satchel_error(vm)); break }
                }
                if token.cancelled { outcome = "Stopped by you" }
                report = HomebrewRun(name: item.name, sha256: item.id, outcome: outcome,
                                     output: String(cString: satchel_output(vm)), instructions: satchel_instructions(vm),
                                     pc: String(format: "0x%llx", satchel_pc(vm)), exitCode: exit,
                                     seconds: ProcessInfo.processInfo.systemUptime - start, timestamp: Date())
            } catch {
                report = HomebrewRun(name: item.name, sha256: item.id, outcome: error.localizedDescription,
                                     output: "", instructions: 0, pc: "n/a", exitCode: nil,
                                     seconds: ProcessInfo.processInfo.systemUptime - start, timestamp: Date())
            }
            DispatchQueue.main.async {
                self.runningID = nil; self.cancellation = nil; self.lastRun = report
                do {
                    let url = self.documents.appendingPathComponent("Homebrew-run.json")
                    let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
                    try encoder.encode(report).write(to: url, options: .atomic)
                    self.runReportURL = url
                } catch { self.error = "The run finished, but its report could not be saved: \(error.localizedDescription)" }
            }
        }
    }

    func stop() { cancellation?.cancel() }
}
