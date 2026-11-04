import XCTest

/// Read-only navigation on the phone. Simulator-only journeys additionally exercise
/// synthetic sessions and container imports, never guest code or real user content.
final class NavigationJourneyTests: XCTestCase {
    private var app: XCUIApplication!

    override func setUpWithError() throws {
        continueAfterFailure = false
        // These skips need no app launch and remain visible even if home is broken.
        if name.contains("testPauseMenuWithoutGame") || name.contains("testInterpreterDemoNavigation") { return }
        app = XCUIApplication()
        // Release the previous scene's orientation lock before orienting the next launch.
        app.terminate()
        XCUIDevice.shared.orientation = .landscapeLeft
        app.launchArguments = ["-SatchelDeveloperControls", "NO", "-SatchelAutoPrepare", "NO", "-SatchelAutoLaunchPreparedGame", "NO", "-SatchelDeletePreparedGames", ""]
#if NAVIGATION_PREVIEW
        app.launchArguments += ["-SatchelPreviewDemo", "library", "-SatchelPreviewScreen", "home", "-SatchelPreviewPreparedOnlyTile", "YES"]
#if !NAVIGATION_NATIVE
        app.launchArguments += ["-SatchelPreviewNativePlay", "NO"]
#endif
#endif
#if NAVIGATION_PREVIEW
        if name.contains("testSimulatorSignatureExpiry") {
            app.launchArguments += ["-SatchelPreviewSignatureDays", "2"]
        }
        // These journeys launch their own fixture and assert its entry screen.
        // Avoid launching and immediately terminating an unrelated default library.
        let fixtureFamilies = ["testDeviceCheck", "testAddDLC", "testPrepareDiagnostics", "testWarmup", "testSplit", "testHomeImportCard", "testSave", "testGameSettings"]
        let fixtureJourneys = ["testSimulatorPanelControls", "testDeveloperControlsHiddenByDefault",
            "testPreparedOnlyLibraryWithoutImports", "testRemovedOriginalDetail", "testEmptyLibraryEntrypoints",
            "testNSZAndXCZImportAndDuplicate", "testImportCancelledThenRestarted", "testGamePageWithoutWarmupRecord",
            "testGamePagePrepareSelectsBase", "testPrepareKeyAndStorageRecoveryMessages",
            "testRemovePreparedGameRemovesAllContentAndTile", "testRemoveOnlyUpdateKeepsPreparedGameAndDLC",
            "testPrepareDLCRowTracksBaseSelection", "testControllerRumblePersistsPerGame",
            "testQuitReturnsHomeAndCanLaunchAgain", "testQuitThenStartDifferentPreparedGame",
            "testPlainMenuQuitReturnsHome", "testLibraryPausesAndResumes", "testPausedGameCardResumes", "testSwitchingGameStopsFirst"]
        if (fixtureFamilies + fixtureJourneys).contains(where: { name.contains($0) }) { return }
#endif
        app.launch()
        _ = try button("import-homebrew")
    }

#if NAVIGATION_PREVIEW && NAVIGATION_NATIVE
    // XCTest on iOS 27.1 can throw while asking for an offscreen element's
    // activation point. Reveal by geometry first, then require real hittability.
    private func revealDeviceCheckControl(_ control: XCUIElement, in scroll: XCUIElement) {
        XCTAssertTrue(control.waitForExistence(timeout: 8))
        for _ in 0..<24 {
            let viewport = scroll.frame.intersection(app.windows.firstMatch.frame).insetBy(dx: 0, dy: 12)
            let frame = control.frame
            if frame.minY >= viewport.minY && frame.maxY <= viewport.maxY { break }
            let step: CGFloat = frame.midY > viewport.midY ? -80 : 80
            let start = scroll.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
            start.press(forDuration: 0.05, thenDragTo: start.withOffset(CGVector(dx: 0, dy: step)))
        }
        XCTAssertTrue(control.isHittable, "Device check control must be hittable: \(control.identifier)")
    }

    private func tapDeviceCheck(_ id: String, scroll: XCUIElement) throws {
        revealDeviceCheckControl(app.buttons[id], in: scroll)
        try tap(id)
    }

    private func openDeviceCheck(_ fixture: String) throws {
        app.launchArguments += ["-SatchelPreviewDeviceCheck", fixture]
        app.launch()
        _ = try button("import-homebrew")
        try tap("open-settings")
        try tap("settings-Device check")
        _ = try button("device-check-copy", scroll: app.scrollViews.firstMatch)
        XCTAssertGreaterThan(app.windows.firstMatch.frame.width, app.windows.firstMatch.frame.height)
    }

    func testDeviceCheckBothLandscapes() throws {
        for orientation in [UIDeviceOrientation.landscapeLeft, .landscapeRight] {
            app.terminate()
            XCUIDevice.shared.orientation = orientation
            try openDeviceCheck("present")
            let scroll = app.scrollViews.containing(.button, identifier: "device-check-copy").firstMatch
            for id in ["environment", "model", "os", "memory-entitlement", "address-entitlement", "available",
                       "limit", "footprint", "address-span", "reservation", "expiry", "free", "team"] {
                revealDeviceCheckControl(app.staticTexts["device-check-" + id], in: scroll)
            }
            XCTAssertEqual(app.staticTexts["device-check-memory-entitlement"].label, "Present in running signature")
            XCTAssertEqual(app.staticTexts["device-check-limit"].label, "6.00 GiB (6442450944 bytes)")
            let shot = XCTAttachment(screenshot: app.screenshot())
            shot.name = "Device check synthetic landscape \(orientation.rawValue)"; shot.lifetime = .keepAlways; add(shot)
            try tapDeviceCheck("device-check-refresh", scroll: scroll)
            _ = try button("device-check-copy", scroll: scroll)
            backgroundAndReturn(app.buttons["close-settings"].firstMatch)
            XCTAssertGreaterThan(app.windows.firstMatch.frame.width, app.windows.firstMatch.frame.height)
            try tap("close-settings")
            _ = try button("import-homebrew")
        }
    }

    func testDeviceCheckCopiesReportWithAndWithoutTeam() throws {
        try openDeviceCheck("present")
        let scroll = app.scrollViews.containing(.button, identifier: "device-check-copy").firstMatch
        let toggle = app.switches["device-check-include-team"]
        XCTAssertEqual(toggle.value as? String, "0")
        func copyReport() throws -> String {
            try tapDeviceCheck("device-check-copy", scroll: scroll)
            XCTAssertEqual(app.buttons["device-check-copy"].label, "Copied")
            try tapDeviceCheck("device-check-inspect-clipboard", scroll: scroll)
            let value = app.staticTexts["device-check-clipboard"].label
            XCTAssertTrue(value.hasPrefix("Satchel Device check\nSampled: "))
            for line in ["Device model: Synthetic phone", "iOS version: 27.1.0 (synthetic)",
                         "Increased memory limit: Present in running signature",
                         "Extended virtual addressing: Present in running signature",
                         "os_proc_available_memory: 5.00 GiB (5368709120 bytes)",
                         "Current process memory limit: 6.00 GiB (6442450944 bytes)",
                         "Contiguous VM reserve tested: At least 64.00 GiB (68719476736 bytes)",
                         "Signature expiry (embedded profile): 2030-01-01T00:00:00Z",
                         "Container volume free space: 40.00 GiB (42949672960 bytes)"] {
                XCTAssertTrue(value.contains(line), "Missing report field: \(line)")
            }
            return value
        }
        let redacted = try copyReport()
        XCTAssertFalse(redacted.contains("SYNTHETIC-TEAM"))
        XCTAssertFalse(redacted.contains("Signing team:"))
        revealDeviceCheckControl(toggle, in: scroll); toggle.tap()
        XCTAssertEqual(toggle.value as? String, "1")
        XCTAssertTrue(try copyReport().contains("Signing team: SYNTHETIC-TEAM"))
        revealDeviceCheckControl(toggle, in: scroll); toggle.tap()
        XCTAssertEqual(try copyReport(), redacted)
        revealDeviceCheckControl(toggle, in: scroll)
        XCTAssertTrue(toggle.isEnabled)
        toggle.tap()
        XCTAssertEqual(toggle.value as? String, "1")
        try tap("close-settings")
        try tap("open-settings")
        try tap("settings-Device check")
        XCTAssertEqual(app.switches["device-check-include-team"].value as? String, "0")
        try tap("close-settings")
    }

    func testDeviceCheckMissingEntitlement() throws {
        try openDeviceCheck("missing")
        let scroll = app.scrollViews.containing(.button, identifier: "device-check-copy").firstMatch
        for id in ["memory-entitlement", "address-entitlement"] {
            revealDeviceCheckControl(app.staticTexts["device-check-" + id], in: scroll)
            XCTAssertEqual(app.staticTexts["device-check-" + id].label, "Missing or disabled in running signature")
        }
        try tapDeviceCheck("device-check-copy", scroll: scroll)
        try tapDeviceCheck("device-check-inspect-clipboard", scroll: scroll)
        let report = app.staticTexts["device-check-clipboard"].label
        XCTAssertTrue(report.contains("Increased memory limit: Missing or disabled in running signature"))
        XCTAssertFalse(report.contains("Present in running signature"))
        XCTAssertFalse(report.contains("SYNTHETIC-TEAM"))
        try tap("close-settings")
    }

    func testDeviceCheckUnknownEntitlement() throws {
        try openDeviceCheck("unknown")
        let scroll = app.scrollViews.containing(.button, identifier: "device-check-copy").firstMatch
        revealDeviceCheckControl(app.staticTexts["device-check-memory-entitlement"], in: scroll)
        XCTAssertEqual(app.staticTexts["device-check-memory-entitlement"].label, "Unknown (signature query unavailable)")
        try tap("close-settings")
    }

    func testDeviceCheckLiveSimulatorProbe() throws {
        try openDeviceCheck("live")
        let scroll = app.scrollViews.containing(.button, identifier: "device-check-copy").firstMatch
        revealDeviceCheckControl(app.staticTexts["device-check-environment"], in: scroll)
        XCTAssertEqual(app.staticTexts["device-check-environment"].label, "Simulator (host limits, not phone capabilities)")
        revealDeviceCheckControl(app.staticTexts["device-check-reservation"], in: scroll)
        XCTAssertTrue(app.staticTexts["device-check-reservation"].label.hasPrefix("At least "))
        XCTAssertTrue(app.staticTexts["device-check-footprint"].label.hasSuffix(" bytes)"))
        revealDeviceCheckControl(app.staticTexts["device-check-expiry"], in: scroll)
        XCTAssertEqual(app.staticTexts["device-check-expiry"].label, "Unknown (profile absent or unreadable)")
        revealDeviceCheckControl(app.staticTexts["device-check-free"], in: scroll)
        XCTAssertTrue(app.staticTexts["device-check-free"].label.hasSuffix(" bytes)"))
        revealDeviceCheckControl(app.staticTexts["device-check-team"], in: scroll)
        XCTAssertEqual(app.staticTexts["device-check-team"].label, "Unknown")
        try tap("close-settings")
    }
#endif

    /// The bundled install guide lives in Settings > About (the first-launch guide is gone).
    func testSettingsInstallHelpBothLandscapes() throws {
        for orientation in [UIDeviceOrientation.landscapeLeft, .landscapeRight] {
            XCUIDevice.shared.orientation = orientation
            try tap("open-settings")
            try tap("settings-About")
            try tap("settings-install-help", scroll: app.scrollViews.containing(.button, identifier: "settings-install-help").firstMatch)
            let content = app.scrollViews["install-help-content"]
            XCTAssertTrue(content.waitForExistence(timeout: 8))
            XCTAssertGreaterThan(app.windows.firstMatch.frame.width, app.windows.firstMatch.frame.height)
            XCTAssertTrue(content.isHittable)
            XCTAssertTrue(app.staticTexts["Install and keep Satchel installed"].isHittable)
            XCTAssertFalse(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "Install help is missing")).firstMatch.exists)
            for heading in ["SideStore", "AltStore Classic", "The seven-day limit", "Satchel's signature warning", "First launch"] {
                revealControl(app.staticTexts[heading], in: content)
            }
            let shot = XCTAttachment(screenshot: app.screenshot())
            shot.name = "Install help landscape \(orientation.rawValue)"; shot.lifetime = .keepAlways; add(shot)
            try tap("install-help-close")
            absent(content)
            try tap("close-settings")
            absent(app.buttons["close-settings"])
            _ = try button("import-homebrew")
        }
    }

