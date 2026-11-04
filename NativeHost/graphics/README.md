# Satchel graphics overlay

This reuses nxnative bbc3b737's hybrid render pipelines, pending-build queue,
vertex pulling, archive serialization and shader-pair preparation. It does not
change shader translation, resolution, game time or rendering semantics.

`PipelineSettings.m`, called after package environment and player settings, replaces
old manifests' `DM_PSO_MTL4=0` with the hybrid compiler policy. This selects the
pipeline compiler, independently of the renderer's command-encoding API. Two workers
prepare known shader pairs before the first frame. Existing recorded pipelines warm
up on each launch. Exact vertex-layout pipelines compile asynchronously while a ready
pulling pipeline draws the same geometry. The engine's bounded queues and descriptor
validation remain intact. The system can still compile on the submit thread when no
compatible fallback is ready, for compute pipelines, merged passes or unsupported
vertex variants. Background demand builds use at most two utility queues. This is not a guarantee that every previously unknown shader is
hitch free. Runtime translation belongs to M5.

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

The optional engine setting `DM_PSO_PREBUILD_A2C=1`, enabled by Satchel's hybrid
policy, prepares both alpha-to-coverage base states. Metal requires distinct bases
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

Developer launch arguments, never player settings:

- `-SatchelPipelineMode legacy`: use the former synchronous compiler path.
- `-SatchelPipelineMode hybrid`: use the new default.
- `-SatchelPipelineTrial <tag>`: isolate recorded pipelines and archives without
  touching saves or existing caches. Tags use letters, digits, underscore or hyphen,
  at most 48 characters. Reuse a tag for a warm repeat; use a new tag for a cold trial.
- `-SatchelGraphicsProfile YES`: enable timing TSV, slow-frame attribution, pipeline
  waits and the engine GPU profile every 300 frames. The log remains `host.log`;
  `graphics-frames.tsv` is under the trial directory, or runtime root without a tag.

Cold here means a fresh app archive and user manifest. It does not purge Apple's
system compiler cache or the package seed manifest. Capture runs are separate:
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
