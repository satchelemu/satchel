# Goal progress checks

Goal: run Castlevania directly from the original XCI imported on the iPhone,
with visible gameplay and controls, and no per-game computer preparation.

## 2026-11-01T04:02:09.423419+00:00: strategy correction

**Decision: change the work cadence and review the architecture.** The recent
CPU fixes are relevant and advanced execution, but repeated instruction-by-
instruction development and extensive fixture expansion have not established
game boot or rendering. Do not present those counts as proximity to completion.

Latest device evidence is Release79: 67,546,090 interpreted instructions,
63 completed IPC requests, 0 guest storage reads,
0 actual VI attempts, 0 actual Window attempts,
and 0 copied game frames. The current stop is UCVTF
W/X-to-D coverage; static lookahead also identifies scalar FDIV D. The latter is
not yet an observed execution boundary. There is no visible game or controls proof.
Evidence: /tmp/satchel-vixl-probe/isb-staging/phone79-validation.json and
/tmp/satchel-runtime-castlevania-79.json.

The graphics audit identifies larger unfinished paths: real captured shader
binding, ordinary shader/resource operations, sampled/compressed/depth/mipmapped
textures, and broader draw/state support. Current synthetic raster success does
not cover those requirements. Presentation plumbing is useful but is not the
main remaining renderer implementation.

Next actions:
1. Finish and preserve the already-running UCVTF and FDIV validation jobs; avoid
   another isolated phone test for each opcode.
2. Assess broad existing-core reuse and family-level CPU coverage; establish a
   host execution path to the first genuine graphics or substantive service
   boundary using the same interpreter, rather than cycling through phone probes.
3. Freeze extra display-fixture/recipe expansion while prioritizing an actual
   game shader, resource binding and draw path through the broader translator.
4. Request the phone only when a concrete build and milestone test are ready.

Device availability: user has requested a rest. No device test is running, and
no further device operation is authorized until the user reconnects for a named
ready test. Host work remains authorized. Next progress check is due within
30 minutes of active work or sooner at the next major result.

CPU assessment: /tmp/satchel-cpu-progression-review.md.
Graphics assessment: phone_gpu_gate is freezing its code-grounded audit.
These assessments distinguish observed title requirements from generic missing
emulator features; they do not claim every listed feature is used by this title.

## 2026-11-01T04:16:58.805517+00:00: host execution established

**Development workflow validated; playability remains unproven.** The existing
host runner built against the combined UCVTF/FDIV CPU checkpoint and executed
the unchanged original XCI. It reached a new unsupported instruction at
main+0x844654, with63 completed IPC requests and0 guest storage reads. It stopped
before the known NVN bootstrap. No game graphics or frame was produced.

This changes the workflow: startup debugging can now proceed on the Mac while
the phone rests. There was no device operation or per-game executable preparation.
The next step is to classify the missing architectural family and complete a
coherent coverage group, not resume one phone test/full regression per opcode.
Do not interpret the new instruction total as a completion percentage.

Evidence: /tmp/satchel-host-runtime-cpu80-validation.json (run57768 exit0),
with the exact builder, CPU, loader and interpreter hashes bound by its manifest.
Host build validation also checked rejection of incorrect archive/manifest inputs;
the earlier wrong-output-directory fixture was explicitly corrected and is not
used as provenance evidence. Current usable refusal record:
/tmp/satchel-host-checkpoint-refusal-2x359g5e/results.json.

## 2026-11-01: focused development run81 and device reconnection

**Progress, with a remaining startup blocker.** The coherent FP immediate/move/sign
family was implemented in an isolated candidate and passed focused native and
sanitizer tests plus independent review. The actual original-XCI host report
advanced beyond the old FMOV stop to main+0x8a8730. It still reports63 completed
IPC requests and0 guest storage reads, before NVN; no game frame or controls.
The report is explicitly labeled cpuDevelopment. This does not establish full
regression, iOS readiness, or a phone gameplay result.

Evidence:/tmp/satchel-host-runtime-cpu81-validation.json binds the inspected report
and every build input. Session39189 disappeared after a turn interruption and no
host-runtime process remains; the terminal exit code was not recovered. The
launcher-created safe report is present. Do not invent an exit-code observation.

