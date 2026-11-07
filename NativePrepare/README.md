# Native preparation

The anygame path uses the same Swift/C code on the phone and in the macOS harness.
It selects an authenticated base application and optional or bundled update, reads
NACP/NPDM metadata, rewrites startup modules and NRR-registered dynamic NROs,
writes DMROMFS1 and a constructor-free iOS dylib, then signs and binds the package.
The host replaces rtld. Imported content is never modified or removed.

Reviewed runtime settings remain overrides selected by exact module builds.
Unknown ARM64 builds use the pinned nxnative memory defaults and an experimental
format 2 recipe. The launcher reconstructs the bounded settings from that recipe;
it still checks signatures, module builds, every file hash and escaping paths.
Format 1 reviewed packages retain their existing contract. Bundled runtime profile
text is checked against the reviewed registry digest.

The Mac harness's `--generic-profile` option bypasses those overrides for a new
diagnostic package. Optional bounded memory observations derive a heap partition
or an alias-pressure pool correction using the pinned engine's generic rules.
Existing recipes without observations and reviewed runtime digests stay unchanged.
The phone does not yet collect observations or retry automatically. See
[generic profile diagnostics](../docs/generic-profiles.md).

A matching existing shader pack is optional. Without one, format 2 starts with an
explicitly empty shader inventory and enables private first-use translation. The
NVN submit thread passes owned control/code snapshots to M5's Swift, NativeAOT and
SPIRV-Cross pipeline, compiles the resulting MSL through Metal, and invalidates the
host caches before binding it. Observed fragment inputs drive vertex padding.
Sources are cached by full request, translator, device and OS identity outside the
signed package. The first use can hitch. Hull and domain shaders translate,
but the host does not draw patch primitives yet; runtime query facts beyond the
pinned defaults remain unsupported; failures appear in the report. No first-frame or gameplay claim follows from preparation
success.

NCA section storage now supports sparse maps and raw, zero-filled and LZ4
compression ranges. Sparse routing stays below virtual-address decryption;
compressed tables and payload stay above IVFC verification. The reader bounds
metadata, compressed blocks and decoded caching independently of game size.
Full archive, CNMT and filesystem checks remain mandatory. Compressed
PartitionFS, newer encryption/hash layouts and bucket trees above the documented
routing limit still fail explicitly.

NSZ and XCZ containers retain their PFS0/HFS0 checks. Native NCZSECTN decoding
supports a single zstd stream and version 2 NCZBLOCK transport, including stored
blocks. It reconstructs encrypted NCAs in private temporary files, then applies
the same NCA/CNMT verification as ordinary imports. Expansion is cancellable,
uses bounded buffers, reserves 512 MiB on the device and removes scratch files
when the content view closes. A killed process can leave an inaccessible temporary
spill until system cleanup. Space for expanded archives is required during Prepare.
Transport keys never replace the user's NCA authentication and content keys.

Registered dynamic modules are selected by their NRR SHA-256 values. Discovery
handles loose files, embedded NROs and zstd-framed NROs, including frame headers
crossing read boundaries. The pinned nxnative indexed archive reader prioritizes
current `.nro` entries. It does not waive unresolved historical registrations;
a complete fallback scan is still required for missing hashes. Dictionaries,
other archive codecs, ZBIC NSOs and system 23 compressed NRO executables remain
unsupported. These are distinct from ordinary NROs carried in zstd frames.

The native decoder reuses M5's pinned zstd 1.5.7 BSD archive under
`~/SatchelBuild/m5/archive-{macosx,iphoneos}/libShaderArchive.a`. It is an
additional link dependency, not a per-game tool. NCZ transport follows MIT NSZ
revision `148218ff89bb4b607d07730a8cf87d5e1f249ebc`; sparse/compression layouts
follow the installed MIT LibHac 0.19.0 used by NX2Apple. License notices are in
`App/Resources/Notices`. No GPL code is used.

Run the original synthetic storage, transport and NRO discovery checks with:

```zsh
ANYGAME_OUT="$HOME/SatchelBuild/anygame/round2" \
  python3 -B NativePrepare/guard.py python3 -B NativePrepare/test_compatibility.py \
  "$HOME/SatchelBuild/anygame/round2/compatibility"
```