#if NAVIGATION_PREVIEW && NAVIGATION_NATIVE
    private func checkPrepareDiagnostics(_ mode: String, hasRows: Bool = false) throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewDiagnostics", mode]
        app.launch()
        _ = try button("import-homebrew")
        try workshop("workshop-prepare")
        let preparation = app.scrollViews.containing(.button, identifier: "prepare-game-diagnostics").firstMatch
        try tap("prepare-game-diagnostics", scroll: preparation)
        XCTAssertTrue(app.staticTexts["Stage: synthetic-runtime"].waitForExistence(timeout: 8),
                      "The real reader must accept this fixture's session metadata")
        XCTAssertTrue(app.staticTexts["Result: returned"].exists)
        let sheet = app.scrollViews.containing(.staticText, identifier: "Stage: synthetic-runtime").firstMatch
        let rows = [
            "MissingService service=acc:u0 interface=IProfile command=10 method=GetImageSize firstSeenFrame=37 count=4",
            "MissingService service=time:u interface=IClock command=42 method=unknown firstSeenFrame=81 count=2"
        ]
        if hasRows {
            for row in rows { revealControl(app.staticTexts.matching(identifier: row).firstMatch, in: sheet) }
        } else {
            XCTAssertFalse(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "MissingService")).firstMatch.exists,
                           "Empty or rejected files must not contribute rows")
        }
        revealControl(app.buttons["Export report"], in: sheet)
        XCTAssertFalse(app.debugDescription.contains("RAW_PAYLOAD_SENTINEL"), "Raw JSON and invalid fields must stay private")
        XCTAssertFalse(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "\"firstSeenFrame\"")).firstMatch.exists)
        XCTAssertEqual(app.state, .runningForeground)
        try tap("Done")
        absent(app.navigationBars["Game report"])
        _ = try button("close-prepare")
        // Reopening exercises the report saved by the first sheet load.
        try tap("prepare-game-diagnostics", scroll: preparation)
        XCTAssertTrue(app.staticTexts["Stage: synthetic-runtime"].waitForExistence(timeout: 8))
        XCTAssertFalse(app.debugDescription.contains("RAW_PAYLOAD_SENTINEL"))
        try tap("Done")
        try tap("close-prepare")
        _ = try button("import-homebrew")
        assertHomeContent()
    }

    func testPrepareDiagnosticsServiceRows() throws { try checkPrepareDiagnostics("rows", hasRows: true) }
    func testPrepareDiagnosticsEmptyFile() throws { try checkPrepareDiagnostics("empty") }
    func testPrepareDiagnosticsEmptyArray() throws { try checkPrepareDiagnostics("empty-array") }
    func testPrepareDiagnosticsMalformedFile() throws { try checkPrepareDiagnostics("malformed") }
    func testPrepareDiagnosticsOversizedFile() throws { try checkPrepareDiagnostics("oversized") }
#endif

#if NAVIGATION_PREVIEW && NAVIGATION_NATIVE
    private func launchDLCFixture(_ mode: String = "pending", keys: Bool = true) throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewDLC", mode, "-SatchelPreviewKeys", keys ? "YES" : "NO"]
        if mode == "pending" {
            app.launchArguments += ["-SatchelPreviewFixTests", "YES", "-SatchelPreviewDLCRun", "YES", "-SatchelPreviewSessionTests", "YES"]
        }
        app.launch()
        _ = try button("import-homebrew")
        if mode == "unprepared" {
            try findLanternShelf()
            try tap("open-focused")
        } else { try openRemovalDetail() }
    }

    private func dlcButton(_ id: String) throws -> XCUIElement {
        revealControl(app.buttons[id], in: app.scrollViews["detail-scroll"])
        return try button(id)
    }

    private func includedDLC(version: Int = 1) throws -> XCUIElement {
        let rows = app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH %@", "included-dlc-"))
        let row = rows.firstMatch
        XCTAssertTrue(row.waitForExistence(timeout: 8), "Included DLC needs its stable title-ID row")
        revealControl(row, in: app.scrollViews["detail-scroll"])
        XCTAssertTrue(row.label.contains("version \(version) · Included"), row.label)
        XCTAssertEqual(row.identifier.count, "included-dlc-".count + 16)
        XCTAssertTrue(row.label.contains(String(row.identifier.suffix(16))), "Row identity must match its displayed DLC title")
        return row
    }

    func testAddDLCIncludedAndNewerVersionAvailability() throws {
        try launchDLCFixture("included")
        let included = try includedDLC().identifier
        XCTAssertFalse(app.buttons["add-dlc"].exists, "An equal included version must not be offered again")
        XCTAssertFalse(app.buttons["cancel-add-dlc"].exists)
        XCTAssertFalse(app.staticTexts["add-dlc-status"].exists)
        app.terminate()
        let mode = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewDLC"))
        app.launchArguments[mode + 1] = "newer"
        app.launch()
        try openRemovalDetail()
        XCTAssertEqual(try includedDLC().identifier, included)
        _ = try dlcButton("add-dlc")
        XCTAssertTrue(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "version 2 · Imported")).firstMatch.exists)
    }

    func testAddDLCWithoutPackageExplainsPrepare() throws {
        try launchDLCFixture("unprepared")
        let explanation = app.staticTexts["Prepare the base game to include this DLC."]
        revealControl(explanation, in: app.scrollViews["detail-scroll"])
        XCTAssertFalse(app.buttons["add-dlc"].exists)
        XCTAssertEqual(app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH %@", "included-dlc-")).count, 0)
    }

    private func startDLC() throws {
        try launchDLCFixture()
        _ = try includedDLC()
        try dlcButton("add-dlc").tap()
        fixtureText("add-dlc-status", equals: "Reading and verifying DLC…")
        XCTAssertFalse(app.buttons["add-dlc"].isEnabled)
        _ = try dlcButton("cancel-add-dlc")
    }

    private func cancelDLC() throws {
        try dlcButton("cancel-add-dlc").tap()
        fixtureText("add-dlc-status", equals: "Cancelling. Your prepared game will be kept.")
        XCTAssertFalse(app.buttons["cancel-add-dlc"].isEnabled)
        fixtureText("add-dlc-status", equals: "Add DLC cancelled. Your prepared game and imports were kept.")
        XCTAssertFalse(app.buttons["cancel-add-dlc"].exists)
        _ = try dlcButton("add-dlc")
        try tap("close-game-detail")
        fixtureText("fixture-removal-state", equals: "base=1 update=1 dlc=2 files=4 packages=2 other=5 save=kept")
    }

    func testAddDLCCancelKeepsPackageAndImports() throws {
        try startDLC()
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        fixtureText("add-dlc-status", equals: "Reading and verifying DLC…")
        try cancelDLC()
    }

    func testAddDLCRunningRefusesPlay() throws {
        try startDLC()
        try tap("play-game")
        // Explicit synthetic-only session boundary. Production PreparedGameView
        // must refuse before invoking the preview verifier or native host.
        try button("player-play").tap()
        XCTAssertTrue(app.staticTexts["Wait for Add DLC to finish, or cancel it before playing."].waitForExistence(timeout: 8))
        try tap("close-verify")
        sessionState("started=0 paused=0 package=none events=", exact: true)
        try openRemovalDetail()
        try cancelDLC()
    }

    func testAddDLCRunningRefusesRemoval() throws {
        try startDLC()
        try dlcButton("detail-remove-import").tap()
        try tap("Remove Lantern Drift")
        XCTAssertTrue(app.staticTexts["Wait for Add DLC to finish before removing this game."].waitForExistence(timeout: 8))
        try tap("OK")
        fixtureText("fixture-removal-state", equals: "base=1 update=1 dlc=2 files=4 packages=2 other=5 save=kept")
        try openRemovalDetail()
        fixtureText("add-dlc-status", equals: "Reading and verifying DLC…")
        try cancelDLC()
    }

    func testAddDLCMissingKeysShowsRecovery() throws {
        try launchDLCFixture("pending", keys: false)
        try dlcButton("add-dlc").tap()
        fixtureText("add-dlc-status", equals: "Import your own prod.keys in Settings > Console keys, then try again.")
        XCTAssertFalse(app.buttons["cancel-add-dlc"].exists)
        XCTAssertTrue(app.buttons["add-dlc"].isEnabled)
        try tap("close-game-detail")
        fixtureText("fixture-removal-state", equals: "base=1 update=1 dlc=2 files=4 packages=2 other=5 save=kept")
    }

    /// The game page's Prepare opens Prepare with that game already chosen.
    func testGamePagePrepareSelectsBase() throws {
        try launchDLCFixture("unprepared")
        try tap("prepare-game")
        _ = try button("close-prepare")
        XCTAssertEqual(app.buttons["prepare-base"].value as? String, "Lantern Drift [Base]", "The game page must preselect its imported base")
        backgroundAndReturn(app.buttons["close-prepare"].firstMatch)
        XCTAssertEqual(app.buttons["prepare-base"].value as? String, "Lantern Drift [Base]")
        // The home screen's single cover swaps the game page for Prepare, so closing returns to the library.
        try tap("close-prepare")
        absent(app.buttons["close-prepare"])
        _ = try button("import-homebrew")
    }

    func testPrepareKeyAndStorageRecoveryMessages() throws {
        let cases = [
            ("missing", "Your prod.keys is missing a key this game needs. Export a fresh prod.keys from your own updated console and replace it in Settings > Console keys."),
            ("invalid", "These keys could not read the game. Replace prod.keys from your own console in Settings > Console keys. If it still fails, check that the game dump is complete."),
            ("space", "There is not enough space on this iPhone. Free space in Settings > General > iPhone Storage, then try again. Your original file in Files is unchanged."),
            ("wrapped-space", "There is not enough space on this iPhone. Free space in Settings > General > iPhone Storage, then try again. Your original file in Files is unchanged.")
        ]
        let original = app.launchArguments
        for (error, expected) in cases {
            app.terminate()
            app.launchArguments = original + ["-SatchelPreviewDLC", "included", "-SatchelPreviewKeys", "YES", "-SatchelPreviewIdentity", "YES", "-SatchelPreviewPrepareError", error]
            app.launch()
            try workshop("workshop-prepare")
            try tap("prepare-base")
            try tap("Lantern Drift [Base]")
            let column = app.scrollViews.containing(.button, identifier: "prepare-base").firstMatch
            // Synthetic worker throws the selected raw error. PrepareJob must map
            // it and the real Prepare panel must render the complete recovery text.
            revealControl(app.buttons["prepare-start"], in: column)
            try button("prepare-start").tap()
            fixtureText("prepare-status", equals: expected)
            revealControl(app.buttons["prepare-retry"], in: column)
            _ = try button("prepare-retry")
            XCTAssertFalse(app.buttons["prepare-cancel"].exists)
            try tap("close-prepare")
            _ = try button("import-homebrew")
        }
    }
#endif

    func testSimulatorSignatureExpiry() throws {
#if NAVIGATION_PREVIEW
        let reminder = app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "SideStore, AltStore or Xcode")).firstMatch
        XCTAssertTrue(reminder.waitForExistence(timeout: 8))
        XCTAssertTrue(reminder.isHittable)
        XCTAssertTrue(reminder.label.contains("expires on"))
        let expiryShot = XCTAttachment(screenshot: app.screenshot())
        expiryShot.name = "Synthetic home expiry reminder"; expiryShot.lifetime = .keepAlways; add(expiryShot)
        app.terminate()
        let days = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewSignatureDays"))
        app.launchArguments[days + 1] = "4"
        app.launch()
        _ = try button("import-homebrew")
        XCTAssertFalse(reminder.exists)
        app.terminate()
        // Quote the negative value as a property-list string so UserDefaults
        // does not interpret it as another command-line option.
        app.launchArguments[days + 1] = "\"-1\""
        app.launch()
        _ = try button("import-homebrew")
        XCTAssertTrue(reminder.waitForExistence(timeout: 8))
        XCTAssertTrue(reminder.label.contains("expired on"))
