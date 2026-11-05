# Testing Satchel

What can be checked on a Mac without a phone, what the simulator covers, and how to read
what a game did on the phone.

## On a Mac, no phone

These compile the component under test with clang or swiftc and run it against synthetic
fixtures. None of them needs keys, games or the dependency snapshot unless the script says
so at the top.

```bash
sh scripts/check_repo_hygiene.sh
```

Em dashes, leftover pre-rename names and tracked secrets. Runs in CI.
It also runs `scripts/check_architecture.py`, which fails when host or importer code
(`NativeHost/`, except the generated coverage tables, and `App/`) names a game by its title
ID. Per-game values belong in the profile data (`App/NativeGameProfiles.json`,
`App/NativeRuntimeProfiles.json`, `NativeHost/PlayerProfiles.plist`) and the compatibility
documents, which are not scanned. Tests, probes and simulator fixtures are left out; the lines
that engine patches add are scanned. `python3 scripts/check_architecture.py --self-test` runs
the pattern's own tests.

```bash
python3 -m unittest Tools/Rename/test_rename.py
```

```bash
python3 Tests/AddOnContent/run.py
```

DLC import, upgrade, conflict and atomic publication cases over synthetic encrypted
fixtures. Likewise `Tests/Onboarding/run.py`, `Tests/PrepareCleanup/run.py` and
`Tests/Saves/run.py`.

```bash
zsh scripts/validate.sh
```

The interpreter route: 1,800 frames of the original guest program compared byte for byte
against native execution, plus the boundary cases. The other `scripts/test_*.sh` scripts
cover one component each (content pipeline, NSO loader, RomFS, Horizon IPC parser, kernel
objects, shader bridge, Vulkan backend, and so on). Read the first lines of a script for
what it needs; several require the prebuilt libraries listed in `BUILDING.md`.

```bash
python3 NativeApp/Distribution/test_source.py
```

The SideStore source generator.

## Simulator UI suite

`SatchelNavigation` runs every app screen against synthetic library data in the iOS
Simulator: home, search, import (including split and compressed containers), Prepare,
settings, identity, saves, DLC rows, quit and relaunch, signature reminders, game
diagnostics, install help, the import, warm-up and paused-game cards. The lifecycle
journeys cover import cancellation and restart, warm-up interrupted by the system
(including late cancellation acknowledgement), user pause, a game without a warm-up
record, and Prepare and warm-up availability after Quit. It is read-only on a phone
and mutating only on the simulator. Synthetic sessions do not establish native game
teardown, cached shader reuse or iCloud background delivery on hardware.

```bash
python3 -B NativeApp/build.py --ui-tests-simulator
```

`Tests/UI/Navigation/README.md` lists the journeys. Use a dedicated simulator for this
suite; sharing one with other work stalls it.

### Game settings and save manager

`Tests/PlayerSettings/run.py` compiles the production Objective-C title store and pipeline
policy for macOS. It checks title isolation, defaults, case-normalized IDs, invalid patches,
reset without losing other overrides, launch-only settings, unchanged live environment,
argument overrides and exact/hybrid/legacy pipeline selection. Display geometry assertions
are retained. `Tests/Saves/run.py` checks the production archive/store over synthetic bytes,
including wrong-title rejection, corruption, atomic restore and bounded three-backup rotation.

Simulator journeys `testGameSettingsPersistResetAndBothLandscapes` and
`testGameSettingsShareMenuValues` cover all seven visible settings (including rumble
strength and rumble cutoff), background/relaunch persistence,
reset, both landscape orientations and the page/menu shared screen setting. Save journeys
cover archive export/import, restore confirmation and cancellation, dated backup rows,
wrong titles, corrupt ZIPs, automatic backups on quit/background and paused-session gates.
`testSaveSystemPickerCancellation` opens the real Files picker and export share sheet.
The import sheet also has an app-owned Cancel import button, so a missing system toolbar
cannot trap the user in the document picker.
Other save journeys replace only document-provider selection with synthetic fixture URLs.
They do not contact iCloud or prove a game can load restored progress.

