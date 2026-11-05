# Building Satchel from source

As of 2026-11-04, a clean Mac still cannot reproduce the complete app. The dependency
bootstrap below reconstructs pinned source inputs and reuses the native recipes in a
fresh root. Five tool source archives now have exact URLs and SHA-256 pins, but their
standalone recipes, two remaining distribution pins and the app's private shader-parity
prerequisite are not yet reproducible. It fails closed instead of downloading unpinned
tools or claiming that an incomplete root is usable. Signing is account-neutral.

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
  --root "$HOME/SatchelBuild/anygame/deps-part2" \
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
executions and iOS compilation, not phone execution. Output comparison found 78
identical files, 44 differing files and three archives absent from the reference root.
The observed differences are archive metadata, build paths, managed module identifiers
and reviewed notice/checkpoint changes. This is not bit-for-bit reproducibility.

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
  Python environment. The 34 recorded engine patches apply without commits; 1,106
  runtime source files matched the existing engine byte for byte.
- Replace the dependency on a private `m5/pipeline-full/result.json` through a reviewed
  public validation design. `NativeHost/shader_dependency.py` still requires this
  game-derived parity evidence. The bootstrap neither copies it nor bypasses its check.
- Complete `NativeApp/build.py --unsigned` with the fresh root. The local attempt
  compiled all three native host archives and the rewriter, then stopped at the
  unchanged `m5/pipeline-full/result.json` prerequisite. No unsigned app was produced.
  Use a new `SATCHEL_OUT` for validation so an existing host-archive cache is not reused.
  Set
  `SATCHEL_NATIVE_BUILD_INPUTS=<root>/build-inputs`,
  `SATCHEL_CAPSTONE_SOURCE=<root>/sources/capstone-d2e5739cbb43df93d589a3a726c8f878b95beccb`,
  `SATCHEL_OPENSSL_IOS=<root>/openssl-ios` and `SATCHEL_SHADER_DEPS=<root>/m5`,
  alongside `SATCHEL_DEPS_ROOT=<root>`. No phone readiness is implied.

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
| NX2Apple / nxnative engine source | commit `bbc3b73784751d2a706932dc69fd67627e3445d4` plus the 34 recorded integration patches | `~/SatchelDeps/NX2Apple/engine` | `SATCHEL_DEPS_ROOT` |
| Interpreter-era libraries (VIXL CPU probe, shader bridge, Vulkan backend, guest graphics) | see each `Tools/*/README.md` | `~/SatchelDeps/tmp-snapshot-2026-11-01/...`, referenced from `project.yml` | `SATCHEL_DEPS_ROOT` through `NativeApp/build.py` |
| Capstone | 5.0.7 | `~/SatchelBuild/rewriter/capstone-src` | `SATCHEL_CAPSTONE_SOURCE` (otherwise `SATCHEL_BUILD_ROOT`) |
| OpenSSL for iOS | 3.6.3, static | `~/SatchelBuild/spike/openssl-ios` | `SATCHEL_OPENSSL_IOS` |
| MoltenVK | v1.4.2 | built by `Tools/VulkanBackend` scripts | see that README |
| SPIRV-Cross, SPIRV-Tools, SPIRV-Headers | pinned in `Tools/ShaderBridge` and `Tools/VulkanBackend` | same | same |
| Shader translator (Ryujinx 1.3.2 parts, NativeAOT) | pinned in `Tools/MaxwellShaderCompiler` and `NativeShaders` | `~/SatchelBuild/m5` | `SATCHEL_SHADER_SOURCE`, `SATCHEL_SHADER_DEPS` |
| Ryujinx 1.3.3 full source (reference only, not linked) | tag 1.3.3 | `~/SatchelDeps/reference/ryujinx-1.3.3` | none |

The per-component READMEs under `Tools/` describe how each library was fetched and built
(`Tools/CPUProbe/README.md`, `Tools/MaxwellShaderCompiler/README.md`,
`Tools/ShaderBridge/README.md`, `Tools/VulkanBackend/README.md`). The bootstrap wraps those recipes rather than duplicating their compiler commands.
The legacy rename staging mode remains available for historical workflows.

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

## Rules for building

- One heavy build at a time. The Mac crashed when three workers compiled at once;
  `NativeShaders/guard.py` and the build driver wait for other compilers and keep a
  35 GiB disk reserve.
- Build outputs never go inside the checkout and never in `/tmp` (which is wiped on
  reboot). Use `~/SatchelBuild/<task>/`.
- Experiments use their own bundle IDs (`com.dell.satchel.<name>`) so the installed app
  and its library stay untouched.
