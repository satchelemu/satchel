# Building Satchel from source

As of 2026-11-04, a clean Mac still cannot reproduce the complete app. The dependency
bootstrap below reconstructs pinned source inputs and reuses the native recipes in a
fresh root. Five tool source archives now have exact URLs and SHA-256 pins, but their
standalone recipes and two remaining distribution pins are not yet reproducible.
Shader staging now has a public synthetic gate with source and binary receipts.
The public gate and real iOS shader staging passed against a fresh local bootstrap
root. The bootstrap fails closed instead of downloading unpinned tools or claiming
that an incomplete root is usable. Signing is account-neutral.

Settings > Device check adds no dependency. Its native probe builds with the iOS SDK;
the running-signature query resolves Security SPI dynamically and reports Unknown if
unavailable. The simulator harness exercises the probe and synthetic report journeys.
Signed device readings still require the checks in [docs/testing.md](docs/testing.md).

## Dependency bootstrap (incomplete)

The entry point is:

```sh
python3 -B scripts/bootstrap_deps.py --root "$HOME/SatchelBuild/deps"
```

It currently exits 2 with the missing clean-Mac input pins. The tracked inventory is
[scripts/deps/inputs.lock.json](scripts/deps/inputs.lock.json): source URL, revision or
version, license, SHA-256 and explicit gaps. File hashes cover the downloaded bytes.
Git hashes cover `git archive --format=tar COMMIT`, verified after fetching the pinned
commit and before checking it out. The separate `tool_sources` list records upstream
source archives pinned from installed package metadata, not downloaded or rebuilt tools.
Each metadata file has its own provenance hash. Dirty source-cache files and prebuilt libraries are
never used as Git inputs. The two NX2Apple pins have distinct roles: the current
engine and patch queue, and the older translator reference required by its source lock.

`--fetch --stage-only` fetches only those inputs that already have hashes, then exits 2
while the inventory has pin or recipe gaps. This stages the tool source archives too,
but does not build or install them. This network path has not been exercised in this round.
No URL or hash is silently learned from a download. Without `--fetch`, outbound IP connections
are denied for the bootstrap and its build children with macOS `sandbox-exec`; local
MSBuild IPC remains available.

The local diagnostic path uses installed tools and an explicit SDK. It does not qualify
as a clean-Mac build. For the maintainer's read-only caches:

```sh
SATCHEL_BUILD_ROOT="$HOME/SatchelBuild" SATCHEL_DEPS_ROOT="$HOME/SatchelDeps" \
  nice -n 10 python3 -B scripts/bootstrap_deps.py \
  --root "$HOME/SatchelBuild/anygame/deps-parity" \
  --cache "$HOME/SatchelBuild/rename/inputs" \
  --cache "$HOME/SatchelBuild/rewriter" \
  --cache "$HOME/SatchelBuild/spike" \
  --cache "$HOME/SatchelBuild/rename/final-app/host/NativeAssets/font" \
  --source-cache "$HOME/SatchelDeps" \
  --dotnet "$HOME/SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/dotnet/dotnet" \
  --compare "$HOME/SatchelDeps" --local-validation
```

These paths are explicit cache arguments, not bootstrap defaults. Use a new empty root
for each attempt. The bootstrap retains `bootstrap-manifest.json`, `output-manifest.json`
and per-step logs, including failures and missing outputs. Exit 2 means incomplete, even
if individual recipes pass. The existing heavy-build guard waits for active builds,
limits each process to less than 9 GiB RSS and protects a 35 GiB disk reserve. Recipes
use two jobs. The dependency layout retains the historical `tmp-snapshot-2026-11-01`
subdirectory because the app still references it; its libraries are built afresh.

The captured compiler README checkpoint has been reconciled against the exact reviewed
bytes from the archived history: only the title separator changed during the public
release. Its lock now includes that punctuation change and the review note. All 249
source files match their checkpoints; compiler and validation code are unchanged.
The local fresh-root rebuild produced all 45 declared dependency artifacts, with
zero missing outputs. The CPU suite passed 5,837,760 checks; scalar gates passed
853,611 compiler and 20,365,499 admission assertions; captured compilation passed
its cold, mutation, typed-schema and 696-module SPIR-V gates. These are Mac synthetic
executions and iOS compilation, not phone execution. The earlier bootstrap comparison
found 78 identical files, 44 differing files and three archives absent from the
reference root.
The observed differences are archive metadata, build paths, managed module identifiers
and reviewed notice/checkpoint changes. This is not bit-for-bit reproducibility.

