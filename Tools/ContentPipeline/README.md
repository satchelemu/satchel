# Native content preparation

Implemented and validated on the Mac for the user's TOTK 1.4.3 base XCI and
update NSP. No device run or gameplay claim. All application code is Swift/C;
no .NET, Python, downloaded reference code or new runtime dependency ships.

Use `SwitchContentPipeline.select` to inspect CNMT title IDs and versions without
extracting the game. Use `withContent` for authenticated composition. Hold
security-scoped, coordinated access to stable imports for the entire operation
and run on a background queue. The synchronous storage is confined to that
operation and that queue. Do not retain `romFS` past the callback.

```swift
try SwitchContentPipeline.withContent(
    base: importedBase, update: importedUpdate, keys: userKeys,
    applicationID: selectedTitleID, cancelled: isCancelled
) { content in
    for module in content.program.modules {
        let decoded = try SwitchDecodedModule.decode(module)
        // Supply decoded.segments' bytes and offsets, bssSize and buildID to
        // nr_request. Keep the owned Data buffers alive during nr_rewrite.
        try rewriteModule(decoded)
    }
    try SwitchRomFSImage.write(content.romFS, to: preparedRomFS,
                              cancelled: isCancelled, progress: reportProgress)
}
```

`rewriteModule` and `reportProgress` above are caller-provided adapters. This
change supplies callable preparation APIs; wiring the app's preparation UI and
host coordinator is a separate integration step. `App/Bridge.h` already exposes
`NSOLoader.h` and `SwitchCrypto.h`, and `project.yml` includes the App directory.
The legacy single-package reader remains available for its existing callers.

## Contracts and integrity

- Selection uses authenticated Application/Patch CNMTs, both application and
  patch IDs, a strictly newer version, matching content ID/size/type and Program
  id offset. Package filenames do not select a game. Missing delta fragments
  are tolerated only for Patch type-6 transport content.
- Common tickets are matched by the complete rights ID. The user's `titlekek_XX`
  unwraps the ticket's encrypted titlekey. `SwitchKeys.parse` now retains those
  keys. Existing Keychain imports made by the older parser need reimporting.
  No key values, rights IDs, content bytes or paths enter diagnostics.
- Before composition, the entire selected base and update Program NCAs are
  streamed through SHA-256 and compared with CNMT. Meta NCAs are also hashed.
  Section headers, ExeFS hash tables and every returned ExeFS/IVFC block are
  checked. The metadata-hash descriptor is range-checked and SHA-256 checked;
  the full NCA hash binds the BKTR routing tables, rather than relying on a
  second, separate lazy metadata IVFC verifier. Publisher RSA signatures and
  CNMT's trailing development digest are not claimed verified.
- BKTR parses the root and every entry-set header, validates sorted contiguous
  virtual coverage and source extents, and splits reads at indirect and CTR-EX
  boundaries. Counter generation, absolute NCA block offsets and explicit
  unencrypted CTR-EX ranges are supported. The same composition works for
  ExeFS and RomFS; TOTK uses a replacement ExeFS and patched RomFS.
- The image writer validates full-tree reachability, cycles, parentage and raw
  UTF-8 name uniqueness, then uses NX2Apple's sorted breadth-first ordering.
  Empty directories/files, case collisions and non-ASCII names are preserved.
  File bytes stream in 1 MiB chunks. Index writes use at most 1 MiB buffering.
  Enumeration metadata is capped at one million entries and 64 MiB of names;
  each raw RomFS table is capped at 64 MiB. The existing IVFC cache is 8 MiB.
  This is bounded metadata memory, not constant memory independent of file count.
- The destination's parent must exist. A unique mode-0600 sibling temporary
  file receives the header, data and index in that order, hashing them as written. After synchronization,
  a same-directory rename atomically publishes it. Cancellation, read integrity
  errors and I/O errors preserve the previous image and remove that invocation's
  temporary file. Progress reports enumeration, copied bytes/files and publishing.
- `SwitchDecodedModule` reuses `satchel_nso_inspect` and `satchel_nso_decode`, returns three
  owned data segments with guest-relative offsets, BSS size and the build ID.
  It creates no executable pages. NSO raw/LZ4 and hash verification cover all
  four selected TOTK modules. Decode modules one at a time to bound peak memory.

