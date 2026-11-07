# Optional warm-up scheduling and measurement

The format 2 warm-up uses the same NativeAOT translator, SPIRV-Cross, MSL
transforms and Metal compile options as first use. Format 1 still loads its
reviewed precompiled libraries. No translation identity or cache version changes.

Phase 1 admits at most 16 records, with two active workers in the background on
battery, or four when foreground or charging. Low Power Mode and serious or
critical thermal state keep two workers. Foreground work uses user-initiated QoS;
background work uses utility QoS. The policy is sampled between drained batches.
The compiler returns to its normal two-lane limit before the runner returns.
Memory warnings and Play still cancel and drain admitted work.

Source-cache hits continue to bypass translation. Immutable regexes are shared in
a bounded cache; source replacements join once instead of repeatedly copying the
whole source. Fragment input masks travel with completed records, avoiding an
extra cache decode and file read. A warm-up remembers at most 65,536 successfully
compiled source hashes, so duplicates need not compile again after live-library
eviction. A new warm-up driver reacquires libraries, and phase 2 always obtains
the executable libraries it needs. Persisted progress never proves a library is
available.

The installed Metal SDK's `MTLDevice` source compilation API and
`MTL4LibraryDescriptor.source` both take a single source string. The generated
shaders share names such as `main0` and resource types. Concatenating them would
require changing sources, entry names and the library contract. This change
therefore keeps separate libraries. The specialization mutex in
`NativeHost/graphics/ShaderBuildQueue.inc` remains intact: it protects renderer
pipeline specialization and is not used by phase 1 library compilation.

## Measurement tools

Build with `NativeApp/build.py --warmup-mac-harness --unsigned` for fixed-request
measurements and equivalence replay. Build with `--warmup-mac-tests --unsigned`
for the actual `WarmupRunner` client, selecting `SATCHEL_WARMUP_MAIN` as
`Tests/Warmup/Checks.swift` or `Tests/Warmup/SpeedRunner.swift`.
Set `SATCHEL_OUT` to a private task directory, `SATCHEL_BUILD_ROOT` and
`SATCHEL_DEPS_ROOT` to the installed roots, and use `NativePrepare/guard.py` at
nice 10. Build parallelism is two. `SATCHEL_WARMUP_SOURCE` can select a private
baseline source snapshot. The request corpus and all generated cache payloads
stay outside the checkout. No keys or package copies are needed.

`WarmupSpeedHarness` takes a captured request-corpus JSON, private cache directory,
output JSON and label. It records individual lookup, translation, persistence
and library times, plus hashes of complete canonical translation objects and the
library set keyed by exact source hash and variant. `request-equivalence` compares
all canonical translation bytes against retained expected output.
`SpeedRunner` takes a corpus, original package path, private runtime root and
output JSON, with an optional final argument naming a captured pair table. It
replays unique captured records and up to 32 known pairs through
both real warm-up phases. It does not copy RomFS or executable content.

`DM_SHADER_EVENTS` optionally names a private TSV file. Events contain monotonic
start time, duration, thread ID and event kind only. NativeAOT, SPIRV-Cross, MSL
transforms, source load/save, store lock wait, library lane wait, library creation,
record reads and archive serialization are measured separately. Nested events
must not be added together. Worker-time totals can exceed elapsed time.
Apple's compiler cache is not cleared. Set `SATCHEL_WARMUP_PRIME=1` and
`SATCHEL_WARMUP_PASSES=3` for three fresh application-cache passes after priming
inside each process. Both binaries run on a utility caller queue with two lanes.
Live libraries are trimmed before each pass. Report cold observations separately:
switching executable identities can reintroduce substantial Metal compilation work.

Executable `MTLLibrary` bytes cannot be exported through this runtime API. The
comparison checks byte-identical translated objects and the exact library set
(source identities, variants and function names), with unchanged compile options.
It does not claim serialized Metal binary or pipeline archive byte equality.

## Mac results and phone hypothesis (2026-11-04)

Three passes used empty application source caches, two worker lanes, the same
utility caller QoS and Metal primed inside each executable. These are captured
request samples, not complete game inventories. Median phase 1 elapsed times:

| Sample | Requests / library variants | Main | Candidate | Throughput change |
|---|---:|---:|---:|---:|
| Castlevania | 63 / 140 | 0.546 s | 0.303 s | 1.80x |
| Odyssey | 225 / 534 | 6.386 s | 4.186 s | 1.53x |

All passes produced the same canonical translated bytes and library sets. The
larger request-equivalence replay passed 760 requests and 1,780 variants with zero
missing or different outputs. A four-worker cold synthetic replay also matched
serial output. The actual production runner replay covered 55 Castlevania and
220 Odyssey unique records, both phases and fresh-process reuse. Castlevania
completed 32 pipelines; Odyssey completed 16 with four deferred, matching main.

Initial, uncontrolled baseline samples took 14.36 s for Castlevania and 112.89 s
for Odyssey. Summed library-compile worker time was 13.83 s and 130.72 s;
translation was 4.14 s and 37.19 s. These overlap across workers. MSL transforms
accounted for 1.75 s and 14.70 s within translation. Source locking and file reads
were small in these samples; renderer specialization locking is outside phase 1.
In the controlled passes, MSL-transform worker medians fell from 0.278 to 0.232 s
and 2.874 to 2.234 s. Duplicate reuse reduced actual library calls from 99 to 97
and 530 to 524, preserving all requested variants. Archive writes in the runner
samples took 0.02 to 0.05 s; this does not predict full phase 2 cost.

