# Pocket Lab

## Required goal checks and device availability

Current user instruction, 2026-11-01: the phone is plugged back in and device
testing is authorized again. Root owns device builds/installs/tests. No device
test is currently running. Batch meaningful changes and request screen/unlock
only for a concrete ready test; do not require an idle screen to remain awake.
Host original-file development remains useful while CPU startup stops before NVN.
The earlier resting instruction is superseded by this explicit reconnection.

During active work, check alignment after every phone run and at least every
30 minutes, and before starting another subsystem. Record the evidence and next
action in Validation/goal-progress.md. Each check must answer:
- What moved closer to running the original imported XCI with visible gameplay
  and controls, without per-game computer preparation?
- What is the next actual end-to-end blocker, and which planned change removes it?
- Is reuse or a broader coherent change a shorter route than the current approach?

Distinguish component correctness from game progress. Assertion counts, source
volume, additional diagnostics, instruction totals, and UI polish do not establish
playability. Track actual startup/services, asset reads, real graphics calls,
shader/resource/draw integration, visible frames, input, sustained play, and a
phone-only cold-launch journey. Do not invent completion percentages.
If two consecutive checks show no meaningful runtime advance or evidence that
changes the route, reassess the architecture before adding more work. This is a
strategy checkpoint, not an automatic pause or permission request. Preserve live
jobs and existing work. Broad regression belongs to coherent functional milestones;
use targeted validation while developing a group of related changes.

Latest checkpoint — Release79 installed and actual Duo journey validated.
Exact ISB SY CPU fix passed 5,627,698 original native/ASan/UBSan checks and iOS17
compilation; five sources and two archives promoted. Prior generated CLREX-neighbor
failure/correction and independent reviews preserved. Phone79 passed automation
without a prompt, advancing to main+0x5fca94 at67,546,090 instructions; next exact
form is UCVTF unsigned64 GPR to binary64 (X to D). All14 original graphics diagnostics passed;
all5 library entries unchanged. Zero actual VI/Window/NVN calls, guest storage
reads, game frames or gameplay. Proof:/tmp/pocket-vixl-probe/isb-staging/phone79-validation.json.
CPU promotion:/tmp/pocket-vixl-probe/isb-staging/canonical-promotion.json.
App78 unchanged; cancellation09 already validated. No device test live.
Next slice: package_inspector owns /tmp/pocket-vixl-probe/ucvtf-f64-staging;
core_options reviews read-only. No canonical UCVTF64 changes yet.
Safe classification:/tmp/pocket-boundary79-forms/classification.json.

## Work in progress after phone79

Current host83 supersedes below: actual originalXCI session93568 exit0, unsupported
LDXP64pair main+0xd2a32c,67,599,796instructions/63IPC/0storage, beforeNVN/no frame.
Proof:/tmp/pocket-host-runtime-cpu83-validation.json. simd-memory-complete82 focused02
25,456checks+review clear; invalid NONE-page fixture corrected, productionunchanged.
Next cpu_memory_family owns exclusive-family83 staging, cpu_family_review reviews.
Canonical CPU remains full FDIV; focused families have not been promoted/phonebuilt.
General NativeAOT C ABI validated40checks+SPIRV in
/tmp/pocket-general-shader-bridge-staging/revision2 (frozen). Root general captured
owner seam validated5938 checks in /tmp/pocket-captured-general-owner-staging;
review:/tmp/pocket-captured-general-owner-final-review.json. This links actual
retained code/C1 snapshots to general translation, not GPU game drawing.
Next general_shader_bridge owns /tmp/pocket-vulkan-general-staging for a real
resource-aware uniform/storage pipeline/draw using existing fence ownership.
All graphics candidates isolated, no canonical/device changes; phone79 available.

Current host82 supersedes host81 below: original-XCI session59241 exit0, unsupported
STP D pre-index main+0x8e21b0,67,589,072instructions/63IPC/0storage, noNVN/frame.
Proof:/tmp/pocket-host-runtime-cpu82-validation.json. Focused memory-family81
candidate passed23,070 native/sanitizer checks and independent review; no canonical
promotion/iOS/phone claim. Next isolated simd-memory-complete82 stage covers
remaining ordinary SIMD pairs/literal loads; cpu_family_review independent.
General managed shader adapter is implemented/validated in
/tmp/pocket-general-shader-bridge-staging (18checks plus actual SPIR-V validation).
Root review:/tmp/pocket-general-shader-root-review.json. Separate native ABI work
is underway there; captured owner and Vulkan resource-aware consumer not linked.
Phone remains79, available by user reconnection; no device test started.

Latest host81: focused FP bit movement family passed204,613 native/sanitizer checks
and independent review. Explicit development host build/run advanced the original
XCI to main+0x8a8730, unsupported STR unsigned immediate (exact form pending),
67,584,715 instructions,63IPC,0storage reads. No NVN/game graphics/playability.
Report/manifest verified:/tmp/pocket-host-runtime-cpu81-validation.json. Run handle
39189 is gone and process absent after interruption; terminal exit code was not
recovered, but the strict launcher published its safe report. No run live.
Focused CPU stage:/tmp/pocket-vixl-probe/fp-bit-moves-staging; canonical remains
full FDIV, installed phone remains79. No focused-only candidate promoted.
Root implemented/reviewed explicit host development mode;10 new admission checks
plus unchanged5 launcher/19NVN checks pass, full-mode build passes, real candidate
binary refuses a relabeled manifest before opening a blocking fixture input.
cpu_memory_family owns new isolated memory-family81 stage; general_shader_bridge
owns isolated general-shader-bridge stage. No device work by agents.
The older dated records below are historical and superseded where stated.

Combined UCVTF/FDIV CPU is now canonical with5,837,760 full native/sanitizer
assertions and iOS17 compilation. Both producer jobs completed; no CPU gate live.
Promotion:/tmp/pocket-vixl-probe/fdiv-staging/canonical-promotion.json.
The phone remains79 and is unavailable by user request. Existing HostRuntime
checkpoint compatibility was repaired without dropping legacy/full source checks;
real host build passed. Original-XCI host run57768 completed exit0 at new unsupported main+0x844654,
67,581,010instructions/63IPC/0storage reads, before the known NVN bootstrap.
Proof:/tmp/pocket-host-runtime-cpu80-validation.json. No host/device test live.
Actual next form is vector FMOV floating immediate4S. package_inspector owns
/tmp/pocket-vixl-probe/fp-bit-moves-staging for a coherent bit construction/movement
family (scalar/vectorS/D immediates, scalarS/D FMOV, reviewed FABS/FNEG).
Classification:/tmp/pocket-boundary80-forms/classification.json. Focused gate only
until review; no per-opcode phone/full-regression cycle. Root next must provide
a clearly labeled, explicit opt-in host development checkpoint path for focused
CPU candidates while preserving the existing full-validated path and phone gates.
This is development validation, never a claim that the full suite passed.
Graphics compatibility scratch is frozen; further fixture/recipe expansion held
for actual game demands. Architecture audits are in /tmp/pocket-core-architecture-audit
and /tmp/pocket-window-policy-followup; main route remains broader current core
coverage and reusable general shader resources, not another miniature profile.

The installed phone remains ISB/phone79. Canonical host/iOS CPU now includes the
validated UCVTF W/X-to-D family (5,816,246 full assertions plus iOS17);
/tmp/pocket-vixl-probe/ucvtf-f64-staging/canonical-promotion.json. No device update. UCVTF W/X to D is implemented in
/tmp/pocket-vixl-probe/ucvtf-f64-staging, independently reviewed, and passed
363,078 focused native/sanitizer assertions. The UCVTF full regression/iOS build session81244 completed successfully;
core_options owns FDIV full session16682, still running at the latest check.
Root owns promotion and the next device test.

