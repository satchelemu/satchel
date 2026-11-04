import XCTest

final class RuntimeJourneyTests: XCTestCase {
    func testRuntimeCancelsWhenBackgrounded() throws {
        let environment = ProcessInfo.processInfo.environment
        guard let name = environment["SATCHEL_RUNTIME_PACKAGE_NAME"],
              let title = environment["SATCHEL_RUNTIME_TITLE_ID"] else {
            throw XCTSkip("Requires an explicitly selected imported package and previously imported console keys.")
        }
        continueAfterFailure = false
        let app = XCUIApplication(); app.launch()
        let search = app.textFields["library-search"]
        XCTAssertTrue(search.waitForExistence(timeout: 10))
        search.tap(); search.typeText(String(name.prefix(24)) + "\n")
        let tile = app.buttons["homebrew-" + name]
        XCTAssertTrue(tile.waitForExistence(timeout: 5)); tile.tap()
        let scroll = app.scrollViews["detail-scroll"]
        func reveal(_ element: XCUIElement) {
            for _ in 0..<8 where !element.isHittable { scroll.swipeUp() }
            XCTAssertTrue(element.isHittable)
        }
        let scan = app.buttons["find-package-programs"]
        XCTAssertTrue(scan.waitForExistence(timeout: 10)); reveal(scan); scan.tap()
        let check = app.buttons["check-title-" + title]
        XCTAssertTrue(check.waitForExistence(timeout: 30)); reveal(check); check.tap()
        XCTAssertTrue(app.buttons["Stop check"].waitForExistence(timeout: 5))
        let executing = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@",
            "Executing the original loader with verified game storage attached…"), object: app.staticTexts["runtime-status"])
        XCTAssertEqual(XCTWaiter.wait(for: [executing], timeout: 30), .completed,
                       "Cancel while the actual process is executing, after file verification")
        // The Duo's Home-button automation left this app foregrounded in the
        // device run. Switch to another app without modifying its settings.
        let settings = XCUIApplication(bundleIdentifier: "com.apple.Preferences")
        settings.activate()
        XCTAssertTrue(settings.wait(for: .runningForeground, timeout: 5))
        let backgrounded = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
            app.state == .runningBackground || app.state == .runningBackgroundSuspended
        }, object: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [backgrounded], timeout: 5), .completed,
                       "Satchel must actually background before testing cancellation; state \(app.state.rawValue)")
        app.activate()
        let status = app.staticTexts["runtime-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 10))
        let cancelled = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Runtime check cancelled."), object: status)
        XCTAssertEqual(XCTWaiter.wait(for: [cancelled], timeout: 10), .completed)
        XCTAssertFalse(app.staticTexts["runtime-outcome"].exists)
        XCTAssertFalse(app.buttons["Stop check"].exists)
        XCTAssertTrue(check.isEnabled)
    }

    func testImportedPackageExecutableCheck() throws {
        guard let name = ProcessInfo.processInfo.environment["SATCHEL_RUNTIME_PACKAGE_NAME"],
              let title = ProcessInfo.processInfo.environment["SATCHEL_RUNTIME_TITLE_ID"] else {
            throw XCTSkip("Requires an explicitly selected, previously imported package and title ID.")
        }
        continueAfterFailure = false
        let app = XCUIApplication(); app.launch()
        let search = app.textFields["library-search"]
        XCTAssertTrue(search.waitForExistence(timeout: 10))
        search.tap(); search.typeText(String(name.prefix(24)) + "\n")
        let tile = app.buttons["homebrew-" + name]
        XCTAssertTrue(tile.waitForExistence(timeout: 5)); tile.tap()
        let scroll = app.scrollViews["detail-scroll"]
        func reveal(_ element: XCUIElement) {
            for _ in 0..<8 where !element.isHittable { scroll.swipeUp() }
            XCTAssertTrue(element.isHittable)
        }
        if ProcessInfo.processInfo.environment["SATCHEL_RUNTIME_IMPORT_KEYS"] == "1" {
            let importKeys = app.buttons["import-console-keys"]
            XCTAssertTrue(importKeys.waitForExistence(timeout: 5)); reveal(importKeys); importKeys.tap()
            XCTAssertTrue(app.buttons["Browse"].waitForExistence(timeout: 5)); app.buttons["Browse"].tap()
            let cloud = app.cells["DOC.sidebar.item.iCloud Drive"]
            XCTAssertTrue(cloud.waitForExistence(timeout: 5)); cloud.tap()
            for folder in ["ROMs", "Switch"] {
                let cell = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@", folder + ",")).firstMatch
                XCTAssertTrue(cell.waitForExistence(timeout: 10)); cell.tap()
            }
            let keys = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@ OR label BEGINSWITH %@", "prod.keys,", "prod,")).firstMatch
            XCTAssertTrue(keys.waitForExistence(timeout: 10)); keys.tap()
        }
        let scan = app.buttons["find-package-programs"]
        XCTAssertTrue(scan.waitForExistence(timeout: 15)); reveal(scan); scan.tap()
        let check = app.buttons["check-title-" + title]
        XCTAssertTrue(check.waitForExistence(timeout: 30)); reveal(check)
        if ProcessInfo.processInfo.environment["SATCHEL_RUNTIME_REQUIRE_DISPLAY_SURFACE"] == "1" {
            let surface = app.descendants(matching: .any).matching(identifier: "runtime-game-surface").firstMatch
            XCTAssertTrue(surface.exists, "The detail must mount the actual display view before execution")
            XCTAssertGreaterThan(surface.frame.width, 0)
            XCTAssertGreaterThan(surface.frame.height, 0)
        }
        check.tap()
        let outcome = app.staticTexts["runtime-outcome"]
        XCTAssertTrue(outcome.waitForExistence(timeout: 120), "The actual package must decrypt, verify and reach the bounded CPU probe")
        reveal(outcome)
        if ProcessInfo.processInfo.environment["SATCHEL_RUNTIME_REQUIRE_DISPLAY_SURFACE"] == "1" {
            XCTAssertTrue(app.staticTexts["runtime-display-service"].exists,
                          "The real attached display must create the checked VI owner")
        }
        let count = app.staticTexts["runtime-instructions"]
        XCTAssertTrue(count.exists)
        XCTAssertFalse(count.label.hasPrefix("0 instructions"), "Original loader instructions must actually execute")
        let environment = ProcessInfo.processInfo.environment
        let minimumInstructions = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_INSTRUCTIONS"] ?? "1") ?? 1
        let instructionCount = UInt64(count.label.components(separatedBy: " instructions")[0].filter(\.isNumber)) ?? 0
        XCTAssertGreaterThanOrEqual(instructionCount, minimumInstructions)
        let queries = app.staticTexts["runtime-memory-queries"]
        XCTAssertTrue(queries.exists)
        let minimumQueries = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_MEMORY_QUERIES"] ?? "0") ?? 0
        let queryCount = UInt64(queries.label.components(separatedBy: " memory queries")[0].filter(\.isNumber)) ?? 0
        XCTAssertGreaterThanOrEqual(queryCount, minimumQueries)
        if let minimumIPC = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_IPC_COMPLETED"] ??
            environment["SATCHEL_RUNTIME_MINIMUM_IPC_CALLS"] ?? "") {
            let ipc = app.staticTexts["runtime-ipc"]
            XCTAssertTrue(ipc.exists)
            let calls = UInt64(ipc.label.components(separatedBy: " service requests")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(calls, minimumIPC)
        }
        if let minimumRequests = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_IPC_REQUESTS"] ?? "") {
            let attempts = app.staticTexts["runtime-ipc-attempts"]
            XCTAssertTrue(attempts.exists)
            let count = UInt64(attempts.label.components(separatedBy: " service requests attempted")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumRequests)
        }
        if let minimumCloses = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_SESSION_CLOSES"] ?? "") {
            let closes = app.staticTexts["runtime-session-closes"]
            XCTAssertTrue(closes.exists)
            let count = UInt64(closes.label.components(separatedBy: " server sessions closed")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumCloses)
        }
        XCTAssertEqual(app.staticTexts["runtime-vulkan-compiled"].label, "On-device shader → Vulkan passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-shader-probe"].label, "Shader compiler self-test passed (synthetic)")
        if let minimumThreads = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_THREADS"] ?? "") {
            let threads = app.staticTexts["runtime-threads"]
            XCTAssertTrue(threads.exists)
            let count = UInt64(threads.label.components(separatedBy: " guest thread objects")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumThreads)
        }
        if let minimumExecuted = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_EXECUTED_THREADS"] ?? "") {
            let threads = app.staticTexts["runtime-threads"]
            XCTAssertTrue(threads.exists)
            let executed = threads.label.components(separatedBy: " • ").last ?? ""
            let count = UInt64(executed.components(separatedBy: " have executed")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumExecuted)
        }
        if let minimumWaits = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_MUTEX_WAITS"] ?? "") {
            let mutexes = app.staticTexts["runtime-mutexes"]
            XCTAssertTrue(mutexes.exists)
            let count = UInt64(mutexes.label.components(separatedBy: " mutex waits")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumWaits)
        }
        if let minimumHandoffs = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_MUTEX_HANDOFFS"] ?? "") {
            let mutexes = app.staticTexts["runtime-mutexes"]
            XCTAssertTrue(mutexes.exists)
            let parts = mutexes.label.components(separatedBy: " • ")
            XCTAssertGreaterThanOrEqual(parts.count, 2)
            let count = UInt64(parts[1].components(separatedBy: " ownership handoffs")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumHandoffs)
        }
        if let minimumMessages = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_APPLET_MESSAGES"] ?? "") {
            let messages = app.staticTexts["runtime-applet-messages"]
            XCTAssertTrue(messages.exists)
            let count = UInt64(messages.label.components(separatedBy: " application messages received")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumMessages)
        }
        XCTAssertEqual(app.staticTexts["runtime-vulkan-probe"].label, "Vulkan self-test passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-lifecycle"].label, "Interpreted Vulkan lifecycle passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-buffers"].label, "Vulkan buffer storage passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-transfers"].label, "Vulkan GPU transfers passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-pools"].label, "Vulkan guest-memory pools passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-pool-dispatch"].label, "Interpreted Vulkan pools passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-transfer-dispatch"].label, "Interpreted Vulkan transfers passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-compute-dispatch"].label, "Interpreted Vulkan compute passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-raster-dispatch"].label, "Interpreted Vulkan drawing passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-captured"].label, "Captured shader → Vulkan passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-provoking"].label, "Vulkan flat-shading modes passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-vulkan-texture-frame"].label, "Vulkan texture → frame passed (synthetic)")
        XCTAssertEqual(app.staticTexts["runtime-modules"].label, "4 NSO modules decoded • 12 segment hashes verified")
        XCTAssertEqual(app.staticTexts["runtime-application-control"].label, "Application control metadata verified and attached")
        XCTAssertEqual(app.staticTexts["runtime-clock-source"].label, "Local clock initialized")
        if let minimumAttributes = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_ATTRIBUTES_SERVICED"] ?? "") {
            let attributes = app.staticTexts["runtime-memory-attributes"]
            XCTAssertTrue(attributes.exists)
            let count = UInt64(attributes.label.components(separatedBy: " memory-attribute requests serviced")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumAttributes)
        }
        if let minimumLocked = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_PERMISSION_LOCKED_BYTES"] ?? "") {
            let bytes = app.staticTexts["runtime-memory-locked-bytes"]
            XCTAssertTrue(bytes.exists)
            let count = UInt64(bytes.label.components(separatedBy: " bytes with locked permissions")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumLocked)
        }
        if let minimumPermissions = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_PERMISSIONS_SERVICED"] ?? "") {
            let permissions = app.staticTexts["runtime-memory-permissions"]
            XCTAssertTrue(permissions.exists)
            let count = UInt64(permissions.label.components(separatedBy: " memory-permission requests serviced")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumPermissions)
        }
        if let minimumBytes = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_PERMISSION_BYTES"] ?? "") {
            let bytes = app.staticTexts["runtime-memory-permission-bytes"]
            XCTAssertTrue(bytes.exists)
            let count = UInt64(bytes.label.components(separatedBy: " bytes cumulatively reprotected")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumBytes)
        }
        if let minimumMappings = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_CLOCK_MAPPINGS"] ?? "") {
            let mappings = app.staticTexts["runtime-clock-mappings"]
            XCTAssertTrue(mappings.exists)
            let count = UInt64(mappings.label.components(separatedBy: " shared clock mappings")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumMappings)
        }
        if let minimumInitialized = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_PARENTAL_INITIALIZED"] ?? "") {
            let initialized = app.staticTexts["runtime-parental-initialized"]
            XCTAssertTrue(initialized.exists)
            let count = UInt64(initialized.label.components(separatedBy: " emulated parental-control")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(count, minimumInitialized)
        }
        if let minimum = UInt64(environment["SATCHEL_RUNTIME_MINIMUM_ROMFS_FILES"] ?? "") {
            let assets = app.staticTexts["runtime-romfs"]
            XCTAssertTrue(assets.exists, "The imported archive must provide verified RomFS access on the phone")
            let fileCount = UInt64(assets.label.components(separatedBy: " RomFS files")[0].filter(\.isNumber)) ?? 0
            XCTAssertGreaterThanOrEqual(fileCount, minimum)
            XCTAssertFalse(assets.label.contains("• 0 asset bytes"))
        }
        XCTAssertFalse(app.buttons["play-homebrew"].exists)
        reveal(app.staticTexts["runtime-vulkan-probe"])
        let screenshot = XCTAttachment(screenshot: app.windows.firstMatch.screenshot())
        screenshot.name = "Original imported executable: bounded CPU diagnostic, not gameplay"
        screenshot.lifetime = .keepAlways; add(screenshot)
    }
}
