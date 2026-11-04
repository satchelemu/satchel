# Split dump import

Satchel accepts a folder containing numbered `00`, `01`, ... parts, or matching
`Game.xc0`, `Game.xc1`, ... and `Game.ns0`, `Game.ns1`, ... parts. Users can select
all parts directly instead. A selection must begin at zero and contain every
number exactly once. Separate filename families and parent folders form separate
queued imports. A folder must contain one dump; hidden provider metadata is ignored.

`SwitchDumpSelection` groups selections before the library queue runs. It retains
security-scoped access to every selected URL while coordinated reads resolve the
folder and acquire all part read intents. `SwitchPackageStream` presents ordered
parts as one bounded random-access stream, with one open part handle at a time.
The existing NSP directory and XCI hash checks use that same stream.

Bytes copy directly from the parts to the library's atomic import file. There is
no preliminary joined dump. The final library file has the same content digest,
format and identity as an unsplit import, so metadata, preparation, cleanup and
reimport deduplication use the existing paths. Progress counts bytes across all
parts. Cancellation, truncated parts and changed source metadata prevent publication.
The final copy receives the same container verification as any other import.
These checks do not claim that every encrypted content byte is authenticated;
content authentication remains in the existing preparation pipeline.

Limits remain 128 GiB per import and bounded read chunks. Split selections are
limited to 4096 regular, nonempty parts. Symlink parts, gaps, duplicate numeric
indices and folders mixing dump families fail before copying.

## Checks

Run under `NativePrepare/guard.py`, with task-owned output paths and two jobs:

```sh
xcrun swiftc -swift-version 6 -j 2 -num-threads 2 App/SwitchPackage.swift App/SwitchPackageImport.swift NativePrepare/SplitDumpTests.swift -o "$SATCHEL_OUT/split-tests"
"$SATCHEL_OUT/split-tests" "$SATCHEL_OUT/synthetic"
python3 -B NativePrepare/check_split_library.py
```

The Swift harness accepts `real SOURCE SPLIT_FOLDER DESTINATION import` to compare
every byte against a read-only source, verify both containers, then import exactly
one local file. Use `stream` as the last argument when only stream parity is being
measured. Fixtures and imported copies must stay outside the repository. The
library harness uses synthetic dumps and an isolated Documents location. Its
preparation-cleanup model is a synthetic placeholder; the import queue and index
code are taken verbatim from the app.

Phone validation remains necessary for the iCloud provider and mixed file/folder
picker. Select a numeric folder, reimport its reversed parts, then select an
extension-based split with another game. Confirm one shelf entry per dump, matching
artwork, a single queued import per dump, progress across parts, cancellation with
no new entry, and normal Prepare on a complete base plus update.