#else
        throw XCTSkip("Synthetic expiry dates use the isolated simulator app.")
#endif
    }

    override func tearDownWithError() throws {
        if app != nil, (testRun?.totalFailureCount ?? 0) > 0 {
            let hierarchy = XCTAttachment(string: app.debugDescription)
            hierarchy.name = "Navigation failure hierarchy"
            hierarchy.lifetime = .keepAlways
            add(hierarchy)
            for (index, screen) in XCUIScreen.screens.enumerated() {
                let shot = XCTAttachment(screenshot: screen.screenshot())
                shot.name = "Navigation failure screen \(index)"
                shot.lifetime = .keepAlways
                add(shot)
            }
        }
        app = nil
    }

    private func revealControl(_ control: XCUIElement, in column: XCUIElement) {
        XCTAssertTrue(control.waitForExistence(timeout: 8))
        // Full swipes can pass small actions. Move toward its frame in
        // bounded steps, retaining the final hittability requirement.
        // The game page includes per-title settings below the save manager.
        for _ in 0..<32 where !control.isHittable {
            let step: CGFloat = control.frame.midY > column.frame.midY ? -80 : 80
            let start = column.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
            start.press(forDuration: 0.05, thenDragTo: start.withOffset(CGVector(dx: 0, dy: step)))
        }
        XCTAssertTrue(control.isHittable, "Control must be hittable: \(control.identifier)")
    }

    /// The Duo can expose multiple bar representations. Require at least one real
    /// hittable control, never tap an offscreen duplicate by coordinate fallback.
    private func button(_ id: String, scroll: XCUIElement? = nil) throws -> XCUIElement {
        let matches = app.buttons.matching(identifier: id)
        XCTAssertTrue(matches.firstMatch.exists || matches.firstMatch.waitForExistence(timeout: 8), "Missing control: \(id)")
        if let scroll, !matches.allElementsBoundByIndex.contains(where: { $0.isHittable }) {
            revealControl(matches.firstMatch, in: scroll)
        }
        var readyControl = matches.allElementsBoundByIndex.first { $0.isHittable }
        if readyControl == nil {
            let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                readyControl = matches.allElementsBoundByIndex.first { $0.isHittable }
                return readyControl != nil
            }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 5), .completed, "Control is covered: \(id)")
        }
        let control = try XCTUnwrap(readyControl, "Control is covered: \(id)")
        XCTAssertTrue(control.isEnabled, "Control must be enabled: \(id)")
        return control
    }

    private func tap(_ id: String, scroll: XCUIElement? = nil) throws {
        // Central safety boundary, including context-menu confirmation and native launch.
        let forbidden = ["player-play", "prepare-start", "prepare-retry", "prepare-play", "warmup-start", "warmup-play",
                         "confirm-remove-prepared", "play-homebrew", "run-homebrew", "identity-import", "identity-remove", "identity-save",
                         "Refresh artwork", "workshop-load-sample", "workshop-save-sample", "workshop-demo", "Restore backup"]
        XCTAssertFalse(forbidden.contains(id), "Read-only suite cannot invoke: \(id)")
        guard !forbidden.contains(id) else { return }
        try button(id, scroll: scroll).tap()
    }

    private func assertRetiredControlsAbsent() {
        for id in ["workshop-prepared", "close-prepared-games", "play-mode-performance",
                   "play-mode-graphics", "play-mode-custom", "player-advanced", "menu-advanced"] {
            XCTAssertFalse(app.buttons[id].exists, "Retired control: \(id)")
        }
        // These panels no longer contain a render-scale slider. Match controls rather
        // than arbitrary text, since a user's game title can be any string.
        XCTAssertEqual(app.sliders.count, 0, "Retired frame/render controls must not return")
    }

    /// Scroll the real shelf, without assuming game names, counts or installed packages.
    private func focus(where predicate: () -> Bool, reason: String) throws {
        try XCTSkipUnless(app.scrollViews["collection-panel"].exists, "Empty library: " + reason)
        let shelf = app.scrollViews["collection-panel"]
        var previous = ""
        for _ in 0..<60 {
            if predicate() { return }
            let tiles = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "homebrew-"))
            let viewport = shelf.frame.intersection(app.frame)
            // Lazy shelf elements can lack activation points on the Duo. Track
            // scroll progress by geometry only; invoked controls still require hittability.
            let visible = tiles.allElementsBoundByIndex.filter {
                let frame = $0.frame
                return frame.width > 0 && frame.height > 0 && viewport.intersects(frame)
            }
            let signature = visible.map { $0.identifier + String(describing: $0.frame) }.joined()
            if signature == previous { break }
            previous = signature
            // A full-width flick can skip several games. Move one tile at a time
            // so the predicate observes each focused game's actions.
            let step = min(shelf.frame.width * 0.45, visible.map { $0.frame.width }.min() ?? 120)
            let start = shelf.coordinate(withNormalizedOffset: CGVector(dx: 0.7, dy: 0.5))
            start.press(forDuration: 0.05, thenDragTo: start.withOffset(CGVector(dx: -step, dy: 0)))
        }
#if NAVIGATION_PREVIEW
        XCTFail("Synthetic fixture must cover: " + reason)
#else
        throw XCTSkip("Library-dependent path unavailable after shelf traversal: " + reason)
