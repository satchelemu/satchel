# Satchel display integration

`NativeHost/build.py` applies `screen-modes.patch` to generated overlay copies after checking
the pinned engine source hashes. The dependency tree and the task's engine copy stay pristine.
The patch touches `front_ios.m`, `DmGamepad.m`, `hle_hid.c`, `hle_audio.c` and the small thermal telemetry hook in `hle_nvn.c`.
`frame-shot.patch` separately updates only `nvn_mtl.m` frame readback. The live presentation
path remains unchanged. `NativeApp/build.py` fingerprints both patches.
`PlayerSettings.m`, `DisplayControls.m` and `InGameMenu.m` join `libSatchelNativeHost.a`.
The Swift settings view and menu adapter join the dev app.

Build from this checkout on macOS with zsh:

```zsh
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/display/build.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/display/check.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/display/thermal-check.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/display/simulator.py
```

Products stay under `~/SatchelBuild/display2`. `SATCHEL_DISPLAY_OUT` can override the device
build/check output root. The drivers query `pgrep -f xcodebuild`, check the executable names
of matches to exclude orchestration shells quoting that word, and wait for actual builds.
Compilers run with `nice -n 10` and two jobs. `NativePrepare/build-dev.py` also excludes other
heavy compilers. The simulator fixture has its own bundle ID and loads no guest or game data.
Build scripts perform no device actions.

The display build reuses media, unchanged graphics archive members and generic utility assets from
`~/SatchelBuild/host`. It recompiles only hle_nvn.c for thermal telemetry and verifies all other archive members stay unchanged. It does not include concurrent shader changes. After merging renderer
changes, rebuild those archives from the merged source. Rebuilding only the Swift app against
old host archives misses the native menu, audio and layout changes.

Player settings use `SatchelPlayerSettings.<titleID>`. TOTK defaults to 60 FPS, scale 0.5 and
fit. PlayerProfiles.plist supplies title-specific Performance and Graphics presets. TOTK Graphics is 30 FPS / 0.85; fallback titles keep the previous 30 FPS / 0.65 default. Saved preferences and valid
command-line overrides retain precedence. The manifest verifier is unchanged. Only the three
player settings override its environment at launch. Frame cap and render scale require a fresh
process. The menu explicitly distinguishes this session from next-launch choices.

Two-finger double tap toggles fit/stretch. Three-finger double tap opens the pause menu.
Controller Options plus Menu held for one second does the same. Normal input is neither
delayed nor cancelled until opening is recognized. `screen fit`, `screen stretch` and
`screen next` change modes via DM_CTL. Saved or launch-argument zoom migrates to fit;
live `screen zoom` is rejected. Picture and touch projection share the half-fold geometry.
The actual engine thermal-cap latch drives the FPS thermometer and menu warm-phone message.

See [MENU.md](MENU.md) for the C/Swift bridge, complete command table, pause semantics,
camera/hinge SDK findings, live preferences and integration hooks for the redesign.

`screen-shot <basename.png>` composes the game frame with visible UIKit overlays. A paused
capture uses the frame cached before suspension and never resumes the guest to capture it.
Detached library captures are rejected; use `menu return-to-game` first. The `shot` command captures the game through the same crop, orientation, scaling and aspect-fit
pass used for presentation, at the current drawable size. It excludes UIKit controls and window
chrome. Without a drawable it uses the swapchain dimensions as the output canvas, still applying
the crop. `DM_NVN_SHOT_OUT=WIDTHxHEIGHT` selects a comparison canvas and
`DM_NVN_SHOT_OUT=raw` explicitly requests the old full-texture diagnostic. Texture dumps remain
raw. External-display presentation remains unvalidated: the pinned engine's
`DmTVSceneDelegate` is compiled in and registers its iOS 27 scene accessory once a game starts,
but `App/GameOrientation.swift` routes an external display scene to it only while Developer
controls are on, until the maintainer checks a cable or AirPlay TV on the phone.

`test-frame-shot.py` runs the pinned engine's real Metal crop regressions and checks frame
screenshots, resizing, aspect fit, windowless capture, explicit output sizes and raw diagnostics.
Pass a private `--engine` copy with `frame-shot.patch` applied and a private `--output` directory.
Run under the shared heavy-build guard with `GAME` and the profile environment set.

The current build evidence, simulator screenshots and device-owner acceptance commands are in
`~/SatchelBuild/display2/report.md`. First-run history is in `~/SatchelBuild/display/report.md`.