The existing menu exposes screen mode, haptics, rumble strength and rumble cutoff. Its reads
use the shared title store. Rumble strength (`DM_RUMBLE_GAIN`, 0 to 3) scales controller
rumble and the phone's haptics; the cutoff (`DM_RUMBLE_MIN`, 0 to 0.9) silences a
controller's weakest levels. Both retune a running engine at once
(`satchel_haptics_tune`, `NativeHost/controllers/overlay.py`); `NativeHost/controllers/check.py`
runs the retune against the pinned engine's fake haptics queue and
`Tests/PlayerSettings/run.py` checks validation, launch environment and reset.
FPS, render scale and pipeline selection are on the game page and take effect after Quit
and a fresh launch. Adding these three selectors to the menu remains with the menu owner.
The shared `SatchelQueuePlayerSetting` API is available for that integration.

Phone follow-up for the orchestrator:

1. On a game's page, change all seven settings. Confirm global/title defaults and override
   markers, close/reopen and restart the app, then reset each setting. A second title must
   keep its own settings. Check both landscape orientations and Duo postures.
2. Launch, change screen/haptics in the menu, return to the page and confirm the same
   values. Change them on the page while paused and resume; the menu must reflect them.
   FPS, scale and pipeline changes must wait for Quit and launch, with the engine log
   showing the chosen launch values. A frame target may need a compatible game patch.
3. Export a known save using Save to Files in the share sheet. Quit the game, import that
   title's ZIP and check the game loads its progress. Cancel a backup restore, then confirm
   one; verify current data was backed up first. Dates and sizes must match the archives.
4. Rumble: with a controller, set Rumble strength to 0%, 50% and 300% from the menu while
   a game rumbles and feel the change at once; set a cutoff of 45% and check light rumble
   stops while strong rumble remains. With touch controls, check the phone's haptics follow
   the strength. Quit and relaunch: the title keeps its values, another title does not.
5. Make four distinct saves with background/quit between them. Only the newest three
   backups remain. While paused, export/import/restore stay disabled. A wrong-title or
   corrupt ZIP must show an error and preserve the existing save. Arbitrary emulator ZIPs
   are unsupported; the imported archive needs Satchel's title-bound inventory.

### Device check

Settings > Device check reads the running task's signature through Security's SecTask
SPI, resolved at runtime. Missing or false entitlements and unavailable queries have
different labels. It never substitutes the profile's allowed entitlements for the
running signature. The embedded profile is used only for `ExpirationDate`, through
the existing bounded CMS reader. Missing or unreadable profiles show Unknown.

The memory snapshot uses `os_proc_available_memory` and `TASK_VM_INFO`. The process
limit is footprint plus positive remaining allowance from the same task-info result;
zero remaining allowance cannot establish a limit and is shown as unknown. The virtual
address span is the kernel's map bounds, not free memory. At most seven `vm_map`
attempts try 64, 32, 16, 8, 4, 2 and 1 GiB with no access and no physical pages touched.
Each successful mapping is immediately released. The result is a contiguous lower
bound, never total free VM or physical RAM. Measurements run off the main thread,
without enumerating games. Refresh takes a new snapshot. A same-device baseline is
needed to attribute an increased memory limit to an entitlement.

