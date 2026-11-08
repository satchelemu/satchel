# Building Satchel from source

The path below goes from a fresh clone to an unsigned app and IPA with one command, from
pinned inputs and pinned tools, without Homebrew or anything already in your account.
On 2026-11-07 it ran end to end from a fresh clone on the maintainer's Mac, and the app
it produced matches a normal maintainer build of the same commit except for build paths,
build identifiers and timestamps (see "What the route proves"). It has not yet run on a
second Mac. Signing is account-neutral and comes after it, with your own team.

## From a fresh clone to an unsigned app

What you install yourself (none of it can be pinned by the repository):

- An Apple Silicon Mac with macOS 27.2.
- Xcode 27.2 (27B52), opened once so its license and components are installed, and
  selected with `sudo xcode-select -s /Applications/Xcode.app` (or `DEVELOPER_DIR`).
- Xcode's Metal toolchain: `xcodebuild -downloadComponent MetalToolchain` if
  `xcrun metal --version` fails.
- Network access to the official sources in
  [scripts/deps/inputs.lock.json](scripts/deps/inputs.lock.json) for the first run.
- Disk: about 8 GiB for the work folder, and the build guard refuses to start below
  36 GiB free (it protects the Mac it runs on).

Then:

```sh
git clone https://github.com/satchelemu/satchel.git
cd satchel
/usr/bin/python3 scripts/clean_build.py --work ~/SatchelClean
```

`/usr/bin/python3` is the Python that comes with Xcode; nothing else is needed first.
The work folder must be outside the checkout. The driver runs four steps and then a check:

