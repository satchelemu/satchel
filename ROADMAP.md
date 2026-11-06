# Roadmap

Where Satchel stands and what is left, with a rough completion percentage per area. The
percentages are the maintainer's judgment of how much of the end state is done and
verified on the phone, not a count of tasks. Updated 2026-11-05.

The end state: one sideloaded app. Import any Switch game you own from iCloud, prepare it
on the phone, and play it with a controller or touch at the frame rate the game expects,
with no computer involved.

## Progress by area

| Area | Done | Status |
|---|---|---|
| Import (XCI, XCZ, NSP, NSZ, split dumps, updates, DLC, artwork) | 92% | Works for the four tested games. Mac synthetic checks now cover compressed PartitionFS, key-area AES-XTS and a second bucket-tree index level. Skip-layer encryption, SHA3 and some storage combinations remain unsupported; the new layouts still need phone validation. |
| Prepare on the phone (compose, RomFS image, ARM64 rewrite, dylib, sign) | 90% | Byte-identical to the Mac pipeline for Zelda; runs end to end as a background task. After a clean reinstall on 2026-11-04, Zelda, Odyssey and Castlevania were all prepared on the phone with no Mac step and each booted from its phone-made package. Mac full package checks match main for Castlevania and Odyssey, and Zelda matches in output-discarding mode. The duplicate ExeFS verification read is removed, required IVFC block reads are batched, and metadata, file enumeration and dynamic-module progress callbacks are added. The large encrypted archive authentication and RomFS read passes remain separate; phone progress validation is still open. |
| Code signing on the phone | 90% | Works with the user's Apple Development identity. The app derives the accepted team from its own signature, so any developer account works; verified so far only with the maintainer's. |
| Shaders on the phone (translate, compile, cache) | 70% | All of Zelda's 71,621 shaders translate and compile on the phone; first use is cached per game. Required first-use translation, library and pipeline waits still stall frames; an opt-in async pipeline experiment uses equivalent ready fallbacks, with fixed-request translation and synthetic pixel parity verified against main on the Mac. All three titles pass cold/warm Mac opening routes in exact and async; phone hitch validation remains open. The previous phone warm-up measured about 1.5 seconds per shader for Odyssey's 25,316. Bounded scheduling, identical-source reuse and faster MSL transforms preserve 760 fixed-request outputs on the Mac. Two-lane samples with primed Metal caches improve throughput 1.80x for Castlevania and 1.53x for Odyssey; extra foreground/charging lanes and phase timing are implemented. Cold phone speed and thermal measurements remain open (see NativeShaders/WarmupSpeed.md). Play is available while warm-up runs. Geometry and tessellation stages are not translated. |
| Horizon services | 60% | Eight IPC rounds plus a measured SDK file-flush correction from NX2Apple and Ryujinx (fs, hid, time, settings, account, applet, audio, parental control, friends, network stubs, play reports, DLC). Each new game reports what it still misses; all three phone-prepared games reported none on 2026-11-04. Kernel calls: transfer memory (create, map, unmap) and the other application SVCs Ryujinx 1.3.3 implements now have answers, 51 of its 72 SVCs in all, with the rest listed with a reason (NativeHost/services/coverage/svc-table.tsv). On the Mac (new engine), Odyssey 1.0.0 without its update now boots past its startup crash, and Odyssey 1.0.0 and 1.4.1, Zelda and Castlevania reach their frame 300, 1200 and 3000 checkpoints with nothing missing; the phone check is open. |
| Graphics (NVN on Metal) | 65% | Zelda, Odyssey, Castlevania and Mario Kart render correctly. Zelda at 100% render scale does not hold 30 FPS yet; anti-aliasing at low render scale is rough. Opt-in Mac pass attribution is validated; no new full-resolution speedup is established. Per-title MetalFX spatial presentation is opt-in, with phone quality and cost acceptance pending. Opt-in paired GPU and CPU frame timing is implemented; phone readings remain unverified. |
| Performance and stability | 65% | Two hour Zelda session without a crash. Mac 30-minute Castlevania and retained-cache Zelda runs pass zero-leak checks. Nine same-process visits across the three titles on main cd6fbf9 show no launch hang or stop timeout. On the phone (2026-11-04), one app process played Castlevania, Odyssey and Zelda twice each through the menu's Quit and relaunch: six of six visits ended cleanly, each stop took 0.15 to 0.32 seconds, and memory at Quit was no higher in the second round. Memory growth within long play remains open. |
| Controllers and input | 90% | Xbox, DualSense and Pro Controller; touch controls; rumble, with per-title rumble strength and controller cutoff in the game page and the pause menu (awaiting hardware validation); phone gyro on by default. Controller mapping UI, category JSON profiles with global and title defaults, live stick testing, dead zone, menu shortcut with normal Plus/Minus taps and chord takeover and controller focus navigation pass Mac synthetic and simulator checks. Physical controller validation remains. |
| In-game menu | 90% | Pause, resume, library, quit, screen fit, FPS counter, DLC. Audio volume, touch size and opacity use the native bridge; the displayed size percentage is corrected. Controller layout and per-title remapping are available from the menu. Synthetic simulator checks pass; hardware acceptance remains. |
| iPhone Duo postures (closed, open, half folded) | 80% | Open and closed screens work. On 2026-11-05 the maintainer half folded the phone during a game on the real hinge: the picture turns upright on the top half at the full screen size and returns to landscape when opened; touch controls while half folded are not yet confirmed on the phone (the simulator journey taps through the turned window and passes). A half-folded game now turns upright, so the picture is on the top half and the controls on the bottom half (the hinge crosses the long side of the inner screen); app screens stay landscape. The app turns the game itself and fills the screen, because iOS only fits a portrait scene into the landscape screen as a small window. A synthetic simulator harness (Tests/Duo) covers the half-folded split, posture changes, both screens and the turn to upright and back from both landscape rotations. The game view follows the presented picture's shape; the Duo's own 1.43:1 picture shape needs per-game patches and is off and gated. The full simulator suite also covers the library, game page, Settings and pause menus closed, open and half folded, all six fold transitions and held touch input across them; real hinge events, screen migration and native frame presentation still need phone checks. |
| Library and app UI | 90% | Redesigned library with real artwork, import flow that keeps going in the background with iCloud download progress, home cards for import, Prepare, warm-up and a paused game, shader warm-up on each game page, settings with the bundled install guide, signature expiry warnings, per-title settings and a dated save-backup list with restore, share-sheet export and import, and optional iCloud save sync through a chosen folder (awaiting hardware validation). The new settings and save journeys still need hardware validation. |
| Build reproducibility | 45% | All 45 declared native artifacts rebuilt locally from pinned cached inputs. Public synthetic shader parity and real staging passed; full unsigned-app validation and clean-Mac tool setup remain. See BUILDING.md. |
| Distribution (IPA, SideStore source, refresh) | 70% | Deterministic unsigned IPA export, hash manifests, source schema and screenshot metadata are validated on the Mac. Ad hoc re-signing preserves both requested memory entitlements. Installed-signature expiry policy and synthetic refresh journeys pass. Real installer capability preservation and refresh need phone validation; URLs remain placeholders and the manual CI draft is disabled. |
| General importer (unknown games boot without a per-game profile) | 50% | Fresh Mac Prepare with reviewed settings removed reaches Odyssey's Cap Kingdom on the starter profile alone and Castlevania gameplay with a generic alias-pressure rule. Bounded boot observations derive memory rules without title lookups. Automatic phone feedback and generic graphics corrections remain open; see the generic profile diagnostics. |
| Compatibility | 4 games | See COMPATIBILITY.md. |

