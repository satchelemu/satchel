# Publishing a release (maintainer checklist)

Nothing in this repository publishes anything. Every tool here writes local files only;
tagging, creating the GitHub release, uploading assets and committing the published
source are the maintainer's own reviewed steps, listed below. `scripts/release_dry_run.py`
prepares all of it in one command and writes a filled-in copy of this checklist
(`PUBLISHING_CHECKLIST.md`) next to the files it describes.

## What gets published where

| What | Where | Made by |
|---|---|---|
| Tag `v<version>` (for example `v1.0`) | `github.com/satchelemu/satchel` tags | the maintainer (`git tag -a`, `git push origin <tag>`) |
| Release text | the GitHub release of that tag | the dry run's `RELEASE_NOTES.md`, reviewed |
| `Satchel.ipa`, `Satchel.manifest.json`, `Satchel.entitlements`, `SHA256SUMS` | assets of that release; the IPA at `https://github.com/satchelemu/satchel/releases/download/<tag>/Satchel.ipa` | the dry run, uploaded unchanged |
| `source.json` (SideStore and AltStore Classic source) | `source.json` at the root of `main`, served at `https://raw.githubusercontent.com/satchelemu/satchel/main/source.json` | the dry run, committed by the maintainer after the release exists |
| Icon and screenshots | already tracked; the source points at them at the tag (`App/Assets.xcassets/AppIcon.appiconset/AppIcon.png`, `docs/screenshots/*.jpg`) | nothing new |

The URL templates live in `NativeApp/Distribution/release.json` (`publish`), so a review
of that file is a review of every published address.

## Steps

1. **Prepare the commit.** In a reviewed commit on `main`, bump `version`, `build` and
   `date` in `NativeApp/Distribution/release.json` (the app's Info.plist takes its version
   from there, and the tag is `v<version>`, so every release needs a new version). Update
   ROADMAP.md, COMPATIBILITY.md and README.md: the release notes are made from them.
2. **Run the dry run** from a clean checkout of exactly that commit, with the dependency
   root of [BUILDING.md](../BUILDING.md) set:

   ```zsh
   python3 -B scripts/release_dry_run.py --out "$HOME/SatchelBuild/release-<version>"
   ```

   or, after a full clean-Mac route run, `--clean-work ~/SatchelClean` to package that
   folder's app instead of building again. It refuses a dirty tree, a failing hygiene
   check or stray private files, runs offline, and prints PASS for each check.
3. **Review** `release/` in the output folder: `RELEASE_NOTES.md`, `source.json`,
   `Satchel.entitlements`, `SHA256SUMS` (`shasum -a 256 -c SHA256SUMS`) and
   `dry-run.json`. Check that nothing personal (name, email, team ID, certificate name)
   appears in any of them.
4. **Phone gates** (below). Do not publish an IPA whose installs lose a memory entitlement.
5. **Publish**, each a single reviewed action, in this order:
   1. `git tag -a v<version> <commit> -m "Satchel <version> (<build>)"`, then
      `git push origin v<version>`.
   2. Create the GitHub release for the tag with `RELEASE_NOTES.md` as its text and the
      four assets, unchanged (the generated checklist has the exact `gh release create`
      line).
   3. Download the published IPA and compare its SHA-256 with `SHA256SUMS`.
   4. Copy `release/source.json` to `source.json` at the repository root, commit it on its
      own and push it. It goes last because it points at the release asset.
   5. Open the source URL and check that it serves that file, and that its icon and
      screenshot URLs load.
6. **After publishing**, add the source on the phone in SideStore and AltStore Classic,
   update Satchel from it, repeat the Device check, and record the release in ROADMAP.md
   and README.md.

## Phone gates

From [the distribution acceptance journey](distribution-validation.md), on the exact IPA:

- Install with SideStore and with AltStore Classic, with your own account, in place over
  the existing app (back up saves first; never uninstall to troubleshoot).
- Settings > Device check must show increased memory and extended virtual addressing in
  the running signature for each installer.
- Refresh with each installer must advance the expiry and keep the library and saves.
- After publishing: install and update from the published source, not only from Files.

A local reference to weigh before the first phone run: the NX2Apple notes kept on the
maintainer's Mac (`ENTITLEMENTS.md` of the pinned NX2Apple checkout, MIT) report that
sideloaders choose the entitlements to request from the code signature they replace, so
an IPA whose executable has no signature carries no entitlement request, and that
AltStore Classic 2.3 kept the increased memory limit only after their IPA gained an ad
hoc placeholder signature carrying it. Satchel's IPA is fully unsigned and carries its
entitlements only as a template file. If the Device check shows the memory entitlements
missing after an AltStore or SideStore install, that is the likely cause; the fix (an ad
hoc placeholder signature in `scripts/build_ipa.py`) is a decision listed below.

