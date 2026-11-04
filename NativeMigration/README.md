# Dev to Satchel migration

The bridge and main app request `group.com.dell.satchel.migration` and the Keychain group
`<team>.com.dell.satchel.migration`. Each keeps its original private Keychain group
first and explicitly selects it for normal console-key and identity operations. The bundle
IDs stay unchanged. The existing normal Satchel library is merged, never reset.

## Why APFS snapshots

A direct rename removes the source path before destination verification. Hard links share
mutable save-file inodes. Instead, migration uses `clonefile` with no full-copy fallback,
checks SHA-256 and sizes, then publishes with exclusive same-volume `renamex_np` moves. Neither operation can
fall back to a cross-volume copy. Clones share
APFS data blocks but have independent inodes and writes. This meets the storage constraint
without a second 40+ GB allocation and retains rollback data until the user verifies it.
Cloning can still require metadata space and later writes can allocate new blocks. An
unsupported filesystem, sandbox refusal, full disk or locked protected data stops migration.
The user can retry without discarding originals. There is no streaming-copy fallback.

The installed iPhoneOS27.2 SDK declares `clonefile` available since iOS 10. Its man page
specifies independent copy-on-write data and atomic clone creation. Only regular files are
cloned; directories are traversed explicitly. Symbolic links, special files and unsafe
manifest paths are rejected. The Mac tests do not establish iOS cross-container sandbox
acceptance. That remains a required physical-device check.

