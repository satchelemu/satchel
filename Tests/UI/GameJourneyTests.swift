import XCTest

final class GameJourneyTests: XCTestCase {
    func testDownloadedGamePlaysOnDevice() {
        continueAfterFailure = false
        let app = XCUIApplication()
        XCUIDevice.shared.orientation = .portrait
        app.launch()
        XCTAssertTrue(app.buttons["import-homebrew"].waitForExistence(timeout: 5))
        let libraryWindow = app.windows.containing(.button, identifier: "import-homebrew").firstMatch
        XCTAssertGreaterThan(libraryWindow.frame.width, libraryWindow.frame.height, "Launch must already be landscape")
        let libraryRow = app.buttons["homebrew-tictactoe"]
        XCTAssertTrue(libraryRow.exists)
        let launchPanel = app.scrollViews["launch-panel"]
        let collection = app.otherElements["collection-panel"]
        XCTAssertTrue(launchPanel.exists)
        XCTAssertTrue(collection.exists)
        XCTAssertLessThanOrEqual(launchPanel.frame.maxX, collection.frame.minX, "Browsing and collection must sit side by side")
        XCTAssertGreaterThan(collection.frame.maxX - launchPanel.frame.minX, libraryWindow.frame.width * 0.85, "The two panels must use the open display width")
        XCTAssertGreaterThan(libraryRow.frame.width, 175, "Game tiles must be large enough to browse")
        let searchLibrary = app.textFields["library-search"]
        searchLibrary.tap(); searchLibrary.typeText("no-matching-homebrew")
        XCTAssertTrue(app.staticTexts["No matching files"].waitForExistence(timeout: 5))
        app.buttons["Clear search"].tap()
        XCTAssertTrue(libraryRow.waitForExistence(timeout: 5))
        searchLibrary.tap(); searchLibrary.typeText("tictactoe")
        XCTAssertTrue(libraryRow.exists)
        XCTAssertFalse(app.buttons["homebrew-hbmenu"].exists)
        app.buttons["Clear search"].tap()
        app.buttons["Sort library"].tap()
        app.buttons["Name A–Z"].tap()
        let launchScreenshot = XCTAttachment(screenshot: app.screenshot())
        launchScreenshot.name = "Landscape launch and console library"
        launchScreenshot.lifetime = .keepAlways
        add(launchScreenshot)
        app.buttons["import-homebrew"].tap()
        XCTAssertTrue(app.buttons["Browse"].waitForExistence(timeout: 5))
        app.buttons["Browse"].tap()
        let local = app.cells["DOC.sidebar.item.On My iPhone"]
        XCTAssertTrue(local.waitForExistence(timeout: 5)); local.tap()
        let folder = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@", "Satchel,")).firstMatch
        XCTAssertTrue(folder.waitForExistence(timeout: 5)); folder.tap()
        let search = app.searchFields["Search"].firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5)); search.tap(); search.typeText("tictactoe")
        let file = app.cells.matching(NSPredicate(format: "label BEGINSWITH %@ OR label BEGINSWITH %@", "tictactoe.nro,", "tictactoe,")).firstMatch
        XCTAssertTrue(file.waitForExistence(timeout: 5)); file.tap()
        XCTAssertTrue(app.buttons["play-homebrew"].waitForExistence(timeout: 10))
        let detailWindow = app.windows.containing(.button, identifier: "play-homebrew").firstMatch
        XCTAssertEqual(detailWindow.frame.width, libraryWindow.frame.width, accuracy: 1, "Details must use the whole window")
        XCTAssertGreaterThan(detailWindow.frame.width, detailWindow.frame.height)
        let detailScreenshot = XCTAttachment(screenshot: app.screenshot())
        detailScreenshot.name = "Console game detail"
        detailScreenshot.lifetime = .keepAlways
        add(detailScreenshot)
        app.buttons["play-homebrew"].tap()
        let display = app.images["game-display"]
        func waitForFrame(_ checksum: String) {
            let expectation = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == true AND value == %@", checksum), object: display)
            XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 60), .completed, "Expected original game's reference framebuffer \(checksum); status: \(app.staticTexts["game-status"].label)")
        }
        waitForFrame("dea936ab7b2addfe") // empty board, center selected
        let gameWindow = app.windows.containing(.image, identifier: "game-display").firstMatch
        XCTAssertGreaterThan(gameWindow.frame.width, gameWindow.frame.height, "Gameplay window must be landscape")
        XCTAssertEqual(display.frame.width, gameWindow.frame.width, accuracy: 1, "Game must fill the available width")
        XCUIDevice.shared.orientation = .portrait
        Thread.sleep(forTimeInterval: 1)
        XCTAssertGreaterThan(gameWindow.frame.width, gameWindow.frame.height, "Gameplay must stay landscape when portrait is requested")
        app.buttons["game-a"].tap()
        waitForFrame("feda1d38a31da472") // player1 center
        app.buttons["game-right"].tap()
        waitForFrame("f96ae1ade4055426") // cursor moved to right-center
        app.buttons["game-a"].tap()
        waitForFrame("fb62bc9c4a553f88") // player2 right-center
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Original downloaded game with both players on iPhone"
        screenshot.lifetime = .keepAlways
        add(screenshot)
        app.buttons["game-pause"].tap()
        XCTAssertTrue(app.buttons["game-pause"].label.contains("Resume"))
        app.buttons["game-pause"].tap()
        app.buttons["game-diagnostics"].tap()
        let diagnosticsStatus = app.staticTexts["player-diagnostics-status"]
        XCTAssertTrue(diagnosticsStatus.waitForExistence(timeout: 5))
        let diagnosticsCard = app.otherElements["player-diagnostics-card"]
        XCTAssertGreaterThanOrEqual(diagnosticsStatus.frame.minX - diagnosticsCard.frame.minX, 16)
        XCTAssertGreaterThanOrEqual(diagnosticsCard.frame.maxX - diagnosticsStatus.frame.maxX, 16)
        let diagnosticsScreenshot = XCTAttachment(screenshot: app.screenshot())
        diagnosticsScreenshot.name = "Padded player diagnostics"
        diagnosticsScreenshot.lifetime = .keepAlways
        add(diagnosticsScreenshot)
        app.buttons["diagnostics-done"].tap()
        XCTAssertTrue(app.buttons["game-stop"].waitForExistence(timeout: 5))
        app.buttons["game-stop"].tap()
        XCTAssertTrue(app.staticTexts["Stopped by you"].waitForExistence(timeout: 10))
        app.buttons["game-done"].tap()
        XCTAssertTrue(app.buttons["play-homebrew"].waitForExistence(timeout: 5))
        XCTAssertGreaterThan(detailWindow.frame.width, detailWindow.frame.height, "Leaving a game must retain landscape")
        app.buttons["Done"].tap()
        XCTAssertTrue(app.buttons["import-homebrew"].waitForExistence(timeout: 5))
        XCTAssertGreaterThan(libraryWindow.frame.width, libraryWindow.frame.height, "Returning to library must retain landscape")
    }
}
