import CryptoKit
import Foundation

/// Mac-only content measurement. A missing output discards every image byte
/// after the same composition, integrity checks and SHA-256 as a real write.
@main struct ContentBenchmark {
    static func main() {
        do {
            let args = Array(CommandLine.arguments.dropFirst())
            guard args.count == 5, let title = UInt64(args[3], radix: 16) else {
                throw SwitchExecutableError.invalid
            }
            let base = URL(fileURLWithPath: args[0]), update = args[1] == "-" ? nil : URL(fileURLWithPath: args[1])
            let keys = try SwitchKeys.parse(Data(contentsOf: URL(fileURLWithPath: args[2])))
            let start = ProcessInfo.processInfo.systemUptime
            var previous: SwitchContentPipeline.Phase?
            var authenticationBytes: UInt64 = 0
            try SwitchContentPipeline.withContent(base: base, update: update, keys: keys, applicationID: title,
                progress: { phase, _, total, _ in
                    if phase == .authentication { authenticationBytes = total }
                    if phase != previous {
                        print(String(format: "PHASE %@ %.3f", String(describing: phase), ProcessInfo.processInfo.systemUptime - start))
                        fflush(stdout); previous = phase
                    }
                }) { content in
                let before = content.inputReads()
                let result = try args[4] == "-" ? SwitchRomFSImage.measure(content.romFS) :
                    SwitchRomFSImage.write(content.romFS, to: URL(fileURLWithPath: args[4]))
                let after = content.inputReads()
                let record: [String: Any] = ["seconds": ProcessInfo.processInfo.systemUptime - start,
                    "sourceBytes": after.bytes, "sourceReadCalls": after.calls,
                    "authenticationBytes": authenticationBytes,
                    "sourcePassEquivalent": Double(after.bytes) / Double(max(1, authenticationBytes)),
                    "imageSourceBytes": after.bytes - before.bytes, "imageSourceReadCalls": after.calls - before.calls,
                    "imageBytes": result.imageBytes, "imageSHA256": result.sha256.cpHex,
                    "discarded": args[4] == "-"]
                print(String(decoding: try JSONSerialization.data(withJSONObject: record, options: [.sortedKeys]), as: UTF8.self))
            }
        } catch let error as SwitchExecutableError {
            print("FAIL content benchmark: \(error.localizedDescription)"); exit(1)
        } catch { print("FAIL content benchmark: \(type(of: error))"); exit(1) }
    }
}
