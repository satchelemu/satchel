# Phone-only package import: 2026-10-31

**Validated: local XCI/NSP import and persistence. Not implemented: XCI/NSP
execution, on-device NCA metadata decryption, update association/application.**

Satchel build 7 (`com.dell.satchel`) is installed on the maintainer's physical iPhone
Duo, iOS 27.1. The original three NRO entries were compared before/after and
preserved exactly. The separate native Zelda bundle and data were not changed.

## Actual phone journey

XCTest operated the real Files picker: Import from Files → Browse → iCloud Drive
→ ROMs → Switch → the selected package. The phone app performed inspection,
copying, hashing and local indexing. No desktop extraction, per-game compilation,
or preprocessed game transfer was used for these imports. The Mac orchestrated
the development build and UI test only.

| User-provided package | Full file bytes | Checked content entries | Result |
| --- | ---: | ---: | --- |
| Castlevania Belmont's Curse XCI | 4,845,553,152 | 8 secure entries | Imported; details reopened after app restart |
| Castlevania Digital Sound & Art Gallery update NSP | 88,755,040 | 6 entries | Imported; details reopened after app restart |

Both device index SHA-256 identities match independent streaming hashes of the
original iCloud source files on the Mac. Device filesystem listings confirm the
complete local file sizes and no `.import-*` partial copies. This is transport
and storage evidence, not decrypted game verification or gameplay evidence.

Both package detail screens explicitly say "Not playable yet"; tests assert
that Play and Run diagnostic are absent. Screenshots were visually inspected:
landscape layout, padded readable cards and native status sidebar remain intact.
Cover-display and accessibility-size layouts were not revalidated in this pass.

## Checks and findings

- Swift parser: 49 synthetic assertions passed, including a 5 GiB sparse file,
  prefixed XCI, malformed names/ranges, overlaps, hashes and cancellation.
- Streaming importer: five scenarios passed with warnings treated as errors:
  256 MiB bounded copy/progress/digest, deduplication/cache repair, mid-copy
  cancellation, changing-size rejection, and startup partial-file recovery.
- Device: existing NRO import/run/persistence/Files-picker journey passed.
- Device: NSP and XCI import/persistence journeys passed independently.
- Final development build-for-testing passed. The only compiler-tool warning
  was skipped AppIntents metadata extraction because there is no dependency.
- The actual XCI is trimmed by 1024 bytes of final secure-partition padding.
  The parser accepts it only because every actual content extent and declared
  hash region is present. Fixtures reject losing even one required content byte.
- Initial Files-search automation failed with "Selected package must be visible
  in the real iCloud Files browser." It had not committed a search suggestion.
  The corrected test browses the known folder and retains all import assertions.
- Review found process-kill partial-file cleanup missing. Startup recovery now
  removes only regular files with the importer-owned `.import-<UUID>` pattern;
  its regression test caught and corrected a prefix-length bug. It preserves
  completed files, unrelated files, symlinks and their targets.

Cancellation during a local streaming copy is tested. Cancellation while an
iCloud provider is still hydrating a file is requested via NSFileCoordinator,
but provider cancellation timing was not physically tested. Disk-full handling
has preflight and normal I/O errors; actual disk exhaustion was not induced.

## Reproduction and evidence

Source tests: `scripts/test_packages.sh`, `scripts/test_package_import.sh`, and
`Tests/UI/PackageJourneyTests.swift`. The latter takes runner environment
`SATCHEL_TEST_PACKAGE_NAME` and optional `SATCHEL_TEST_PACKAGE_FOLDER`; no game
fixture is shipped in the repository.

Development artifacts remain outside the repository:

- `/tmp/satchel-phone-import-build`: final build and configured XCTest runs.
- `/tmp/satchel-phone-import-nsp-01.xcresult`: passing NRO journey and initial
  failed Files-search attempt.
- `/tmp/satchel-phone-import-nsp-02.xcresult`: passing NSP journey.
- `/tmp/satchel-phone-import-xci-01.xcresult`: passing XCI journey on final build.
- Corresponding `*-attachments` directories contain screenshots/UI traces.
- `/tmp/satchel-package-import-digest-checks.json`: source identity comparisons.
- `/tmp/satchel-import-device-files.json`: device local-file sizes.

The next execution gate is the no-JIT CPU/Horizon/GPU work described in
[PHONE_RUNTIME.md](../Tools/PHONE_RUNTIME.md). Import success does not resolve it.
