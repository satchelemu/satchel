# Guest stop and join boundary

Status: full teardown and Quit to home are implemented. Real Castlevania and TOTK
sessions render, stop, release their guest reservations and restart in one Mac
process. The canonical app enables the boundary by default. Phone execution and
the signed-dylib reload path require the orchestrator's device test.

Round 4 harness: `soak-4`, based on public `main` at `cd6fbf9`, which already
contains the ownership and cancellation fixes. Historical round 2 work was based
on `0856739`. The earlier `soak` changes were renamed in isolated throwaway worktrees before porting. Runtime pin:
`f8a5618d5f505a4c0a0bcac7c413872d3a53c009`, including the canonical build's
29 materialized patches. `overlay.py` applies the two lifetime patches with zero
fuzz, then exact-anchor overlays. Generated copies change, the shared engine does
not. The existing privacy, display, audio-volume, shader-capture and graphics
policy overlays remain composed. No dependency or Ryujinx code was added.

## Contract

One coordinator owns `begin`, stop, join, subsystem reset and `end`. Calls return
POSIX error numbers. There is one session at a time. Begin refuses an existing
session or a conflicting SIGVTALRM handler (SIGUSR1 belongs to the
phone shader translator's NativeAOT runtime). Thread admission closes atomically when
stop begins. `end` refuses until every managed pthread and the reaper have joined.
An unsuccessful stop leaves the session active and rejects a new begin.

`satchel_session_pthread_create` creates joinable guests and records their pthreads
separately from guest handle objects. A dedicated reaper joins normally exiting
threads throughout the session, so closing guest handles cannot leak joinable
pthread resources. A cleanup handler reports impending exit. The reaper waits
for actual pthread exit, including every TLS destructor, before reclaiming owned
mappings. Main gate stacks are registered with that owner. Gate slots are cleared
on exit. Process-lifetime TLS keys and mutexes remain initialized.

`satchel_session_run_guest` saves a native return point before entering a root guest
call. Its callback must have `no_instrument_function`. Every native C/Objective-C
function reachable from the guest is compiled with `-finstrument-functions` and
`-fno-optimize-sibling-calls`. The hooks count native call depth per guest thread.
A stop at the exit of the outermost native call returns to the saved native frame,
after normal C cleanup and mutex release. Nested guest callbacks retain the
native depth of their callers. Unguarded callbacks must return normally. A blocked
native call can cause a timeout, never forced unwinding through its locks.

`satchel_session_call_guest` adds an explicit scoped boundary. Its uninstrumented
callback may own guest locks, but no native resource that needs cleanup. On stop,
it returns zero to its native caller, which releases its locks and buffers. The
interrupted guest state is terminal. No SDK operation is retried or resumed.
Scopes nest: the innermost eligible scope receives cancellation first. The SVC
gate recognizes that scope so a guest kernel wait inside it can also be released.

For a pure guest loop, join pulses SIGVTALRM. Its handler can escape only at depth
the current scope depth and only when the interrupted PC is in an explicitly registered guest code
range. It performs no module lookup, allocation, logging or mutex operation.
The stop atomic is required to be lock-free. SIGVTALRM is blocked across pthread
creation and inherited blocked by the child until its TLS is initialized.
Ranges are immutable after the first root guest call. All offline NSO/NRO
executable ranges are registered before entry, including modules activated later.
Loading executable content outside that prepared inventory remains unsupported. Registering writable guest data as code is not permitted by the caller
contract.

The kernel overlay closes CreateThread admission, wakes pre-StartThread parking,
releases pause waits, cancels root kernel waits and makes root timed sleeps and
handoffs stop-aware. WaitSynchronization must propagate cancellation instead of
immediately rejoining its wait. Scoped HLE guest callbacks can be cancelled; unguarded ones cannot.
The child gate stack is established below the saved termination frame, preventing
SVC dispatch from overwriting that frame. Background clock reads also have a
stop predicate and wake operation. The optional hitch sampler is not started for
managed sessions because it has no session owner.

After join, explicit reset operations clear kernel handles/events/thread records,
clock epochs, profile state, filesystem mounts, open files and directories,
RomFS image indexes and cached paths. They refuse before quiescence. Their caller
must first stop every other user of those subsystems. They are not a host-wide
shutdown certificate. Kernel scheduling overrides are reread on the next session.
The crash-stack destructor retains a nonzero size in its disable request:
this Mac's Darwin rejects the zero-sized form with `ENOMEM` even with SS_DISABLE.

## Save ordering

Native calls in progress finish before join. The probe deliberately blocks a
guest Commit on the actual filesystem path lock, requests stop, verifies a
join timeout, releases the lock, then verifies both committed files after join
and reset. The boundary cannot interrupt the clone/swap halfway through.

Reset never forces a new Commit. Only the guest knows when its logical save is
complete. The last successful atomic commit is retained, and remount discards
unfinished work. The probe stops between two updates of a second transaction and
checks that both files from the prior commit return on the next session. Quit
cannot create a game save. Save in game before quitting.

Cloud save sync (Settings > Storage, the pinned engine's `save_sync.c`) is session
scoped. `SessionBridge.m` sets `DM_SAVE_SYNC` to the chosen folder's path, or 0, after
the manifest environment; the coordinator still refuses any other non-zero value,
including the Mac's `icloud`. `save_sync_overlay.py` makes the sender joinable and adds
`satchel_save_sync_session_stop`, the `save-sync-stop` stage right after `guest-join`:
no Commit can arrive any more, and the file system state a send clones from is still
intact. The stage flushes what was committed (at most 20 seconds), stops and joins the
sender and clears the once-per-process mount state, so the next session mounts again.
A send still running at the deadline fails the stage with ETIMEDOUT and Quit can be
retried; a save that did not reach the cloud goes out at the next start.
`check_save_sync.py` covers off, send at stop, a same-process remount with another
title, an in-step remount and a pull with the engine's own backup, over a plain folder.

## Validation

Run from the worktree, on an ARM64 Mac with the canonical engine provisioned:

```zsh
nice -n 10 python3 -B NativeHost/session/probe.py
```

`SESSION_OUT` can select another external output directory. The driver waits for
other heavy builds, checks the 35 GiB reserve and compiles serially. It runs the
existing 98-check transaction test against the patched filesystem source. Then
it runs expected timeout cases, nested scoped cleanup and actual SVC cancellation,
an in-flight Commit case, one warmup, five same-title restarts, then three more
cycles completing an A/B/A/B switch sequence. Each cycle creates a main guest and ten
kernel threads, using native synthetic assembly and the unmodified engine
`entry.S`. The cases include a pure loop, address/condition/mutex/event waits,
a long sleep, pre-start parking, a frozen clock, a signal-paused thread on an
alternate stack, an SVC-paused thread and normal ExitThread with CloseHandle.

Assertions require zero live managed pthreads/mappings, all 1,024 gate slots
returned, stable process thread and descriptor counts, and RSS less than 2 MiB
above the warmed baseline. Every cycle touches the actual kernel heap and physical aliases, as well as
native media buffers.
A same-title relaunch reads the prior commit; alternating titles verifies mount
and profile replacement. The actual 5 GiB and 4.5 GiB guest reservations alternate between the two
synthetic profiles. Mach VM-region queries verify that each old reservation is
unmapped, and the next heap starts zeroed. A live dirty-tracked graphics borrower
makes reset return EBUSY. No actual dylib, hardware audio device, real game or
phone is exercised by this probe. The actual
AudioOut silent sink, watchdog, render helper, decoder queue and worker are now
exercised with synthetic SDK functions and a blocking codec adapter.

The canonical single app enables the boundary and fingerprints all overlay inputs:

```zsh
SATCHEL_OUT="$HOME/SatchelBuild/session/restart-build" nice -n 10 python3 -B NativeApp/build.py
```

`SessionBridge` prepares on a background task, starts a managed boot pthread when
the UIKit surface appears, and stops on a serial coordinator queue. The coordinator
closes admission, joins guests and native producers, retires typed resources,
unmaps modules and the arena, then ends the boundary. Only then does the bridge
`dlclose`, restore cwd/stderr/DM variables/TZ, release player settings, notify
`SatchelNativeSessionDidEnd` and return home. Both menu implementations use this path.
`PreparedLaunch.current` clears on that notification. Library retains the paused
session; choosing it again reattaches it, and choosing another title first stops it.
A failed stage keeps its ownership and prevents a new prepare. Quit can retry.

## Typed retirement

`SessionGraphicsRetire.inc` releases submitted and unsubmitted ownership
separately: completion records own transferred post operations; scratch records
own only work never submitted. Resource maps use their actual CF or heap
releasers. ARC assignments release textures, pipelines, heaps, residency sets,
program/shader caches, deferred objects and queue state. A persistent weak
borrower table refuses completion until every no-copy pool buffer is gone,
including across multiple retries. Dirty tracking closes fault-handler admission
before retiring descriptor blocks and sparse aliases. NVN side tables, command
segments and capture copies are freed before the guest arena is unmapped.

`SessionGraphicsState.inc` explicitly lists the pinned backend's reviewed POD and
strong globals. `state_overlay.py` registers function-local scalar caches and
ARC cache owners before first mutation. Byte snapshots never include locks, TLS,
once guards or Objective-C references. ARC objects use typed nil stores.
Process utility objects initialized by dispatch_once remain process owners.
Monotonic pool/program generations remain monotonic. Phone shader runtime caches
and on-phone compiler owners reset after all graphics jobs join.

`services_overlay.py` owns generic object-map values by destructor and rejects an
unknown owner. It releases font mappings, codec objects and Unreal malloc zones;
resets SF/OE/network/DLC/friend/keyboard state, SDK pointers and environment caches;
and joins notification and vsync workers. The cached CreateSystemEvent SDK address
must reset when switching SDK images. Bootstrap argument storage also resets.

`SessionInputGate` closes controller/motion/touch publication and waits for active
publishers before clearing HID events. Native UIKit views and hardware observers
remain process owners. The frontend overlay blocks audio interruption callbacks
through teardown, resets audio activation and progress UI, and boots a new game
when the retained controller reappears. Menu screenshot generations discard old
callbacks. Memory and CPU samplers are owned, awakened, joined and closed before
environment restoration. Arbitrary diagnostic probes, cheat directories, FD
logging and enabled cloud sync are refused by coordinator preflight because
their independent workers have no reusable session contract.

The installed Metal 4 descriptor over-releases a custom feedback queue when the
descriptor is destroyed. A standalone five-cycle fixture reproduces this without
a game. The lifetime overlay uses the documented nil feedbackQueue default,
which lets Metal own its queue. `queue_probe.m` exercises that supported path.

Ryujinx 1.3.3 is the order reference: KProcess marks Exiting, prepares/unpauses
threads, tears down handles and signals exit. KThread releases waits and stops
its CPU context at the post-syscall boundary. Horizon.Dispose terminates the
application before service processes; ServerBase joins its thread before closing
service sessions. Switch disposes system, audio, filesystem and memory. Its CPU
context interruption is provided by the emulator. Native ARM64 execution needs
the distinct boundary implemented here.

## Media stop protocol

The coordinator closes guest admission with `satchel_session_request_stop`, then calls
kernel, clock, movie, audren and audio request-stop operations. Request functions
do not wait on a mutex held by a guest callback. Movie timed waits also cover a
broadcast racing entry into the wait. Keep guest memory mapped throughout.

The renderer catches cancellation around SDK execute, command generation, guest
mutex acquisition, processing, open, update and close calls. It releases its
native render mutex and closes its PCM output normally. Work buffers allocated
before an interrupted SDK open stay owned until all guests join. Caches of SDK
addresses, command layout and resampler history are invalidated for the next
session. Cache initialization and rendering share the render mutex.

Movie SDK accessors and event signals have typed scoped calls. The deferred event
helper leaves its native condition wait and releases its queue lock on stop.
Decoder objects are registered until destruction. Submitted codec jobs must finish
and acknowledge completion before their packet can be released. After guest join,
`satchel_movie_session_reset` submits QUIT, joins each decoder and releases its buffers
and codecs. It clears the event queue and guest function table.

SDK-created helper ThreadType and entry records have a separate native registry.
A cancelled SDK creation or WaitThread keeps that record. After actual pthread
join, `satchel_guest_helpers_session_reset` frees the native records. It does not call
the dead guest allocator. Guest stack allocations belong to the guest
address-space owner. Its reset now exists, but the app cannot call it until every
other borrower has retired.

`satchel_audio_session_reset` joins the watchdog, closes every registered output,
joins its notifier and silent sink, disposes AudioUnits and closes diagnostic
output files. Driver errors retain the output and buffers and return EIO. The
probe injects a stop error and verifies retention before synthetic recovery.
Its caller must first stop front-end lifecycle callbacks. Call
media resets before filesystem/kernel/profile reset and before unmapping guest
memory. A successful media reset is not a graphics shutdown certificate.

The probe tests the real helper registry with synthetic SDK allocator/thread
adapters. Every cycle blocks a native decoder, requires join to time out without
reclaiming its input, then releases it and requires complete teardown. Alternating
movie deferred-event cases cover an empty/future wait and an event signal spinning
in guest code while holding the native queue lock. Two distinct synthetic code
entries and vtables verify that switching replaces cached SDK addresses. The
probe uses an existing Mac libvpx archive at `<host-gen>/vpx/libvpx.a`, where `<host-gen>`
is `SATCHEL_MAC_HOST_GEN` (default `<build inputs>/host-gen`; the probe stops with that
variable named when the folder is missing); no dependency is added.

## Native producers and guest memory

`SessionNativeWorkers.c` owns joinable native workers and a reaper. Completion
includes actual pthread exit and TLS destructors. Closing admission wakes timed
waits. Timeout retains records and refuses reset. A fixture blocks a TLS
destructor to prove that a cleanup callback alone is insufficient.

`owners_overlay.py` adds explicit lifetimes for DM_CTL, scripted input, recording,
and memory-map diagnostics. Long holds, drags, future-frame waits and diagnostic
sleeps become interruptible. HID reset closes recording, frees scripts, clears
style-event guest pointers, buttons, motion and sampling. Touch reset separately
clears fingers, short-tap latches and gesture history. Input admission closes and active frontend publishers drain before reset.
Hardware controllers and their observers remain process owners.

The kernel arena no longer uses a one-shot initializer. After all borrowers have
retired, `satchel_memory_session_reset` unmaps the complete reservation and releases
alias bitmaps and region tables. Dirty tracking is an additional refusal check,
not an inventory of every GPU borrower. Profile-dependent program IDs are reread.
The loader creates process TLS keys once, registers only executable module pieces
before root guest entry, and supplies a reset for its mappings and symbol tables.
The app and Mac probe use the managed bootstrap. Loader reset is exercised with
real Mac mappings. iOS vm-remapped signed dylib unload/reload awaits device testing.

## Separate graphics lifetime overlay

`graphics_overlay.py`, `SessionGraphicsAsync.h` and `SessionGraphicsStop.inc`
own lifetime changes separately from display and pipeline policy. They transform
`nvn_mtl.m` and its `nvn_rec.h` include in generated output only. The include uses
its generated absolute path so a later graphics policy overlay cannot silently
restore the upstream detached workers. Both policy/lifetime application orders
have been checked against the graphics worker's current pipeline overlay.

Present and display-link threads are owned, stopped and joined. Uniform-copy and
encoding consumers stay alive while outstanding records and presentation work
finish, then drain and join. Dispatch work is counted when scheduled, including
nested jobs. Metal completion, feedback and shared-event callbacks remain counted
until their bodies return. A dedicated owned drain worker lets a timeout retain
all state while driver waits finish. Retry joins the same worker.

`build_graphics_probe.py` builds a real-Metal fixture after `build_mac_probe.py`.
It checks five producer restarts, held completion callback timeout and retry,
nested delayed dispatch, actual uniform copies and encoding records, presentation
and display-link joins. It invokes the production typed retirement and holds an extra no-copy buffer
reference across two refused retirements before releasing it and retrying.

The full Mac host can be built and exercised with private existing inputs:

```zsh
nice -n 10 python3 -B NativeHost/session/build_mac_probe.py
nice -n 10 python3 -B NativeHost/session/build_graphics_probe.py
nice -n 10 python3 -B NativeHost/session/run_mac_restart.py
```

These scripts need the provisioned canonical engine and existing Mac generated
host headers/libvpx archive (`SATCHEL_MAC_HOST_GEN`, generated from the pinned engine). The builder creates its own privacy overlay and
composes the current pipeline policy. The runner uses only the existing private
Castlevania and TOTK packages, fresh private runtime/save directories and public
numeric metrics. Other host/guest output is discarded at the pipe.

The default sequence is Castlevania, TOTK, five consecutive Castlevania sessions,
TOTK, Castlevania, all in one process. Every session renders for 30 seconds.
Assertions require zero managed guests/mappings, successful arena unmap and
settled RSS within 32 MiB of the warm baseline after both titles have run.
Thread and FD counts are also bounded. Cold-process RSS is not the warm Metal
baseline. Synthetic saves, rather than gameplay progress, establish transactional
readback across cycles. The report records the exact evidence and phone steps.

## Long-session soaks

`soak.py` runs the real Mac host with only the three authorized existing private
packages. It uses private save copies, appends button/stick commands to `DM_CTL`,
and requests a screenshot every minute. The first 90 seconds send confirmations;
subsequent commands exercise movement, camera, jump and action. Zelda pairs
opposite movement before camera turns to limit route drift. Do not overlap an
independent movement script with that loop. Screenshots need visual review before
calling a run gameplay; a frame counter can keep advancing on a Game Over screen. Audio uses the null sink.

Build through the resource guard with
`SATCHEL_OUT=~/SatchelBuild/session/round4 NativeApp/build.py --session-mac-host --unsigned`
(using `python3 -B` to invoke the driver). Set
`SESSION_MAC_OUT=~/SatchelBuild/session/round4/session-mac` for the runner. `mac-stop-probe dispatch-check` is a no-game
regression: five state-reset/lookup cycles must preserve all 537 constructor-built
NVN dispatch entries. Repeat with `DM_NVN_PROF=1` to cover profiling wrappers.
The initializer guard is process-owned. Resetting it drops the large-stack and
empty-function wrappers on the next title.

`check_crash_timeout.py` reproduces a crash alarm delivered to a non-owner thread
and verifies bounded exit. `check_state_caches.py` exercises
scalar reset and ARC retirement when a function's first declaration is static.
`check_prebuild_stop.py` runs the actual pipeline worker loop and completion
prefix with synthetic pairs: cancellation must admit no further pairs, leave no
completed stamp, and release a guest warmup wait. In-flight compiler calls still
return normally, and the real graphics join deadline remains unchanged.

Run these commands from the worktree, each through `NativePrepare/guard.py` with
`ANYGAME_OUT=~/SatchelBuild/session/soak-guard`:

```sh
python3 -B NativeHost/session/soak.py --seconds 1800 --titles Castlevania Odyssey TOTK --label long --require-ready
python3 -B NativeHost/session/soak.py --seconds 300 --titles Castlevania Odyssey TOTK Castlevania Odyssey TOTK Castlevania Odyssey TOTK Castlevania Odyssey TOTK --label switch --fixed-slots
```

Both use one process for their whole title sequence and the production session
stop/prepare path between titles. Five-second samples include RSS, task footprint,
compressed bytes, malloc usage, Metal allocated bytes, file descriptors, threads,
guest mappings and Mach port names. The runner discards raw guest output at the
pipe and retains numeric measurements plus a bounded symbolic error allowlist.
It fails on missing teardown, fatal diagnostics, save commit errors, changed
source-save hashes, absent rendered frames, or a frame-progress gap of 60 seconds.
A separate 90-second telemetry watchdog bounds a host that hangs in a crash handler.
Each run holds an idle-sleep assertion only for its host process. Wall time and
active monotonic time are both recorded; a five-second discrepancy rejects
uninterrupted-soak acceptance. Lid closure can still suspend the Mac.

The long run first emits `warming` samples. Inspect its private screenshots and
use DM_CTL to reach controllable gameplay, then create `TITLE/play.ready` inside
the printed run folder. That starts the full 1,800-second measured interval,
marked `play-start`. Failure to reach gameplay within 900 seconds fails the run.
Do not create the marker for a menu, loading screen or black picture. The switch
run anchors stop requests to the process's five-minute boundaries. Each next slot
includes the preceding teardown and its own loading. The final teardown follows
the 60-minute stop request. Private save seeds come from
existing allowed backups; `--seed <previous-run>` copies committed saves from a
previous private run for reload checks. Original save manifests must stay equal.

The Mac-only probe compiles missing Metal libraries into
`~/SatchelBuild/session/soak-mac/libraries`. It uses the pinned MIT shader variant
generators and the installed Mac compiler, one compiler at a time. Existing source
caches remain read-only. Cold compilation is preparation, not play-soak evidence.
Queued helper work cancels when the session stops. An active compiler finishes
and is reaped before its helper acknowledges cancellation. Cancelled demand loads
propagate as cancellation through their callers; missing shaders during gameplay
still fail acceptance. `check_mac_cache_stop.py` verifies queued cancellation and
that an active compiler child cannot outlive its helper.
The canonical iPhone app does not contain this NSTask-based test helper. Mac audio
uses the null sink, so real audio playback and phone thermal behavior remain device
checks. Screenshot review is required in addition to numeric frame progress.
The probe also leaves recorded warmup and background bulk prebuild enabled. This
stresses their cancellation and resource lifetime. Ordinary app launch disables
those features in `PipelineSettings.m` unless explicitly requested, so the soak's
startup time and prebuild memory curve are not the default phone launch policy.

`soak_curves.py <run-folder>` writes SVG curves and JSON trends from `metrics.csv`.
Slopes omit the first five minutes of each session. They are measurements, not
leak acceptance by themselves. Compare repeated post-stop baselines, identify
cache warmup, and inspect scene changes before attributing growth to a leak.
Metal allocated bytes do not enumerate every driver resource. Game-save hashes
are evidence of preservation, not a substitute for successfully loading the save
in the game and passing the existing transactional filesystem checks.

Pending and amplified pipeline arrays contain strong Objective-C fields. Their
typed retirement clears those fields before restoring scalar keys and counters.
`check_pipeline_retire.py` uses the pinned struct declarations and production
retirement helper to verify release across five resets, and reproduces the leak
when only the old byte restoration runs. Its second part runs the large pool table
through the engine's own removal (which leaves a stale copy in the vacated slot) and
the retirement under AddressSanitizer: each chunk table is freed once. With
`--baseline-ref <rev>` it also reproduces the double free of an older revision.

The engine frees its `game_env` copy itself when the profile's `game_env` is empty;
the services overlay then forgets its own pointer to that copy, so the os resources
reset frees it once (the quit abort seen in Castlevania on 2026-11-05).
`check_second_launch.py` cuts the generated getenv table and reset from `hle_os.c`
and runs a stop after `getenv` with an empty `game_env` and later sessions under
AddressSanitizer; `--baseline-ref <rev>` reproduces the abort.

Dirty-range growth retains replaced state and generation arrays for fault readers.
The session reset frees those retired arrays only after guest quiescence, closed
fault admission and zero active readers. `check_dirty_retire.py` exercises the
pinned tracker through five growth/reset cycles, verifies both reclamation guards,
and reproduces the old lost allocations.

Use `--leak-check` for an allocation scan after the final settled sample. The Mac
probe holds for 45 seconds after that sample; the runner gives the installed
`leaks --noContent --nostacks --groupByType --quiet` command 30 seconds. It
retains the numeric summary and, on failure, allocation type/count metadata.
Allocation contents, addresses and stacks are not retained.
Missing summaries, timeouts, nonzero exits or detected leaks fail that check.
The hold follows the measured play interval and does not alter switch deadlines.


## Round 2 ownership and stall diagnostics

Set `SESSION_MAC_OUT` for both the builder and runner to keep new runs separate.
`--allocation-stacks` enables in-memory malloc stack logging and retains only
symbolic host/system allocation stacks with addresses removed. It supplements
`--leak-check`; no allocation category is excluded. A frame counter unchanged
for 20 seconds triggers a bounded host stack sample on a separate reader thread.
The 60-second stall failure threshold and five-minute switch deadlines are unchanged.
Private compiler telemetry distinguishes cache-lock, heavy-build and compiler waits.
RED diagnostic categories come from engine format literals, without runtime arguments.

Round 2 reproduced a Zelda render stall at `lib_from_file`, waiting for the private
Mac shader helper. Host samples show the shader-load call chain; helper samples
show the cache-lock owner sleeping while other helpers wait in `flock` and an
unrelated shader harness is active. The source path leads to the shared heavy-build
wait. This narrows a new reproduction, not the unsampled historical 115-second gap.
Do not bypass the shared wait or remove the stalled interval from acceptance.
The phone does not use this private Mac helper.

`check_specialization_lifetime.py` runs the real two-lane pipeline helper with
synthetic Metal shaders. The original helper reproduces an AGX dynamic-library
leak after 100 cycles while all public weak owners are nil. Serializing the Metal
specialization call inside the worker passes the same zero-leak scan. Other
pipeline compilation still uses two lanes. This Mac evidence does not establish
phone performance or resolve the previous run's autorelease-pool roots.

`--capture-shaders` writes executed shader inputs to each title's private capture
folder, using the app's existing capture-lifetime overlays. Nothing is captured
unless requested. `--shader-inputs` accepts a private JSON object mapping an
authorized title to `tables` and `cache` folders under the session output root.
This permits a captured offline translation to be checked without changing the
source package or its caches. Captures and shader outputs never belong in git or logs.

Deferred shader keys must retain their control bytes when the code area is not
mapped at registration. The caller can release those controls before `prog_key`
runs. The capture-lifetime overlay now keeps that snapshot even when capture and
phone translation are disabled, and releases it on replacement or finalization.
`check_deferred_shader_key.py` exercises the actual registration and hash code
with synthetic controls overwritten immediately after registration. It compares
the original and modified overlays across five replacement cycles.

The corrected round 2 fixed-slot loop completed all 12 stops in 60 minutes, with
zero detected leaks, shader/fatal/save errors and changed source-save hashes.
All 12 visits have visual movement/action checks. The historical five pool roots
did not reproduce, so their old ownership is not inferred from this pass. Settled
footprint varies with driver/cache retention and is recorded separately from the
allocation scan. One separate long Zelda run failed guest join after five seconds;
a deliberately blocked private-cache fixture cancelled cleanly and did not reproduce
that timeout. Stop-failure stack sampling remains enabled for the long-run retry.

Castlevania's reproduced late intro failure contained 16 missing map entries and
one missing-vertex diagnostic. Executed capture confirms those 16 shaders were
absent from the original 198-entry offline map/cache. The first expanded private pack
contains 214 translations and passes the same transition with capture disabled.
A later indoor area exposed 40 more absent entries. Executed captures validate
as a 254-entry union, and the pinned translator compiles all 254 without failure.
This repairs observed route coverage in the private Mac input; it does not establish
whole-game coverage or a phone result.
The deferred-key ownership regression is a separate defect, not the cause assigned
to these missing translations.

The final paired-control Zelda interval completed 1800.019 uninterrupted seconds,
with all visual checks, a successful stop and zero detected leaks. It averaged
25.214 FPS. The combined run still fails because system sleep interrupted its
Castlevania interval. Repeated Castlevania attempts were interrupted by system
sleep, so acceptance was still open at that point. The awake round 3 run below
passed. Zelda live footprint peaked at 14.3 GiB and Metal allocation at 12.2 GiB before retiring on stop. A zero-leak
result does not establish a bounded live cache or a fit within phone memory.

A subsequent capture-disabled sewers entry produced two missing-map diagnostics
and one missing-vertex diagnostic. The attempt was aborted before its play gate.
A short capture replay passed, validated 182 shaders already in the 254-entry pack,
and found no additional entries. That isolated failure remains unassigned; neither
a new coverage gap nor a capture-dependent defect is inferred from one comparison.

## Round 3 baseline comparison

The pre-merge main baseline (`da5830f`) and fixed host reproduced the same street-to-indoor entry
failure with the retained private Castlevania inputs: twelve missing-map diagnostics
and one missing-vertex diagnostic. Both runs reach the indoor scene. This establishes
a pre-existing entry failure, with no increase in diagnostic count or categories on
the matched route. The older three-report attempt did not retain shader identities,
so an exact identity match to that attempt cannot be established.

Stop-failure sampling also records owned helper processes and their host/system
stacks. Process arguments, paths, guest frames and addresses are excluded. This
supplements the host stack without retrying the failed stop or changing its deadline.

The awake Castlevania rerun passes the unchanged gated acceptance: 1801.115 measured
seconds, 0.003 seconds of clock drift, zero shader/fatal/save errors, unchanged source
saves, full guest-resource retirement and zero detected leaks. Guest join takes
1.377 seconds. The interval averages 27.219 FPS, with a maximum frame-progress gap
of 5.844 seconds, on the shared Mac. Other workers and diagnostic hosts overlapped
this run, so these frame rates are not an isolated performance comparison.

The matched entry replay still fails shader acceptance in both hosts. The pre-merge baseline reports
399 leaked allocations (307248 bytes); the fixed host reports zero. Both retire all
tracked guest resources and preserve source saves. This separates a mergeable
ownership improvement from the pre-existing entry diagnostic follow-up.

A short Zelda diagnostic reproduces the private helper stall for 284.971 seconds,
but guest join succeeds in 0.458 seconds, both helpers cancel, and the leak scan
reports zero. Its frame-progress acceptance fails. It does not prove the earlier
five-second guest-join timeout fixed.

Two longer Zelda attempts were killed by the unchanged disk-reserve guard during
warmup, at 341.809 and 100.609 seconds. Neither opened its gameplay gate or attempted
stop. The second reused retained libraries through read-only per-file symlinks.
These are external resource failures, not guest-join failures or successful soaks.
They do not prevent merging the independently validated ownership fixes.

The specialization, deferred-key and dirty-retirement fixtures verify the current
implementation by default. To reproduce the old defect too, pass `--baseline-ref`
with an unfixed revision. The cache-state fixture uses the same convention. Avoid
using a moving `HEAD` as an implicit unfixed baseline after the fix has been merged.


The round 4 harness also samples a launch that produces no numeric telemetry for
90 seconds before terminating it. `watchdog-host-stacks.json` and
`watchdog-helper-stacks.json` use the same symbol-only filter as failed stops.
A sampling timeout is recorded and does not discard the other process sample.
The watchdog still fails the run; it is not a retry or an extended launch allowance.
`check_soak_diagnostics.py` covers the filters, owned-descendant selection and
sample-timeout path. `check_build_wait.py` covers conservative build admission.

`check_soak_watchdog.py` also runs a silent synthetic host through the actual
90-second watchdog. It requires a failed run, process termination, preserved
synthetic saves and both stack reports. It reads no game package or user save.


## Round 4 retained-cache Zelda

The new host built from main `cd6fbf9` completes 1800.006 uninterrupted gameplay
seconds with the retained private libraries. Visual checks cover movement and
jumping on Great Sky Island and the five-minute checkpoints through the final
minute, without a menu, death or loading interruption. The unchanged acceptance
passes: zero shader/fatal/save errors, original saves unchanged, zero detected
leaks and full guest-resource retirement. Guest join takes 0.521 seconds and stop
to settled takes 5.376 seconds. The interval averages 27.387 FPS, with a maximum
frame-progress gap of 5.077 seconds, on the shared Mac.

Five-second telemetry peaks are 11.605 GiB footprint and 1.360 GiB RSS. The
higher-frequency guard observes 1.528 GiB peak RSS and at least 50.942 GiB free
disk space. These are distinct counters; the guard caps RSS, while phone memory
acceptance still needs device measurement.
The run starts above 45 GiB free and the reserve guard does not fire. The earlier
guest-join timeout does not reproduce in this retained-cache run. No cause fix
is inferred from that absence. The live-memory and phone checks remain open.


## Round 4 same-process launch matrix

The same new host passes nine 120-second visits, ordered Castlevania, Castlevania,
Odyssey, Odyssey, TOTK, TOTK, Castlevania, Odyssey, TOTK. This covers three immediate
same-title relaunches and five cross-title transitions in one process. All nine
reach visible gameplay, stop and retire every tracked guest thread and mapping.
The final leak scan reports zero. Shader/fatal/save errors remain zero and original
save hashes are unchanged. Guest join ranges from 0.002 to 0.037 seconds; stop to
settled ranges from 2.478 to 7.436 seconds. The maximum frame-progress gap is
15.040 seconds during the first Odyssey visit, below the unchanged 60-second gate.

The matrix does not reproduce the post-session launch hang on main `cd6fbf9`'s
native code. The six merged core files and seven fixtures also remain unchanged
on the subsequently observed main `619fb28`. This is bounded Mac evidence, not a
universal absence proof or a signed-dylib/phone result. The visits include startup;
the late-visit screenshot for each title confirms gameplay separately. No speculative
production fix or weakened deadline is included in this harness split.
