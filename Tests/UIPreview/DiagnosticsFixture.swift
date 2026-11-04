import Foundation

/// Files only: the production report reader, sheet and Prepare job are unchanged.
@MainActor enum DiagnosticsFixture {
    static func seed() {
        guard let mode = UserDefaults.standard.string(forKey: "SatchelPreviewDiagnostics") else { return }
        do {
            let package = "Synthetic-Diagnostics"
            let titleID = "010000000000D1A8"
            let fm = FileManager.default
            let documents = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
            let runtime = documents.appendingPathComponent("NativeRuntime/" + titleID)
            if fm.fileExists(atPath: runtime.path) { try fm.removeItem(at: runtime) }
            try fm.createDirectory(at: runtime, withIntermediateDirectories: true)
            let report = NativeGameReport(package: package, titleID: titleID, stage: "prepare",
                                          result: "failed", message: "Synthetic diagnostics fixture")
            try report.save()
            let session: [String: Any] = ["package": package, "time": Date().timeIntervalSince1970,
                                         "stage": "synthetic-runtime", "result": "returned"]
            try JSONSerialization.data(withJSONObject: session).write(to: runtime.appendingPathComponent("session.json"))
            let rows: [[String: Any]] = [
                ["service": "acc:u0", "interface": "IProfile", "command": 10, "method": "GetImageSize",
                 "firstSeenFrame": 37, "count": 4, "payload": "RAW_PAYLOAD_SENTINEL"],
                ["service": "time:u", "interface": "IClock", "command": 42,
                 "firstSeenFrame": 81, "count": 2],
                ["service": "bad", "interface": "RAW_PAYLOAD_SENTINEL invalid field", "command": 99,
                 "firstSeenFrame": 1, "count": 1]
            ]
            let valid = try JSONSerialization.data(withJSONObject: rows)
            let bytes: Data
            switch mode {
            case "rows": bytes = valid
            case "empty": bytes = Data()
            case "empty-array": bytes = Data("[]".utf8)
            case "malformed": bytes = Data(valid.dropLast())
            case "oversized":
                // Valid JSON beyond the reader's 512 KiB ceiling. A missing size
                // guard would expose the otherwise valid service rows.
                bytes = valid + Data(repeating: 0x20, count: 512 * 1024 + 1 - valid.count)
            default: preconditionFailure("Unknown synthetic diagnostics mode")
            }
            try bytes.write(to: runtime.appendingPathComponent("missing-services.json"))
            let outcome = PrepareJob.Outcome(kind: .failed, package: package, title: "Synthetic diagnostics",
                message: "Synthetic diagnostics fixture", baseID: "", updateID: "", finishedAt: Date())
            UserDefaults.standard.set(try JSONEncoder().encode(outcome), forKey: "PrepareJobOutcome")
        } catch { preconditionFailure("Synthetic diagnostics setup failed: \(error)") }
    }
}
