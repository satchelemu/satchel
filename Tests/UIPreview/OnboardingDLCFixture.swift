import Foundation

// Synthetic metadata and worker results only. The production views and jobs own
// navigation, refusal, progress, cancellation and recovery text.
enum OnboardingDLCFixture {
    static func seed() {
        guard let mode = UserDefaults.standard.string(forKey: "SatchelPreviewDLC") else { return }
        do {
            let fm = FileManager.default
            let documents = NavigationFixFixture.documents
            let art = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("GameArt")
            let base = try JSONDecoder().decode(GameInfo.self, from: Data(contentsOf: art.appendingPathComponent(NavigationFixFixture.baseID + ".json")))
            let items = try JSONDecoder().decode([HomebrewItem].self, from: Data(contentsOf: NavigationFixFixture.index))
            for item in items {
                try Data("Synthetic local import".utf8).write(to: documents.appendingPathComponent("Homebrew/" + item.id + "." + item.fileExtension))
            }
            if mode == "unprepared" {
                try fm.removeItem(at: documents.appendingPathComponent("PreparedGames"))
            }
            if mode == "newer" {
                let url = art.appendingPathComponent(NavigationFixFixture.dlcIDs[0] + ".json")
                var info = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [String: Any]
                var rows = info["addOns"] as! [[String: Any]]
                rows[0]["version"] = 2
                info["addOns"] = rows
                try JSONSerialization.data(withJSONObject: info).write(to: url)
            }
            UserDefaults.standard.set(base.applicationID.titleIDString, forKey: "SatchelPreviewFixTitle")
        } catch { preconditionFailure("Synthetic onboarding and DLC setup failed: \(error)") }
    }

    static func prepareError() throws {
        switch UserDefaults.standard.string(forKey: "SatchelPreviewPrepareError") {
        case "missing": throw SwitchExecutableError.missingKey("synthetic_key_name")
        case "invalid": throw SwitchExecutableError.invalidKeys
        case "space": throw NSError(domain: NSCocoaErrorDomain, code: NSFileWriteOutOfSpaceError)
        case "wrapped-space":
            throw NSError(domain: "SyntheticFileProvider", code: 1, userInfo: [
                NSUnderlyingErrorKey: NSError(domain: NSPOSIXErrorDomain, code: Int(ENOSPC))])
        default: break
        }
    }
}

// Replaces only the data transaction in the preview target. No NCA processing,
// package verification, signing or real DLC publication is claimed by these tests.
enum AddOnUpdate {
    enum Phase: String { case checking, cloning, extracting, binding, verifying, publishing }

    static func run(root: URL, sources: [URL], keys: [String: Data], token: PrepareCancellation,
                    progress: (Phase) throws -> Void) throws -> Int {
        guard UserDefaults.standard.bool(forKey: "SatchelPreviewDLCRun"),
              NavigationFixFixture.packages.contains(root.lastPathComponent), !sources.isEmpty,
              sources.allSatisfy({ $0.deletingLastPathComponent().lastPathComponent == "Homebrew" }) else {
            throw PrepareFailure(message: "Only explicitly enabled synthetic DLC work can run in this fixture.")
        }
        let manifest = try Data(contentsOf: root.appendingPathComponent("manifest.json"))
        try progress(.extracting)
        let deadline = Date().addingTimeInterval(240)
        while !token.cancelled {
            guard Date() < deadline else { throw PrepareFailure(message: "Synthetic DLC work timed out.") }
            Thread.sleep(forTimeInterval: 0.05)
        }
        // Keep the cancelling state observable before acknowledgement.
        Thread.sleep(forTimeInterval: 5)
        guard try Data(contentsOf: root.appendingPathComponent("manifest.json")) == manifest else {
            throw PrepareFailure(message: "Synthetic package changed during cancellation.")
        }
        try token.check()
        return 0
    }
}
