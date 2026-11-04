# Satchel UI journeys

The `SatchelNavigation` suite targets the single Satchel app. Phone tests stay read-only:
no game launch, Prepare, removal, file selection, identity changes or sample imports.
Launch preferences disable automatic Prepare/play and legacy cleanup, and hide developer
controls. Missing phone-library prerequisites produce explicit skips. Every invoked control
must exist, be enabled and hittable. A control that is already hittable proceeds
immediately; otherwise the same bounded readiness wait applies. Fixture journeys launch
their own prerequisites directly instead of first opening an unused default library. Synthetic Simulator prerequisites fail if missing.

The full suite is this navigation target. The older `Tests/UI/*JourneyTests.swift` target
uses retired interpreter screens and mutates content, so it is not part of the canonical
one-app scheme.

## Coverage

- Home content, search, sort, Add, prepared-only shelves with zero imported records,
  cached artwork, retained records after original removal and empty-library entry points.
- Both landscape rotations and portrait requests while home and Settings remain landscape.
- Simulator only: Device check opens in both landscape orientations, refreshes and
  survives background/return. Synthetic present, missing and unknown entitlements
  have distinct labels. Copy report is read from the real clipboard and checked with
  team inclusion off, on and off again, then reset on reopen. A separate live simulator
  journey runs the native memory/signature probe with host limits, not phone claims.
  Its reveal helper scrolls by geometry before querying activation points, because
  iOS 27.1 XCTest can throw for offscreen controls. It still requires real hittability
  before every action. The clipboard journey closes with inclusion enabled and requires
  it to reset off when Settings reopens.
- Settings, Prepare, identity controls, player settings, detail, picker cancellation,
  background/return, close/reopen and prepared removal cancellation.
- Absence of Performance/Graphics modes, frame cap and render scale controls.
- Simulator only: SwiftUI Quit with cancellation and repeated relaunch, plain-menu Quit
  with cancellation, library pause through background/return, resume, and stop completion
  before starting another title. Both Quit paths must leave the app in the foreground at home.
- Simulator only: bonus app and update in game details, DLC in player settings, no standalone
  DLC or bonus shelf tile, including a filtered shelf.
- Save panel import-picker cancellation on the phone, without choosing a file. The rumble
  control is inspected without changing phone preferences.
- Simulator only: real save export/import/backup restore over synthetic bytes, export and
  backup cancellation, rejection of a wrong-title ZIP and a corrupt ZIP without changing
  the current save, backup rotation at three, background and quit backup events, and
  disabled mutation controls while a session is paused. System Files pickers also open
  and cancel; content selection for successful round trips uses the synthetic provider.
- Simulator only: rumble is changed, saved by Play, and checked after process relaunch;
  a second title retains its own choice. Physical vibration is not measured.
- Simulator only: split `.nsz.00/.01/.02` and `.xcz.00/.01/.02` imports must produce one
  container and deduplicate a second selection. These are positive requirements, not
  expected failures. The `.nsz.00` and `.xcz.00` names remain distinct requirements
  from the numeric and `ns0`/`xc0` naming schemes.
- Simulator only: numeric split folders and reversed selected `ns0`/`xc0` parts for
  NSP, XCI, NSZ and XCZ. Each import must produce one byte-identical local container
  and deduplicate reimport. The fixture supplies the selection to the real import queue.
- Simulator only: remove a prepared game, its update, two DLC imports and both prepared
  packages, preserve saves and unrelated records, then require no shelf tile after relaunch.
  Removing only the update must retain the game, DLC and prepared packages.
- Simulator only: the Prepare DLC row follows no base, a base without DLC and a base
  with two DLC imports. These fixtures write small local placeholders so the real
  `hasOriginal` checks determine availability.
- Simulator only: a running home warm-up card disappears after completion; paused and
  failed cards can be dismissed through the real home controls.
- Simulator only: NSZ/XCZ container import, format classification, duplicate handling and
  import results surviving background/return. Selection is supplied by a fixture button;
  the separate picker test covers open/cancel. This does not validate iCloud download or NCZ
  decompression. Payload bytes are synthetic and not executable.

- Simulator only: included DLC title-ID rows, no Add DLC for an equal included version,
  Add DLC for a newer imported version or additional DLC, and the Prepare explanation
  without a package. Running work disables Add DLC, can be cancelled through the real
  job, preserves imports and packages, and refuses Play and confirmed removal.
- Simulator only: the game page's Prepare opens Prepare with the game's imported base
  already selected, also after background and return. Key and direct/nested storage
  errors from a synthetic discovery worker pass through the real Prepare job and
  recovery UI.
