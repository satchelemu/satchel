# Roadmap

Where Satchel stands and what is left, with a rough completion percentage per area. The
percentages are the maintainer's judgment of how much of the end state is done and
verified on the phone, not a count of tasks. Updated 2026-11-04.

The end state: one sideloaded app. Import any Switch game you own from iCloud, prepare it
on the phone, and play it with a controller or touch at the frame rate the game expects,
with no computer involved.

## Progress by area

| Area | Done | Status |
|---|---|---|
| Import (XCI, XCZ, NSP, NSZ, split dumps, updates, DLC, artwork) | 90% | Works for the four tested games. Compressed PartitionFS, newer encryption layouts and some bucket tree layouts still fail explicitly. |
| Prepare on the phone (compose, RomFS image, ARM64 rewrite, dylib, sign) | 88% | Byte-identical to the Mac pipeline for Zelda; runs end to end as a background task. After a clean reinstall on 2026-11-04, Zelda, Odyssey and Castlevania were all prepared on the phone with no Mac step and each booted from its phone-made package. Speed (the 17 GB file is read more than once) and plain progress for every step are open. |
| Code signing on the phone | 90% | Works with the user's Apple Development identity. The app derives the accepted team from its own signature, so any developer account works; verified so far only with the maintainer's. |
| Shaders on the phone (translate, compile, cache) | 70% | All of Zelda's 71,621 shaders translate and compile on the phone; first use is cached per game. Builds still happen on the frame thread in places, so first play stutters. The optional warm-up for a phone-prepared game translates every shader on the phone and is slow (about 1.5 seconds per shader for Odyssey's 25,316, so many hours); play is available while it runs. Geometry and tessellation stages are not translated. |
| Horizon services | 60% | Eight IPC rounds plus a measured SDK file-flush correction from NX2Apple and Ryujinx (fs, hid, time, settings, account, applet, audio, parental control, friends, network stubs, play reports, DLC). Each new game reports what it still misses; all three phone-prepared games reported none on 2026-11-04. |
| Graphics (NVN on Metal) | 65% | Zelda, Odyssey, Castlevania and Mario Kart render correctly. Zelda at 100% render scale does not hold 30 FPS yet; anti-aliasing at low render scale is rough. Opt-in Mac pass attribution is validated; no new full-resolution speedup is established. |
| Performance and stability | 60% | Two hour Zelda session without a crash. Memory growth between scenes and one post-session launch hang are open. |
| Controllers and input | 80% | Xbox, DualSense and Pro Controller; touch controls; rumble. Controller mapping UI and a controller shortcut for the menu are missing. |
| In-game menu | 75% | Pause, resume, library, quit, screen fit, FPS counter, DLC. Audio volume, touch control size and opacity, controller layout are missing. |
| iPhone Duo postures (closed, open, half folded) | 60% | Open and closed screens work; half folded layout and live relayout while playing are built but not fully verified. |
| Library and app UI | 85% | Redesigned library with real artwork, import flow that keeps going in the background with iCloud download progress, home cards for import, Prepare, warm-up and a paused game, shader warm-up on each game page, settings with the bundled install guide, signature expiry warnings, per-title settings and a dated save-backup list with restore, share-sheet export and import. The new settings and save journeys still need hardware validation. |
| Build reproducibility | 45% | All 45 declared native artifacts rebuilt locally from pinned cached inputs. Five tool source pins added; clean-Mac tool setup and private shader-parity evidence still prevent full app reproduction. See BUILDING.md. |
| Distribution (IPA, SideStore source, refresh) | 40% | Install guide and source generator exist; no published IPA or source URL yet. |
| General importer (unknown games boot without a per-game profile) | 50% | A starter profile is generated from the game's own metadata; Castlevania ran on it with one memory rule. More games will find more gaps. |
| Compatibility | 4 games | See COMPATIBILITY.md. |

The simulator UI suite covers import cancellation and restart, system-interrupted
warm-up, paused-game Resume after backgrounding, and Prepare availability after Quit.
These synthetic journeys do not replace phone checks of iCloud delivery, native session
teardown or shader cache reuse. See [the test guide](docs/testing.md).

## Help wanted

In the order the maintainer would pick them up. Each is self-contained enough to be a
pull request. Open an issue first if you want to claim one.

1. **Reproducible build on a clean Mac.** `scripts/bootstrap_deps.py` stages hashed
   inputs and wraps the existing native recipes. Five tool source archives have exact
   URLs and hashes. Finish the two missing distribution pins, validate standalone tool
   recipes, and provide public shader validation without bypassing the private parity
   check. The local diagnostic path is not a clean-Mac build. See `BUILDING.md` and `scripts/deps/inputs.lock.json`.
