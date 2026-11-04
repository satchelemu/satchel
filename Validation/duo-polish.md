# Duo UI polish: build 6

Validated 2026-10-31. Version 0.3.0 (6) is installed on the maintainer's iPhone Duo,
iOS 27.1. The app remains an experimental interpreter with limited compatibility.

Implemented: removed the home-page footer; restored native side toolbars;
added a compact collection layout for the cover display; improved title wrapping,
contrast and button spacing; replaced detail and diagnostic lists with explicitly
padded cards. Library adaptation depends on width so opening the keyboard does
not switch layouts. Landscape locking and the interpreter are unchanged.
No dependencies were added. Cartridge artwork remains procedural placeholder art.

## Validation

- Physical inner display: four journeys passed in DeviceVerified.xcresult,
  covering actual Files import, persistence, search/sort, original TicTacToeNX
  reference frames for both players, pause/resume/stop, diagnostics and spacing.
  Summary: duo-polish-game-summary.json.
- Physical inner display after footer removal: both layout tests passed in
  FooterFreeDevice.xcresult. Summary: duo-polish-open-summary.json.
- Physical cover display, final code: both layout tests passed in
  StatusDevice.xcresult. Summary: duo-polish-closed-summary.json.
- iPhone Duo simulator, iOS 27.2, largest accessibility text size: both layout
  tests passed in LargeFinal.xcresult. Summary: duo-polish-large-text-summary.json.
  Simulator text size was restored afterward. Its library used real sample NROs
  with a synthetic long title for layout review.
  An additional layout-only run (LargeVisual.xcresult, one test passed) captured
  the native simulator display during scrolling: XCTest's app screenshot was
  horizontally cropped, while the direct display capture confirmed full-width
  labels and insets. The saved large-text image is the direct display capture.

Layout checks verify readable scrolling content, at least 16-point card insets,
44-point import geometry, and actual taps at all four edges of the import target
opening Files and returning successfully. Screenshots were visually inspected.
Full result bundles are under /tmp/satchel-polish. No core changes were made, so
the earlier core regression suites were not repeated for this UI-only pass.

Evidence: duo-polish-open.png, duo-polish-detail.png,
duo-polish-diagnostics.png, duo-polish-closed.png,
duo-polish-closed-detail.png and duo-polish-large-text.png.

## Remaining limitation

Menus explicitly request visible system status indicators. They appear on the
inner display but remain absent on the physical cover display in landscape.
This is observed behavior, not a claim about intended OS behavior. Further
investigation needs a minimal native-toolbar reproduction on the cover display.
Gameplay intentionally remains immersive. No synthetic status indicators were
added.

## API references

Native toolbar and status APIs were checked against the installed Xcode 27.2 SDK:
axisBehavior(.verticalPreferred), contentMarginsRemoved(), and
toolbarVisibility(.visible, for: .statusBar). Apple guidance consulted:

- https://developer.apple.com/iphone-duo/prepare/
- https://developer.apple.com/design/human-interface-guidelines/designing-for-iphone-duo
- https://developer.apple.com/videos/play/tech-talks/111462/

## Noticed, not changed

The host ffmpeg executable failed with `Library not loaded:
/opt/homebrew/opt/x265/lib/libx265.215.dylib`. Video review used Apple AVFoundation
instead; no host packages were changed.
