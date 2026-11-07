# Anygame host extension

Format 2 packages reconstruct the runtime profile from a bounded metadata recipe.
The existing signature, manifest, module, path and loader checks remain mandatory.
The first-use hook uses owned NVN controls, the M5 native translator/cache APIs and
Metal source compilation. Mutable output stays under the title's NativeRuntime
shader-cache directory. It never changes the signed package's empty inventory.
The recipe identifier `runtime-capture-v1` describes that immutable inventory;
it does not claim the Mac offline compiler's provenance.

New vertex padding invalidates shader, serial and composite program caches. Old
shader objects remain alive because the pinned pipeline keys include their addresses.
The adapter refuses more than 4,096 retained revisions with a diagnostic. A delayed
GPU mapping is retried on use. Source memory is bounded to 64 MiB, its disk cache to
2 GiB and live compiler libraries to 64. Host pipeline caches retain their existing
engine limits. Translation is serialized; the compiler permits at most two jobs.
Pinned default query facts are used, with observed fragment input requirements.
Geometry records take the engine's compute route. Hull and domain records translate
(NativeShaders/RuntimeIntegration.md, tessellation handoff), but the host does not
draw patch primitives yet, so those draws are still skipped. Runtime observations do not
prove full-title shader coverage.

`NativeGameReport` exports safe stage, service/SVC, symbolic crash and shader fields.
Capture payloads and shader source stay private. First frame and gameplay are not
inferred from submission counts. The sections below are historical host milestones.

# Satchel M3.5 native host

The `host-in-app` worktree adds a dev-only Workshop action, **Dev: launch prepared game**, to Satchel's existing library screen. The Release app is `com.dell.satchel.dev`, display name **Satchel Dev**, the maintainer's team, landscape only. The normal target is unaffected. The dev target resolves its existing binary dependencies through the durable dependency snapshot.

Build products, generated Xcode project, source snapshot, signed game container and prepared package live under `~/SatchelBuild/host/`. No game content belongs in this worktree. The source pin is nxnative `f8a5618d5f505a4c0a0bcac7c413872d3a53c009` with exactly the 29 fork patches of `scripts/deps/upstream/nxnative-queue`, materialized by `scripts/bootstrap_deps.py`. The build verifies the copied runtime sources against that tree before applying the two documented diagnostic overlays. It does not modify the shared engine.

```zsh
cd "$HOME/SatchelBuild/host/wt"
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/build.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/verify_build.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/package.py
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/check_package.py
/bin/zsh NativeHost/transfer.zsh
```

`transfer.zsh` prints install, transfer and launch commands. It performs device actions only with `--execute`, which is reserved for the orchestrator/device owner. This worker does not install or launch. `build.py` checks Zelda's state and process list and refuses heavy compilation while its rebuild is active. Xcode uses two jobs. Package creation refuses an incomplete or mismatched new-chain shader build.

## Architecture and lifecycle

Three static libraries retain the pinned host, graphics and media source sets. Explicit force-load flags retain `swhle`, `swforce`, assembly gates and registration constructors in the app executable. The generated inputs follow the pinned Makefile, including its Metal-backed Vulkan/OpenGL compatibility source; TOTK uses NVN on Metal. No new Vulkan rendering route is introduced.

`SessionBridge.m` supplies the session boundary, the upstream frontend's boot functions and a no-launcher adapter. `front_ios.m` and `DmGamepad.m` provide the embedded view controller, CAMetalLayer, touch/controller input, haptics, audio interruptions, keyboard, power notices and shader warm-up progress. SwiftUI owns the app entry and presentation. The guest starts on a 64 MiB-stack pthread only after the surface exists. SwiftUI scene changes forward to the upstream focus/audio/GPU suspension hooks. If package verification finishes in the background, guest boot waits until the app is active.

The canonical app now supports successive native sessions in one process. Quit
joins guest/native work, retires graphics and services, releases guest mappings
and the dylib, then returns to the library. Library pauses and retains the same
session. Starting another title first stops the retained one. See
[session lifetime contract](session/README.md) for ordering, failure retention,
save semantics and validation. Real Mac switching is verified; the iOS signed
dylib reload journey requires the orchestrator's phone test.

## Package contract

The app looks only at `Documents/PreparedGames/TOTK-1.4.3/`:

| Path | Purpose |
| --- | --- |
| `manifest.json` | Format, title/version, source pin, chain/input identity, ordered modules, resolved environment, layout metadata and every file's byte count/SHA-256 |
| `game.dylib` | Separate Apple Development signed, constructor-free module container |
| `profile.tsv` | All runtime profile fields exported by the pinned `game.py profile` |
| `dm/` | Fixup/gate tables, shader map, PSO seed/pairs and shader analysis |
| `msl/b-26dc2fa9477c/` | iPhoneOS metallibs, reflection JSON and matching `inputs.tsv` |
| `romfs.img` | Existing composed `DMROMFS1` image, APFS-cloned on the Mac |