- Simulator only: an import started from the home screen shows its card while it runs,
  then "Added" with Open (nothing opens by itself) or "Couldn't add" with Details, which
  opens the import screen with the result. The card stays until dismissed; + starts fresh.
- Simulator only: Library from the pause menu leaves a "Paused · Resume" card on the home
  header that reopens the paused game; Play reads Resume; Workshop > Prepare is disabled
  with "Quit the game before preparing another."
- Simulator only: shader warm-up lives on the game page. The home card opens that page;
  Pause there leaves Resume; a finished warm-up leaves the home header but the game page
  keeps showing it as warmed up, with Warm up again.
- Simulator only: cancel a partially copied 2 MiB synthetic import, require no library
  entry or temporary copy and unchanged source bytes, then restart and require one
  byte-identical container. The production importer performs cancellation and cleanup.
- Simulator only: a game with no warm-up record offers Warm up; a user pause survives
  background and return. System expiration resumes on return, including cancellation
  acknowledged only after the app is active again.
- Simulator only: Prepare is disabled beside a paused game, then enabled after Quit
  with a selected local base and synthetic keys. The game's warm-up also becomes available.
- Simulator only: upcoming, absent and expired signature reminders with synthetic dates.

- Simulator only: Prepare > Game diagnostics renders two sanitized missing-service rows
  with command, optional method, first-seen frame and count. Zero-byte, empty-array,
  malformed and oversized JSON must leave the sheet responsive with no service rows
  or raw payload. Each case closes and reopens the real sheet, then returns to the library.
- Settings > About > Install and refresh help opens in both landscape orientations,
  scrolls through the bundled installation and refresh sections, and closes back to
  Settings. First launch lands on the home screen; there is no separate guide.
- Simulator only: Quit from the SwiftUI pause menu returns to a usable library in the
  same process, then a different prepared title starts, pauses and resumes. The exact
  host event sequence requires stop completion before the second start.

The Files cancellation helpers wait up to 30 seconds for the system document-picker
navigation bar after each single opening tap. The separate Files service can start
near the ordinary control helper's eight-second deadline on the simulator. Cancel
must still exist, be enabled and hittable, survive background and return, dismiss
the picker, and work again on reopen. The shared button helper and paused save
controls use the existing bounded, directional reveal helper so a full upward swipe
cannot leave a small action above the viewport. The save picker also waits up to
30 seconds for a hittable Cancel or Duo overflow control, since its content can
arrive before the system toolbar. The selected route must pass the ordinary
enabled and hittable checks and survive
background and return; cancellation still requires the real Cancel action. No tap
retries or skipped assertions are used.

## Simulator boundaries

`Tests/UIPreview` composes production SwiftUI screens, native menu bridge and display controls
with synthetic library records, artwork, manifests, host and signer. It loads no guest code.
The compression fixture refuses decoder creation; import checks stop at container validation.
The project generator stages `PreparedGame.swift` outside the checkout and replaces only the
call to the device package verifier with `PreviewPackage.verify`. The view's actual
stop-before-switch, notification and dismissal logic is unchanged. The fixture session verifier
accepts only its two named synthetic packages and requires an explicit launch flag.
`GameFixture.m` records start, stop request and asynchronous stop completion for ordering
assertions. These assertions establish UI routing against a host stand-in, not native
thread cleanup, save durability or successful game restart on hardware.

The navigation generator stages `GameSavePanel.swift` with only its document picker call
replaced by `PreviewSaveDocumentPicker`. With `SatchelPreviewSaveProvider` disabled, that
wrapper opens the original system picker. With it enabled, explicit fixture buttons
supply matching, wrong-title and corrupt archives to the unchanged panel completion.
Exported bytes are copied to a fixture destination and restored into an independent
synthetic root before being compared. `SaveStore`, `SaveArchive` and `SaveLifecycle` stay
unchanged. The native save adapter copies synthetic snapshots and models the session gate;
it does not test the engine's commit lock. No fixture is part of the phone target. Fresh library seeds clear the prior Prepare
outcome and default to no console keys unless explicitly requested. Relaunches without
reseeding preserve state, so persistence assertions still exercise stored data.

The harness includes the production warm-up job, panel and home slot. With the explicit
`SatchelPreviewWarmupTests` flag, its runner emits controlled synthetic progress and
completion, cancellation or failure. Other runner requests still fail explicitly. The
fixture starts the real job with `verified: true` because its manifests contain no game
code. The generator also stages `WarmupJob.swift` with only the verifier call replaced
by a flag-gated synthetic verifier, allowing Resume and the game page's Warm up to
exercise the real job. A fixture context receives the production expiration callback
when the app backgrounds; another case withholds cancellation acknowledgement until
return. This tests recovery routing, not the operating system's expiration schedule.
It performs no real package verification, shader translation or compilation. The progress
model is staged from production source. The preview requires iOS 26 APIs; validation uses
iOS 27.1. `NavigationFixFixture` exposes persisted synthetic records and file counts to
verify removal beyond the visible tile. The import cancellation case stages `SwitchPackageImport.swift` with a single hold
callback after a copied chunk. The hold applies once under an explicit fixture flag;
all cancellation checks, file cleanup and publication remain production code. It uses
2 MiB of synthetic bytes, not large sparse files or real games.

