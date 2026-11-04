# Local distribution artifacts

Run from the repository root on macOS with Python 3.9 or later and the existing Xcode
tools. No Python packages are required. Nothing here signs with an identity, contacts
an account, hosts files, uploads, or installs on a device.

## Build and export

Use the canonical builder, not the old project.yml-only build. With the provisioned
dependencies described in [NativeApp](../README.md):

```zsh
SATCHEL_OUT="$HOME/SatchelBuild/dlc/distribution-build" \
ANYGAME_OUT="$HOME/SatchelBuild/dlc/distribution-checks" \
nice -n 10 python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py --unsigned

nice -n 10 python3 -B NativeApp/Distribution/source.py export \
  --build-root "$HOME/SatchelBuild/dlc/distribution-build" \
  --out "$HOME/SatchelBuild/dlc"

python3 -B NativeApp/Distribution/source.py generate "$HOME/SatchelBuild/dlc/Satchel.ipa"
python3 -B NativeApp/Distribution/source.py validate \
  "$HOME/SatchelBuild/dlc/source.json" "$HOME/SatchelBuild/dlc/Satchel.ipa"
```

The guard waits for heavy work, uses nice 10, stops owned processes at 9 GiB RSS
(below the 10 GiB cap), and stops before the 35 GiB disk reserve. Builds use two jobs.
The export step can also be used on an existing canonical Release build without
rebuilding it. It verifies the generated project's entitlement input, copies the app
into Payload, removes signatures and provisioning profiles from that copy, verifies
every Mach-O is unsigned, and writes Satchel.ipa. The original build stays untouched.
The export rejects nested apps, extensions, symlinks and known private game/key inputs.

The IPA is an unsigned handoff artifact. Its source declarations do not grant
capabilities. The eventual signer must apply the required entitlements and a matching
profile using the user's own account. Removing binary signatures also removes their
entitlements: generic stores may not reconstruct Satchel's required capabilities from
this export. Installer capability preservation needs a real-device check before this
can be called a working public distribution. No ad hoc signing is performed here.

For repeat runs, use a fresh --out directory. Existing IPA and JSON files are not
overwritten. Output defaults to ~/SatchelBuild/dlc.

## Source metadata

The schema follows the [official AltStore source format](https://faq.altstore.io/developers/make-a-source).
The generator emits versions and appPermissions, plus legacy version fields for
SideStore clients. Its validator checks the emitted schema's required fields, types,
date, HTTPS URLs, positive byte size, identity, privacy dictionary and entitlement
array. It then compares every field against the IPA. It is a validator for this
generator's output, not a general validator for all possible store source features.

Version, build number, minimum iOS and privacy descriptions come from the built
Info.plist. Framework privacy declarations are included too; conflicting or non-string
declarations fail explicitly. Entitlements are not normally in Info.plist. Export
records only their names from the generated Satchel.entitlements and any embedded
binary signatures into a SatchelDistribution dictionary in the copied Info.plist.
No entitlement values, account identities or certificates are written to the source.
That dictionary also records the original executable's modification time in UTC as
the build date. The source's release date uses that build date, not export time.
Size is the final compressed IPA's byte count.

For an IPA exported elsewhere, provide --entitlements with that build's generated
entitlements plist and --build-date with an ISO 8601 timestamp including a timezone
to both generate and validate. The external export must be the same single Satchel
app with no extensions. The caller must provide the union of any embedded binary
entitlement names in that plist. Missing metadata fails rather than inventing values.

Default source, download and icon URLs use https://example.invalid/satchel. These are
deliberately nonfunctional placeholders. Screenshots and screenshotURLs are empty
placeholder arrays. No game image or personal screenshot is included. Before a later,
separately authorized publication, provide a real HTTPS directory with --base-url,
provide icon.png and optional landscape screenshots, then host source.json and the
exact Satchel.ipa together. Revalidate after any IPA change. Bump the app version or
build number before an update; this tool only describes the existing build.

Schema validation does not check URL reachability or prove that a store can sign,
install, refresh or run this app. [Installation help and current limits](../../docs/install.md)
are also bundled under Getting started, Install and refresh help.

## Checks

```zsh
python3 -B NativeApp/Distribution/test_source.py
```

Synthetic tests cover metadata extraction, privacy and entitlements, schema rejection,
unsafe archives and mismatches. Run generate and validate on the real exported IPA
as well. The orchestrator must separately verify the phone journeys in the worker
report before claiming installation or play works for a new account.
