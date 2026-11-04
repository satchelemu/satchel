import Darwin
import Foundation

@main struct SignerTests {
    static func main() {
        do { try run() }
        catch let error as PrepareFailure { print("FAIL: \(error.message)"); exit(1) }
        catch { print("FAIL signing check: \(error.localizedDescription)"); exit(1) }
    }
    static func run() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let input = URL(fileURLWithPath: CommandLine.arguments[2])
        let token = PrepareCancellation()
        let p12 = try Data(contentsOf: root.appendingPathComponent("fixture.p12"))
        let identity = try SatchelSigningIdentity(pkcs12: p12, passphrase: "fixture-only", team: "PROBETEST1")
        func check(_ condition: Bool, _ label: String) throws {
            guard condition else { throw PrepareFailure(message: label) }
            print("PASS: \(label)")
        }
        func reject(_ label: String, _ body: () throws -> Void) throws {
            do { try body() } catch { print("PASS rejection: \(label)"); return }
            throw PrepareFailure(message: "Unexpected acceptance: \(label)")
        }
        try reject("wrong passphrase") { _ = try SatchelSigningIdentity(pkcs12: p12, passphrase: "incorrect", team: "PROBETEST1") }
        try reject("wrong team") { _ = try SatchelSigningIdentity(pkcs12: p12, passphrase: "fixture-only", team: "OTHERTEAM1") }
        try reject("malformed PKCS12") { _ = try SatchelSigningIdentity(pkcs12: Data([0]), passphrase: "", team: "PROBETEST1") }
        try check(identity.team == "PROBETEST1" && identity.expiry > Date() && identity.certificateName == "Satchel Disposable Fixture", "identity metadata")
        try check(SatchelSigningIdentity.team(fromKeychainGroup: "ABCDE12345.com.dell.satchel") == "ABCDE12345"
                  && SatchelSigningIdentity.team(fromKeychainGroup: "com.dell.satchel") == nil
                  && SatchelSigningIdentity.team(fromKeychainGroup: "short.com.dell.satchel") == nil
                  && SatchelSigningIdentity.team(fromKeychainGroup: nil) == nil, "team from keychain group")
        let target = root.appendingPathComponent("game.dylib")
        try PhonePreparedGameSigner.freshCopy(input, to: target, token: token)
        let before = try PrepareIO.digest(target, token: token)
        let original = try FileHandle(forReadingFrom: target)
        defer { try? original.close() }
        var oldStat = stat(); try check(fstat(original.fileDescriptor, &oldStat) == 0, "original inode held open")
        let state = try PhonePreparedGameSigner(identity: identity).sign(target, token: token)
        var newStat = stat(); try check(stat(target.path, &newStat) == 0 && newStat.st_ino != oldStat.st_ino, "published file uses a fresh inode")
        try check(state == .signed, "signer returns signed only after verification")
        try PrepareSignatureVerifier.verify(target, identity: identity, token: token)
        print("PASS: signed pipeline container verifies in process")
        try check(try PrepareIO.digest(input, token: token) == before, "pipeline source unchanged")
        try check(try FileManager.default.contentsOfDirectory(atPath: root.path).allSatisfy { !$0.hasPrefix(".sign-") }, "signing scratch removed")

        // Retain a shared writable mapping while copying. Mutating it afterwards must
        // change only the mapped inode, never the plain-written output.
        let mapped = root.appendingPathComponent("mapped-control"), fresh = root.appendingPathComponent("fresh-control")
        try Data(repeating: 17, count: 16384).write(to: mapped)
        let fd = open(mapped.path, O_RDWR)
        guard fd >= 0, let memory = mmap(nil, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), memory != MAP_FAILED else {
            throw PrepareFailure(message: "Mapping fixture failed.")
        }
        defer { munmap(memory, 16384); close(fd) }
        try PhonePreparedGameSigner.freshCopy(mapped, to: fresh, token: token)
        memory.storeBytes(of: UInt8(99), as: UInt8.self); msync(memory, 16384, MS_SYNC)
        try check(try Data(contentsOf: fresh) == Data(repeating: 17, count: 16384), "fresh file remains independent of shared writable mapping")
        try reject("fresh copy refuses an existing file") { try PhonePreparedGameSigner.freshCopy(mapped, to: fresh, token: token) }

