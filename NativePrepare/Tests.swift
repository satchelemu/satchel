import Foundation

@main struct PrepareTests {
    static func main() throws {
        if CommandLine.arguments.count == 4 && CommandLine.arguments[1] == "--container" {
            try PrepareMachO.write(modules: PreparePipeline.moduleNames,
                from: URL(fileURLWithPath: CommandLine.arguments[2]), to: URL(fileURLWithPath: CommandLine.arguments[3]), token: PrepareCancellation())
            print("PASS container generated")
            return
        }
        let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let token = PrepareCancellation()
        let layouts: [[String: String]] = [
            ["TEXT_END":"0x4000", "PATCH_OFF":"0x8000", "KEY_PAGE_OFF":"0x4000", "KEY_PAGE_SIZE":"0x4000"],
            ["TEXT_END":"0x4000", "PATCH_OFF":"-0x8000", "KEY_PAGE_OFF":"0xc000", "KEY_PAGE_SIZE":"0x4000", "FILE_IMAGE_LEN":"0x8000"],
            ["TEXT_END":"0x4000", "PATCH_OFF":"-0x8000", "KEY_PAGE_OFF":"0xc000", "KEY_PAGE_SIZE":"0x4000", "FILE_IMAGE_LEN":"0x8000", "BACK_LEN":"0x4000", "BACK_OFF":"0x10000"]
        ]
        for (i, name) in PreparePipeline.moduleNames.enumerated() {
            let size = i == 2 ? 0x10000 : 0x8100
            try Data(repeating: UInt8(i + 1), count: size).write(to: root.appendingPathComponent(name + ".patched.bin"))
            let text = layouts[i].sorted(by: { $0.key < $1.key }).map { "# \($0.key) \($0.value)\n" }.joined()
            try Data(text.utf8).write(to: root.appendingPathComponent(name + ".fixups.tsv"))
        }
        try PrepareMachO.write(modules: PreparePipeline.moduleNames, from: root, to: root.appendingPathComponent("synthetic.dylib"), token: token)
        func reject(_ label: String, _ body: () throws -> Void) throws {
            do { try body() } catch { print("PASS rejection: \(label)"); return }
            throw PrepareFailure(message: "Unexpected acceptance: \(label)")
        }
        var malformed = layouts[0]; malformed["PATCH_OFF"] = "0x7000"
        try reject("unaligned patch") { _ = try PrepareMachO.split(name: "main", source: root, layout: malformed, size: 0x8100) }
        malformed = layouts[2]; malformed["BACK_LEN"] = "0x20000"
        try reject("back area beyond file") { _ = try PrepareMachO.split(name: "main", source: root, layout: malformed, size: 0x10000) }
        try reject("invalid module name") { _ = try PrepareMachO.split(name: "../main", source: root, layout: layouts[0], size: 0x8100) }
        try reject("shader traversal") { _ = try PrepareIO.regular(root, "../main.patched.bin") }
        let link = root.appendingPathComponent("symlink")
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: root.appendingPathComponent("main.patched.bin"))
        try reject("shader symlink") { _ = try PrepareIO.regular(root, "symlink") }
        let record = try PrepareIO.record(root.appendingPathComponent("main.patched.bin"), token: token)
        let wrong = PrepareFileRecord(bytes: record.bytes, sha256: String(repeating: "0", count: 64))
        try reject("corrupt shader copy") { try PrepareIO.copy(root.appendingPathComponent("main.patched.bin"), to: root.appendingPathComponent("bad-copy"), expected: wrong, token: token) }
        let before = try Data(contentsOf: root.appendingPathComponent("main.patched.bin"))
        try reject("existing destination") {
            _ = try PreparePipeline.run(base: root, update: root, keys: [:], shaderPack: root,
                destination: root, signer: DevelopmentUnsignedSigner(), token: token) { _, _ in }
        }
        guard try Data(contentsOf: root.appendingPathComponent("main.patched.bin")) == before else {
            throw PrepareFailure(message: "Existing package was modified.")
        }
        do {
            _ = try PreparePipeline.run(base: root, update: nil, keys: [:], shaderPack: root,
                destination: root.appendingPathComponent("generic"), signer: DevelopmentUnsignedSigner(),
                token: token, allowReviewedProfiles: false) { _, _ in }
            throw PrepareFailure(message: "Generic preparation accepted a reviewed shader pack.")
        } catch let failure as PrepareFailure {
            guard failure.message == "Generic profile validation requires runtime shaders, without a reviewed shader pack." else { throw failure }
            print("PASS rejection: generic profile with reviewed shader pack")
        }
        do {
            _ = try PreparePipeline.run(base: root, update: nil, keys: [:],
                destination: root.appendingPathComponent("observed"), signer: DevelopmentUnsignedSigner(),
                token: token, memoryObservation: .init(largestFailedHeap: 0, heapOpened: false, aliasMappedHighWater: 0)) { _, _ in }
            throw PrepareFailure(message: "Reviewed preparation accepted memory observations.")
        } catch let failure as PrepareFailure {
            guard failure.message == "Memory observations require explicit generic profile validation." else { throw failure }
            print("PASS rejection: memory observation without generic profile mode")
        }
        token.cancel()
        try reject("cancelled signer") { _ = try DevelopmentUnsignedSigner().sign(root, token: token) }
        try reject("cancelled container") { try PrepareMachO.write(modules: PreparePipeline.moduleNames, from: root, to: root.appendingPathComponent("cancelled.dylib"), token: token) }
        print("PASS: native container synthetic generation and eleven rejection checks")
    }
}
