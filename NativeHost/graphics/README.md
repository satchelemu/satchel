# Satchel graphics overlay

This reuses nxnative f8a5618d's hybrid render pipelines, pending-build queue,
vertex pulling, archive serialization and shader-pair preparation. It does not
change shader translation, resolution or game time. Exact and Async preserve
rendering semantics; the separate opt-in Skip mode can omit a terminal draw.

`PipelineSettings.m` selects `exact` by default. Compiler calls run on two
workers, but required first use still joins completion. `hybrid` is an explicit
experiment using equivalent vertex pulling while an exact layout builds.
`async` additionally queues ordinary demand pipelines before waiting and creates
fallback pipelines on workers from owned descriptor copies. The existing shader
revision, target formats, blend, channel mask and alpha-to-coverage state remain
part of the key. A later draw takes the completed exact pipeline.

The async optional wait budget defaults to 4 ms shared across the presented frame.
A ready equivalent fallback can serve the draw. Without one, exact completion is
required. This is **not a universal 4 ms frame-thread bound**: translation, library
loading, descriptor variant resolution, compute, utility and specialized passes
can still wait. OS scheduling can also overshoot a timed wait. Async mode
never discards a visible draw or substitutes an unrelated shader. A pulling library
must already be loaded for its exact revision before a fallback job is admitted;
the frame thread does not load it merely to try the timed fallback.

Async introduces no draw-skipping rule. Existing proven no-effect eliminations
remain. Skip has the separate terminal-pass policy described below. Stencil,
storage/image writes, transform feedback, active queries and unknown dependencies
within the current submission require exact results. The scan cannot prove that
later submissions or frames will not sample an omitted color or depth result.
Skip therefore remains experimental even when its terminal-draw check succeeds.
A strict bound without these omissions needs owned command/resource continuations
or a ready equivalent implementation of every required shader.

Archives live in `Documents/NativeRuntime/<title>/pipelines/<GPU>-<OS build>/`.
The existing serializer writes temporary files and renames successful results;
content-based Metal lookups and the engine's chain/depth/pair stamp reject stale
bases. Different GPU architectures or OS builds use different folders. Older folders
are retained. The engine already handles corrupt archives by rebuilding and guards
compaction against repeated framework failures. First launch can take longer and
consume additional memory and disk while making the fallback bases. Those costs need
phone measurements before release acceptance, especially for TOTK's large pair list.

`pipeline.patch` publishes each successfully serialized archive to this session's
lookup arrays after its atomic rename. Previously the prebuild's new bases existed
only on disk until a restart, so a cold launch prepared them and then built them again
on the submit thread. Readers retain immutable snapshots under a short lock; the
archive queue opens and publishes completed files. Failed writes or opens publish
nothing and keep the existing exact-pipeline fallback. No reader sees a serializer
that is still capturing or an unfinished file.

The optional engine setting `DM_PSO_PREBUILD_A2C=1`, enabled by the explicit
prebuild experiment, prepares both alpha-to-coverage base states. Metal requires distinct bases
even for one sample; disabling coverage to reuse a base would change rendering.
The preparation stamp includes this policy so older caches receive the missing bases.
This increases first-load preparation work and needs phone memory/time measurement.

The patch also removes the early return when the package has no seed manifest.
Previously this also discarded the device's valid user manifest and skipped shader
pair preparation. An empty seed is now optional. An explicit `DM_PSO_MANIFEST=off`
retains its original meaning. The overlay also disables upstream's iOS cold-start
compiler-folder cleanup: Satchel can prepare shaders and then play within one process,
so those folders cannot be assumed to belong to a previous process. Metal owns cleanup.
This does not disable archive compaction within Satchel's dedicated archive directory.

`overlay.py` chains after the display overlay and only replaces the generated
`nvn_mtl.m` build input and the three host inputs used by the optional pause diagnostics. The Metal build input retains the previous overlay directory for quoted includes,
so its generated runtime-capture and phone-shader includes remain reachable.
The shared engine and display patch remain unchanged.
`NativeApp/build.py` fingerprints these inputs. Existing MIT attribution is in
`NativeHost/THIRD_PARTY.md` and the shipped notices.

## Measurement controls

Developer launch arguments override the saved per-title pipeline mode for one launch:

- `-SatchelPipelineMode legacy`: use the former synchronous compiler path.
- `-SatchelPipelineMode exact`: the default, join exact completion.
- `-SatchelPipelineMode hybrid`: the existing vertex-pulling experiment.
- `-SatchelPipelineMode async`: timed selection with equivalent ready fallbacks.
- `-SatchelPipelineMode skip`: the same selection, with omission of a proven terminal draw after timeout.
- `-SatchelPipelineMode bounded`: async selection plus bounded first use (`DM_PSO_AHEAD=1`), below.
- `-SatchelPipelineBudgetMS <0..16>`: optional wait budget in milliseconds, default
  4. Invalid values use 4. Mac equivalents are `DM_PSO_ASYNC=1` and
  `DM_PSO_BUDGET_MS=4`. Required correctness waits are outside this budget.
- `-SatchelPipelineTrial <tag>`: isolate recorded pipelines and archives without
  touching saves or existing caches. Tags use letters, digits, underscore or hyphen,
  at most 48 characters. Reuse a tag for a warm repeat; use a new tag for a cold trial.
- `-SatchelPipelineTrialListFrom <tag>`: a new trial starts with another trial's recorded
  list (`pso_user.tsv`) and its own empty archive, the state after an OS update or a cache
  namespace bump. Copied only when the new trial has no list yet.
- `-SatchelPipelinePrewarm YES`: launch prewarm of recorded pipelines, below.
  `-SatchelPipelinePrewarmMS <250..30000>` bounds its loading time, default 3000.
- `-SatchelPipelineLanes <1..8>` and `-SatchelPipelineAhead <1..128>` (Mac: `DM_PSO_LANES`,
  `DM_PSO_SPECULATIVE`; `bench_shaders.py --lanes --speculative`): developer trials of first-use
  pipeline scheduling. Lanes are the pipeline compiler workers (default 2). Ahead lets the prediction
  walk start that many of a submission's predicted pipelines at once (default 2, 16 in bounded) and
  makes the walk and the library prefetch follow the replay's return stack, so a first miss inside a
  called command list still sees the rest of the submission. With ahead set, speculative builds wait on
  serial feeders in admission order (a draw can still take its queued build and make it at once) and the
  submitting thread takes the next free lane first. Invalid values leave both unset. They change when a
  pipeline is built, never which one a draw uses. Measured in `NativeHost/ShaderStalls.md` (Prediction
  coverage); `-SatchelPipelineLanes 4 -SatchelPipelineAhead 128` is the Mac-tested pair.
- `-SatchelGraphicsProfile YES`: enable timing TSV, slow-frame attribution, pipeline
  waits and the engine GPU profile every 300 frames. The log remains `host.log`;
  `graphics-frames.tsv` is under the trial directory, or runtime root without a tag.
  Each launch starts `shader-events.tsv` (with a `launch` row) and `pipeline-skips.tsv`
  fresh, as the engine does for `graphics-frames.tsv`. Before 2026-11-06 the two event
  files were appended, so a warm repeat under its cold run's tag mixed both launches and
  misaligned `stall_attribution.py`; the script now uses the events after the last
  `launch` row and refuses events that span more frames than the frame file
  (`NativeHost/check_stall_attribution.py`).

Cold here means a fresh app archive and user manifest. It does not purge Apple's
system compiler cache or the package seed manifest. A phone-prepared package's seed
(`dm/pso_manifest.tsv`) holds only a comment, and the translated shader cache and the
first-use capture are per title, not per trial, so a cold trial knows no pipeline at all. Capture runs are separate:
Metal cannot capture specialized pipelines, so `DM_GPU_CAPTURE` or
`MTL_CAPTURE_ENABLED` forces the engine's legacy compiler path. Never report those
capture timings as hybrid timings.

`check.py` reproduces both the unpublished-archive and missing-seed defects in the
unmodified engine, then checks the fixes with real Metal pixels. It also runs the pinned pending, archive,
stalled-compiler, pulling and warm-up tests. `summarize.py` reads per-frame TSV data,
including upload cost, faults, submission, pacing, GPU time and pipeline creations.
Use frame ranges matching the same route. Summaries of all frames include loading
and movie time and do not establish open-world performance.

## Whole-frame pause attribution

`-SatchelPauseProfile YES` enables the engine's bounded hitch sampler after 40
seconds, at a 100 ms no-present threshold, at most 24 snapshots per process.
`stalls.patch` removes general register values from that sampler. It retains
symbolic stack offsets and adds resolved mutex-owner handles, requested/acknowledged
pause state and cumulative user/system CPU time. The acknowledged pause flag may
remain set until a resumed thread next leaves its SVC; read it together with the
request flag. Snapshots suspend each thread briefly and are attribution runs,
not frame-rate acceptance runs. The optional session-boundary experiment still
disables the sampler until its diagnostic thread has a managed lifetime.