Apple documents [App Group sharing](https://support.apple.com/guide/security/sec1a976c067/web)
and [Keychain access groups](https://developer.apple.com/documentation/security/sharing-access-to-keychain-items-among-a-collection-of-apps).
Both installed profiles must grant the App Group; matching entitlement text alone is not enough.

## Durable protocol

1. The temporary Dev update opens only migration, before creating a library or game session.
   Both apps serialize migration with a shared `flock`, released automatically on process death.
2. Dev exports only the two known Keychain items, read back and compared in the shared Keychain.
   Originals remain private and intact. No credential enters files, defaults, manifests or logs.
   Player defaults (`SatchelPlayerSettings.*`, `InGameSettings.*`, `SatchelLiveControls`) go into
   a binary property list. Automation flags and arbitrary preferences are not transferred.
3. Dev inventories all Documents, clones files into the shared snapshot and verifies sizes,
   paths and SHA-256. It rechecks the source before publishing `manifest.json`. A restart
   reuses existing clones and verifies them. The published snapshot and metadata are immutable.
4. A cold launch of Satchel detects the snapshot and gates normal UI. A persisted local
   `started` marker keeps that gate across interruption, even if a later build loses group access.
   The main app builds a merged staging tree using COW, verifies it against expected source
   bindings and writes a durable publication plan before changing any active Documents paths.
5. For each top-level root, Satchel renames the old root into `Original`, then moves the staged
   root into Documents. The plan and presence of pending/backup/destination roots resolve crashes
   before or after either rename. No original is unlinked. All published files are rehashed.
6. Dev credentials become active in Satchel's private Keychain. If a different private item
   exists, it is preserved under the service suffix `.before-migration` and read back first.
   Shared and Dev originals remain. Player settings are applied only after saving and syncing
   the original settings backup. Required shared credential absence blocks completion.
7. A local completion receipt is written only after document, credential and settings checks.
   A shared adoption acknowledgement follows. Opening the library is then allowed. Future
   launches do not reimport the immutable snapshot or overwrite newly played saves.

Migration remains in the foreground with auto-lock disabled. If backgrounded or killed,
reopen and continue with the migration button. Start migration also resumes durable work
after a process restart; Resume migration appears after an in-app error. There is no deadline-driven deletion or automatic cleanup.
Keep enough free space for filesystem metadata and small journals. No full payload duplicate
is required. Full hashing reads the payload several times and can take minutes.

## Merge and rollback policy

* Homebrew indexes are unioned by their content-derived ID. Existing metadata wins for a
  duplicate ID. Matching filenames must contain identical bytes or migration stops. Every
  import remains available under its original content ID.
* Prepared game folder-name collisions retain both, naming the Dev entry `<name>-from-dev-N`.
  Package contents and their internal manifest bindings are unchanged.
* Shader packs merge by name. A same-name Dev pack becomes active, preserving the previous
  main pack in the original-root backup. Unrelated main packs remain active.
* Native runtime folders merge by title. A migrated title uses the entire Dev folder, including
  its byte-identical save tree. No stale main save slot is mixed into it. Other main titles
  remain. The complete original main runtime root is retained.
* Other Documents roots use the Dev version on a collision, preserving the main original.
  This includes unknown existing user files, not just today's known directories.

Rollback locations are inside Satchel's `Library/Application Support/SatchelMigration-v1/Original`
and `OriginalPlayerSettings.plist`; Dev's original container and the immutable shared snapshot
also remain. These are not deletable cache directories. If manual recovery is needed, keep
both apps installed and use a reviewed recovery build. Do not blindly move a partial stage
or restore an entire root over newly played saves. A general save import/restore UI is outside
this task. There is deliberately no destructive rollback or cleanup button in the first bridge.

## Orchestrator and one-time user steps

1. Register the App Group for your team and enable it for both existing app IDs in the
   Apple developer account. Ensure both profiles permit the shared Keychain group and both
   memory entitlements. Xcode automatic provisioning may perform this during the build.
   Account authentication, if needed, is performed privately by the user.
2. Build both artifacts using the two documented commands. Require the signed verifier to
   pass for both. The worker does not perform any phone actions.
3. Finish any import or Prepare job, save the game and completely close both apps. Then
   install over the existing apps, with the same team and bundle IDs. Never uninstall either
   app first. Open the updated Satchel Dev, tap Start migration and keep it open and unlocked
   until its snapshot verifies. No PKCS#12 export,
   password entry, game transfer or Mac data operation is needed.
4. Completely close Satchel if it was already running, then open it. Confirm the stated
   conflict policy and tap Start migration. On interruption, reopen and resume. Wait for
   Verification complete, then Open library.
5. Check both libraries, all three prepared packages, keys, the signing identity metadata,
   player settings and each migrated title's save. Boot a prepared game, load a known save,
   play, save, close and reopen to verify the new save. A successful build is not this test.
6. Only after these checks, the user can remove Satchel Dev. This removes its original private
   file paths, after destination verification. Satchel still holds its own COW files and the
   shared group snapshot. Keychain items have been copied to Satchel's private group.
   Retain the shared and original-main rollback snapshots through an agreed backup window.
   Deleting them later requires a separately authorized cleanup; this implementation never
   reclaims them automatically. COW snapshots retain old blocks as saves subsequently change.
   Remove signing identity clears the active private Keychain slot only. Shared migration
   credentials and any `.before-migration` identity backup remain for recovery. The confirmation
   says so explicitly. Removing the Dev app is not a credential-erasure operation performed
   by this migration; credential cleanup is also a separately authorized step.

After retirement, distribute only Satchel.app under the Satchel name. Keep the bridge source
for users who have not migrated; no feature development should target a separate Dev app.

## Synthetic tests

```zsh
/bin/zsh NativeMigration/check.zsh
```

This compiles the exact production migration store for macOS, then exercises interrupted
snapshot creation and publication, lock exclusion, library union, package collisions,
whole-save replacement and preserved originals, clone write isolation, corruption rejection,
unsafe paths, and a full source/clone hash comparison of a sparse 48 GiB file. Hashing drains an
autorelease pool per chunk, as the existing Prepare pipeline does; enumeration and cloning
also drain temporary Foundation objects per entry. The harness records peak memory with `time -l`. All fixtures and logs stay under the task output.
The sparse fixture proves 64-bit sizes and allocation behavior, not real-game payload parity.
Phone Keychain ACLs, sandbox cloning, actual user data and gameplay require the device owner.
