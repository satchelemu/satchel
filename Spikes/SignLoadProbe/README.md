# Sign Load Probe

Standalone feasibility probe, bundle ID `com.dell.satchel.signprobe`, built for the
maintainer's team (set `DEVELOPMENT_TEAM` in `project.yml` and `probeTeam` in the app
to yours). It does not modify Satchel or the Zelda app. The complete device
report and retrieved results are in `~/SatchelBuild/spike/`.

## Build and run

On macOS with Xcode, XcodeGen, and the team's Apple Development signing identity:

```zsh
# Reuse the existing OpenSSL library in ~/SatchelBuild/spike/openssl-ios.
/bin/zsh Spikes/SignLoadProbe/Scripts/build-payload.zsh
/bin/zsh Spikes/SignLoadProbe/Scripts/format-check.zsh
/bin/zsh Spikes/SignLoadProbe/Scripts/build.zsh
xcrun devicectl device install app \
  --device 00008160-001A30E02E600036 \
  "$HOME/SatchelBuild/spike2/DerivedData/Build/Products/Release-iphoneos/SignLoadProbe.app"
xcrun devicectl device process launch \
  --device 00008160-001A30E02E600036 \
  --terminate-existing --console com.dell.satchel.signprobe --probe-test S1a-copy
```

Other isolated launch modes: `S1a-bundle`, `S1a-external`, `S1b-shared`,
`S1b-copy`, `S2`. `S1a-external` expects a separately Mac-signed synthetic dylib
transferred after installation to `Documents/postinstall-probe.dylib`.
The report records the exact compilation, signing and transfer commands.
Each console mode writes `results.json` and exits normally after completion.
A normal launch leaves the landscape UI open. S1c and S1d share one signing-matrix
button. Eight rows display results, with raw JSON available below them. Each
attempt is recorded before the next begins. A returned dlopen error does not
stop later attempts. A process termination cannot be caught by the matrix;
on relaunch, unfinished attempts are shown as interrupted.

```zsh
xcrun devicectl device copy from \
  --device 00008160-001A30E02E600036 \
  --domain-type appDataContainer --domain-identifier com.dell.satchel.signprobe \
  --source Documents/results.json --destination "$HOME/SatchelBuild/spike2/results.json"
```

The app copies its own installed executable and provisioning profile to Documents
for signature inspection. These are synthetic probe artifacts. It never exports
an imported identity or passphrase. The signer accepts PKCS#12 bytes in memory,
checks certificate validity and team OU, and retains the private key only in the
signing session. Use Forget identity or terminate the app to end that session.
This is a feasibility prototype, not a production credential-management design.

## User-only S1c and S1d step

1. On the Mac, open Keychain Access, select login, then My Certificates. Find
   your `Apple Development: <your Apple ID> (<certificate ID>)` identity. Check that the certificate's
   Organizational Unit is your team ID. Expand it to confirm its private key exists.
   Do not choose the Developer ID Application identity.
2. Select the certificate together with its associated private key and choose
   File > Export Items. Export Personal Information Exchange (`.p12`) outside
   the repository. Choose and enter the export passphrase yourself. Handle all
   Keychain authorization or login prompts yourself.
3. AirDrop that file to the iPhone and save it in Files, or place it in your
   iCloud Drive and wait for it to appear in Files. Do not send the file or its
   passphrase to the agent.
4. Open Sign Load Probe on the phone. Tap Import .p12 from Files and choose it.
   Type its passphrase in the phone's secure field, then tap Unlock identity.
   The field is cleared after import. The identity is not saved by the app.
5. Tap **Run signing matrix** once. It runs A, B, C, D for S1c, then S1d.
   S1c signs the unsigned compiled resource; S1d creates a Mach-O containing
   `mov w0, #42; ret` on the phone. Each attempt uses a fresh file and performs
   `dlopen`, `dlsym`, and a call. Scroll the result list to see all eight rows.
6. Tap **Forget identity** when done. If the app terminates, preserve its crash
   log and results, reopen, reimport, and repeat the matrix. It cannot continue
   across a process termination without reimporting the in-memory identity.
   Results are in `Documents/results.json`, keyed `S1c-A` through `S1d-D`.
   Signed synthetic files are retained under
   `Documents/signing-matrix/<run UUID>/<test>-signed.dylib`. No identity is exported.

| Variant | Entitlements | Page size | Special slots | Phone expectation |
| --- | --- | --- | --- | --- |
| A | None | 16 KiB | 2 | Closest match to Mac codesign, unverified |
| B | None | 4 KiB | 2 | Isolate page size, unverified |
| C | Empty XML and DER | 16 KiB | 7 | Isolate paired entitlements, unverified |
| D | Empty XML only | 4 KiB | 5 | Previous output, expected failure control |

All variants use the dylib's `__TEXT` file offset and file size as exec-segment
base and limit, and zero exec-segment flags. The corrected parser reads these
fields at CodeDirectory byte offsets 64, 72, and 80.

The Release build uses `nice -n 10`, two Xcode jobs, and output under
`~/SatchelBuild/spike2/`. Only the device owner may install or launch it.
The spike2 report is `~/SatchelBuild/spike2/report.md`. This build has not
run on the phone and has not played a game.

The Mac format script creates its own disposable identity, with team
`PROBETEST1`, only under `~/SatchelBuild/spike2/format/`. It accepts no user's
passphrase. A self-issued fixture uses a designated requirement pinned to its
leaf certificate; Apple-issued identities retain zsign's Apple/WWDR requirement.
The script validates all eight self-signed variants, creates six Apple-team
Mac references (A/B/C for both payloads), and checks every code and special-slot
hash using `Scripts/check-signatures.py`. Variant C's DER blob must match Mac
codesign byte for byte. `format/structure.json` preserves all parsed fields.
An ad-hoc macOS generated-image control checks layout and returns 42.
See `THIRD_PARTY.md` for licenses and the pinned signer source.

## Interpretation

S1a-copy alone tests execution of a bundled image from the data container.
S1a-external tests an image absent from the installed bundle. A positive external
result establishes Mac-signed post-install native execution for this development
profile. It does not establish on-phone signing, free-team behavior, expiry
handling, or a Switch runtime. No game has been played by this probe.

S1b uses an isolated 16 KiB aligned assembly function with no PC-relative data.
It first reserves a destination with `vm_allocate`, then calls `vm_remap` with
fixed overwrite, preserving read and execute protection. Both copy modes are
separate tests. It does not test writing to remapped executable pages.

S2 reserves and immediately releases each of 4, 8, 16, 32, and 64 GiB without
touching it. A second phase retains 256 MiB allocations, fills every byte with
random data, and checks process headroom and system memory pressure every 16 MiB.
It stops with at least a 512 MiB process budget margin or earlier on pressure,
then releases its allocations. It reports the last partial step separately.
Touched bytes are not a guarantee that every page remains resident; iOS can
compress or evict pages. The result is a safe observed lower bound, not a
jetsam-limit measurement.