`host-time.patch` and `HostPause.h` add opt-in `DM_HOST_PAUSE=1` timing for ReadFile,
heap/map/unmap/query-memory SVCs and SetThreadActivity/GetThreadContext3. A cleanup
scope covers success and error returns. Logs contain operation IDs, cumulative
calls, wall time, CPU time and maximum single-call duration, plus calls over 10 ms.
Concurrent cumulative counters are approximate snapshots rather than one atomic
transaction. They include time waiting for locks or scheduling, so compare wall
and CPU. The diagnostics never print file contents, hashes, general registers or
memory payloads. `thread-cpu.tsv` uses the engine's per-thread CPU monitor.
These controls do not change the game clock, resolution, scheduler or rendering.

The graphics profile also enables the engine's GPU wait ledger. Its timings do
not include every guest wait or presentation wait. A small submit and GPU time
does not imply that the rest of a frame is unused host CPU time.

For Mac comparisons, use adjacent interleaved trials with the same scene and build
(or a single explicit candidate switch), and record shared load for each trial. Also
check `front end: window covered`: an occluded CAMetalDisplayLink can fire once
per second. `DM_WINDOW_TOP=1` helps but does not guarantee visibility across Spaces.
Offscreen runs without `DM_WINDOW` isolate guest and host loading costs from that
presentation issue; they cannot validate onscreen pacing or phone performance.

## Async validation

`check.py` retains the pending, archive, pulling and failure assertions and adds
held-compiler deadline, duplicate, exact-key fallback, retry, session-reset and
real-Metal descriptor-snapshot fixtures. Timeout events contain only timings and
categories. `pipeline-required-wait` means the optional budget expired without a
ready fallback, so the caller continues through the existing exact join.
`pipeline-fallback` means equivalent drawing continued; it does not mean a draw
was skipped. `pipeline-ready` means the exact request completed during selection.

`bench_shaders.py --pipeline-mode async --pipeline-budget-ms 4` uses the same
idle opening inputs and 1,800-frame limit as the exact baseline. Check captured
scenes as well: Odyssey cold async remained on its controller screen at that
limit, requiring a matched 3,600-frame comparison to reach mode selection.
Cold means a new application cache namespace, not a cleared Apple compiler cache.
First-use hitch frames exceed 50 ms and also record a new library or pipeline;
that is correlation, not proof of the entire delay's cause. The harness also reports
merged submit-thread wait episodes above the budget, total frames above 50 ms
and worst frame time. No phone result follows from a Mac run.


The 2026-11-04 Mac fixed-request comparison covers 760 captured requests
(Castlevania 63, Odyssey 225, Zelda 472) and 1,780 translated variants. Both
independently built shader closures reproduced every source, reflection and
identity field with zero missing or different requests. The shader source closure is unchanged
by the pipeline modes, including after the current-main integration. This proves that fixed translation
corpus; it does not prove every game's rendering or a frame-time bound.

The integrated 2026-11-04 check against main `1d0893c` also compares 2,048
framebuffer bytes per mode across 16 synthetic states and 32 draws. Main exact,
candidate exact and candidate async have zero differing bytes. The full graphics
suite passes 931 assertions plus the archive Python test. All 12 exact/async
cold/warm Mac opening routes across Castlevania, Odyssey and Zelda finish with
zero skipped draws and zero shader failures; warm async exercises 34 ready
fallbacks. The endpoint checks include visual review, zero Odyssey RGB
differences, small Castlevania differences and different Zelda animation phases.
These checks do not establish whole-game pixel equality or a phone speedup.

## Terminal draw skipping

The shared per-title Game settings panel offers Exact (default), Async
and Skip (experimental), alongside Hybrid and Legacy. They share the `SatchelPlayerSettings.<titleID>` dictionary with screen
mode and haptics, under `DM_PIPELINE_MODE`. A change applies on the next launch.
Launch arguments take precedence without saving. The developer overlay shows
skipped draws in the most recently submitted frame. Graphics profiling writes
`pipeline-skips.tsv` with a frame number and count, including zero-count frames.

Skip is deliberately narrow. Only an ordinary final draw in the current submission with known vertex and
fragment reflection can be omitted. Storage bindings, images, bindless or dynamic
resource access, transform feedback, stencil, active queries, geometry and merged
or deferred passes wait. The complete remainder of the submission is walked,
including nested calls, caller returns and later handles. Only harmless state
commands may follow. Another draw, target change, copy, readback, query, dispatch,
signal, unknown command, excessive nesting or scan limit requires waiting.
Implicit pass-end storage mirrors, compressed/depth view shadow writebacks and
attachment diagnostic readbacks also reject skipping. Even copies unrelated to
the current attachments reject skipping in this policy.
No general deferred-draw continuation is introduced.