The bootstrap was repeated on 2026-11-05 for the engine pin move (nxnative `f8a5618d`,
see "The engine pin" below), into a fresh root from the same caches plus clones of the
two engine repositories. Every step exited 0, all 19 inputs verified, 125 output files
with none missing, the same CPU and scalar gate counts, public shader parity passed on
its 23 requests against the regenerated lock, and the comparison with the previous
dependency root again found 78 identical files, 44 differing and three absent there.
The engine it materialized is byte-identical to the snapshot the Mac checks ran against.
A signed Release device build and the simulator suite were then built from that root
with the variables listed below; the host build checked 1,329 copied engine source
files against the snapshot before applying Satchel's overlays. The simulator suite ran
105 tests with 3 skipped and no failures. Nothing was installed on a phone.

Remaining closure requirements:

- Obtain matching .NET SDK 10.0.403 and fontTools 4.68.0 distribution hashes. Their
  installed files are diagnostic inputs, not substitutes for archive pins.
- Verify the five pinned tool source archives when downloads are authorized and finish
  their standalone recipes: Python 3.14.4, CMake 4.4.4, XcodeGen 2.45.3 and matching
  SPIRV-Tools/SPIRV-Headers Vulkan SDK 1.4.363.0 sources. Python's required native
  dependencies and XcodeGen's Swift package closure also need pins. No Homebrew bottle
  is a build input. See [the tool pin notes](scripts/deps/README.md).
- Pin the font environment used to generate app fonts. Local validation generates
  host metadata and fonts inside the new root, using an explicitly supplied local
  Python environment. The 29 engine queue patches apply without commits.
- Complete the unsigned app validation with the fresh root and public proof.
  Use a new `SATCHEL_OUT` for validation so an existing host-archive cache is not reused.
  Set
  `SATCHEL_NATIVE_BUILD_INPUTS=<root>/build-inputs`,
  `SATCHEL_CAPSTONE_SOURCE=<root>/sources/capstone-d2e5739cbb43df93d589a3a726c8f878b95beccb`,
  `SATCHEL_OPENSSL_IOS=<root>/openssl-ios` and `SATCHEL_SHADER_DEPS=<root>/m5`,
  alongside `SATCHEL_DEPS_ROOT=<root>`. No phone readiness is implied.

## Public shader validation

App builds default to `NativeShaders/public_parity.py` evidence instead of a private
game-derived result. The bootstrap builds the native components with source-bound
binary receipts and runs 23 original synthetic requests through the same NativeAOT,
SPIRV-Cross and Swift translation path used by the app. The tracked
`NativeShaders/public-parity-lock.json` records the source inventory, generated input hashes,
and expected SPIR-V, resource, MSL-variant and reflection hashes. Staging rejects
missing or changed sources, binaries, receipts and incomplete or stale proof files.
The public baseline is a regression checkpoint, not a claim about all games.

To rerun after rebuilding the shader dependencies, use the shared resource guard:

```sh
ANYGAME_OUT="$HOME/SatchelBuild/anygame/bootstrap-guard" \
  SATCHEL_SHADER_DEPS="$HOME/SatchelBuild/anygame/deps-parity/m5" \
  nice -n 10 python3 -B NativePrepare/guard.py \
  python3 -B NativeShaders/public_parity.py \
  --output "$HOME/SatchelBuild/anygame/parity-check"
```

The private check is retained. Set `SATCHEL_SHADER_PRIVATE_PROOF` to its result path
to require both public and real-game parity. `SATCHEL_SHADER_PARITY=private` explicitly
selects the prior maintainer route for existing dependency roots, using
`<products>/pipeline-full/result.json` unless an explicit path is supplied. Public
mode never silently falls back. The private check requires a complete result with
zero differences. Neither check is fabricated by bootstrap.

Only the private corpus run establishes parity across its real-game shader outputs.
The retained legacy result check requires current maintainer evidence; its flags alone
do not bind an older result to newly rebuilt binaries.
Public synthetic validation establishes reviewed source identity and exact output
agreement for its finite cases, with receipts binding the local iOS binaries. It
does not establish rendered pixels, phone compilation, gameplay, geometry coverage
or cross-toolchain bit-for-bit reproducibility. See
[the public parity contract](NativeShaders/PublicParity.md) for coverage and baseline
review instructions. `--candidate` writes review material only and cannot issue a
passing build proof.

## Tools

- macOS with Xcode 27.2 (iOS 27.2 SDK) and command line tools
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.45 or newer (2.45.3 used locally)
- CMake 3.24 or newer (4.4.4 used locally) and a Vulkan 1.1 capable `spirv-val`
- Python 3.12 or newer (no third-party packages)
- .NET SDK 10.0.403 with the pinned NativeAOT 10.0.14 packages, for the shader translator
- An Apple developer account, and a device with Developer Mode on