`OnboardingDLCFixture` replaces the preview target's `AddOnUpdate` transaction only.
`AddOnJob`, the game page and the Play/Remove guards remain production source. The worker
requires an explicit launch flag, named synthetic package and local fixture sources,
reports extraction, and waits for cancellation. Its manifest is compared before and after
cancellation. These tests do not authenticate, decrypt, sign, verify or publish real DLC.
The guided route uses cached synthetic metadata and preseeded zero-valued key placeholders;
it does not claim successful system-picker selection or real key validation. Discovery
throws real error types for recovery tests without filling the disk or reading game content.

`DiagnosticsFixture` seeds a failed Prepare outcome, a safe report, matching session
metadata and `missing-services.json` in its own synthetic runtime directory. There are
no host or service log fallbacks. The report's initial stage differs from the session
stage, so assertions prove the production reader reached the supplied runtime. Valid
rows include an unknown method, an extra raw payload field and an invalid interface
field. The oversized case is valid JSON padded to exactly 512 KiB plus one byte; without
the size guard it would display the same rows. The empty-file case is zero bytes, and
an additional empty-array case covers valid JSON with no entries. No parser, report
view or Prepare source is substituted. These files contain no guest data.

The Simulator runner also builds and installs `NavigationBackground`, a blank landscape
peer with no storage or networking. Lifecycle checks activate that peer, assert Satchel
is backgrounded, then require the original panel to return hittable in landscape. On this
iOS 27.1 Duo Simulator, returning from Home or system apps left XCTest using portrait
coordinates even while Satchel rendered landscape. The peer avoids that automation
limitation without changing Satchel's lifecycle or orientation code. The phone suite
continues to use Settings; the orchestrator must check whether hardware has the same
coordinate issue. The companion is never built or installed by the phone command.

Session and import fixture tests compile only with `NAVIGATION_PREVIEW`. The phone target
cannot start them. The central read-only `tap` helper still rejects `player-play`; synthetic
session tests invoke that button directly within their Simulator-only code.

## Simulator command

Run from the checkout containing the changes. `NativeApp/build.py --ui-tests-simulator`
selects the isolated UI harness without building device archives. The existing native
build remains the default. The generator applies the current display patches to the
pinned engine under
`$SATCHEL_DEPS_ROOT/NX2Apple/engine` (default `~/SatchelDeps/NX2Apple/engine`).
Its overlay lives under the selected output folder; it does not reuse another
worker's generated gamepad source.

```zsh
cd "$HOME/SatchelBuild/uitests3/wt-ui-tests-9"
SATCHEL_OUT="$HOME/SatchelBuild/uitests3/run9" \
ANYGAME_OUT="$HOME/SatchelBuild/uitests3/run9" \
SATCHEL_BUILD_ROOT="$HOME/SatchelBuild" SATCHEL_DEPS_ROOT="$HOME/SatchelDeps" \
SATCHEL_UI_SIMULATOR=B1F54F54-BA40-4A62-86E7-2E9E6AF666C0 \
nice -n 10 python3 -B NativePrepare/guard.py \
  python3 -B NativeApp/build.py --unsigned --ui-tests-simulator \
  > "$HOME/SatchelBuild/uitests3/full-9.log" 2>&1
```

The shared guard waits for heavy compilers and shader drivers, enforces a stricter 9 GiB
per-process RSS limit and stops before free disk reaches 35 GiB. Xcode uses two jobs and
serial testing, building only the selected simulator architecture. Each run writes a
timestamped xcresult under `SATCHEL_OUT`. The default
Simulator is the task-owned `Satchel uitests3`, Duo iOS 27.1,
`B1F54F54-BA40-4A62-86E7-2E9E6AF666C0`. Override with `SATCHEL_UI_SIMULATOR`. Targeted
investigations can set `SATCHEL_UI_ONLY_TESTING` to comma-separated method names; omit it
for the full suite. Simulator checks do not establish the phone's vertical bar or hinge behavior.

## Phone command, orchestrator only

The orchestrator must own the phone, with no active game or Prepare job. From the merged
checkout, build and run the read-only phone suite using these exact commands:

```zsh
export SATCHEL_OUT="$HOME/SatchelBuild/uitests3/phone-651b8c1"
export ANYGAME_OUT="$SATCHEL_OUT"
nice -n 10 python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py \
  > "$HOME/SatchelBuild/uitests3/phone-build-651b8c1.log" 2>&1
nice -n 10 python3 -B NativePrepare/guard.py xcodebuild test \
  -project "$SATCHEL_OUT/app/Satchel.xcodeproj" \
  -scheme SatchelNavigation -configuration Release \
  -destination 'id=00008160-001A30E02E600036' \
  -derivedDataPath "$SATCHEL_OUT/app/DerivedData" \
  -resultBundlePath "$SATCHEL_OUT/navigation-phone.xcresult" \
  -parallel-testing-enabled NO -jobs 2 -allowProvisioningUpdates \
  > "$HOME/SatchelBuild/uitests3/navigation-phone-651b8c1.log" 2>&1
```

Use a fresh result path on repeat runs. Collect results with:

```zsh
xcrun xcresulttool get test-results summary --path "$SATCHEL_OUT/navigation-phone.xcresult"
xcrun xcresulttool get test-results tests --path "$SATCHEL_OUT/navigation-phone.xcresult"
```

Manual hardware steps, performed by the orchestrator in a separate session:

1. Play an allowed prepared game, save in-game, open the menu, Quit, cancel, Quit again.
   Confirm home appears without process exit and the same game can start again.
2. Open the plain menu with developer controls and `-SatchelPlainNativeMenu YES`, repeat
   Quit cancellation and confirmation. Remove that launch override afterward.
3. From a running game, choose Library, background and return, then reopen that game.
   Confirm it stayed paused and Resume continues. Return to Library and play a different
   allowed game. Confirm the previous session stops and the new game starts.
4. Inspect prepared-only home entries, bonus content in details and prepared DLC in player
   settings. Confirm none appears as a separate shelf tile. Rotate in home, panels and game.
5. On a permitted game, inspect Save data, export a copy to a separate writable folder,
   cancel an import, then import the exported ZIP and verify progress by playing. Never
   write into the read-only ROM input folders. Verify backup before and after import,
   cancel then confirm a restore, and check four changed saves retain only three backups.
   Pause to Library and confirm export/import/restore stay disabled until Quit completes.
6. Set Game vibration off, press Play, confirm the controller does not rumble, then Quit,
   reopen and confirm the setting persisted. Repeat with it on and verify a second title
   kept its own choice. Simulator checks establish settings routing only.
7. If an allowed NSZ/XCZ input exists, import it through Files and confirm detection,
   progress, completion and duplicate handling. When split support is integrated, select
   all numbered parts and require one complete library item. Do not substitute unrelated
   game inputs. The worker's only split inputs are synthetic containers.

8. In an isolated synthetic library, check an included DLC row and absence of Add DLC
   when every imported version is already included. Add a newer synthetic record and
   require Add DLC. Without a prepared package, require the Prepare explanation.
   With suitable permitted inputs, start Add DLC, attempt Play and Remove, then cancel.
   Require refusal messages and unchanged package/imports. Real transactional integrity
   and publication require separate device validation by the owning orchestrator.
9. After the user imports their own keys and a permitted base, open its game page and
   press Prepare: check the selected base. Inspect signature reminders without changing
   device time or certificate contents. Do not clear the installed Satchel library to
   manufacture a first-run condition.

10. Open Prepare after a permitted preparation attempt, choose Game diagnostics, and
    check safe service rows and Done in both rotations. Malformed and oversized file
    scenarios require an isolated synthetic test container, never editing user reports.
11. Open Settings > About > Install and refresh help in each landscape orientation. Scroll
    through SideStore, AltStore Classic, refresh guidance and First launch. Close help and
    require Settings again. Do not alter accounts or signing identities.
12. Play an allowed prepared game, pause, Quit and confirm. Require the library without
    process exit, then launch a different allowed prepared game and verify pause/resume.
    This hardware check is needed to establish real native teardown and restart.

The worker performs no phone actions. Exact measured checks, failures and the remaining
blocker are in `~/SatchelBuild/uitests3/report.md`.

## Game settings and save manager

The game-page settings journeys cover five per-title choices, default and override labels,
reset, persistence across process launch, both landscape orientations and shared menu/page
screen values. Save journeys inspect dated, sized backup rows, confirm or cancel restore,
verify exported synthetic bytes, reject wrong-title/corrupt archives and gate save actions
while paused. The system boundary journey opens Files for import and the share sheet for
export. See `docs/testing.md` for hardware follow-up and the native storage checks.