An unfinished pipeline job remains queued after omission. Later draws retry it;
failed jobs and exhausted queues retain the existing exact path. Skipping can
leave a missing final layer, stale color or depth, or a brief flicker. It is an
experimental artifact tradeoff, not equivalent rendering. Shader translation and
library loading still wait and can exceed the budget in all three modes. The
phone warm-up queue and eviction policy are unchanged.

The 2026-11-04 Mac opening matrix completes all 18 title/mode/cache cases with
zero shader failures. Skip omits zero draws in its six 4 ms measurements. Separate
0 ms diagnostics for all three titles also omit zero draws; their endpoint
captures retain the visible menus and artwork. All six diagnostic routes complete.
No real omission-triggered frame was captured,
so omission artifacts and any skipping speedup remain unverified. The synthetic
suite exercises an eligible omission and the policy's conservative rejection cases.

`request-equivalence.swift` re-translates a fixed captured request set with an
independently built shader closure. It verifies the captured record and payload
hashes, reconstructs the exact request identity, and compares every translated
field. Missing requests fail. Captures and payloads must remain outside the
checkout and logs. This complements the real-Metal pulling and descriptor pixel
fixtures; it does not establish whole-game rendering coverage.

## Bounded first use

The `bounded` mode (`DM_PSO_AHEAD=1`, per-title choice "Bounded (experimental)") is async
selection plus three changes that move shader work off the frame path. It adds no stand-in
shader and omits no draw, so every draw uses the same translated program and exact pipeline as
in exact mode; only when the work happens changes.

- `phone-ahead.patch` calls `satchel_phone_ahead` (PhoneShaderRuntime.inc) when the game records
  BindProgram. A program's pixel (or compute) stage, then its vertex stage padded with that
  pixel stage's inputs and the key's persisted mask, are translated and their base libraries
  compiled on two ahead lanes (ShaderWarmupResources.swift). The requests are the identities the
  draw computes, so a finished job makes the draw's translation a source-store hit and its base
  library a held result (at most 128 held, 256 queued jobs). Embedded kernels and geometry and
  tessellation stages are left to the draw. The draw then prepares on its own thread instead of
  queuing behind background lanes; the source store and compiler join identical in-flight work.
  The compiler allows three concurrent compilations in this mode; a warm-up that finishes during
  play restores its usual two.
- The pipeline prediction walk no longer translates and loads later programs one by one on the
  frame path: programs whose ahead job has not finished are left out, a later pipeline miss in
  the same submission walks again (at most 32 times, 10 ms apart, only after more ahead jobs
  finished), up to 16 predicted pipelines are admitted, and the walk never compiles a pulling
  library.
- A pipeline build the submitting thread waits for takes the next free compiler lane before
  speculative and background builds (ShaderBuildQueue.inc).

Required waits remain: a draw whose translation, library or pipeline is not ready still waits
for exactly that work, as in exact mode. This is not a strict per-frame bound. The measured
distribution is in `NativeHost/ShaderStalls.md`.

## Launch prewarm of recorded pipelines

Each launch appends the pipelines it builds to `pso_user.tsv` (in first-use order), but the
launch policy has always turned the engine's prewarm off (`DM_PSO_MANIFEST=off`), so the list
was never read: a later launch is faster only because the pipeline archive and Apple's compiler
cache keep what the previous one built. When the archive is empty (an OS update or a namespace
bump changes its folder, or a trial) every recorded pipeline is built again at its first draw.
The prewarm could not simply be turned on: it drops every line whose shader key is not in the
shader map, and a phone-translated shader enters that map only when the game hands its bytes
over at the first draw, so no recorded descriptor could be rebuilt before then.

`-SatchelPipelinePrewarm YES` (Mac: `DM_SATCHEL_PREWARM=1`, `DM_PSO_PREPARE=1`,
`DM_PSO_PREPARE_MS`) runs the engine's own blocking prewarm of the seed and `pso_user.tsv` at the
first submission, before anything of it is encoded, and claims the results before its first
draw. `prewarm.patch` and `PhoneShaderRuntime.inc` add three things:

- A recorded key missing from the shader map is prepared from this device's first-use capture
  (`shader-capture/<key>.ctl` and `.code`, kept for every format 2 package) through the same
  `PhoneShaderRuntime.prepare` a draw calls, with the persisted vertex padding. The capture only
  holds bytes whose key they reproduce, and the request check repeats that. Keys without a
  capture stay unknown and are built at first use, as before.
- A loading budget: past `DM_PSO_PREPARE_MS` no worker takes another line (one already started
  finishes), and the rest is built at first use. The list is in first-use order, so the opening
  is built first. The engine's own progress screen shows after 0.5 s.