## Native dependencies the build expects

All paths are the defaults; every one can be overridden with the environment variable
named next to it. The pins are the ones the maintainer validated against.

| Dependency | Pin | Expected location | Override |
|---|---|---|---|
| nxnative engine source with the NX2Apple fork patches | commit `f8a5618d5f505a4c0a0bcac7c413872d3a53c009` plus the 29 patches of [the tracked queue](scripts/deps/upstream/nxnative-queue/README.md) | `~/SatchelDeps/NX2Apple/engine` | `SATCHEL_DEPS_ROOT` |
| Interpreter-era libraries (VIXL CPU probe, shader bridge, Vulkan backend, guest graphics) | see each `Tools/*/README.md` | `~/SatchelDeps/tmp-snapshot-2026-11-01/...`, referenced from `project.yml` | `SATCHEL_DEPS_ROOT` through `NativeApp/build.py` |
| Capstone | 5.0.7 | `~/SatchelBuild/rewriter/capstone-src` | `SATCHEL_CAPSTONE_SOURCE` (otherwise `SATCHEL_BUILD_ROOT`) |
| OpenSSL for iOS | 3.6.3, static | `~/SatchelBuild/spike/openssl-ios` | `SATCHEL_OPENSSL_IOS` |
| MoltenVK | v1.4.2 | built by `Tools/VulkanBackend` scripts | see that README |
| SPIRV-Cross, SPIRV-Tools, SPIRV-Headers | pinned in `Tools/ShaderBridge` and `Tools/VulkanBackend` | same | same |
| Shader translator (the engine's Ryujinx shader parts, NativeAOT) | engine commit above, see [the shader translator pin](#the-shader-translator-pin) | `~/SatchelDeps/m5` | `SATCHEL_SHADER_SOURCE`, `SATCHEL_SHADER_DEPS` |
| Ryujinx 1.3.3 full source (reference only, not linked) | tag 1.3.3 | `~/SatchelDeps/reference/ryujinx-1.3.3` | none |

The per-component READMEs under `Tools/` describe how each library was fetched and built
(`Tools/CPUProbe/README.md`, `Tools/MaxwellShaderCompiler/README.md`,
`Tools/ShaderBridge/README.md`, `Tools/VulkanBackend/README.md`). The bootstrap wraps those recipes rather than duplicating their compiler commands.
The legacy rename staging mode remains available for historical workflows.

## The engine pin

The native host is built from one engine snapshot: nxnative at the pinned commit with the
fork patches applied, without commits. `scripts/bootstrap_deps.py` materializes it at
`<root>/NX2Apple/engine` and records what it applied in
`<root>/NX2Apple/engine-materialization.json`. `NativeHost/build.py` refuses any other
commit or patch list, copies the runtime sources, checks every copy against the snapshot,
and only then applies Satchel's own overlays to the copy. The snapshot is never edited.

| Item | Value | Recorded in |
|---|---|---|
| Engine commit | `f8a5618d5f505a4c0a0bcac7c413872d3a53c009` (seedds/nxnative main, 2026-11-05) | `scripts/deps/upstream/nxnative-queue/.upstream-ref`, `scripts/deps/inputs.lock.json`, `PIN` in `NativeHost/build.py` |
| Fork patches | 29 (25 as published by NX2Apple `8d13135e`, 4 ported) | `scripts/deps/upstream/nxnative-queue/series` and `ports/`, `PATCHES` in `NativeHost/build.py` |
| Shader chain | `26dc2fa9477c` | `CHAIN` in `NativeHost/build.py`, `shaderChain` in `App/NativeGameProfile.swift` and `App/NativeGameProfiles.json` |
| Pipeline cache namespace | `f8a5618d-26dc2fa9477c-v3-<GPU>-<OS build>` | `NativeHost/graphics/PipelineSettings.m`, described in `NativeHost/ShaderStalls.md` |
| Shader implementation identity | `satchel-native-v4-xlate-f8a5618d-26dc2fa9477c-f11ba9f0` | `NativeShaders/ShaderAPI.swift`, `NativeShaders/integration.json` |

The phone's shader translator follows this pin too since its own move on 2026-11-05 (below).

The on-phone rewriter followed in a separate step: it now reproduces `patch_nso.py` at
`f8a5618d` (newer jump table and tail call proofs, the frame rate target lookup and
Snowdrop's page pools; see `Tools/NativeRewriter/README.md`). Its reference is recorded in
every package Prepare writes (`rewriter` in the manifest, `rewriterRevision` in
`App/NativeGameProfile.swift`). A package without it was rewritten when the reference was
still `bbc3b737`: it stays current only when every module build is one the Mac differential
run proved byte-identical under both references (`rewriterUnchangedBuilds`: Zelda 1.4.3,
Odyssey 1.0.0 and 1.4.1, Castlevania 1.0.0), and any other one needs Prepare again. When the reference moves again,
bump `rewriterRevision` and keep only builds proven unchanged in that list.

### The shader translator pin

The phone translates shaders with the same chain as the engine's Mac tools at the engine
pin. Before 2026-11-05 it followed NX2Apple `8227f80f`, whose translator is nxnative
`ee6648cb`; the move brings in the upstream translator changes between `ee6648cb` and
`f8a5618d`: float comparisons that honour flush to zero (a denormal compares as zero),
`LDC` with its bank chosen at run time, texture types taken from a shader's other
instructions for a size query, storage words the translator split read whole again,
compute shared memory past Metal's 32 KB moved to a device buffer, a first-invocation
store handed to the host, integer colour outputs found by how their values are computed,
projective and pixel-position tile reads, screen varyings, primitive ID and new bindless
binding kinds in the reflection. Switch 2 (NVN2, Ampere) records are refused: their
route, and the mesh stage it needs, is outside this Switch app.

| Item | Value | Recorded in |
|---|---|---|
| Translator sources | 223 files of the engine's `dm/third_party/ryujinx` (Ryujinx shader parts, Spv.Generator, five Ryujinx.Common files and licenses), MIT | `Tools/MaxwellShaderCompiler/game-source-lock.json` |
| Accessor | `dm/gfx/tools/dmsh_xlate/Program.cs`, ported to `GameShaderTranslator.cs` | `Program.cs` hash in the same lock |
| Source transforms | `dmsh_build.py` and its modules, ported to the Swift files of `NativeShaders` | `NativeShaders/source-lock.json` (the parity oracle's hashes) |
| Original fixture framework | unchanged at NX2Apple `8227f80f` | `Tools/MaxwellShaderCompiler/source-lock.json` |

`Tools/MaxwellShaderCompiler/build.py --checkout <root>/NX2Apple/engine` builds the game
translator (the bootstrap's `game-aot` step passes it); `--original-fixture` keeps the
NX2Apple checkout. Shader outputs changed with the move, so the implementation identity
and the pipeline namespace above changed with it: translated sources, Metal libraries and
pipeline archives made by an earlier build are not read, and first play fills them
again. Prepared packages hold no translated shader, and the engine commit, patch count and
chain they record are unchanged, so they stay valid: nothing has to be prepared again.

The Mac gates for this pin compare the phone path with the engine's own tools on real games
(maintainer data, outside the repository):

- `NativeShaders/check-translator.py`: the NativeAOT translator against the engine's
  prebuilt `dmsh_xlate` (SPIR-V and resource bytes), for every shader of a game.
- `NativeShaders/check-pipeline.py --corpus <b-chain folder> [--keys <file>]`, with
  `SATCHEL_SHADER_PROFILE` and `SATCHEL_SHADER_PACKAGES` naming the game: every source,
  variant and reflection against an unmodified `dmsh_build.py` run of the engine for the
  same records. The engine keeps only `<key>_b.metal`; variant sources are made from it
  by the engine's own variant functions, and a variant counts as present when its Mac
  library exists.

Results on 2026-11-05 (Mac only): `check-translator.py` 28,669 of 28,669 Zelda and 27,984
of 27,984 Odyssey shaders equal; `check-pipeline.py` with zero differences for 4,719 Zelda
shaders (every tenth key and its pixel pairs), 5,447 Odyssey shaders (the same) and the 198
captured Castlevania shaders, every variant included. The public synthetic lock was
regenerated: 20 of its 23 cases changed, every one through the pinned tools (pixel shaders
that pass flat inputs to a colour output gain the `main0_u` entry point, which also changes
their side source and the trailing newline of their tile read variant; every pixel reflection
gains `primitive_id` and every vertex reflection `screen`); their SPIR-V is unchanged. The pipeline measurement host built from these products completed
all 12 Mac opening routes (Castlevania, Odyssey and Zelda; exact and async; cold and warm)
with zero shader failures and zero skipped draws.

### Clean break of 2026-11-05

The pin moved from `bbc3b73784751d2a706932dc69fd67627e3445d4` (34 patches, chain
`e65a4ebd3322`) to the values above: 265 upstream commits. Nothing is migrated:

- A package prepared with an earlier build is refused, because its manifest records the
  engine commit, the patch count and the chain. Prepare each game again. Imported game
  files, keys, saves and per-game settings are not touched.
- Shader caches start empty. Translated sources, Metal libraries and pipeline archives
  are keyed by the chain, the implementation identity and the namespace above, so
  nothing made by the earlier engine is read. First play stutters again until they refill.
- A Mac-prepared package (format 1) has to be rebuilt with the new engine's tools. The
  reviewed runtime profiles in `App/NativeRuntimeProfiles.json` were regenerated
  (`fps_max` is a new key), so an old `profile.tsv` no longer matches either.

### Moving the engine pin

The steps that were needed for the move above, in order. Work in a new folder under
`~/SatchelBuild` and never edit the snapshot the current main builds from.

1. Clone seedds/nxnative and LNPANDY/NX2Apple outside the checkout. Treat both as data:
   read them, do not run their scripts from the clone. Pick the engine commit.
2. Read what changed for a front end. Upstream's `docs/frontends.md` ("Following main")
   gives the commands; with `A` the old commit and `B` the new one they are
   `git log A..B --grep='^Front-ends:'`,
   `git log -p A..B -- docs/frontends.md docs/launcher.schema.json dm/games/settings.tsv`,
   `git diff A..B -- scripts/build_ipa_from_nsp.py | grep add_argument`,
   `git diff A..B -- docs/env_switches.tsv` and
   `git log --oneline A..B -- dm/gfx/nvn/dmsh_chain.h` (the shader chain).
3. Rebase the patch queue as `scripts/deps/upstream/nxnative-queue/README.md` describes,
   then update the two rows of `scripts/deps/inputs.lock.json`.
4. Materialize the snapshot into a fresh root with the bootstrap command above, passing a
   `--source-cache` folder that holds the two clones as `NX2Apple` and `NX2Apple/engine`.
5. Update the identities in the table above. The chain is `DMSH_CHAIN` in the snapshot's
   `dm/gfx/nvn/dmsh_chain.h`. `git grep` the old commit and
   the old chain to find the test fixtures and check scripts that repeat them.
6. Rebase Satchel's overlays. `NativeHost/build.py --prepare-only` applies every patch
   with `patch --fuzz=0` and every text overlay with an exact anchor, and stops at the
   first one that no longer fits. Fix them one at a time; do not add fuzz.
7. Review new process-wide state. Every file-scope or function-scope variable the new
   engine added has to be reset, retired or refused (`EBUSY`) by the session overlays
   under `NativeHost/session`, or a second game in the same app process inherits it.
8. Regenerate what is derived from the snapshot:
   `python3 -B NativeGames/runtime_profiles.py --engine <root>/NX2Apple/engine --write`
   (reviewed runtime profiles and their hashes),
   `python3 -B NativeHost/services/inventory.py --engine <root>/NX2Apple/engine`
   (service coverage tables), and, if lock-covered shader sources changed,
   `NativeShaders/public-parity-lock.json` through the `--candidate` review in
   `NativeShaders/PublicParity.md`.
9. If the engine's shader build tools changed, port the change to `NativeShaders`
   (the Swift side of the phone pipeline) and bump the implementation identity.
   `NativeShaders/check-side.py` compares the side library with the engine's own.
10. Bump the pipeline namespace revision, rebuild, run the checks in
    [docs/testing.md](docs/testing.md), and document the clean break here and in
    `README.md`, `ROADMAP.md` and `COMPATIBILITY.md`.
11. Switch over after the merge: either point `SATCHEL_DEPS_ROOT` (and the variables in
    "Dependency bootstrap" above) at the new root, or move the new root's
    `NX2Apple/engine` and `NX2Apple/engine-materialization.json` into place under
    `~/SatchelDeps` once no build uses the old snapshot.

## The app build

`NativeApp/build.py` is the single driver. It rebuilds the host archives from the pinned
engine source, generates the Xcode project from `project.yml`, builds a signed Release
app with two jobs at nice 10, and runs the signature, entitlement, landscape and
"no game content in the bundle" checks. Outputs go under `SATCHEL_OUT` (default
`~/SatchelBuild/oneapp`), never inside the checkout.

```bash
SATCHEL_OUT=~/SatchelBuild/oneapp SATCHEL_BUILD_ROOT=~/SatchelBuild SATCHEL_DEPS_ROOT=~/SatchelDeps nice -n 10 python3 -B NativeApp/build.py
```

The generated `Satchel.xcodeproj` is not tracked (run `xcodegen generate` to open the
library target in Xcode). Options: `--unsigned` compiles only (no provisioning check, not installable);
`--ui-tests-simulator` runs the synthetic navigation suite instead of building for the
phone. The unsigned option also skips signing-team lookup while generating the native
host project. The navigation generator reads the pinned engine from
`$SATCHEL_DEPS_ROOT/NX2Apple/engine`, applies this checkout's display patches under
`SATCHEL_OUT`, and builds only the selected simulator architecture. It does not reuse
another worker's display build. Per-title settings and the save manager add no dependencies.
The simulator suite covers their game-page journeys using synthetic saves and adds
Duo posture previews with synthetic sessions. No new dependencies are needed. Fold
controls and the scaled menu wrapper exist only in the preview target. Set
`SATCHEL_UI_SIMULATOR` to a dedicated simulator UUID; `SATCHEL_UI_ONLY_TESTING` accepts
comma-separated journey names for a targeted run. `Tests/PlayerSettings/run.py` checks
the shared settings and real pipeline policy on the Mac, and `Tests/Saves/run.py` checks
archive validation and backup rotation. Run both through `NativePrepare/guard.py` with
`ANYGAME_OUT`, `SATCHEL_SETTINGS_TEST_OUT` and `SAVE_TEST_OUT` inside your build folder.
The app lands at
`<SATCHEL_OUT>/app/DerivedData/Build/Products/Release-iphoneos/Satchel.app`.

Install and launch with Apple's `devicectl` (replace the UDID with your paired device):

```bash
xcrun devicectl device install app --device YOUR_DEVICE_UDID <SATCHEL_OUT>/app/DerivedData/Build/Products/Release-iphoneos/Satchel.app
```

```bash
xcrun devicectl device process launch --device YOUR_DEVICE_UDID com.dell.satchel
```

Do not install over a running Prepare or import; the app's `Documents/prepare-progress.json`
says whether one is active.

## Generic profile diagnostics

The Mac Prepare harness also supports `--generic-profile` and an optional numeric
`PREPARE_MEMORY_OBSERVATION` JSON file. This exercises unknown-game settings while
retaining the bundled overrides. It rejects reviewed shader packs in generic mode.
See [generic profile diagnostics](docs/generic-profiles.md) for the contract and
commands. An unsigned Mac diagnostic package is not an installable phone build.

## GPU diagnostic host

`NativeApp/build.py --graphics-mac-host --unsigned` builds only the private Mac
GPU host under `SATCHEL_OUT/gpu-mac`. It requires the maintainer's existing Mac
libvpx and utility shader outputs in addition to the dependency snapshot. See
[the graphics diagnostics](NativeHost/graphics/README.md#gpu-pass-cost-diagnostics)
for prerequisites, paired counter analysis and synthetic checks.
`SATCHEL_GRAPHICS_REPLAY=1` also builds the private captured-frame replayer, with
strict runtime-profile, layout and shader-interface checks. It neither builds an
installable phone app nor establishes phone performance. The unsigned app build
also skips signing identity discovery when preparing its native host archives.

The spatial upscaler reuses the pinned engine and the SDK's existing MetalFX
framework dependency. `upscale-check.py` checks profile persistence and synthetic
Metal presentation after a diagnostic Mac build. `SATCHEL_UPSCALE_DIAGNOSTICS=1`
adds private Mac paired-timing and matched-image controls; it does not instrument
the iOS app. See [spatial presentation upscaling](NativeHost/graphics/README.md#spatial-presentation-upscaling).

`python3 -B NativeHost/graphics/frame-timing-check.py` uses `SATCHEL_OUT` and the
shared build wait to test timing accounting and real Metal callbacks on synthetic
Mac buffer fills. `-SatchelGraphicsProfile YES` enables per-frame GPU and CPU
summaries in the app's `host.log`; phone verification is separate from the unsigned
build. See [phone frame GPU timing](NativeHost/graphics/README.md#phone-frame-gpu-timing).

## Signing with your own team

No Apple team ID is stored in the repository. `NativeApp/build_support.py` resolves the
team for a signed build in this order:

1. the `SATCHEL_TEAM` environment variable;
2. the untracked file `local/signing-team` (one line, the ten-character team ID; the
   `local/` folder is ignored by git);
3. the single Apple Development identity in your login keychain, if there is exactly one.

Xcode shows the ID under Settings, Accounts, Team. The same module resolves the Mac
codesign identity for development-only Mac signing (`SATCHEL_SIGNING_IDENTITY`,
`local/signing-identity`, or the single identity in the keychain).

At run time the app learns its own team from the private Keychain group that Xcode
writes into Info.plist (`$(AppIdentifierPrefix)` plus the bundle ID), with the embedded
provisioning profile as a fallback. Prepare accepts a PKCS#12 identity only for that
team, so a game is always signed by the same team as the app that loads it. The build
verifiers compare the signature, entitlements and profile with each other instead of
with a constant.

`bundleIdPrefix` in `project.yml` is `com.dell`; change it if your account cannot
register that App ID. Opening the generated `Satchel.xcodeproj` in Xcode with automatic
signing and no team set simply asks you to pick one.

## Importer component checks

The [importer validation guide](docs/importer-validation.md) documents synthetic
containers and an output-discarding content benchmark. The content test and
benchmark scripts accept `SATCHEL_ARCHIVE_LIBRARY` for a task-local build of the
existing pinned Zstd decoder. `CONTENT_MAIN` and `CONTENT_EXECUTABLE` select the
benchmark entry point and binary path. The Prepare core harness also accepts
`PREPARE_PIPELINE` for a private measurement adapter; production builds always use
`NativePrepare/Pipeline.swift`. These Mac tools do not sign or run games.
They do not replace the canonical unsigned Release app build or phone validation.

## Rules for building

- One heavy build at a time. The Mac crashed when three workers compiled at once;
  `NativeShaders/guard.py` and the build driver wait for other compilers and keep a
  35 GiB disk reserve.
- Build outputs never go inside the checkout and never in `/tmp` (which is wiped on
  reboot). Use `~/SatchelBuild/<task>/`.
- Experiments use their own bundle IDs (`com.dell.satchel.<name>`) so the installed app
  and its library stay untouched.

## Session diagnostics on the Mac

The [session harness](NativeHost/session/README.md) builds a private Mac host with
`NativeApp/build.py --session-mac-host --unsigned`. Set `SATCHEL_OUT` to a private
folder and `SESSION_MAC_OUT` to its `session-mac` subfolder when running `soak.py`.
Packages remain external and read-only.
Run through `NativePrepare/guard.py` with a private `ANYGAME_OUT`, two compiler jobs
and the existing disk/RSS watchdog. `--leak-check --allocation-stacks` records only
allocation counts, types and symbolic host/system frames. It never stores allocation
contents. The zero-leak requirement is unchanged. These runs do not install or launch
the iPhone app; use `NativeApp/build.py --unsigned` for the separate app build check.

The private Mac helper waits for shared heavy builds before compiling a missing
library. That wait can stall a running game and is retained in acceptance results.
For captured offline translations, use an isolated tools directory whose
`bin/xcrun` points to the current `scripts/native_xcrun.py`, and verify the pinned
compiler with the unchanged upstream tool lock. Pre-rename tool symlinks may be stale.

For gated long runs, verify control before creating each private `play.ready` file.
Keep the Mac awake: the harness records wall and active time and rejects a run
with five seconds or more of inactive time, including sleep during warmup. Keep
failed run results intact when separately reporting a later gameplay interval.
`--diagnostic-stop` samples the host and its owned compiler helpers after a failed
stop. It omits arguments, paths, guest frames and addresses and does not retry stop
or change the five-second guest-join deadline.

Developer Quit/cycle automation adds no dependency. Its pure Swift policy check is
`python3 -B Tests/PreparedCycle/run.py`; the navigation suite also exercises the
production menu and cycle adapter over synthetic sessions. Build the normal unsigned
app with `NativeApp/build.py --unsigned`. Only the orchestrator signs and installs for
the [phone procedure](docs/phone-cycle.md). Neither a simulator pass nor an unsigned
build establishes phone Quit/relaunch acceptance.

For asynchronous pipeline experiments, use `-SatchelPipelineMode async` and
`-SatchelPipelineBudgetMS 4` after building. The default remains `exact`.
The budget covers timed pipeline waits; descriptor resolution and required
first-use waits can exceed it. Synthetic checks and Mac comparison controls are documented in
[graphics validation](NativeHost/graphics/README.md#async-validation).

Game settings on each game page save Exact, Async, experimental Bounded or experimental Skip
for that title's next launch. `-SatchelPipelineMode skip` (or `bounded`) overrides the saved
choice for a trial. `python3 NativeHost/stall_attribution.py graphics-frames.tsv
shader-events.tsv` splits a profiled trial's frames over 50 ms into shader stages.
Each profiled launch starts its event files fresh; `python3 -B NativeHost/check_stall_attribution.py`
checks that the script uses one launch only. `-SatchelPipelinePrewarm YES` (opt-in, with
`-SatchelPipelinePrewarmMS`) builds the recorded pipelines before the first frame, and
`-SatchelPipelineTrialListFrom <tag>` gives a new trial another trial's recorded list with an empty
archive ([launch prewarm](NativeHost/graphics/README.md#launch-prewarm-of-recorded-pipelines)). On the
Mac host, `NativeHost/bench_shaders.py --capture --prewarm` and `--cache relist` do the same.
Skip is restricted to a terminal draw in the current submission; all other timeouts wait. With
`-SatchelGraphicsProfile YES`, `pipeline-skips.tsv` records frame/count rows beside
the shader events. The developer overlay shows the last frame's skipped count.
Keep cold-cache trials in a fresh task-owned namespace; preserve player data.

Build the private pipeline measurement host with `NativeApp/build.py
--pipeline-mac-host --unsigned`, setting `SATCHEL_OUT` to your task folder and
running through `NativePrepare/guard.py`. It uses the installed pinned shader
translator and existing Castlevania host prerequisites (only the prebuilt libvpx; since the
f8a5618d pin it builds dav1d, the profile header and the host's Metal shaders from the
pinned engine). The host instruments
skipped-frame captures only when requested; normal timing routes leave capture
disabled. It is a Mac diagnostic, not an iPhone execution result.

## Unsigned IPA and source

Use [NativeApp/Distribution/README.md](NativeApp/Distribution/README.md) for the exact
package, manifest, source and ad hoc re-sign commands. `scripts/build_ipa.py` consumes
a canonical unsigned Release app and keeps its original build untouched. Packaging
normalizes timestamps, ordering and permissions; it does not establish bit-for-bit
reproducibility of Xcode or native dependencies. The native app's version, build and
minimum iOS now come from `NativeApp/Distribution/release.json`, preserving the actual
shipped `1.0 (1)` instead of relying on XcodeGen's implicit Info.plist defaults.

The export includes an account-neutral entitlement template and requires both memory
entitlements. Ad hoc signing checks that those values survive a local re-sign. Real
installer preservation still needs the [phone journey](docs/distribution-validation.md).
The source JSON uses placeholder URLs, measured IPA size/SHA-256 and tracked screenshots.
No release, tag or upload is created. The draft Actions workflow is manual dispatch
only with a hard-disabled job until the clean-runner dependency gaps above are closed.

## Controller checks

No new dependency is required. Controller profile and input code uses the installed
GameController SDK. The simulator suite compiles the production controller overlay
against synthetic snapshots, without game data. Use a dedicated simulator through
`SATCHEL_UI_SIMULATOR`; the `controls` worker uses its own “Satchel controls” iPhone Duo
on iOS 27.1. `SATCHEL_UI_ONLY_TESTING` can select `testControlsSettingsAndPersistence`,
`testControlsLibraryDetailAndBack` and `testControlsMenuShortcutAndLayout`.

The small macOS remapping check is `NativeHost/controllers/check_remapping.py`, with
`SATCHEL_OUT` set to the worker's output directory. Run it through
`NativePrepare/guard.py` with a worker-owned `ANYGAME_OUT` directory. The existing
`NativeHost/controllers/check.py` additionally checks HID, rumble and player settings.
The remapping suite covers short Plus/Minus taps, complete-chord suppression through
release, the 0.75 second hold and reserved Home input.
See [docs/testing.md](docs/testing.md) for hardware acceptance steps. Simulator results
establish UI behavior and synthetic input routing, not physical controller behavior.

For a disk-constrained UI run, `SATCHEL_UI_DIAGNOSTICS=never` omits Xcode's verbose
failure sysdiagnostics. Assertions, failure screenshots and test result bundles remain
in place. The default diagnostics policy is unchanged when the variable is absent.

The optional warm-up clients build through `NativeApp/build.py
--warmup-mac-harness --unsigned` and `--warmup-mac-tests --unsigned`, under the same
resource guard and private output-root rules. The latter selects integration
checks with `SATCHEL_WARMUP_MAIN=$PWD/Tests/Warmup/Checks.swift` or a private
captured-inventory replay with `Tests/Warmup/SpeedRunner.swift`. These clients
use the installed NativeAOT and bridge dependencies, with two build workers.
See [warm-up measurement and phone steps](NativeShaders/WarmupSpeed.md).

The memory worker retains private matched-route measurements and reproducible
commands outside the checkout. The unmerged diagnostic tools require the shared
heavy-build guard, unsigned builds and worker-owned outputs. See
[the scene-memory evidence](Validation/memory-pressure-scenes-2026-11-04.md)
for the validated measurements and unresolved exact-replay gate.
