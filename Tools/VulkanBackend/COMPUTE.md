# Original Maxwell compute pipeline

Implemented and validated on the Mac; arm64 iOS framework and diagnostic archive
compiled. Matched artifacts promoted with parent authorization; stage36 passed on the actual Duo. This is a genuine
GPU pipeline for one independently encoded original Maxwell program. It is not
an NVN compute command, a game shader, or a rendered game frame.

`VulkanCompute.h` adds a versioned limits snapshot without changing the existing
`SatchelGuestDeviceCaps` ABI. It queries actual uniform/storage range, alignment and
per-stage/set descriptor limits. Buffers explicitly request UNIFORM usage (bit8)
when needed. The previous pool profile remains usage7. The fixed program requires
one64KiB UBO, one16..4096-byte SSBO, set0, entry`main`, local/group1×1×1 and no
optional Vulkan feature. Two distinct binding numbers in1..7 are permitted.
Unused support binding0 is declared by the compiler but is statically inactive;
it is neither fabricated into an active binding nor supplied a dummy resource.

`satchel_vulkan_compute_pipeline_create_original` copies and validates the caller's
actual SPIR-V. Its admission gate accepts only the exact reviewed original output
shape, with two Binding operands varied to match the explicit profile. Every
other byte must match `ComputeOriginal.inc`. That reference is only an admission
comparison: the Vulkan shader module consumes the copied caller output. No
precomputed shader or result replaces the runtime compiler's output. Provenance,
initial and cold-fixed facade hashes, source pin and the1208-byte SPIR-V hash are
in `ComputeOriginal.json`, also bundled in framework notices. General shader
admission needs a separate reviewed compiler/state/resource-budget contract.

Persistent pipeline tokens own the real shader module, descriptor layout,
pipeline layout and pipeline, parented to a live device. They occupy a distinct,
monotonic token range and never expose host pointers. There are at most8 pipelines,
4 submitted jobs globally and1 submitted job per device. Each compute job owns
an immutable descriptor set/pool and command pool/fence, retaining both buffers
and its pipeline. Full descriptor ranges, usage, parent identity and queried
alignment are checked before recording. HOST/TRANSFER/COMPUTE→COMPUTE and
COMPUTE→HOST/TRANSFER/COMPUTE barriers provide actual visibility. Descriptor and
pipeline destruction follows a successful fence, not an assumed completion.
Any nonzero submit token remains owned on an API error. Host access and destruction
are BUSY while retained; cancellation/timeout never drops ownership. Actual device
retirement drain uses the same completion path as explicit waits. Buffer allocation
limits still charge Vulkan's padded memory requirements. Pipeline/descriptor
object counts are bounded; their driver-private heap use is not measured.

Pinned MoltenVK1.4.2 is configured with `MVK_CONFIG_FAST_MATH_ENABLED=0` (NEVER),
verified in its installed configuration and Metal compilation code. The reported
`precise_math_requested` field describes the requested configuration; actual
bit-preservation is independently tested with GPU readback. The shader's declared
UBO type is float4[4096], although its loads supply integer bits. Therefore the
original fixture binds a real64KiB block, copies its exact32-byte CB at offset0,
and explicitly zeroes the rest. This zero-padding is original fixture data,
not a default for unknown game constant buffers.

`Tools/GuestGraphics/GuestComputeDiagnostic.h` exposes the phone-ready fixed
journey: independently encode64-byte LDC/IADD/STG/EXIT Maxwell input; call the
production NativeAOT compiler cold; consume its returned SPIR-V; create the
pipeline; update UBO operands; dispatch and read actual GPU results. Fourteen
operand pairs cover integer overflow, qNaN/sNaN payloads, infinities and subnormal
bit patterns. The same pipeline is reused with changing parameters. Every result
must equal uint32 modular addition, with SSBO guards and the entire UBO unchanged.
The report exports numeric status/counts only, never code, inputs, resource tokens
or shader payloads.

The whole diagnostic belongs on the App's serial graphics worker alongside
retirement polling. Its private bounded owner retains only device/pipeline/buffer/
job tokens on cancellation or timeout; no CPU, provider or callback context escapes.
The next invocation reaps completed work or returns BUSY. Success is status0,
stage10,14 cases and14 fences,1208 SPIR-V bytes,2 active/3 declared buffers,
zero runtime dynamic-code flags, and all cleanup/guard booleans true. A10s
cooperative deadline surrounds work; individual waits are bounded by5s. Synchronous
compiler and driver pipeline creation cannot be forcibly interrupted.

