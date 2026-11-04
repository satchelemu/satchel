import Foundation
@main struct ServiceReportTest {
    static func main() throws {
        var report = NativeGameReport(package: "synthetic", titleID: "0100000000000001", stage: "boot", result: "running", message: "")
        let unknown = "ServiceIPC service=set command=65537 status=unknown caller=main+0x1234"
        let stub = "ServiceIPC service=acc:u0 command=51 status=stub caller=sdk+0x42"
        let missing = "ServiceIPC service=time:u command=4294967295 status=missing caller=main+0x5678"
        report.ingest("[dm] \(unknown)\n[dm] \(stub)\n[dm] \(missing)\n[dm] \(unknown)\n[guest] PRIVATE BYTES\n[dm] UNIMPL nnExample (x0=PRIVATE)\n")
        precondition(report.missingServices.count == 4)
        precondition(report.missingServices.contains(unknown) && report.missingServices.contains(stub) && report.missingServices.contains(missing))
        let text = String(decoding: try JSONEncoder().encode(report), as: UTF8.self)
        precondition(!text.contains("PRIVATE"))
        precondition(report.firstFrame == "not verified" && report.gameplay == "not verified")
        // Exercise the actual file reader with a retained diagnostic and a rolling host tail.
        precondition(CommandLine.arguments.count == 2)
        let runtime = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent(UUID().uuidString)
        precondition(NativeGameReport.runtimeDiagnostics(at: runtime) == nil)
        try FileManager.default.createDirectory(at: runtime, withIntermediateDirectories: true)
        try Data((unknown + "\n").utf8).write(to: runtime.appendingPathComponent("services.log"))
        try Data(("UNIMPL oldTail\n" + String(repeating: "frame 1200\n", count: 30000)).utf8).write(to: runtime.appendingPathComponent("host.log"))
        report.ingest(NativeGameReport.runtimeDiagnostics(at: runtime)!)
        precondition(report.missingServices == [unknown])
        try Data().write(to: runtime.appendingPathComponent("services.log"))
        report.ingest(NativeGameReport.runtimeDiagnostics(at: runtime)!)
        precondition(report.missingServices.isEmpty)
        let summary = """
        [{"service":"acc:u0","interface":"IProfile","command":10,"method":"GetImageSize","firstSeenFrame":37,"count":4}]
        """
        let summaryURL = runtime.appendingPathComponent("missing-services.json")
        try Data(summary.utf8).write(to: summaryURL)
        report.ingest(NativeGameReport.runtimeDiagnostics(at: runtime)!)
        precondition(report.missingServices == ["MissingService service=acc:u0 interface=IProfile command=10 method=GetImageSize firstSeenFrame=37 count=4"])
        try Data(summary.replacingOccurrences(of: "IProfile", with: "PRIVATE BYTES").utf8).write(to: summaryURL)
        report.ingest(NativeGameReport.runtimeDiagnostics(at: runtime)!)
        precondition(report.missingServices.isEmpty)
        try Data("invalid".utf8).write(to: summaryURL)
        report.ingest(NativeGameReport.runtimeDiagnostics(at: runtime)!)
        precondition(report.missingServices.isEmpty)
        print("PASS: service, command, caller, stub, missing connection, deduplication, privacy and busy-log retention")
    }
}
