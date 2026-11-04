import Foundation

@main
struct OnboardingTests {
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        var checks = 0
        func check(_ condition: Bool, _ name: String) {
            precondition(condition, name); checks += 1
        }
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        let soon = now.addingTimeInterval(2 * 86400)
        check(SignatureExpiry.warnings(app: nil, games: [], now: now).isEmpty, "unknown dates are not invented")
        check(SignatureExpiry.warnings(app: now.addingTimeInterval(3 * 86400 + 1), games: [], now: now).isEmpty, "outside threshold")
        check(SignatureExpiry.warnings(app: now.addingTimeInterval(3 * 86400), games: [], now: now).count == 1, "three day boundary")
        let warnings = SignatureExpiry.warnings(app: soon, games: [("older", "Synthetic game", now), ("future", "Future game", now.addingTimeInterval(10 * 86400))], now: now)
        check(warnings.map(\.id) == ["older", "app"], "game with earlier certificate comes first")
        check(warnings[0].message(now: now).contains("expired on"), "expired boundary")
        check(warnings[1].message(now: now).contains("SideStore, AltStore or Xcode"), "app recovery")
        check(warnings[0].message(now: now).contains("prepare this game again"), "game recovery")
        check(SignatureExpiry.warnings(app: nil, games: [("game", "Game", soon)], now: now).count == 1, "game date without app profile")
        let plist = try PropertyListSerialization.data(fromPropertyList: ["ExpirationDate": soon], format: .xml, options: 0)
        check(SignatureExpiry.profileExpiry(plist) == soon, "profile expiry")
        check(SignatureExpiry.profileExpiry(Data("not a plist".utf8)) == nil, "malformed profile")
        let wrongType = try PropertyListSerialization.data(fromPropertyList: ["ExpirationDate": "tomorrow"], format: .binary, options: 0)
        check(SignatureExpiry.profileExpiry(wrongType) == nil, "wrong date type")
        let profile = try Data(contentsOf: root.appendingPathComponent("profile.cms"))
        check(SatchelSignatureMetadata.profileContent(profile).flatMap(SignatureExpiry.profileExpiry) == now, "real CMS envelope, synthetic profile")
        let signedCMS = try Data(contentsOf: root.appendingPathComponent("signature.cms"))
        let certExpiry = SatchelSignatureMetadata.signerExpiry(signedCMS)
        check(certExpiry != nil && certExpiry! > Date(), "actual synthetic signer certificate")
        check(SatchelSignatureMetadata.signerExpiry(Data([0, 1, 2])) == nil, "malformed CMS")
        check(SatchelSignatureMetadata.profileContent(profile + Data([0])) == nil, "trailing CMS bytes")
        check(SatchelSignatureMetadata.signerExpiry(try Data(contentsOf: root.appendingPathComponent("no-cert.cms"))) == nil, "missing signer certificate")
        check(SatchelSignatureMetadata.signerExpiry(try Data(contentsOf: root.appendingPathComponent("extra-cert.cms"))) == certExpiry, "unrelated certificate ignored")

        func write32(_ bytes: inout Data, _ at: Int, _ value: UInt32, big: Bool = false) {
            for i in 0..<4 { bytes[at + i] = UInt8(truncatingIfNeeded: value >> (8 * (big ? 3 - i : i))) }
        }
        var file = Data(repeating: 0, count: 48 + 28)
        write32(&file, 0, 0xfeedfacf); write32(&file, 4, 0x0100000c); write32(&file, 12, 6)
        write32(&file, 16, 1); write32(&file, 20, 16)
        write32(&file, 32, 0x1d); write32(&file, 36, 16); write32(&file, 40, 48)
        write32(&file, 44, UInt32(28 + signedCMS.count))
        write32(&file, 48, 0xfade0cc0, big: true); write32(&file, 52, UInt32(28 + signedCMS.count), big: true)
        write32(&file, 56, 1, big: true); write32(&file, 60, 0x10000, big: true); write32(&file, 64, 20, big: true)
        write32(&file, 68, 0xfade0b01, big: true); write32(&file, 72, UInt32(8 + signedCMS.count), big: true)
        file.append(signedCMS)
        let game = root.appendingPathComponent("synthetic.dylib")
        try file.write(to: game)
        check(try SignatureExpiry.gameCMS(game) == signedCMS, "CMS from bounded Mach-O metadata")
        for (label, offset, value) in [("invalid magic", 0, UInt32(0)), ("huge commands", 20, UInt32.max), ("bad command size", 36, 0), ("signature outside file", 40, UInt32.max), ("truncated signature", 44, UInt32.max)] {
            var broken = file; write32(&broken, offset, value); try broken.write(to: game)
            check((try? SignatureExpiry.gameCMS(game)) == nil, label)
        }
        for count in [0, 7, 31, 47, 60, file.count - 1] {
            try file.prefix(count).write(to: game)
            check((try? SignatureExpiry.gameCMS(game)) == nil, "truncated file \(count)")
        }
        var badSlot = file; write32(&badSlot, 64, 4, big: true); try badSlot.write(to: game)
        check((try? SignatureExpiry.gameCMS(game)) == nil, "overlapping slot table")
        let synthetic = Data(("header_key = " + String(repeating: "00", count: 32) + "\nkey_area_key_application_00 = " + String(repeating: "11", count: 16)).utf8)
        check(try SwitchKeys.parse(synthetic).count == 2, "synthetic keys accepted without saving")
        check((try? SwitchKeys.parse(Data("not keys".utf8))) == nil, "invalid key file")
        check((try? SwitchKeys.parse(synthetic + Data("\nheader_key = 00".utf8))) == nil, "duplicate key rejected")
        check((try? SwitchKeys.parse(Data(repeating: 0, count: SwitchKeys.maximumFileSize + 1))) == nil, "oversize keys rejected")
        let full = NSError(domain: NSPOSIXErrorDomain, code: Int(ENOSPC))
        check(FirstRunHelp.message(full) == FirstRunHelp.space, "disk full")
        check(FirstRunHelp.message(NSError(domain: NSCocoaErrorDomain, code: NSFileWriteOutOfSpaceError)) == FirstRunHelp.space, "Cocoa disk full")
        check(FirstRunHelp.message(NSError(domain: NSCocoaErrorDomain, code: 1, userInfo: [NSUnderlyingErrorKey: full])) == FirstRunHelp.space, "wrapped disk full")
        check(FirstRunHelp.noKeys.contains("Settings > Console keys"), "missing key recovery")
        check(FirstRunHelp.newerKeys.contains("updated console"), "key version recovery")
        check(FirstRunHelp.unsupported.contains("XCI, NSP, XCZ or NSZ"), "unsupported file recovery")
        print("PASS: \(checks) synthetic onboarding and signature expiry checks. No Keychain writes.")
    }
}