## Explicit unsupported cases

Personalized RSA tickets; NCA0/1/2; sparse or rights-managed AES-XTS sections;
NCZ sections recorded as XTS or skip-layer; compacted or non-Nx CNMT layouts and
DataPatch (DLC update) metadata; empty generic BKTR tables or more than two million
BKTR records. NSO limits remain 128 MiB stored and 512 MiB decoded including BSS.
Prepare discovers loose, embedded and zstd-framed NROs; other archive codecs can
still prevent discovery. Native sparse storage, raw/zero/LZ4 compressed PartitionFS
and RomFS, key-area AES-XTS sections, skip-layer AES-CTR and AES-CTR-EX, SHA3-256
hash layers (iOS 26 or later) and IVFC trees of 3 to 7 layers are supported. The
full variant table is in [the importer validation guide](../../docs/importer-validation.md#format-coverage).
Generic add-on preparation is documented in `NativePrepare/README.md`.

The second bucket-tree index level has a 64 MiB serialized-table bound. The native
content tests generate large routing tables, compressed containers and synthetic
keys. See [the importer validation guide](../../docs/importer-validation.md) for
format provenance, remaining gaps, progress coverage and discarded-output timing.

## Validation commands

From the worktree, all compilation is low-priority and Swift uses two threads.
Build/test outputs default to `~/SatchelBuild/content`.

```sh
scripts/test_content_pipeline.sh
CONTENT_BUILD="$HOME/SatchelBuild/content/sanitized" CONTENT_SANITIZE=1 scripts/test_content_pipeline.sh
scripts/build_content_parity.sh
```

The synthetic suite reuses the existing ExeFS/RomFS fixture constructors and
runs their original assertions. Its new packages contain generated bytes only.
The Mac parity executable takes `BASE UPDATE KEYS REFERENCE_DIRECTORY OUTPUT_DIRECTORY`.
It compares composed NSO hashes, writes decoded segments privately, streams
RomFS and compares the entire image hash. The output directory must exist.
`--select BASE UPDATE KEYS TITLE_ID` performs only selection, which can check
that an unrelated update is refused without exporting content.

Independent NX2Apple oracles, using its existing Python environment:

```sh
nice -n 15 /tmp/satchel-switch-integration/venv/bin/python3 Tools/ContentPipeline/compare_reference.py \
  --tools "$HOME/SatchelDeps/NX2Apple/engine/dm/tools" \
  --synthetic "$HOME/SatchelBuild/content/sanitized/synthetic" \
  --reference "$HOME/SatchelBuild/zelda/work/extract/v720896" \
  --native "$HOME/SatchelBuild/content/parity"
nice -n 15 xcrun clang -std=c11 -O1 -Wall -Wextra -Werror \
  -I "$HOME/SatchelDeps/NX2Apple/engine/dm/host" Tools/ContentPipeline/HostImageTest.c \
  -o "$HOME/SatchelBuild/content/tests/host-image-test"
nice -n 15 "$HOME/SatchelBuild/content/tests/host-image-test" "$HOME/SatchelBuild/content/parity/RomFS.img"
```

The private report at `~/SatchelBuild/content/report.md` records exact inputs,
results, limits, commands and the next integration blocker.

## Format provenance

Independent native implementations reuse Satchel's existing storage/crypto
and consult the installed MIT LibHac 0.19.0 assembly (commit
`cd4004af194a51712812c680f0a028ba9152c0f8`): `Nca`, `Ticket`, `BucketTree`,
`IndirectStorage`, `Aes128CtrExStorage` and `AesCtrCounterExtendedStorage`.
The original source repository was unavailable, so ILSpy 11.2.0.10050 was
installed only under the private build directory to inspect that assembly.
No decompiled code is included here. Modern metadata fields were checked
against [Switchbrew NCA](https://switchbrew.org/wiki/NCA) and
[ContentMetaAttributes](https://switchbrew.org/wiki/NCM_services#ContentMetaAttributes).
The DMROMFS1 contract was checked against the local NX2Apple
`engine/dm/tools/romfs_image.py` and `engine/dm/host/romfs_image.h`.