| Step | What it does | Output |
|---|---|---|
| `tools` | `scripts/setup_tools.py`: checks the Mac, Xcode, the iOS SDK and the Metal toolchain; downloads the pinned tools, verifies each hash, installs CMake and XcodeGen release binaries and the .NET SDK, builds Python and `spirv-val` from pinned sources, keeps the pinned fontTools wheel | `<work>/tools` (`bin/` holds every tool) |
| `deps` | `scripts/bootstrap_deps.py --tools --fetch`: fetches the 19 pinned inputs from their official URLs (Git commits verified by `git archive` hash, files by SHA-256), applies the 29 engine patches, then builds every native dependency with outbound network denied by `sandbox-exec` | `<work>/deps`, with `bootstrap-manifest.json` and `output-manifest.json` |
| `app` | `NativeApp/build.py --unsigned` against that root, under the shared build guard | `<work>/out/app/DerivedData/Build/Products/Release-iphoneos/Satchel.app` |
| `ipa` | `scripts/build_ipa.py` | `<work>/ipa/Satchel.ipa` and `Satchel.manifest.json` (every file's SHA-256) |

Every step runs with a minimal environment: `PATH` is `<work>/tools/bin` plus the macOS
system folders, `HOME` is `<work>/home`, and every `SATCHEL_*` location points inside the
work folder. Homebrew, `/usr/local`, `~/SatchelDeps`, `~/SatchelBuild` and the rest of
your account are never on the path. `--steps tools,deps` runs a subset; the `deps` step
needs a fresh `<work>/deps`. `--cache <folder>` reuses previously downloaded archives,
each only after its pinned hash matches.

To see which steps pass, at any time:

```sh
/usr/bin/python3 scripts/clean_build.py --work ~/SatchelClean --check
```

It prints one PASS or FAIL line per step (`xcode`, `tools`, `deps`, `app`, `ipa`) and an
`isolation` line: every generated Xcode project, build log and the app's link commands
are scanned for paths outside the checkout, the work folder and the system (Homebrew,
`/usr/local`, any other folder of your account). It exits 0 only when every line passes,
and keeps `check.json` and `isolation.json` in `<work>/logs`. `--reference-manifest
<other>/Satchel.manifest.json` also writes a per-file comparison with another IPA.

Signed builds for your phone use the same root afterwards: set the variables the driver
uses (`SATCHEL_DEPS_ROOT=<work>/deps`, `SATCHEL_NATIVE_BUILD_INPUTS=<work>/deps/build-inputs`,
`SATCHEL_CAPSTONE_SOURCE=<work>/deps/sources/capstone-d2e5739cbb43df93d589a3a726c8f878b95beccb`,
`SATCHEL_OPENSSL_IOS=<work>/deps/openssl-ios`, `SATCHEL_SHADER_DEPS=<work>/deps/m5`, a
`SATCHEL_BUILD_ROOT` and a `SATCHEL_OUT`), put `<work>/tools/bin` first on `PATH`, and run
`NativeApp/build.py` without `--unsigned` (see "Signing with your own team" below).

### The pinned tools

All in the `tools` list of the lock. A hash comes from the publisher where one is
published; nothing is pinned by trusting a first download.

| Tool | Pin | Form | Hash from |
|---|---|---|---|
| CMake | 4.4.4 | Kitware's macOS universal release archive | Kitware's `cmake-4.4.4-SHA-256.txt` (it also lists the source archive hash the lock had) |
| XcodeGen | 2.45.3 | release `xcodegen.zip`, with `tools/share/xcodegen` linked so it finds its setting presets | GitHub's release asset digest |
| .NET SDK | 10.0.403 (runtime 10.0.14) | Microsoft's `osx-arm64` archive | SHA-512 in Microsoft's `releases.json` |
| Python | 3.14.4 | built from `Python-3.14.4.tgz` with Xcode's compiler and SDK (no OpenSSL; downloads use `curl`) | python.org release file record |
| spirv-val | SPIRV-Tools and SPIRV-Headers `vulkan-sdk-1.4.363.0` | built from the two tag archives, only the `spirv-val` target | the earlier formula pins (GitHub tag archives have no publisher checksum); both matched on download |
| fontTools | 4.68.0 | `py3-none-any` wheel, unpacked into a private environment made from the pinned Python | PyPI release metadata |

### What the route proves

Run on 2026-11-07 on the maintainer's Mac, from a fresh clone of the branch that added it
(based on main 3ed0a5e), with the isolated `PATH` and `HOME` above. Mac execution only;
nothing was installed on a phone. All four steps exited 0 and every `--check` line passed:

- `tools` (145 s): every archive downloaded from its pinned URL and matched its pin (both
  SPIRV tag archives matched the earlier formula pins); Python 3.14.4 and `spirv-val`
  (SPIRV-Tools v2026.4) built with Xcode 27.2; XcodeGen's setting presets checked with a
  probe project. The tools folder is about 1 GiB.
- `deps` (1,394 s): all 19 inputs fetched from their official URLs and verified, the 29
  engine patches applied, then all 16 recipe steps exited 0 offline under `sandbox-exec`:
  the CPU suite (5,837,760 checks), the scalar gates (853,611 compiler and 20,365,499
  admission assertions), 696 captured modules through the pinned `spirv-val`, NuGet
  author and repository signatures verified with the pinned SDK, the venv made from the
  pinned wheel, and public shader parity. 127 output files, none missing; the root is
  about 4.6 GiB.
- `app` (148 s) and `ipa` (2 s): `com.dell.satchel` 1.0 (1), unsigned, 124 files.
- `isolation`: 102 generated projects, logs and the app's link commands name no
  Homebrew, `/usr/local` or other account path.

Compared with an unsigned build of main 3ed0a5e made the maintainer's usual way (the
`engine-pin` dependency root, the current shader products, Homebrew tools), 116 of the
app's 124 files are byte-identical. The others, all explained:

- `Satchel`: the same symbols and section sizes except `__cstring`, which holds build
  paths: VIXL's source file names, OpenSSL's configured install prefix and two signer
  source paths. Their different lengths move string addresses, so code and data bytes
  that refer to them differ too.
