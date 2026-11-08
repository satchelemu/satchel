Scene memory diagnostics

The memory overlay runs after display, session and graphics overlays. It never edits
shader cache files. SATCHEL_SCENE_MEMORY names a private TSV output. Samples run on
the submission thread every 60 guest frames, inside an autorelease pool. Counts are
resident Metal allocation objects, with heap-backed textures counted as heaps.
Serial counts also include programs and packaged texture wrappers. Pipeline counts
are cache entries, not a claim about Metal's internal compiler allocations.
Texture and buffer byte fields sum per-object allocatedSize values. Views can share
backing storage and sparse textures can report large virtual sizes, so these sums
are not unique physical bytes. Use metal_bytes for the device allocation total.
The dead_pools field is the weak table's raw count, which can include zeroed slots.
Use dead_pool_bytes, summed over live objects, to identify retained pool storage.
The Mac driver additionally counts retained runtime shader revisions, which sit
outside the active shader map. This extra instrumentation is not part of the app.

Idle staging buffers of any size have a combined 64 MiB retention budget after 600
submissions of inactivity. Buffers above 32 MiB retain the existing idle expiration.
In-flight and recently used buffers are exempt. This bounds idle reuse capacity,
not the game's active working set or buffers retained by another owner.

Use NativePrepare/guard.py with ANYGAME_OUT and SATCHEL_OUT below the worker's own
output directory. Build the reference with memory/build_mac.py --baseline and the
candidate without that switch. Run memory/run_mac.py with identical frame scripts
and save snapshots. Mac results do not establish phone behavior.
Use --shader-mode runtime for the format-2 app path, with private translation and
capture directories and the app's hybrid pipeline policy. The default pack mode
is a retained-library reference control. The summarizer rejects mixed shader modes.
The runner requires every requested capture and a memory sample within 60 frames
of the final checkpoint. Run memory/summarize.py on the shared output directory
after both variants of all three allowed games complete. It checks route equality
and writes measurements.json and measurements.md. Review the captured scenes before
calling the checkpoints matched scene transitions; identical frame scripts alone
do not guarantee that asynchronous loading reached the same scene.
After inspecting a route's captures, record sceneValidated=true and the reviewed
scene labels in its result.json. The summarizer refuses unreviewed or rejected
routes. The bundled Castlevania input recording reuses the display worker's
validated opening route, including touch confirmation on the language screen.
Build compare.swift with the installed Swift compiler under the same guard, then
pass the shared output directory to the executable. It writes decoded sRGB pixel
differences to capture-comparison.json. It does not accept a tolerance or replace
visual inspection of different animation frames.

Finalized texture descriptors can still be replayed while their guest pool is live.
The existing 512-entry ghost table preserves those textures. The overlay records
which pool owns each ghost and retires matching ghosts on pool finalization, through
the existing deferred GPU fence path. The metadata shares the same 512-entry bound
and is freed at session teardown. It does not evict live descriptors or discard
GPU-only texture contents based only on their age.

Long-play soaks (`soak_mac.py`)

`soak_mac.py GAME VARIANT --seconds N --label L [--leak-check] [--route]` runs the
scene harness built by build_mac.py on an existing package, read in place, for N wall
seconds. It uses the app's current shader path (the phone translator at runtime, an
empty seed library) and PipelineSettings.m's default exact pipeline policy; the
translation and pipeline caches persist under `SATCHEL_OUT/caches/<name>/<game>` like
the app's runtime folder. By default it starts like session/soak.py: the title's
private Mac save backup, 90 seconds of confirmations from the first frame, then that
tool's balanced movement loop (MOVEMENT), one entry every six seconds. `--route`
plays run_mac.py's frame-scripted route instead, so its capture checkpoints compare
against another build. Every five minutes of the loop it saves a screenshot for
visual review. `--leak-check` scans the live host with the installed `leaks` tool
before stopping and keeps only counts and type names; `--allocation-stacks` adds
in-memory stack logging and keeps only symbolic host and system frames (it grows memory
itself, so use it in a separate run from growth measurements). The harness does not
carry the session overlays: the engine's replaced dirty-page arrays, which the app
keeps on a retired list until Quit, show up here as 16-byte leaks from
`nvn_dirty_add_range`, a fixed number per title.

Each run writes memory.tsv (scene memory, every 60 frames), census.tsv, vm.tsv (the
engine's DM_MEMSTAT per-class accounting, every second) and progress.tsv (resident
size and frame, every five seconds). census.tsv is a Mac-only column per file-scope
host table of the composed renderer: every `nvn_hmap` (entries and capacity) and
every Foundation collection (count). census.py generates it from the composed source,
so a new cache cannot be left out; the app does not contain it. The long run's
executable is named `memory-soak-host`, which the shared build exclusion does not
wait for, so an hour of soak does not hold other lanes' builds.

`soak_summary.py RUN...` classifies every counter over the movement loop: "steady"
when it grows in at least three quarters of the second half's ten-minute windows and
its second-half slope exceeds 8 MiB (bytes) or six entries per hour and its final value
clears the first half's peak (a counter that dips and recovers is not steady);
otherwise "step" for net growth, "falling" or "flat". Given a folder copied from the
phone (scene-memory.tsv and the DM_MEMSTAT file), it starts the loop after `--skip`
seconds and maps frames to seconds at the run's mean frame rate. It writes soak-summary.json and soak-summary.md
in the run folder. A step needs the screenshots to tell a new area from repeated
content; a steady counter on repeated content is a leak candidate.

The scene harness build now generates the profile header, host shaders and dav1d from
the pinned engine, as build-pipeline-mac.py does: the older reference gen folder lacks
the f8a5618d profile fields (DM_GAME_LAUNCH_PARAM and the module build list). Only its
prebuilt libvpx is reused.

Idle title-screen attribution (`soak_mac.py --no-input`)

`--no-input` runs without confirmations or movement, for a scene the phone measured idle; the
summary starts 300 seconds after the first frame. `--app-profile` turns on what the app's
`-SatchelGraphicsProfile YES` does; `--window` presents through the display link and `--audio`
uses the real, muted output path, both as the app does. `--snapshot-at S,S` records `heap`
totals by class and size at those run seconds (counts and type names only), and
`heap_diff.py A B` lists what changed, so reachable growth shows as well as leaks.
`build_mac.py --services` adds the app's service overlay to the harness. The 2026-11-08 Zelda
title result is in `Validation/title-malloc-2026-11-08.md`: the phone's steady 23 MiB per
hour of malloc growth did not reproduce on the Mac, so its owner is not attributed.