#endif
    }

    private func inspectPlayer() throws {
        _ = try button("player-play") // Inspect only. This is the game-session boundary.
        let picture = app.scrollViews.containing(.button, identifier: "player-screen-fit").firstMatch
        for id in ["player-screen-fit", "player-screen-stretch"] { _ = try button(id, scroll: picture) }
        let vibration = app.switches["player-haptics"]
        XCTAssertTrue(vibration.exists, "Every prepared game exposes its rumble setting")
        XCTAssertTrue(vibration.isEnabled)
        assertRetiredControlsAbsent()
        XCTAssertFalse(app.buttons["close-verify"].exists)
    }

    private func assertSearchHittable(_ field: XCUIElement) {
        if !field.isHittable {
            let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in field.isHittable }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 5), .completed, "Search field must be hittable")
        }
        XCTAssertTrue(field.isHittable, "Search field must be hittable")
    }

    private func absent(_ element: XCUIElement) {
        if element.exists {
            let gone = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in !element.exists }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [gone], timeout: 8), .completed, "Previous screen must disappear")
        }
        XCTAssertFalse(element.exists, "Previous screen must disappear")
    }

    private func backgroundAndReturn(_ marker: XCUIElement) {
        XCTAssertTrue(marker.exists || marker.waitForExistence(timeout: 8))
#if NAVIGATION_PREVIEW
        // A landscape peer avoids iOS 27.1 Simulator's stale portrait automation
        // coordinates after returning from Home or a system app. Satchel still
        // must really background and preserve its panel and session state.
        XCUIApplication(bundleIdentifier: "com.dell.satchel.navigationbackground").activate()
#else
        // Read-only phone journey: activate Settings without changing a setting.
        XCUIApplication(bundleIdentifier: "com.apple.Preferences").activate()
#endif
        let background = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            self.app.state == .runningBackground || self.app.state == .runningBackgroundSuspended
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [background], timeout: 5), .completed, "Activating another app must background Satchel; state: \(self.app.state.rawValue)")
        app.activate()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 5), "App must return to the foreground; state: \(app.state.rawValue)")
        XCTAssertTrue(marker.exists || marker.waitForExistence(timeout: 8), "Panel must survive background and activation")
        // runningForeground arrives during the app-switch animation. Wait for
        // landscape geometry and a usable panel before inspecting or scrolling it.
        let restored = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            let frame = self.app.windows.firstMatch.frame
            return frame.width > frame.height && marker.isHittable
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [restored], timeout: 8), .completed,
                       "Returning app must restore a hittable landscape panel")
    }

    private func cycle(close: String, open: () throws -> Void, inspect: () throws -> Void = {}) throws {
        let marker = app.buttons.matching(identifier: close).firstMatch
        absent(marker)
        try open()
        _ = try button(close)
        try inspect()
        backgroundAndReturn(marker)
        try inspect()
        try tap(close)
        absent(marker)
        _ = try button("import-homebrew")
        try open()
        _ = try button(close)
        try tap(close)
        absent(marker)
    }

    private func workshop(_ item: String) throws {
        try tap("workshop")
        try tap(item)
    }

    /// A toolbar alone is not proof that the home NavigationStack rendered its content.
    private func assertHomeContent() {
        let visible = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            let tiles = self.app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "homebrew-"))
            let viewport = self.app.frame
            return tiles.allElementsBoundByIndex.contains {
                let frame = $0.frame
                return frame.width > 0 && frame.height > 0 && viewport.contains(frame) && $0.isHittable && $0.isEnabled
            }
                || self.app.buttons["hero-import-homebrew"].isHittable
                || self.app.buttons["welcome-step-1"].isHittable
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [visible], timeout: 5), .completed,
                       "Home must show a hittable shelf tile or welcome control within five seconds")
    }

    func testLaunchShowsHomeContent() {
        assertHomeContent()
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        assertHomeContent()
    }

    func testHomeBarsInBothLandscapeRotations() throws {
        for orientation in [UIDeviceOrientation.landscapeLeft, .landscapeRight] {
            XCUIDevice.shared.orientation = orientation
            for id in ["workshop", "open-settings", "import-homebrew"] { _ = try button(id) }
            if app.scrollViews["collection-panel"].exists {
                for id in ["search-library", "sort-library"] { _ = try button(id) }
            }
            backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
            let window = app.windows.firstMatch
            XCTAssertGreaterThan(window.frame.width, window.frame.height)
            try cycle(close: "close-settings", open: { try self.tap("open-settings") })
            try cycle(close: "close-import", open: { try self.tap("import-homebrew") })
        }
    }

    func testSearch() throws {
        try XCTSkipUnless(app.scrollViews["collection-panel"].exists, "Empty library: search and sort are intentionally absent.")
        try tap("search-library")
        let field = app.textFields["library-search"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        assertSearchHittable(field)
        XCTAssertTrue(field.isEnabled)
        field.tap()
        field.typeText("satchel-navigation-" + UUID().uuidString + "\n")
        absent(app.buttons["open-focused"])
        backgroundAndReturn(field)
        assertSearchHittable(field)
        try tap("close-search")
        absent(field)
        XCTAssertTrue(app.buttons["open-focused"].exists || app.buttons["play-focused"].exists)
    }

    func testSort() throws {
        try XCTSkipUnless(app.scrollViews["collection-panel"].exists, "Empty library: sort is intentionally absent.")
        for item in ["Name", "Recently added"] {
            try tap("sort-library")
            _ = try button(item)
            backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
            if !app.buttons[item].exists { try tap("sort-library") }
            try tap(item)
            absent(app.buttons[item])
            try tap("sort-library")
            let selected = try button(item)
            XCTAssertTrue(selected.isSelected, "The chosen sort option must carry the native selected state: \(selected.debugDescription)")
            try tap(item)
            absent(app.buttons[item])
        }
    }

    func testDeveloperControlsHiddenByDefault() throws {
        app.terminate()
        app.launchArguments[1] = "NO"
        app.launch()
        try tap("workshop")
        XCTAssertFalse(app.buttons["workshop-load-sample"].exists)
        XCTAssertFalse(app.buttons["workshop-demo"].exists)
#if NAVIGATION_NATIVE
        _ = try button("workshop-prepare")
        XCTAssertFalse(app.buttons["workshop-prepared"].exists)
#endif
    }

    func testWorkshopControls() throws {
        try tap("workshop")
        for id in ["workshop-load-sample", "workshop-save-sample", "workshop-demo", "workshop-prepared"] {
            XCTAssertFalse(app.buttons[id].exists)
        }
#if NAVIGATION_NATIVE
        _ = try button("workshop-prepare")
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        if !app.buttons["workshop-prepare"].exists { try tap("workshop") }
        _ = try button("workshop-prepare")
#endif
    }

    func testInterpreterDemoNavigation() throws {
        throw XCTSkip("Interpreter tools remain hidden. Opening the demo automatically runs a workload and writes diagnostic reports.")
    }

    func testSettingsSections() throws {
        for page in ["Console keys", "Storage", "Prepared games", "About"] {
            try cycle(close: "close-settings", open: {
                try self.tap("open-settings")
                try self.tap("settings-" + page)
                XCTAssertTrue(self.app.staticTexts["settings-page-" + page].waitForExistence(timeout: 5))
                if page != "Console keys" { self.absent(self.app.staticTexts["settings-page-Console keys"]) }
            }, inspect: {
                self.assertRetiredControlsAbsent()
                for _ in 0..<4 { self.app.scrollViews.firstMatch.swipeDown() }
                if page == "Console keys" {
                    _ = try self.button("import-keys", scroll: self.app.scrollViews.firstMatch)
                    try self.pickerCycle(open: "import-keys")
                } else if page == "Storage" {
                    let setting = self.app.switches["remove-originals-after-prepare"]
                    let scroll = self.app.scrollViews.firstMatch
                    for _ in 0..<4 where !setting.isHittable { scroll.swipeUp() }
                    XCTAssertTrue(setting.exists)
                    XCTAssertTrue(setting.isEnabled)
                    XCTAssertTrue(setting.isHittable)
                    _ = try self.button("Refresh artwork", scroll: self.app.scrollViews.firstMatch)
                } else if page == "Prepared games" {
                    _ = try self.button("Check again", scroll: self.app.scrollViews.firstMatch)
                } else if page == "About" {
                    XCTAssertFalse(self.app.switches["developer-controls"].exists)
                }
            })
        }
    }

    func testImportSettingsLink() throws {
        try tap("import-homebrew")
        _ = try button("close-import")
        try XCTSkipUnless(app.buttons["import-open-settings"].exists, "Console keys already exist: the import-to-settings link is intentionally absent.")
        _ = try button("import-open-settings", scroll: app.scrollViews.firstMatch)
        try tap("import-open-settings")
        absent(app.buttons["close-import"])
        _ = try button("close-settings")
        backgroundAndReturn(app.buttons["close-settings"].firstMatch)
        try tap("close-settings")
        absent(app.buttons["close-settings"])
        try tap("import-homebrew")
        _ = try button("import-open-settings", scroll: app.scrollViews.firstMatch)
        try tap("import-open-settings")
        try tap("close-settings")
        absent(app.buttons["close-settings"])
    }

    func testImportAndFilesCancellation() throws {
        try cycle(close: "close-import", open: { try self.tap("import-homebrew") }, inspect: {
            _ = try self.button("choose-files", scroll: self.app.scrollViews.firstMatch)
            try self.pickerCycle(open: "choose-files")
        })
    }

    private func savePickerCancelControl() throws -> XCUIElement {
        let owned = app.buttons["cancel-save-import"]
        if owned.exists { return try button("cancel-save-import") }
        let direct = app.buttons.matching(identifier: "Cancel").allElementsBoundByIndex.first { $0.isHittable }
        if let direct { XCTAssertTrue(direct.isEnabled); return direct }
        // The Duo document picker puts Cancel in its visible overflow menu.
        // Never tap the covered accessibility representation behind More.
        try tap("BottomOverflowBarButtonItem")
        return try button("Cancel")
    }

    private func savePickerCancellationMarker() throws -> XCUIElement {
        let owned = app.buttons["cancel-save-import"]
        if owned.exists { return try button("cancel-save-import") }
        let cancel = app.buttons["Cancel"].firstMatch
        let overflow = app.buttons["BottomOverflowBarButtonItem"].firstMatch
        // The remote picker can expose its content before its toolbar. Require
        // a usable cancellation route, whether direct or inside the Duo overflow.
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            cancel.isHittable || overflow.isHittable
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 30), .completed,
                       "Files picker must expose a hittable cancellation route")
        return try button(cancel.isHittable ? "Cancel" : "BottomOverflowBarButtonItem")
    }

    private func savePickerCycle(open: String) throws {
        let panel = app.scrollViews["detail-scroll"]
        let picker = app.navigationBars["FullDocumentManagerViewControllerNavigationBar"]
        for _ in 0..<2 {
            // Full upward swipes can pass the small save actions and leave them
            // above the viewport. Reveal in bounded steps in either direction.
            revealControl(app.buttons[open], in: panel)
            try tap(open)
            XCTAssertTrue(picker.waitForExistence(timeout: 30), "Files picker must appear after one tap")
            backgroundAndReturn(try savePickerCancellationMarker())
            try savePickerCancelControl().tap()
            absent(app.navigationBars["FullDocumentManagerViewControllerNavigationBar"])
            _ = try button("close-game-detail")
        }
    }

    private func pickerCycle(open: String) throws {
        // A picker interruption can reset the presenting panel's scroll offset.
        // Resolve its scroll view again before each open, preserving hit checks.
        let panel = app.scrollViews.containing(.button, identifier: open).firstMatch
        try tap(open, scroll: panel)
        let picker = app.navigationBars["FullDocumentManagerViewControllerNavigationBar"]
        let cancel = app.buttons["Cancel"].firstMatch
        // Files loads in a separate system service. On a cold simulator its
        // presentation can start near the ordinary control's eight-second limit.
        // Wait for the real picker once per open, then keep the control checks.
        XCTAssertTrue(picker.waitForExistence(timeout: 30), "Files picker must appear after one tap")
        _ = try button("Cancel")
        backgroundAndReturn(cancel)
        try tap("Cancel")
        absent(cancel)
        absent(picker)
        try tap(open, scroll: panel)
        XCTAssertTrue(picker.waitForExistence(timeout: 30), "Files picker must reopen after one tap")
        try tap("Cancel")
        absent(cancel)
        absent(picker)
    }

#if NAVIGATION_PREVIEW
    /// Supplemental fixture checks keep panel validation possible when home fails.
    /// They do not replace the real home open/close journeys or prove phone navigation.
    func testSimulatorPanelControls() throws {
        let screens = [("settings", "close-settings"), ("import", "close-import"),
                       ("detail", "close-game-detail"), ("prepare", "close-prepare"),
                       ("player", "close-player-settings")]
        for (screen, close) in screens {
            app.terminate()
            let index = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewScreen"))
            // Setup uses a single screen argument; replace rather than persist defaults.
            app.launchArguments[index + 1] = screen
            app.launch()
            _ = try button(close)
            func inspect() throws {
                switch screen {
                case "settings":
                    for page in ["Console keys", "Storage", "Prepared games", "About"] {
                        _ = try button("settings-" + page)
                    }
                    _ = try button("import-keys", scroll: app.scrollViews.firstMatch)
                case "import": _ = try button("choose-files", scroll: app.scrollViews.firstMatch)
                case "detail": _ = try button("play-game")
                case "prepare":
                    _ = try button("prepare-base")
                    _ = try button("prepare-update")
                    let identity = app.scrollViews.containing(.button, identifier: "identity-import").firstMatch
                    _ = try button("identity-import", scroll: identity)
                case "player": try inspectPlayer()
                default: XCTFail("Unknown fixture screen")
                }
                assertRetiredControlsAbsent()
            }
            try inspect()
            backgroundAndReturn(app.buttons[close].firstMatch)
            _ = try button(close)
            try inspect()
        }
    }

    func testPreparedOnlyLibraryWithoutImports() throws {
        app.terminate()
        let index = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewDemo"))
        app.launchArguments[index + 1] = "prepared-only"
        app.launch()
        XCTAssertTrue(app.scrollViews["collection-panel"].waitForExistence(timeout: 5),
                      "Prepared packages must show a shelf even with zero imported library records")
        assertHomeContent()
        _ = try button("homebrew-Navigation Meadow")
        XCTAssertFalse(app.buttons["hero-import-homebrew"].exists)
        XCTAssertFalse(app.buttons["open-focused"].exists)
        try cycle(close: "close-player-settings", open: { try self.tap("play-focused") },
                  inspect: { try self.inspectPlayer() })
    }

    func testRemovedOriginalDetail() throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewOriginalRemoved", "YES"]
        app.launch()
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "retained prepared import record")
        try cycle(close: "close-game-detail", open: { try self.tap("open-focused") }, inspect: {
            _ = try self.button("play-game")
            XCTAssertTrue(self.app.staticTexts["game-original-status"].label.contains("original file was removed"))
            XCTAssertFalse(self.app.buttons["prepare-game"].exists)
            self.assertRetiredControlsAbsent()
        })
    }
#endif

    func testGameDetail() throws {
        try focus(where: { self.app.buttons["open-focused"].exists }, reason: "imported game page")
        try cycle(close: "close-game-detail", open: { try self.tap("open-focused") }, inspect: {
            XCTAssertTrue(self.app.scrollViews["detail-scroll"].waitForExistence(timeout: 5))
            self.assertRetiredControlsAbsent()
            // Launch and removal controls are never invoked.
            for id in ["play-homebrew", "play-game", "prepare-game"] where self.app.buttons[id].exists {
                _ = try self.button(id)
            }
            for id in ["show-diagnostics", "detail-remove-import"] where self.app.buttons[id].exists {
                _ = try self.button(id, scroll: self.app.scrollViews["detail-scroll"])
            }
        })
    }

    func testGameDetailPrimaryLink() throws {
        try focus(where: { self.app.buttons["open-focused"].exists }, reason: "imported game primary action")
        try tap("open-focused")
        let paths = [("play-game", "close-player-settings"), ("prepare-game", "close-prepare"),
                     ("detail-add-keys", "close-settings")]
        guard let path = paths.first(where: { app.buttons[$0.0].exists }) else {
            try tap("close-game-detail")
            throw XCTSkip("Focused imported item has no read-only primary link; homebrew launch is never invoked.")
        }
        try tap("close-game-detail")
        try cycle(close: path.1, open: {
            try self.tap("open-focused")
            try self.tap(path.0)
            self.absent(self.app.buttons["close-game-detail"])
        }, inspect: {
            if path.0 == "play-game" { try self.inspectPlayer() }
        })
    }

#if NAVIGATION_NATIVE
    func testGameSaveImportPickerCancellation() throws {
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "imported prepared game with a save panel")
        try tap("open-focused")
        let scroll = app.scrollViews["detail-scroll"]
        revealControl(app.buttons["import-save"], in: scroll)
        _ = try button("import-save")
        XCTAssertTrue(app.buttons["export-save"].exists)
        try savePickerCycle(open: "import-save")
        try tap("close-game-detail")
        _ = try button("import-homebrew")
    }
