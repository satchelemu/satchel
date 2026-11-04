import SwiftUI

/// Set to your ten-character Apple team ID before building this historical probe.
let probeTeam = "YOUR_TEAM_"
import UniformTypeIdentifiers

final class ProbeModel: ObservableObject {
    @Published var results: [String: Any] = [:]
    @Published var persistenceError = ""
    @Published var busy = false
    @Published var identityReady = false
    @Published var importMessage = "Import your Apple Development identity to unlock S1c and S1d."
    let variants = ["A", "B", "C", "D"]
    let payloads = ["S1c", "S1d"]
    let variantDescriptions = ["A": "No entitlements, 16 KiB", "B": "No entitlements, 4 KiB",
                               "C": "Empty XML + DER, 16 KiB", "D": "XML only, 4 KiB, failure control"]
    private let queue = DispatchQueue(label: "signprobe.tests", qos: .userInitiated)
    private var signer: SPSigner?
    private var importedPKCS12: Data?
    let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    private var resultsURL: URL { documents.appendingPathComponent("results.json") }
    private var support: URL { FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0] }

    init() {
        if let data = try? Data(contentsOf: resultsURL), let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] { results = value }
        if var matrix = results["signingMatrix"] as? [String: Any], matrix["status"] as? String == "running" {
            matrix["status"] = "interrupted"
            results["signingMatrix"] = matrix
            for id in matrix["tests"] as? [String] ?? [] {
                if var result = results[id] as? [String: Any],
                   ["pending", "running", "signed, awaiting dlopen"].contains(result["status"] as? String ?? "") {
                    result["status"] = "interrupted"
                    result["error"] = "Previous process ended before this attempt completed. No pass or failure was observed."
                    results[id] = result
                }
            }
        }
        results["launch"] = SPProcessInfo()
        if results["S1c"] == nil { results["S1c"] = ["status": "awaiting user", "reason": "PKCS#12 import and phone-entered passphrase required"] }
        if results["S1d"] == nil { results["S1d"] = ["status": "awaiting user", "reason": "PKCS#12 import and phone-entered passphrase required"] }
        var error: NSError?
        if !SPDumpInstalledSignature(documents.path, &error) { results["signatureDump"] = ["status": "fail", "error": error?.description ?? "Unknown error"] }
        else { results["signatureDump"] = ["status": "pass", "source": "Copied from this installed app's executable and embedded profile"] }
        save()
        let args = ProcessInfo.processInfo.arguments
        if let index = args.firstIndex(of: "--probe-test"), args.count > index + 1 {
            let test = args[index + 1]
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.run(test, exitAfter: true) }
        }
    }

    private func save() {
        do {
            let data = try JSONSerialization.data(withJSONObject: results, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: resultsURL, options: .atomic)
            persistenceError = ""
        } catch {
            persistenceError = "Could not save results.json: \(error.localizedDescription)"
            print("RESULTS_WRITE_ERROR \(error)")
        }
    }

    private func record(_ id: String, _ value: [String: Any]) {
        var value = value
        value["timestamp"] = ISO8601DateFormatter().string(from: Date())
        results[id] = value
        save()
    }

    func selectIdentity(_ url: URL) {
        do {
            let access = url.startAccessingSecurityScopedResource()
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            importedPKCS12 = try Data(contentsOf: url)
            importMessage = "File read into memory. Enter its passphrase on this phone."
        } catch { importMessage = error.localizedDescription }
    }

    func unlock(_ passphrase: String) {
        guard var data = importedPKCS12 else { importMessage = "Choose a .p12 file first."; return }
        defer { data.resetBytes(in: 0..<data.count); importedPKCS12 = nil }
        do {
            signer = try SPSigner(pkcs12: data, passphrase: passphrase, team: probeTeam)
            identityReady = true
            importMessage = "Identity is held in memory. Tap Run signing matrix."
        } catch {
            importMessage = error.localizedDescription
            record("identityImport", ["status": "fail", "error": error.localizedDescription])
        }
    }

    func forget() {
        signer = nil
        if let count = importedPKCS12?.count { importedPKCS12?.resetBytes(in: 0..<count) }
        importedPKCS12 = nil
        identityReady = false
        importMessage = "Identity released. Import again to sign another probe."
    }

    func runSigningMatrix(payloads selectedPayloads: [String]? = nil, exitAfter: Bool = false) {
        guard !busy, let activeSigner = signer else { return }
        busy = true
        let runID = UUID().uuidString
        let selected = selectedPayloads ?? payloads
        let ids = selected.flatMap { payload in variants.map { "\(payload)-\($0)" } }
        record("signingMatrix", ["status": "running", "runID": runID, "tests": ids, "process": SPProcessInfo()])
        for id in ids { record(id, ["status": "pending", "runID": runID]) }
        queue.async {
            var passed = 0
            for payload in selected {
                for variant in self.variants {
                    let id = "\(payload)-\(variant)"
                    let path = self.support.appendingPathComponent("\(id)-\(UUID().uuidString).dylib")
                    let exportDirectory = self.documents.appendingPathComponent("signing-matrix/\(runID)")
                    let exported = exportDirectory.appendingPathComponent("\(id)-signed.dylib")
                    var result: [String: Any] = ["status": "running", "runID": runID, "variant": variant,
                        "settings": self.variantDescriptions[variant]!, "path": path.path, "payload": payload,
                        "pageSize": ["A", "C"].contains(variant) ? 14 : 12,
                        "entitlements": variant == "C" ? "XML + DER" : variant == "D" ? "XML only" : "none",
                        "generatedOnPhone": payload == "S1d", "signedOnPhone": false]
                    DispatchQueue.main.sync { self.record(id, result) }
                    do {
                        try FileManager.default.createDirectory(at: self.support, withIntermediateDirectories: true)
                        try FileManager.default.createDirectory(at: exportDirectory, withIntermediateDirectories: true)
                        if payload == "S1c" {
                            guard let unsigned = Bundle.main.url(forResource: "unsigned-probe", withExtension: "bin") else {
                                throw NSError(domain: "Probe", code: 2, userInfo: [NSLocalizedDescriptionKey: "unsigned-probe.bin resource missing"])
                            }
                            try FileManager.default.copyItem(at: unsigned, to: path)
                        } else {
                            var error: NSError?
                            guard SPGenerateDylib(path.path, true, &error) else { throw error ?? NSError(domain: "Probe", code: 3) }
                        }
                        try activeSigner.signFile(path.path, identifier: "com.dell.satchel.signprobe.\(payload)", variant: variant)
                        result["signedOnPhone"] = true
                        // Export synthetic code before loading so a crash cannot lose signing evidence.
                        try Data(contentsOf: path).write(to: exported, options: .atomic)
                        result["exportedPath"] = exported.path
                        // The signer writes through a shared writable mapping, which leaves that
                        // file's pages tainted for code signing: dlopen of it was killed with
                        // CODESIGNING "Invalid Page" although every page hash matched. Load a fresh
                        // file written with plain writes instead.
                        let loadPath = self.support.appendingPathComponent("\(id)-\(UUID().uuidString)-load.dylib")
                        try Data(contentsOf: path).write(to: loadPath, options: .atomic)
                        result["loadPath"] = loadPath.path
                        result["status"] = "signed, awaiting dlopen"
                        DispatchQueue.main.sync { self.record(id, result) }
                        result.merge(SPLoad(loadPath.path) as! [String: Any]) { _, new in new }
                    } catch {
                        result["status"] = "fail"
                        result["error"] = String(describing: error)
                    }
                    if result["status"] as? String == "pass" { passed += 1 }
                    // A returned loader error is just one result. Always continue the matrix.
                    DispatchQueue.main.sync { self.record(id, result) }
                }
            }
            let summary: [String: Any] = ["status": "complete", "runID": runID, "tests": ids,
                                          "passed": passed, "failed": ids.count - passed]
            DispatchQueue.main.async {
                self.record("signingMatrix", summary)
                self.busy = false
                if exitAfter { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { exit(0) } }
            }
        }
    }

    func run(_ id: String, exitAfter: Bool = false) {
        if id == "S1c" || id == "S1d" {
            runSigningMatrix(payloads: [id], exitAfter: exitAfter)
            return
        }
        guard !busy else { return }
        busy = true
        record(id, ["status": "running", "process": SPProcessInfo()])
        queue.async {
            let result: [String: Any]
            do {
                try FileManager.default.createDirectory(at: self.support, withIntermediateDirectories: true)
                guard let bundleProbe = Bundle.main.url(forResource: "probe", withExtension: "dylib") else { throw NSError(domain: "Probe", code: 1, userInfo: [NSLocalizedDescriptionKey: "probe.dylib resource missing"]) }
                switch id {
                case "S1a-copy":
                    let path = self.support.appendingPathComponent("mac-signed-\(UUID().uuidString).dylib")
                    try FileManager.default.copyItem(at: bundleProbe, to: path)
                    result = SPLoad(path.path) as! [String: Any]
                case "S1a-external": result = SPLoad(self.documents.appendingPathComponent("postinstall-probe.dylib").path) as! [String: Any]
                case "S1a-bundle": result = SPLoad(bundleProbe.path) as! [String: Any]
                case "S1b-shared": result = SPRemap(bundleProbe.path, false) as! [String: Any]
                case "S1b-copy": result = SPRemap(bundleProbe.path, true) as! [String: Any]
                case "S2":
                    result = SPMemory { progress in
                        let snapshot = progress as! [String: Any]
                        DispatchQueue.main.sync { self.record(id, snapshot) }
                    } as! [String: Any]
                default: result = ["status": "fail", "error": "Unknown test: \(id)"]
                }
            } catch { result = ["status": "fail", "error": String(describing: error)] }
            DispatchQueue.main.async {
                self.record(id, result)
                self.busy = false
                if let data = try? JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]), let text = String(data: data, encoding: .utf8) { print("PROBE_RESULT \(id) \(text)"); fflush(stdout) }
                if exitAfter { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { exit(0) } }
            }
        }
    }

    var text: String {
        guard let data = try? JSONSerialization.data(withJSONObject: results, options: [.prettyPrinted, .sortedKeys]) else { return "" }
        return String(decoding: data, as: UTF8.self)
    }
}

