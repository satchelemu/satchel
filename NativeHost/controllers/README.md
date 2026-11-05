# Controller integration

`overlay.py` layers controller behavior after the display, session and service overlays. The pinned engine stays unchanged. The app keeps its controller objects and slot table across guest sessions, then republishes the physical mask when a new guest starts.

Player 1 uses Handheld with touch controls and No1 FullKey with a physical controller. A game that only accepts No1 uses FullKey for touch as well. Supported style and ID changes signal the affected npad events, including Handheld. Player 2 keeps No2 while player 1 disconnects and returns to touch. Replacing the second controller fills player 2's free slot. Disconnecting an assigned controller opens the menu and pauses immediately. Reconnecting leaves the game paused until Resume.

The pinned engine already implements the native SDK HID vibration entry points, device handles, two motors per player, amplitude and frequency sanitization, and GameController/Core Haptics output. Satchel reuses those paths. This task adds a bounded output queue, immediate hardware suspension for the menu and lifecycle, and a per-title vibration preference alongside screen mode. It preserves old global haptics preferences as a migration default. Controller haptics use advertised localities, with the existing default-locality and plain-player fallbacks. A controller without haptics remains silent. Touch uses the phone's Core Haptics engine.

The output queue holds 128 normal snapshots. Under overload it retains one latest snapshot per player. Normal short pulse edges stay ordered; overload can coalesce intermediate effects. Existing epochs invalidate old routes and suspended work. Resume requires a fresh game send. Core Haptics intensity and sharpness approximate Switch vibration bands; these are not HD Rumble waveforms.

The native execution path calls SDK takeovers, not a new HID shared-memory service. The synthetic HID checks call those exact entry points. Raw `hid` HIPC shared-memory setup and Joy-Con splitting are outside this change.

## References and license

- nxnative engine `f8a5618d5f505a4c0a0bcac7c413872d3a53c009`, MIT: `dm/host/hle_hid.c`, `hle_haptics.c`, `haptics_apple.m`, `pad_slots.h`, and `dm/ios/Sources/DmGamepad.m`.
- Ryujinx 1.3.3, copyright Ryujinx Team and Contributors, MIT: `src/Ryujinx.HLE/HOS/Services/Hid/HidDevices/NpadDevices.cs` for accepted styles and style-event transitions; `IHidServer.cs` commands 200, 201, 202, 203, 205, 206 and 211 and `HidServer/Types/Vibration` for vibration layout and routing review. The existing SDK layout already matches these structures. MIT text is retained in `../services/RYUJINX-LICENSE.txt` and bundled as `NativeAssets/Notices/Ryujinx-services.txt`.
- Installed iPhoneOS27.2 SDK: `GCDeviceHaptics.h` and `CHHapticEngine.h` verify controller engine creation, localities, and phone audio-session initialization.

## Mac checks

From the session worktree:

```sh
ANYGAME_OUT="$HOME/SatchelBuild/session/pad-guard" python3 -B NativePrepare/guard.py python3 -B NativeHost/controllers/check.py
ANYGAME_OUT="$HOME/SatchelBuild/session/pad-guard" SATCHEL_OUT="$HOME/SatchelBuild/session/pad-app" python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py --unsigned
```

The guard uses nice 10, serializes heavy work, stops before the 35 GiB disk reserve, and stops any owned process at 9 GiB RSS, below the requested 10 GiB cap. The canonical app build uses two jobs. The checks use synthetic controller state and fake haptic engines, with HID-device enumeration replaced by an empty synthetic set. No hardware input, keys, packages or game files are read.

The check suite covers independent players, style events, supported-ID changes, stale vibration handles, invalid values, unsupported haptics, disconnect latches, nested suspension, a 10,001-send flood, Apple engine failure/fallback paths, and saved vibration/screen settings across titles and processes. Phone acceptance steps and actual results are in `~/SatchelBuild/session/report.md`.
