# Satchel native menu bridge

The native host owns session state and pause. The plain UIKit menu is the default.
`NativeMenuBridge.swift` is an ObservableObject adapter for the redesign's SwiftUI menu.
The dev cover in `App/HomebrewView.swift` has a notification hook to reattach a paused game
from a Mac command and an auto-launch guard for returning to the library. Retain both
hooks in the redesign integration.

## API

Import `InGameMenu.h` through `NativeHost/Bridge.h`. Call these APIs on the main thread:

- `SatchelMenuState()` returns a snapshot. Observe `SatchelMenuDidChange` for changes.
- `SatchelMenuPerform(action, value)` returns false for an invalid action or value.
- `SatchelMenuSetCustomView(view)` replaces the plain menu with a supplied view. Pass nil to
  restore it. Retain a UIHostingController yourself if supplying its view. The native
  bridge sizes that view to the current camera-aware interactive bounds and hides it
  when the menu is closed. SwiftUI should observe the bridge instead of owning pause.

`NativeMenuBridge` provides `snapshot`, `perform(_:value:)`, and `installMenuView(_:)`.
For example, `bridge.perform("screen", value: "fit")` applies immediately. Frame rate and
render scale are not player choices: each game runs as it expects (see Player profile data).

Snapshot keys: `state` (running, opening, menu, library, confirmQuit), `paused`,
`current` (launch settings), `nextLaunch`, `fps`, `opacity`, `size`, `haptics`, `volume`,
`rumbleGain`, `rumbleMin`,
`restartRequired`, `attached`, `thermalLimited`, `thermalMessage`.
`thermalLimited` and `thermalMessage` follow the engine's actual cap. `restartRequired` describes the engine's frame-cap/render-scale limitation,
not whether every preference differs. While `opening`, paused is false and input is blocked while capture and
pause finish. Menu changes do not write process environment variables.

| Action | Value | Effect |
| --- | --- | --- |
| open | none | Capture the last frame, pause, show menu |
| resume or close | none | Resume the attached game surface |
| library | none | Keep game paused, notify SwiftUI to dismiss to library |
| return-to-game | none | Ask the library to reattach the retained surface, still paused |
| quit | none | Show explicit quit confirmation |
| cancel | none | Cancel quit, stay paused in menu |
| confirm-quit | none | Drain commits, stop the session and return to the home library |
| screen | fit, stretch | Live picture and touch projection |
| fps | on, off | Live FPS counter visibility |
| opacity | decimal 0.2 through 1.0 | Live touch-layer opacity |
| size | decimal 0.75 through 1.25 | Live touch geometry and hit targets |
| haptics | on, off | Live touch feedback and game rumble |
| rumble-gain | decimal 0 through 3 | Live rumble strength (`DM_RUMBLE_GAIN`), saved for the title |
| rumble-min | decimal 0 through 0.9 | Live controller rumble cutoff (`DM_RUMBLE_MIN`), saved for the title |
| volume | decimal 0 through 1 | Live game audio gain, independent of device volume |
| layout-next | none | Cycle the engine's touch layouts |

Only open, resume/close, library and quit confirmation transitions work outside the menu.
Library cannot resume until its existing surface is reattached. The host retains exactly
one native controller. `PreparedGameView` recognizes that running session and reuses it.
`SatchelMenuSurfaceAttached` is called from the Swift representable on attachment/detachment.
`menu return-to-game` posts `SatchelMenuReturnToGame`; the dev cover reopens the retained surface.
Direct open/resume commands are rejected while the surface is detached.
Choosing the same title reuses the paused session. Choosing another title stops it
before preparing the replacement. Confirmed Quit enters `quitting`; failed
teardown enters `stopFailed` with Retry, retaining resources and rejecting resume.
Successful teardown enters `ended`, clears `PreparedLaunch.current`, releases the
menu model/capture and dismisses to home. No app exit is requested.
A menu quit cannot manufacture a game save. It waits for any native Commit already
in progress and preserves the last committed generation. Unfinished save.work is
never promoted. Save in game before quitting.

## Satchel's presented menu and the pause button

`SatchelMenu` (`NativeHost/SatchelMenuSession.swift`) does not hand its SwiftUI view to the bridge.
It installs a transparent, non-interactive placeholder with `SatchelMenuSetCustomView`, which keeps the
plain menu hidden, and observes `SatchelMenuDidChange`. When the state becomes `menu` or `confirmQuit` it
presents `InGameMenuScreen` (a NavigationStack whose close button is a toolbar item) over the front-most
controller in the game's window, `overFullScreen` with a cross-dissolve, so iOS 27.1 shows the status
bar and the vertical bar with the close button, as on the app's screens. `running` and `library`
dismiss it; the bridge keeps owning pause. If no controller can present it, or the presented menu has
no window one second later, the placeholder is removed and the plain menu shows. A controller's B
button resumes from the presented menu.

