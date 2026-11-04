# Original captured-program hardware diagnostic

`satchel_guest_captured_program_diagnostic` is a bounded original-fixture check for the
actual checked guest-memory capture → source admission → live-target ABI2 compiler
→ independently typed Vulkan pipeline → offscreen draw/readback path. It is ready
for root-owned device integration; it has not executed on the phone yet.

It initializes a real common guest Program and pool through checked CPU memory,
then captures two original VS/FS pairs, with a changed varying location and fragment
swizzle. Two first-vertex colors per pair produce four real draw/fence/readback
checks. Shader modules come from the actual on-device compiler output. Host facts
come from the actual device/target adapter. Raster state is explicitly ORIGINAL
test state; it is not observed game state. This diagnostic does not execute guest
instructions, call an interpreted NVN dispatcher, acquire a window, or display a
frame. Existing game state restrictions and all prior diagnostics are unchanged.

The private CPU contains260KiB of bounded guest mappings and never executes or
escapes to callbacks. One64KiB mirrored pool, a16x16RGBA8 target,96-byte vertex and
1408-byte guarded readback buffers are charged by existing Vulkan registries.
Both complete wrapper/SPH/code/C1 spans stay retained through the real GPU fence.
Capture copies borrowed controls. Common identity/pool physical guards are active.
The returned source-unused host6/state8 values are proofs, not observed defaults.

## API and expected report

Header: `GuestCapturedProgramDiagnostic.h`.
Return type is a status enum, **not** a boolean:0OK,1FAILED,2CANCELLED,3BUSY,4INVALID.
The output is committed once an owned invocation ends. Null/misaligned output or
concurrent/reentrant BUSY returns without modifying output. Run on the same serial
graphics worker as every GPU diagnostic, game dispatch and retirement operation.
A pure cancellation callback may read a token/deadline only.

A successful hardware check must require all of the following together:

- Return status0, report.status0, version1, matching size and stage10.
-2source admissions,2compiled pairs,2real pipelines,4submitted draws,4completed fences.
-4actual pre-submit cancellations with no job;4pending-fence cancellation checks
  retaining the existing job before actual completion.
- Source rebound, pixel values, byte guards and private CPU register state verified.
- Program/pool/device finalized, private CPU destroyed, exact baseline/final device,
  buffer, job, target, pipeline and allocated-byte counts matched; no retained owner.

BUSY is not completion. It can have resources_retained0 if a second nonblocking
cleanup attempt succeeds before return; never reinterpret it as a passed fixture.
Global baseline counts may be nonzero: the host gate also preserves a separate
live external device, buffer and target during a complete diagnostic.

## Cancellation and retirement

The cooperative deadline is10seconds, per-compile bound1second and per-fence wait
at most2.5seconds. Driver creation cannot be forcibly interrupted. A failed first
monotonic clock read latches cancellation before allocation; later recovery cannot
turn the finite deadline into an unbounded wait.

One bounded private C owner holds every nonzero job/resource/ref token, including
submit failures with a token. The private CPU is destroyed before GPU cleanup.
No borrowed callback/context/CPU survives. Pipeline/buffer/target destruction waits
for actual fence completion; source references release before programs/pools and
device. Any cleanup failure keeps remaining ownership for retry.

A later invocation first polls and drains the previous owner without starting a
second fixture while it is retained. For cleanup-only asynchronous polling, call
this same function with a permanent pure callback that always returns1 and a null
context, on the graphics worker. A drained call returns CANCELLED rather than
starting new work. Stop polling once resources_retained0; BUSY/CANCELLED are not
successful diagnostics. Bound polling (for example50attempts/100ms); if still
retained, leave the bounded C owner intact and retry at the next graphics task.
Never retain an expired runtime cancellation token or call from multiple workers.

## Build and link

Run the repository recipe with explicit validated inputs:

```sh
zsh scripts/test_captured_diagnostic.sh \
  --compiler-build /tmp/satchel-guest-captured-compiler-final \
  --owner-build /tmp/satchel-guest-captured-owner-final2 \
  --renderer-build /tmp/satchel-captured-host-renderer-build \
  --cpu-archive /tmp/satchel-guest-captured-cpu-reference/libSatchelCPUProbe.a \
  --cpu-header /tmp/satchel-guest-captured-cpu-reference/CPUProbe.h \
  --ios --out /tmp/new-captured-diagnostic-build
```

A relocated overlay can additionally pass `--source-root <canonical-repo>`.
Implementation source is repository-owned; producer paths are explicit artifact
inputs, validated by exact source/artifact manifests. No downloads or shared build
mutation. The copied CPU dependency preserves the exact comparison-checkpoint gate
while root independently promotes new CPUs with the same public ABI.

The iOS17 archive is `libSatchelGuestCapturedProgramDiagnostic.a`. It exports only
`_satchel_guest_captured_program_diagnostic`; import checks reject all test/fault symbols.
Root can place it at `/tmp/satchel-guest-diagnostic/libSatchelGuestCapturedProgramDiagnostic.a`.
It also needs the already validated common Program/pool/CPU libraries, new
`libSatchelGuestCapturedPrograms.a`, `libSatchelCapturedHostAdapter.a`, the canonical
SatchelCapturedFlat framework and SatchelVulkan. Do not link a second copy of
GuestPrograms if the existing GuestComputeOwner archive already contains it.
Root owns App/bridge/project/embed changes and physical-device execution.

## Evidence

Final relocated gate: `/tmp/satchel-captured-diagnostic-final2/validation-manifest.json`.
Normal host gate94 assertions covers four completed4-draw journeys, reentry,
misalignment, cancellation and nonzero-baseline preservation. Separate fault-only
gate (27–28 assertions in observed runs; one check per nonblocking retry) holds a real submitted job/fence, then forces pool cleanup to
retry after CPU destruction; it also tests first-clock-failure recovery. Those
substitutions exist only in a separate test object, never the production archive.
The iOS17 object has exactly the one public export. The host sanitizer object
also has the pinned compiler-generated ASan registration symbol, explicitly
allowed only for that host check. No phone/game/display claim. Counts are assertions, not independent
compatibility scenarios; per-pixel/guard comparisons also run inside the diagnostic.

Independent read-only review: `captured-diagnostic-independent-review.json`.
The reviewer found the initial-clock sentinel edge; it was fixed and covered by
the first-failure/recovery test before freeze. Earlier host tests incorrectly used
UINT32_MAX (backend-busy sentinel) as a live-job count, then expected an immediate
new success while an actual cancelled fence could remain pending. They now treat
BUSY as unknown, use the real outside-backend cancellation checkpoint, and drain
retained ownership explicitly. No check was removed or weakened.