- `SatchelShaderTranslator`, `SatchelShaderProbe`, `SatchelCapturedFlat` (NativeAOT):
  same size, 31 or 32 bytes apart, the per-build module and image identifiers.
- `Assets.car`: same size, 59 bytes apart, the asset compiler's rendition order.
- `nintendo_ext.ttf`: only the `head` table differs (creation and modification time).
- `install.md`: the copy of `docs/install.md`, which that branch changes.
- `SatchelShaderProbe.framework/Notices/nxnative.MIT.txt`: only in the clean build, a
  notice the current fixture recipe adds; the maintainer's older root predates it.

Of the dependency root's 127 files, 82 are identical to the maintainer's roots and 44
differ, all by build metadata: 36 static archives differ only in their archive header and
symbol table (every object is byte-identical), 5 have objects that embed the checkout or
install path (CPU probe, shader bridge, scalar compiler, rewriter, and OpenSSL's version
date and install prefix in 4 of its 1,047 objects) and 3 are NativeAOT identifiers. The
fixture notice above is the one file absent there.

This is not bit-for-bit reproducibility: build paths and identifiers still reach the
binaries. Two fixes came out of the runs. XcodeGen started through `tools/bin` found no
setting presets, so the first clean app lacked the standard Release settings and Xcode
stripped its embedded frameworks; `setup_tools.py` now links the presets and refuses to
finish without them, and the check fails an app whose log shows the XcodeGen warning. The
host build now moves `project.yml`'s `/Users/<name>/SatchelDeps` paths to
`SATCHEL_DEPS_ROOT` (below). With the maintainer's usual environment, that change leaves
both the unsigned and the signed default build unchanged: the same 124 library and search
paths under the configured root, a signed app that passes `codesign --verify --deep --strict`
and the build's own signature checks, and an unsigned app identical to main's except for
checkout paths in `Satchel`, the asset catalog's rendition order and `install.md`.

## Dependency bootstrap details

`scripts/bootstrap_deps.py` is what the `deps` step runs. Standalone, with tools from
`scripts/setup_tools.py`:

```sh
<tools>/bin/python3 -B scripts/bootstrap_deps.py --root <new empty folder> --tools <tools> --fetch
```

The tracked inventory is [scripts/deps/inputs.lock.json](scripts/deps/inputs.lock.json):
for each of the 19 inputs its source URL, revision or version, license and SHA-256; for
each tool its URL, version, hash and where the hash comes from. File hashes cover the
downloaded bytes. Git hashes cover `git archive --format=tar COMMIT`, verified after
fetching the pinned commit and before checking it out. Dirty source-cache files and
prebuilt libraries are never used as Git inputs. The two NX2Apple pins have distinct
roles: the current engine and patch queue, and the older translator reference required
by its source lock. `gaps` and `tool_recipe_gaps` are empty; the bootstrap refuses to
build while either lists anything.

With `--fetch`, the bootstrap downloads only into a new empty root and only pinned URLs,
promotes nothing whose hash differs, then hands the staged root to a second run of
itself under `sandbox-exec` with outbound IP connections denied (local MSBuild IPC stays
available). That run checks every staged input again (Git HEAD and a clean tree, file
hashes) before building. Without `--fetch` the whole run is offline and every input
must come from a `--cache` folder. No URL or hash is learned from a download.
`--fetch --stage-only` stops after staging. `--tools` is required unless
`--local-validation` is given.

The root keeps `bootstrap-manifest.json` (inputs, step exit codes, the tool versions and
`clean_mac_ready`, true only when every input verified, every step exited 0 and no
declared artifact is missing on the pinned-tool route), `output-manifest.json` (SHA-256
of every declared artifact, and with `--compare <root>` the comparison with another
root) and per-step logs, including failures. Exit 0 means complete; exit 2 means
incomplete, even if individual recipes passed. The heavy-build guard waits for active
builds, limits each process to less than 9 GiB RSS and protects a 35 GiB disk reserve.
Recipes use two jobs. The dependency layout retains the historical
`tmp-snapshot-2026-11-01` subdirectory because the app still references it; its
libraries are built afresh. `project.yml` spells those libraries under the maintainer's
`~/SatchelDeps`; the host build moves any `/Users/<name>/SatchelDeps` path to
`SATCHEL_DEPS_ROOT`, so another account never links whatever exists at that spelling.

