import Foundation
import SwiftUI
import UIKit

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
            case "rows", "learned": bytes = valid
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
            if mode == "learned" { try seedStarterPackage(package: package, titleID: titleID, runtime: runtime) }
            let outcome = PrepareJob.Outcome(kind: .failed, package: package, title: "Synthetic diagnostics",
                message: "Synthetic diagnostics fixture", baseID: "", updateID: "", finishedAt: Date())
            UserDefaults.standard.set(try JSONEncoder().encode(outcome), forKey: "PrepareJobOutcome")
        } catch { preconditionFailure("Synthetic diagnostics setup failed: \(error)") }
    }

    /// A synthetic starter profile package whose previous launch failed a heap request. The real
    /// NativeBootFeedback reader derives the learned settings from these files.
    static func seedStarterPackage(package: String, titleID: String, runtime: URL) throws {
        let fm = FileManager.default
        let recipe = NativePreparationRecipe(schema: 1, titleID: titleID, version: "1.0.0", contentVersion: 0, name: "Synthetic diagnostics",
            modules: ["main", "sdk"], buildIDs: ["main": String(repeating: "1", count: 32), "sdk": String(repeating: "2", count: 32)],
            sdkVersions: ["17_5_0"], systemResource: 0x2000000, usesHeap: true, largeBSS: 0, usedMemory: 0x5a00000, nros: [], reviewedVersion: nil)
        let profile = try recipe.profile(profiles: [])
        let runtimeProfile = try recipe.runtime(profiles: [])
        func object<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
        let manifest: [String: Any] = [
            "format": 2, "signingStatus": "signed", "contentVersion": 0, "recipe": try object(recipe), "preparedProfile": try object(profile),
            "titleID": titleID, "version": "1.0.0", "enginePin": profile.enginePin, "shaderChain": profile.shaderChain,
            "shaderInputs": profile.shaderInputs, "modules": profile.modules, "environment": profile.environment,
            "rewriter": NativePreparationRecipe.rewriterRevision,
            "files": ["profile.tsv": ["bytes": runtimeProfile.count, "sha256": NativePreparationRecipe.hash(runtimeProfile)]]]
        let root = NativeBootFeedback.packageURL(package: package)!
        if fm.fileExists(atPath: root.path) { try fm.removeItem(at: root) }
        try fm.createDirectory(at: root, withIntermediateDirectories: true)
        try JSONSerialization.data(withJSONObject: manifest).write(to: root.appendingPathComponent("manifest.json"))
        NativeBootFeedback.reset(package: package)
        let started = Date().timeIntervalSince1970 - 60
        let record = NativeBootFeedback.LaunchRecord(package: package, kind: .starter, profileSHA256: NativePreparationRecipe.hash(runtimeProfile), time: started)
        try JSONEncoder().encode(record).write(to: runtime.appendingPathComponent(NativeBootFeedback.launchName))
        try Data(("[dm] SetHeapSize OOM: req 0x84600000 > cap 0x8000000\n[guest] RAW_PAYLOAD_SENTINEL\n" +
                  "[dm] [RED] PhoneShader key=0123456789abcdef stage=1 prepare failed status=-4\n").utf8)
            .write(to: runtime.appendingPathComponent("host.log"))
    }
}

/// Reads the clipboard back into the sheet so the journey can check what was copied.
struct GameReportClipboardFixture: View {
    @State private var copied = ""
    var body: some View {
        if UserDefaults.standard.string(forKey: "SatchelPreviewDiagnostics") == "learned" {
            Button("Inspect synthetic clipboard") { copied = UIPasteboard.general.string ?? "Clipboard is empty" }
                .accessibilityIdentifier("game-report-inspect-clipboard")
            Text(copied).font(.system(size: 10)).accessibilityIdentifier("game-report-clipboard")
        }
    }
}