@main
struct SignLoadProbeApp: App {
    @StateObject private var model = ProbeModel()
    @State private var picker = false
    @State private var passphrase = ""
    var body: some Scene {
        WindowGroup {
            HStack(alignment: .top, spacing: 16) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Sign / Load Probe").font(.title2.bold())
                        Text("Team \(probeTeam)").font(.caption)
                        ForEach(["S1a-copy", "S1a-bundle", "S1a-external", "S1b-shared", "S1b-copy", "S2"], id: \.self) { id in
                            Button("Run \(id)") { model.run(id) }.disabled(model.busy)
                        }
                        Divider()
                        Button("Import .p12 from Files") { picker = true }.disabled(model.busy)
                        SecureField("Passphrase, phone only", text: $passphrase)
                            .textFieldStyle(.roundedBorder).textContentType(.password).privacySensitive()
                        Button("Unlock identity") { model.unlock(passphrase); passphrase = "" }.disabled(model.busy)
                        Text(model.importMessage).font(.caption)
                        Button("Run signing matrix") { model.runSigningMatrix() }
                            .disabled(!model.identityReady || model.busy)
                        Button("Forget identity") { model.forget(); passphrase = "" }.disabled(model.busy)
                    }.buttonStyle(.bordered)
                }.frame(width: 290)
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Signing matrix: 8 attempts").font(.headline)
                        if !model.persistenceError.isEmpty {
                            Text(model.persistenceError).foregroundStyle(.red).textSelection(.enabled)
                        }
                        if let summary = model.results["signingMatrix"] as? [String: Any] {
                            Text(summary["status"] as? String == "complete"
                                 ? "Complete: \(summary["passed"] as? Int ?? 0) passed, \(summary["failed"] as? Int ?? 0) failed"
                                 : model.busy ? "Running. Each result is saved before the next attempt."
                                 : "Previous run was interrupted. Import the identity and run the matrix again.")
                        }
                        ForEach(model.payloads, id: \.self) { payload in
                            Text(payload == "S1c" ? "S1c: compiled payload" : "S1d: generated Mach-O").font(.headline)
                            ForEach(model.variants, id: \.self) { variant in
                                let id = "\(payload)-\(variant)"
                                let result = model.results[id] as? [String: Any] ?? [:]
                                let status = result["status"] as? String ?? "not run"
                                VStack(alignment: .leading, spacing: 4) {
                                    Text("\(id): \(model.variantDescriptions[variant]!)").bold()
                                    Text(status == "pass" ? "PASS, returned \(result["value"] as? Int ?? 0)" : status.uppercased())
                                        .foregroundStyle(status == "pass" ? Color.green : status == "fail" ? Color.red : Color.primary)
                                    if let error = result["error"] as? String, !error.isEmpty {
                                        Text(error).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                                    }
                                }.frame(maxWidth: .infinity, alignment: .leading).padding(10)
                                    .background(Color.secondary.opacity(0.08)).cornerRadius(8)
                            }
                        }
                        DisclosureGroup("All results JSON") {
                            Text(model.text).font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
                }
            }.padding()
                .fileImporter(isPresented: $picker, allowedContentTypes: [.data]) { result in
                    switch result { case .success(let url): model.selectIdentity(url); case .failure(let error): model.importMessage = error.localizedDescription }
                }
        }
    }
}