### The maintainer's local diagnostic route

The earlier route uses installed tools, an explicit installed SDK and the maintainer's
read-only caches. It does not qualify as a clean-Mac build and always exits 2:

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

These paths are explicit cache arguments, not bootstrap defaults. Its venv (fontTools)
is copied from the source cache instead of being made from the pinned wheel.

History of that route: the captured compiler README checkpoint was reconciled against
the exact reviewed bytes from the archived history (only the title separator changed
during the public release); all 249 source files match their checkpoints. The local
fresh-root rebuild of 2026-11-04 produced all 45 declared dependency artifacts. The CPU
suite passed 5,837,760 checks; scalar gates passed 853,611 compiler and 20,365,499
admission assertions; captured compilation passed its cold, mutation, typed-schema and
696-module SPIR-V gates. These are Mac synthetic executions and iOS compilation, not
phone execution. It was repeated on 2026-11-05 for the engine pin move (nxnative
`f8a5618d`): every step exited 0, all 19 inputs verified, 125 output files with none
missing, public shader parity passed on its 23 requests, and the engine it materialized
is byte-identical to the snapshot the Mac checks ran against. A signed Release device
build and the simulator suite (105 tests, 3 skipped, no failures) were built from that
root; the host build checked 1,329 copied engine source files against the snapshot
before applying Satchel's overlays.

Settings > Device check adds no dependency. Its native probe builds with the iOS SDK;
the running-signature query resolves Security SPI dynamically and reports Unknown if
unavailable. The simulator harness exercises the probe and synthetic report journeys.
Signed device readings still require the checks in [docs/testing.md](docs/testing.md).

## Public shader validation

App builds default to `NativeShaders/public_parity.py` evidence instead of a private
game-derived result. The bootstrap builds the native components with source-bound
binary receipts and runs 29 original synthetic requests through the same NativeAOT,
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

A full run first marks `<products>/public-parity/result.json` incomplete, so a failed
rerun never leaves an older success for staging; on a root other builds share, that
briefly fails their staging too. To check an existing proof without writing anything,
use `--validate`: it rechecks the proof against the tracked lock, the source inventory
and every product receipt, builds no harness, and exits 1 with the reason when the
proof is stale:

```sh
python3 -B NativeShaders/public_parity.py --validate   # products from SATCHEL_SHADER_DEPS
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

The clean route installs every tool except Xcode itself (see "The pinned tools" above):

- macOS 27.2 with Xcode 27.2 (iOS 27.2 SDK) and its Metal toolchain: installed by you
- CMake 4.4.4, XcodeGen 2.45.3, Python 3.14.4, `spirv-val` (Vulkan SDK 1.4.363.0 sources),
  .NET SDK 10.0.403 with the pinned NativeAOT 10.0.14 packages, fontTools 4.68.0:
  installed into `<work>/tools` by `scripts/setup_tools.py`
- `git`, `curl`, `tar`, `ditto`, `make`, `patch`, `ruby` and `sandbox-exec` from macOS
  and Xcode
- An Apple developer account, and a device with Developer Mode on, only for signed builds

Builds outside the clean route still find these tools on `PATH` (Homebrew versions
work for the maintainer's diagnostic routes) but are not reproducibility evidence.

## Native dependencies the build expects

All paths are the defaults; every one can be overridden with the environment variable
named next to it, and the clean route sets every one inside its work folder (the
`<root>` paths in "From a fresh clone" above). The pins are the ones the maintainer
validated against.

Every build tool and check resolves the dependency root, the engine snapshot, the native
build inputs and the shader products through `scripts/dependency_paths.py`, with one
rule: an explicit variable wins; otherwise the root is `SATCHEL_DEPS_ROOT`, or
`~/SatchelDeps` only when that folder records the current engine pin in its
`NX2Apple/engine-materialization.json`. The build inputs default to
`<root>/build-inputs` (`SATCHEL_NATIVE_BUILD_INPUTS`), the shader products to `<root>/m5`
(`SATCHEL_SHADER_DEPS`) and the zstd archive to
`<shader products>/archive-<platform>/libShaderArchive.a` (`SATCHEL_ARCHIVE_LIBRARY`).
Anything else stops before building with the variable to set; no tool falls back to an
older materialization. `python3 -B scripts/dependency_paths.py engine` (or `deps-root`,
`build-inputs`, `shader-deps`, `archive-library macosx`) prints what a tool would use.
On the maintainer's Mac the current root is set by sourcing one environment script that
exports these variables.

| Dependency | Pin | Expected location | Override |
|---|---|---|---|
| nxnative engine source with the NX2Apple fork patches | commit `f8a5618d5f505a4c0a0bcac7c413872d3a53c009` plus the 29 patches of [the tracked queue](scripts/deps/upstream/nxnative-queue/README.md) | `<root>/NX2Apple/engine` (rule above) | `SATCHEL_DEPS_ROOT` |
| Interpreter-era libraries (VIXL CPU probe, shader bridge, Vulkan backend, guest graphics) | see each `Tools/*/README.md` | `<root>/tmp-snapshot-2026-11-01/...`, referenced from `project.yml` | `SATCHEL_DEPS_ROOT` through `NativeApp/build.py` |
| Capstone | 5.0.7 | `~/SatchelBuild/rewriter/capstone-src` | `SATCHEL_CAPSTONE_SOURCE` (otherwise `SATCHEL_BUILD_ROOT`) |
| OpenSSL for iOS | 3.6.3, static | `~/SatchelBuild/spike/openssl-ios` | `SATCHEL_OPENSSL_IOS` |
| MoltenVK | v1.4.2 | built by `Tools/VulkanBackend` scripts | see that README |
| SPIRV-Cross, SPIRV-Tools, SPIRV-Headers | pinned in `Tools/ShaderBridge` and `Tools/VulkanBackend` | same | same |
| Shader translator (the engine's Ryujinx shader parts, NativeAOT) | engine commit above, see [the shader translator pin](#the-shader-translator-pin) | `<root>/m5` | `SATCHEL_SHADER_SOURCE`, `SATCHEL_SHADER_DEPS` |
| Ryujinx 1.3.3 full source (reference only, not linked or fetched; the shader parts are the engine's vendored copy above) | tag 1.3.3 | `~/SatchelDeps/reference/ryujinx-1.3.3` | none |

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

Tessellation stages (2026-11-07): `GameShaderTranslator.cs` answers the domain shader's
tessellation mode from request facts (`tess_domain`, `tess_spacing`, `tess_cw`; the engine's
accessor always answers triangles, equal spacing, counter-clockwise, as the facts' absence
still does), so the NativeAOT product was rebuilt. The public lock gained six hull and
domain cases; the other 23 cases' hashes are unchanged with the rebuilt translator, as
outputs without those facts are. The implementation identity is unchanged, because no
existing stage's output changed and hull and domain requests had never produced a cached
result. See [NativeShaders/RuntimeIntegration.md](NativeShaders/RuntimeIntegration.md#tessellation-handoff).

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
# SATCHEL_DEPS_ROOT and the other variables in "From a fresh clone" name a root at the current pin.
SATCHEL_OUT=~/SatchelBuild/oneapp SATCHEL_BUILD_ROOT=~/SatchelBuild nice -n 10 python3 -B NativeApp/build.py
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
commands. The app's automatic feedback and Copy diagnostics are checked with
`SATCHEL_OUT=<folder> python3 -B NativePrepare/check_feedback.py`. An unsigned Mac diagnostic package is not an installable phone build.

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
The edge smoothing present compiles `NativeHost/graphics/PresentFilter.metal` at run time;
`SATCHEL_QUALITY_DIAGNOSTICS=1` on the pipeline Mac host build adds the render-scale burst
captures, and `xcrun swiftc -O NativeHost/graphics/quality/present-quality.swift` builds the
Mac comparison tool (see [present edge smoothing](NativeHost/graphics/README.md#present-edge-smoothing-opt-in)).
The 3D view resample compiles `NativeHost/graphics/ViewUpscale.metal` at run time the same way;
`xcrun swiftc -O NativeHost/graphics/quality/view-upscale.swift` builds its offline resample tool
(see [3D view resample](NativeHost/graphics/README.md#3d-view-resample-opt-in)).

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
existing pinned Zstd decoder; without it they link the shader products' archive (the
rule in "Native dependencies the build expects") and stop with the variable to set when
it is missing. `CONTENT_MAIN` and `CONTENT_EXECUTABLE` select the
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
`-SatchelShaderAhead <1..8>` (Mac host `--shader-ahead N`) is the opt-in
[translation ahead](NativeHost/graphics/README.md#translation-ahead) trial.
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
The source JSON uses the published URL layout from `release.json` (the IPA as an asset
of the `v<version>` GitHub release of satchelemu/satchel, the source as `source.json` on
`main`, icon and screenshots at the tag), measured IPA size and SHA-256 and the tracked
screenshots. None of those URLs exists until the maintainer publishes.

`scripts/release_dry_run.py --out <new folder>` is the whole release in one offline
command: from a clean, committed tree that passes the hygiene check it builds the
unsigned app (or takes a `scripts/clean_build.py` work folder with `--clean-work`),
packages it twice and compares, checks the ad hoc re-sign, and writes the IPA, manifest,
entitlement template, `SHA256SUMS`, source, release notes and a filled-in publishing
checklist. It runs inside `sandbox-exec` with outbound IP denied and publishes nothing;
the maintainer's steps are in [docs/publishing.md](docs/publishing.md). The draft Actions
workflow runs the clean route, its `--check` and this dry run for an existing tag; it is
manual dispatch only with a hard-disabled job until the steps in that checklist are done.

## Controller checks

No new dependency is required. Controller profile and input code uses the installed
GameController SDK. The simulator suite compiles the production controller overlay
against synthetic snapshots, without game data. Use a dedicated simulator through
`SATCHEL_UI_SIMULATOR`; the `controls` worker uses its own “Satchel controls” iPhone Duo
on iOS 27.1. `SATCHEL_UI_ONLY_TESTING` can select `testControlsSettingsAndPersistence`,
`testControlsSettingsFocusAndDiagnostics`, `testControlsLibraryDetailAndBack` and
`testControlsMenuShortcutAndLayout`.

The small macOS remapping check is `NativeHost/controllers/check_remapping.py`, with
`SATCHEL_OUT` set to the worker's output directory. Run it through
`NativePrepare/guard.py` with a worker-owned `ANYGAME_OUT` directory. The existing
`NativeHost/controllers/check.py` additionally checks HID, rumble and player settings.
The remapping suite covers short Plus/Minus taps, complete-chord suppression through
release, the 0.75 second hold, reserved Home input, and the hold-Menu shortcut for a pad
without Home or Options. `NativeHost/controllers/check_hardware.py` (no build, no
hardware) checks the paths only a real paired pad takes: discovery at launch and on
return, the host log line for every reported controller, input independent of vendor
name and category, focus on every full-screen screen, the diagnostics panel and the
Info.plist keys. `NativeHost/controllers/verify_build.py` checks the built app's
Info.plist and diagnostics.
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
