# Building Satchel from source

Honest summary first: as of 2026-11-03 the app builds on the maintainer's Mac and nowhere
else, because it links about twenty prebuilt native libraries that live in a dependency
snapshot outside the repository. Making this reproducible is the first item on the
[roadmap](ROADMAP.md). Signing is no longer tied to one account: the build reads your
team from your Mac, and the app reads its own team at run time. This page documents what the build expects so that work can
start, and so that you can build the parts that do not need the snapshot.

## Tools

- macOS with Xcode 27.2 (iOS 27.1 SDK) and command line tools
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.45 or newer (`brew install xcodegen`)
- Python 3.12 or newer (no third-party packages)
- .NET SDK 10.0.403 with the pinned NativeAOT 10.0.14 packages, for the shader translator
- An Apple developer account, and a device with Developer Mode on

## Native dependencies the build expects

All paths are the defaults; every one can be overridden with the environment variable
named next to it. The pins are the ones the maintainer validated against.

| Dependency | Pin | Expected location | Override |
|---|---|---|---|
| NX2Apple / nxnative engine source | commit `bbc3b73784751d2a706932dc69fd67627e3445d4` plus the 34 recorded integration patches | `~/SatchelDeps/NX2Apple/engine` | `SATCHEL_DEPS_ROOT` |
| Interpreter-era libraries (VIXL CPU probe, shader bridge, Vulkan backend, guest graphics) | see each `Tools/*/README.md` | `~/SatchelDeps/tmp-snapshot-2026-11-01/...`, referenced from `project.yml` | edit `project.yml` |
| Capstone | 5.0.7 | `~/SatchelBuild/rewriter/capstone-src` | `SATCHEL_BUILD_ROOT` |
| OpenSSL for iOS | 3.6.3, static | `~/SatchelBuild/spike/openssl-ios` | `SATCHEL_OPENSSL_IOS` |
| MoltenVK | v1.4.2 | built by `Tools/VulkanBackend` scripts | see that README |
| SPIRV-Cross, SPIRV-Tools, SPIRV-Headers | pinned in `Tools/ShaderBridge` and `Tools/VulkanBackend` | same | same |
| Shader translator (Ryujinx 1.3.2 parts, NativeAOT) | pinned in `Tools/MaxwellShaderCompiler` and `NativeShaders` | `~/SatchelBuild/m5` | `SATCHEL_SHADER_SOURCE`, `SATCHEL_SHADER_DEPS` |
| Ryujinx 1.3.3 full source (reference only, not linked) | tag 1.3.3 | `~/SatchelDeps/reference/ryujinx-1.3.3` | none |

The per-component READMEs under `Tools/` describe how each library was fetched and built
(`Tools/CPUProbe/README.md`, `Tools/MaxwellShaderCompiler/README.md`,
`Tools/ShaderBridge/README.md`, `Tools/VulkanBackend/README.md`). Those are the scripts
to turn into one fetch-and-build step.

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
host project. The app lands at
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