Root's isolated sRGB consumer in /tmp/pocket-runtime-frame-srgb-staging passed
47 ASan and 47 TSan assertions, including asymmetric midtones and both color
transfer directions, plus iOS17 compilation. Independent review is clear:
/tmp/pocket-runtime-frame-srgb-independent-review.json. No canonical changes.
Root's /tmp/pocket-runtime-window-preview-staging adds a pure checked metadata
policy (46 ASan/46 TSan assertions and iOS17 compilation), and staged App opt-in
passes whole-App iOS17 typecheck. C API is still staged; no integration/runtime
or actual phone frame claim. Preserve gate01-input-preservation.json: it retains
an earlier unused composite header after the draft ABI advanced for typecheck.

phone_gpu_gate owns isolated C asynchronous presentation compatibility work in
/tmp/pocket-window-compatibility-staging. It preserves explicit settings, carries
device origin/generation through a checked descriptor, and adds distinct sRGB
metadata. Missing interval/full crop use named emulator defaults only in this
opt-in. Pacing/vsync are not implemented by asynchronous copied-frame preview.
Focused owner/GPU checks passed; interpreted bridge and final review still pending.

core_options owns /tmp/pocket-vixl-probe/fdiv-staging, based on frozen UCVTF source.
FDIV scalar D was identified only in bounded static lookahead; it has not yet
been observed running on the phone. Production and original tests are staged;
focused01 passed21,493 assertions; focused02 adds an explicit underflow midpoint.
package_inspector reviews it. FDIV broad regression/iOS build is now session16682, based on the frozen combined
UCVTF+FDIV source. No one-opcode phone test is planned.

Prior Swift recipe digest finding was withdrawn: the original two-level hash
chain was correct. Additive retraction:/tmp/pocket-window-swift-review-retraction.json.

## Latest working checkpoint — 2026-10-31, phone78 installed/validated

Release78 actual Duo journey and cancellation09 passed. Window backend owner
created, actual UIKit surface attached; all14 original graphics checks passed;
all5library records byte-identical. Original title still stops at exact ISB SY,
sdk+0x13d9b4,67,544,364instructions. Zero actual Window/VI/NVN/storage calls,
game frames/playability. Proofs:/tmp/pocket-window-app-staging/phone78-validation.json
and phone-cancel09-validation.json; /tmp/pocket-runtime-castlevania-78-artifacts.json
binds freshRelease app/currentarchives/sources/unchangedrunner. No phone test live.
Canonical CPU remains combinedFCVTL/FCVTZU (5,626,857checks+iOS17,validated77).

ISB scratch:/tmp/pocket-vixl-probe/isb-staging. First fullgate89026 failed:
"CLREX altered fixed bits rejected before side effects". The unchanged generated
CLREX SY xorbit7 is exactly now-supported ISB SY. v2 preserves this input as a
positive PC/register/memory/live-exclusive test; every other negative retained.
Root+peer correction review clear; expandedfocused7711native/ASan/UBSan checks
passed. package_inspector owns finalfull+iOS session15140/full-02.log (running).
NoISBpromotion yet; originalfailure/checkpoints/reviews retained. Safe staticlookahead
ISB SY→physical-counter MRS→ISB SY→RET, only first observed at runtime.

Window C canonical:/tmp/pocket-guest-window-repro-overlay/canonical-promotion.json.
27source/test/doc/lock files+3iOSarchives,10bound gates and independent reviews.
Original real-Process/VI/Window/queued Vulkan fixture validates actual immutable
pixels; no game/UIKit proof. Legacy composites unchanged. Libraries:
/tmp/pocket-guest-windows/libPocketGuestWindows.a and
/tmp/pocket-guest-graphics/libPocketGuestVulkanWindows.a; matcheddispatcher updated.

Frameconsumer canonical:/tmp/pocket-runtime-frame-consumer-staging/canonical-promotion.json.
40ASan/40TSan assertions,64concurrentoffers each, actual CoreGraphics2x1/padded2x2
pixels, strictSwift6/iOS17 compile, App typecheck/peer review; existing4022surface/
40000TSan snapshot checks preserved. Owned copiedbacking8MiB/frame/24MiBglobal;
opaque linear-sRGB/top-left host profile, not an inferred NVN default.

AppWindow canonical:/tmp/pocket-window-app-staging/canonical-promotion.json.
8files:actual Session/Bridge/project/Window helper+model/reportfixture. WholeApp
finaltypecheck03 passed; helper65originalSwift/actualC assertions+iOS17; full
report49assertions; exact production frameworker8508Swift/realGPU assertions.
Cproducer4233checks includes original realProcess/VI/Window/raster READY frame,
liveCPU and CPUdestroyedbeforecopy modes. Copy/release/cancel/retirement reaches
zero; pure test lifecycle epoch/presenter publication is not UIKit/game display.
Pending WAIT parks exactcontext before scheduling; pure context+PC/LR revalidated.
Release retries are CPU-free and precede dispatcherretirement, thenVI retirement.
Nil presentationpolicy explicitly refuses Present; Acquire independently owns a
slot. Source/refusal/copydeadline checks are not guessed vendor defaults.

Portable fixture recipes are canonical in Tools/RuntimeWindow and
Tools/RuntimeWindowFrame (18source/test/doc/script files). Promotions:
/tmp/pocket-window-app-repro-overlay/canonical-promotion.json and
/tmp/pocket-window-frame-bridge-repro-overlay/canonical-promotion.json.
Relocated65helper+8508frame+iOS and C4233checks/source-CPU mismatch negatives
passed; independent cross-reviews clear. No additional runtime/artifact changes.
They use explicit source/dependency roots, CPUartifact and validatedproducerrecord.
No App/runtime dependency download. Root owns shared builds/promotion/device.
Policy evidence:/tmp/pocket-window-policy-evidence.md:libnx and observedNVN
interval/default behavior differ; UNORMbytes alone do not establish display
encoding or origin for every producer. Unknown presentation remains refused.
Goal remains active and unfinished; no actual Castlevania frame/control proof.

## Previous status — 2026-10-31, stage77 validated

Canonical combined CPU now includes exact FCVTL S→D lower/upper and vector2D
FCVTZU, with5,626,857 original native/ASan/UBSan assertions and iOS17 compile.
/tmp/pocket-vixl-probe/fcvtzu-combined-staging/canonical-promotion.json binds
8sources+2archives. Pool checkpoint keeps historical focused evidence unchanged.
Root Release77 build and actual Duo journey session11402 passed with unchanged
76runner/thresholds. /tmp/pocket-runtime-castlevania-77-artifacts.json binds fresh
Release app, sources/archives and unchanged5-entry library. App remains76.
67,544,364instructions, next unsupported sdk+0x13d9b4; classifier reports ISB,
exact form pending. All14graphics checks pass; zero actual VI/NVN/storage calls,
frames/gameplay. Proof:combined-staging/phone77-validation.json. No phone test live.
package_inspector next owns isolated ISB review/implementation after frame review;
no full gate until source reviewed. Root owns promotion/device.

Process context identity API is canonical:
/tmp/pocket-process-context-staging/canonical-promotion.json. Pure numeric
process/threadgeneration validation,746 focused/9300thread/245791Process checks,
iOS17 compile and independent review passed. Identity is not stop/PC/LR proof;
caller serializes the Process lifetime. Recycled pointers require numeric tokens.

Queue order lease helper is canonical:
/tmp/pocket-present-queue-repro-overlay/canonical-promotion.json. Full affected
legacy raster/captured/texture gates and iOS17 passed, old composites unchanged.
Root canonical real-Vulkan output guard reproduction passed67859 assertions,
431preserved normal/30adaptive fault checks; separate canonical-range-gpu-reproduction.json.
10ms bounds requested fence wait, not total synchronous driver/readback work.
No actual game Window or frame has been reached.

phone_gpu_gate owns isolated Window owner/composite at
/tmp/pocket-guest-window-staging/window-integration. Owner gate08+peer review
passed12590added/6361preserved assertions; joint real-Process gate02 passed4419
added/13057preserved. Composite regression/iOS/review remain pending; no canonical
Window promotion. core_options owns independent final composite review.

