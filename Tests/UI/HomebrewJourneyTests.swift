import XCTest

final class HomebrewJourneyTests: XCTestCase {
    func testImportRunPersistAndPicker() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.buttons["import-homebrew"].waitForExistence(timeout: 5))
        for _ in 0..<4 where !app.buttons["Load Horizon sample"].isHittable { app.scrollViews["launch-panel"].swipeUp() }
        app.buttons["Load Horizon sample"].tap()
        XCTAssertTrue(app.buttons["run-homebrew"].waitForExistence(timeout: 5))
        app.buttons["run-homebrew"].tap()
        XCTAssertTrue(app.staticTexts["Completed"].waitForExistence(timeout: 5))
        let output = app.staticTexts["homebrew-output"]
        XCTAssertTrue(output.label.contains("Hello from Switch homebrew!"))
        for _ in 0..<4 where !output.isHittable { app.scrollViews["detail-scroll"].swipeUp() }
        XCTAssertTrue(output.isHittable, "Diagnostic output must be readable by scrolling")
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Completed NRO on iPhone"
        screenshot.lifetime = .keepAlways
        add(screenshot)
        app.buttons["Done"].tap()
        XCTAssertTrue(app.buttons["homebrew-hello-horizon"].exists)
        app.terminate()
        app.launch()
        XCTAssertTrue(app.buttons["homebrew-hello-horizon"].waitForExistence(timeout: 5))
        app.buttons["import-homebrew"].tap()
        // This verifies the actual system browser is presented, not a custom
        // imitation. Cloud-provider availability depends on this device's account.
        XCTAssertTrue(app.buttons["Cancel"].waitForExistence(timeout: 5))
        app.buttons["Cancel"].tap()
        XCTAssertTrue(app.buttons["import-homebrew"].exists)
    }

    func testCloudRoundTrip() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        for _ in 0..<4 where !app.buttons["Save sample to Files…"].isHittable { app.scrollViews["launch-panel"].swipeUp() }
        app.buttons["Save sample to Files…"].tap()
        XCTAssertTrue(app.buttons["Save"].waitForExistence(timeout: 5))
        let cloud = app.descendants(matching: .any).matching(NSPredicate(format: "label == %@", "iCloud Drive")).firstMatch
        for _ in 0..<4 where !cloud.exists {
            if app.buttons["Browse"].exists { app.buttons["Browse"].tap() }
            else if app.buttons["BackButton"].exists { app.buttons["BackButton"].tap() }
            else if app.buttons["Back"].exists { app.buttons["Back"].tap() }
        }
        XCTAssertTrue(cloud.waitForExistence(timeout: 5), "iCloud Drive must be available for the cloud round trip")
        let cloudRoot = app.otherElements.matching(NSPredicate(format: "identifier BEGINSWITH %@", "DOC.browsingRoot Source: com.apple.CloudDocs.iCloudDriveFileProvider/")).firstMatch
        if app.cells["DOC.sidebar.item.iCloud Drive"].exists { app.cells["DOC.sidebar.item.iCloud Drive"].tap() }
        else if !cloudRoot.exists { cloud.tap() }
        XCTAssertTrue(cloudRoot.waitForExistence(timeout: 5))
        // A unique fixture name avoids replacing any existing cloud document.
        let cloudName = "satchel-" + String(UUID().uuidString.prefix(8)).lowercased()
        let filename = app.textFields["DOCPicker.filenameTextField"]
        XCTAssertTrue(filename.exists)
        filename.tap()
        let currentName = filename.value as? String ?? ""
        filename.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: currentName.count) + cloudName)
        app.buttons["Save"].tap()
        XCTAssertTrue(app.staticTexts["Sample saved. Import it from Files to try the round trip."].waitForExistence(timeout: 10))
        for _ in 0..<4 where !app.buttons["import-homebrew"].isHittable { app.scrollViews["launch-panel"].swipeDown() }
        app.buttons["import-homebrew"].tap()
        XCTAssertTrue(app.buttons["Browse"].waitForExistence(timeout: 5))
        app.buttons["Browse"].tap()
        XCTAssertTrue(cloud.waitForExistence(timeout: 5))
        if app.cells["DOC.sidebar.item.iCloud Drive"].exists { app.cells["DOC.sidebar.item.iCloud Drive"].tap() }
        else if !cloudRoot.exists { cloud.tap() }
        XCTAssertTrue(cloudRoot.waitForExistence(timeout: 5))
        // Match the provider's file cell, not the library row behind the sheet.
        let search = app.searchFields["Search"].firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5)); search.tap(); search.typeText(cloudName)
        let sample = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@ OR label BEGINSWITH %@", cloudName + ".nro,", cloudName + ",")).firstMatch
        XCTAssertTrue(sample.waitForExistence(timeout: 10))
        sample.tap()
        XCTAssertTrue(app.buttons["run-homebrew"].waitForExistence(timeout: 10))
        app.buttons["run-homebrew"].tap()
        XCTAssertTrue(app.staticTexts["Completed"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["homebrew-output"].label.contains("Hello from Switch homebrew!"))
    }

    // Device fixture: official nx-hbmenu v3.6.1 copied to Documents/hbmenu.nro.
    // This asserts the known compatibility boundary, not successful menu startup.
    func testExistingHomebrewDiagnostic() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        for _ in 0..<4 where !app.buttons["import-homebrew"].isHittable { app.scrollViews["launch-panel"].swipeDown() }
        app.buttons["import-homebrew"].tap()
        XCTAssertTrue(app.buttons["Browse"].waitForExistence(timeout: 5))
        app.buttons["Browse"].tap()
        let local = app.cells["DOC.sidebar.item.On My iPhone"]
        XCTAssertTrue(local.waitForExistence(timeout: 5))
        local.tap()
        let folder = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@", "Satchel,")).firstMatch
        XCTAssertTrue(folder.waitForExistence(timeout: 5))
        folder.tap()
        let search = app.searchFields["Search"].firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5)); search.tap(); search.typeText("hbmenu")
        let file = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@ OR label BEGINSWITH %@", "hbmenu.nro,", "hbmenu,")).firstMatch
        XCTAssertTrue(file.waitForExistence(timeout: 5))
        file.tap()
        XCTAssertTrue(app.buttons["run-homebrew"].waitForExistence(timeout: 10))
        app.buttons["run-homebrew"].tap()
        let outcome = app.staticTexts["run-outcome"]
        XCTAssertTrue(outcome.waitForExistence(timeout: 5))
        XCTAssertEqual(outcome.label, "Unsupported ARM64 instruction: 0x3cc0827f at PC 0x2fb38")
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Existing homebrew compatibility boundary"
        screenshot.lifetime = .keepAlways
        add(screenshot)
    }

}
