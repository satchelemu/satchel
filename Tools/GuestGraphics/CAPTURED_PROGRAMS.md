# Captured guest-program source admission (staged)

Implemented and host-validated: a separate owner captures real checked guest
`NVNshaderData` entries, preserves the current common Program identity, copies
borrowed control bytes, retains the complete GPU-visible code/C1 span, and admits
an actual paired flat-varying source profile with the installed NativeAOT code.
Admission does not compile modules, create a pipeline, or claim a successful
NVN game call. This component is not attached to the game dispatcher.

The later explicit compile operation calls the live-device/target ABI2 adapter.
It publishes modules only after actual translation, independent shader proof,
and a second complete GPU-visible code/C1 comparison. The source API does not
accept host masks, substitute fixture binaries, or fabricate state.

## Reference and ownership

Reference pin: NX2Apple `8227f80f9b381c680611303319656c4e0c806a50`.
`dm/gfx/nvn/include/nvn.h` defines the ordinary16-byte shader-data entry.
`dm/gfx/nvn/hle_nvn.c:636–659` implements ProgramSetShaders and explicitly says
control memory is guaranteed only during that call while code remains in a pool
for program use. We reuse that lifetime distinction, not its host pointer access,
per-title shader-map lookup, or logging. Extended24-byte SetShadersExt is not
implemented by this owner.

The common Program owns its tag/generation; this independent owner never writes
a competing tag. Two stages derive VS1/FS5 from SPH, with NVNmask3. Wrapper/control
magic, extent, C1 offset, stage flags and the original pool-generation checks are
preserved. The full retained wrapper/SPH/instructions/C1 must remain identical in
the GPU mirror, including padding and C1, before every use. Unflushed CPU edits do
not become GPU-visible. CPU/control aliases, common identity changes and pending
GPU access remain checked failures.

Recording references prevent replacement and forget. Successful replacement
moves the previous capture to a bounded retiring slot; failed replacement keeps
the old capture intact. Failed cleanup keeps ownership for retry. Teardown stores
no CPU/provider/cancellation pointer. The composite must release recording refs
only after all submitted jobs and publication have drained, then destroy this
owner before common programs, pools and device. The original pure shared pool
range guard remains composed with the common program owner.

## Source proof and state

`satchel_captured_flat_admit` has the same cold-safe unmanaged-entry/non-inlined
managed-worker pattern, shared gate and WorkBudget as the preserved v1/v2 compiler.
It copies both bounded sources before callbacks, invokes the SAME StrictSource
pair decoder, and publishes requirements atomically. Limit: SPH80 plus1–1024code
bytes per stage,64semantic operations, one full flat varying, current resource-free
profile. Work limit1–65,536, monotonic timeout1–1,000,000,000ns. Owner capture uses
65,536/1second. Both new C request/output must be naturally8-byte aligned.

Success reports host-unused mask0x06 and state-unused mask0x08 (bit3, YNegate).
These are source-level proofs, not observed host or guest values. All consumers
of `YNegateEnabled` in the pinned translator were checked: the sole translation
read is `Instructions/InstEmitAttribute.cs:186`, in fragment PositionY IPA.
StrictSource accepts only user-varying offsets0x80..0xff and the flat SPH map,
rejecting PositionY/FragCoord, FrontFacing, dynamic attributes and resources.
The other occurrences are its stored field, constructor and definitions getter.
The two existing driver-bug proofs are unchanged. Tests include valid original
LDC32, malformed resource encoding, all tested system offsets and failed source
mutations; unsuccessful admission cannot publish any proof.

**Existing v1 and v2 contracts are unchanged.** The new owner compile requires
`SatchelCFState.known==255` and `extra_known==4095`. Current actual guest recordings
produce known247 and no verified provoking-mode fact; they return
`SATCHEL_GCAP_STATE_UNAVAILABLE`. Source-unused Y is not silently converted to an
observed false value. No new dispatcher opt-in, game module, pipeline or frame is
claimed. The complete state used in host GPU tests is explicitly original test
state, while host facts and target ownership come from the actual live backend.

## Next concrete state work

1. Add a separately reviewed source-aware compile entry (leave v1/v2 unchanged).
   It must copy/admit the exact source before granting requested state-unused8,
   normalize only that proven-unobservable fact internally, report proof separately,
   and still require every other actual field. A previous caller-provided admission
   result cannot authorize normalization by itself. Owner generation/full-span
   checks must remain in front of and after translation.