The simulator UI suite covers import cancellation and restart, system-interrupted
warm-up, paused-game Resume after backgrounding, and Prepare availability after Quit.
These synthetic journeys do not replace phone checks of iCloud delivery, native session
teardown or shader cache reuse. See [the test guide](docs/testing.md).

## Engine update (2026-11-05)

The native engine pin moved from nxnative `bbc3b737` to `f8a5618d`, with the fork patch
queue now tracked in `scripts/deps/upstream/nxnative-queue` (29 patches). It is built and
checked on the Mac only. The percentages above were judged on the previous engine and are
unchanged until the four games are prepared and played again on the phone: every prepared
package and every shader cache from before the move is invalid by design (see
[BUILDING.md](BUILDING.md#the-engine-pin)).

Left open by the move:

- The phone's shader translator still follows NX2Apple `8227f80f`. Upstream's translator
  fixes since then (denormal comparisons, bank selection at run time, storage words read
  whole, Switch 2 shaders) need that pin to move and `dmsh_build` to be ported again.
- Done on the Mac: the on-phone rewriter now reproduces `patch_nso.py` at `f8a5618d`, with
  upstream's stricter register-write proofs for jump tables, the padded and framed tail call
  forms, the frame rate target lookup (passed to the host's limiter through the fixups) and
  Snowdrop's page pools with each title's reviewed sizes. Every module of Zelda 1.4.3,
  Odyssey 1.0.0 and 1.4.1 and Castlevania 1.0.0 is byte-identical to the Python oracle, and
  their rewritten code did not change, so packages already prepared with this engine stay
  current. Not yet run on the phone.
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
   It trades possible visible artifacts for fewer waits and needs phone validation.
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
   the library, game actions and pause menu. Physical Xbox, DualSense and Pro Controller
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
to the current 65% performance/stability rating. See
[the measured scenes and remaining gate](Validation/memory-pressure-scenes-2026-11-04.md).
