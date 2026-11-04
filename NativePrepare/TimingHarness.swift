import Foundation

@main struct PrepareTimingHarness {
    static func main() {
        do {
            let args = CommandLine.arguments.dropFirst().map { URL(fileURLWithPath: $0) }
            guard args.count == 7 else { throw PrepareFailure(message: "Usage: timing BASE UPDATE KEYS SHADER_PACK OUTPUT FIXTURE TRACE") }
            let keys = try SwitchKeys.parse(Data(contentsOf: args[2]))
            let identity = try SatchelSigningIdentity(pkcs12: Data(contentsOf: args[5]), passphrase: "fixture-only", team: "PROBETEST1")
            let token = PrepareCancellation()
            let start = ProcessInfo.processInfo.systemUptime
            var previous: PrepareStep?, last = start, events: [PrepareProgressSnapshot] = []
            _ = try PreparePipeline.run(base: args[0], update: args[1], keys: keys, shaderPack: args[3],
                destination: args[4], signer: PhonePreparedGameSigner(identity: identity), token: token, details: { value in
                    events.append(value)
                    if value.step != previous {
                        let now = ProcessInfo.processInfo.systemUptime
                        if let previous { print(String(format: "TIME %.3f %@", now - last, previous.rawValue)); fflush(stdout) }
                        previous = value.step; last = now
                    }
                }) { _, fraction in
                    precondition(fraction != nil && fraction!.isFinite && (0...1).contains(fraction!))
                }
            let now = ProcessInfo.processInfo.systemUptime
            if let previous { print(String(format: "TIME %.3f %@", now - last, previous.rawValue)) }
            try JSONEncoder().encode(events).write(to: args[6], options: .atomic)
            guard events.last?.overallFraction == 1,
                  zip(events, events.dropFirst()).allSatisfy({ $0.overallFraction <= $1.overallFraction }) else {
                throw PrepareFailure(message: "Progress is incomplete or went backwards.")
            }
            print(String(format: "TOTAL %.3f", now - start))
            print("PASS: signed preparation and monotonic progress")
        } catch { print("FAIL timing: \(error.localizedDescription)"); exit(1) }
    }
}