A fresh production compile initially aborted at `RhFailFast`, before any Vulkan
operation. The compiler owner fixed the NativeAOT GS-cookie lazy-initialization
ordering using a tiny unmanaged entry and non-inlined managed worker, then gated
fresh processes without test exports or warm-up. Both this backend test and the
phone diagnostic call the production export first. They require the matched
cold-fixed compiler framework, not the older staged fixture framework.

Validation on2026-10-31:

- 11,370 ASan/UBSan assertions for the backend: all14 GPU operand pairs, all42
  binding pairs with fresh compiler output, every single-bit SPIR-V mutation and
  truncation, descriptor usage/range/offset/parent rejection, immutable snapshots,
  cancellation, pipeline/job limits, stale tokens and real completion cleanup.
- 88 ASan/UBSan assertions for the phone diagnostic on Mac: complete cold compile
  through GPU execution, multiple cancellation stages, pending retention/reap,
  reentrancy rejection, unrelated-job isolation, admission exhaustion and cleanup.
- Existing affected backend gates:11 initial compute,115 device,64,400 buffer,
 136 single-transfer and254 transfer-batch assertions. Buffer counts include
  per-byte comparisons; these are assertion counts, not unique scenarios.
- Arm64 iOS framework compiled with exactly26 public C exports; existing component
  notices plus original admission provenance bundled. dSYM remains outside the
  framework. Diagnostic static archive compiled for iOS. Parent-confirmed stage36 device run passed:14/14 GPU cases and fences, all
  value/guard/cleanup flags, zero retained resources. All9 App GPU checks passed
  together; this production compile ran before the older shader diagnostics.

The wrapper and original callers are sanitizer-instrumented. Prebuilt MoltenVK
and NativeAOT runtime internals are not. No additional dependency was introduced.

Reproduce:

```sh
zsh scripts/test_vulkan_backend.sh --ios --staging --output-root /tmp/satchel-vulkan-compute-staging/framework
zsh scripts/test_vulkan_compute.sh
zsh scripts/test_guest_compute_diagnostic.sh
```

The last two scripts default to the cold-fixed production host dylib in
`/tmp/satchel-shader-cold-staging/repro/host-production-publish`; override only with
`SATCHEL_SHADER_HOST_DIR` pointing to a compatible validated build. Staged outputs:

- `/tmp/satchel-vulkan-compute-staging/framework/ios/SatchelVulkan.framework`
- `/tmp/satchel-vulkan-compute-staging/diagnostic/libSatchelGuestComputeDiagnostic.a`
- `/tmp/satchel-shader-cold-staging/SatchelShaderProbe.framework`
- `/tmp/satchel-vulkan-compute-staging/compute-manifest.json` (matched hashes/exports)

Matched artifacts were promoted with parent authorization on2026-10-31. Previous
canonical artifacts are preserved in `/tmp/satchel-graphics-before-compute-joxjhhq8`.
`/tmp/satchel-vulkan-compute-staging/canonical-promotion.json` records verified
canonical locations, hashes and all26 exports/files. Vulkan binary hash begins
`06f93a1a`, cold-fixed compiler `0c55cd2d`, diagnostic archive `c46f8153`.
The parent validated phone36 with this matched set; subsequent general-program
work stays in separate scratch staging and does not change these artifacts.
The earlier transfer framework/guest-queue milestone was subsequently promoted
and validated on the Duo:282 original interpreted instructions,94 NVN dispatch
events and3 real transfer commands, all guards/fences/cleanup passed. That is
separate from this new compute path and still not a game-originated graphics call.

Primary API references, checked against the installed pinned headers/source:
[Vulkan buffer descriptors](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBufferInfo.html),
[descriptor writes](https://docs.vulkan.org/refpages/latest/refpages/source/VkWriteDescriptorSet.html),
[compute pipeline creation](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateComputePipelines.html),
[dispatch](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatch.html),
[SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html),
[MoltenVK1.4.2 configuration](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md),
and the compiler owner's [upstream NativeAOT fix reference](https://github.com/dotnet/runtime/pull/136265).
