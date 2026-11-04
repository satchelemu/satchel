# Single Satchel build

From the repository root, on macOS with the existing Xcode, XcodeGen and durable dependencies:

```zsh
nice -n 10 python3 -B NativeApp/build.py
```

Output: `~/SatchelBuild/oneapp/app/DerivedData/Build/Products/Release-iphoneos/Satchel.app`.
Bundle ID `com.dell.satchel`, display name Satchel, normal icon. The internal project,
product and scheme remain Satchel. No install, launch, transfer or game extraction occurs.
The lower-level `NativePrepare/build-dev.py` now also defaults to Satchel. Only its explicit
`--migration-bridge` option emits the temporary Dev update. `NativeHost/build.py` without
archive-generation options delegates to this canonical build.
Use `SATCHEL_OUT` to select another external build directory. `--unsigned` is an explicit
compile-only option, with signature and provisioning checks reported as skipped.

This command rebuilds the three native host archives from the repository's bridge/display
sources and pinned durable NX2Apple source, then builds the app with Prepare, the native
rewriter, signer, native player and pause menu. Its local host cache is invalidated by a
source fingerprint. It does not consume another worker's generated project or host archives.
Builds wait for active heavy compilers and shader drivers, use nice 10 and two jobs. The
host guard requires 35 GiB free; Prepare requires 15 GiB. Generic host utility shaders are
built, never game shaders. All outputs stay outside the checkout.

Existing dependencies: `~/SatchelDeps/NX2Apple/engine`, the durable snapshot referenced
by `project.yml`, the locked Metal toolchain and materialization metadata under
`~/SatchelBuild/zelda`, Capstone 5.0.7 under `~/SatchelBuild/rewriter/capstone-src`, and
OpenSSL 3.6.3 under `~/SatchelBuild/spike/openssl-ios` (override with `SATCHEL_OPENSSL_IOS`).
First-use shaders additionally consume the M5 native translator sources and validated
artifacts under `~/SatchelBuild/m5`. Override their locations with
`SATCHEL_SHADER_SOURCE` and `SATCHEL_SHADER_DEPS`. After integrating `phone-shaders`,
the local `NativeShaders` sources are used automatically. The source staging manifest
records input digests. The app embeds/signs the staged `SatchelShaderTranslator.framework` and links
the pinned SPIRV-Cross bridge and M5's BSD zstd archive for native NCZ and registered-module decoding. The original fixture probe stays separate.
No new packages were downloaded. These are provisioned build prerequisites; a clean Mac
needs them before this command can reproduce the app. `project.yml` supplies the base
library target and dependency list to this generator; generating it alone is no longer the
complete app build.

The signed build runs the existing host checks, parameterized for the selected bundle:
strict signature, team and profile, both memory entitlements, landscape, retained native
registrations/symbols and absence of guest code/content. It also verifies the normal icon,
`com.dell.satchel.prepare.*`, App Group, private/shared Keychain access groups, native
rewriter/signer symbols and registry bytes. A provisioning failure is fatal.

Developer sample/import/export and interpreter tools are hidden by default. Tap the Satchel
name seven times under Settings, About to expose the Developer controls switch. Native play,
Prepare and signing remain normal features. Automation launch flags also require this switch.
`SATCHEL_NATIVE_DEV` is removed from the app code. Only the minimal simulator fixture omits
native UI, using `SATCHEL_UI_PREVIEW_MINIMAL`.

## One-time Dev update

```zsh
nice -n 10 python3 -B NativeApp/build.py --migration-bridge
```

Output: `~/SatchelBuild/oneapp/bridge/DerivedData/Build/Products/Release-iphoneos/SatchelDev.app`.
This temporary `com.dell.satchel.dev` update shows only the migration screen. It cannot
import, prepare or play, preventing changes to the migration source. It is not a second
ongoing app product. See [migration design and steps](../NativeMigration/README.md).

## Navigation tests

The canonical generated project contains `SatchelNavigation`, with all native navigation
journeys enabled against `com.dell.satchel`. It excludes the mutating legacy UI suite.
See [test commands](../Tests/UI/Navigation/README.md). The full native app needs a physical
phone to run. The Simulator fixture uses real screens with synthetic runtime data.

## Reusable game sessions

The canonical build defaults `SATCHEL_SESSION_BOUNDARY=1` and fingerprints the
session overlays. Quit drains in-flight save commits, stops and joins the native
session, then returns to the home library. Library remains pause and return.
See [lifetime and validation](../NativeHost/session/README.md). The signed build
alone does not establish phone reload behavior; the orchestrator runs that journey.
