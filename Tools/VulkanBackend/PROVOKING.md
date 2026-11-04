# Explicit FIRST/LAST flat interpolation

Implemented in the additive `VulkanProvoking.h` API, with original host GPU
validation. This is a renderer primitive, not a claim about NVN enum values,
initialization defaults, captured-game shader state, or displayed game frames.
The original `satchel_vulkan_flat_create` remains FIRST-only and its ABI is unchanged.

`satchel_vulkan_flat_create_provoking` independently copies and validates both modules
using the existing typed, bounded pure VS/flat-FS schema. Explicit renderer LAST
uses an immutable private UINT32 index buffer: triangle `(a,b,c)` is drawn as
`(c,a,b)` through the native FIRST pipeline. The cyclic permutation preserves
winding, primitive order, geometry, and the original vertex fetch indices. The
schema excludes VertexID, shader side effects, resources, extra stages and
non-triangle topologies. This proof does not extend to arbitrary shaders.

Counts are 3..4095 and divisible by three, with firstVertex zero and one instance.
Incomplete triangles are explicitly rejected, so a four-vertex request cannot
fetch a nonexistent fifth vertex. Existing full vec4 vertex-range checks remain.
Mixed FIRST/LAST commands preserve their actual order in one render pass; every
native pipeline still uses FIRST. LOAD does not introduce a clear.

The pinned MoltenVK1.4.2 implementation exposes native LAST through its private
Metal API path. This project does not enable that path or the native extension.
`satchel_vulkan_provoking_capabilities` separately reports actual physical extension
and feature support, native enabled status (zero), and the bounded rotated
profile. The tested host reports physical/native flags0/0/0. These facts are
queried, not device-model assumptions. Allocation can still fail after a positive
capability query.

Each LAST pipeline owns a 16,380-byte index payload. Actual Vulkan memory
requirements (including padding) are charged, at most128KiB per pipeline and
1MiB aggregate, with the existing eight-pipeline registry limit. Required or
preferred dedicated allocation is honored. All mappings are flushed when needed
and unmapped before use. `satchel_vulkan_provoking_stats` exposes the separate private
allocation budget; it is not included in the public buffer registry counters.
Pipeline references retain the allocation through nonzero-job submission errors,
cancellation and real fence completion. Destruction refuses pending ownership;
partial failures and later device teardown release their exact charged bytes.

The captured compiler v1/v2 still requires FIRST and complete state. Its copied
source currently validates provoking state but does not specialize shader output
using it. This backend change supplies no source-state proof and does not relax
that contract. Actual NVN provoking values/default/reset/inheritance remain
unverified, so the game capture boundary continues to stop rather than infer them.

## Reproduce

Use the already installed pinned dependencies, without downloads:

```sh
python3 Tools/VulkanBackend/build_flat.py --ios --output /tmp/satchel-flat-check
zsh scripts/test_vulkan_provoking.sh --renderer-build /tmp/satchel-flat-check --output /tmp/satchel-provoking-check
SATCHEL_CPU_PROBE_ARCHIVE=/path/to/validated/libSatchelCPUProbe.a zsh scripts/test_nvn_raster.sh --ios --output /tmp/satchel-raster-check
```

The flat recipe validates pinned MoltenVK headers/libraries, original translator
module provenance, old FIRST behavior, accepted mutation modules with spirv-val,
and exact52 iOS17 production exports. The new recipe binds that renderer build,
all production inputs and every new fixture/helper/recipe checksum. Its original
GPU tests cover two triangles with different first/last colors, matching coverage
and unchanged vertex bytes, mixed batches/LOAD, maximum counts and span failures,
padded aggregate exhaustion, malformed allocation requirements, partial failures,
nonzero-job errors, cancellation and cleanup. Assertions include byte/pixel checks,
not thousands of independent scenarios. No production fault exports are added.

Source requirements and behavior were independently reviewed. Relevant primary
contracts are Vulkan's [triangle-list provoking vertex definition](https://docs.vulkan.org/spec/latest/chapters/drawing.html),
[provoking raster state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationProvokingVertexStateCreateInfoEXT.html),
and the installed MoltenVK source at commit
`db66022459ffb663aa2b50f6b018bc2e124f5edf`, `MVKDevice.mm` (feature advertisement
and extension gating). No new dependency or license is introduced.