`DisplayControls.m` adds a small pause button (`game.pause`) to the game root: in the top row of the
touch controls, 12 points right of the + key, using the pad's cutout-aware control bounds and unit.
It stays while a controller is connected, dims to 0.32 after four seconds without touches, brightens on
any touch or controller connection, hides while the menu is up, and performs `open`.
`view-shot` draws the window, so it includes the button and the presented menu (the menu is presented
in the game's window); its log lists the presented controller chain.

## Input and pause

Two-finger double tap toggles fit/stretch. Three-finger double tap opens the menu. Its recognizer neither delays nor cancels normal
input, and allows simultaneous recognition. The constituent taps can reach the game before
the gesture is recognized. Ordinary controller buttons remain unmodified; holding Options
and Menu together for one second opens the menu. Those buttons also reach the game until
the combination is recognized. No singleton button is reserved. Ordinary touches are not delayed or cancelled, even while double-tap recognition is pending.

The plain menu supports controller D-pad up/down, physical bottom button to activate and
physical right button to resume/cancel. A custom view owns its navigation. Native physical
and touch input, shake input and new scripted presses are blocked while the menu is open.
Held controls are reset at opening and resuming. Controller polling uses a common-mode timer,
independent of the paused game and without replacing the engine's valueChangedHandler.

Pause uses the existing focus path: events 0 and 1 release inputs, freeze the guest clock,
suspend audio output and drain/suspend graphics. This is the host's existing background pause,
not a save state or a new CPU thread suspension mechanism. Active lifecycle events cannot
resume a menu/library-paused game. Resume uses events 2 and 3 only when the app is active.

The renderer can only capture a next present. Opening requests one before pause; after
500 ms the menu pauses even if no frame arrives. Composed captures while paused reuse the
captured frame. The log reports `game-frame=unavailable` when none was captured. They never
resume the game just to take a screenshot. A screenshot obtained while running is current.

## Engine findings

The pinned engine's `dm/host/loader.c` `dm_fps_state_init` writes the frame-step target and
render scale into the state page at startup. `dm/gfx/nvn/hle_nvn.c` caches `DM_FPS_TARGET` in
`g_present_cap`; game-specific FPS hooks also initialize at boot. There is no complete live
setter coordinating these paths. Writing environment values would not safely update them.
Both are therefore fixed per game at launch (PlayerProfiles.plist). Live screen mode remains atomic.
The new volume gain is read atomically by `hle_audio.c` while pulling PCM16 samples.

## Cutouts, screens and posture

The installed device SDK is iPhoneOS27.2, not a separate 27.1 SDK. The following public APIs
are marked available from iOS 27.1 in its installed UIKit headers:

- `UIViewReservedRegion.h`: active reserved regions, kind, frame and interactive margins.
- `UIView.h`: `reservedRegionsOfKind:`. `occlusionRegionKind` supplies per-side occlusions
  in the current view's coordinates. Frames already include interactive margins.
- `UIHinge.h`: unknown, closed, partially open, fully open, and angle in radians.
- `UIHingeInteraction.h`: an initial update and updates when hinge state or hierarchy changes;
  a nil hinge means no hinge information in that hierarchy.

`SatchelInteractiveBounds(view, region)` ignores symmetric horizontal safe-area insets on 27.1+,
but respects active occlusion frames on their nearest outer edge. Only that edge moves.
The pad adds an 8-point thumb margin; its existing shapes add their own clearance.
Top/bottom system safe areas remain. Controls, menu and FPS overlay share the same rule.
An empty reserved-region list means no reported active occlusion. No private camera APIs or
hard-coded Duo dimensions are used. Before iOS 27.1 only, the fallback assumes a camera on
the current screen's portrait-top edge and maps it to the landscape edge. That fallback
cannot establish an inner-screen camera position and is not the target-device acceptance path.

`UIWindowScene.screen`, `effectiveGeometry.interfaceOrientation`, `UIScreen.nativeBounds`,
`bounds`, `scale`, and the view's safe area describe the current screen. The headers expose
no cover/inner screen enum. Logs therefore report actual screen identity and geometry,
plus closed/open/half hinge posture rather than inventing a screen role.

Partially open puts the game in the top half and touch controls in the bottom half, with
the picture clipped at the half boundary. The control hit tests and game-touch projection use those
same rectangles. Root layout and hinge callbacks update immediately; a 250 ms geometry
observer also catches same-sized screen moves without replacing SwiftUI's scene delegate.
Geometry changes preserve held touch ownership and reproject game touches into the new
picture. Touch-up, cancellation and the existing menu/background resets still release
input. Both SwiftUI and plain menus use the lower half while partially open, leaving the
picture above. Simulator posture journeys observe the production pad callbacks and
layout over a synthetic frame; real hinge and screen-migration checks remain on the phone.
See [the posture test guide](../../docs/testing.md#duo-postures).
The guest/session is retained. `posture` commands below are explicitly synthetic overrides.

## Mac control

Use the existing runtime's `DM_CTL` path and `~/SatchelBuild/host/ctl.zsh`:

```zsh
/bin/zsh "$HOME/SatchelBuild/host/ctl.zsh" send 'menu open'
/bin/zsh "$HOME/SatchelBuild/host/ctl.zsh" send 'menu state' 'screen-shot menu.png'
/bin/zsh "$HOME/SatchelBuild/host/ctl.zsh" send 'menu screen stretch'
/bin/zsh "$HOME/SatchelBuild/host/ctl.zsh" send 'menu volume 0.5' 'menu fps off'
/bin/zsh "$HOME/SatchelBuild/host/ctl.zsh" send 'menu resume'
```

Every action in the table is a `menu <action> [value]` command. `menu state` prints JSON.
Wait for `state=menu` before sending settings after open. `screen-shot <basename.png>`
composes the game picture and visible UIKit overlays, including the paused menu.
`posture half`, `posture open`, `posture closed`, `posture auto` select or clear a test
layout override; logs always identify `source=test-override`. These cannot validate a
physical hinge sensor, screen migration, cutout geometry or rotation.

## Player profile data and thermal reporting

`NativeHost/PlayerProfiles.plist` supplies the defaults. `titles.<uppercase title ID>`
holds title defaults; the global entry uses target 0 (the game's own frame logic) and
render scale 1.0. Tears of the Kingdom defaults to 30 at 1.0. A forced target can change
game speed without a compatible frame-step patch. This is separate from the binary-rewrite
registry. There are no Performance or Graphics presets.

Game pages show FPS target, render scale, screen mode, haptics and pipeline mode, with
both global and title defaults and an override marker. `SatchelSavedPlayerSettings` merges
the defaults with validated per-title overrides. `SatchelPlayerOverrides` exposes only
those overrides, and `SatchelResetPlayerSetting` removes one without changing other choices.
`SatchelSavePlayerSettings` validates and merges a patch atomically. The existing screen
and haptics menu commands use the same store. Old menu JSON is not authoritative for them.

FPS accepts 0, 30 or 60; scale accepts finite decimal values from 0.5 through 1.0;
screen accepts fit/stretch; haptics accepts on/off; pipeline accepts exact/hybrid/legacy.
`SatchelQueuePlayerSetting` accepts these five keys. FPS, scale and pipeline changes affect
only `nextLaunch` until the game is closed and launched again. Screen and haptics also update
the live atomic state. No environment variable changes during a session. Adding menu
selectors for the three launch-only choices remains separate from the game-page work.

`-SatchelFPSTarget`, `-SatchelRenderScale`, `-SatchelScreenMode` and
`-SatchelPipelineMode` remain validated one-launch developer overrides and are never saved.
Pipeline startup reads the launch snapshot before configuring exact/hybrid/legacy behavior.

Saved zoom is rewritten to fit. `-SatchelScreenMode zoom` resolves to fit without persisting
any unrelated launch overrides. Live `screen zoom` is rejected and logs the allowed modes.
`screen next` cycles only fit/stretch.

The tiny `hle_nvn.c` overlay exports `nvn_thermal_limited()`, an atomic snapshot written by
`fps_step_cap`. It is true only when the engine thermal latch reduces an active target above
30 FPS. It remains true through the engine's 30-second cool-down hold, even after iOS reports
fair or nominal temperature. A requested 30 FPS cap, a movie by itself, disabled frame step,
or `DM_THERMAL_CAP=0` does not produce a thermal warning. UI polling never changes the cap.
The plain menu says "Phone is warm: limited to 30 FPS to cool down" while active, and a small
thermometer with 30 appears beside the enabled FPS counter. Custom SwiftUI menus should show
`thermalMessage` when `thermalLimited` is true. `SatchelMenuDidChange` fires on transitions;
`Satchel thermal: limited=1 cap=30 source=engine` and the corresponding limited=0 row are logged.

The display-only builder recompiles that one graphics translation unit, replaces its object
in this task's copied graphics archive, and verifies all other members are unchanged. Full
host builds use the same source overlay in SatchelNativeGraphics. Preserve this hook when
merging the shaders work; no `nvn_mtl.m` change is involved.

For device validation with the plain fallback, enable Developer controls and
launch with `-SatchelPlainNativeMenu YES`. `SatchelMenu.install` leaves the native
menu in place. Launch with `-SatchelPlainNativeMenu NO` to test the SwiftUI menu.
Both paths call the same `SatchelStopNativeSession` coordinator.

## Developer remote Quit

The control-file command `quit` calls `SatchelMenuRequestDeveloperQuit` on the main
thread. It requires Developer controls, opens the same menu, waits for asynchronous
pause and performs `quit` plus `confirm-quit`. Requests are tied to the menu generation
so delayed work cannot quit a replacement session. The prepared-game cycle uses the
same entry point. See [the cycle procedure](../../docs/phone-cycle.md).
