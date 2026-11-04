# Typed flat-varying Vulkan raster profile

Implemented additive pipeline admission for caller-supplied bounded SPIR-V pairs,
with independent typed interfaces, constant indices, resource activity and exact
state checks. This does not replace the earlier raster compiler/schema. It is
not a general shader compiler, captured-game gate, window or presentation path.

Run `zsh scripts/test_vulkan_flat.sh --ios --output /tmp/satchel-flat-build`.
The full `scripts/test_nvn_raster.sh --ios` also runs this additive gate and packages
VulkanFlat.h with its four public exports. Builds stay outside the source tree.
Only installed pinned MoltenVK/Xcode and spirv-val are used; no downloads.

The original modules in FlatFixtures were emitted by the pinned Ryujinx translator
for independently encoded VS/FS source. Their provenance manifest records hashes;
they are regression inputs, never replacements for a caller's modules. The
captured-source C facade is a separate integration gate. Counts include mutation,
pixel and guard assertions, not independent scenarios.

Limits:4096 words/stage,256 IDs, one straight-line block, Shader-only capability,
one vec4 flat varying, float32 vec4 vertex attributes, RGBA8_UNORM color target,
fully known state255 with YNegate0, first provoking vertex and no active resources.
Declared inactive support UBO and clip/cull arrays are reported separately.
All active locations require a full16-byte fetch, including overwritten reads.
No pipeline is created until both immutable module copies validate. Existing
bounded pipeline/job/target ownership and real fence cleanup remain in force.

Physical B8G8R8A8 color-attachment support is queried from the live device and
reported separately from implemented formats; only RGBA8 is admitted. Do not infer
original Maxwell state from SPIR-V or turn an output-level unused fact into a
source-level proof. A captured compiler must supply truthful actual state/facts.

Vulkan interface matching excludes interpolation decorations for this monolithic
pipeline: VS without Flat and FS with Flat is valid. The default provoking vertex
is FIRST; LAST is unsupported. Only statically used resources require bindings.
Primary references: https://docs.vulkan.org/spec/latest/chapters/interfaces.html
and https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html .

Existing MoltenVK/Ryujinx/SPIRV-Cross notices and source pins remain in Notices/.
No new dependency is introduced. This source is a device prototype, not a claim
of game compatibility or production readiness.
