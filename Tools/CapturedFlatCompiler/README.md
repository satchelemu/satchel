# Captured flat-pair compiler: isolated review checkpoint

Implemented: a C-callable NativeAOT facade that translates the supplied copied
Maxwell SPH/code pair through the pinned Ryujinx translator. It never substitutes
an original fixture for supplied code. This is a narrow admission profile, not a
general Switch shader compiler, game renderer, or phone graphics result.
Canonical App/framework/renderer sources are unchanged.

## Inputs and state

`CapturedFlat.h` is the complete version1 arm64 C ABI. The caller provides two
80-byte SPHs and separate code spans (8-byte aligned lengths, 8..1024 bytes),
actual emulator-owned graphics-state facts, and explicit host facts. Pointers
must name readable host storage, not guest virtual addresses. The entire source
is copied before callbacks/translation; no source pointer escapes the call.

Admission supports VertexB + fragment, one flat full-float4 varying at locations
0..7, float vertex attributes at locations0..7, Position and RGBA0 stores,
immediate/register moves, direct attribute reads/stores, flat IPA and EXIT.
There are at most64 semantic instructions/stage. Undefined register reads,
resources, calls, control flow, geometry/tessellation, unsupported header bits
and non-flat interpolation refuse. Scheduling words are ignored as in the
pinned decoder; they cannot encode semantic operations. `StrictSource.cs`
validates the actual bytes, including matching declared varyings.

All eight raster facts must be known (`known=255`), including actual YNegate0.
The twelve extra fields must be known (`extra_known=4095`). The versioned profile
admits triangles, native VS/FS, FIRST provoking, float inputs/output0, disabled
alpha testing/coverage/dither, transform feedback, draw-parameter CB, dual-source
blend, earlyZ and primitive-ID halving, and fixed PointSize1. The explicit fixed
point-size fact is checked even though triangle rasterization ignores it.
Tessellation-only settings and fixed offline geometry capture are disabled by
this owned native VS/FS compilation mode, not guessed guest state. Unexpected
resource accesses/binding creation throw rather than inherit permissive defaults.

Every host boolean has a known bit; unknown is never automatically false. These
24 bits mix physical/implementation properties and enabled-device-profile
facts. The caller must establish each from its actual backend/device or an
explicitly disabled enabled feature; the original test's all-disabled optional
features are SYNTHETIC and are not an integration recipe. In particular precision
and workaround flags cannot be inferred from a device brand or made false just
to obtain accepted output. Missing facts must stop integration.

The BGRA query is narrowly tied to this resource-free native-color profile:
require an actual physical B8G8R8A8_UNORM optimal COLOR_ATTACHMENT feature plus
both admitted/current target format RGBA8. The sole pinned query consumer is
EmitterContext's fragment R/B support-buffer remap. This avoids that remap for
native RGBA; it does not claim implemented BGRA targets or texture support.
The renderer independently exposes physical and implemented-format facts.

## Bounds, cancellation and ownership

One non-reentrant atomic gate returns BUSY for concurrent/reentrant calls. The
tiny unmanaged entry includes all managed operations in its exception boundary
and calls a non-inlined managed implementation; no test warm-up is required.
Only a whole paired output is committed on success. Every failure leaves it
unchanged, including failed fragment admission after a valid vertex input.
Outputs cannot overlap the input structure or either source span.

The pure optional callback is borrowed only during the synchronous operation;
it must return promptly, must not mutate external resources, and cannot be
retained. Cancellation and a real monotonic timeout (1..1e9 nanoseconds after
bounded input copying) are checked at work charges/stage boundaries. The
original reentry/source-mutation tests deliberately violate the pure callback
contract to test defensive snapshot/gate behavior, not to authorize such callers.
A callback cannot be forcibly preempted. There is no hard realtime guarantee.

Existing pool/IR/operand/block/code-fetch/SPIR-V/AST limits remain, with a total
logical work-event limit1..65536. The private copy additionally instruments142
loop bodies in19 explicit translator/generator files and12 source/proof loops;
`upstream/.../Ryujinx.Graphics.Shader/loop-sites.json` and root `loop-sites.json`
list them. Fixed static instruction-table initialization is not cancellably
instrumented, and cleanup/finally paths remain unconditional. BCL collection
operations operate on bounded admitted structures but are not instruction-counted.
These bounds apply only to this admitted profile; they are not a proof for broad
Ryujinx shaders or arbitrary reachable optional passes. Cancellation does not
poison static constructors or leave generator pool registrations installed.

