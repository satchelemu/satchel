# Shader stall measurement

`bench_shaders.py` runs an idle opening route for each allowed game with no controller
input. It captures frames 300, 1200 and 1800, then a DM_CTL image. Castlevania reaches
the language menu, Odyssey reaches mode selection, and Zelda reaches the title menu
using a private copy of the existing `host/saves/dmios-final/Documents/save` snapshot.
The original saves and packages are read-only. Every launch starts with the same save
state. The host receives full render scale and Zelda's 30 FPS target.

`M5_BENCH_OUT` is an external scratch directory containing instrumented `baseline/dmhost`
and `candidate/dmhost` executables. `M5_BENCH_HOST` can select another executable.
The package and host input roots are the three named directories under
`~/SatchelBuild/anygame`. No ROM search or directory discovery is performed.

```sh
ANYGAME_OUT=$HOME/SatchelBuild/m5/stalls M5_BENCH_OUT=$HOME/SatchelBuild/m5/stalls \
python3 -B NativePrepare/guard.py python3 -B NativeHost/bench_shaders.py \
  --route baseline --game TOTK --cache clean
```

Repeat with `--cache warm`, then the candidate, for `Castlevania`, `Odyssey`, and `TOTK`.
Clean mode requires a new application cache directory. Warm mode reuses that directory
in a new process. Neither mode clears Apple's private compiler cache. Do not call this
an OS-cold measurement. Captures and frame counts must establish a matching scene before
a timing comparison is accepted. Castlevania's disappearing labels are a known baseline
issue; record their state separately from the scene match.

`DM_SHADER_EVENTS` enables payload-free TSV events with monotonic start nanoseconds,
duration nanoseconds, thread ID and operation. `submit-thread` identifies submission
threads. `translation` and `library-create` measure translator and source-library calls.
`pipeline-create` measures Metal render/compute creation and specialization;
`pipeline-archive` measures archive lookups, including misses. `prepare-use`,
`library-use`, `pipeline-use`, and `pipeline-wait` include required first-use waits.
Do not add nested durations. Union their intervals on each submit thread for a total.
The existing `DM_NVN_TSV` supplies frame intervals, draw counts and worst-frame evidence.
Events cover the whole process route; frame statistics stop at frame 1800.

The trace is diagnostic and includes per-event file I/O overhead. Production tracing is
off unless requested. The app's `-SatchelGraphicsProfile YES` writes `shader-events.tsv`
beside `graphics-frames.tsv` under the selected runtime or graphics trial directory.

# Scheduling and cache behavior

Mapped shader registration copies control and code into bounded background requests.
Unmapped shaders retain exact first-use translation. Vertex library compilation waits
for the actual fragment-input padding or a previously observed startup request.
Fragment and compute base modules can compile after registration. Optional variants
compile only when required or previously observed. Two translation/library workers
share the existing compiler and persistent source store with Prepare and warm-up.
Once a launch has recorded requests, registration speculation yields to that exact
startup list. Replay takes at most 32 library variants, leaving space in the 64-library
live cache for demand instead of evicting its own work with a 128-request replay.

Exact pipeline creation uses two serial worker lanes. Dispatch is asynchronous and the
caller joins completion before encoding its draw. Nested work stays on the same lane.
New work reserves an available lane, so a long compile cannot trap later jobs behind
it while the other lane is idle. Only actual Metal creation calls occupy lanes;
duplicate base requests wait outside them, leaving unrelated work able to progress.
The pending-pipeline fixture retains its 52 checks,
with demand compilation and prediction ordering verified on the worker thread.
The default pipeline policy is `exact`: Metal 4 and persistent archives remain enabled,
with hybrid stand-ins disabled. Diagnostic `hybrid` and `legacy` policies remain explicit
launch options. The bounded graphics suspend overlay is unchanged.

The source store validates translator identity and payload hashes; Metal library keys
include source hash, GPU, OS build and compile options. Pipeline archive namespaces add
the pinned engine and translation chain to GPU and OS identity. Prepare and runtime use
the same archive directory. Update the namespace revision when pipeline construction or
translation semantics change.

A source cache hit does not prove that Metal avoided compilation. Apple's public
[MTLLibrary API](https://developer.apple.com/documentation/metal/mtllibrary/)
provides no executable-library serialization method. The existing Metal 4 archives
persist pipeline bases; the OS manages the source compiler's own cache. No private
metallib extraction or unsupported serializer is used here.

The runtime source working set is 64 MiB. Each retained resource pool has at most 64
compiler library references; the pool retains three game directories. Speculative
requests are limited to 64 outstanding jobs per resource, 32 MiB for translation input
and 64 MiB for compilation input. Startup metadata retains 128 request identities.
These are bounds on owned references and queued inputs, not a bound on Metal's internal
allocation or the renderer's existing shader and pipeline caches. Measure process RSS,
GPU allocation, and persisted cache size on the route and on a scene-switch soak before
claiming a total memory bound.
