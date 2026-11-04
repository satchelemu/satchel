import XCTest

final class PlayerJourneyTests: XCTestCase {
    func testDeviceValidationAndPlayerControls() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        for _ in 0..<4 where !app.buttons["Open interpreter demo"].isHittable { app.scrollViews["launch-panel"].swipeUp() }
        app.buttons["Open interpreter demo"].tap()
        XCTAssertTrue(app.staticTexts["Device check passed. Your turn."].waitForExistence(timeout: 25))

        let pause = app.buttons["pause"]
        pause.tap()
        XCTAssertTrue(app.staticTexts["PAUSED"].waitForExistence(timeout: 3))
        pause.tap()
        XCTAssertFalse(app.staticTexts["PAUSED"].exists)

        let field = app.otherElements["playfield"].firstMatch
        XCTAssertTrue(field.exists)
        let start = field.coordinate(withNormalizedOffset: CGVector(dx: 0.2, dy: 0.8))
        let end = field.coordinate(withNormalizedOffset: CGVector(dx: 0.8, dy: 0.8))
        start.press(forDuration: 0.05, thenDragTo: end)
        XCTAssertTrue(app.buttons["Manual"].waitForExistence(timeout: 3))
        let moved = NSPredicate { _, _ in
            guard let value = field.value as? String,
                  let number = value.split(separator: " ").dropFirst().first,
                  let position = Int(number.replacingOccurrences(of: ",", with: "")) else { return false }
            return position >= 120 && position <= 135
        }
        expectation(for: moved, evaluatedWith: field)
        waitForExpectations(timeout: 3)

        app.buttons["Restart game"].tap()
        // Read immediately: this live game can score again before the first
        // one-second poll used by waitForExistence.
        XCTAssertTrue(app.staticTexts["0000"].exists, "Reset must immediately zero the score")
        app.buttons["Diagnostics"].tap()
        XCTAssertTrue(app.navigationBars["Lab notes"].waitForExistence(timeout: 3))
        XCTAssertTrue(app.staticTexts["Passed · 600 frames"].exists)
        app.buttons["Done"].tap()
        XCTAssertTrue(app.buttons["pause"].exists)
    }
}