The previous turn made progress by implementing the reviewed focused-development
host workflow and launching this real original-file run. It was interrupted during
a verified live wait, not restarted. The next actual blocker classifies as STR
unsigned immediate; exact form and checked-memory coverage are being examined as
a coherent family. In parallel, work targets the existing broad translator's
captured shader/resource interface, not another synthetic graphics profile.

The user has now explicitly reconnected the phone and authorized testing. No
device operation was used for host81. A phone run will follow a meaningful ready
milestone; replaying a known CPU stop would not yet establish graphics progress.

## 2026-11-01: host82 memory coverage and broader shader adapter

**Progress, but still no graphics startup.** Original XCI run82 completed with
exit0 using the reviewed focused unsigned B/H/S/D/Q SIMD memory candidate. It
passed the observed STR S stop and now stops at main+0x8e21b0, STP D pre-index.
It still has63 completed IPC requests and0 storage reads. Changed code location
is CPU coverage evidence, not a gameplay milestone. Proof:
/tmp/satchel-host-runtime-cpu82-validation.json; all consumed build hashes verified.

Two successive development runs remain in CPU initialization without a new service
or graphics stage. To avoid another narrow store fix, the next CPU group covers
remaining ordinary SIMD pair forms and literal loads together, building on the
already implemented unsigned/register/unscaled families. This is an explicit
scope/cadence correction toward broader ordinary memory coverage; no additional
phone probe or full regression per instruction. No original executable preparation
is introduced. Phone testing remains authorized but no device operation occurred.

Graphics work now has a real broad-translator adapter: immutable shader/state
snapshots, upstream descriptor reflection and implicit support-buffer requirements.
An original uniform/arithmetic shader pair emits actual SPIR-V accepted by spirv-val;
18 focused checks passed. Root verified artifacts and reviewed adapter source:
/tmp/satchel-general-shader-root-review.json. This is not GPU output or a game shader.
Next concrete integration is a separate native ABI, then retained capture/state and
resource-aware Vulkan pipeline consumption. Strict flat validation stays unchanged.

Active ownership: cpu_memory_family owns new simd-memory-complete82 staging;
cpu_family_review reviews read-only; general_shader_bridge owns isolated native
bridge work. Root owns canonical promotion and device tests. Original app data and
installed phone79 are unchanged. No host runtime remains live after run82.

## 2026-11-01: host83, native compiler and captured-owner integration

**Progress with no gameplay milestone yet.** Host83 ran the unchanged original XCI
with the reviewed pair/literal memory candidate and completed exit0 at a new
unsupported LDXP64 pair in main+0xd2a32c. It still reports63 completed IPC requests
and0 guest storage reads, before NVN. Proof:/tmp/satchel-host-runtime-cpu83-validation.json.
The focused CPU gate passed25,456 checks after correcting an invalid initial page
permission in a fixture; production code was unchanged by that correction.

Startup has advanced through ordinary SIMD memory families, but repeated CPU
stops have not advanced services or graphics. Next work is a coherent baseline
exclusive-operation family, with actual shared-memory/monitor semantics, rather
than admitting one LDXP encoding. No additional phone test or full regression per
opcode. The installed app remains79 and user device testing is authorized.

Graphics moved from a managed adapter into actual macOS NativeAOT C ABI and then
through the real retained captured-program owner. Native40-check gate and both
SPIR-V modules passed; owner5938-check gate includes preserved legacy tests plus
an actual owned-pool snapshot→native uniform/arithmetic translation journey.
Native export epilogue crash and its thin-shim correction are preserved, as is the
owner builder's initial stale self-hash refusal. Evidence:
/tmp/satchel-general-shader-bridge-staging/revision2/validation-manifest.json and
/tmp/satchel-captured-general-owner-staging/validation.json; independent owner review
is /tmp/satchel-captured-general-owner-final-review.json.

Before moving to the renderer subsystem: the remaining concrete gap is consuming
these real modules and reflection with live resource bindings, not compilation
or another flat-shader proof. Next isolated Vulkan work reuses existing device,
buffer, job and fence ownership for a resource-aware pipeline/draw. Start with
uniform/storage buffers; unsupported image/sampler requirements remain explicit.
An original uniform-driven actual GPU image is the component milestone, not the
final goal. Actual title state/capture, iOS linkage, visible gameplay, guest controls
and a phone-only cold launch remain unproven. No canonical graphics changes or
device operations this turn. No host-runtime job remains live after host83.
