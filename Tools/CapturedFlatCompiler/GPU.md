# Live original compiler-to-GPU gate

Implemented host-only validation of actual production NativeAOT compilation,
independent typed SPIR-V admission, real Vulkan pipeline creation and GPU output.
The source programs are independently encoded originals. No precompiled SPIR-V
is read or substituted. This is not an imported-game shader or presentation test.

Build the compiler with its existing `build.py` recipe and the renderer with
`scripts/test_vulkan_flat.sh --ios`. Then run:

```
zsh scripts/test_captured_flat_gpu.sh \
  --compiler-build /tmp/<validated-compiler-output> \
  --renderer-build /tmp/<validated-flat-renderer-output> \
  --output /tmp/<new-live-gpu-output>
```

`--renderer-source-root` permits a separately relocated reviewed renderer tree;
it defaults to the current repository. Both compiled inputs must have successful
validation manifests. The recipe verifies the production dylib/object, consumed
source locks, reviewed API/schema hashes and official MoltenVK dependency hash.
It accepts only a clean production backend object, not the fault-injection one.
It changes neither compiler source-lock nor source implementation. All generated
outputs stay in a marked owned directory under `/tmp`. No downloads are needed.

The first managed operation is the production C export while Vulkan is already
live.33 successful compilations include32 original pairs (varying locations0–7
and four fragment swizzles). Each pair creates a pipeline and draws twice with
different first-vertex colors.64 actual fences/readbacks validate all pixels and
row/buffer guards. Failed/cancelled/deadline compiler calls preserve output;
cancelled GPU waits retain resources until the real fence; every registry returns
to zero.83,040 assertions include pixel/guard comparisons, not independent cases.

State and source fixtures are explicit originals. Physical BGRA color-attachment
support and storage alignment come from the actual live Vulkan device; current
and admitted targets are actual RGBA8_UNORM. Other compiler host booleans are an
explicit fixed original-test profile, **not** a complete captured-game capability
binding. Optional mechanisms are disabled and the profile has no resources,
front-facing input, scaled vertex attributes or depth conversion. The pinned
source shows FrontFacingBug only affects FrontFacing reads; VectorIndexingBug
transforms dynamic constant-buffer indices; scaled-format choice leaves Float
attributes FP32; DepthClipControl is consulted only for MinusOneToOne conversion.
These observations explain the original fixture, but do not create a reusable
source-level unused-proof ABI or authorize guessing facts for other programs.

No game compatibility, on-phone execution, VI/native-window/acquire, presented
frame or production readiness is claimed. This gate leaves those owners untouched.
The existing pinned translator and MoltenVK full licenses/notices continue to
apply; this adds no dependency or third-party source.