#endif

    func testHomeWelcomeButtons() throws {
        try XCTSkipIf(app.scrollViews["collection-panel"].exists, "Populated library: welcome buttons are intentionally absent.")
        try welcomeEntrypoints()
    }

    func testHomeKeysStatus() throws {
        try cycle(close: "close-settings", open: { try self.tap("keys-status") })
    }

    func testPreparedTileFocusThenPlay() throws {
        try focus(where: { self.app.buttons["play-focused"].exists && !self.app.buttons["open-focused"].exists },
                  reason: "prepared-only shelf tile")
        let focused = app.staticTexts.matching(NSPredicate(format: "identifier BEGINSWITH %@", "focused-item-prepared:")).firstMatch
        XCTAssertTrue(focused.exists, "Prepared-only focus uses a prepared:<folder> shelf ID")
        let id = focused.identifier
        let shelf = app.scrollViews["collection-panel"]
        // Scrolling moves the shelf focus. Return to this package and then use Play.
        shelf.swipeLeft()
        for _ in 0..<60 where !app.staticTexts[id].exists { shelf.swipeRight() }
        XCTAssertTrue(app.staticTexts[id].exists)
        _ = try button("play-focused")
        try cycle(close: "close-player-settings", open: { try self.tap("play-focused") },
                  inspect: { try self.inspectPlayer() })
    }

    func testHomePlayScreen() throws {
        try focus(where: { self.app.buttons["play-focused"].exists }, reason: "prepared game Play shortcut")
        try cycle(close: "close-player-settings", open: { try self.tap("play-focused") },
                  inspect: { try self.inspectPlayer() })
    }

    func testPreparedTileRemovalCancellation() throws {
        try focus(where: { self.app.buttons["play-focused"].exists && !self.app.buttons["open-focused"].exists },
                  reason: "prepared-only context menu")
        let focused = app.staticTexts.matching(NSPredicate(format: "identifier BEGINSWITH %@", "focused-item-prepared:")).firstMatch
        XCTAssertTrue(focused.exists)
        let id = "homebrew-" + focused.label
        _ = try button(id)
        for _ in 0..<2 {
            try button(id).press(forDuration: 1)
            _ = try button("library-context-open")
            _ = try button("library-context-remove-prepared")
            // Native menus can dismiss on background. Reopen and require the same actions.
            backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
            if !app.buttons["library-context-remove-prepared"].exists { try button(id).press(forDuration: 1) }
            try tap("library-context-remove-prepared")
            _ = try button("confirm-remove-prepared") // Never invoke.
            _ = try button("cancel-remove-prepared")
            backgroundAndReturn(app.buttons["cancel-remove-prepared"].firstMatch)
            _ = try button("confirm-remove-prepared")
            try tap("cancel-remove-prepared")
            absent(app.buttons["confirm-remove-prepared"])
            _ = try button(id)
            _ = try button("play-focused")
        }
        // Exercise Open in the context menu, which must also stop at the play screen.
        try cycle(close: "close-player-settings", open: {
            try self.button(id).press(forDuration: 1)
            try self.tap("library-context-open")
        }, inspect: { try self.inspectPlayer() })
    }

    private func welcomeEntrypoints() throws {
        _ = try button("hero-import-homebrew")
        _ = try button("welcome-step-1")
        for id in ["hero-import-homebrew", "welcome-step-1", "choose-focused-files"] where app.buttons[id].exists {
            let close = id == "welcome-step-1" ? "close-settings" : "close-import"
            try cycle(close: close, open: { try self.tap(id) })
        }
    }

    func testHomeAddTile() throws {
        try XCTSkipUnless(app.buttons["search-library"].exists, "Empty library uses the welcome buttons instead of a shelf add tile.")
        if app.buttons["search-library"].exists {
            // Filtering to an absent title makes the add tile reachable for any library size.
            try tap("search-library")
            let search = app.textFields["library-search"]
            XCTAssertTrue(search.waitForExistence(timeout: 5))
            assertSearchHittable(search)
            search.tap()
            search.typeText("satchel-navigation-" + UUID().uuidString + "\n")
            absent(app.buttons["open-focused"])
            try tap("add-games-tile")
            if !app.buttons["close-import"].exists { try tap("add-games-tile") }
            _ = try button("close-import")
            backgroundAndReturn(app.buttons["close-import"])
            try tap("close-import")
            absent(app.buttons["close-import"])
            try tap("add-games-tile")
            try tap("close-import")
            absent(app.buttons["close-import"])
            try tap("close-search")
            absent(app.textFields["library-search"])
            if app.buttons["choose-focused-files"].exists {
                try cycle(close: "close-import", open: { try self.tap("choose-focused-files") })
            }
        }
    }

    func testPauseMenuWithoutGame() throws {
        throw XCTSkip("Pause menu requires an active native game session. Read-only navigation never starts one; the synthetic preview is not evidence for this phone path.")
    }

#if NAVIGATION_NATIVE
    func testPrepareAndSigningIdentity() throws {
        try cycle(close: "close-prepare", open: { try self.workshop("workshop-prepare") }, inspect: {
            _ = try self.button("prepare-base")
            _ = try self.button("prepare-update")
            // SwiftUI Picker menus expose their option titles through UIKit, not the
            // accessibility identifiers attached to the source Text views.
            for (id, title) in [("prepare-base", "Select base"), ("prepare-update", "No update selected")] {
                try self.tap(id)
                _ = try self.button(title)
                self.backgroundAndReturn(self.app.buttons["close-prepare"].firstMatch)
                if !self.app.buttons[title].exists { try self.tap(id) }
                try self.tap(title)
                self.absent(self.app.buttons[title])
            }
            let start = self.app.buttons["prepare-start"]
            let gameColumn = self.app.scrollViews.containing(.button, identifier: "prepare-base").firstMatch
            for _ in 0..<5 where !start.isHittable { gameColumn.swipeUp() }
            XCTAssertTrue(start.isHittable)
            XCTAssertTrue(self.app.buttons["prepare-start"].exists)
            XCTAssertFalse(self.app.buttons["prepare-start"].isEnabled, "No base is selected and Prepare must not run")
            XCTAssertTrue(self.app.staticTexts["prepare-status"].exists)
            let identityColumn = self.app.scrollViews.containing(.button, identifier: "identity-import").firstMatch
            _ = try self.button("identity-import", scroll: identityColumn)
            // Identity controls are inspected only, no picker or Keychain mutation.
            if self.app.buttons["identity-remove"].exists { _ = try self.button("identity-remove", scroll: identityColumn) }
            XCTAssertFalse(self.app.secureTextFields["identity-passphrase"].exists)
            XCTAssertFalse(self.app.buttons["identity-save"].exists)
            for _ in 0..<3 { gameColumn.swipeDown() }
        })
    }
#endif

#if NAVIGATION_PREVIEW
    func testEmptyLibraryEntrypoints() throws {
        app.terminate()
        let index = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewDemo"))
        app.launchArguments[index + 1] = "empty"
        app.launch()
        _ = try button("import-homebrew")
        assertHomeContent()
        absent(app.buttons["search-library"])
        absent(app.buttons["sort-library"])
        try welcomeEntrypoints()
    }
