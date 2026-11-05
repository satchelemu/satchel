import Foundation

@main struct PrepareHarness {
    static func main() {
        do {
            let cancelTest = CommandLine.arguments.count == 7 && CommandLine.arguments[1] == "--cancel-romfs"
            let genericProfile = CommandLine.arguments.count == 7 && CommandLine.arguments[1] == "--generic-profile"
            guard CommandLine.arguments.count == 6 || cancelTest || genericProfile else { throw PrepareFailure(message: "Usage: prepare [--cancel-romfs | --generic-profile] BASE UPDATE KEYS SHADER_PACK OUTPUT") }
            let args = CommandLine.arguments.dropFirst(cancelTest || genericProfile ? 2 : 1).map { URL(fileURLWithPath: $0) }
            let observation = try ProcessInfo.processInfo.environment["PREPARE_MEMORY_OBSERVATION"].map {
                try JSONDecoder().decode(NativePreparationRecipe.MemoryObservation.self, from: Data(contentsOf: URL(fileURLWithPath: $0)))
            }
            var last = ""
            let token = PrepareCancellation()
            do {
                let state = try PreparePipeline.run(base: args[0], update: args[1].lastPathComponent == "-" ? nil : args[1], keys: SwitchKeys.parse(Data(contentsOf: args[2])),
                    shaderPack: args[3].lastPathComponent == "-" ? nil : args[3], destination: args[4], applicationID: ProcessInfo.processInfo.environment["PREPARE_TITLE"].flatMap { UInt64($0, radix: 16) }, signer: DevelopmentUnsignedSigner(), token: token, allowReviewedProfiles: !genericProfile, memoryObservation: observation) { stage, fraction in
                    if stage != last { print(stage); fflush(stdout); last = stage }
                    if cancelTest && stage == "Writing composed RomFS…" && (fraction ?? 0) > 0 { token.cancel() }
                }
                guard !cancelTest else { throw PrepareFailure(message: "Cancellation test unexpectedly published a package.") }
                print("PASS native prepare: \(state.rawValue)")
            } catch is CancellationError where cancelTest {
                let fm = FileManager.default
                guard !fm.fileExists(atPath: args[4].path),
                      try !fm.contentsOfDirectory(atPath: args[4].deletingLastPathComponent().path).contains(where: { $0.hasPrefix(".prepare-") }) else {
                    throw PrepareFailure(message: "Cancellation left a partial package.")
                }
                print("PASS cancellation during real RomFS copy: no destination or staging files remain")
            }
        } catch let error as PrepareFailure { print("FAIL: \(error.message)"); exit(1) }
        catch let error as SwitchExecutableError { print("FAIL content: \(error.localizedDescription)"); exit(1) }
        catch { print("FAIL prepare: \(type(of: error))"); exit(1) }
    }
}