Root frame consumer scratch:/tmp/pocket-runtime-frame-consumer-staging/source.
Immutable8MiBframe/24MiBcopied-backing budget, linear-RGB opaque host canvas,
epoch-checked coalescing mailbox and UIKit aspect-fit image consumer implemented
in isolation. gate03 passed38original ASan/TSan checks+64concurrentoffers each,
actual CoreGraphics pixel draw and strictSwift6/iOS17 compilation. Whole-App
initial typecheck passed; final peer review pending. No GPU/game/UIKit display
validation yet. Deferred observable invalidation avoids publishing from UIView
updates; surface epoch invalidates synchronously before image/mailbox clear.
No new App source is canonical yet. Root owns shared builds/device/promotion.

## Previous status — 2026-10-31, stage76


App VI integration is canonical and installed:
/tmp/pocket-vi-app-staging/canonical-promotion.json. Root Release76 build/link and
actual Duo journey passed. Real attached UIKit canvas, Process protected identity,
all7 fully checked VI symbol definitions and owner creation validated. Report:
67,505,950instructions/main+0x55db34, same FCVTL boundary as75,63/63IPC,
zero actual VI/NVN calls and guest asset reads. All14 synthetic graphics checks
pass; all5 library entries unchanged. No game frame/playability; Play remains
unavailable and graphicsImplemented=false. Count difference is not CPU progress.
Proof:/tmp/pocket-vi-app-staging/phone76-validation.json and matched76artifacts.
Actual background cancellation08 also passed (executing→Settings→background→
cancelled/re-enabled) with unchanged library; separate phone-cancel08-validation.json.
No phone test remains live. User's unlocked/no-prompt reply now resolved.

App sources received final independent review, whole-App iOS17 typecheck and
43report/actual-C-early-refusal assertions against actual sources; original gate
used frozen UCVTF, relocated root gate used current FMUL. Reusable fixture recipe
is now Tools/RuntimeVIReport; promotion:/tmp/pocket-vi-app-report-staging/canonical-promotion.json.
Surface state recipe is Tools/RuntimeSurface; original4022 lifecycle assertions/
40000 ThreadSanitizer concurrent snapshots and strictSwift6/iOS17 compile pass.
Explicit borrowed surface validation/cancellation occurs inside graphics queue;
C returned status overrides untouched early-error output; no invented success.
Dispatcher handoff precedes VI retirement, followed by Process destruction.
Window/frame consumer remains unimplemented in App; canvas is a waiting label.

Next CPU work: FCVTL S→D lower/upper focused gate passed220,813 original
native/ASan/UBSan assertions; /tmp/pocket-vixl-probe/fcvtl-staging/focused-validation.json,
source checkpoint a8ae47c7, independent review4f6cd92e clear. No canonical change.
Root bounded static lookahead (not runtime progress) found next FCVTZU2D at
main+0x55db38 before STR/MOV/BL. Evidence:
/tmp/pocket-boundary75-forms/bounded-forward-families.json and
/tmp/pocket-boundary75-following-forms/classification.json. Only FCVTL is observed
at runtime. To avoid immediately repeating full gates, core_options now owns
/tmp/pocket-vixl-probe/fcvtzu-combined-staging, copied from frozen FCVTL source,
implementing exact unsigned2D toward-zero conversion; package_inspector owns
independent delta review. Core owns eventual combined full native/sanitizer+iOS
build once reviewed; no separate FCVTL broad gate. Root owns promotion/device.
Canonical CPU remains FMUL/5,047,392 full native/sanitizer assertions+iOS17.
phone_gpu_gate owns /tmp/pocket-guest-window-staging; queue helper review clear
with timeout wording corrected separately; durable recipe/full legacy raster+iOS
reproduction pending before promotion. Window owner initial synthetic host gate
2409added+6361preserved assertions passed, but final composite/dispatch/iOS and
review remain pending. No canonical queue/Window change. Root owns shared builds.
Queue full raster overlay first stopped beforecompile at old dispatcher source
lock; agent retained log and binds reviewed canonical output guard into scratch
lock rather than bypassing validation. Root preliminary Window concern: recheck
cancel/epoch after synchronous ordering/publish and before Acquire/Initialize
commit; agent owns correction/test before final owner freeze. Next root review
must cover this and stale-slot lifetime. No actual phone test remains running.

## Previous status — 2026-10-31, stage75

Release75 actual Duo journey passed:67,505,708 instructions, main+0x55db34,
after indexed FMUL at main+0x55db28. All14 original synthetic graphics checks
passed; all5 library entries byte-identical. No game NVN call, guest asset IPC
read, game frame or gameplay. graphicsImplemented=false and package Play disabled.
Canonical CPU:/tmp/pocket-vixl-probe/fmul-staging/canonical-promotion.json;
5,047,392 native/ASan/UBSan assertions+iOS17. Phone proof is separate:
/tmp/pocket-vixl-probe/fmul-staging/phone75-validation.json. Next actual instruction
classified RAM-only:/tmp/pocket-boundary75-forms/classification.json: FCVTL S→D,
2D lower source half. package_inspector owns next isolated CPU slice after its
current VI App report fixture. Root owns canonical promotion and all device builds.

Dispatcher output guard is canonical:/tmp/pocket-dispatch-range-staging/canonical-promotion.json.
324 focused+4638 unchanged dispatcher assertions/iOS17; actual unchanged Vulkan
composite reproduction67859 added assertions/510 queries/84 aliases+84 live cases/
20 released cases,431 original normal and32 adaptive fault assertions this run.
Separate proof:/tmp/pocket-dispatch-range-staging/canonical-gpu-reproduction.json.
No texture guard inversion existed; reviewer retracted that finding; composites
remain unchanged. New guard and Process opt-in API compiled in75, but unchanged
App does not yet use either. VI archive remains separate and unlinked in75.

Root App VI draft:/tmp/pocket-vi-app-staging/source (canonical App unchanged).
Real UIKit surface with invalidating attachment epochs, Process identity opt-in,
merged VI/NVN breakpoints, serialized borrowed dispatch, optional typed telemetry
and CPU-free retirement. Whole-App iOS17 typecheck03 terminal0. Source review:
/tmp/pocket-vi-app-independent-review.json, both epoch/status findings resolved.
Surface original state gate4022 assertions/40000 ThreadSanitizer snapshots and
strict Swift6 iOS17 compile; relocation gate in progress. package_inspector owns
/tmp/pocket-vi-app-report-staging actual C early-refusal/report compatibility gate.
Whole-App build/link and actual view/owner/background validation remain pending.
No actual VI call, Window or game frame is claimed. Root added optional UI
integration assertions in scratch only; old checks preserved.

phone_gpu_gate owns /tmp/pocket-guest-window-staging: proposed Window owner and
separate composite, with reviewed queue-order lease dependency. Queue-helper
producer manifest frozen; core_options independently reviewing. Actual old queue
work must drain; cancellation cannot fabricate READY or drop jobs. Poll caps
requested fence waits, not total synchronous driver/readback wall time. No
canonical queue/Window mutation yet. Root owns shared builds and promotion.

## Previous status — 2026-10-31, stage74b

Canonical CPU is `/tmp/pocket-vixl-probe/ucvtf-staging/canonical-promotion.json`:
unsigned W/X GPR→S,3,846,042 full native/sanitizer assertions and iOS17 compile.
Release74 initially failed before test initialization: "Timed out while enabling
automation mode." User reported unlocked/no prompt; unchanged74b retry passed.
Actual phone74b:67,505,705 instructions,main+0x55db28,after previous UCVTF stop
main+0x55db14. All14 synthetic graphics checks pass; all5 library entries byte-identical.
No actual game NVN call, asset IPC read, frame or gameplay; graphicsImplemented=false.
Evidence:/tmp/pocket-vixl-probe/ucvtf-staging/phone74b-validation.json;
/tmp/pocket-runtime-castlevania-74-artifacts.json and74b.{json,log,xcresult}.
Counts vary; boundary advancement, not instruction-count growth, proves progress.