The installed iOS SDK's `os/proc.h` and `mach/task_info.h` define the memory APIs.
The runtime Security ABI follows Apple's
[SecTask header](https://github.com/apple-oss-distributions/Security/blob/main/sectask/SecTask.h).
No new dependency is required. Security SPI availability and signed readings must
still be checked on hardware.

The full suite includes `testDeviceCheckBothLandscapes`,
`testDeviceCheckCopiesReportWithAndWithoutTeam`, `testDeviceCheckMissingEntitlement`,
`testDeviceCheckUnknownEntitlement` and `testDeviceCheckLiveSimulatorProbe`.
The first four use explicit synthetic data and a non-account team sentinel. The
clipboard fixture reads the actual pasteboard after the production action. Tests
check all report fields, default redaction, opt-in inclusion, switching back to
redaction and reset on reopen. The live probe uses simulator host limits, not phone
capabilities. Fixtures are absent from the phone target. Clipboard copies are local
to the device and expire after five minutes.

Orchestrator hardware checks, in a separate authorized phone session:

1. Open Settings > Device check in both landscape orientations. Confirm it opens
   promptly, scrolls to every field and returns to the library. Background and return,
   then check Refresh updates the sample time.
2. Compare the entitlement labels with the installed app's signed entitlements.
   Inspect memory allowance, process limit and reservation independently. Do not
   interpret a large address span as physical memory. An unavailable API must show
   Unknown, not a positive result.
3. Compare the team and embedded profile expiry privately. Do not attach team IDs,
   profile contents or screenshots containing the team to logs. Compare free space
   with the app container's volume, allowing for changes over time.
4. Copy into a local note with the switch off and confirm no signing-team line. Enable
   it and verify the team privately, then disable it and copy again. Close and reopen
   Settings and confirm the switch defaults off. Do not send the report automatically.

These checks need no game, keys, identity import or library changes. A separately
provisioned experiment is needed for a same-device baseline with an entitlement absent;
do not replace the user's app to manufacture it.

### iCloud save sync

Settings > Storage > iCloud save sync chooses one folder (usually in iCloud Drive) through
the Files folder picker and keeps a security-scoped bookmark, or turns sync off. At each
game start the bridge sets `DM_SAVE_SYNC` to the folder (or 0) and the pinned engine's
`save_sync.c` keeps `<folder>/<title ID>/save.dmsync`: the cloud's copy comes in at the
first save mount when it is the one to keep, the committed save goes out a few seconds
after each Commit, on background and at Quit, and whichever save is replaced is kept in
`NativeRuntime/<title ID>/save-backups/`. `App/GameSavePanel.swift` documents how this and
the save manager share the save folder without writing it at the same time.

```bash
python3 -B NativeHost/session/check_save_sync.py
```

Session lifetime over a plain folder: off, send at stop, a same-process remount with
another title, an in-step remount and a pull with the engine's backup. Simulator journeys
`testCloudSaveFolderChooseRelaunchAndOff` (synthetic folder in place of the picker; real
bookmark storage) and `testCloudSaveFolderPickerCancellation` (the real Files folder picker,
cancelled) cover the setting. Nothing here contacts iCloud.

Phone follow-up for the orchestrator (two devices or a reinstall with the same signature):

1. Choose an iCloud Drive folder, play and save, Quit. The folder shows
   `<title ID>/save.dmsync` within a minute; `host.log` has `save sync: sent`.
2. Background the app right after a save and check the file updates.
3. On a second device (or after deleting the local save), start the game: `host.log` has
   `brought in the cloud's save` and the game loads that progress.
4. Change the save on both sides, then start: the newer one is used and the other appears
   in `save-backups/`. Import or restore a save on the game page, start the game and check
   it is sent.
5. Re-sign the app with another identity: Settings shows the choose-again note and games
   start with sync off. Turn sync off and check no further sends.

### iPhone Duo harness

```bash
SATCHEL_DUO_SIMULATOR=<private simulator udid> python3 -B Tests/Duo/run.py
```

XCTest over the production display layout with the posture test override: the
half-folded split (picture above the fold, controls below), posture changes without a
resize, both screens and odd heights, a camera beside either half, and the real scene in
both landscape rotations. `Tests/Duo/test_orientation_policy.py` (also run by the driver)
keeps every orientation declaration landscape only. See `Tests/Duo/README.md`. Synthetic:
the hinge sensor, real touches and the renderer are not involved.

Phone follow-up: on the Duo, play with the hinge half open in both landscape rotations,
open and close it while playing, and check the picture stays on the top half, the controls
on the bottom half, and no input sticks across a change.

### Picture shape, motion and TV

The game view follows the shape of the picture a game presents
(`NativeHost/display/picture-shape.patch`); 16:9 games keep their layout. The Duo picture
shape setting (`DM_SCREEN_ASPECT`) is hidden for every tested title (`PlayerProfiles.plist`
`screenAspectTitles` is empty) and each launch gets 0; `NativeHost/display/check.m` covers the
gate and the taller-picture geometry. Phone motion is on for every game (no `DM_MOTION`
override): check a motion-aiming game turns with the phone on both Duo screens. External
display (TV) output is routed to the engine only with Developer controls on: with them on,
connect a cable or AirPlay TV during a game and check the picture moves there, the phone
keeps the controls, and Quit and the pause menu still work.

## The Mac harness: run a game on the Mac

The engine also builds for macOS, so most bring-up happens on the Mac. The capture
harness (`NativeGames/run_capture.py`) runs a prepared game for a bounded time with a
scripted input sequence, captures shaders, and writes the host log and screenshots:

```bash
python3 NativeGames/run_capture.py --host <host build> --patched <rewritten modules> --profile <game.tsv> --romfs <romfs image> --tables <tables dir> --output <out dir> --seconds 60 --input-script "<script>"
```

`NativeGames/runtime-capture.md` explains the inputs and the capture format. The harness
needs your own prepared content; nothing in the repository supplies it.

## On the phone

Everything a game does is recorded under the app's data container:

- `Documents/NativeRuntime/<title ID>/host.log`: the engine log, with frame pacing lines
  (p50, p95, p99 per 300 frames), pipeline builds, thermal state and footprint.
- `Documents/NativeRuntime/<title ID>/missing-services.json`: every Horizon command the
  game asked for that the host does not implement, with first-seen frame and count.
- `Documents/GameReports/<package>.json`: the sanitized report shown in Prepare, Game
  diagnostics (stage reached, services, symbolic crash address, shader failures).
- `Documents/prepare-progress.json`: whether a Prepare is running.

Copy them off with Finder (the app's Documents folder is visible) or:

```bash
xcrun devicectl device copy from --device YOUR_DEVICE_UDID --domain-type appDataContainer --domain-identifier com.dell.satchel --source Documents/NativeRuntime/0100F2C0115B6000/host.log --destination ./host.log
```

### Developer controls

Settings, About, tap the Satchel name seven times to show the Developer controls switch.
With it on, launch arguments drive the app from a Mac for repeatable tests:

```bash
xcrun devicectl device process launch --device YOUR_DEVICE_UDID --terminate-existing com.dell.satchel -- -SatchelDeveloperControls YES -SatchelAutoLaunchPreparedGame YES -SatchelPreparedGame TOTK-1.4.3-2
```

`-SatchelPreparedGame` takes the package name shown under Settings, Prepared games.
`-SatchelFPSTarget`, `-SatchelRenderScale` and `-SatchelScreenMode` override the
per-game profile for one launch. The FPS counter in the pause menu shows the measured
rate on screen.

Screen capture: `xcrun devicectl device capture screenshot` returns a black image for
the game view on the tested phone. The host writes its own frame captures when asked
through its control file; `NativeGames/ctl.zsh` wraps that.

Remote Quit and repeated prepared-game visits are described in
[Developer Quit and prepared-game cycles](phone-cycle.md). It includes exact
orchestrator commands for one Quit/relaunch, two three-game rounds and a 60-minute
cycle, with the TSV columns and failure rules. No phone result is implied by the
synthetic Mac and simulator checks.

### What to report

For a compatibility report: game, version, title ID, phone model and iOS version, the
Satchel commit, how far it got, the FPS counter reading in a typical scene, how long you
played, and the game report from Game diagnostics. For a crash, the symbolic crash
address and the last 50 lines of `host.log`. Never attach keys, dumps, decrypted code or
shader dumps.

## Controller profiles and menu navigation

`NativeHost/controllers/check_remapping.py` exercises category defaults, JSON validation,
global inheritance, title and category isolation, preservation while saving picture and
rumble settings, reset, cross-process persistence, radial dead zones, single Plus/Minus delivery, chord takeover and release, Home reservation,
hold timing, focus edges and GameController snapshots on the Mac. It uses its own executable
preferences domain and no game files. `NativeHost/controllers/check.py` retains the existing
synthetic host HID and rumble checks.

The simulator journeys are `testControlsSettingsAndPersistence`,
`testControlsLibraryDetailAndBack` and `testControlsMenuShortcutAndLayout`. Test-only buttons
drive a GameController snapshot and invoke the value-change callback installed by the
native host. Plus/Minus taps and navigation are short pulses; the stick and full chord
stay held until the journey explicitly releases them, so slow accessibility queries
cannot outlast the input. The production readers, profile storage, input overlay,
menu bridge and focus navigator process those pulses. A native fixture records any game
button delivery and the menu's volume, opacity and size. The fixture and its controls are
absent from the phone target.

Profiles are JSON data blobs under `ControllerProfiles` in each `SatchelPlayerSettings.<title>`
preferences dictionary. `SatchelPlayerSettings.default` holds global category defaults.
An absent title category inherits that category's global default, then factory defaults.
Categories are Xbox, DualSense, Pro Controller and Generic. Nintendo face labels keep their
labels; other controllers use Switch positions. D-pad names identify the four buttons of
GameController's Direction Pad. Sticks retain their original axes with a radial dead zone.

Home is reserved by default and opens the menu after 0.75 seconds. Without Home, the shortcut
uses Menu + Options. Menu + Options and Off are configurable. Each chord button reaches the
game normally when pressed alone, including short Plus and Minus taps. On the first input
sample with both down, both are suppressed until both are released. Holding both for 0.75
seconds opens the menu once. Partial release cancels an unfinished hold but keeps suppression;
a fired chord cannot reopen the menu until both buttons are released.

If the first input sample contains both buttons, the chord exposes zero frames. A
staggered chord exposes the first button on all guest frames before the second press, with no fixed upper bound since the user controls
that interval. There is no added delay or extra published frame after chord completion. Takeover also
clears unconsumed chord edges in the native HID latch; ordinary short taps stay latched.
The Mac checks cover both press orders, both release orders, short chords, rearming and
per-controller snapshot state. The simulator proves short Plus/Minus delivery and the full
chord's suppression and menu opening through the native publisher. Home remains reserved,
including short taps. This behavior is shown in Settings. Off restores every mapped button.
Navigation uses physical bottom for accept and right for back, independent of game mappings.
A stick must pass 0.6 to move and return below 0.3 to rearm the same direction. Held inputs
do not repeat. The highlighted menu slider advances in steps when accepted, wrapping at its
maximum. The library, game actions and pause menu expose controller focus; system file
pickers and detailed settings editors still use touch.

Orchestrator phone checks, with separate phone authorization:

1. Pair Xbox, DualSense and Pro Controller in turn. In Settings > Controllers, choose the
   matching category. Confirm each named button lights in the tester and both stick dots
   track the hardware. Set a dead zone and check drift is suppressed without losing full tilt.
2. Map one face button to ZR. Launch a permitted title and verify the game sees ZR. Open
   Controller layout and remapping from pause, change this title only, then launch another
   permitted title and confirm its global mapping is unchanged. Relaunch to check persistence.
3. Hold Home for 0.75 seconds. Confirm the pause menu opens once, the system overlay stays
   closed and no game action occurs, including for a short press. Select Menu + Options and
   first tap Plus alone and Minus alone to check the game's pause and menus. Hold the chord
   in either press order: only the first button may reach the game before chord completion.
   Release one button, then the other: neither may reappear until both have been released.
   Verify a short full chord stays suppressed without opening Satchel's menu. Set Off and
   confirm both mapped buttons reach the game. Repeat using the no-Home fallback.
4. Use only d-pad or stick edges, bottom face button to accept and right face button to go
   back: library tile, game page, Play, pause, Resume, Library and Quit confirmation. Confirm
   focus stays in the active screen, scrolls into view and does not repeat while held.
5. Change volume while paused, resume and listen. Change touch opacity and size, disconnect
   the controller and inspect the real controls. Check both landscape orientations and both
   Duo screens. Verify layout diagrams match the selected touch layout and physical mappings.
6. Disconnect while holding a direction and reconnect. Confirm the game pauses, no input
   sticks, rumble stays suspended until Resume, and navigation works after background/return.

No controller-only or simulator check changes a title's gameplay compatibility rating.