The macOS harness accepts `-` for no update or shader pack, and `PREPARE_TITLE`
selects a hexadecimal application ID for multi-title dumps. It uses an unsigned
development signer. The phone uses the existing Keychain signer. Do not interpret
an unsigned harness result as a playable signed package.

```zsh
PREPARE_OUT="$HOME/SatchelBuild/anygame/corebuild" \
  python3 -B NativePrepare/guard.py zsh NativePrepare/build-core.zsh
PREPARE_TITLE=0100BA4026E94000 "$HOME/SatchelBuild/anygame/corebuild/prepare" \
  /private/base.xci - /private/prod.keys - /private/output-package
SATCHEL_OUT="$HOME/SatchelBuild/anygame/app-build" \
  python3 -B NativePrepare/guard.py python3 -B NativeApp/build.py
```

The report view on a prepared game's shelf context menu exports only diagnostic
JSON, not game code or shader captures. Preparation reports are also exportable
from Prepare. Boot markers survive process termination. First frame and gameplay
remain unverified unless separately observed.

Memory defaults and NRO layout follow NX2Apple/nxnative at the pinned revision.
Its MIT notice is retained in `Tools/NativeRewriter/licenses/NX2Apple-MIT.txt`
and bundled by the existing preparation build. First-use shaders link the M5
NativeAOT framework and pinned SPIRV-Cross bridge, with their MIT and Apache 2.0 notices.
These are existing sibling-worker artifacts, not newly downloaded packages.

## Optional device warm-up

Prepare ends at "Ready to play": a signed package passes the full launch verifier,
the verified original-import cleanup runs (respecting the storage setting), older
copies are replaced, and Prepare's continued-processing task completes. Prepare
does not start the shader warm-up. The warm-up is optional and long (hours for a
large game such as Zelda); a game plays without it, and new areas may stutter the
first time their shaders build.

The player starts it with **Warm up** on the game page (the game's settings show the
same panel). It asks iOS for its own continued-processing task and runs the same
verifier before reading the package's content. The game page and the home card show
the current discovery, shader or pipeline count, and say that a large game only gets
far with Satchel open, the screen on and the phone charging (in the background iOS may
stop it; it resumes when the player returns). The continued-processing task's progress
counts whole units of finished work: one per scanned file, then one per shader, then one
per pipeline variant (`unitsDone` and `unitsTotal` in `progress.json`). Each shader
counts as it finishes, not when its 16-record batch ends: in the background one batch
took over a minute on the phone, and iOS expired the task as stalled. The done count
never goes back within a run; the total grows once, when discovery has found the
shaders. Nothing moves during a single Metal compile, so no heartbeat is reported then.
A stop leaves the same words in `progress.json` and the saved job (`WarmupStopWords`).
Pause, background-task expiration,
memory pressure or starting a game cancels further warm-up work. A currently
admitted Metal compile can finish, but Play does not join the job. Warm-up failure
never marks a verified package unplayable.

**Resume** reuses durable source entries and device pipeline archives, including
after relaunching Satchel. A run the player started that iOS stopped in the
background, or that ended with the app, resumes when Satchel is active again; the
saved job records `startedByUser` for this. A saved run from a release that started
warm-up after every Prepare has no such field: it stays paused with its Resume
control and cached work, and never resumes by itself (`WarmupResume` in
`WarmupJob.swift`, checked by `Tests/WarmupResume/run.py`). Saved progress does not
authorize skipping verification or library acquisition. Shaders and vertex/fragment
pairs in the private first-use capture (what the game drew in play on this phone) are
built first, then the rest in their usual order; a partial run then covers the areas
the player has been. This only orders the work: every record and pair is still built,
and resume reuses each one's cached work as before. iOS must accept the
continued-processing request for extended background time; the foreground fallback
pauses when its limited background time expires.

Reviewed format 1 packages reuse their bound Metal libraries and published pair
tables. Their shader records are not translated again. For generated format 2
packages, `WarmupInventory` reads the bound DMROMFS1 image without extracting the
game. It uses the M5 SARC, BNSH, BAHS and dictionary-zstd readers, with a bounded Yaz0 decoder
following the pinned MIT nxnative layout. It scans known shader/effect candidates,
package shader-map origins and private first-use captures. General SARC, BLARC and
SHARCB containers are included in compressed and uncompressed form, so nested
library and UI shaders do not require a shader hint in the outer filename.
The versioned inventory invalidates earlier discovery results when these rules
change. This is a known-record
inventory, not proof that every shader in the game has been found. Unsupported
containers and new runtime query facts still depend on demand compilation.

