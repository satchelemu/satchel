# Local distribution artifacts

The tools produce a local unsigned IPA and a shared SideStore/AltStore Classic source.
They use Python's standard library and macOS codesign, with no added dependencies.
They do not publish, upload, install, contact an account or use a signing identity.
The ad hoc check uses codesign's `-` identity, which is not installable on an iPhone.

## Release dry run (one command)

`scripts/release_dry_run.py` writes everything a release would publish into a fresh
folder outside the checkout, and publishes nothing. From a clean checkout of the commit
to release, with the dependency root set as in [BUILDING.md](../../BUILDING.md):

```zsh
python3 -B scripts/release_dry_run.py --out "$HOME/SatchelBuild/release-1.0"
```

It refuses to start unless `git status` shows no modified or untracked file, the
hygiene check (`scripts/check_repo_hygiene.sh`) passes, and no ignored key, dump,
identity or prepared file sits inside the checkout outside the private folders
(`local/`, `notes/`, `private/`, `scratch/` and the agent folders). It then builds the
unsigned app with `NativeApp/build.py --unsigned` under the build guard (or, with
`--clean-work <folder>`, takes the app of a `scripts/clean_build.py` work folder whose
`--check` passes and whose app step recorded this exact commit), and writes
`release/` with the IPA, its manifest, the entitlement template, `SHA256SUMS`, the
source in the published layout, this version's source entry, `RELEASE_NOTES.md` (from
ROADMAP.md and COMPATIBILITY.md at this commit) and `PUBLISHING_CHECKLIST.md` (the
manual steps with this release's values), plus `dry-run.json` with every check. It
exports the IPA twice and requires identical bytes, runs the ad hoc re-sign check, and
deletes the build afterwards unless `--keep-build` is given. `--expect-tag v1.0` also
requires that local tag to name the commit.

The whole run happens inside `sandbox-exec` with outbound IP traffic denied (the
dependency recipes' policy). Two exact binaries start outside it because a sandboxed
process cannot run them: the build guard's set-user-ID `/bin/ps`, and
`/usr/bin/sandbox-exec`, through which the Swift compiler starts its macro plugin server
under Swift's own sandbox profile (a sandbox cannot be applied inside another).
Everything else, git, xcodebuild and codesign included, stays inside. It never runs
`gh`, `git fetch`, `git push` or `git tag`.
`NativeApp/Distribution/test_release.py` checks the refusals, the sandbox (a loopback
connection inside it is denied) and that none of the release tools imports network
code. The maintainer's checklist is [docs/publishing.md](../../docs/publishing.md).

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
- `source.json`, after the generate step, with the published URL layout below (the
  URLs do not exist until the maintainer publishes).

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

## Published URL layout

`release.json` holds the URL templates, resolved per version (`{tag}` is `v{version}`):

| URL | Shape | Served from |
|---|---|---|
| IPA (`downloadURL`) | `https://github.com/satchelemu/satchel/releases/download/v1.0/Satchel.ipa` | the GitHub release asset of the tag |
| Source (`sourceURL`) | `https://raw.githubusercontent.com/satchelemu/satchel/main/source.json` | a tracked `source.json` on `main` |
| Icon (`iconURL`) | `.../satchelemu/satchel/v1.0/App/Assets.xcassets/AppIcon.appiconset/AppIcon.png` | the tracked app icon at the tag |
| Screenshots | `.../satchelemu/satchel/v1.0/docs/screenshots/library.jpg` and the other three | the tracked screenshots at the tag |

`source.py generate` and `validate` use this layout by default. The generator refuses a
template with another host, a repository other than `satchelemu/satchel`, an unknown
placeholder, a download, icon or screenshot URL without the tag, or an icon that is not
a tracked PNG. Icon and screenshots are served at the tag, so a published version never
changes under its users. `--base-url` keeps the older flat layout (base/Satchel.ipa,
base/icon.png, base/screenshots/, base/source.json) for a self-hosted mirror or a local
test server. None of these URLs exists until the maintainer publishes the release and
commits `source.json`; nothing here creates them.

No local copy of the AltStore or SideStore documentation or of any schema of theirs is on
this Mac (searched 2026-11-07); `source.schema.json` is the transcription made on
2026-11-04. The fields the clients really read are listed for confirmation in
[docs/publishing.md](../../docs/publishing.md#source-fields-to-confirm).

For an external export, `generate` and `validate` retain `--entitlements` and
`--build-date`. These explicitly supply missing metadata, but do not substitute for
the canonical packager's manifest and re-sign checks.

## Checks and remaining gates

```zsh
python3 -B NativeApp/Distribution/test_source.py
python3 -B NativeApp/Distribution/test_ipa.py
python3 -B NativeApp/Distribution/test_release.py
```

Synthetic tests exercise deterministic ZIP bytes, file modes and hashes, unchanged
inputs, private-input rejection, template policy, no overwrite, archive tampering,
source schema, the published URL layout, privacy and screenshot metadata, and the dry
run's tree, tag, output-folder and sandbox rules. They mock codesign and never count as
real signing evidence. Run the separate ad hoc check on the actual app too (the dry run
does).

The [refresh journey](../../docs/distribution-validation.md) covers installed-signature
expiry and installer refresh, including prepared games and preservation of saves.
The Actions draft `.github/workflows/ipa-draft.yml` has manual dispatch only and its
job is hard-disabled. For an existing release tag it runs the clean-Mac route
(`scripts/clean_build.py`, see [BUILDING.md](../../BUILDING.md)), its `--check`, the
distribution tests and hygiene, then the release dry run with `--clean-work` and
`--expect-tag`. It has read-only permissions, no signing secrets and no upload step, a
pinned runner label and Xcode path, and `actions/checkout` pinned by commit. Before
enabling it, follow "Enabling the CI draft" in [docs/publishing.md](../../docs/publishing.md).
