# Add DLC to a prepared game

The game page offers Add DLC for imported add-on content whose application ID matches
its prepared package. Included DLC is read from that package's manifest, even after
the original game and update have been removed. The operation needs the DLC import
and the stored content keys. It does not need the base, update, shader pack or signing
identity. Imports are retained.

## Publication and signatures

`NativePrepare/AddOnUpdate.swift` uses the normal package verifier before staging. It
requires a numeric `contentVersion`; legacy Mac packages without one cannot safely
check a DLC's required application version. They need one preparation with the current
app before using this path.

An APFS clone under a private `.satchel-dlc-*` scratch directory shares unchanged data
blocks without sharing writable inodes. Only incoming DLC images and the manifest are
changed. The existing authenticated content reader verifies CNMT, NCA content hashes
and RomFS integrity. Older versions never replace newer included DLC. Equal versions
must have identical prepared records. Newer versions replace only their own image and
record. Other DLC stays included.

The complete staged package must pass `PreparedPackage.verify(forceFull: true)`.
The final `renameatx_np(RENAME_SWAP)` exchanges the verified stage with the live package
atomically. Cancellation is accepted until the finalizing boundary. After publication,
cleanup or cache-write failures cannot turn success into a reported failure. A killed
process leaves either the old or new complete package at the original path. Scratch
folders have no top-level manifest, so library scans do not expose them. The next
transaction reclaims interrupted scratch work.

The Mach-O, profile, modules, base RomFS, shader files and their bindings remain unchanged.
DLC images and `manifest.json` are data outside the signed Mach-O. No executable is
rewritten or re-signed, and no signing identity is opened. The verifier's completed
stamp is carried across the atomic rename; failure to write that optimization merely
requires full verification on the next launch. Full staged verification still reads
the whole package, so this is not a constant-time operation.

## Runtime behavior

Pinned NX2Apple `bbc3b737` implements `CountAddOnContent` and `ListAddOnContent` in
`dm/host/hle_misc.c`. The first query caches numeric indices from `DM_AOC_DIR`; an empty
result is cached too. Its list-changed event is never signalled. Mounting resolves
`aoc/<index>.img` through `hle_fs.c`.

Satchel already resets `g_aoc` and `g_naoc` through the misc session reset in
`NativeHost/session/services_overlay.py`, after guest quiescence. Add DLC requires
Quit, not Pause or return-to-library, and excludes package launch/removal while it runs.
The next session discovers and mounts the new images. This path does not install DLC
into an active guest or claim live list-change notification support. Ryujinx 1.3.3's
`IAddOnContentManager` was consulted for application filtering and count/list semantics;
no reference code was copied or changed.

## Validation

`Tests/AddOnContent/run.py` compiles the production importer, metadata reader, image
writer, package verifier and updater with synthetic encrypted fixtures. `UpdateTests`
covers first and additional DLC, upgrades, older/equal versions, conflicting records,
wrong applications, required updates, corruption, disk reserve, operation exclusion,
cancellation/failure at transaction boundaries, interrupted scratch recovery, unchanged
executable bytes, unknown manifest fields, both manifest formats and missing versions.

`Tests/AddOnContent/engine.py` compiles pinned engine AOC/mount code and the actual misc
reset transform against the synthetic image. It checks cached absence, refusal to reset
before quiescence, next-session discovery, sorted pagination and image mounting.

Device validation is still required for the production UI and iOS signature loading.
Use synthetic DLC only; the currently permitted games have no real DLC fixtures.