The app hashes all package files on a background task before loading, including the large RomFS. Initial verification can take several minutes. It rejects missing, altered, extra or escaping files and mismatched identity/settings. The manifest is copied last. Keep the app closed during transfer and replacement. A partial copy cannot launch successfully.

The packer uses the pinned `gen_embed.py` to split x0/W/x1/x2, links only those sections and a data marker into an arm64 dylib, signs with `--timestamp=none`, checks the signature, rejects initializers/dependencies beyond libSystem, and compares every packed byte with its rewritten input, including padding. The app checks the Mach-O before `dlopen`, resolves the marker with `dlsym`/`dladdr`, verifies its image path and hands that header to the existing `dm_set_game_image_header` hook. Guest imports/relocations remain the Horizon loader's job. Host registration lookups continue to use the main executable header. The app has no embedded TOTK module sections.

TOTK's exact resolved runtime profile is passed through `DM_PROFILE` before host initialization. The fork's runtime-profile-every-site patch covers the design document's previously missing fields. The upstream generated default header remains a fallback for compilation, but the bridge requires a verified explicit profile. Session settings match Zelda: 30 FPS, render scale 0.65, docked, prewarm enabled, Metal 4 disabled and thermal cap enabled. The package uses the Zelda worker's exact launch-environment JSON. No settings are changed to improve a build result.

The original TOTK profile accepts three prepared NSO modules and refuses an unexpected `nro.tsv`. Profiles may additionally review a complete NRO inventory with `nroInventorySHA256` and `nroBuildIDs`. The packager reuses the pinned engine's packed NRO sections and verifies their table, permissions and contents. Dynamic modules stay separate from the ordered startup modules, so the existing loader activates them only when the game asks. `test_nro_package.py` exercises synthetic packed modules and the actual Swift verifier; its output directory must be private and new. Device execution remains a separate validation step.

Mutable state stays under `Documents/NativeRuntime/0100F2C0115B6000/`: saves, temporary files and an empty DLC directory. Existing Satchel imports and library data are untouched; this dev app has its own sandbox. Memory TSV uses the upstream Documents-relative path convention. There is no existing save migration, DLC, cloud-save selection, profile picker or on-phone preparation/signing in this milestone.

## Deviations from upstream

1. Satchel owns app startup and the library UI. The upstream app delegate, launcher and game-resource discovery are replaced by the explicit prepared-package bridge. The native surface and input code are reused unchanged.
2. The guest container is a loose signed dylib in the data container, loaded after install. The upstream external-image header hook is reused unchanged. No game framework or game sections are linked into the app.
3. The app requires an explicit verified runtime profile, ordered modules and paths before starting the host, with the Zelda worker's fixed diagnostic environment.
4. `memstat.m` is copied to an external overlay that exposes its existing memory sampler as `satchel_memstat_start`; it is started at session preparation instead of an app-load constructor, so the session's environment takes effect. Other host constructors retain upstream behavior.
5. `crash.c` is copied to an external overlay omitting raw register-target and stack-byte dumps. Signal information, symbolic offsets and backtraces remain. This prevents those paths from writing private guest bytes to diagnostics. No generated game bytes are read into build logs.
6. Cloud saves and the upstream launcher are explicitly off. The existing local-save implementation remains. External-display scene registration is not configured for the Satchel app in M3.5, so TV behavior is unvalidated.
7. A digest-checked OFL Noto Sans font replaces upstream's macOS Arial Unicode copy. It has narrower coverage. The generated extension font is retained. Notices ship in NativeAssets/Notices.
8. The dev app adds extended virtual addressing alongside increased memory limit, uses iOS 26 as its minimum because the pinned renderer references Metal 4 types, and retains landscape only.

## Local checks

```zsh
PYTHONDONTWRITEBYTECODE=1 python3 NativeHost/check_container.py
xcrun --sdk iphoneos clang -target arm64-apple-ios26.0 \
  -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -fobjc-arc -fsyntax-only \
  -I "$HOME/SatchelBuild/host/engine/dm/host" NativeHost/SessionBridge.m
xcrun swiftc -typecheck -sdk "$(xcrun --sdk iphoneos --show-sdk-path)" \
  -target arm64-apple-ios26.0 -import-objc-header NativeHost/SessionBridge.h NativeHost/PreparedGame.swift
/bin/zsh -n NativeHost/transfer.zsh
```

The synthetic container check covers all split variants, padding, constructor rejection and an altered module byte. `verify_build.py` checks signatures, both granted/provisioned memory entitlements, bundle identity, orientation, retained host symbols/registration sections and absent game module sections. None of these checks establishes device execution or playability. The device owner must validate the actual prepared TOTK package through the library action, title screen and gameplay separately.
