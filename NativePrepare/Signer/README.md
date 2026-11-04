# Satchel on-phone signer

`PhonePreparedGameSigner` implements `PreparedGameSigner`. The dev Prepare screen
loads its identity from the device Keychain before starting content work. The
pipeline signs after writing the native container and assets, verifies before
returning `signed`, then binds the final dylib hash into the manifest and publishes
the package. The host package verifier is unchanged. No per-game Mac signing is
part of this path. The command-line content harness explicitly selects the old
unsigned signer for content-only diagnostics.

## Identity storage

Open Prepare game, choose Import signing identity, select a PKCS#12 and enter its
passphrase in the phone's secure field. The importer reads at most 1 MiB directly
from the selected security-scoped URL and never copies it to app storage. It checks
the private key, certificate team, Apple Development common name, code-signing
usage and chain to the embedded Apple roots on iOS. Import requires a currently
valid certificate chain. The required team is the one the running app was signed with
(`SatchelSigningIdentity.applicationTeam`, read from the app's own Keychain group and
provisioning profile), so any developer account works with its own build.

OpenSSL re-encodes the identity and supplied certificate chain in memory with an
empty PKCS#12 password. This normalized identity is stored as a Keychain generic
secret, not as an app file or a UserDefaults value. Keychain encryption is the
protection for this envelope. The item has `kSecAttrSynchronizable = false` and
`kSecAttrAccessibleWhenUnlockedThisDeviceOnly`. It is not a Secure Enclave key:
zsign requires the imported software private key in memory during signing.
The original passphrase is not retained in this envelope or in the signer object.
The UI clears it on import, cancellation, disappearance and loss of active state.
Mutable native secret storage is cleansed on release. Swift and platform string
copies cannot be guaranteed to be zeroized. No secret is written to diagnostics.

Certificate name, team and expiry appear only in the identity section, hidden
while inactive. Within 30 days of expiry the section warns to renew and import
again. Expired identities remain inspectable and removable, but import storage
and signing reject expired credentials. Replacing the item uses `SecItemUpdate`,
so a rejected replacement preserves the existing identity. Remove signing
identity deletes only this app's identity item. It does not remove existing games
or the user's original export in Files. The user should delete that export and
its Recently Deleted copy after successful import.

No PKCS#12, private key or original passphrase is placed in a prepared package.
The public leaf certificate and CA chain are necessarily embedded in the dylib's
CMS signature. This is public signature metadata, not an exported private identity.
No certificate metadata is added to the manifest, logs or crash diagnostics.

## Signature and fresh-file contract

The single recipe is the phone-proven spike variant A: no entitlements, SHA-256
CodeDirectory, 16 KiB pages, flags zero and the certificate's team. zsign includes
the supplied CA chain, or the matching embedded WWDR intermediate and Apple root.
Signing rechecks certificate and chain validity. zsign diagnostics are disabled
at compile time in Satchel; user-facing errors are fixed strings.

The signer makes a private `.sign-UUID` directory inside Prepare's staging tree:

1. Plain-write the unsigned input into `mapped.dylib`.
2. Sign that scratch inode and release zsign's writable mapping.
3. Open a unique `fresh-UUID.dylib` with `O_CREAT | O_EXCL | O_NOFOLLOW` and stream the signed
   bytes through ordinary writes, then synchronize and close.
4. Verify the fresh file with a bounded independent parser. Check thin ARM64
   dylib commands, signature bounds, unique nonoverlapping slots, exact recipe,
   team, identifier, exec segment, every code page and special-slot hash. Verify
   the CMS detached signature and require its signer to match the imported leaf.
5. Atomically rename only that fresh file over the staging `game.dylib`.

Never load `mapped.dylib`. No clonefile, copyfile or writable mapping is used for
its successor. Failure and cancellation leave the input unchanged and remove the
operation's scratch directory. A killed process can leave an unpublished Prepare
staging tree, as in the existing pipeline. zsign itself is synchronous; cancellation
is checked before and after it, throughout streaming and during hash verification.

## Dependencies and builds

The existing MIT zsign subset moved here from `Spikes/SignLoadProbe/Vendor`. The
spike now refers to this shared copy. Its pin remains
`9299685bab7ef99827e06e5dfda93af747386590`, with the existing phone-proven patches.
The only additional vendor change suppresses diagnostic capture when compiled
with `SATCHEL_SIGNER_QUIET`. The MIT and OpenSSL Apache 2.0 notices ship in
`PrepareAssets`. No new dependency version was introduced.

The app links the existing OpenSSL 3.6.3 iOS archive at
`~/SatchelBuild/spike/openssl-ios`. Set `SATCHEL_OPENSSL_IOS` to use another
3.6.3 installation. If needed, `build-openssl.zsh` builds that version with two
jobs under `~/SatchelBuild/signer/openssl-ios`; it is not run per game. The Mac
checks use installed Homebrew OpenSSL 3.6.3. New builds wait for other xcodebuild
processes and use nice 10, at most two compiler jobs.

Run from the worktree:

```zsh
/bin/zsh NativePrepare/Signer/build-mac.zsh
PREPARE_OUT="$HOME/SatchelBuild/signer" PREPARE_SIGNER=1 \
  PREPARE_MAIN=NativePrepare/Signer/Tests.swift \
  PREPARE_EXECUTABLE="$HOME/SatchelBuild/signer/signer-tests" \
  /bin/zsh NativePrepare/build-core.zsh
SIGNER_FIXTURE_OUT="$HOME/SatchelBuild/signer/format-new" \
  /bin/zsh NativePrepare/Signer/check-mac.zsh
SATCHEL_PREPARE_OUT="$HOME/SatchelBuild/signer" \
  SATCHEL_HOST_OUT="$HOME/SatchelBuild/menubar" \
  python3 NativePrepare/build-dev.py --unsigned
```

Use a fresh fixture directory for every acceptance run. The script creates only
its own disposable identities and signs a copy of the existing pipeline harness
output `~/SatchelBuild/prepare/game-unsigned.dylib`. It does not read the user's
real PKCS#12, console keys or certificate. Override `SIGNER_UNSIGNED_INPUT` for an
unsigned synthetic container from the existing container harness. Tests cover
wrong passphrase/team, malformed identity, metadata, fresh inodes, an actively
shared writable mapping, input preservation, corrupt/truncated signatures,
wrong signer, expiry and cancellation. The final container must pass both Apple
codesign strict verification and the spike's independent structure checker.

The unsigned app build does not use a real signing identity and cannot be
installed until the orchestrator signs it. Its signature/provisioning checks are
explicitly skipped, not reported as passed. Running the build driver without
`--unsigned` retains the existing signed build and verification flow for the
orchestrator. Phone Keychain behavior, UI, Prepare, loading and gameplay require
the device-owner test plan in `~/SatchelBuild/signer/report.md`.