Process identity reservation source is newly canonical:
/tmp/pocket-process-identity-staging/canonical-promotion.json. Four reviewed files;
2132 focused,245791 Process,8553 thread assertions and final iOS17 compile passed
against frozen DUP/SHRN CPU. Existing constructors unchanged; opt-in App integration
and4096-byte preflight charge pending. This source was NOT included in74b build.
VI owner/dispatch is now canonical in Tools/GuestVI, with separate archives at
/tmp/pocket-guest-vi/{macos,ios}/libPocketGuestVI.a. Promotion:
/tmp/pocket-guest-vi-repro-overlay/canonical-promotion.json.68705normal/70292fault/
142real-Process joint assertions, iOS17/11exports, relocation and independent
review passed. Per-transaction deadline; no real surface/Window or game calls yet.
App is not linked to VI. Requires actual App surface epoch, Process opt-in+4096
preflight charge, output guard and merged breakpoints before actual invocation.

Root owns new pl_guest_dispatch_check_output_range in scratch only:
/tmp/pocket-dispatch-range-staging/validation-manifest.json.324original synthetic
backend/service assertions +4638unchanged dispatcher checks, iOS17/19exports
passed. Production read-only review clear; review record predates21 added
provisional-device assertions and relocated recipe. Actual composite coverage
is being implemented by phone_gpu_gate in /tmp/pocket-dispatch-range-gpu-staging.
Do not promote the guard/archive or claim real-composite validation until that
matched gate completes. No existing composite was edited. Reviewer initially
suspected inverted texture status but retracted it: canonical/frozen sources
match immutable old hashes and all nine texture guard calls are correctly
negated. The correction is explicit in /tmp/pocket-dispatch-range-independent-review.json.

Exact next CPU classification at main+0x55db28: indexed FMUL2S. Evidence:
/tmp/pocket-boundary74-forms/classification.json SHA fbd6c187bbb200e5e3284ed282cf338ffdb6f6fe4914c0ac17fd1daa5956ce8b.
package_inspector owns /tmp/pocket-vixl-probe/fmul-staging: exact indexed2S/4S/2D,
integer binary32 multiply+existingBinary64, fullQ snapshots and atomic flags.
Root/peer preliminary production review clear; initialfocused compile failed
unknown qreg_t and corrected to Simulator::qreg_t before tests. Focused02 session
32505 last reported running; agent owns polling/fullgate. No FMUL pass/promotion
or device claim yet. core_options owns /tmp/pocket-fmul-independent-review.json.
Root owns canonical promotion, App and device builds; no device test is live.
Host UCVTF diagnostic build and filtered original XCI run passed; output
/tmp/pocket-host-runtime-ucvtf-result.json matches actual74b main+0x55db28 at
67,505,705 instructions (118797ms host). Host output remains separate from phone proof.
No phone test remains live. No need to request authentication again now.

## Historical checkpoints

This project is an exploratory no-JIT Switch homebrew interpreter for iPhone.
The original downloadable TicTacToeNX NRO is validated on the iPhone Duo.
It is not a complete Switch emulator; preserve the limited compatibility claims.

Phone availability (2026-10-31): user explicitly authorized the reconnected Duo.
Latest installed check is stage73: DUP/SHRN CPU and unchanged52-export Vulkan.
67,505,918 instructions; main+0x55db14 is UCVTF scalar S from unsigned64 GPR.
All14 original graphics checks pass, including FIRST/LAST shader colors/coverage
and the new texture-frame check:2uploads,1completed immutable snapshot,
1cancelled/drained snapshot, post-CPU-destruction lease, allresources0→0.
All5 library entries unchanged. No game NVN/asset IPC/frame; packages remain
non-playable. Stage68 first validated captured-source pipelines; both separate
NativeAOT frameworks execute in the App with existing compiler first. No other
cold-order or actual captured-compiler background cancellation claim.

The previous CPU foundation combined count (CNT/CLS/CLZ) and widening reductions
(SADDLV/UADDLV) with prior FMOV and integer SIMD support:
/tmp/pocket-vixl-probe/simd-widen-reduce-staging/canonical-promotion.json.
2,699,982 native/ASan/UBSan assertions and iOS17 compilation passed; device
validation passed stage71. Earlier failed/misclassified original fixtures and
correction reviews are preserved in its validation manifest. Memory/pool code
is unchanged; PoolReprotectCheckpoint accurately retains the older focused run.
Do not modify immutable promotion manifests; later evidence belongs separately.

Captured NVN admission is canonical and App-linked (phone70), separate from the
unchanged legacy raster composite. SetShaders admits bounded source; Bind stops
with state-unavailable19 until actual state is known. No game NVN call yet.
/tmp/pocket-nvn-captured-program-repro-overlay/canonical-promotion.json.

The 52-export Vulkan backend with explicit FIRST/LAST modes is canonical:
/tmp/pocket-vulkan-provoking-staging/canonical-promotion.json. All matched host
gates and iOS compile passed; installed71 now has52 exports. LAST uses
bounded per-triangle index rotation for admitted pure typed triangle shaders;
no private Metal API or native provoking extension is enabled. CompilerABI2
remains FIRST-only; this does not establish actual NVN enum/default state.

An original offscreen FIRST/LAST diagnostic is canonical and newly App-linked:
/tmp/pocket-provoking-diagnostic-staging/canonical-promotion.json. 271normal+
17fault host assertions, iOS17 compile, and12 Swift report assertions passed.
It uses one original source pair, two pipelines/draws/fences and verifies color,
coverage, guards, vertices, cancellation and all public/private resource counts.
App source peer review and stage71 Release build/device validation passed.
Root owns all App/device builds. The package agent owns an isolated, bounded
original-SDK bootstrap lookup experiment; no setter/default or game-frame claim.
Host NVN tools remain frozen at the earlier FMOV checkpoint, not rebuilt against
combined CPU. No actual game NVN execution has been established there either.

Active work after73: canonical CPU is
/tmp/pocket-vixl-probe/simd-element-narrow-staging/canonical-promotion.json.
3,671,512 full native/sanitizer assertions+iOS17 compile and actual73 passed;
terminal evidence and phone73-validation.json are separate immutable records.
Pool provenance retains the earlier focused pool gate; no new focused pooltest.
Exact vector-element DUP and nonrounding SHRN/SHRN2 added; no scalar DUP,
rounding or saturation. core_options owns /tmp/pocket-vixl-probe/ucvtf-staging:
exact unsigned W/X GPR→S conversion, direct integer-only binary32 nearest-even,
FPCR0/DN and cumulativeFPSR preserved. Actual73 is X→S; W→S is a fixture scope.
Root preliminary production review is clear; tests and independent peer review
are in progress. No new CPU gate or admission yet. No gamebytes/operands exported.
package_inspector owns /tmp/pocket-bootstrap-probe-staging: isolated original-SDK
bootstrap resolution, no setter invocation, original data RAM-only, throwaway
process with bounded interpreter lookup and strict output whitelist. Synthetic
177 assertions and all3 fresh original SDK lookup trials passed: device resolver
and provoking setter resolve into original SDK text, unknown control returnsnull.
/tmp/pocket-bootstrap-probe-staging/validation-manifest.json binds evidence.
No returned function/setter was invoked; ABI/default/state remains unproven.
The bounded read-only capability scan is complete at
/tmp/pocket-provoking-capability-scan-staging/validation-manifest.json:
12,579 original sanitizer assertions; actual scan visits5 instructions with2stores
and stops at1opaque-read frontier. No setter invocation or enum/ABI proof.
/tmp/pocket-provoking-abstract-memory-plan.md is a research plan only; symbolic
implementation and further original analysis are deferred. Defaults remain UNKNOWN.
Texture-to-immutable-frame ownership is now canonical and App-linked:
/tmp/pocket-texture-frame-repro-overlay/canonical-promotion.json.
Normal25,034/fault33,987 assertions+iOS17 and new-composite captured admission,
finalize retry and compute regressions passed. Explicit host-selected texture,
generation/epoch/format/origin; no automatic game window or display selection.
The original texture-frame diagnostic is also canonical:
/tmp/pocket-texture-frame-diagnostic-repro-overlay/canonical-promotion.json.
431 normal/30 fault host assertions+iOS17 compilation and root/peer review passed.
App integration:/tmp/pocket-texture-frame-app-staging/canonical-promotion.json;
16 Swift ASan report assertions and matched Release73 actual-device journey
passed. Actual dispatcher now opts into texture_frames; optional report and
cleanup collector added. Existing13 checks unchanged; new14th check passed.
phone73-validation.json binds full counts and library preservation. The GPU agent
completed /tmp/pocket-window-presentation-plan.md from public local references;
no Window/VI/display implementation or new game-frame claim yet. Next GPU task
is a strict public nn::vi symbol/prototype whitelist and metadata-only reader
proposal; root review is required before the authorized original-symbol scan.
No original code/layout inspection or function invocation is part of that task.
Root owns canonical promotion, App and device. No device build/test remains live.
Validation/provoking-vulkan-phone.md now binds actual71 Vulkan evidence.