- When phone shaders are on, a prewarmed pipeline or shader whose key already holds another
  shader revision (a vertex shader translated again with wider padding) is dropped at claim time
  instead of being attached to that revision.

The pipelines are the engine's prewarm pipelines: built from the recorded key with
`pso_desc` and taken by a draw only when its whole pipeline key (shader objects and every state
field) is equal. No stand-in, no skipped draw, and the pair-list bulk prebuild stays off. It is
opt-in because it adds loading time before the first frame and keeps every prewarmed library
alive for the session; exact without it is unchanged. Measurements are in
`NativeHost/ShaderStalls.md`. It does nothing on a first launch: nothing is recorded yet.

## GPU pass cost diagnostics

`gpu-cost.patch` extends the opt-in Metal counter records with the generated source
line that ended each render pass and the union of its shader write stages (vertex 1,
fragment 2, other 4). `-SatchelGraphicsProfile YES` enables sampling;
`-SatchelGraphicsCounters NO` disables encoder sampling while retaining frame TSVs.
Counters are off on a normal launch. The diagnostic Mac host uses
`DM_NVN_GPUPROF=every:120` directly.

`gpu-cost-summary.py LOG GENERATED_NVN_SOURCE` records the exact source hash, valid
and declared timestamp counts, pass-ending sites and pass workloads grouped by
attachment shape or compute/copy operation counts. Both render and compute/copy
encoders participate in the workload ranking. Source lines change when overlays change, so retain the generated
source with each build. A group is a measured workload, not a semantic game pass
name or proof that the boundary can be removed. Encoder intervals may overlap;
never equate their sum with whole-frame GPU busy time. The span includes idle gaps.
Missing timestamps make per-frame workload means lower bounds, and frames with no valid
counter results remain in the summary. Use `--first` and `--last` to exclude loading.

Build the private Mac host with `NativeApp/build.py --graphics-mac-host --unsigned`,
then run `gpu-check.py` and `gpu-summary-check.py` with the same `SATCHEL_OUT`.
This host needs the existing graphics Mac libvpx and utility shader outputs under
`SATCHEL_BUILD_ROOT/graphics/mac-engine/dm/host/gen`, plus the pinned engine,
Zelda profile/toolchain metadata and the documented tool snapshot. It is a maintainer
diagnostic, not a reproducible standalone distribution. It applies the current
pipeline worker and metrics overlays before compilation. No game packages are copied.

Gameplay timing uses paired, interleaved ABAB trials in one session, with at least
five pairs per scene. A/A repeats establish attribution variability. Record load and
process counts per trial; use Metal counters, a settled shader cache and scale 1.0.
Adjacent pairs reduce shared-load drift, but do not guarantee equal instantaneous
contention. Report median B minus A, quartiles, range and load differences. Do not
wait for a quiet Mac for these measurements. The heavy-build exclusion still applies
to compilation. Preparation runs and menu captures do not establish gameplay GPU cost.

`gpu-paired-summary.py LOG SOURCE --first FRAME` pairs adjacent samples 120 frames
apart. Add `--phase` with counters sampled inside `DM_AB=120` phases to discard each
phase's first quarter and compare the remaining medians. Each accepted phase
requires at least three samples, as with `every:30` and 120-frame phases. The log must prefix frame
summaries with `[observed UNIX_SECONDS load1 VALUE]`. Unpaired trials are unused;
fewer than five complete pairs fails. Missing timestamps reject whole-frame sums.
`--shape 'ATTACHMENT LABEL'` can compare a complete target only when every missing
encoder has a `gpuprof-invalid` label and none matches that target. Missing unrelated
work stays explicit and no whole-frame span comparison is emitted. No missing value
is converted to a zero duration. `gpu-paired-check.py` covers these boundaries.

The summarizer also ranks individual occurrences within each shape and ending site,
with sample counts. `gpu-paired-summary.py --shape LABEL --occurrence N` compares
one such occurrence rather than the sum of its group. Add `--site LINE` when the
same shape has several ending sites. The experiment selector counts all occurrences
of its shape; the analysis identifies them by ending site too, so verify their
correspondence in the sampled frame before attributing an effect. These are structural pass
names; they do not assert an unverified semantic role in the game.

