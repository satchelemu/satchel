> Maintainer's technical plan and history. The public summary is [ROADMAP.md](ROADMAP.md);
> how to build is in [BUILDING.md](BUILDING.md).

# Satchel plan (from 2026-11-01)

Goal: one sideloaded iPhone app. The user imports a game (XCI/NSP plus update) and
`prod.keys` from iCloud, the app prepares the game on the phone, and it plays at a stable
frame rate. No computer step per game.

Route: the native-execution design proven by NX2Apple/nxnative (Zelda TOTK already runs on
this phone through the Mac pipeline), with every per-game Mac step moved onto the phone:

| Mac step today | Phone replacement |
|---|---|
| Decrypt and extract XCI/NSP | Already done in Satchel (`App/SwitchExecutable.swift`, `SwitchCrypto.c`, `NSOLoader.c`, `SwitchRomFS.swift`) |
| `patch_nso.py` binary rewrite | Port to C/Swift (Capstone is C) and run on the phone |
| Shader translation and Metal libraries | Ryujinx translator (already on the phone via NativeAOT), SPIRV-Cross, runtime Metal compile, on-phone cache |
| Embed rewritten code in a signed app | Write a dylib, sign it on the phone with the user's certificate, `dlopen` it |
| Install a per-game app | None: one app, games load inside it |
| Generic host and NVN-on-Metal | Build NX2Apple's `dm/host` and `dm/gfx` into Satchel once |

## Direction: general importer first, profiles as tweaks (user decision 2026-11-01)

Any XCI/NSP should import and try to boot without game-specific work, the way desktop
emulators behave. Per-game profiles (memory layout, patches such as 60 FPS, workarounds)
are optional improvements layered on top, shipped as data with the app.
- General: content import, CPU rewrite rules, Horizon host services, NVN on Metal, signing,
  loading. A starter profile is generated from the game's own metadata (NPDM, NACP, module
  build IDs) when no reviewed profile exists.
- The one game-specific step today is offline shader discovery. It is replaced by runtime
  shader capture, translation and compilation on the phone, cached per game (M5).
- Failures are recorded precisely (missing service, crash address, shader) so each engine
  fix helps every game. Workers bringing up a new title report what the general path
  lacked, and that list drives the importer.

## Milestones

**M0. Safety.** Done 2026-11-01: git initialized, `/tmp` build dependencies copied to
`~/SatchelDeps/tmp-snapshot-2026-11-01`. `project.yml` now points at the durable snapshot (`~/SatchelDeps`), not `/tmp`.

**M1. Feasibility spikes (decide the route).** Must pass before big porting work.
- S1: an app loads and runs code from a dylib stored in its data container, signed by Mac
  with the team identity. Then the same with a dylib signed on the phone. Then code at a
  `vm_remap`ped address (needed by the nxnative loader).
- S2: memory capabilities under the signing team (increased memory limit, extended virtual
  addressing) and how much a test process can actually allocate.
- S3 (later): repeat S1 with a free Apple ID personal team.
- If S1 fails: fallback is JIT through a StikDebug-style enabler; reassess before porting.

**M2. Zelda stability (parallel, uses the existing Mac pipeline).** Profile the open-world
drops on the phone, then rebuild with the fix the profile points to (render scale, PSO
prewarm, latest upstream fixes, memory). This also improves the renderer Satchel reuses.

**M3. Integration design.** Map NX2Apple's host, loader, renderer, profiles and shader chain
onto Satchel: what links once, what runs per game on the phone, effort per piece.

**M4. First game launched from inside Satchel** using Mac-prepared shaders as a temporary
crutch, to prove rewrite plus sign plus load plus host on the phone. Zelda TOTK first
(profile exists), then Castlevania.

**M5. On-phone shader pipeline and caching.** Removes the last Mac step.

**M6. Onboarding and distribution.** In-app keys import, progress UI, SideStore source,
signature-expiry warnings, install guide.

**M3.5. Zelda launched from inside Satchel with a fully Mac-prepared package.** Proves
the host-as-library integration, real game modules loaded from a container dylib, and NVN
on Metal inside Satchel's own surface. Then each Mac step is replaced in turn (rewriter,
signer, extraction, shaders). Built as a dev variant (`com.dell.satchel.dev`) so the
installed Satchel and its library stay untouched until it works.

## Results so far

- S1a/S1b PASSED on the phone (2026-11-01): a Mac-signed dylib copied into the app's data
  container after install loads and runs; `vm_remap` of its signed page works with copy
  FALSE and TRUE. S2: increased-memory-limit and extended-virtual-addressing both granted;
  64 GiB reserved, 4.6 GiB touched. Report: `~/SatchelBuild/spike/report.md`.
- S1c/S1d (signing on the phone) built, awaiting the user's `.p12` import on the phone.
- M3 design done: `docs/integration-design.md`.
- 2026-11-01 01:40: the Mac crashed while three workers ran heavy builds at once and `/tmp`
  was wiped. Rule from now on: at most one heavy build (shaders, OpenSSL, full app) at a
  time, and builds use limited parallelism.
