import XCTest

final class PackageJourneyTests: XCTestCase {
    // Explicit fixture selection keeps account-specific game names out of tests.
    // Run only with a known, user-provided iCloud package present on the device.
    func testCloudPackageImportAndPersistence() throws {
        guard let name = ProcessInfo.processInfo.environment["SATCHEL_TEST_PACKAGE_NAME"], !name.isEmpty else {
            throw XCTSkip("Set SATCHEL_TEST_PACKAGE_NAME to an available iCloud package filename.")
        }
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.buttons["import-homebrew"].waitForExistence(timeout: 10))
        app.buttons["import-homebrew"].tap()
        XCTAssertTrue(app.buttons["Browse"].waitForExistence(timeout: 5))
        app.buttons["Browse"].tap()
        let cloud = app.cells["DOC.sidebar.item.iCloud Drive"]
        XCTAssertTrue(cloud.waitForExistence(timeout: 5))
        cloud.tap()
        let stem = (name as NSString).deletingPathExtension
        if let path = ProcessInfo.processInfo.environment["SATCHEL_TEST_PACKAGE_FOLDER"] {
            for folder in path.split(separator: "/") {
                let cell = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@", String(folder) + ",")).firstMatch
                XCTAssertTrue(cell.waitForExistence(timeout: 10))
                for _ in 0..<4 where !cell.isHittable { app.collectionViews["File View"].firstMatch.swipeUp() }
                cell.tap()
            }
        } else {
            let search = app.searchFields["Search"].firstMatch
            XCTAssertTrue(search.waitForExistence(timeout: 5))
            search.tap(); search.typeText(String(stem.prefix(24)) + "\n")
        }
        let file = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@ OR label BEGINSWITH %@", name + ",", stem + ",")).firstMatch
        XCTAssertTrue(file.waitForExistence(timeout: 30), "Selected package must be visible in the real iCloud Files browser")
        file.tap()
        XCTAssertTrue(app.staticTexts["Import complete"].waitForExistence(timeout: 300), "Import must complete, not merely dismiss the picker")
        XCTAssertTrue(app.staticTexts["Not playable yet"].exists)
        XCTAssertFalse(app.buttons["play-homebrew"].exists)
        XCTAssertFalse(app.buttons["run-homebrew"].exists)
        let screenshot = XCTAttachment(screenshot: app.windows.firstMatch.screenshot())
        screenshot.name = "Phone-only package import: storage, not gameplay"
        screenshot.lifetime = .keepAlways
        add(screenshot)
        app.buttons["Done"].tap()
        app.terminate(); app.launch()
        let librarySearch = app.textFields["library-search"]
        XCTAssertTrue(librarySearch.waitForExistence(timeout: 10))
        librarySearch.tap(); librarySearch.typeText(String(stem.prefix(30)) + "\n")
        let tile = app.buttons["homebrew-" + String(stem.prefix(100))]
        XCTAssertTrue(tile.waitForExistence(timeout: 5), "Package must survive a cold app launch")
        tile.tap()
        XCTAssertTrue(app.staticTexts["Import complete"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Not playable yet"].exists)
        XCTAssertFalse(app.buttons["play-homebrew"].exists)
    }
}