2. **Second-account verification.** The team is now read from the build and the signed
   app (done 2026-11-03), but only the maintainer's account has exercised it. Build and
   sign with a different account, import an identity, prepare and launch a game, and
   report what broke.
3. **Asynchronous pipeline builds.** New shaders should compile off the frame thread
   with a bounded pause, so first play does not stall. The `shader-stalls` work is the
   starting point.
4. **Missing services.** Pick a game, read its `missing-services.json`, and port the
   commands from Ryujinx 1.3.3 (`src/Ryujinx.HLE`, `src/Ryujinx.Horizon`, MIT) into
   `NativeHost/services`, keeping the MIT notice. No GPL code.
5. **Memory between scenes.** Bounded reclamation after scene changes (Mario Kart grows
   per race) without harming games that keep large resident sets. Two pieces: a memory
   pressure dispatch source that makes the host drop GPU staging buffers, stale pipeline
   caches and texture copies on warning; and a RAM donor app extension that allocates
   purgeable memory, shares it through `xpc_shmem_create`, and lets the app map it as
   guest RAM so it does not count against the main process footprint (a technique
   MeloNX 2.7 uses; MeloNX is GPLv3, so write it from Apple's APIs, copy nothing). The
   extension is a second target with its own bundle ID, so check signing with a free
   account first.
6. **Zelda at 100% render scale.** Profile the GPU cost per pass and find the 30 FPS
   budget at full resolution; no render scale tricks. The opt-in pass counters and
   workload and paired-trial summaries are in `NativeHost/graphics`. Rejected
   experiments are excluded. Mac gameplay attribution is available for
   Zelda and Odyssey; no full-resolution phone speedup is established. Validate
   matched pixels and GPU cost before promoting a candidate.
7. **Controller remapping and controller navigation.** A physical input list keyed to
   GameController input names, one JSON profile per controller product category, a
   settings page with a stick visualizer and dead zone, and a button combination that
   opens the pause menu without reaching the game. Then a small navigator that turns
   d-pad and stick edges into focus moves, so the library and the pause menu work
   without touching the screen. The per-title player profiles plist is the storage.
8. **Entitlement and memory self-check, hardware validation remaining.** Settings >
   Device check queries both entitlements in the running signature and measures
   remaining memory, the current process limit, address span and a bounded contiguous
   VM reservation. It also shows model, iOS version, team, profile expiry and container
   volume free space. Copy report omits the team by default. Simulator journeys cover
   both landscape orientations, clipboard contents with and without the team, missing
   and unknown entitlements, and the live simulator probe. Signed phone readings and
   other phones remain unverified. A measured limit alone cannot establish how much
   the entitlement increased it without a same-device baseline.
9. **Per-game settings, hardware validation remaining.** Game pages expose FPS target,
   render scale, screen mode, haptics and pipeline mode. Each shows the global default,
   any title default, an override marker and reset. Launch and existing menu screen/haptics
   controls share the per-title store. FPS, scale and pipeline mode apply on next launch.
   Menu controls for those three launch-only choices are follow-up work for the menu owner.
10. **Save manager, hardware and other-emulator import remaining.** Game pages list save
    dates and sizes and the newest three automatic backups, restore with confirmation
    while the game is closed, export a ZIP through the share sheet and import through
    Files. Title mismatch and archive errors leave the current save intact. Only Satchel's
    title-bound ZIP format is supported; arbitrary emulator ZIPs remain unsupported.
11. **Delete imports after a verified Prepare** (user option) so a game does not exist
    twice on the phone.
12. **More games.** Any title you own, with a report. See COMPATIBILITY.md.

## Not planned

- Running on the App Store, or anything that would need Apple's approval.
- Runtime JIT or a hypervisor. Code is rewritten, signed and loaded; it is never
  generated at run time.
- Vulkan as the rendering API on iOS. Metal is native; MoltenVK stays a compatibility
  layer for the parts that need it.
- GPL licensed emulator code (Eden, Citron, yuzu forks). It would change the license.

## History

The project started on 2026-10-31 as a no-JIT ARM64 interpreter experiment, moved to the
native execution route on 2026-11-01 after NX2Apple's approach proved out on the phone,
and had its first game prepared and played entirely on the phone the same day. The
technical history is in `PLAN.md`, the goal log in `GOALS.md`, and the evidence records
in `Validation/`.