Set `SATCHEL_GRAPHICS_REPLAY=1` during the Mac host build to also build
`gpu-mac/tools/nvn-replay` from the pinned MIT engine. Its title check uses the same
runtime profile as the host; record-layout and shader-interface checks remain strict.
Keep captured frames outside the repository, use the same capture for both modes,
and compare decoded pixel bytes with zero tolerance unless an existing reviewed
allowance applies. Replay and capture times are not live-game performance results.
The 2026-11-04 Mac trials do not establish a full-resolution phone speedup.
Rejected clear and consumer-barrier experiments are excluded from this diagnostic
patch. Their private source and evidence remain in the maintainer's graphics task
folder. No rendering optimization is enabled by the diagnostic controls.

For a selected target, every accepted sample must contain that shape, ending site
and occurrence in both phases. An absent candidate target is rejected, never
counted as zero time. Check a screenshot of the measured scene too: frame numbers
alone do not prove that scripted menu input reached gameplay under shared load.

Exact captured-frame bytes are the first image check. When the pinned engine's
existing regression allowance is used instead, label it explicitly: eight fresh
baseline replays set a fixed limit of twice the worst baseline differing-pixel
count before any candidate validation. Passing that rule is not byte identity,
a measured speedup, or phone acceptance.

## Spatial presentation upscaling

`DM_PRESENT_UPSCALE` is a per-title player-profile value: `off` (the default) or
`spatial`. `SatchelSavePlayerSettings` preserves the choice when a screen or haptics
save omits it. `SatchelQueuePlayerSetting` saves it for the next launch, leaving the
running renderer unchanged. The developer argument is `-SatchelUpscaler spatial`.
The game-page worker can show this value through the existing settings bridge; this
change adds no page UI.

`upscale.patch` reuses the pinned MIT engine's Metal 4 spatial scaler at final
presentation. It requires a valid render scale from 0.5 up to but excluding 1.0 and
an output viewport at least as large in both dimensions, larger in one. Raw RGBA8
and BGRA8 perceptual scan-out formats are supported. Other formats, unavailable
MetalFX, refused sizes or allocation failure use the existing bilinear filter.
The engine copies the valid crop into private input storage with the scaler's
required usage bits, preserves gamma bytes and origin, serializes resource reuse,
and throttles scaler replacement while the display resizes. MetalFX does not
supply temporal anti-aliasing or recover scene detail already lost before scan-out.

`upscale-check.py`, after the diagnostic Mac build, checks profile persistence and
the pinned crop, color, orientation, resize and GPU readback cases with synthetic
pixels. It also checks scale policy and unsupported-format fallback. No game
payload is required. The default remains off pending phone acceptance.

For measurement only, build the Mac host with `SATCHEL_UPSCALE_DIAGNOSTICS=1`.
`DM_UPSCALE_TRIAL=ab` alternates the presentation filter every 120 presents;
`aa` keeps both phases off. GPU feedback includes the copy, scaler and final
presentation pass. `upscale-summary.py LOG --first FIRST_GAMEPLAY_PRESENT` discards
the first quarter of each phase and requires three unique samples per phase and
five complete adjacent pairs. Fallback, failed or zero-duration GPU samples reject
the pair. Use `--aa` for the control and `--exclude-phase N` for capture phases.
The sampled `upscale-time` lines must carry the same wall-clock and load prefix as
the existing paired tools. This measures presentation, not the game's whole frame.

`DM_UPSCALE_COMPARE=1` writes off and spatial images from the same source texture
for requested screenshots; set `DM_NVN_SHOT_OUT` to the measured display canvas.
Keep images outside the repository, and exclude their phases from timing.
`DM_UPSCALE_AUDIT` optionally names a private TSV of sampled draw-stage identifiers
for joining against pass counters. It never dumps shader source or buffers.
These three controls exist only in the explicitly instrumented Mac host, not the
normal app build. Use a settled Mac shader cache and record shared load. No Mac
measurement establishes phone cost, thermal behavior or motion quality.

## Present edge smoothing (opt-in)

`DM_PRESENT_UPSCALE` also takes `edge` and `edge-spatial` (developer argument
`-SatchelUpscaler edge`, saved per title like `spatial`, next launch). `present_filter.py`
runs last in the overlay chain. It embeds `PresentFilter.metal`, compiles it once on a
background queue and, until it is ready, keeps the plain present. The filter smooths the
valid crop at the scan-out's own texel centres into a crop-sized texture: it finds a local
luma edge, its orientation and side, walks along it to both ends and moves the sample
across the edge by the distance to the nearer end, with a bounded blend for isolated
detail. Flat and low-contrast areas return the plain sample, so a flat picture is
byte-identical. The existing present then shows that texture bilinearly (`edge`) or
through the spatial scaler under its existing rules (`edge-spatial`). Raw RGBA8 and BGRA8
single-sample scan-outs only; anything else keeps the plain present. `off` and `spatial`
encode exactly the commands they did before; the only change on those paths is a larger
present constant buffer (20 floats instead of 8, the last 12 unused).