Existing custom xctestrun paths target Release-iphoneos: always build with
-configuration Release and bind actual runner/app/framework/archive hashes.
Test67 mistakenly used stale Release while Debug was rebuilt; retained mismatch
record is /tmp/pocket-runtime-castlevania-67-build-mismatch.json. Corrected67b,
68 and69 passed. Do not count67 as new CPU validation.

Current direction: the user requires one on-phone emulator that imports games
and updates directly from iCloud, with no computer or per-game preparation.
The Mac native path below is an existing proof/benchmark, not the solution to
this requirement. Do not start another per-game native port as a substitute.
`App/SwitchPackage.swift` and `SwitchPackageImport.swift` implement bounded,
file-backed XCI/NSP container inspection and atomic, cancellable local copies.
Package storage is validated. New `SwitchExecutable.swift`/`SwitchCrypto.c`
implement bounded NCA3/ExeFS decryption and integrity checks with user-imported
console keys in device-only Keychain storage; `NSOLoader.c` decodes LZ4/raw NSOs
and checks declared segment hashes. `SwitchRuntimeSession` connects these to
an explicitly limited VIXL CPU probe. It is a loader diagnostic, not game boot.
Generic XCI/NSP gameplay, title/update association, full guest filesystem services and guest GPU execution remain
incomplete. Do not show Play for package entries. Preserve backwards decoding of the original NRO library JSON.
Run scripts/test_packages.sh and scripts/test_package_import.sh for changes.
Build 8 is installed. Build 7's real Castlevania XCI (4.85 GB) and NSP (88.8 MB) were imported
through the Duo iCloud picker and persisted after app restart; three original
NRO entries are unchanged. See Validation/phone-import.md for evidence/limits
and Tools/PHONE_RUNTIME.md for the execution architecture gates.