#endif
#if NAVIGATION_PREVIEW
    func testNSZAndXCZImportAndDuplicate() throws {
        for format in ["nsz", "xcz"] {
            app.terminate()
            app.launchArguments = ["-SatchelDeveloperControls", "NO", "-SatchelPreviewDemo", "empty",
                "-SatchelPreviewScreen", "compressed-import", "-SatchelPreviewKeys", "NO",
                "-SatchelPreviewCompressedFormat", format]
            app.launch()
            _ = try button("choose-files", scroll: app.scrollViews.firstMatch)
            XCTAssertTrue(app.staticTexts["XCI, NSP, XCZ or NSZ"].exists)
            try tap("fixture-import-package")
            XCTAssertTrue(app.staticTexts["ADDED TO YOUR LIBRARY"].waitForExistence(timeout: 15))
            XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, format)
            _ = try button("open-imported", scroll: app.scrollViews.firstMatch)
            backgroundAndReturn(app.buttons["close-import"].firstMatch)
            XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, format)
            try tap("fixture-import-package")
            XCTAssertTrue(app.staticTexts["ALREADY IN YOUR LIBRARY"].waitForExistence(timeout: 15))
            XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, format, "Duplicate import must keep exactly one record")
        }
    }

    func testImportCancelledThenRestarted() throws {
        app.terminate()
        app.launchArguments = ["-SatchelDeveloperControls", "NO", "-SatchelPreviewDemo", "empty",
            "-SatchelPreviewScreen", "compressed-import", "-SatchelPreviewKeys", "NO",
            "-SatchelPreviewCompressedFormat", "nsz", "-SatchelPreviewImportCancellation", "YES"]
        app.launch()
        try tap("fixture-import-package")
        fixtureText("fixture-import-cancellation", equals: "items=0 partial=1 source=unchanged")
        try tap("cancel-import", scroll: app.scrollViews.firstMatch)
        XCTAssertTrue(app.staticTexts["Stopped importing Synthetic import"].waitForExistence(timeout: 8))
        fixtureText("fixture-import-cancellation", equals: "items=0 partial=0 source=unchanged")
        backgroundAndReturn(app.buttons["close-import"].firstMatch)
        try tap("fixture-import-package")
        XCTAssertTrue(app.staticTexts["ADDED TO YOUR LIBRARY"].waitForExistence(timeout: 15))
        fixtureText("fixture-import-cancellation", equals: "items=1 partial=0 source=unchanged")
        fixtureText("fixture-import-integrity", equals: "one complete byte-identical container")
        XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, "nsz")
        XCTAssertFalse(app.buttons["cancel-import"].exists)
    }

    private func launchHomeImport(_ mode: String) throws -> XCUIElement {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewHomeImport", mode]
        app.launch()
        _ = try button("import-homebrew")
        let card = app.buttons["import-job-card"].firstMatch
        XCTAssertTrue(card.waitForExistence(timeout: 15), "The home screen shows the import's card")
        return card
    }

    private func cardLabel(_ card: XCUIElement, contains text: String) {
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in card.exists && card.label.contains(text) }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 15), .completed,
                       "Expected the card to say \(text); actual: \(card.exists ? card.label : "missing")")
    }

    /// An import from home ends on a card that stays until dismissed; nothing opens by itself.
    func testHomeImportCardAddedOpensGame() throws {
        let card = try launchHomeImport("nsz")
        cardLabel(card, contains: "Added")
        XCTAssertFalse(app.buttons["close-game-detail"].exists, "A finished import must not open the game page by itself")
        backgroundAndReturn(card)
        cardLabel(card, contains: "Added")
        try tap("import-job-open")
        _ = try button("close-game-detail")
        try tap("close-game-detail")
        cardLabel(app.buttons["import-job-card"].firstMatch, contains: "Added")
        try tap("import-job-dismiss")
        absent(app.buttons["import-job-card"])
        // A + entry point starts fresh: the import screen shows its intro, not the old result.
        try tap("import-homebrew")
        _ = try button("choose-files", scroll: app.scrollViews.firstMatch)
        try tap("close-import")
    }

    /// A failed import keeps its reason on the card; Details shows the full result.
    func testHomeImportCardFailureShowsDetails() throws {
        let card = try launchHomeImport("broken")
        cardLabel(card, contains: "Couldn't add Broken Fixture")
        XCTAssertFalse(app.alerts.firstMatch.exists, "Import results use the card, not an alert")
        try tap("import-job-details")
        _ = try button("close-import")
        XCTAssertTrue(app.staticTexts["Couldn't add Broken Fixture"].waitForExistence(timeout: 8),
                      "Details opens the import screen with the result still showing")
        try tap("close-import")
        cardLabel(app.buttons["import-job-card"].firstMatch, contains: "Couldn't add Broken Fixture")
        try tap("import-job-dismiss")
        absent(app.buttons["import-job-card"])
    }

    private func fixtureText(_ id: String, equals expected: String) {
        let marker = app.staticTexts[id]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            marker.exists && marker.label == expected
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 8), .completed,
                       "Expected \(expected); actual: \(marker.exists ? marker.label : "missing")")
    }

    private func launchFixFixture() throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewFixTests", "YES"]
        app.launch()
        fixtureText("fixture-removal-state", equals: "base=1 update=1 dlc=2 files=4 packages=2 other=5 save=kept")
    }

    private func openRemovalDetail() throws {
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "synthetic prepared game with update and DLC")
        try tap("open-focused")
    }

    private func findLanternShelf() throws {
        try tap("search-library")
        let search = app.textFields["library-search"]
        assertSearchHittable(search)
        search.tap(); search.typeText("Lantern\n")
    }

    func testRemovePreparedGameRemovesAllContentAndTile() throws {
        try launchFixFixture()
        try openRemovalDetail()
        try tap("detail-remove-import", scroll: app.scrollViews["detail-scroll"])
        _ = try button("Remove Lantern Drift")
        if app.buttons["Keep"].exists {
            try tap("Keep")
        } else {
            // The Duo popover omits the cancel button and uses outside dismissal.
            let dismissRegion = app.otherElements["PopoverDismissRegion"]
            XCTAssertTrue(dismissRegion.waitForExistence(timeout: 5))
            XCTAssertTrue(dismissRegion.isHittable)
            dismissRegion.tap()
        }
        absent(app.buttons["Remove Lantern Drift"])
        try tap("close-game-detail")
        fixtureText("fixture-removal-state", equals: "base=1 update=1 dlc=2 files=4 packages=2 other=5 save=kept")
        try openRemovalDetail()
        try tap("detail-remove-import", scroll: app.scrollViews["detail-scroll"])
        try tap("Remove Lantern Drift")
        _ = try button("import-homebrew")
        fixtureText("fixture-removal-state", equals: "base=0 update=0 dlc=0 files=0 packages=0 other=5 save=kept")
        try findLanternShelf()
        XCTAssertEqual(app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "homebrew-")).count, 0,
                       "No imported, update, DLC or prepared-only tile may remain")
        // Relaunch without reseeding to require the deletion to persist on disk.
        app.terminate()
        let demo = try XCTUnwrap(app.launchArguments.firstIndex(of: "-SatchelPreviewDemo"))
        app.launchArguments.removeSubrange(demo...(demo + 1))
        app.launch()
        fixtureText("fixture-removal-state", equals: "base=0 update=0 dlc=0 files=0 packages=0 other=5 save=kept")
        try findLanternShelf()
        XCTAssertEqual(app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "homebrew-")).count, 0)
    }

    func testRemoveOnlyUpdateKeepsPreparedGameAndDLC() throws {
        try launchFixFixture()
        try openRemovalDetail()
        let menu = app.buttons["Update options"]
        XCTAssertTrue(menu.waitForExistence(timeout: 8))
        let column = app.scrollViews["detail-scroll"]
        // A full swipe can skip this small row. Scroll toward it in short steps.
        for _ in 0..<18 where !menu.isHittable && !menu.buttons.firstMatch.isHittable {
            let step: CGFloat = menu.frame.midY > column.frame.midY ? -80 : 80
            let start = column.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
            start.press(forDuration: 0.05, thenDragTo: start.withOffset(CGVector(dx: 0, dy: step)))
        }
        let control = menu.isHittable ? menu : menu.buttons.firstMatch
        XCTAssertTrue(control.isHittable, "Update options must be hittable")
        XCTAssertTrue(control.isEnabled)
        control.tap()
        try tap("Remove update")
        try tap("Remove Lantern Drift")
        try tap("close-game-detail")
        fixtureText("fixture-removal-state", equals: "base=1 update=0 dlc=2 files=3 packages=2 other=5 save=kept")
        try findLanternShelf()
        _ = try button("homebrew-Lantern Drift [Base]")
        _ = try button("open-focused")
        _ = try button("play-focused")
    }

    func testPrepareDLCRowTracksBaseSelection() throws {
        try launchFixFixture()
        try workshop("workshop-prepare")
        let column = app.scrollViews.containing(.button, identifier: "prepare-base").firstMatch
        func row(_ text: String) {
            let row = app.descendants(matching: .any).matching(identifier: "prepare-dlc").firstMatch
            XCTAssertTrue(row.waitForExistence(timeout: 5))
            for _ in 0..<4 where !row.isHittable { column.swipeUp() }
            XCTAssertTrue(row.isHittable)
            XCTAssertTrue(row.label.contains(text), "DLC row must show \(text), actual: \(row.label)")
        }
        func select(_ title: String) throws {
            for _ in 0..<4 where !app.buttons["prepare-base"].isHittable { column.swipeDown() }
            try tap("prepare-base")
            try tap(title)
        }
        row("Choose a base game first")
        try select("Tidebreaker")
        row("None imported")
        try select("Lantern Drift [Base]")
        row("2 DLC included")
        try select("Select base")
        row("Choose a base game first")
    }

    private func startWarmupFixture(ending: String = "complete") throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewWarmupTests", "YES", "-SatchelPreviewWarmupEnd", ending]
        app.launch()
        XCTAssertFalse(app.buttons["warmup-home-card"].exists)
        try tap("fixture-warmup-start")
        fixtureText("fixture-warmup-state", equals: "running:running")
        _ = try button("warmup-home-card")
        XCTAssertFalse(app.buttons["warmup-home-dismiss"].exists)
    }

    func testGamePageWithoutWarmupRecord() throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewWarmupTests", "YES"]
        app.launch()
        fixtureText("fixture-warmup-state", equals: "idle:none")
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "prepared game with no warm-up record")
        try tap("open-focused")
        let status = app.descendants(matching: .any)["warmup-status"]
        revealControl(status, in: app.scrollViews["detail-scroll"])
        XCTAssertTrue(status.label.contains("Builds this game's known shaders ahead of time"), status.label)
        let start = try button("warmup-start", scroll: app.scrollViews["detail-scroll"])
        XCTAssertEqual(start.label, "Warm up")
        XCTAssertFalse(app.buttons["warmup-play"].exists, "The game page already has its own Play action")
        start.tap()
        _ = try button("warmup-pause", scroll: app.scrollViews["detail-scroll"])
        try tap("warmup-pause", scroll: app.scrollViews["detail-scroll"])
        fixtureText("fixture-warmup-state", equals: "idle:paused")
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        fixtureText("fixture-warmup-state", equals: "idle:paused")
        XCTAssertTrue(try button("warmup-start", scroll: app.scrollViews["detail-scroll"]).label.contains("Resume"))
        XCTAssertFalse(app.descendants(matching: .any)["warmup-status"].label.contains("Stopped by iOS"),
                       "A user pause must not auto-resume on activation")
    }

    private func interruptedWarmup(lateAcknowledgement: Bool) throws {
        app.launchArguments += ["-SatchelPreviewWarmupExpire", "YES",
            "-SatchelPreviewWarmupHoldCancellation", lateAcknowledgement ? "YES" : "NO"]
        try startWarmupFixture()
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        if lateAcknowledgement { try tap("fixture-warmup-release") }
        fixtureText("fixture-warmup-lifecycle", equals: "starts=2 interrupted=1")
        fixtureText("fixture-warmup-state", equals: "running:running")
        try tap("warmup-home-card")
        _ = try button("warmup-pause", scroll: app.scrollViews["detail-scroll"])
        let status = app.descendants(matching: .any)["warmup-status"]
        XCTAssertTrue(status.label.contains("Synthetic shader work"), status.label)
        try tap("warmup-pause", scroll: app.scrollViews["detail-scroll"])
        fixtureText("fixture-warmup-state", equals: "idle:paused")
    }

    func testWarmupSystemStopResumesOnReturn() throws {
        try interruptedWarmup(lateAcknowledgement: false)
    }

    func testWarmupSystemStopAcknowledgedAfterReturnResumes() throws {
        try interruptedWarmup(lateAcknowledgement: true)
    }

    func testWarmupHomeCardDisappearsWhenFinished() throws {
        try startWarmupFixture()
        try tap("fixture-warmup-finish")
        fixtureText("fixture-warmup-state", equals: "idle:complete")
        absent(app.buttons["warmup-home-card"])
        XCTAssertFalse(app.buttons["warmup-home-dismiss"].exists)
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        XCTAssertFalse(app.buttons["warmup-home-card"].exists)
    }

    func testWarmupPausedCardCanBeDismissed() throws {
        try startWarmupFixture()
        // The card opens the game page, which owns warm-up (not the Prepare screen).
        try tap("warmup-home-card")
        _ = try button("close-game-detail")
        try tap("warmup-pause", scroll: app.scrollViews["detail-scroll"])
        fixtureText("fixture-warmup-state", equals: "idle:paused")
        revealControl(app.buttons["warmup-start"], in: app.scrollViews["detail-scroll"])
        XCTAssertTrue(app.buttons["warmup-start"].label.contains("Resume"), app.buttons["warmup-start"].label)
        XCTAssertTrue(app.buttons["warmup-start"].isEnabled)
        try tap("close-game-detail")
        _ = try button("warmup-home-card")
        try tap("warmup-home-dismiss")
        fixtureText("fixture-warmup-state", equals: "idle:none")
        absent(app.buttons["warmup-home-card"])
    }

    /// After a finished warm-up the home card leaves, but the game page still shows the
    /// result and offers it again (the warm-up used to vanish after the first play).
    func testWarmupStaysOnGamePageAfterFinish() throws {
        try startWarmupFixture()
        try tap("fixture-warmup-finish")
        fixtureText("fixture-warmup-state", equals: "idle:complete")
        absent(app.buttons["warmup-home-card"])
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "synthetic prepared game with a finished warm-up")
        try tap("open-focused")
        let status = app.descendants(matching: .any)["warmup-status"]
        revealControl(status, in: app.scrollViews["detail-scroll"])
        XCTAssertTrue(status.label.contains("Known shaders are warmed up"), status.label)
        revealControl(app.buttons["warmup-start"], in: app.scrollViews["detail-scroll"])
        XCTAssertTrue(app.buttons["warmup-start"].label.contains("Warm up again"), app.buttons["warmup-start"].label)
        XCTAssertTrue(app.buttons["warmup-start"].isEnabled)
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        XCTAssertTrue(app.descendants(matching: .any)["warmup-status"].label.contains("Known shaders are warmed up"))
        try tap("close-game-detail")
        _ = try button("import-homebrew")
    }

    func testWarmupFailedCardCanBeDismissed() throws {
        try startWarmupFixture(ending: "failed")
        try tap("fixture-warmup-finish")
        fixtureText("fixture-warmup-state", equals: "idle:failed")
        _ = try button("warmup-home-card")
        try tap("warmup-home-dismiss")
        fixtureText("fixture-warmup-state", equals: "idle:none")
        absent(app.buttons["warmup-home-card"])
    }

    func testSplitFolderImports() throws {
        for format in ["nsp", "xci", "nsz", "xcz"] { try supportedSplitImport(format: format, style: "folder") }
    }

    func testSplitSelectedPartsImports() throws {
        for format in ["nsp", "xci", "nsz", "xcz"] { try supportedSplitImport(format: format, style: "selected") }
    }

    private func supportedSplitImport(format: String, style: String) throws {
        app.terminate()
        app.launchArguments = ["-SatchelDeveloperControls", "NO", "-SatchelPreviewDemo", "empty",
            "-SatchelPreviewScreen", "compressed-import", "-SatchelPreviewKeys", "NO",
            "-SatchelPreviewCompressedFormat", format, "-SatchelPreviewSplitImport", "YES", "-SatchelPreviewSplitStyle", style]
        app.launch()
        try tap("fixture-import-package")
        XCTAssertTrue(app.staticTexts["ADDED TO YOUR LIBRARY"].waitForExistence(timeout: 15), "\(style) \(format) split import")
        fixtureText("fixture-imported-formats", equals: format)
        fixtureText("fixture-import-integrity", equals: "one complete byte-identical container")
        try tap("fixture-import-package")
        XCTAssertTrue(app.staticTexts["ALREADY IN YOUR LIBRARY"].waitForExistence(timeout: 15))
        fixtureText("fixture-imported-formats", equals: format)
        fixtureText("fixture-import-integrity", equals: "one complete byte-identical container")
    }

    func testSplitNSZImport() throws { try splitImport(format: "nsz") }
    func testSplitXCZImport() throws { try splitImport(format: "xcz") }

    private func splitImport(format: String) throws {
        app.terminate()
        app.launchArguments = ["-SatchelDeveloperControls", "NO", "-SatchelPreviewDemo", "empty",
            "-SatchelPreviewScreen", "compressed-import", "-SatchelPreviewKeys", "NO",
            "-SatchelPreviewCompressedFormat", format, "-SatchelPreviewSplitImport", "YES"]
        app.launch()
        try tap("fixture-import-package")
        XCTAssertTrue(app.staticTexts["ADDED TO YOUR LIBRARY"].waitForExistence(timeout: 15),
                      "Selecting all three .\(format).00/.01/.02 parts must import one complete container")
        XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, format)
        backgroundAndReturn(app.buttons["close-import"].firstMatch)
        try tap("fixture-import-package")
        XCTAssertTrue(app.staticTexts["ALREADY IN YOUR LIBRARY"].waitForExistence(timeout: 15))
        XCTAssertEqual(app.staticTexts["fixture-imported-formats"].label, format)
    }

    private func openSettingsDetail() throws {
        _ = try button("import-homebrew")
        // This journey owns a known synthetic title. Select its real shelf tile
        // instead of depending on scroll snapping to change the focused game.
        try tap("homebrew-Lantern Drift [Base]", scroll: app.scrollViews["collection-panel"])
        if !app.buttons["close-game-detail"].waitForExistence(timeout: 1) { try tap("open-focused") }
        _ = try button("close-game-detail")
        XCTAssertEqual(app.staticTexts["detail-game-title"].label, "Lantern Drift")
    }

    private func chooseGameSetting(_ key: String, _ label: String, value: String) throws {
        let scroll = app.scrollViews["detail-scroll"]
        revealDeviceCheckControl(app.buttons["game-setting-" + key], in: scroll)
        try tap("game-setting-" + key)
        try tap(label)
        XCTAssertEqual(app.buttons["game-setting-" + key].value as? String, value)
        XCTAssertEqual(app.staticTexts["game-override-" + key].label, "Override")
    }

    func testGameSettingsPersistResetAndBothLandscapes() throws {
        app.launch()
        try openSettingsDetail()
        let choices = [("DM_FPS_TARGET", "60 FPS", "60"), ("DM_RENDER_SCALE", "75%", "0.75"),
                       ("DM_SCREEN_MODE", "Stretch", "stretch"), ("DM_HAPTICS", "Off", "off"),
                       ("DM_PIPELINE_MODE", "Hybrid", "hybrid")]
        for (key, label, value) in choices {
            try chooseGameSetting(key, label, value: value)
            XCTAssertTrue(app.staticTexts["game-default-" + key].label.hasPrefix("Global default:"))
        }
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        app.terminate(); app.launch()
        try openSettingsDetail()
        XCUIDevice.shared.orientation = .landscapeRight
        for (key, _, value) in choices {
            let scroll = app.scrollViews["detail-scroll"]
            revealDeviceCheckControl(app.buttons["game-setting-" + key], in: scroll)
            XCTAssertEqual(app.buttons["game-setting-" + key].value as? String, value)
            revealDeviceCheckControl(app.buttons["game-reset-" + key], in: scroll)
            try tap("game-reset-" + key)
            XCTAssertEqual(app.staticTexts["game-override-" + key].label, "Using default")
            XCTAssertFalse(app.buttons["game-reset-" + key].exists)
        }
        let expected = ["DM_FPS_TARGET": "0", "DM_RENDER_SCALE": "1.0", "DM_SCREEN_MODE": "fit",
                        "DM_HAPTICS": "on", "DM_PIPELINE_MODE": "exact"]
        app.terminate(); app.launch()
        try openSettingsDetail()
        for (key, value) in expected {
            revealDeviceCheckControl(app.buttons["game-setting-" + key], in: app.scrollViews["detail-scroll"])
            XCTAssertEqual(app.buttons["game-setting-" + key].value as? String, value)
        }
    }

    func testGameSettingsShareMenuValues() throws {
        app.launchArguments += ["-SatchelPreviewSessionTests", "YES"]
        app.launch()
        try openSettingsDetail()
        try chooseGameSetting("DM_SCREEN_MODE", "Stretch", value: "stretch")
        try chooseGameSetting("DM_HAPTICS", "Off", value: "off")
        try tap("close-game-detail")
        try tap("play-focused")
        try button("player-play").tap()
        try tap("game.pause")
        XCTAssertTrue(app.buttons["Stretch: Fills the screen"].isSelected)
        try tap("Fit: 16:9, whole picture", scroll: app.scrollViews.containing(.button, identifier: "Fit: 16:9, whole picture").firstMatch)
        let haptics = app.switches["Haptics"]
        revealDeviceCheckControl(haptics, in: app.scrollViews.containing(.switch, identifier: "Haptics").firstMatch)
        XCTAssertEqual(haptics.value as? String, "0", "The menu reads the game page's haptics choice")
        haptics.tap()
        XCTAssertEqual(haptics.value as? String, "1")
        try tap("menu-library")
        try openSettingsDetail()
        revealDeviceCheckControl(app.buttons["game-setting-DM_SCREEN_MODE"], in: app.scrollViews["detail-scroll"])
        XCTAssertEqual(app.buttons["game-setting-DM_SCREEN_MODE"].value as? String, "fit")
        XCTAssertEqual(app.buttons["game-setting-DM_HAPTICS"].value as? String, "on")
        try chooseGameSetting("DM_SCREEN_MODE", "Stretch", value: "stretch")
        try tap("close-game-detail")
        try tap("play-focused")
        XCTAssertTrue(app.buttons["Stretch: Fills the screen"].isSelected, "Resuming a paused session reloads the shared title settings")
        try tap("menu-quit")
        try tap("Quit game")
        try openSettingsDetail()
        for key in ["DM_SCREEN_MODE", "DM_HAPTICS"] {
            revealDeviceCheckControl(app.buttons["game-reset-" + key], in: app.scrollViews["detail-scroll"])
            try tap("game-reset-" + key)
        }
    }

    private func openSaveDetail(running: Bool = false) throws {
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "synthetic game with save data")
        try tap("open-focused")
        if running {
            let control = app.buttons["import-save"]
            XCTAssertTrue(control.waitForExistence(timeout: 8))
            revealControl(control, in: app.scrollViews["detail-scroll"])
            XCTAssertTrue(control.isHittable)
            XCTAssertFalse(control.isEnabled)
        } else {
            revealControl(app.buttons["import-save"], in: app.scrollViews["detail-scroll"])
            _ = try button("import-save")
        }
    }

    private func launchSaveFixture(provider: Bool = true) throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewSaveTests", "YES", "-SatchelPreviewSaveProvider", provider ? "YES" : "NO"]
        app.launch()
        try openSaveDetail()
    }

    private func saveState(_ expected: String) {
        let marker = app.staticTexts["fixture-save-state"]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            marker.exists && marker.label == expected
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 8), .completed,
                       "Expected synthetic save: \(expected); actual: \(marker.exists ? marker.label : "missing")")
    }

    private func saveMessage(_ expected: String) {
        let message = app.staticTexts["save-result"]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            message.exists && message.label == expected
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 8), .completed,
                       "Expected save result: \(expected); actual: \(message.exists ? message.label : "missing")")
    }

    func testSaveExportImportAndBackupRestore() throws {
        try launchSaveFixture()
        XCTAssertTrue(app.staticTexts["save-summary"].exists)
        saveState("payload=checkpoint-current backups=1 export=none")
        try tap("export-save", scroll: app.scrollViews["detail-scroll"])
        try tap("fixture-save-cancel")
        saveMessage("Export cancelled.")
        saveState("payload=checkpoint-current backups=1 export=none")
        try tap("export-save", scroll: app.scrollViews["detail-scroll"])
        try tap("fixture-save-export")
        saveMessage("Save shared.")
        saveState("payload=checkpoint-current backups=1 export=verified")
        try tap("import-save", scroll: app.scrollViews["detail-scroll"])
        try tap("fixture-save-import")
        saveMessage("Save restored.")
        saveState("payload=checkpoint-old backups=2 export=verified")
        // The most recent backup holds the pre-import checkpoint. Cancel first.
        for confirm in [false, true] {
            try tap("restore-save-backup", scroll: app.scrollViews["detail-scroll"])
            let restore = try button("save-backup-restore-0", scroll: app.scrollViews["detail-scroll"])
            XCTAssertTrue(app.staticTexts["save-backup-info-0"].exists, "Backup rows include their date and size")
            restore.tap()
            _ = try button("Restore backup")
            if confirm { try button("Restore backup").tap() }
            else {
                let dismiss = app.otherElements["PopoverDismissRegion"]
                XCTAssertTrue(dismiss.exists && dismiss.isEnabled && dismiss.isHittable)
                dismiss.tap()
                absent(app.buttons["Restore backup"])
                _ = try button("close-game-detail")
                saveState("payload=checkpoint-old backups=2 export=verified")
            }
        }
        saveState("payload=checkpoint-current backups=3 export=verified")
        try tap("import-save", scroll: app.scrollViews["detail-scroll"])
        try tap("fixture-save-import")
        saveMessage("Save restored.")
        saveState("payload=checkpoint-old backups=3 export=verified")
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        try tap("close-game-detail")
        try openSaveDetail()
        XCTAssertEqual(app.buttons["restore-save-backup"].label, "Backups (3)")
        saveState("payload=checkpoint-old backups=3 export=verified")
    }

    func testSaveImportRejectsWrongTitleAndCorruptArchive() throws {
        try launchSaveFixture()
        for (selection, message) in [
            ("fixture-save-wrong-title", "This save belongs to a different title or an unsupported archive version."),
            ("fixture-save-corrupt", "The ZIP is truncated or too large.")
        ] {
            try tap("import-save", scroll: app.scrollViews["detail-scroll"])
            try tap(selection)
            saveMessage(message)
            saveState("payload=checkpoint-current backups=1 export=none")
        }
    }

    func testSaveSystemPickerCancellation() throws {
        try launchSaveFixture(provider: false)
        try savePickerCycle(open: "import-save")
        try tap("export-save", scroll: app.scrollViews["detail-scroll"])
        let saveToFiles = app.cells["Save to Files"]
        XCTAssertTrue(saveToFiles.waitForExistence(timeout: 8), "Export opens the system share sheet")
        XCTAssertTrue(saveToFiles.isHittable)
        XCTAssertTrue(app.otherElements["LP.CaptionBar.BottomCaption"].label.hasPrefix("ZIP Archive"))
        let close = app.buttons["Close"].firstMatch
        XCTAssertTrue(close.exists && close.isHittable)
        close.tap()
        _ = try button("close-game-detail")
        saveState("payload=checkpoint-current backups=1 export=none")
    }

    func testSaveBackupOnQuit() throws {
        app.launchArguments += ["-SatchelPreviewSaveTests", "YES"]
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-quit")
        try tap("Quit game")
        _ = try button("import-homebrew")
        saveState("payload=checkpoint-current backups=2 export=none")
        try openSaveDetail()
        XCTAssertEqual(app.buttons["restore-save-backup"].label, "Backups (2)")
    }

    func testSaveBackupOnQuitAndPausedSaveControls() throws {
        app.launchArguments += ["-SatchelPreviewSaveTests", "YES", "-SatchelPreviewSaveProvider", "YES"]
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-library")
        try openSaveDetail(running: true)
        for id in ["export-save", "import-save", "restore-save-backup"] {
            XCTAssertTrue(app.buttons[id].exists)
            XCTAssertFalse(app.buttons[id].isEnabled, "A paused session still owns its save")
        }
        XCTAssertTrue(app.staticTexts["Quit this game to home before exporting or restoring. Pausing is not enough."].exists)
        backgroundAndReturn(app.buttons["close-game-detail"].firstMatch)
        saveState("payload=checkpoint-current backups=2 export=none")
        try tap("close-game-detail")
        try tap("play-focused")
        try tap("menu-quit")
        try tap("Quit game")
        _ = try button("import-homebrew")
        // An unchanged save must not gain another backup after quit.
        saveState("payload=checkpoint-current backups=2 export=none")
        try openSaveDetail()
        _ = try button("export-save", scroll: app.scrollViews["detail-scroll"])
        _ = try button("restore-save-backup", scroll: app.scrollViews["detail-scroll"])
    }

    func testControllerRumblePersistsPerGame() throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewSessionTests", "YES"]
        app.launch()
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "synthetic rumble setting")
        try tap("play-focused")
        let vibration = app.switches["player-haptics"]
        let column = app.scrollViews.containing(.switch, identifier: "player-haptics").firstMatch
        for _ in 0..<5 where !vibration.isHittable { column.swipeUp() }
        XCTAssertTrue(vibration.isEnabled && vibration.isHittable)
        let initial = try XCTUnwrap(vibration.value as? String)
        let changed = initial == "1" ? "0" : "1"
        vibration.tap()
        XCTAssertEqual(vibration.value as? String, changed)
        // Player settings are committed by Play. Use only the synthetic host.
        try button("player-play").tap()
        try tap("game.pause")
        try tap("menu-quit")
        try tap("Quit game")
        _ = try button("import-homebrew")
        app.terminate(); app.launch()
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "same synthetic title after process relaunch")
        try tap("play-focused")
        XCTAssertEqual(app.switches["player-haptics"].value as? String, changed)
        try tap("close-player-settings")
        app.scrollViews["collection-panel"].swipeRight()
        try focus(where: { self.app.buttons["play-focused"].exists && !self.app.buttons["open-focused"].exists },
                  reason: "second synthetic game's rumble setting")
        try tap("play-focused")
        XCTAssertEqual(app.switches["player-haptics"].value as? String, "1", "Changing one title must not change another")
    }

    private func startSessionFixture(plain: Bool = false) throws {
        app.terminate()
        app.launchArguments += ["-SatchelPreviewSessionTests", "YES", "-SatchelPlainNativeMenu", plain ? "YES" : "NO"]
        if plain { app.launchArguments[1] = "YES" }
        app.launch()
        _ = try button("import-homebrew")
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "synthetic imported and prepared game")
        _ = try button("play-focused")
        try tap("play-focused")
        // Session start is authorized only in the synthetic Simulator fixture.
        try button("player-play").tap()
        _ = try button("game.pause")
    }

    private func sessionState(_ value: String, exact: Bool = false) {
        let marker = app.staticTexts["fixture-session-state"]
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            marker.exists && (exact ? marker.label == value : marker.label.contains(value))
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 8), .completed,
                       "Expected synthetic host state: \(value); actual: \(marker.exists ? marker.label : "missing")")
    }

    func testQuitReturnsHomeAndCanLaunchAgain() throws {
        try startSessionFixture()
        for _ in 0..<2 {
            try tap("game.pause")
            _ = try button("menu-resume")
            for id in ["play-mode-performance", "play-mode-graphics", "menu-advanced"] {
                XCTAssertFalse(app.buttons[id].exists)
            }
            XCTAssertFalse(app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "reopen Satchel")).firstMatch.exists)
            try tap("menu-quit")
            try tap("Keep playing")
            _ = try button("menu-resume")
            try tap("menu-quit")
            try tap("Quit game")
            _ = try button("import-homebrew")
            assertHomeContent()
            XCTAssertEqual(app.state, .runningForeground)
            sessionState("started=0 paused=0 package=none")
            absent(app.buttons["menu-resume"])
            try tap("play-focused")
            try button("player-play").tap()
            _ = try button("game.pause")
        }
    }

    func testQuitThenStartDifferentPreparedGame() throws {
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-quit")
        try tap("Quit game")
        _ = try button("import-homebrew")
        assertHomeContent()
        XCTAssertEqual(app.state, .runningForeground)
        absent(app.buttons["menu-resume"])
        sessionState("started=0 paused=0 package=none events=start:LanternDrift-1.2.0,stop-request:LanternDrift-1.2.0,stop-complete:LanternDrift-1.2.0", exact: true)
        app.scrollViews["collection-panel"].swipeRight()
        try focus(where: { self.app.buttons["play-focused"].exists && !self.app.buttons["open-focused"].exists },
                  reason: "different prepared game after Quit")
        try tap("play-focused")
        try button("player-play").tap()
        _ = try button("game.pause")
        try tap("game.pause")
        _ = try button("menu-resume")
        try tap("menu-resume")
        _ = try button("game.pause")
        try tap("game.pause")
        try tap("menu-library")
        _ = try button("import-homebrew")
        sessionState("started=1 paused=1 package=Navigation-Orphan events=start:LanternDrift-1.2.0,stop-request:LanternDrift-1.2.0,stop-complete:LanternDrift-1.2.0,start:Navigation-Orphan", exact: true)
        XCTAssertEqual(app.state, .runningForeground)
    }

    func testPlainMenuQuitReturnsHome() throws {
        try startSessionFixture(plain: true)
        try tap("game.pause")
        let scroll = app.scrollViews.containing(.button, identifier: "menu.quit").firstMatch
        try tap("menu.quit", scroll: scroll)
        try tap("menu.cancel")
        try tap("menu.quit", scroll: scroll)
        try tap("menu.confirm-quit")
        _ = try button("import-homebrew")
        assertHomeContent()
        XCTAssertEqual(app.state, .runningForeground)
        sessionState("started=0 paused=0 package=none")
    }

    func testLibraryPausesAndResumes() throws {
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-library")
        _ = try button("import-homebrew")
        sessionState("started=1 paused=1 package=LanternDrift-1.2.0 events=start:LanternDrift-1.2.0", exact: true)
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        sessionState("started=1 paused=1 package=LanternDrift-1.2.0 events=start:LanternDrift-1.2.0", exact: true)
        try tap("play-focused")
        _ = try button("menu-resume")
        XCTAssertFalse(app.buttons["menu.resume"].exists, "Reopening must not fall back to the plain menu")
        try tap("menu-resume")
        _ = try button("game.pause")
        try tap("game.pause")
        try tap("menu-library")
        sessionState("started=1 paused=1 package=LanternDrift-1.2.0 events=start:LanternDrift-1.2.0", exact: true)
    }

    /// Menu > Library leaves a card on the home header; Play reads Resume for that game.
    func testPausedGameCardResumes() throws {
        app.launchArguments += ["-SatchelPreviewDLC", "included", "-SatchelPreviewKeys", "YES"]
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-library")
        _ = try button("import-homebrew")
        let card = try button("paused-game-card")
        XCTAssertTrue(card.label.contains("Resume"), card.label)
        XCTAssertTrue(try button("play-focused").label.contains("Resume"), "Play must read Resume for the paused game")
        backgroundAndReturn(app.buttons["paused-game-card"].firstMatch)
        try tap("paused-game-card")
        _ = try button("menu-resume")
        try tap("menu-resume")
        _ = try button("game.pause")
        try tap("game.pause")
        try tap("menu-library")
        _ = try button("paused-game-card")
        sessionState("started=1 paused=1 package=LanternDrift-1.2.0 events=start:LanternDrift-1.2.0", exact: true)
        // Prepare waits for the open game, and says why.
        try tap("workshop")
        let prepare = app.buttons["workshop-prepare"]
        XCTAssertTrue(prepare.waitForExistence(timeout: 8))
        XCTAssertFalse(prepare.isEnabled, "Prepare must not start beside a paused game")
        XCTAssertTrue(prepare.label.contains("Quit the game before preparing another."), prepare.label)
        // UIKit dismisses the toolbar menu when the app backgrounds. Reuse the
        // lifecycle route, retaining the paused session and requiring a usable home.
        backgroundAndReturn(app.buttons["import-homebrew"].firstMatch)
        absent(prepare)
        try tap("paused-game-card")
        try tap("menu-quit")
        try tap("Quit game")
        _ = try button("import-homebrew")
        sessionState("started=0 paused=0 package=none")
        absent(app.buttons["paused-game-card"])
        XCTAssertFalse(try button("play-focused").label.contains("Resume"))
        try workshop("workshop-prepare")
        try tap("prepare-base")
        try tap("Lantern Drift [Base]")
        _ = try button("prepare-start", scroll: app.scrollViews.containing(.button, identifier: "prepare-base").firstMatch)
        try tap("close-prepare")
        try tap("open-focused")
        _ = try button("warmup-start", scroll: app.scrollViews["detail-scroll"])
        XCTAssertFalse(app.staticTexts["warmup-start-reason"].exists,
                       "Quitting the session must also release the warm-up gate")
    }

    func testSwitchingGameStopsFirst() throws {
        try startSessionFixture()
        try tap("game.pause")
        try tap("menu-library")
        _ = try button("import-homebrew")
        sessionState("started=1 paused=1 package=LanternDrift-1.2.0 events=start:LanternDrift-1.2.0", exact: true)
        // The orphan is newer than the imported base, so it is before it in this fixture.
        app.scrollViews["collection-panel"].swipeRight()
        try focus(where: { self.app.buttons["play-focused"].exists && !self.app.buttons["open-focused"].exists },
                  reason: "different prepared game")
        try tap("play-focused")
        // Merely inspecting another game's settings must keep the old game paused.
        _ = try button("player-play")
        try button("player-play").tap()
        _ = try button("game.pause")
        try tap("game.pause")
        try tap("menu-library")
        sessionState("started=1 paused=1 package=Navigation-Orphan events=start:LanternDrift-1.2.0,stop-request:LanternDrift-1.2.0,stop-complete:LanternDrift-1.2.0,start:Navigation-Orphan", exact: true)
    }

    func testBonusAppsAndDLCStayNested() throws {
        try focus(where: { self.app.buttons["open-focused"].exists && self.app.buttons["play-focused"].exists },
                  reason: "game with synthetic bonus content")
        XCTAssertFalse(app.buttons["homebrew-Lantern DLC"].exists)
        XCTAssertFalse(app.buttons["homebrew-Lantern Sound Gallery"].exists)
        try tap("open-focused")
        let scroll = app.scrollViews["detail-scroll"]
        let bonus = app.staticTexts["Lantern Sound Gallery"]
        for _ in 0..<5 where !bonus.isHittable { scroll.swipeUp() }
        XCTAssertTrue(bonus.exists)
        XCTAssertTrue(bonus.isHittable)
        XCTAssertTrue(app.staticTexts["LOADED WITH THIS GAME"].exists)
        XCTAssertTrue(app.staticTexts["Bonus app on the same cartridge  ·  not playable in Satchel yet"].exists)
        let importedDLC = app.staticTexts.matching(NSPredicate(format: "label BEGINSWITH %@", "DLC ")).firstMatch
        for _ in 0..<5 where !importedDLC.isHittable { scroll.swipeUp() }
        XCTAssertTrue(importedDLC.isHittable)
        XCTAssertTrue(app.staticTexts["ADD-ON CONTENT"].exists)
        try tap("close-game-detail")
        try tap("play-focused")
        let dlc = app.staticTexts.matching(NSPredicate(format: "label BEGINSWITH %@", "DLC ")).firstMatch
        XCTAssertTrue(dlc.waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["LOADED WITH THIS GAME"].exists)
        try tap("close-player-settings")
        // Search the full shelf too: a lazily offscreen DLC tile must not escape this check.
        try tap("search-library")
        let search = app.textFields["library-search"]
        search.tap(); search.typeText("Lantern\n")
        XCTAssertFalse(app.buttons["homebrew-Lantern DLC"].exists)
        XCTAssertFalse(app.buttons["homebrew-Lantern Sound Gallery"].exists)
    }
#endif

    func testLandscapeLockThroughPortraitRequests() throws {
        for orientation in [UIDeviceOrientation.portrait, .portraitUpsideDown, .landscapeRight, .landscapeLeft] {
            XCUIDevice.shared.orientation = orientation
            _ = try button("import-homebrew")
            XCTAssertGreaterThan(app.windows.firstMatch.frame.width, app.windows.firstMatch.frame.height)
            try tap("open-settings")
            _ = try button("close-settings")
            XCTAssertGreaterThan(app.windows.firstMatch.frame.width, app.windows.firstMatch.frame.height)
            try tap("close-settings")
        }
    }


}
