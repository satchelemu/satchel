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
`testGameSettingsShareMenuValues` cover all five settings, background/relaunch persistence,
reset, both landscape orientations and the page/menu shared screen setting. Save journeys
cover archive export/import, restore confirmation and cancellation, dated backup rows,
wrong titles, corrupt ZIPs, automatic backups on quit/background and paused-session gates.
`testSaveSystemPickerCancellation` opens the real Files picker and export share sheet.
The import sheet also has an app-owned Cancel import button, so a missing system toolbar
cannot trap the user in the document picker.
Other save journeys replace only document-provider selection with synthetic fixture URLs.
They do not contact iCloud or prove a game can load restored progress.

The existing menu exposes screen mode and haptics. Its reads now use the shared title store.
FPS, render scale and pipeline selection are on the game page and take effect after Quit
and a fresh launch. Adding these three selectors to the menu remains with the menu owner.
The shared `SatchelQueuePlayerSetting` API is available for that integration.

Phone follow-up for the orchestrator:

1. On a game's page, change all five settings. Confirm global/title defaults and override
   markers, close/reopen and restart the app, then reset each setting. A second title must
   keep its own settings. Check both landscape orientations and Duo postures.
2. Launch, change screen/haptics in the menu, return to the page and confirm the same
   values. Change them on the page while paused and resume; the menu must reflect them.
   FPS, scale and pipeline changes must wait for Quit and launch, with the engine log
   showing the chosen launch values. A frame target may need a compatible game patch.
3. Export a known save using Save to Files in the share sheet. Quit the game, import that
   title's ZIP and check the game loads its progress. Cancel a backup restore, then confirm
   one; verify current data was backed up first. Dates and sizes must match the archives.
4. Make four distinct saves with background/quit between them. Only the newest three
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

### What to report

For a compatibility report: game, version, title ID, phone model and iOS version, the
Satchel commit, how far it got, the FPS counter reading in a typical scene, how long you
played, and the game report from Game diagnostics. For a crash, the symbolic crash
address and the last 50 lines of `host.log`. Never attach keys, dumps, decrypted code or
shader dumps.
