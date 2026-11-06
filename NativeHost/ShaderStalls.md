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

# Launch prewarm of recorded pipelines (2026-11-06, Mac)

Why Zelda's title frame (frame 144: 159 new libraries, 122 new pipelines, 1.8 to 2.0 s on the phone)
was built at draw time on every cold trial, in both exact and bounded:

- The package seed is empty. Every format 2 (phone-prepared or runtime-capture) package's
  `dm/pso_manifest.tsv` holds only a comment, so a trial that keeps the seed keeps nothing.
- The recorded list was never read. Each launch appends what it builds to `pso_user.tsv`, but the
  launch policy sets `DM_PSO_MANIFEST=off` (since the first release), so the engine's prewarm never ran
  in the app; a warm launch is faster only because the pipeline archive and Apple's compiler cache
  keep the previous launch's work. A cold trial also starts its list fresh.
- Turning it on alone would not help: the prewarm drops every line whose shader key is not in the
  shader map, and a phone-translated shader enters the map only when the game hands its bytes over at
  its first draw. The descriptors could not be rebuilt before the program is bound.

So on a game's first launch nothing is known and the title frame still builds everything at draw
time. What can move is everything a device has already recorded: the next launch after an OS update
or a cache namespace bump (an empty archive with the list kept), and the library, translation and
archive-lookup residue of every warm launch. The opt-in launch prewarm
(`NativeHost/graphics/README.md`, Launch prewarm of recorded pipelines) builds the recorded list
before the first frame, preparing shaders from the device's own first-use capture. The alternative,
building exact pipelines in the after-Prepare warm-up, was not taken: that job runs before anything is
recorded, and it builds only vertex-pulling bases without a vertex layout, which exact draws never
use; building exact descriptors there would mean reimplementing the engine's descriptor construction.

Routes: exact, render scale 1.0, 1,800 frames, first-use capture on in every run (as in the app). Clean
is a fresh namespace (nothing recorded); warm reuses it; relist is a new namespace with the clean run's
recorded list, translated sources and capture but no archive. Main is c2c4c45's host, prewarm is this
change with `--prewarm` (budget 3,000 ms). Each was run twice in the order main then prewarm, then
prewarm then main, because macOS keeps one system Metal compiler cache for every process. Two Odyssey
routes (one main relist, one prewarm clean) ended with the same guest crash at the same address in
frames 6 to 11 on both hosts and were repeated as a third pair. Values are the two runs. Split is time
inside frames over 50 ms; first present is seconds from host start.

| Route | Worst frame, ms | > 50 / > 100 ms | Translation / library / pipeline, ms | First present, s |
|---|---|---|---|---|
| Zelda clean, main | 14,363; 13,252 | 19/13; 19/12 | 4,500 / 16,136 / 3,600; 4,811 / 14,865 / 3,594 | 1.08; 0.77 |
| Zelda clean, prewarm | 14,509; 5,780 | 18/14; 14/12 | 4,917 / 15,760 / 3,654; 4,265 / 2,390 / 3,535 | 0.76; 0.78 |
| Zelda relist, main | 2,652; 3,031 | 22/10; 15/10 | 258 / 185 / 3,556; 345 / 208 / 4,131 | 0.81; 0.87 |
| Zelda relist, prewarm | 367; 353 | 6/2; 6/1 | 7 / 0 / 0.2; 7 / 0 / 0.2 | 2.69; 2.86 |
| Zelda warm, main | 566; 742 | 9/3; 11/4 | 257 / 149 / 61; 347 / 174 / 85 | 0.61; 0.69 |
| Zelda warm, prewarm | 343; 404 | 5/1; 3/1 | 7 / 0 / 0.2; 8 / 0 / 0.2 | 1.13; 1.19 |
| Odyssey clean, main | 264; 213 | 3/3; 3/2 | 157 / 254 / 85; 171 / 134 / 81 | 1.16; 0.92 |
| Odyssey clean, prewarm | 204; 266 | 3/2; 3/3 | 167 / 117 / 84; 145 / 273 / 90 | 0.97; 1.06 |
| Odyssey relist, main | 71; 69 | 2/0; 2/0 | 8 / 5 / 75; 10 / 5 / 66 | 0.92; 0.90 |
| Odyssey relist, prewarm | 24; 26 | 0/0; 0/0 | 0 / 0 / 0; 0 / 0 / 0 | 0.95; 0.98 |
| Odyssey warm, main | 37; 36 | 0/0; 0/0 | none | 0.85; 0.85 |
| Odyssey warm, prewarm | 31; 30 | 0/0; 0/0 | none | 0.87; 0.91 |
| Castlevania clean, main | 305; 294 | 5/4; 5/5 | 206 / 249 / 106; 196 / 251 / 100 | 1.86; 1.93 |
| Castlevania clean, prewarm | 382; 261 | 10/8; 5/3 | 250 / 422 / 116; 116 / 9 / 112 | 1.86; 0.98 |
| Castlevania relist, main | 282; 216 | 5/3; 5/3 | 18 / 6 / 123; 15 / 8 / 110 | 0.89; 1.00 |
| Castlevania relist, prewarm | 226; 217 | 2/2; 2/2 | 0 / 0 / 0; 0 / 0 / 0 | 1.09; 0.92 |
| Castlevania warm, main | 292; 215 | 3/2; 3/2 | 9 / 4 / 1; 8 / 2 / 1 | 0.80; 0.75 |
| Castlevania warm, prewarm | 236; 214 | 2/2; 2/2 | 0 / 0 / 0; 0 / 0 / 0 | 1.18; 0.78 |

The prewarm's own time before the first frame: Zelda relist 1.84 and 2.02 s for 469 recorded lines
(264 shaders, 188 render and 17 compute pipelines, 177 bases compiled on two lanes), Zelda warm 0.17 to
0.20 s (archive hits), Odyssey 0.09 s for 15 lines, Castlevania 0.17 to 0.27 s for 24. Every recorded
shader and pipeline was claimed before the first draw, and the routes built no pipeline on the submit
thread afterwards. Clean routes have nothing recorded, so prewarm and main do the same work there; their
spread (Zelda's library time from 2.4 to 16 s) is Apple's compiler cache and shared load, not the
change. Zelda's worst relist and warm frames are now its title frame's other work (about 0.3 s:
guest work, 260 MB of first texture uploads, GPU), not shader waits. No route used a fallback pipeline
or skipped a draw. Odyssey's captured frames 300, 1200 and 1800 are byte-identical between main and
prewarm in clean, warm and relist, and between main runs; Castlevania and Zelda differ between two main
runs as much as between main and prewarm (their openings animate with wall time).

Bounded with prewarm (one Zelda relist) reached the 3,000 ms budget with 190 of 264 shaders: bounded is
hybrid, so the prewarm also loads every vertex shader's pulling library. Its worst frame was 1,020 ms;
the prewarm is meant for exact. Nothing here is a phone result: the phone has fewer cores, and its cold
library compiles after an OS update would make the prewarm slower, up to its budget.

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