`upscale-check.py` adds real Metal checks through the offscreen present: the filter
compiles, a flat picture is unchanged, a shallow staircase gains intermediate values only
near its edge, and `edge-spatial` keeps the spatial scaler's scale rule.

### Picture quality comparison on the Mac

A reduced render scale cannot be compared with 1.0 from two runs: wind, idle animation
and Zelda's slowly panning title camera make two 1.0 captures one second apart differ more
(SSIM 0.78 to 0.80) than 0.65 differs from 1.0. The comparison therefore uses one session:

1. Build the pipeline Mac host with `SATCHEL_QUALITY_DIAGNOSTICS=1`
   (`quality-diagnostics.py`; never in the app). Writing `SCALE DIRECTORY` to the file
   named by `DM_QUALITY_SCALE_FILE` saves the next twelve presents as raw scan-out PNGs
   while Zelda's render-scale state is SCALE for presents 3 to 5 only. The reduced size
   reaches the picture three presents later: frames 6 to 8 are reduced.
2. `present-quality --sharpness DIR/f*.png` confirms which frames are reduced.
3. `present-quality PresentFilter.metal 2853x1604 DIR/f005.png DIR/f007.png --floor DIR/f003.png --crop ...`
   presents the 1.0 frame bilinearly as the reference (what the phone shows at 1.0) and the
   reduced frame through `off`, `edge`, `edge1` (single pass at display size), `spatial`
   (MetalFX) and `edge-spatial`, at the phone's inner-screen viewport. It prints SSIM
   (11x11 Gaussian), SSIM over the reference's edge pixels, PSNR per region and the GPU time
   of each pass. `--floor` is a second 1.0 frame two presents earlier; its distance is the
   noise floor. Use a scene that holds still: Zelda's Room of Awakening save gives a floor
   SSIM of 0.999. `--save DIR` writes the presented pictures and region crops; keep them
   outside the repository. `--stride S` lengthens the smoothing step for experiments.

Mac results, 2026-11-07 (M4, Zelda's Room of Awakening save, docked operation mode as the
app runs it, so the 3D view's base is 1600x896 of a 1920x1080 scan-out, presented at
2853x1604). Whole frame, SSIM / PSNR against the 1.0 picture; the floor is SSIM 0.9987 to
0.9988 (PSNR 54 dB):

| Scale | off | edge | spatial | edge-spatial |
|---|---|---|---|---|
| 0.50 | 0.9585 / 38.28 dB | 0.9597 / 38.45 | 0.9414 / 36.99 | 0.9434 / 37.29 |
| 0.65 | 0.9710 / 40.60 dB | 0.9723 / 40.87 | 0.9486 / 38.29 | 0.9523 / 38.96 |
| 0.75 | 0.9765 / 41.62 dB | 0.9779 / 41.95 | 0.9511 / 38.66 | 0.9563 / 39.60 |
| 0.85 | 0.9809 / 43.29 dB | 0.9822 / 43.68 | 0.9523 / 39.23 | 0.9593 / 40.55 |

PSNR on a 3D region (the stone ring and Link) and on the HUD icons, which the game draws at
full size at every scale:

| Scale | 3D off / edge / spatial | HUD off / edge / spatial |
|---|---|---|
| 0.50 | 36.24 / 36.50 / 34.87 dB | 42.64 / 38.15 / 34.66 dB |
| 0.65 | 38.31 / 38.74 / 35.97 dB | 44.26 / 38.58 / 34.82 dB |
| 0.75 | 39.52 / 40.06 / 36.42 dB | 45.45 / 38.81 / 34.98 dB |
| 0.85 | 40.88 / 41.44 / 36.92 dB | 46.92 / 39.27 / 35.02 dB |

Render scale itself dominates: each step from 0.5 to 0.85 gains more than any filter.
Edge smoothing moves every reduced frame slightly closer to the 1.0 picture (SSIM +0.0012
to +0.0014, 3D regions +0.26 to +0.56 dB) and visibly straightens the stair-stepped bright
outlines along silhouettes, but softens the HUD icons by 4.5 to 7.7 dB. The MetalFX spatial
scaler moves every frame further away (SSIM -0.017 to -0.029): it sharpens the reduced
view's stairs and rings the HUD. Edge before spatial recovers part of that loss but stays
below off. A longer smoothing step (`--stride` 1.25 or 1.5) adds at most 0.2 dB on 3D
regions and costs the HUD another 2 to 4 dB; at 0.5 and step 2 the icon outlines break
visibly. The present therefore keeps a one-texel step. The same route in handheld mode
(base 1280x720) shows the same order with smaller edge gains (whole-frame SSIM unchanged
to four places, 3D +0.02 to +0.07 dB). On Castlevania's menu text the filter keeps SSIM
0.998 against the plain present. A filter after scan-out cannot separate the game's 3D
view, upscaled by the game before the HUD is drawn, from the HUD; the place to fix the
reduced view's roughness without touching the HUD is the game's own upscale draw, as the
engine's `DM_UPSCALER` does for one game's upscaler shader.