        let held = root.appendingPathComponent("signed-mapped-control.dylib")
        let successor = root.appendingPathComponent("signed-successor.dylib")
        try PhonePreparedGameSigner.freshCopy(target, to: held, token: token)
        let heldFD = open(held.path, O_RDWR)
        guard heldFD >= 0, let heldMemory = mmap(nil, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, heldFD, 0), heldMemory != MAP_FAILED else {
            throw PrepareFailure(message: "Signed mapping fixture failed.")
        }
        defer { munmap(heldMemory, 16384); close(heldFD) }
        try PhonePreparedGameSigner(identity: identity).publishVerifiedSuccessor(held, to: successor, token: token)
        var heldStat = stat(), successorStat = stat()
        try check(fstat(heldFD, &heldStat) == 0 && stat(successor.path, &successorStat) == 0 && heldStat.st_ino != successorStat.st_ino,
                  "production publication replaces the mapped inode with a fresh inode")
        heldMemory.storeBytes(of: UInt8(0), as: UInt8.self); msync(heldMemory, 16384, MS_SYNC)
        try PrepareSignatureVerifier.verify(successor, identity: identity, token: token)
        print("PASS: published signature survives subsequent mutation of the mapped file")

        let pristine = try Data(contentsOf: target)
        func u32(_ bytes: Data, _ at: Int, big: Bool = false) -> Int {
            (0..<4).reduce(0) { $0 | Int(bytes[at + $1]) << (8 * (big ? 3 - $1 : $1)) }
        }
        var at = 32, sig = 0
        for _ in 0..<u32(pristine, 16) {
            if u32(pristine, at) == 0x1d { sig = u32(pristine, at + 8) }
            at += u32(pristine, at + 4)
        }
        var locations: [Int: Int] = [:]
        for i in 0..<u32(pristine, sig + 8, big: true) {
            locations[u32(pristine, sig + 12 + i * 8, big: true)] = sig + u32(pristine, sig + 16 + i * 8, big: true)
        }
        let corrupt = root.appendingPathComponent("corrupt.dylib")
        for (label, position) in [("code page", 0x4000), ("requirements hash", locations[2]! + 8),
                                  ("CMS", locations[0x10000]! + 24), ("CodeDirectory", locations[0]! + 39),
                                  ("load command bounds", 36), ("superblob bounds", sig + 4)] {
            var bytes = pristine; bytes[position] ^= 0xff
            try bytes.write(to: corrupt)
            try reject(label) { try PrepareSignatureVerifier.verify(corrupt, identity: identity, token: token) }
        }
        for length in [0, 4, 31, 100, sig + 8, pristine.count - 1] {
            try pristine.prefix(length).write(to: corrupt)
            try reject("truncated signature \(length)") { try PrepareSignatureVerifier.verify(corrupt, identity: identity, token: token) }
        }
        let other = try SatchelSigningIdentity(pkcs12: Data(contentsOf: root.appendingPathComponent("other.p12")), passphrase: "fixture-only", team: "PROBETEST1")
        try reject("different signing certificate") { try PrepareSignatureVerifier.verify(target, identity: other, token: token) }
        let expired = try SatchelSigningIdentity(pkcs12: Data(contentsOf: root.appendingPathComponent("expired.p12")), passphrase: "fixture-only", team: "PROBETEST1")
        try check(expired.expiry < Date(), "expired identity remains inspectable")
        let signedDigest = try PrepareIO.digest(target, token: token)
        try reject("expired signing identity") { _ = try PhonePreparedGameSigner(identity: expired).sign(target, token: token) }
        try check(try PrepareIO.digest(target, token: token) == signedDigest, "failed signing preserves input")
        token.cancel()
        try reject("cancelled signing") { _ = try PhonePreparedGameSigner(identity: identity).sign(target, token: token) }
        try check(try PrepareIO.digest(target, token: PrepareCancellation()) == signedDigest, "cancelled signing preserves input")
        print("PASS: signer acceptance suite complete")
    }
}
