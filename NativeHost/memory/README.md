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