- M3.5 PASSED on the phone (2026-11-01 05:46): Satchel Dev verifies the 19.6 GB
  Mac-prepared TOTK package once (78 s), then launches to the title screen in about 7 s and
  plays saves. Open world (Great Sky Island) holds 28 to 30 FPS at the 30 FPS cap and 0.65
  render scale: about 97% of frames on time, footprint 3.8 GB, thermal 0. The remaining
  hitches (100 to 540 ms) are first-time pipeline builds (`pso_new`/`lib_new` in the
  slowframe rows); `pso_user.tsv` records them and the next launch prewarms them.
  Screenshots and input from the Mac go through the host's `DM_CTL` file
  (`~/SatchelBuild/host/ctl.zsh`); `devicectl` screen capture is black on this phone.
- Stability (2026-11-01 05:44 to 07:50): one Satchel Dev session ran Zelda for 2 hours
  (226,000 frames) at 30 FPS without a crash. Second session with the recorded pipeline list
  (772 rows prewarmed): hitches over 50 ms fell from 26 to 17 on a short route, worst from
  1.55 s to 0.42 s; nearly all remaining hitches are synchronous pipeline builds.
- User direction (2026-11-01): any Switch game, in one real app, with better performance.
  Vulkan stays out: on iOS it only runs through MoltenVK on top of Metal. Performance work
  goes to native execution, shader caching and asynchronous pipeline builds.
- Great Sky Island's orange-yellow grass matches the Switch (checked by the user).

## Current assignments

Device owner: the orchestrator only. Workers report "ready to install" with exact commands.

| Task | Worker | Status |
|---|---|---|
| M2 Zelda rebuild | `zelda` | superseded: Zelda now runs in Satchel Dev; the Mac-built app was removed |
| M3.5 Satchel Dev with Mac-prepared TOTK | `host` | passed on the phone: boots, plays saves, open world 28 to 30 FPS |
| Native port of `patch_nso.py` | `rewriter` | merged: TOTK 1.4.3 byte-identical to the Python oracle |
| Base plus update composition, titlekeys, `DMROMFS1` writer | `content` | merged: TOTK 1.4.3 byte-identical, full 17 GB image |
| On-phone signer fix (S1c/S1d failed: code signature invalid) | `spike2` | running: signing-variant matrix |
| M4 first half: on-phone Prepare action (content, rewrite, dylib writer, signer hook) | `prepare` | running |
| S1c/S1d signing matrix on the phone | user | installed; needs the user's identity import and passphrase on the phone |
| M5 spike A: phone Metal compiler replaces offline metallibs | `shaders` | running |
| Asynchronous pipeline builds in the host (no frame stalls on new shaders) | next worker | queued after `shaders` (same code) |
| Fill-screen modes with in-game toggle, 30/60 FPS and render scale settings, controller check | `display` (Codex) | running |
| Switch-inspired redesign: artwork library, import flow, settings | `redesign` (Opus 5.5 session, branch `app-redesign`) | running |
| More games: Super Mario Odyssey 1.4.1 (profile exists), Castlevania: Belmont's Curse (new profile), multi-game dev app | `games` (Codex, branch `more-games`) | running |
| App shell audit (first launch, import, Prepare, warm-up, game page, leaving a game): findings and fixes in `docs/app-audit-2026-11-03.md` | Opus 5.5 session | items 1 to 10 implemented; both simulator previews build and the targeted suite passes; signed Release build installed on the phone 2026-11-04 and the user checked the new import (iCloud progress, background, home card), warm-up and paused-game flows there |
| Rename to Satchel, part 2 | orchestrator | tracked-tree names updated; external relocation, dependency rebuilds and phone reinstall require separate validation |

## Rename to Satchel (clean-break decision 2026-11-03)

Part 1 set the home-screen name. Part 2 uses `Tools/Rename/rename_to_satchel.py`
on the final merged main, with every worker stopped. It updates project, targets,
schemes, source identifiers, launch options, user-facing strings, namespaces,
bundle IDs, Keychain services, distribution metadata and file paths together.
The app ID is `com.dell.satchel`; all variants use that prefix. The repo folder
is already `satchel`.

Apply order: verify external save backups, merge worker changes, stop all workers,
preview and apply the tracked-tree script, relocate build/dependency folders with
the external mover, repair worktrees, rebuild every renamed native dependency,
then validate the host and unsigned Release app. The orchestrator signs and
installs only after those checks pass. Read `Tools/Rename/PHONE.md` for the exact
backup, uninstall, provisioning and restore procedure.

This is a fresh installation. The user imports keys, the signing identity and
games again and runs Prepare again. There are no old-name aliases, old ABI exports
or rename migration. History under `Validation/history` and third-party code and
notices remain unchanged. Prior milestone notes are historical evidence, not a
promise that renamed artifacts have been built or tested on the phone.
