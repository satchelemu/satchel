# Local distribution artifacts

The tools produce a local unsigned IPA and a shared SideStore/AltStore Classic source.
They use Python's standard library and macOS codesign, with no added dependencies.
They do not publish, upload, install, contact an account or use a signing identity.
The ad hoc check uses codesign's `-` identity, which is not installable on an iPhone.

## Build, export and verify

The clean-Mac route in [BUILDING.md](../../BUILDING.md) (`scripts/clean_build.py`) runs
the build and packaging steps below for you, into its own work folder. To run them by
hand against configured dependencies, from the repository root:

```zsh
export SATCHEL_BUILD_ROOT="$HOME/SatchelBuild"
export SATCHEL_DEPS_ROOT="$HOME/SatchelDeps"
export SATCHEL_OUT="$HOME/SatchelBuild/session/distribution-build"
export ANYGAME_OUT="$HOME/SatchelBuild/session/distribution-guard"
nice -n 10 python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py --unsigned

nice -n 10 python3 -B scripts/build_ipa.py \
  --build-root "$SATCHEL_OUT" --out "$HOME/SatchelBuild/session/distribution-artifacts"
python3 -B scripts/build_ipa.py \
  --verify-resign "$HOME/SatchelBuild/session/distribution-artifacts/Satchel.ipa"
python3 -B NativeApp/Distribution/source.py generate \
  "$HOME/SatchelBuild/session/distribution-artifacts/Satchel.ipa" \
  --out "$HOME/SatchelBuild/session/distribution-artifacts"
python3 -B NativeApp/Distribution/source.py validate \
  "$HOME/SatchelBuild/session/distribution-artifacts/source.json" \
  "$HOME/SatchelBuild/session/distribution-artifacts/Satchel.ipa"
```

The guard waits for heavy work, uses nice 10, stops owned processes at 9 GiB RSS
(below the 10 GiB cap), and protects the 35 GiB disk reserve. Builds use two jobs.
The packager checks space for the staged app and ZIP before copying, then checks the
reserve per entry. Use a fresh output directory for each export. Existing artifacts
are never overwritten. The original build is untouched. The old `source.py export`
command delegates to `scripts/build_ipa.py`.

Outputs:

- `Satchel.ipa`, containing only `Payload/Satchel.app` and its resources.
- `Satchel.manifest.json`, with the IPA SHA-256 and size, each file's SHA-256, size
  and mode, version, build, minimum iOS, release date and requested entitlement keys.
- `Satchel.entitlements`, the account-neutral template for the eventual signer.
- `source.json`, after the generate step, with nonfunctional HTTPS placeholders.

The IPA has fixed 1980 ZIP timestamps, lexical ordering, 0755 directories and Mach-O
binaries, and 0644 resources. It carries no provisioning profile or code signatures.
The template contains AppIdentifierPrefix placeholders, never a team identifier.
The IPA omits the custom `SatchelPrivateKeychainGroup` Info.plist field: sideloaders
do not expand arbitrary Xcode variables. Existing app code then uses the installed
signature's default Keychain group and reads the team from the installed profile.
The original signed Xcode build keeps its resolved private group.
The packager rejects signed build settings, symlinks, extensions, nested apps and
known private content types. The only allowed NRO is a byte-identical copy of the
repository-authored synthetic `App/Resources/hello-horizon.nro`. Utility graphics
resources and the app's own native binaries are retained. No game package is an input.

Re-exporting identical build bytes with the same Python/zlib/codesign toolchain yields
identical IPA and manifest bytes, independent of source mtimes and Unix permissions.
This is packaging reproducibility. It does not make separate Xcode builds or native
dependency builds byte-identical.

`--verify-resign` checks the manifest, archive paths, modes, timestamps and unsigned
Mach-O files, extracts a disposable copy, ad hoc signs its code and verifies it with
`codesign --verify --deep --strict`. It reads back all entitlement values, requiring
both increased-memory-limit and extended-virtual-addressing to be true. Only the
copy's Keychain prefix is removed for this account-free check. The original IPA must
retain its hash, and the copy is deleted afterwards.

An unsigned executable has no signed entitlements for an installer to discover.
The IPA therefore includes `SatchelDistribution.entitlements` as a template resource
and the export provides the same template beside the IPA. These are declarations,
not grants. A real signer must apply the capabilities and resolve its own prefix.
Ad hoc verification establishes that re-signing can retain them, not that SideStore
or AltStore automatically reads this template. Store installation, capability
preservation, refresh and prepared-game loading remain hardware acceptance gates.

## Tracked metadata and source schema

`release.json` pins version, build, minimum iOS, release date and screenshot filenames.
The native app builder now explicitly uses its version and build in Info.plist. The
initial values preserve the existing canonical app's actual `1.0 (1)`; previously
XcodeGen emitted those defaults despite the legacy project settings saying `0.3.0 (8)`.
Bump the tracked release metadata before a future release. Export refuses version,
build or minimum-iOS drift from the built app.

The generator reads built Info.plist privacy declarations, including frameworks, and
the entitlement names copied from the generated build's entitlement input. Release
date comes from tracked metadata, not wall clock time or a file mtime. It computes
compressed IPA size and SHA-256 from the final bytes. Screenshot filenames come from
tracked metadata and dimensions come from the existing `docs/screenshots` JPEG headers.
No screenshots are created or changed.

The checked-in `source.schema.json` describes this generator's subset of the
[published AltStore source format](https://faq.altstore.io/developers/make-a-source),
reviewed 2026-11-04. It is a local schema, not an upstream-published JSON Schema.
It covers versions, landscape screenshot objects, permissions and privacy strings.
Legacy version fields and screenshotURLs support older clients. SideStore documents
[adding an AltSource](https://docs.sidestore.io/docs/advanced/url-schema).
The version's `sha256` is a Satchel integrity extension; clients are not assumed to
verify it. The external manifest is the authoritative local integrity check.
`schema.py` validates the schema's limited keyword set without third-party packages;
`validate` then compares every generated field with the IPA and tracked screenshots.

Default download, source, icon and screenshot URLs use `https://example.invalid/satchel`.
They cannot install anything. At release time the maintainer must provide a real
HTTPS base using `--base-url`, stage the existing screenshots under `screenshots/`
and a reviewed `icon.png`, and separately authorize hosting. No release is created
by any of these commands. Revalidate against the exact IPA before hosting it.

For an external export, `generate` and `validate` retain `--entitlements` and
`--build-date`. These explicitly supply missing metadata, but do not substitute for
the canonical packager's manifest and re-sign checks.

## Checks and remaining gates

```zsh
python3 -B NativeApp/Distribution/test_source.py
python3 -B NativeApp/Distribution/test_ipa.py
```

Synthetic tests exercise deterministic ZIP bytes, file modes and hashes, unchanged
inputs, private-input rejection, template policy, no overwrite, archive tampering,
source schema, privacy and screenshot metadata. They mock codesign and never count
as real signing evidence. Run the separate ad hoc check on the actual app too.

The [refresh journey](../../docs/distribution-validation.md) covers installed-signature
expiry and installer refresh, including prepared games and preservation of saves.
The Actions draft `.github/workflows/ipa-draft.yml` has manual dispatch only and its
job is hard-disabled. It outlines building an existing release tag with the clean-Mac
route (`scripts/clean_build.py`, see [BUILDING.md](../../BUILDING.md)), has read-only
permissions, no signing secrets and no upload step. Before enabling it, pin a runner
image with Xcode 27.2, run the route there once, confirm disk capacity and review the
complete workflow. It cannot currently produce a release on CI.
