# Prepare shader warm-up

`SatchelShaderWarmup` is the bounded background driver for the any-game Prepare
worker. The shipping source list includes `ShaderWarmup.swift`. No new library or
phone action is required. The Mac tests and iOS link pass. The app integration is
in `NativePrepare/WarmupJob.swift` and `WarmupRunner.swift`; see
[Prepare](../NativePrepare/README.md#optional-device-warm-up).

## API and lifetime

Create one `SatchelShaderStore` and `SatchelShaderCompiler` for the active game and
Metal device. Share those exact objects with the renderer. On the existing Prepare
background task queue, call:

```swift
let warmup = try SatchelShaderWarmup(
    store: sourceStore, compiler: runtimeCompiler,
    maximumConcurrentRecords: 2,
    workingMemoryBudgetBytes: 512 * 1024 * 1024)
let result = try warmup.run(
    totalRecords: knownUniqueRequestCount,
    next: { try inventory.nextAuthenticatedShaderRequest() },
    cancelled: { cancellation.isCancelled },
    progress: { state, records in
        // Dispatch UI changes to the main actor. Persist this small progress
        // summary in the app's Prepare job journal after each completed batch.
        reportPreparedShaders(state.completedRecords, state.totalRecords)
        try saveProgress(state, records)
    })
```

The inventory and UI functions above are app adapter examples, not additional
library symbols. `next` returns `SatchelShaderRequest?` and is called serially.
Keep it lazy: authenticate/read one record on demand from discovered archive
ranges or previously captured runtime records. Do not materialize every record's
bytes in an array. Discovery callbacks can spool a bounded metadata index in app
storage; they must not queue an unbounded collection of record data. Supply the
actual resource facts and effective vertex/pixel pairing used by the renderer so
warm-up and first-use have the same request identity.

Only one or two records run at once, in bounded batches with an autorelease pool
per record. The producer cannot run ahead of that batch. Progress runs serially on
the calling thread after each batch, including successful records in a batch where
another record failed. An error throws after that progress update. The caller must
mark the inventory incomplete on errors and cancellation. A non-nil total is an
exact count; an early end or excess inventory is rejected. Pass nil for unknown
coverage and show a count rather than a fabricated percentage.

The memory budget selects one or two workers using an adjustable estimate of
256 MiB per worker. It is an admission estimate, not a guarantee of process RSS.
The app owns memory-pressure handling: cancel, reduce the worker count and resume
when appropriate. Keep the compiler's live-library bound suited to the device.
The default is 128 libraries. The timing experiment used 512 to measure complete
in-process reuse for its small sample, not a recommended whole-game allocation.

Cancellation is checked before records, between native translation calls and
between variants. It cannot interrupt an in-progress Apple runtime compile.
The cancellation callback can run on worker threads and must be thread-safe.
Return from `run` only follows completion or cancellation of its admitted work;
there are no detached jobs left running after a failed Prepare step.

## Durable cache and launch ordering

`SatchelShaderStore.prepareMeasured` returns the shader, `sourceCacheHit`, lookup,
translation and persistence seconds. The ordinary `prepare` API remains available.
`SatchelShaderWarmupRecord` adds per-variant runtime compile seconds and live-library
hit count. Progress totals are sums of worker times, so they can exceed wall time.
Source data is persisted before compilation. A cancelled run can enumerate the
same inventory again and recover verified source-cache entries. A saved completed
count is diagnostic progress, not permission to skip library acquisition after a
process restart.

Partitioning includes translator implementation, record and query facts; the
source store also partitions by device identity. Runtime library identity includes
MSL, device, OS build and compile options. Changing geometry support bumps the
implementation identity to `satchel-native-v2-geometry-e65a4ebd3322-f11ba9f0`.
Reserve enough private cache space for the chosen preparation scope. Full Zelda
MSL is about 4.8 GiB before cache encoding overhead; a 2 GiB default source cache
will evict entries during a complete warm-up. Configure a larger bounded budget
when space allows, preserving the app's reserve and eviction policy.

Warm archived shaders during Prepare, then warm the known startup working set
last so it remains in the bounded live-library cache at launch. Preserve the same
compiler object through that transition. Persist actual observed render/compute
pipeline descriptors through `SatchelPipelineArchiveCache` or the engine's existing
archive path and warm those descriptors as a separate renderer-owned step. Library
compilation alone does not create all render pipelines, whose attachments, vertex
formats and function constants come from actual bound state. Do not report PSOs as
ready based only on a successful library warm-up.

For titles without a shader archive, only previously captured records can be
warmed in advance. The runtime callback discovers new records on first use. The
app must retain those requests and effective query facts for the next Prepare.
Unknown first-use paths can still stall. No synthetic shader inventory can remove
that limitation.

## Measured Mac sample

The same Swift, NativeAOT and in-process SPIRV-Cross path as iOS prepared 192 Zelda
1.4.3 records, 64 per stage, evenly selected through the sorted vertex, fragment and
compute inventories. It generated 375 sources. Two workers completed an empty
application-cache pass in 33.331 s. Translation median was 22.960 ms per record,
p95 115.672 ms and maximum 164.118 ms. Library acquisition median was 100.424 ms
per source, p95 280.797 ms and maximum 374.380 ms. Eight acquisitions reused
identical source in the live cache; 367 called Apple's runtime compiler.

A live-library repeat took 0.420 s with 192 source hits and 375 live-library hits.
A separate process took 0.449 s with 192 source hits and 367 runtime library
acquisitions totaling 23.347 ms. The OS Metal cache was already warm and was never
cleared. These results do not establish phone latency or cache persistence across
an OS upgrade, eviction or reboot. They show why Prepare warm-up can move expensive
compilation out of a launch on this Mac. The earlier shader worker's 436 phone
compilations at median 0.33 s remain separate historical evidence.

Evidence is in `~/SatchelBuild/m5/warmup-result.json` and incremental
`warmup-timings.jsonl`. No shader source or game data is logged. Temporary records
and the experimental source cache are removed after the check.
