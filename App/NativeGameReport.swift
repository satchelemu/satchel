import Foundation

/// Export only bounded diagnostic text. Shader captures and guest bytes stay private.
struct NativeGameReport: Codable {
    var schema = 1
    var package: String
    var titleID: String
    var stage: String
    var result: String
    var message: String
    var updatedAt = Date()
    var attemptStartedAt: Double? = Date().timeIntervalSince1970
    var missingServices: [String] = []
    var missingSVCs: [String] = []
    var crash: String?
    var shaderFailures: [String] = []
    var lastLogLines: [String] = []
    var firstFrame = "not verified"
    var gameplay = "not verified"

    static func url(package: String) -> URL {
        let safe = NativePreparationRecipe.hash(Data(package.utf8))
        return FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("GameReports", isDirectory: true).appendingPathComponent(safe + ".json")
    }
    static func record(package: String, titleID: String = "", stage: String, result: String, message: String) {
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .iso8601
        let previous = (try? Data(contentsOf: url(package: package))).flatMap { try? decoder.decode(Self.self, from: $0) }
        var report = Self(package: package, titleID: titleID.isEmpty ? previous?.titleID ?? "" : titleID,
                          stage: stage, result: result, message: String(message.prefix(2048)))
        if stage != "verify" && stage != "waiting" { report.attemptStartedAt = previous?.attemptStartedAt ?? Date().timeIntervalSince1970 }
        try? report.save()
    }
    func save() throws {
        let destination = Self.url(package: package)
        try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys, .prettyPrinted]; encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(self).write(to: destination, options: .atomic)
    }
    static func load(package: String, titleID: String, sessionActive: Bool = false) -> Self {
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .iso8601
        var report = (try? decoder.decode(Self.self, from: Data(contentsOf: url(package: package)))) ??
            Self(package: package, titleID: titleID, stage: "verify", result: "not started", message: "No launch has been recorded.")
        if !titleID.isEmpty { report.titleID = titleID }
        if report.titleID.isEmpty, !package.contains("/"), package != ".", package != ".." {
            let manifest = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("PreparedGames/" + package + "/manifest.json")
            if let size = try? manifest.resourceValues(forKeys: [.fileSizeKey]).fileSize, size < 32 * 1024 * 1024,
               let data = try? Data(contentsOf: manifest), let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let title = value["titleID"] as? String { report.titleID = title }
        }
        guard NativePreparationRecipe.hex(report.titleID, count: 16) else { return report }
        let runtime = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("NativeRuntime/" + report.titleID)
        // A different package of the same title must not inherit this session's log.
        guard let session = try? Data(contentsOf: runtime.appendingPathComponent("session.json")),
              let state = try? JSONSerialization.jsonObject(with: session) as? [String: Any],
              state["package"] as? String == package,
              let time = state["time"] as? Double,
              time >= (report.attemptStartedAt ?? report.updatedAt.timeIntervalSince1970) else { return report }
        if let stage = state["stage"] as? String { report.stage = stage }
        if let result = state["result"] as? String { report.result = result }
        if let diagnostics = runtimeDiagnostics(at: runtime) { report.ingest(diagnostics) }
        if report.result == "running" && !sessionActive {
            report.result = "interrupted"
            if report.shaderFailures.isEmpty {
                report.message = "The native session ended without a completion marker. The iOS termination reason is not available in this report."
            }
        }
        report.updatedAt = Date()
        return report
    }
    struct MissingCommand: Decodable {
        let service: String
        let interface: String
        let command: UInt32
        let method: String?
        let firstSeenFrame: UInt64
        let count: UInt64
        var diagnostic: String? {
            let label = #"^[a-zA-Z0-9_:.-]{1,80}$"#
            guard service.count <= 15, service.range(of: label, options: .regularExpression) != nil,
                  interface.range(of: label, options: .regularExpression) != nil,
                  method == nil || method!.range(of: label, options: .regularExpression) != nil,
                  count > 0 else { return nil }
            return "MissingService service=\(service) interface=\(interface) command=\(command) method=\(method ?? "unknown") firstSeenFrame=\(firstSeenFrame) count=\(count)"
        }
    }
    static func runtimeDiagnostics(at runtime: URL) -> String? {
        var diagnostics = "", readLog = false
        if let handle = try? FileHandle(forReadingFrom: runtime.appendingPathComponent("missing-services.json")) {
            defer { try? handle.close() }
            if let bytes = try? handle.read(upToCount: 512 * 1024 + 1), bytes.count <= 512 * 1024,
               let entries = try? JSONDecoder().decode([MissingCommand].self, from: bytes), entries.count <= 1024 {
                diagnostics = entries.compactMap(\.diagnostic).joined(separator: "\n") + "\n"; readLog = true
            }
        }
        // The once-per-command sink survives a busy host log. It is reset at launch.
        if let handle = try? FileHandle(forReadingFrom: runtime.appendingPathComponent("services.log")) {
            if let bytes = try? handle.read(upToCount: 256 * 1024) {
                diagnostics += String(decoding: bytes, as: UTF8.self) + "\n"; readLog = true
            }
            try? handle.close()
        }
        if let handle = try? FileHandle(forReadingFrom: runtime.appendingPathComponent("host.log")) {
            defer { try? handle.close() }
            let end = (try? handle.seekToEnd()) ?? 0
            try? handle.seek(toOffset: end > 256 * 1024 ? end - 256 * 1024 : 0)
            if let bytes = try? handle.read(upToCount: 256 * 1024) {
                diagnostics += String(decoding: bytes, as: UTF8.self); readLog = true
            }
        }
        return readLog ? diagnostics : nil
    }
    mutating func ingest(_ log: String) {
        // Extract fixed diagnostic fields, never arbitrary guest prints or raw byte dumps.
        func match(_ pattern: String, _ line: String) -> String? {
            guard let range = line.range(of: pattern, options: .regularExpression) else { return nil }
            return String(line[range].prefix(512))
        }
        var lines: [String] = [], services = Set<String>(), svcs = Set<String>(), shaders = Set<String>()
        for raw in log.split(separator: "\n") {
            let line = String(raw.prefix(2048))
            if let value = match(#"CRASH sig=[0-9]+(?: addr=[a-zA-Z0-9_+x]+| \(stderr held for 1 s, no dump\)) pc=[a-zA-Z0-9_+x]+ lr=[a-zA-Z0-9_+x]+"#, line) {
                crash = value; result = "failed"; message = "Native guest crashed. See the module offset in this report."; lines.append(value)
            } else if let value = match(#"SVC (0x)?[0-9a-fA-F]+"#, line), line.contains("placeholder") || line.contains("unimplemented") || line.contains("unsupported") {
                svcs.insert(value); lines.append(value + " unimplemented")
            } else if let value = match(#"^MissingService service=[a-zA-Z0-9_:.-]{1,15} interface=[a-zA-Z0-9_:.-]{1,80} command=[0-9]{1,10} method=[a-zA-Z0-9_:.-]{1,80} firstSeenFrame=[0-9]{1,20} count=[0-9]{1,20}$"#, line) {
                services.insert(value); lines.append(value)
            } else if let value = match(#"ServiceIPC service=[a-zA-Z0-9_:.-]{1,79} command=[0-9]{1,10} status=(?:unknown|missing|stub) caller=[a-zA-Z0-9_:.-]{1,63}\+0x[0-9a-fA-F]+"#, line) {
                services.insert(value); lines.append(value)
            } else if let value = match(#"UNIMPL [a-zA-Z0-9_$:]+"#, line) {
                services.insert(value); lines.append(value)
            } else if let value = match(#"(?:missing|unknown|unimplemented|unsupported) service[: =']+[a-zA-Z0-9_:.-]+"#, line.lowercased()) {
                services.insert(value); lines.append(value)
            } else if let value = match(#"shader [0-9a-fA-F]{16} is not in the shader map"#, line) {
                shaders.insert(value); lines.append(value)
            } else if let value = match(#"(?:failed to load shader [0-9a-fA-F]{16}_[a-z]+|shader [0-9a-fA-F]{16} \([a-z]+\) failed to load|shader [0-9a-fA-F]{16}_[a-z]+ reflection conversion failed)"#, line) {
                shaders.insert(value); lines.append(value)
            } else if let value = match(#"PhoneShader key=[0-9a-fA-F]{16} (?:stage=[0-5] (?:translated|prepare failed status=-?[0-9]+)|compile failed code=-?[0-9]+)"#, line) {
                lines.append(value)
                if value.contains("failed") {
                    let reasons = ["-2": "invalid shader record or identity", "-3": "unsupported shader stage",
                                   "-4": "native translation failed", "-5": "shader cache unavailable or full"]
                    let status = value.components(separatedBy: "status=").last ?? ""
                    shaders.insert(value + (reasons[status].map { ": " + $0 } ?? ""))
                }
            } else if line.contains("phone shader revision budget exceeded") {
                shaders.insert("Phone shader revision budget exceeded"); lines.append("Phone shader revision budget exceeded")
            } else if line.contains("ro::LoadModule: no rewritten NRO") {
                lines.append("ro::LoadModule: no rewritten NRO matches the registered image")
            } else if line.contains("program has no vertex shader") {
                shaders.insert("Program has no translated vertex shader"); lines.append("Program has no translated vertex shader")
            } else if line.contains("program has no compute shader") {
                shaders.insert("Program has no translated compute shader"); lines.append("Program has no translated compute shader")
            } else if line.contains("native session returned"), let value = match(#"native session returned -?[0-9]+"#, line) {
                lines.append(value); result = "returned"
            } else if line.contains("[bootprof]"), let value = match(#"(?:nnMain|load_modules|init_modules)[a-zA-Z0-9 =.:_+-]*"#, line) {
                lines.append(value)
            } else if line.contains("[nvn-mtl]"), line.localizedCaseInsensitiveContains("compile"), line.localizedCaseInsensitiveContains("fail") {
                shaders.insert("Metal shader compilation failed"); lines.append("Metal shader compilation failed")
            }
        }
        missingServices = services.sorted(); missingSVCs = svcs.sorted(); shaderFailures = shaders.sorted()
        lastLogLines = Array(lines.suffix(80))
        if !shaders.isEmpty, crash == nil { message = "Guest boot reached shaders. Rendering is blocked by missing or failed shader translation." }
    }
}
