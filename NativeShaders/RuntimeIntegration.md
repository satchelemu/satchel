# Runtime renderer handoff

Status: the final atomic prepare-and-snapshot version passed its Mac build,
C ABI and lifetime checks, real GPU/cache checks and iOS link. Evidence is in
`~/SatchelBuild/m5/snapshot-api-validation.json`. No phone execution is claimed.

The any-game worker already has the games worker's program/draw-pair capture hook
and a replacement/finalization lifetime for copied controls. Reuse those controls.
The debug capture files are useful evidence, but a file poller alone cannot update
the running renderer's resolved shader and composite-program objects.

## Install and call

Create one `SatchelShaderPublicationService` for the active content/runtime root
and Metal device, then call `SatchelShaderRuntime.install(service)` before starting
the host. Install `nil` when stopping that session. Pass reviewed control-key masks
through `skippedControlRanges` when the selected build needs them.

The C declarations are in `Tools/ShaderBridge/RuntimeShaderBridge.h`.
`ShaderPublication.swift` and `ShaderRuntimeBridge.swift` are included in the
integration source list. They add no dependency or subprocess.

`satchel_shader_runtime_prepare_snapshot` takes copied control/code, the canonical 16-byte
ASCII key, optional query-facts JSON, a 32-bit pixel-input-location mask, and variant
flags. Borrowed input buffers remain valid for the synchronous call. A mapped
code range must be checked with the host's `mtl_addr` before passing its pointer.
Success returns NVN stage 0, 1, 2 or 5 and a retained snapshot of that exact request
through its final output parameter. The output is null on failure. Status -1 means no installed service, -2 means
invalid framing, -3 means an unsupported preparation stage and -4 means preparation
failed. No native compiler diagnostic containing shader source crosses this ABI.

Use actual bound state to produce `tex`, `img`, `attr` and `fout` facts. The slots
and accepted strings are those in `GameShaderTranslator.Accessor`. For graphics,
derive the wanted vertex output locations from the effective fragment program's
SPH input map at code offset `0x30 + 4 * (6 + (location >> 2))`, eight bits per
location. Separate stage binds matter. A changed fact set or newly observed pixel
input mask is a different request. Compute dimensions/shared memory come from its
control section. Missing query answers retain the pinned defaults and diagnostics;
they are not evidence of whole-game correctness.

## Load a stable publication

Keep the snapshot returned by `prepare_snapshot`. A separate
`satchel_shader_runtime_snapshot(key, 16)` lookup returns the latest publication
and could observe another concurrent request for that key. Its directory contains `<key>_b.json`, per-variant geometry JSON when applicable, all generated
`<key>_<suffix>.metal` files and `analysis.json`. The latter contains c1 proof,
constant-buffer extents, unwritten-storage bindings and content hashes. It never
asserts the Mac offline compiler produced these files.

Use the same snapshot for reflection and libraries. Copy its immutable directory
and request identity with `satchel_shader_runtime_directory` and
`satchel_shader_runtime_identity`; both functions require room for a terminating
zero and fail without writing when the buffer is too small.
`satchel_shader_runtime_library(snapshot, suffix, length)` returns a retained
Objective-C `id<MTLLibrary>` or null. Transfer it to ARC with `CFBridgingRelease`.
Release a snapshot exactly once with `satchel_shader_runtime_release_snapshot`.

Retain the snapshot on the renderer's `NvShader` object until it is destroyed.
Later bp/bv/bx/bf/bg0/bg1/bg2/bg3/bg4/bgv loads must use that same snapshot, even if a newer request for
the canonical shader key is published. The immutable snapshot also remains valid
after the service is uninstalled or another request is published.

## Existing host integration points

The pinned `nvn_mtl.m` already uses a mutable `g_shmap`, reads reflection JSON in
`shader_load`, and creates functions/argument encoders from an `MTLLibrary`. It does
not require regenerated C source or a new host build per shader.

1. Prepare a missing runtime record before resolving it into the shader map. Add
   its long stage name only after publication succeeds. A null library or failed
   reflection parse must remain a reported failure, not a successful inventory row.
