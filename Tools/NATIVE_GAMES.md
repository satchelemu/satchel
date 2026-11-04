# Native Switch game development path

The user now requires phone-only imports and execution; see [the active runtime
direction](PHONE_RUNTIME.md). This Mac pipeline is retained as a benchmark, not
the delivery path for that requirement. The phone app still executes only limited
NROs; its new XCI/NSP import path stores packages without executing them. These tools prepare
a separate, per-game native development build using NX2Apple/nxnative. They are
not a general XCI/NSP emulator inside Satchel, and do not establish App Store
eligibility. The purpose of this path is to establish actual game execution on
the user's phone using existing CPU, graphics and Horizon implementations.

## Source and dependencies

Reference source: [NX2Apple](https://github.com/LNPANDY/NX2Apple), pinned to
`8227f80f9b381c680611303319656c4e0c806a50`. Its host project is MIT; individual
third-party licenses remain in the checkout. The package reader adaptation keeps
its notices in [SwitchContent/THIRD_PARTY_NOTICES.md](SwitchContent/THIRD_PARTY_NOTICES.md).

The local tool directory currently is `/tmp/satchel-switch-integration`:

- `NX2Apple/`: pinned reference checkout, including its shader translator.
- `dotnet/`: .NET SDK 10.0.403, from Microsoft's release metadata and verified
  against its SHA-512. `dotnet-source.json` records the download and hash.
- `venv/`: isolated Python environment with lz4, Capstone 5 and fonttools.
- `nuget/`: LibHac 0.19.0, locked by the inspector's `packages.lock.json`.
- `content-inspector/`: compiled metadata inspector; intermediate files are in
  `content-artifacts/` outside the app repository.
- `bin/xcrun`: symlink to `scripts/native_xcrun.py` for this build environment.
- `native-toolchain.json`: selects the installed Metal toolchain's directory.

Xcode 27.2, XcodeGen, zstd and make were already available. glslang 16.6.0 and its
SPIR-V dependencies were installed through Homebrew. Apple's Metal component was
installed through Xcode. The newest Metal compiler differs from the reference
fingerprint; an already mounted older Apple toolchain has the exact required
`32023.921.6` compiler. The selector executes that compiler for Metal tools and
forwards other commands to Apple's xcrun. It does not alter the lock, spoof a
version, or allow an unverified compiler. Its path may need rediscovery after an
OS/toolchain update or reboot; `doctor` detects that condition.

Build the inspector from the repository root:

```sh
DOTNET_ROOT=/tmp/satchel-switch-integration/dotnet \
NUGET_PACKAGES=/tmp/satchel-switch-integration/nuget \
/tmp/satchel-switch-integration/dotnet/dotnet build \
  Tools/SwitchContent/SwitchContent.csproj -c Release -p:RestoreLockedMode=true \
  --artifacts-path /tmp/satchel-switch-integration/content-artifacts \
  -o /tmp/satchel-switch-integration/content-inspector
```

## Inspect and prepare

Use the inspector directly for read-only metadata checks:

```sh
/tmp/satchel-switch-integration/dotnet/dotnet \
  /tmp/satchel-switch-integration/content-inspector/SwitchContent.dll \
  inspect --keys /path/to/prod.keys --base /path/to/base.xci \
  --update /path/to/update.nsp
```

It associates updates by internal Application/Patch IDs and numeric version,
not by filenames. Missing optional Patch DeltaFragments are reported explicitly;
missing required game content still fails. It does not verify every content hash
or prove that the selected game will run. Source packages are opened read-only.
For bundled packages, the inspector accepts `--application-id` with a verified
16-digit internal title ID. The companion accepts the same option to confirm the
selected title, but currently refuses native preparation of multi-application
packages: the pinned extractor cannot reliably select a specific member.

Check the toolchain and prepare on a volume with enough free space:

```sh
python3 scripts/switch_native.py doctor
python3 scripts/switch_native.py prepare \
  --base /path/to/base.xci --update /path/to/update.nsp \
  --keys /path/to/prod.keys \
  --work-dir /Volumes/BuildDrive/Satchel/Zelda \
  --team-id YOURTEAMID
```

Use a new dedicated work directory outside this repository and the tool checkout.
The command checks free space before reading cloud-backed packages, validates the
pair, requires a registered profile/version and required shader source versions,
and creates canonical-name symlinks to the originals. The upstream pipeline then
merges content, rewrites NSOs, translates shaders, and prepares an Xcode project.
Game data stays separate from the signed app. No game or key file is copied into
this repository. No signing or installation happens in this command.

Review the extraction's module build IDs and any derived-profile notices before
building/signing. A prepared project is explicitly `prepared_unvalidated`, not
playable or release-ready. Use its separate `com.dell.satchel.native.<id>` bundle
ID to preserve the installed Satchel app and library. Install through Xcode,
transfer the generated RomFS image as the upstream pipeline documents, then
verify launch, rendering, controls, saves and stability on the actual phone.

Preparation currently refuses nonempty work directories; resume is not yet
implemented. Keep failed output for diagnosis instead of overwriting it.

## Validation

The inspector's `self-test` exercises synthetic binary archives and metadata.
`PYTHONDONTWRITEBYTECODE=1 python3 Tests/test_switch_native.py` tests the companion
using synthetic metadata and harmless subprocesses. Real-game evidence and the
current blocker are recorded in [the validation record](../Validation/switch-native.md).