## Decisions before the first release

- **IPA signature.** Keep the unsigned IPA (current), or add an ad hoc placeholder
  signature carrying the entitlements so installers can read them (see above). The
  keychain group template (`$(AppIdentifierPrefix)...`) cannot be resolved inside an ad
  hoc signature, so a placeholder would carry the memory entitlements and the app group
  only. Probed on the Mac on 2026-11-07 on a copy of the dry run's executable: ad hoc
  signing it twice with those three entitlements (no timestamp, no team) gave
  byte-identical results that `codesign -d --entitlements` reads back, so a placeholder
  would keep the IPA reproducible. Whether SideStore and AltStore then keep the
  entitlements is the phone question.
- **Tag scheme.** `v<version>` (current) means every release bumps `version`; use
  `v<version>-<build>` in `release.json` instead to allow rebuilds of one version.
- **Where the source lives.** `source.json` at the root of `main` served by
  raw.githubusercontent.com (current), or GitHub Pages, or a release asset. Changing it
  later means users re-add the source.
- **Version history.** The source lists only the newest version (schema `maxItems: 1`);
  AltStore can show older versions if the source keeps them.
- **Release assets.** Whether to attach `Satchel.manifest.json` and
  `Satchel.entitlements` besides the IPA and `SHA256SUMS`.
- **Tag signing.** Annotated tag (current suggestion) or a signed tag; signing a tag uses
  the maintainer's own key and is never done by these tools.
- **The CI draft.** Whether to enable it at all (below), and whether it may keep the
  dry-run folder as a workflow artifact.

## Source fields to confirm

There is no local copy of the AltStore or SideStore documentation or of any schema of
theirs on the Mac (searched 2026-11-07). `NativeApp/Distribution/source.schema.json` is
the transcription of the AltStore source documentation made on 2026-11-04, and the dry
run validates against it. Before the first release, confirm against the current
upstream documentation:

- Source: `name`, `identifier`, `sourceURL`, `apps`, `news` (empty), and whether
  `subtitle`, `iconURL`, `website` or `tintColor` are wanted.
- App: `name`, `bundleIdentifier`, `developerName`, `localizedDescription`, `iconURL`,
  `screenshots` as `{imageURL, width, height}` objects, `appPermissions` with
  `entitlements` as a list of names and `privacy` as a dictionary of usage strings.
- Version: `version`, `buildVersion`, `date`, `downloadURL`, `size`, `minOSVersion`, and
  whether AltStore or SideStore verify `sha256`.
- Legacy app-level fields (`version`, `versionDate`, `downloadURL`, `size`,
  `minOSVersion`, `screenshotURLs`): whether the current SideStore release still needs
  them.
- Whether either client refuses an app whose signed entitlements differ from
  `appPermissions` (an unsigned IPA has none).
- That both clients accept a source served by raw.githubusercontent.com (`text/plain`)
  and follow the redirect GitHub uses for release asset downloads.

## Enabling the CI draft

`.github/workflows/ipa-draft.yml` is manual dispatch only and its job is `if: false`.
For an existing tag it runs the clean-Mac route, `clean_build.py --check`, the
distribution tests and hygiene, then the release dry run with `--clean-work` and
`--expect-tag`. It uploads nothing. Before removing `if: false`:

- Confirm that the runner label `macos-27` offers macOS 27.2 and Xcode 27.2 (27B52) at
  `/Applications/Xcode_27.2.app` (`scripts/setup_tools.py` refuses any other Xcode).
- Confirm that the pinned `actions/checkout` commit is the v4.2.2 release.
- Run the clean route on that runner once and confirm time (the Mac run took about 28
  minutes) and disk (about 8 GiB of work folder; the build guard wants 36 GiB free).
- Decide about artifacts (above).

The draft was checked on the Mac on 2026-11-07 by parsing it with Ruby's YAML and by the
GitHub Actions language server of the installed VS Code extension (0.32.3), run offline:
no diagnostics, while a deliberately broken copy was reported.

## What still needs the phone

Everything the Mac cannot show: that SideStore and AltStore Classic keep both memory
entitlements and the app group on install and on refresh, that refresh advances the
expiry and keeps the library, saves and prepared games, and that installing and updating
from the published source works. See [distribution-validation.md](distribution-validation.md).