2. In `shader_load`, use the snapshot's JSON and library when present. Preserve the
   existing reflection parser, chain check, function specialization and argument
   layout checks. Keep the existing pack route for publications absent from this
   service. Variants also need the snapshot library route.
3. Apply the publication's analysis to that shader. Do not mark an unrelated
   legacy c1 table valid or use an offline `inputs.tsv` stamp for runtime output.
4. When the request identity changes, invalidate the affected `g_shcache` entries
   and source program's serial entry, using the engine's deferred-release rules
   for objects still referenced by submitted work. `prog_get` has a fast last-result
   cache tied to the serial generation. `bind_stages` also has a 64-entry composite
   ring keyed by source pointers/serials. That ring needs a publication-generation
   component or an explicit rebuild; deleting only the source serial is insufficient.
5. Resolve the effective draw pair before pipeline creation, after its binding
   state is known. A program may first be seen at a bind before the fragment shader
   or texture descriptors have been bound. Do not cache that incomplete preparation
   as final for later draws. Mark affected pipeline/argument state dirty after a
   publication change. The existing pipeline cache or `SatchelPipelineArchiveCache`
   can then persist the actual descriptor for that device/OS.

The library does not edit the other worker's host or app. The integration above
still needs an actual host build and boot check. The 198-shader Castlevania capture
test proves record translation, not these renderer state transitions.

## Storage and lifetime

The publication service keeps only small snapshots in memory, with a bounded live
Metal library cache. MSL and reflection live in immutable request directories.
The publication budget defaults to 8 GiB and source-cache budget to 2 GiB; the caller
can choose smaller budgets. Full Zelda source output is about 4.8 GiB, so a budget
must reflect the selected preparation scope. Exhaustion fails explicitly and leaves
the free-space reserve intact. Publications are not evicted while they may have
live renderer snapshots. The app can remove an inactive session's private cache
between sessions. These paths never own the user's imported game files.

Hull/domain preparation is explicitly unsupported. No stage may be silently
omitted, and observed runtime capture coverage is not whole-game coverage.

## Geometry and warm-up handoff

Geometry now returns NVN stage 2. The pinned MIT engine already implements the
compute route in `dm/gfx/nvn/nvn_geometry.inc`, including vertex capture, ordered
emission/index compaction and raster replay. Retain that path. Vertex base sources
already supply `main0_gs`. The new geometry coordinator emits `b` and `bg0` through
`bg4` kernels for points, lines, lines adjacency, triangles and triangles adjacency,
plus the `bgv` vertex reader. Every variant has its own reflection. All three
Odyssey thunder records match the retained chain exactly, 21 sources and 21
reflection files. All 21 library acquisitions and 18 compute PSO creations pass on
Mac. Synthetic viewport replay passes 48 GPU cases. No new game run is claimed.

In the any-game worker's current alternate `PhoneShaderRuntime`, update reflection
lookup to split a name such as `<key>_bg3` into canonical key and suffix, obtain the
same request's translated value, then call `value.reflection(variant: suffix)`.
A canonical key or `<key>_b` uses `b`. Forward the full variant name through the
Objective-C `satchel_phone_reflection` adapter. It currently truncates every name to its first 16 characters; preserve the suffix
and do not resolve `_bg3` with base reflection. The library
adapter already splits the suffix. Keep the adapter's existing success-status convention and geometry stage mapping; `bgv` is loaded by `shader_get` as a vertex stage. Preserve request
identity through reflection and library lookup, preferably with the snapshot API.
Account for the extra variant-reflection bytes in the adapter's bounded memory cache.
Add `Geometry.swift` and `ShaderWarmup.swift` to a hard-coded source list, or consume
`integration.json`. This worktree does not edit the other worker's files.

The optional lean passthrough and slice-fold optimizations are not required by the
three observed non-passthrough line emitters and are not ported. The full compute
route remains the geometry implementation. Hull/domain tessellation has no fixture
in either complete available inventory and remains explicitly unsupported.

Call `SatchelShaderWarmup.run` during the existing Prepare background task, sharing
the source store and compiler with the runtime. It has bounded lazy input, progress,
cancellation and per-record timing. The app owns source inventory, query facts,
progress persistence and real PSO descriptor warm-up. Full API usage, cache limits
and measured Mac timing are in `PrepareWarmup.md`.