## Outputs and independent validation

Each module is the actual SPIR-V byte stream with at most4096 words and ID bound
at most256, padded with zeros in the fixed C result. The module layout matches
`SatchelVulkanFlatModule`. Per-stage facts separately report actual source semantic
operations/attribute/varying masks and SPIR-V declared/active uniforms/ID bound.
The included SPIR-V proof is capability/resource admission, NOT complete type,
access-chain or renderer validation. The renderer must independently validate
both modules and the actual draw/capability state before creating a pipeline.
A compile result alone never establishes pipeline creation or draw success.

## Reproduce

Use installed .NET10.0.403, existing10.0.14 NativeAOT packs, Xcode clang and
SPIRV-Tools. No package download, global SDK/workload installation, or new
third-party dependency is used. The NuGet configuration has no feeds and
ASP.NET certificate generation is explicitly disabled.

```
python3 build.py --renderer-root /tmp/satchel-vulkan-flat-caps-staging
```

The default output is a newly created /tmp/satchel-captured-flat-build-* directory
(or explicit --out); source directories receive no build artifacts.
The recipe validates `source-lock.json`, builds host/iOS17 artifacts, runs four
fresh-process production-first C gates, hostile original-source mutation checks,
independent typed renderer schema checks and spirv-val Vulkan1.1 checks. It emits
an unsigned `SatchelCapturedFlat.framework` and a hash-bound validation manifest.
Renderer schema test dependencies are explicit, hash-checked files specified in
`renderer-reference-lock.json`, not an implicit build of another agent's work.
Native C callers and the independent schema checker use ASan/UBSan; NativeAOT
managed code uses runtime bounds checks, not sanitizer instrumentation. Leak
detection is disabled for the persistent NativeAOT runtime, so this is not a
whole-runtime leak check.
No GPU/device/App test occurs in this recipe. The framework is not installed,
embedded, signed, or promoted by this task.

## Provenance and limits

The private translator copy derives from frozen strict-visits checkpoint and
NX2Apple8227f80f9b381c680611303319656c4e0c806a50. The frozen input manifest records
pre-change bytes; `upstream-review.patch` records the additional source delta.
`pinned-upstream-lock.json` verifies every original against its committed blob,
and `pinned-upstream.patch` records the complete delta from the pinned checkout.
Ryujinx/Spv.Generator MIT licenses and .NET runtime notices are in `licenses/`.
Original fixture generation is in Test.c, composed from pinned public decoder
bitfields; no game shader/code, keys or payloads enter these tests. The frozen baseline managed harness is excluded from the new project; its
historic443-check gate is not claimed as a run of this facade.

Preserved initial failures: first C# build rejected capturing a fixed-buffer
Input in a lambda (CS1686; now capture only callback/context/timeout); first
native gate refused the newly explicit offline-geometry setting; a reentrant
Monitor gate incorrectly returned INTERNAL; and a1ms deadline test legitimately
finished before expiry. Assertions were retained with corrected code or a
precise nanosecond timeout ABI, not weakened to accept errors. The early linker
attempt used unsupported GNU `-l:` syntax on Apple ld; the recipe links the
explicit library path. Logs remain in this scratch directory.

## Additive live host-facts ABI2

`CapturedFlatV2.h` preserves the version1 entry and adds explicit queried/policy/
source-unused provenance. Only FrontFacingBug and VectorIndexingBug may remain
unknown and are normalized only after copied strict source admission proves them
unused. The result reports that proof separately. See `HOST_FACTS.md` for the
actual device/target adapter, bounds, reproducible GPU gate and remaining limits.
The framework packages both headers and both production entries. Existing v1
cold/source/GPU tests are retained; no general game compatibility is implied.

## Additive source-only admission

`satchel_captured_flat_admit` returns bounded source requirements, never compiled
modules or observed state. It shares the cold-safe gate/budget and strict immutable
source decoder with unchanged v1/v2. Source-unused YNegate mask0x08 is a proof, not
a guessed false fact; v1/v2 retain their complete known255/extra4095 requirements.
The framework now exports three production entries plus the .NET debug header.
See `../GuestGraphics/CAPTURED_PROGRAMS.md` for exact guest-pool ownership, state
blockers, source provenance and original real-GPU validation.
