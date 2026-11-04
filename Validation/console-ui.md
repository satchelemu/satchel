# Console library UI: build 5

Validated 2026-10-31 on the maintainer's unfolded iPhone Duo, iOS 27.1.

The library now has side-by-side import/workshop and collection panels, a
turquoise/coral/charcoal identity, abstract cartridge tiles, local name search,
and recently-added/name sorting. File detail pairs a large tile with Play,
diagnostics and compatibility information. All existing imports remain local;
no new dependencies or emulator capabilities were added. Artwork is procedural
placeholder design, not screenshots, downloaded covers, or compatibility labels.

The Release build installed successfully. Two device journeys passed in the final
run: GameJourneyTests/testDownloadedGamePlaysOnDevice and
HomebrewJourneyTests/testImportRunPersistAndPicker. See console-ui-summary.json.
They cover the side-by-side layout occupying more than 85% of window width,
search/no-results/clear, sorting, actual Files import, full-width landscape
details/gameplay, original game reference frames for both players, pause/resume,
stop, return to library, sample diagnostic output, persistence and picker cancel.

The initial layout test reported `XCTAssertTrue failed` because the collection
container was not exposed to accessibility. Added an explicit contain group;
the same assertion and the complete game journey then passed. Visual inspection
also caught and fixed oversized decorative artwork clipping the tile header.

Evidence: console-library-device.png (native active-display screenshot),
console-library.png (sorted library), console-detail.png and console-sample.png
(XCTest attachments). Full test bundle: /tmp/satchel-ui/Final.xcresult.

Prior core/format validation was not repeated for this UI-only change. General
Switch compatibility, GPU rendering and audio remain out of scope and unsupported.