2. Resolve provoking mode from a verified NVN API contract or explicit supported
   setter. The pinned reference lists `nvnCommandBufferSetProvokingVertexMode`
   (`names.txt:532`, `nvn_thunks.S:6988`) only as an unimplemented prototype
   (`nvn_protos.h:538`). Its available public header does not define the enum,
   argument contract or initialization default. No authoritative NVN default was
   established by the current source/web search. Vulkan's FIRST default is not
   evidence that NVN defaults to FIRST. The [Khronos extension design](https://github.com/KhronosGroup/Vulkan-Docs/issues/1527)
   explicitly distinguishes API conventions. Keep the unknown state stopped;
   do not borrow OpenGL, Vulkan, deko3d or Tegra hardware reset defaults.
   The independent public implementation comparison in
   `CAPTURED_PROVOKING_REVIEW.md` confirms the gap: deko3d has its OWN First0/Last1
   and Last default; a TotK runtime log-only stub comments an incompatible NVN
   zero/default relationship but implements no state or conformance. Neither
   establishes NVN argument/default/reset/inheritance semantics. Unknown provoking
   must remain refused; source-unused YNegate cannot replace varying semantics.
3. Populate ABI2's extra state from the actual checked recording/queue/target,
   distinguishing source-proven inapplicable fields from observed values. Existing
   state handles prove several raster facts, but no guessed all-known mask belongs
   in the game adapter. The old exact no-varying shader route is unchanged.

No VI/nativewindow/acquire/present-to-display implementation is added here.

## Validation and reproduction

Build the compiler with `python3 Tools/CapturedFlatCompiler/build.py
--renderer-root <repo> --out /tmp/<new-compiler-build>`. Then run
`zsh scripts/test_captured_programs.sh --compiler-build /tmp/<new-compiler-build>
--renderer-build <validated-flat-renderer-build> --ios --out /tmp/<new-owner-build>`.
The renderer build must expose its validated `backend-production.o` and manifest;
`Tools/VulkanBackend/build_flat.py` is the established producer. Paths and source
roots are explicit arguments, never hidden implementation files. A relocated
source overlay can pass `--cpu-header <canonical-repo>/Tools/CPUProbe/CPUProbe.h`.
The CPU archive is an explicit configurable dependency, checked immutable during
the gate with its exact path/hash and expected public header. No CPU source/archive
is modified or included in this promotion.

The compiler recipe preserves every v1/v2 check, adds all4 admission cold modes,
validates all generated original/mutated modules and emits the exact additive
export/header/notice set. The owner recipe validates its own complete source lock,
the compiler build/source manifest, renderer build/source manifest, scalar manifest
and MoltenVK hash before/after. The renderer object is reused only with its reviewed
manifest and matching sources. It compiles production host+iOS17 owner archives
separately from test fault-injection objects. No precompiled shader substitutes are
used. The ABI2/live-GPU recipes remain available with their exact updated compiler
contract locks; no old tests were deleted or relaxed.

Validated output: `/tmp/satchel-guest-captured-program-validation2`.
- Source admission:1,276 assertions cold-success;1,278 each cold-deadline,
  cold-cancel and cold-invalid. These share cases and are not unique test counts.
- Guest owner:5,879 original ASan/UBSan assertions including byte comparisons;
 4actual captured source pairs,8real GPU draws/fences, changed source/swizzle,
 changed first-vertex color, guards and zero retained resources. No game evidence.
- Existing v1:1,901,395 assertions across4fresh production-entry modes;
 hostile source:6,249,348original/mutated pairs independently typed-validated,
 696modules passed spirv-val Vulkan1.1. Existing ABI2 boundary241 unchanged.
- iOS17 NativeAOT library and13-export owner archive compiled; no device execution.

The first new recipe attempt incorrectly passed iOS deployment17 to the macOS
build and failed with `invalid version number in '--target=arm64-apple-macos17.0'`.
The recipe now applies that option only to iOS; the fresh validation2 run passed.
Earlier test setup supplied an invalid v2 request when testing shared-gate BUSY;
that request was corrected to the existing valid v2 shape, preserving BUSY and
whole-output assertions. No production or existing test weakened to pass.

Independent source review:
`/tmp/satchel-guest-captured-program-independent-review.json` (read-only, no build).
