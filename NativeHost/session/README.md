# Guest stop and join boundary

Status: full teardown and Quit to home are implemented. Real Castlevania and TOTK
sessions render, stop, release their guest reservations and restart in one Mac
process. The canonical app enables the boundary by default. Phone execution and
the signed-dylib reload path require the orchestrator's device test.

Current work: `quit-to-home`, fast-forwarded to `one-app` at `edc4df9`, including
DLC, graphics policy, any-game and navigation changes. Runtime pin:
`bbc3b73784751d2a706932dc69fd67627e3445d4`, including the canonical build's
34 materialized patches. `overlay.py` applies the two lifetime patches with zero
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
probe uses the existing Mac libvpx archive at
`~/SatchelBuild/zelda/host-gen/vpx/libvpx.a`; no dependency is added.

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
host headers/libvpx archive. The builder creates its own privacy overlay and
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