`WarmupRunner` uses the M5 driver with a bounded 16-record queue, two active
workers in the background on battery, or four when foreground or charging.
Low Power Mode and serious thermal state retain two workers. It keeps a
64-library live cache and a bounded 12 GiB source cache shared with first-use
rendering. See [timing fields and phone trials](../NativeShaders/WarmupSpeed.md). Individual
source-cache payloads are bounded at 128 MiB, with 192 MiB encoded envelopes;
each compiled MSL source retains its 64 MiB limit. Source identity includes the
record, translator and query facts; compiled libraries
include device, OS and compiler options. Records and caches stay outside the
signed package. Persisted source entries and published archive batches survive
interruption. Resume reacquires libraries and rebuilds any unpublished batch. The
job checks its storage reserve before publishing records, sources or archives.

Known vertex/fragment pairs build four Metal 4 base variants, covering both depth
entries and alpha-to-coverage states. Their unspecialized color state follows the
pinned engine's descriptor construction. Draw-specific layouts and unsupported
variants are deferred explicitly. Reviewed packages use their exact bound
libraries; generated packages use translated libraries. Atomic archive batches
of at most 1,000 variant attempts are written to the same GPU and OS namespace
the engine reads at launch. New draw states still specialize or build on demand. Launch bulk preparation and
recorded prewarm are off by default; the existing explicit developer prebuild
switch can restore them for tests.

The Mac harness compiles this same runner, Metal code and package verifier:

```zsh
python3 -B Tests/Warmup/build.py
~/SatchelBuild/dlc/warmup/mac/shader-harness /private/prepared-package /private/warmup-runtime
```

Run it under the task resource guard, with heavy builds serialized and the 35 GiB
Mac disk reserve preserved. The harness reports metadata and timings only. Test
products, source caches and game records must stay outside this repository.

The sections below document the earlier TOTK format 1 path and its validation.


The single Satchel app prepares imported TOTK 1.4.3 content with the
existing authenticated content reader and native rewriter. It writes a thin arm64
iOS dylib directly, streams DMROMFS1, verifies and copies a Mac shader pack, and
publishes the manifest last through a staging-directory rename. No Python,
compiler or linker participates in the app's per-game path.

The preparer links the existing rewriter's pinned Capstone 5.0.7 and the
phone-proven zsign/OpenSSL 3.6.3 signer.
The build uses its existing source archive and licenses. The registry generator
compiles the reviewed JSON into immutable C tables, including SDK match rules.
The app also carries the registry, source lock and license notices for provenance.

The supported content version is 720896. Both title identity and all three module
build IDs are checked. Unknown versions, mismatched shader packs and existing
prepared destinations are refused. After signing and publishing, the app runs the full
launch verifier with its cached stamp bypassed. Only a successful verification can
remove the imported base, covered updates and the dedicated shader pack. Cancellation
before the cleanup commit keeps all originals. A cancelled verification can leave a
completed package, while cancellation during preparation removes its staging directory.
A process killed by iOS can leave a hidden `.prepare-*` staging directory; it is
not a playable package and can be removed in Files after confirming no operation
is running. Automatic recovery and resume are outside this milestone.

## Build and use

Build the full app with `nice -n 10 python3 -B NativeApp/build.py`. See
[the canonical build](../NativeApp/README.md) and [Dev migration](../NativeMigration/README.md).
The following existing content harness and shader-pack steps are for development on macOS/zsh:

```zsh
NativePrepare/build-core.zsh
python3 NativePrepare/shader_pack.py \
  "$HOME/SatchelBuild/host/package/TOTK-1.4.3" \
  "$HOME/SatchelBuild/prepare/shader-pack"
nice -n 10 python3 -B NativeApp/build.py
```

The shader pack tool refuses existing outputs. It copies shaders, reflection,
profile and shader analysis tables, with a bound manifest. It never copies the
reference dylib, module fixups/gates or RomFS. The native preparer rechecks every
copied size and SHA-256. This manifest provides consistency, not publisher
signature authentication.