The CPU probe source/patch/API is in Tools/CPUProbe, with pinned VIXL source and
builds outside the repo in /tmp/pocket-vixl-probe. Before building the iOS app,
run `zsh scripts/test_cpu_probe.sh --ios` (see its README to fetch the pinned
source if absent). project.yml links the resulting static archive, not per-game
native code. Keep its BSD license in app resources. Never broaden its instruction
allowlist without reviewing reachable upstream helpers and adding native/sanitizer
comparisons. Preserve DEBUG/NEGATIVE_TESTING/memory-guard build requirements;
no native callbacks, executable guest mappings, or fabricated SVC results.
No raw game instruction words, operands, key values or decoded module bytes may
enter reports/logs/files. Only original synthetic assembly belongs in tests.
Run test_switch_executable.sh, test_nso.sh and test_switch_keys.sh for affected
loader/key changes. Actual graphics imports are documented in Tools/NVN_RUNTIME.md.
`ProcessMetadata.c` reads bounded structural NPDM data; `SwitchProcess.c` maps
all decoded modules plus NPDM-sized stack/TLS with correct initial memory types.
QueryMemory uses actual mappings; GetInfo implements process entropy and reserved
virtual regions with ABI validation. Unknown SVCs/info types still stop.
Guest TLS registers are context-owned and initialized to the actual mapped TLS.
This process diagnostic has a bounded cooperative scheduler, but no full
Horizon environment or guest GPU rendering. Preflight all decoded images plus stack/TLS against
the 256 MiB limit before allocating any images. Keep the eight-module limit,
16 MiB stack cap and explicit rejection of unsupported metadata flags.
Run test_process_metadata.sh and test_switch_process.sh for those changes.
Do not silently repurpose the old NRO kernel's alias-code state values for NSOs.
Latest validated phone check: stage69,67,464,992instructions;63/63IPC completed.
Phone67b adds register-offset/ADD-SUB/compare/logical/reduction/permutation CPU
slices and the49-export renderer; all11original graphics checks pass. All5library
entries byte-identical before67/after67b. Main+0x17477b8 is the current unsupported
CPU instruction boundary after FMOV; no game NVN/asset IPC read/frame.
Application PCTL Initialize1 serviced once,1live/initialized/metadata-bound child,
4created,policy UNCONFIGURED;3server closes/0disconnected client handles.
Time:u getter20 returns a real copied shared handle; actual SVC13 mapped its4096
read-only page on phone. Seed configured/attached once from actual device time,
1map requested/serviced/live,network uninitialized,timezone name-only profile1.
Clock IPC/sharedpage use one19.2MHz timeline, including continuous-adjustment data.
No timezone-rule conversion/network synchronization/general shared-memory claim.
Stages58/59b passed reviewed Q128 pairs and general-register DUP;63b passes
D64 unscaled/pre/post loads/stores;64 passes exact D64 unsigned-immediate
loads/stores;65 passes the reviewed integer MOVI family;66b passes B/H/S/Q
unscaled/pre/post transfers. Next actual boundary after66b:
main PC0x882a958,reportedoffset0x825958,an instruction outside
the reviewed CPU subset; no unsupported SVC is reported at this stop.
Telemetry60 identifies actual SVC02 address0x104a3000,size0x4c000,newpermission1(R).
First QueryMemory block starts there,size0x5f000,state4CodeData,attributes0,oldRW3.
This76-page prefix of a95-page queried block was actually reprotected on61:
SVC02requests1/serviced1/changedBytes311296. CPU retained backing/split permissions
and Process uniform-state transaction are installed and validated. Stage62 also
services the one-way SVC03 PermissionLocked transition:3requests/3serviced,
4,456,448bytes currently locked. SVC02 now totals2requests/2serviced/491,520bytes
changed. Attribute metadata preserves backing, permissions and accounting;
Uncached requests still stop rather than claiming a cache operation.
Typed optional request/first-block report is App-only and validated60; no raw
code, general-register dump or operands exported.
Time Process:/tmp/pocket-time-model-staging/canonical-promotion.json.
CPU readonly page:/tmp/pocket-vixl-probe/readonly-shared-staging/canonical-promotion.json.
App sampler/seed/report:/tmp/pocket-clock-sampling-staging/canonical-promotion.json.
Run scripts/test_switch_time.sh for changed clock sampling/packing. Reserve the
extra4096 clock backing in App initial256MiB preflight. Time seed/rawlocation/UUID
are not exported in reports. Unsupported cross-backing CPU reads remain explicit.
The phone verifies/links selected Application Control directly from the imported
XCI: title0100ba4026e94000/version0,Meta3584bytes/1block,Control1,219,584bytes/8blocks,
32ratings. Numeric metadata attaches once to matching NPDM program ID; policy
stays UNCONFIGURED. No policy initialization/approval is fabricated. Verified
base-Application Control metadata is now required by package runtime checks;
Program-reader APIs and NRO gameplay are unchanged. Integrity scope/limitations
are documented in Validation/phone-runtime-progress.md; no publisher-signature,
full encrypted Program-NCA digest, or retail CNMT-dev-digest recomputation claim.
All11 graphics checks pass. All5library entries match before36/after67b.
Cancellation07 passed actual backgrounding during execution after59b. No game NVN,
asset IPC read or Castlevania frame yet. The earlier time slice passed
216,395Process/8,553thread assertions. SVC2 passed242,371Process/8,553thread
assertions and was installed61; its record remains
/tmp/pocket-memory-permission-staging/canonical-promotion.json.
Canonical Process:/tmp/pocket-memory-attribute-staging/canonical-promotion.json
(243,659Process/8,553thread assertions); SVC3 validated62.
Canonical CPU:/tmp/pocket-vixl-probe/simd-integer-combined-staging/canonical-promotion.json
(1,015,100original/native/ASan/UBSan assertions; iOS17 compiled). Combines
eight vector logical operations, five reductions, EXT and six permutations.
Independent merge audit reconstructs prior baseline byte-for-byte after removing
exact additions; all prior/new tests retained. Phone67b passed; no combined-checkpoint host game run yet.
Previous comparison:/tmp/pocket-vixl-probe/simd-compare-staging/canonical-promotion.json
(559,090original/native/ASan/UBSan assertions; iOS17 compiled; original-game
host run reached67,462,784instructions/main+0x8217c8 in117963ms, then another
unsupported instruction;63IPC and memory counters unchanged, no NVN/frame). Six exact integer vector comparisons/tests admitted after root and
independent helper review; header/API unchanged. Generic unsigned App build
with this CPU and49-export renderer passed; no install.
Prior ADD/SUB:/tmp/pocket-vixl-probe/simd-addsub-staging/canonical-promotion.json
(302,942original/native/ASan/UBSan assertions; iOS17 compiled). Integer vector
ADD/SUB across seven allocated arrangements is host-game validated:67,462,783
instructions,main+0x8217c4 (121669ms under explicit240s host limit).63IPC and
permission/attribute counters unchanged; no game NVN/frame or guest asset reads.
Only the exact vector guard changes; pinned unsigned modulo helpers reviewed.
Initial fixture incorrectly queried FP status after terminal unsupportedQ0/D;
now asserts INVALID/unchanged outputs there, preserving all valid-form checks.
Prior register offset:/tmp/pocket-vixl-probe/simd-register-staging/canonical-promotion.json
(217,372assertions). Register-offset
B/H/S/D/Q transfers promoted and host original-game check validated:67,453,390
instructions,main+0x824980 (114799ms under explicit240s host limit),63/63IPC,
permissions2/491520,attributes3/4456448. Included in phone67b validation. Host120s run
first cancelled before comparison; keep /tmp/pocket-host-runtime-register-01.json.
Tools/HostRuntime is now canonical:5files, RAM-only original-file CPU/Process
adapter, exact canonical stub bytes, verified Control/RomFS/real host time.
Require exactlyone known NVN bootstrap and stop before dispatch; never claim
host output as a phone/game-frame result. Build binds reviewed CPU sources and
archive, canonical IPC source and all consumed headers; launcher checks binary
hash, suppresses private errors and strictly filters output. Baseline matched
phone66b at main+0x825958 before register expansion. See
/tmp/pocket-host-runtime-staging/canonical-promotion.json and
Validation/host-runtime-progress.json. No raw game instruction/operands exported.
Previous CPU:/tmp/pocket-vixl-probe/simd-widths-staging/canonical-promotion.json
(212,454assertions); historically installed/validated66b, superseded by67b.
B/H/S/Q signed single transfers use full-span preflight before integer copies and
writeback. Next actual instruction is SIMD128 single store, register offset.
Initial66 failed minimum count67401669<67401880; unchanged66b passed67401881
at the same main+0x825958 endpoint. Preserve failure and unchanged retry; no test
threshold or randomness changed. Exact cause of count variability is unproven.
Earlier MOVI:/tmp/pocket-vixl-probe/movi-staging/canonical-promotion.json
(209,184assertions); integer MOVI validated65.
Native fixtures exhaust256 immediates across20 arrangements; no FP/MVNI/ORR/BIC added.
Earlier unsigned D64:/tmp/pocket-vixl-probe/simd-unsigned-staging/canonical-promotion.json
(202,973assertions); validated64.
Matched GPU pool gate196,700 passed. Earlier D64 unscaled/pre/post record:
/tmp/pocket-vixl-probe/simd-single-staging/canonical-promotion.json
(202,453assertions); validated63b. Its initial load-reservation failure was fixed
with checked integer bit copies,
not by removing the assertion. Prior reprotection record remains
/tmp/pocket-vixl-probe/reprotect-staging/canonical-promotion.json
(201,439fullCPU +181focused checks, not summed); installed61.
Initial63 ran no tests: UI automation initialization timed out. After the user
authenticated, unchanged63b passed; this is not a guest-runtime regression.
App attribute counters:/tmp/pocket-memory-attribute-app-staging/canonical-promotion.json.
Earlier permission counters:/tmp/pocket-memory-permission-app-staging/canonical-promotion.json.
Real GPU pool reprotection196,700assertions passed; durable focused test is
scripts/test_pool_reprotect.sh (see Tools/GuestGraphics/POOL_REPROTECT.md).
Protected/fragmented allocation reshape remains explicitly unsupported until
wholeRW restoration; no invented kernel error or detached replacement backing.
Canonical GPU:/tmp/pocket-captured-host-repro-overlay/canonical-promotion.json.
Matched full raster/flat/ABI2 adapter gates passed;49-export iOS17 framework
and16archives are promoted. Unsigned generic iOS App build passed with the
comparison CPU; no physical device action. Previous48-export checkpoint remains
at /tmp/pocket-vulkan-flat-repro-overlay/canonical-promotion.json.
Phone67b now validates49exports and all11legacy original graphics checks.
Prior44-export phone framework checkpoint66b remains:
/tmp/pocket-nvn-raster-repro-overlay/canonical-promotion.json.
The actual dispatcher opts into bounded raster alongside compute/transfers.
Original interpreted offscreen raster passed59b:558instructions/186events,
2programs/2pipelines,clear+2draws,1fence,verified pixels/cache/padding/cleanup.
Alltracked GPUresources0→0. No window/acquire/present-to-screen implementation.
Standalone frame handoff is now canonical but not linked to App:
/tmp/pocket-frame-handoff-staging/canonical-promotion.json (8source files;
10,580host ASan/UBSan assertions; iOS17 archive compiled, no phone/display test).
Immutable leases survive detach; retain device through readback/cleanup, and
budget UI copies separately from the3-frame/24MiB owner-managed CPU cap.
Run scripts/test_nvn_raster.sh --ios for matched raster source/artifact changes;
Tools/GuestGraphics/RASTER.md documents profiles, limits and reproduction.
App raster integration:/tmp/pocket-raster-app-staging/canonical-promotion.json.
Initial59 runner launch failed socketEOF; unchanged59b retry passed.
Control-loader:/tmp/pocket-control-staging/canonical-promotion.json.
Report activeModuleOffset derives from actual mapping;50/51 SDK+0x17014c agrees
PC0xfad814c. Never manually guess relative offsets; prior49 subtraction error
was caught before CPU edits (actual STURH+0xc435c, unrelated SUB+0x9435c).
Earlier raster foundations are now integrated by the canonical raster promotion.
Canonical typed paired Vulkan renderer adds independently typed scalar
SPIR-V admission, one flat varying, complete Position/RGBA writes, FIRST
provoking and fully known state255/y_negate0. It does not infer source-state
independence from output. Full reproduction includes55,992typed GPU assertions,
80preflight/24capability assertions and819spirv-val modules. Physical BGRA
capability is separate from actual RGBA8-only target support.
Canonical captured C API: Tools/CapturedFlatCompiler; promotion at
/tmp/pocket-captured-flat-staging/canonical-promotion.json. Its242source files
include licensed pinned translator source, strict admission and offline recipe.
Cold C gates,348typed pairs and696spirv-val modules passed; iOS17 framework
compiled but NOT linked App. Its6-file additive live GPU test is also canonical:
/tmp/pocket-captured-flat-gpu-repro-overlay/canonical-promotion.json. Matched
canonical gate83,040assertions passed33actual compiles/32original pairs/64draws
and fences. No precompiled SPIR-V fixture replaces input. Actual physicalBGRA
and alignment are queried; other fixture host facts are explicitly synthetic.
The reviewed ABI2 device→compiler adapter is now canonical:27source files,
49-export matched Vulkan framework, compiler framework and adapter archive.
/tmp/pocket-captured-host-repro-overlay/canonical-promotion.json binds sources,
artifacts and full matched raster validation. Actual device/target facts and
implemented policy resolve22queries; two driver facts stay unknown and only
strict admitted source proves them unused.39facts/241ABI2/83072liveGPU assertions
passed; original v1 gates preserved. Not App-linked or phone validated.
Four NativeAOT coexistence test files are also canonical;106900assertions across
24fresh macOS processes cover both revisions, iOS17link-only, no device test.
/tmp/pocket-nativeaot-coexist-repro-overlay/canonical-promotion.json. Separate
frameworks retained for process lifetime; guest captured-source/state wiring
and presentation remain incomplete.
Source-only admission and separate captured-program owner are also canonical:
/tmp/pocket-guest-captured-program-repro-overlay/canonical-promotion.json.
22files, additive compiler framework and13-export owner archive; standalone
5879owner assertions/8actual original-source GPU draws plus all v1/v2 regressions
passed. Matched canonical owner gate also passed. Full GPU-visible wrapper/code/
C1 retained and revalidated; source requirements never invent missing state.
No App linkage/phone validation yet. Provoking ABI/default remains unverified;
known247 is refused by existing compile contract. See CAPTURED_PROGRAMS.md.
No game shader/frame claim. Latest phone raster still uses original fixtures.
Never export opaque mutex tagX2 or the mutex word; only typed metadata/counts.
Background cancellation passed separately in cancel01 after Settings activation
proved the app actually backgrounded. Home automation on this Duo had left it
foreground; stage28/29 cancellation failures remain recorded. See progress record.
No game frame or guest-originated NVN call yet. Thread queries, acquire/release
instructions and memory accounting passed on device. SetHeapSize has real bounded
backing and host validation, but this game has not requested a heap yet.
See Validation/phone-runtime-progress.md and runtime-core.md/json. Scheduling, guest asset IPC and
guest NVN integration remain required; do not replace them with blanket success.