Mac GPU time per pass at 2853x1604 (medians of 20 command buffers, shared Mac): plain
present 0.28 to 0.54 ms; edge pass at 1920x1080 0.15 to 0.44 ms; single-pass edge at
display size 0.45 to 0.88 ms (so the two-pass form is used); MetalFX spatial 1.05 to 1.8 ms.
The phone measured the spatial present at 3.3 to 6.5 ms, about three to four times these
Mac figures, which puts the edge pass near 0.5 to 1.8 ms on the phone, unmeasured. Both filters stay
opt-in per title; their look in motion and their phone cost need phone checks.

Pixel identity: the overlay leaves `off` and `spatial` command encoding unchanged, and
`upscale-check.py`'s unchanged upstream crop, colour, orientation, resize and readback
assertions cover that path with real Metal. `present-quality --check` compares the engine's
own presented picture (`DM_NVN_SHOT_OUT=2853x1604`, `DM_QUALITY_PAIR=1`) with the tool's
rendering of the same raw frame. On 2026-11-07 (Zelda title screen, docked), three frames
of an `off` session matched the tool's `off` with 0 of 13,728,636 bytes different, and
three frames of an `edge` session matched the tool's `edge` with 0 bytes different (and
about 2.0 million bytes from `off`), so the engine runs exactly this filter and the plain
present is unchanged.

## Phone frame GPU timing

`-SatchelGraphicsProfile YES` enables `[frame-gpu]` summaries in `host.log`, next to
the existing frame and power lines every 300 frames. The process power field
`GPU ... ms/s` is a separate OS accounting value, and may be zero on the phone.
It is not the source of this summary.

The new accounting reads `GPUStartTime` and `GPUEndTime` only from completed Metal
command buffers or Metal 4 commit feedback. It covers game submission segments,
tagged legacy buffers, the scan-out snapshot copy and final presentation (including
MetalFX when used). GPU milliseconds per frame are the sum of those submission
durations, not exclusive GPU busy time: overlapping execution can count twice.
The presentation component is also reported separately. Initialization, diagnostic
readback and unrelated GPU queues are excluded. No GPU wait is added.

CPU time is the present thread's actual CPU clock delta between consecutive
frames, including guest work and host translation on that thread. It excludes
sleep and GPU waits, and does not sum other threads. The wall interval is reported
separately. CPU and GPU samples are matched by frame number and collected eight
frames later. Missing, incomplete, failed, zero or nonfinite timestamps and frames
with no completed presentation are counted and excluded, never recorded as zero.
If none are valid, timing fields read `-1`, with `limit unknown`. Late completions
are excluded from that window rather than blocking the game. Rings and cached
enablement reset at renderer initialization for a new session.

The summary gives nearest-rank median and p95 for GPU submission time, CPU time
and wall interval. `limit` is an inference from their medians and the active frame
budget, with at least 30 paired samples and at least 80 percent valid coverage:

- `GPU-likely` or `CPU-likely`: that time reaches 90 percent of the budget and is
  more than 20 percent above the other.
- `mixed`: both reach 90 percent of the budget without a clear dominant side.
- `paced`: wall cadence meets the budget within 5 percent and both are below
  90 percent of it.
- `unknown`: insufficient evidence, waits, worker-thread limits or another cause.

This is diagnostic evidence, not proof of the critical path. Inspect p95, shader
events, frame intervals and thermal state as well, especially during loading and
compilation. Sparse pass counters remain enabled once every 300 frames when the
iOS Metal 4 device can allocate the timestamp heap. Use
`-SatchelGraphicsCounters NO` to keep frame timing while disabling pass counters.
The installed iOS 27.2 SDK declares both Metal 4 feedback and counter heaps for
iOS 26 onward; phone measurements still need validation.

Without the profile argument, the new hot-path work is a cached enable check:
no locks, allocations, timestamp accumulation or summary writes. Existing engine
diagnostics are unchanged. `frame-timing-check.py` checks synthetic accounting and
real Mac Metal/Metal 4 feedback with generated buffer fills, using the shared
build wait. Its CPU values are synthetic and establish no gameplay performance.