Place the pack's contents at `Documents/ShaderPacks/TOTK-1.4.3` inside
`com.dell.satchel`. In the existing library, import the base XCI and update
NSP and import console keys using the existing key controls. Open Workshop,
choose **Prepare game**, select both imported files and start. iOS continued
processing keeps an accepted task running in the background. If iOS declines the
request, preparation falls back to foreground work with limited background time.
Cancellation or task expiration removes the current staging directory.

The job belongs to the app, not to the screen: `PrepareJob.swift` is the one place
that starts, cancels and reports preparation, one job at a time. Closing the Prepare
screen returns to the library while the job continues. The home screen then shows a
card with the game, the current step, the overall bar and elapsed and remaining time;
tapping it reopens the Prepare screen. When the job ends, the card offers Play (the
new package) or the failure reason with Retry until it is dismissed. The finished
state survives relaunching the app.

The Satchel progress panel lists completed, current and pending steps. The current
step shows its own fraction and byte counters; the overall bar uses expected-cost
weights calibrated against the TOTK Mac harness. Elapsed time and a rough remaining
time accompany an independently animated activity indicator. Animation does not
advance either fraction. File metadata planning uses checked file counts, since
stat calls do not consume payload bytes. Analysis and composition byte counters
refer to the current subpass, so counters can reset while fractions remain monotonic.

`Progress.swift` supplies the same snapshots to the panel, the continued task and
`Documents/prepare-progress.json`. The JSON retains `fraction` and adds `step`,
`stepFraction`, `overallFraction`, `completedBytes`, `totalBytes`, elapsed and
estimated remaining seconds, and the step list. Updates are throttled to five per
second except for transitions. The waiting state distinguishes system scheduling
from active pipeline work.

Container directories are parsed once per input and their bounded hash regions
are all checked before entries are exposed. Each selected Program NCA is still
fully SHA-256 checked against its authenticated CNMT exactly once. Sequential NCA
hash reads use 4 MiB chunks through CryptoKit. Composition retains its ExeFS and
IVFC checks. DMROMFS1 writes its final header first and hashes the exact header,
payload and index as it writes them, avoiding a second full RomFS read at binding.
The launch verifier still independently checks the published digest.

`TimingHarness.swift` uses only a disposable `PROBETEST1` identity, writes a signed
package, records step times and JSON snapshots, and rejects missing or regressing
overall progress. `ProgressTests.swift` tests weights, subpass transitions,
throttling and JSON round trips. The synthetic content suite independently hashes
the writer output to test its streamed digest.

The output is `Documents/PreparedGames/TOTK-1.4.3`. Preparation refuses to overwrite
an existing package, including the orchestrator's installed package. Move an old
package aside explicitly before preparing this title again.

## Signing boundary

`PhonePreparedGameSigner` is now wired into the dev Prepare screen. Import an
Apple Development PKCS#12 in the Signing identity section and enter its passphrase
on the phone. The identity is kept in the device-only, unlock-protected Keychain.
The signer plain-writes a fresh file after zsign releases its writable mapping,
verifies structure, page hashes and CMS, and returns `signed`. The pipeline then
binds the signed file into its manifest. No Mac step is needed for signing.

The launch verifier remains strict. `DevelopmentUnsignedSigner` is still available
for the content-only Mac harness, which explicitly selects it and produces
`needs signature`. Production call sites must explicitly provide a signer.
The older `sign_package.py` remains a separate development bridge, not the
phone workflow. See [Signer/README.md](Signer/README.md) for the storage contract,
recipe, dependency setup and Mac acceptance checks. The device-owner handoff is
`~/SatchelBuild/signer/report.md`.

## Container contract and validation

The writer reproduces the host's pinned iOS 26.0 Mach-O layout, iOS SDK 27.2
metadata and ld-27106.1 UUID algorithm. It preserves x0/W/x1/x2 ordering,
16 KiB alignment and padding, the exported marker, one RX segment, separate RW
segments, and a read-only linkedit. It emits no constructors, guest imports,
rpaths or relocations. libSystem is the only dependency. The payload is never
mapped executable during preparation.