The app also embeds /tmp/pocket-shader-aot/PocketShaderProbe.framework and links
/tmp/pocket-shader-bridge/ios/libPocketShaderBridge.a. Reproduction/source locks
are in Tools/MaxwellShaderCompiler and Tools/ShaderBridge; build them before
XcodeGen/app compilation. NativeAOT runtime packs 10.0.14 use scratch-only caches;
keep certificate generation disabled and source/SDK verification intact.
Core/GraphicsDiagnostic.m runs only original fixed synthetic fixtures. The Duo
validated Maxwell EXIT->SPIRV->MSL->GPU completion and a separate SPIRV store
fixture with a verified buffer value/guards; runtime dynamic-code flags are 0.
That is not guest-originated rendering or game shader compatibility. Preserve
resource active/declared distinctions, completion deadlines, app license notices,
and graphicsImplemented=false until actual game graphics are connected.
Do not feed arbitrary imported shaders into the diagnostic facade. Real state
snapshots/bindings, compiler resource budgets and guest NVN calls are not present.

The Vulkan portability work embeds /tmp/pocket-vulkan-backend/build/ios/PocketVulkan.framework.
Build Tools/VulkanBackend before Xcode. Its fixed original SPIR-V diagnostic is
separate from game rendering. Keep MoltenVK's SPIRV-Cross symbols hidden inside
the framework and preserve all component notices. Debug symbols stay outside
the embedded framework. Do not claim Vulkan performance gains from these tests.
Stage09 passed the actual Duo Vulkan dispatch with value/guards/fence verified,
alongside the direct Metal tests. Stage17 also validated a separate original
interpreted NVN device-create/query/finalize fixture:48 instructions,14 dispatch
events,7 actual compute limits,guards/state/cleanup verified. This is not a call made by the game. Original NRO
regression stage03 also passed. Stage19 validated bounded real Vulkan buffer allocation, guarded host copies
and cleanup (no GPU submission in that fixture). The app links the independent fixture archive
/tmp/pocket-guest-diagnostic/libPocketGuestVulkanDiagnostic.a.

App/SwitchRomFS.swift implements lazy IVFC verification and bounded read-only
RomFS lookup; withRomFS invalidates storage when coordinated source access ends.
No extracted game files are written. The narrow attached-provider FSP/IStorage
path is implemented; actual guest usage has not yet been reached.
Actual Duo stage09 indexed 3,075 files and verified a 4,096-byte asset read.
Core/SwitchStorage.h and App/SwitchStorage.swift provide bounded atomic verified
raw-image reads for future IStorage integration; 65 original storage assertions
passed. The provider must outlive the process inside withRomFS coordinated access.
Run scripts/test_romfs.sh and test_switch_executable.sh for affected changes.
Never bypass hashes, permit escaped filesystem handles, or log asset names/data.

Tools/GuestGraphics resolves the NVN bootstrap symbol using checked guest reads,
validated on the Duo in stage10. Build scripts/test_guest_graphics.sh --ios before
Xcode; project links /tmp/pocket-guest-graphics/libPocketGuestSymbols.a. CPU
breakpoints stop before guest instructions; checked set_pc never calls native
code. Future NVN dispatch must bound zero-step trap events as well as instructions,
validate return PCs, and stop unsupported calls rather than return false success.
Original NVN stubs use a separate visible GeneratedCode (0x14), RX map. Never
label them NSO Code (3): actual rtld treated that as another NSO and faulted.
The corrected type passed stage12. No game headers or NSO bytes are patched.
NPDM ACI/ACID core/priority ranges now derive optional emulated capability masks;
missing descriptors do not get invented defaults. The requested system-resource
size now has real Core/SwitchResources backing. Its first page stores the actual
map metadata. GetInfo16/17 report capacity/assigned pages; explicit normal-application
GetInfo6/7 add full capacity to21/22. Preserve aggregate budget/gap accounting.
This is emulator metadata storage, not hardware page tables or host RSS.


The user explicitly wants real XCI/NSP games, including base
and update handling. The installed iPhone app retains the NRO milestone below.
`Tools/SwitchContent` and `scripts/switch_native.py` develop a separate Mac-side
path using pinned NX2Apple/nxnative: metadata inspection, update matching, and
preparing a per-game signed native build. This is not a general dynamic emulator
and does not establish App Store viability. Keep that distinction visible.
Use a separate native-game bundle ID; preserve the installed Pocket Lab app/data.
Zelda 1.4.3 is installed as `com.dell.pocketlab.native.0100f2c0115b6000` on the
Duo. Its cave scene, touch-driven menu open/close and stick movement are verified
with captures and guest HID recordings. This is limited gameplay validation;
save/reload, broader traversal and arbitrary XCI/NSP imports remain unverified.
See `Validation/switch-native.md` before restarting preparation or device tests.
No keys, game data or generated game code belong in this repository or logs.
Do not weaken upstream integrity checks or compiler fingerprint checks. The
Metal selector executes the actual matching installed compiler; it must never
forge version output. Build artifacts stay outside the repo. Check disk space
before large iCloud hydration/extraction and preserve source game files.

- The demo is original source in `Guest/brick.c`; `Guest/hello_horizon.S` is an
  original freestanding NRO using the real Switch homebrew ABI. No Nintendo files or keys.
- In the existing NRO app, all guest gameplay and framebuffer drawing must
  execute in `Core/Interpreter.c`; do not add native execution to that interpreter.
- The separate native-game development path prepares game code on the Mac and
  embeds it in a signed app using the reference pipeline. Do not add runtime
  JIT/executable-memory workarounds to Pocket Lab.
