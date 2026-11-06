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

Each route result also carries `timing.stalls` from `stall_attribution.py`, which reads the
same frame TSV and event TSV (a phone trial's `graphics-frames.tsv` and `shader-events.tsv`
too): frames over 50 and 100 ms, the worst frames, and for every frame over 50 ms its time
split into library, translation (the draw's own stages), look-ahead translation (a walk
resolving a later program), descriptor, shader setup, pipeline, prediction walk and other.
Frame boundaries are the host's own `frame` events; each submit-thread instant goes to one
category, innermost first. Other is everything without a shader event: guest work, GPU
waits, pacing and loading.

# Bounded first use (2026-11-06, Mac)

Where cold first use went (exact mode, first run of the table below): Zelda's title frame
(155 new libraries, 120 pipelines, 6.3 s) spent 1.35 s in the draw's own translation, 0.84 s
in look-ahead walks translating later programs one by one on the frame path, 1.78 s in
libraries and 2.10 s in pipelines. Descriptor building stayed under 2 ms per route. Odyssey's slow frames were mostly
libraries and translation; Castlevania's worst frames had no shader work at all.

The opt-in `bounded` pipeline mode (`NativeHost/graphics/README.md`, Bounded first use) starts
translation and base libraries when the game binds a program, lets the draw do its own work on
its thread instead of queuing behind background lanes, keeps prediction walks from translating
unready programs, and gives the submitting thread the next pipeline lane. It never draws with a
different program or pipeline. Cold opening routes, render scale 1.0, final build, one run each
(exact twice), 1,800 frames. "Shader wait" is the shader part of the worst frame; the second
value is the next worst frame's.

| Route | Frames > 50 / > 100 ms | Worst frame | Shader wait, worst / next | Frames with shader wait > 100 ms |
|---|---|---|---|---|
| Castlevania exact (two runs) | 6/3, 5/2 | 207, 214 ms | 120 / 63, 138 / 56 ms | 1, 1 |
| Castlevania bounded | 5/3 | 233 ms | 117 / 64 ms | 1 |
| Odyssey exact (two runs) | 3/3, 3/3 | 238, 245 ms | 221 / 112, 229 / 104 ms | 3, 3 |
| Odyssey bounded | 3/1 | 132 ms | 113 / 57 ms | 1 |
| Zelda exact (two runs) | 15/11, 13/12 | 6,330, 3,814 ms | 6,112 / 790, 3,592 / 853 ms | 11, 11 |
| Zelda bounded | 13/10 | 2,407 ms | 2,181 / 531 ms | 8 |

In those Zelda slow frames, translation fell from 3.8 and 3.2 s (exact) to 0.68 s; pipelines
stayed at 3.2 to 3.5 s and are now most of what remains. The worst Zelda frame is the title
scene's first frame. After it, Zelda still has frames over 100 ms: bursts of 7 to 23 new
pipelines in the next frames (119 to 556 ms, pipeline waits 95 to 428 ms) and a vertex shader
translated again because a new pixel shader pairing widened its padding (frame 875: 48 ms
translation and 217 ms library). So the
goal of no shader stall over 100 ms after a scene's first frame is not met on the Mac for
Zelda, and only Castlevania's and Odyssey's single worst first-use frames come close.

Warm routes are unchanged in kind: exact and bounded have no frame with shader wait over
100 ms in Castlevania or Odyssey, and one in Zelda (the title frame: 294 ms exact, 258 ms
bounded). The order of runs matters: exact ran first in every set, so later runs reuse Apple's
system compiler cache (exact's second Zelda run spent 0.2 s in libraries against 3.0 s in its
first). Translation is the application's own work and its reduction does not depend on that.
Mac CPU has more cores than the phone; the lanes are the phone's (two ahead, two pipeline,
three library compilations), but no phone result follows.

Pixels: no bounded run used a fallback pipeline or skipped a draw (zero `pipeline-fallback`
events, zero skips), so every draw used the exact pipeline exact mode makes. Captured frames
300, 1200 and 1800: Odyssey is byte-identical across both exact runs and bounded. Castlevania
and Zelda differ between the two exact runs as much as between exact and bounded (the opening
advances with wall time: Castlevania's frame 300 showed the notice screen in one run and the
middleware logo in another; Zelda's title animates). A detclock run was tried for parity and
Castlevania reached only 115 frames in 300 s, so it is not used.

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

The namespace is `<engine pin, 8 digits>-<chain>-v<revision>-<GPU>-<OS build>`
(`NativeHost/graphics/PipelineSettings.m`). It is `f8a5618d-26dc2fa9477c-v2` since the
engine pin move of 2026-11-05: the pin, the chain and the revision all changed, because
the new engine builds pipelines differently (one `_bx` side library for the alpha test
and logic operation entry points, continued render passes, sparse and chunked pools).
Archives under the earlier `bbc3b737-e65a4ebd3322-v1` namespace are left on disk and are
never read again; every game rebuilds its pipelines on first play after the update.
It is `f8a5618d-26dc2fa9477c-v3` since the phone's shader translator moved to the engine pin
(also 2026-11-05): the translated shaders changed, so `-v2` archives are not read either.

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