`check_synthetic.py` compares the complete native unsigned dylib with clang/ld
for synthetic normal, front and back layouts, then checks changed-byte rejection
through the host's independent parser. `check_parity.py` checks real module
sections, all six tables, the full RomFS and whole-file parity excluding only
code-signature data and the load-command fields changed by codesign. A separate
`cmp` against a freshly linked unsigned oracle also checks every header byte.
UUIDs are computed from the new file, never imported from the reference package.

The Mac harness has a `--cancel-romfs` test mode. It cancels only after a real
RomFS copy reports nonzero progress, then requires both destination and staging
files to be absent. The actual app verifier is used on the Mac by
`check_package.py`, including its unsigned-state rejection. Live UI interaction,
iOS loading, signing on the phone and gameplay remain device-owner validation.

## Original cleanup

Settings > Storage > Remove originals after Prepare is on by default. Turning it off
keeps originals on future prepares. This setting does not clean up older packages or
imports retroactively. The source selected in Files or iCloud Drive is never deleted.

The library index keeps each entry and records its prepared title and package before
removing any files. Availability is read from disk, so an interrupted cleanup does not
claim that a surviving original was removed. Reimport restores the same entry. Artwork
stays under GameArt, including its per-title copy, and Refresh artwork preserves entries
whose originals were removed. The game page explains when a new import is needed.

Cleanup only accepts direct XCI/NSP/XCZ/NSZ children of Homebrew and the exact shader folder
used by Prepare. It refuses symlinks and changed files, keeps newer updates and unrelated
imports, and keeps shader folders with extra files. The current pipeline copies shader
files into the package; it never references the source folder at launch. Saves live
outside cleanup's permitted roots. Any cleanup problem is shown as a Ready result with
a note about the originals that were kept. No automatic retry runs after an interruption.

Synthetic Mac validation (no game inputs or credentials):

```zsh
python3 -B Tests/PrepareCleanup/run.py "$HOME/SatchelBuild/cleanup2/tests"
```

The navigation suite also has a preview-only removed-original journey. Its manifests,
icons and library records are fictional; it checks the real game page and artwork refresh,
not gameplay or actual signing.

## Add-on content

Prepare includes imported AddOnContent (CNMT 0x82) associated with the selected
application, including DLC in a cartridge. The native content reader parses both
extended-header sizes, checks the required application version, verifies the full
CNMT-bound archive hashes, and streams the verified Data/PublicData RomFS into
`aoc/<index>.img`. The pinned engine enumerates those images through its existing
AOC count/list and filesystem mount hooks. The index is the DLC title ID minus the
application ID minus 0x1000. Indices outside the engine's supported 1 through 2000
range fail explicitly. DataPatch CNMT 0x84 composition remains unsupported.

The manifest's `addOnContent` rows bind identity, required version, index, image
path, size and SHA-256 to `files`. Launch checks the exact AOC directory membership
on every verification, including cached verification. Unbound entries, symlinks,
wrong-title rows and changed image bytes fail validation. `DM_AOC_DIR` points to
the verified package, so an old writable runtime folder cannot claim ownership.
Packages without DLC continue to launch with an empty AOC list.

DLC imports remain in the library, attached to their base game, without their own
shelf tiles. Their title IDs serve as names because AddOnContent has no Control
data. The game page lists imported DLC and its size; Play lists the DLC included
in that prepared package. Newly imported DLC requires another Prepare. Standalone
DLC originals are retained because the existing cleanup index describes only
base games and updates. A cartridge's DLC is included before cleanup can remove
that cartridge's verified import.

Mac tests use synthetic content and the production Swift reader, shelf grouping,
packager and launch verifier. `Tests/AddOnContent/engine.py` exercises the pinned
engine's actual AOC discovery, pagination, mount routing and DMROMFS1 reader with
that synthetic image. Its mount registration is a harness stand-in, not a game
session. Run under the task's resource watchdog, with outputs outside the repo:

```zsh
python3 -B Tests/AddOnContent/run.py "$HOME/SatchelBuild/dlc/tests"
python3 -B Tests/AddOnContent/engine.py "$HOME/SatchelBuild/dlc/engine-tests" \
  "$HOME/SatchelBuild/dlc/tests/synthetic/engine-aoc-path.txt"
```