- The private `SVC #0xF001` ABI is not a Horizon OS service implementation.
- Imported NROs never get that private ABI. Unsupported instructions/services
  must fault with a diagnostic; never report success for an unimplemented service.
- `Core/NROLoader.c` validates NROs; NRO mode uses a bounded 64 MiB virtual
  address space with guest page permissions, not native executable mappings.
- Import via Apple's Files picker with security-scoped, coordinated reads.
  Copy selected files into the local library by content hash; preserve originals.
- Run imported programs on the worker queue with cancellation and the execution
  budget. Keep file size, pointer, segment, and syscall bounds checks intact.
- `scripts/build_guest.py` uses installed Xcode clang and refuses relocations.
- Validate execution against native compilation of the guest, including complete
  framebuffer comparisons, before extending the instruction set.
- Run `zsh scripts/test_homebrew.sh` for native ARM64 instruction comparisons,
  NRO execution, malformed inputs, and sanitizer checks. An optional argument
  supplies a third-party NRO for compatibility diagnostics, not a success claim.
- Use `project.yml` and installed XcodeGen to update the Xcode project.
- Build products and test logs go outside the project in `/tmp/pocket-lab-build` or `/tmp/pocket-lab-homebrew`.
- App bundle ID: `com.dell.pocketlab`. Use the user's existing signing team.
- Gameplay targets the unfolded iPhone Duo: landscape-locked, full-width aspect
  fit, overlay controls. The entire app, including launch, library and file detail,
  stays landscape. Keep Info.plist and delegate masks consistent; do not rotate
  on modal entry/exit. On iOS27 use scene orientation
  callbacks; verify the actual window rather than XCUIApplication's outer frame.
- Duo menus use native NavigationStack toolbars with titled symbols; preserve
  system status visibility and asymmetric safe areas. Collapse the import panel
  by available width on the cover display; keyboard height must not switch modes.
  Keep detail/debug text inside explicitly padded cards, including large text.
  LayoutJourneyTests covers reachable content, spacing and native import taps.
- `Core/HorizonKernel.inc`, `Core/HorizonIPC.inc`, and `Core/Arm64Extras.inc` are
  compiled through Interpreter.c. New services must implement state/validation,
  not blanket success. `pl_set_buttons` queues input edges until guest polling.
- Run scripts/test_kernel.sh, test_horizon.sh, test_arm64_extra.sh, test_formats.sh
  for affected changes. scripts/test_game.sh compares the pinned original NRO's
  complete RGB frames with independently compiled original source and checks exit.
- See Validation/game.md for source hashes, device evidence and known limits.
  Format recognition of NSP/XCI is not loading/execution support.
- Never publish to App Store Connect or distribute the app without a new request.

Tools/HorizonIPC implements bounded CMIF/TIPC parsing and explicit reply encoding;
585,039 sanitizer/public-libnx codec assertions passed. Build its archive before
Xcode; project links /tmp/pocket-horizon-ipc/ios/libPocketHorizonIPC.a. TIPC is
confirmed by actual Castlevania stage18; SM initialization passed stage19.
Service-name diagnostics return only static public labels, never guest strings.
LM objects/log sink and DMB passed stage23. Shared CPU contexts/atomic writev
passed22,832 CPU checks, though the game had no scheduled secondary threads at that stage.
Vulkan transfers passed stage21 on the phone with original synthetic data.
FSP/provider IPC plus resource integration passed7,973 process assertions;
App attachment, resource arena and Vulkan retirement migration passed stage24/25.
Atomic map-missing/unmap/check-range CPU APIs passed99,824 checks; combined
Process mapping checks passed9,201, and actual mapping passed stage26.
No guest storage read or game NVN call has been reached. The provider must outlive
process destruction inside withRomFS. Vulkan uses v2 backend, worker handoff and
a bounded global retirement collector; never drop pending ownership.
Do not conflate host checks with installed phone support.

The original Vulkan pool diagnostic passed stage28 on the actual Duo (upload,
GPU fill/fence, invalidate, guarded values and zero retained resources). Link
/tmp/pocket-guest-pools/libPocketGuestPools.a and
/tmp/pocket-guest-pool-diagnostic/libPocketGuestPoolDiagnostic.a; both have explicit
library search paths. Pool diagnostics are synthetic checked-memory operations,
not actual game NVN calls. Stage30 validates interpreted pool dispatch on the Duo:
78 original instructions/26 events, real upload/fill/fence/invalidate, all guards
and cleanup checked, zero retained resources. App GPU calls and retirement polls
share one serial worker. No actual game GPU call has been reached.
The events helper and copied-handle codec feed the bounded event/thread wait
scheduler. Mutex arbitration is implemented in the later slices described above. Keep explicit unknown-service stops.

Stage35 validates actual interpreted Vulkan transfers on the Duo:282 original
instructions/94 events/3 commands, real recording/submission/fence/invalidate,
values/guards and complete zero-resource cleanup. App uses the explicit transfer
opt-in (including checked pools) on the actual game dispatcher. These methods
are available but no game call has reached them. All physical overlap guards
include locked alias source views and reject unknown backing. New archives:
/tmp/pocket-guest-transfers/libPocketGuestTransfers.a,
/tmp/pocket-guest-graphics/libPocketGuestVulkanTransfers.a,
/tmp/pocket-guest-diagnostic/libPocketGuestTransferDiagnostic.a.
The stage35 Vulkan framework SHA starts988dabfa; matched promotion manifest is
/tmp/pocket-guest-transfer-queue/canonical-promotion.json. All eight App GPU
checks share the graphics worker with retirement. Submit preparation may modify
bounded caller queue RAM before a zero-token failure, but cannot commit pending
job/counter state. Nonzero jobs remain owned even on backend error.

NRO regression06 passed after stage35 (real original downloaded game, framebuffer,
controls and layout). Its library index still matches all5 pre-runtime entries.
Stage36 installed the canonical thread and integer-FP implementation. All9 App
GPU checks passed, including the cold-first compiled Maxwell→SPIR-V→Vulkan
fixture:14 cases/fences,guards,resources0→0. It uses actual compiler output and
remains synthetic, not game graphics. Compute host11,370 and fixture host88
assertions also passed. Matched manifests are in /tmp/pocket-thread-staging,
/tmp/pocket-vixl-probe/integer-fp-staging and /tmp/pocket-vulkan-compute-staging.
The stage36 Vulkan framework SHA starts06f93a1a; compiler starts0c55cd2d.
Compiler pending resources stay owned and are reaped on the next diagnostic.
Strengthened cancel02 passed during actual loader execution using Settings.
No game frame, asset IPC read or guest NVN call yet; graphicsImplemented=false.
App affinity-request telemetry and actual affinity mutation are installed37.
Two executed threads are required by the UI gate (passed38/39). General
guest shader/program support remains staged separately, not connected to games.

Build40 installed: App reports base/effective priority separately and safe mutex
counters. No opaque tag is persisted. The generic blocked message makes no claim
about the reason no thread is runnable. Guest compute adapter source/test work is
in /tmp/pocket-guest-compute-staging; its first206host assertions and iOS compile
passed but independent review/final freeze remain pending. It uses reciprocal
program/pool guards, fixing the earlier capture-owner callback cycle. No game
NVN compute command or captured game shader is enabled yet.

Build43/45 installed explicit begin_foreground_launch and queue counters.
For build46, the matched NVN compute/compiler promotion is installed in canonical
sources/artifacts: /tmp/pocket-nvn-compute-staging/canonical-promotion.json.
The actual game dispatcher now opts into pl_guest_dispatch_enable_vulkan_compute,
which includes existing pools/transfers. New archives are in project.yml;
GuestComputeOwner already includes GuestPrograms (do not link it twice).
The restricted scalar compiler admits actual captured code, then revalidates
live resources before emitting SPIR-V; unsupported shader features stop.
Host gates:16,174 interpreted compute,7,512 backend,6,521 transfer,44 diagnostic;
compiler853,611+20,365,499. Device validation passed46. Diagnostic uses original
synthetic shaders; no game frame, game shader or game NVN call is established.
