import XCTest

final class LayoutJourneyTests: XCTestCase {
    func testNativeImportTouchTarget() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        let button = app.buttons["import-homebrew"]
        XCTAssertTrue(button.waitForExistence(timeout: 10))
        for offset in [CGVector(dx: -21.5, dy: 0), CGVector(dx: 21.5, dy: 0), CGVector(dx: 0, dy: -21.5), CGVector(dx: 0, dy: 21.5)] {
            button.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).withOffset(offset).tap()
            let cancels = app.buttons.matching(identifier: "Cancel")
            let presented = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                cancels.allElementsBoundByIndex.contains { $0.isHittable }
            }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [presented], timeout: 10), .completed, "Import must respond across a 44-point touch target")
            // Duo exposes both the horizontal and vertical bar representations;
            // interact with the currently visible Cancel control.
            let railCancel = app.toolbars.buttons["Cancel"].firstMatch
            let cancel = try XCTUnwrap(railCancel.exists && railCancel.isHittable
                                      ? railCancel : cancels.allElementsBoundByIndex.last { $0.isHittable })
            cancel.tap()
            let dismissed = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                !cancels.allElementsBoundByIndex.contains { $0.isHittable }
            }, object: nil)
            XCTAssertEqual(XCTWaiter.wait(for: [dismissed], timeout: 5), .completed, "Wait for Files to dismiss before the next edge tap")
            XCTAssertTrue(button.isHittable)
        }
    }

    func testLibraryAndDetailSpacing() {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launch()
        let importButton = app.buttons["import-homebrew"]
        XCTAssertTrue(importButton.waitForExistence(timeout: 10))
        let window = app.windows.containing(.button, identifier: "import-homebrew").firstMatch
        let collection = app.otherElements["collection-panel"]
        XCTAssertTrue(collection.exists)
        XCTAssertGreaterThan(window.frame.width, window.frame.height)
        XCTAssertGreaterThanOrEqual(importButton.frame.width, 44)
        XCTAssertGreaterThanOrEqual(importButton.frame.height, 44)
        XCTAssertTrue(importButton.frame.minX >= collection.frame.maxX || importButton.frame.maxX <= collection.frame.minX,
                      "Native actions should occupy the side rail, not cover library content")
        if window.frame.width < 800 {
            XCTAssertFalse(app.scrollViews["launch-panel"].exists, "The cover display should show the collection without the large import panel")
            XCTAssertGreaterThan(collection.frame.width, window.frame.width * 0.7)
        } else {
            XCTAssertTrue(app.scrollViews["launch-panel"].exists)
        }
        attach(app, "Adaptive library")
        let search = app.textFields["library-search"]
        search.tap(); search.typeText("hello-horizon\n")
        let sample = app.buttons["homebrew-hello-horizon"]
        XCTAssertTrue(sample.waitForExistence(timeout: 5))
        sample.tap()
        let play = app.buttons["play-homebrew"]
        XCTAssertTrue(play.waitForExistence(timeout: 5))
        let scroll = app.scrollViews["detail-scroll"]
        for _ in 0..<5 where !play.isHittable { scroll.swipeUp() }
        XCTAssertTrue(play.isHittable)
        let card = app.otherElements["detail-actions-card"]
        let introduction = app.staticTexts["detail-introduction"]
        assertHorizontalInsets(introduction, inside: card)
        assertHorizontalInsets(play, inside: card)
        XCTAssertGreaterThanOrEqual(play.frame.height, 44)
        attach(app, "Padded file detail")
        let copy = app.staticTexts["compatibility-copy"]
        for _ in 0..<5 where !copy.isHittable { scroll.swipeUp() }
        XCTAssertTrue(copy.isHittable, "All compatibility text should be reachable")
        assertHorizontalInsets(copy, inside: app.otherElements["compatibility-card"])
        attach(app, "Readable compatibility card")
        app.buttons["Done"].tap()
        XCTAssertTrue(importButton.waitForExistence(timeout: 5))
        XCTAssertEqual(search.value as? String, "hello-horizon", "Dismissal should preserve the library search")
    }

    private func assertHorizontalInsets(_ content: XCUIElement, inside card: XCUIElement) {
        XCTAssertTrue(card.exists)
        XCTAssertTrue(content.exists)
        XCTAssertGreaterThanOrEqual(content.frame.minX - card.frame.minX, 16, "Text and controls need breathing room on the leading edge")
        XCTAssertGreaterThanOrEqual(card.frame.maxX - content.frame.maxX, 16, "Text and controls need breathing room on the trailing edge")
    }

    private func attach(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