Cold Metal compilation has not been made intrinsically faster. Uncontrolled
runner trials were slower for the candidate (6.12 versus 4.50 s for Castlevania,
59.44 versus 43.64 s for Odyssey phase 1). Executable identity and Apple compiler
cache state made those trials unsuitable for attributing a speed change. The
controlled result isolates the application work after Metal cache priming.

A testable phone hypothesis is 1.3x to 2.0x throughput while foreground or
charging at nominal thermal state, based on lower application overhead and four
available lanes instead of two. It is not a measured phone result or a confidence
interval. If sustained across the historical Odyssey inventory, that would mean
roughly 5.5 to 8.5 hours for phase 1 instead of 11 hours, excluding pipelines.
Memory pressure, compiler serialization and thermal throttling could erase the
additional-lane benefit. Background battery operation retains two lanes. Keep
the roadmap's shader estimate at 70% until matched phone evidence is available.

## Orchestrator phone steps

Only the orchestrator performs these steps. The worker never connects to the
phone. Use the normal signed Release build workflow, preserving the library,
imports and saves. An unsigned validation build cannot be installed directly.

1. Confirm Castlevania and Odyssey have format 2 phone-prepared packages. Quit any
   running game. Record OS build, battery/charging state, Low Power Mode, thermal
   state and the build commit. Keep the display awake and app foreground for the
   matched foreground measurements.
2. Launch with `-SatchelWarmupTrial castlevania-cold-1`. Open Castlevania's game
   page and start Warm up. A new trial name creates fresh private warm-up,
   translation and pipeline caches under the title's `WarmupTrials` directory.
   Existing gameplay caches and saves are untouched. First-use capture remains a
   read-only input from the normal runtime directory, so the trial uses the same
   known shader inventory without copying the normal source-cache directory. Let both phases finish and
   record failures/deferred work. Repeat with another new name for a second
   application-cache-cold trial. Apple compiler caches remain uncontrolled.
3. Launch with `-SatchelWarmupTrial odyssey-first1024-1
   -SatchelWarmupRecords 1024`. Start Odyssey warm-up. It pauses at the first
   published phase 1 progress at or above 1,024 attempted records. Records already
   running and progress throttling can overshoot the threshold, so record the actual count.
   Remove the limit and reuse the trial name to test resume: completed sources
   must produce source hits, with no translation work for those hits.
4. Repeat Odyssey with new trial names while charging in the background, and on
   battery in the background. Check `phaseOne.lanes` becomes four when charging
   and two otherwise. Enable Low Power Mode to check the conservative policy.
   Allow an in-flight batch to drain before expecting the new count. Memory
   warnings or background expiration can pause work; record that separately.
5. Pause, resume, request Play during warm-up, Quit and relaunch. Confirm the
   package remains playable and resume reuses source entries. Remove all trial
   launch arguments for ordinary use. Trial work intentionally does not warm the
   normal gameplay cache, so it is measurement evidence only.
6. In Xcode's Devices and Simulators window, use Download Container and inspect
   only the selected title's progress files. Do not print credential files or
   copy game/shader payloads into the repository or logs. Within the downloaded
   container, read:
   `AppData/Documents/NativeRuntime/<titleID>/WarmupTrials/<trial>/warmup/<manifestSHA256>/progress.json`.
   Castlevania's title ID is `0100BA4026E94000`; Odyssey's is `0100000000010000`.
   The directory component is the SHA-256 of the package's exact `manifest.json`.
   Without a trial, omit `WarmupTrials/<trial>`.

For a phase 1 snapshot, `completed` counts attempted records, `shaders` counts
successes, `failed` counts errors, `sourceHits` counts translation-cache reuse,
and `libraries` counts variant requests, including duplicates. Use
`phaseOne.seconds` for elapsed phase 1 time and divide successful `shaders` by it
for throughput. `phaseOne.translationSeconds`, `lookupSeconds`,
`persistenceSeconds` and `compilationSeconds` are summed worker times.
`reusedLibraries` counts live hits plus already-warmed identical source.
Do not divide worker-time totals to infer elapsed throughput.

For two snapshots in the same run and phase, use differences in counts and
`phaseOne.seconds`. The top-level `seconds` also includes discovery and other
phases. A phase transition resets `completed` and `total`; the `phaseOne` summary
remains. `state` distinguishes running, paused, partial, failed and complete.
Compare partial samples with the same ordered records and actual counts, not an
arbitrary later interval. The historical 0.64 shaders/second Odyssey result implies about
10.99 hours for 25,316 shaders in phase 1 alone. A measured improvement must be
reported separately from phase 2 and from the unmeasured remainder of the inventory.

Read only progress fields with this command, supplying the downloaded
`progress.json` path as the final argument:

```sh
python3 - /path/to/progress.json <<'PYPROGRESS'
import json, sys
p = json.load(open(sys.argv[1]))
t = p.get('phaseOne') or {}
print({k: p.get(k) for k in ('state', 'phase', 'completed', 'total', 'shaders',
                            'failed', 'deferred', 'sourceHits')})
print(t)
if t.get('seconds', 0) > 0:
    print('phase1_shaders_per_second', p.get('shaders', 0) / t['seconds'])
PYPROGRESS
```
