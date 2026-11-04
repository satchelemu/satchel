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
