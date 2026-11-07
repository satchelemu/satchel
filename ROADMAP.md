# Roadmap

Where Satchel stands and what is left, with a rough completion percentage per area. The
percentages are the maintainer's judgment of how much of the end state is done and
verified on the phone, not a count of tasks. Updated 2026-11-06.

The end state: one sideloaded app. Import any Switch game you own from iCloud, prepare it
on the phone, and play it with a controller or touch at the frame rate the game expects,
with no computer involved.

## Progress by area

| Area | Done | Status |
|---|---|---|
| Import (XCI, XCZ, NSP, NSZ, split dumps, updates, DLC, artwork) | 92% | Works for the four tested games. Skip-layer encryption, SHA3 hash layers, IVFC trees of 3 to 7 layers and the sparse, compressed and patch combinations around them now read on the Mac against synthetic checks; Castlevania, and Odyssey and Zelda with their updates, still compose byte-identically to main on the Mac. The format table in the importer validation guide lists what is left: pre-release NCA versions, personalized tickets, rights-managed or sparse AES-XTS, NCZ skip-layer sections and DLC update (DataPatch) metadata. No real game with the new layouts has been seen, and none of this has run on the phone yet. |
| Prepare on the phone (compose, RomFS image, ARM64 rewrite, dylib, sign) | 94% | Byte-identical to the Mac pipeline for Zelda; runs end to end as a background task. After a clean reinstall on 2026-11-04, Zelda, Odyssey and Castlevania were all prepared on the phone with no Mac step and each booted from its phone-made package. The on-phone rewriter now follows the engine pin's `patch_nso.py` at nxnative `f8a5618d` (cd75caa). Each package records the rewriter revision, and a package made by an earlier rewriter is refused at launch with a message to Prepare again (unless all its modules are among those proven unchanged); the warm-up card for such a package offers Prepare again instead of Play (04afddb). On the phone on 2026-11-06, Odyssey 1.4.1 was prepared again with this build and ran through the unattended Quit cycle. Mac full package checks match main for Castlevania and Odyssey, and Zelda matches in output-discarding mode. The duplicate ExeFS verification read is removed, required IVFC block reads are batched, and metadata, file enumeration and dynamic-module progress callbacks are added. The large encrypted archive authentication and RomFS read passes remain separate; phone progress validation is still open. |
| Code signing on the phone | 90% | Works with the user's Apple Development identity. The app derives the accepted team from its own signature, so any developer account works; verified so far only with the maintainer's. |
| Shaders on the phone (translate, compile, cache) | 75% | All of Zelda's 71,621 shaders translate and compile on the phone; first use is cached per game. The phone translator follows the engine pin `f8a5618d` (96242d7) and the shader cache identity changed with it; on the phone (2026-11-06) Castlevania, Zelda and Odyssey 1.4.1 ran cold and warm on the new translator with no shader failures. Required first-use translation, library and pipeline waits still stall frames. Two opt-in pipeline modes in the game page reduce them without a skipped draw: async uses equivalent ready fallbacks (fixed-request translation and synthetic pixel parity verified against main on the Mac), and "Bounded (experimental)" starts translation and libraries when a program is bound, with no fallback. Bounded was measured on the Mac only, on cold opening routes: Zelda's worst frame went from 6.3 s (exact) to 2.4 s and Odyssey's from 245 ms to 132 ms; Castlevania was unchanged, because its slow frames were not shader work. Exact runs went first, so Metal's system cache favoured bounded in library and pipeline time (see NativeHost/ShaderStalls.md). All three titles pass cold and warm Mac opening routes in exact and async. Remaining: pipeline build stalls (3.2 to 3.5 s across Zelda's slow frames) and frames over 100 ms after a scene's first frame. On the phone on 2026-11-06 (title screen, no input), bounded cut Zelda's cold worst frame from 1,970 to 1,773 ms and shader stall time by about 12%, removing library waits but not pipeline waits of about 2.5 s. Those pipelines cannot be built earlier on a game's first launch: nothing is recorded yet (a phone-prepared package's seed list is empty), and the app never read the list it records. An opt-in launch prewarm (`-SatchelPipelinePrewarm YES`, Mac only so far) now builds a device's recorded pipelines before the first frame, preparing their shaders from the device's own capture; with an empty archive and the list kept (as after an OS update) Zelda's worst opening frame went from 2.7 to 3.0 s to about 0.36 s for about 1.9 s more loading, and warm launches lost their remaining shader waits. No phone result yet. Why Zelda's title frame still builds about 120 pipelines one at a time (Mac, 2026-11-07): its one prediction walk computes only 40 of their keys, so with every predicted build admitted the draws still built 81 themselves (108 by default), and four or six compiler lanes did not shorten it (submit-thread pipeline time 1.9 to 2.3 s against 2.8 to 4.5 s, under shared load); making the walk compute the draw's key comes first. A phone-prepared package has no pipeline data a Prepare-time seed could use (its shader and pipeline lists are empty), so a first launch cannot be seeded. Warm-up: the previous phone warm-up measured about 1.5 seconds per shader for Odyssey's 25,316. Bounded scheduling, identical-source reuse and faster MSL transforms preserve 760 fixed-request outputs on the Mac. Two-lane samples with primed Metal caches improve throughput 1.80x for Castlevania and 1.53x for Odyssey; extra foreground/charging lanes and phase timing are implemented. Cold phone warm-up speed and thermal measurements remain open (see NativeShaders/WarmupSpeed.md). The warm-up is opt-in: it starts only from the game page, never after Prepare, and Play is available while it runs. Geometry shaders translate through the engine's compute route (Odyssey's three geometry records matched the engine byte for byte on the Mac at the 2026-11-05 pin move). Hull and domain (tessellation) shaders now translate too (2026-11-07): Metal 4 has no tessellator, so the hull runs as a kernel, a compute tessellator makes the domain points and the domain shader runs as a vertex function; synthetic shaders drawn this way on the Mac GPU match a reference draw. The host does not draw patch primitives yet, none of the tested games uses them, and fractional spacing does not reproduce the Switch's exact vertex positions. |
| Horizon services | 63% | Nine IPC rounds plus SDK corrections from NX2Apple and Ryujinx (fs, hid, time, settings, account, applet, audio, parental control, friends, network stubs, play reports, DLC, and since round 11 performance mode, battery, random bytes, multimedia clocks and fatal reports). NativeHost/services/coverage/service-table.tsv lists all 237 services Ryujinx 1.3.3 registers against what Satchel answers: of the 46 an application may use, 38 reach a host session or an SDK takeover and 8 are open, each with a reason (none of them used by the tested games). Round 11 also takes over 34 SDK functions of the applet, parental control, DLC, data delivery and album families whose service the engine never opens, so a game calling them no longer reads a null object, as Odyssey 1.0.0 did for game play recording. Each new game reports what it still misses; all three phone-prepared games reported none on 2026-11-04. Kernel calls: transfer memory (create, map, unmap) and the other application SVCs Ryujinx 1.3.3 implements now have answers, 51 of its 72 SVCs in all, with the rest listed with a reason (NativeHost/services/coverage/svc-table.tsv). Odyssey 1.0.0's startup crash without its update was caused by the game play recording SDK calls (EnableGamePlayRecording, pause and restart), which now get high-level answers. On the Mac (new engine), Odyssey 1.0.0 without its update now boots past that crash, and Odyssey 1.0.0 and 1.4.1, Zelda and Castlevania reach their frame 300, 1200 and 3000 checkpoints with nothing missing; the phone check of the Odyssey 1.0.0 base alone is open. |
| Graphics (NVN on Metal) | 68% | Zelda, Odyssey, Castlevania and Mario Kart render correctly. Phone readings with the opt-in paired GPU and CPU frame timing, in the Zelda cave at 1.0 render scale: GPU 10.8 ms p50 and 16.1 ms p99, 30 FPS held; every over-budget frame was a 1 to 2 s first-use pipeline or library stall, not GPU time. Holding 30 FPS at 100% render scale across the game, beyond that cave, is not yet shown; anti-aliasing at low render scale is rough. Opt-in Mac pass attribution is validated; no new full-resolution speedup is established. Per-title MetalFX spatial presentation is opt-in and costs 3.3 to 6.5 ms on the phone. A Mac picture comparison against 1.0 in one session (Zelda's Room of Awakening, docked, the scale switched for three frames; 2026-11-07) shows that render scale itself dominates: MetalFX spatial moves reduced frames further from the 1.0 picture (SSIM 0.017 to 0.029 lower), and a new opt-in edge smoothing present (`-SatchelUpscaler edge`, 0.15 to 0.44 ms on the Mac) moves them slightly closer (SSIM about 0.0013 higher, 3D regions 0.3 to 0.6 dB) and straightens stair-stepped outlines but softens the HUD. Neither recovers detail lost at low scale; that needs the game's own 3D-view upscale replaced. None of Zelda, Odyssey and Castlevania calls NVN's discard commands, so they offer no store elision. |
| Performance and stability | 68% | Two hour Zelda session without a crash. Mac 30-minute Castlevania and retained-cache Zelda runs pass zero-leak checks. Nine same-process visits across the three titles on main cd6fbf9 show no launch hang or stop timeout. On the phone (2026-11-04), one app process played Castlevania, Odyssey and Zelda twice each through the menu's Quit and relaunch: six of six visits ended cleanly, each stop took 0.15 to 0.32 seconds, and memory at Quit was no higher in the second round. A Quit double free that could abort the app after Castlevania is fixed (the engine and the session reset both freed the same storage). On the phone on 2026-11-06 with main 96242d7, Castlevania and Zelda passed 4 of 4 unattended cold and warm visits, with Quit returning home each time, and Odyssey 1.4.1 passed 6 of 6 at 60 FPS with a warm p99 of 16.67 ms. Memory grows in steps on area loads, with no steady growth seen in the phone graphics session; memory growth within long play remains open. |
| Controllers and input | 90% | Xbox, DualSense and Pro Controller; touch controls; rumble, with per-title rumble strength and controller cutoff in the game page and the pause menu (awaiting hardware validation); phone gyro on by default. Controller mapping UI, category JSON profiles with global and title defaults, live stick testing, dead zone, menu shortcut with normal Plus/Minus taps and chord takeover and controller focus navigation pass Mac synthetic and simulator checks. A first physical Xbox-style pad (2026-11-06) did nothing in the app or in Castlevania; the fix round added launch-time controller discovery, a rescan on return to the app, focus and back in Settings and Add games, navigation for any controller profile, a hold-Menu shortcut for pads without Home or Options, and a "What iOS reports" panel in Settings > Controllers. The maintainer confirmed on the phone (2026-11-07) that a Bluetooth Xbox pad works in the app and in game. App navigation now has a console-style focus in place of the yellow outline (the lifted library tile, lifted cards, a sliding row highlight and brightened buttons, each following the target's shape), hold-to-repeat, a tick per move, an A/B hint and focus that hides on touch; it passes simulator checks and awaits a look on the phone. DualSense, Pro Controller and full hardware acceptance remain. |
| In-game menu | 90% | Pause, resume, library, quit, screen fit, FPS counter, DLC. Audio volume, touch size and opacity use the native bridge; the displayed size percentage is corrected. Controller layout and per-title remapping are available from the menu. Synthetic simulator checks pass; hardware acceptance remains. |
| iPhone Duo postures (closed, open, half folded) | 80% | Open and closed screens work. On 2026-11-05 the maintainer half folded the phone during a game on the real hinge: the picture turns upright on the top half at the full screen size and returns to landscape when opened; touch controls while half folded are not yet confirmed on the phone (the simulator journey taps through the turned window and passes). A half-folded game now turns upright, so the picture is on the top half and the controls on the bottom half (the hinge crosses the long side of the inner screen); app screens stay landscape. The app turns the game itself and fills the screen, because iOS only fits a portrait scene into the landscape screen as a small window. A synthetic simulator harness (Tests/Duo) covers the half-folded split, posture changes, both screens and the turn to upright and back from both landscape rotations. The game view follows the presented picture's shape; the Duo's own 1.43:1 picture shape needs per-game patches and is off and gated. The full simulator suite also covers the library, game page, Settings and pause menus closed, open and half folded, all six fold transitions and held touch input across them; real hinge events, screen migration and native frame presentation still need phone checks. |
| Library and app UI | 90% | Redesigned library with real artwork, import flow that keeps going in the background with iCloud download progress, home cards for import, Prepare, warm-up and a paused game, an opt-in shader warm-up on each game page (Prepare no longer starts it; a paused run from an earlier release waits for Resume), settings with the bundled install guide, signature expiry warnings, per-title settings and a dated save-backup list with restore, share-sheet export and import, and optional iCloud save sync through a chosen folder (awaiting hardware validation). The new settings and save journeys still need hardware validation. |
| Build reproducibility | 45% | All 45 declared native artifacts rebuilt locally from pinned cached inputs. Public synthetic shader parity and real staging passed; full unsigned-app validation and clean-Mac tool setup remain. See BUILDING.md. |
| Distribution (IPA, SideStore source, refresh) | 70% | Deterministic unsigned IPA export, hash manifests, source schema and screenshot metadata are validated on the Mac. Ad hoc re-signing preserves both requested memory entitlements. Installed-signature expiry policy and synthetic refresh journeys pass. Real installer capability preservation and refresh need phone validation; URLs remain placeholders and the manual CI draft is disabled. |
| General importer (unknown games boot without a per-game profile) | 52% | A game without reviewed settings now teaches itself in the app: after a starter launch the app reads that launch's memory and conditional rendering signals from the host log and uses the derived generic settings on the next launch (at most two corrections, bound to the package, never by title), and Game diagnostics has Copy diagnostics with only command names, counts, error codes and revisions. On the Mac (new engine, fresh Prepare with reviewed settings removed, the app's own feedback code), Odyssey reaches Cap Kingdom on the starter profile alone, Castlevania reaches its opening dialogue after one learned memory rule, and Zelda reaches a rendered Great Sky Island after a learned heap split and render conditions; small Zelda texture defects remain, with no generic signal for the texture switches yet. The kernel and recording calls the Odyssey 1.0.0 base needed to boot are answered for every game (Mac only). Phone validation of the feedback is open; see the generic profile diagnostics. |
| Compatibility | 4 games | See COMPATIBILITY.md. |

The simulator UI suite covers import cancellation and restart, system-interrupted
warm-up, a Prepare that finishes without starting warm-up, an earlier release's paused
warm-up that waits for Resume, paused-game Resume after backgrounding, and Prepare
availability after Quit.
These synthetic journeys do not replace phone checks of iCloud delivery, native session
teardown or shader cache reuse. See [the test guide](docs/testing.md).

## Engine update (2026-11-05)

The native engine pin moved from nxnative `bbc3b737` to `f8a5618d`, with the fork patch
queue now tracked in `scripts/deps/upstream/nxnative-queue` (29 patches). It is built and
checked on the Mac first. Every prepared package and every shader cache from before the
move is invalid by design (see [BUILDING.md](BUILDING.md#the-engine-pin)). The percentages
above were judged again on 2026-11-06, after the phone ran main 96242d7: Castlevania and
Zelda passed the unattended cold and warm Quit cycle, and Super Mario Odyssey 1.4.1, prepared
again, passed 6 of 6 visits at 60 FPS. Mario Kart 8 Deluxe has not been repeated on the new
engine, and the maintainer's hands-on checks on the new engine are still open.

Left open by the move:

- Done (2026-11-05), run on the phone 2026-11-06: the phone's shader translator moved from
  NX2Apple `8227f80f` to the engine pin, with `dmsh_build` ported again. Its output matches
  the engine's own tools on the Mac for Zelda, Odyssey and Castlevania; shader caches start
  empty once more, prepared packages stay valid. On the phone, Castlevania, Zelda and
  Odyssey 1.4.1 ran cold and warm on it with no shader failures. Switch 2 shaders stay
  refused. See [BUILDING.md](BUILDING.md#the-shader-translator-pin).
- Done on the Mac: the on-phone rewriter now reproduces `patch_nso.py` at `f8a5618d`, with
  upstream's stricter register-write proofs for jump tables, the padded and framed tail call
  forms, the frame rate target lookup (passed to the host's limiter through the fixups) and
  Snowdrop's page pools with each title's reviewed sizes. Every module of Zelda 1.4.3,
  Odyssey 1.0.0 and 1.4.1 and Castlevania 1.0.0 is byte-identical to the Python oracle, and
  their rewritten code did not change, so packages already prepared with this engine stay
  current. On the phone (2026-11-06), Odyssey 1.4.1 was prepared again with this rewriter
  and ran through the unattended Quit cycle.
- Upstream's multi-program games, `nn::jit` images, Switch 2 video codec and 120 FPS
  profiles are compiled in but not offered: the session reset refuses a second game in
  the same process while one of them is active, and Prepare does not produce their inputs.

## Help wanted

In the order the maintainer would pick them up. Each is self-contained enough to be a
pull request. Open an issue first if you want to claim one.

1. **Reproducible build on a clean Mac.** `scripts/bootstrap_deps.py` stages hashed
   inputs and wraps the existing native recipes. Five tool source archives have exact
   URLs and hashes. Finish the two missing distribution pins, validate standalone tool
   recipes, and repeat the full build on a clean Mac. Public synthetic shader validation
   is available, with the private real-game gate retained for maintainers. The local diagnostic path is not a clean-Mac build. See `BUILDING.md` and `scripts/deps/inputs.lock.json`.
2. **Second-account verification.** The team is now read from the build and the signed
   app (done 2026-11-03), but only the maintainer's account has exercised it. Build and
   sign with a different account, import an identity, prepare and launch a game, and
   report what broke.
3. **Asynchronous pipeline builds.** New shaders should compile off the frame thread
   with a bounded pause, so first play does not stall. The `shader-stalls` work is the
   starting point. An opt-in `async` mode now shares a 4 ms optional wait budget
   across the frame and uses equivalent ready fallbacks. Translation, library
   loading, descriptor resolution and required pipeline waits remain, so the full
   bounded-first-use goal is open. A second opt-in `skip` mode can omit only a
   terminal draw in the current submission, with a per-frame developer counter and per-title setting.
   It trades possible visible artifacts for fewer waits and needs phone validation. A third
   opt-in `bounded` mode (2026-11-06) moves translation and base libraries ahead to when the
   game binds a program and keeps prediction walks off unready programs, without changing any
   draw; on the Mac it cuts Zelda's cold translation waits by about 80 percent. What is left is
   pipeline compilation (bursts of new pipelines) and vertex shaders recompiled for wider
   padding; per-frame stall attribution for Mac routes and phone trials is in
   `NativeHost/stall_attribution.py`. Phone validation is open. On the phone, bounded removed
   Zelda's cold library waits but not its pipeline waits. An opt-in launch prewarm (2026-11-06,
   Mac only) builds the pipelines a device already recorded before the first frame; a game's
   first launch still builds its title scene's pipelines at draw time. On the Mac (2026-11-07), the prediction walk computes only about a third of the title frame's new
   pipeline keys, which caps what more compiler lanes can do; measurement knobs and per-draw counters are
   in `NativeHost/ShaderStalls.md` (Prediction coverage).
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
7. **Controller hardware acceptance.** Settings > Controllers provides a physical input
   list keyed to GameController names, one JSON profile per product category in the
   player settings plist, global defaults, a live tester and radial dead zone. Holding
   Home opens the menu; Menu + Options is the fallback and an explicit alternative.
   Home stays reserved. Plus/Minus taps reach the game; a complete chord suppresses both
   until both are released. The first button can reach the game before chord completion.
   D-pad and stick edges navigate
   the library, game actions, Settings, Add games and the pause menu. Settings >
   Controllers shows what iOS reports for each connected pad (name, category, profile,
   live presses and sticks) and the app writes connection events to
   `Documents/controller-log.txt`. Physical Xbox, DualSense and Pro Controller
   checks on the phone are still needed, including system Home gesture handling and
   disconnect/reconnect. See the controller checks in `docs/testing.md`.
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
   render scale, screen mode, haptics, rumble strength, rumble cutoff and pipeline mode. Each shows the global default,
   any title default, an override marker and reset. Launch and existing menu screen/haptics
   controls share the per-title store. FPS, scale and pipeline mode apply on next launch.
   Menu controls for those three launch-only choices are follow-up work for the menu owner.
10. **Save manager, hardware and other-emulator import remaining.** Game pages list save
    dates and sizes and the newest three automatic backups, restore with confirmation
    while the game is closed, export a ZIP through the share sheet and import through
    Files. Title mismatch and archive errors leave the current save intact. Only Satchel's
    title-bound ZIP format is supported; arbitrary emulator ZIPs remain unsupported.
    Optional iCloud save sync (Settings > Storage) keeps one packed save per game in a
    chosen folder; it is synthetic-checked on the Mac and the simulator and awaits a
    two-device check on hardware.
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

The unmerged memory-pressure candidate has 37 reviewed Mac scene pairs and 74
ownership/VM joins. Castlevania passes its strict pressure replay; Odyssey and
TOTK differ even without pressure. This evidence supports no further increase
to the current 68% performance/stability rating. See
[the measured scenes and remaining gate](Validation/memory-pressure-scenes-2026-11-04.md).
