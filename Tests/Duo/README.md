# iPhone Duo layout harness

XCTest coverage for the half-folded layout and the orientation contract, adapted from
nxnative's `dm/ios/tests/duo` harness and `test_orientation_policy.py` (MIT, upstream
e2fadf22 and 600c0b37). The host app links Satchel's production `DisplayControls.m`,
`DisplayGeometry.h`, `PlayerSettings.m`, `InGameMenu.m` and the engine gamepad with the same
display and controller overlays the app uses, plus inert host stubs (`HarnessApp.m`).
The fixture `game_profile.h` carries no game data.

`DuoLayoutTests.m` drives the posture through the production `posture` test override
(the same command the developer control file accepts) and checks:

- open and closed: the 16:9 picture overlays the whole screen;
- half folded: the picture fits the top half, centred, and the face buttons, stick and
  d-pad and the whole control area sit below the fold; stretch fills only the top half;
- each posture change relays out at once without a resize and keeps held input owned;
- an unknown override is rejected and `auto` returns to UIKit's report;
- touch projection uses the presentation rectangle and clamps below the fold;
- fold regions tile the inner screen, the cover screen and an odd height;
- a camera beside either half, in either landscape rotation, narrows only its own half;
- the real window: half folding a game turns the window a quarter inside the landscape
  scene, upright-sized and still covering the whole scene, with the picture on the top half
  and the controls on the bottom half; opening it restores the plain window, from landscape
  right and left;
- only a game surface in a window turns: without one, or in a fixture that draws its own
  screen, the window stays plain.

`test_orientation_policy.py` checks that every app and preview declaration is exactly
landscape left and right, full screen, with no iPad override, that no app or host source
asks the system for an upright scene, and that the one upright rule and the one window turn
live in `NativeHost/DisplayControls.m` (a game on screen while half folded).

Run it on a private simulator, never the shared one:

```bash
xcrun simctl create "Satchel Duo harness" com.apple.CoreSimulator.SimDeviceType.iPhone-Duo com.apple.CoreSimulator.SimRuntime.iOS-27-2
```

```bash
SATCHEL_DUO_SIMULATOR=<udid> python3 -B Tests/Duo/run.py
```

Products go to `~/SatchelBuild/duo-harness` (or `SATCHEL_DUO_OUT`). The harness uses
synthetic postures and touches. It does not read the hinge sensor, the system's own
hit-testing, a game or the renderer, so the half-folded layout still needs a check on the
phone.
