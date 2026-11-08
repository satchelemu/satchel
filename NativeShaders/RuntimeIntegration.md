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

Hull/domain preparation follows the tessellation handoff below. No stage may be
silently omitted, and observed runtime capture coverage is not whole-game coverage.

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
route remains the geometry implementation. Hull/domain tessellation has no record
in either complete available inventory; it is prepared as described below.

Call `SatchelShaderWarmup.run` during the existing Prepare background task, sharing
the source store and compiler with the runtime. It has bounded lazy input, progress,
cancellation and per-record timing. The app owns source inventory, query facts,
progress persistence and real PSO descriptor warm-up. Full API usage, cache limits
and measured Mac timing are in `PrepareWarmup.md`.

## Tessellation handoff

Since 2026-11-07, hull (NVN stage 3) and domain (stage 4) shaders translate. The
upstream engine never drew them (`primitive type 14 not implemented`, its
`Tessellation stages are not drawn` entry): Metal 4's render encoder and pipelines
have no tessellation, no `drawPatches` and no factor buffer. Ryujinx 1.3.3 has no
Metal backend, and MoltenVK relies on Metal's hardware tessellator, which Metal 4
lacks, so neither had code to port for this. The route is Satchel's own, using the
pinned SPIRV-Cross output (a hull kernel and a `[[patch]]` vertex function) and
changing only their plumbing (`Tessellation.swift`), plus a compute tessellator
(`Tessellator.swift`) written from the OpenGL 4.6 rules for primitive generation.
Since 2026-11-08 the host draws NVN patch primitives through this route
(`NativeHost/graphics/tessellation_draw.py`, "Host draw" below). None of the tested
games draws patches, so no game result follows from this.

The host contract, per patch draw with `P` patches of `N` input control points:

1. Run the vertex shader's `main0_gs` capture entry (the geometry route's ABI,
   `dm_geometry_capture` at buffer 13), with a slot table that keeps the words the
   hull's reflection lists in `tessellation.capture_words`.
2. Dispatch the hull `b` kernel: `P` threadgroups of `max(N, output_vertices)` threads,
   threadgroup memory 0 of `input_bytes_per_vertex * N` bytes, buffer 13 the same
   capture constant, 14 a `dm_tess_input` (`index`, an optional list of capture
   records per control point, null for sequential, and `first`, added to each),
   26 the patch records, 28 the control point records, 29 `{N, P}`.
3. Run `SatchelShaderTessellator.source`'s `dm_tess_count` (`P` threads),
   `dm_tess_scan` (one threadgroup of up to 1,024 threads) and `dm_tess_emit` (`P`
   threads) with the domain shader's reflected `tessellation.domain`, `spacing`,
   `winding` and `point_mode`. Patches that would overflow the point or index buffer
   are dropped whole and counted (`dropped`), never cut.
4. Draw the domain `b` library's `main0` or `main0_z` (the same depth convention
   choice as a vertex shader) as triangles, lines for isolines or points, indexed by
   the emitted uint32 list, through the indirect arguments in the totals buffer
   (`indexedArgumentsOffset`, `pointArgumentsOffset`). Buffers 16 to 19: points,
   patch records, control point records and the hull's output vertex count.

Records are the route's own ABI, so any hull pairs with any domain shader: a control
point is 576 bytes in the geometry capture's 144-word map (position, point size,
clip distances, layer, viewport, user locations 0 to 31), a patch is 528 bytes
(four outer and two inner levels, kept as floats rather than Metal's half factors,
then per-patch user locations 0 to 30). Both shaders assert these sizes when they
compile. The tessellation mode (domain, spacing, winding) is the 3D engine's state
that NVN sets from the program; the record carries no decoded field for it yet, so
requests supply facts `tess_domain`, `tess_spacing` and `tess_cw`, defaulting to
triangles, equal spacing and counter-clockwise. Finding that field in a real
tessellated title's control section is open. Counter-clockwise means a positive
signed area in the (u, v) plane. Isolines are handed to SPIRV-Cross as quads (it
refuses isolines); reflection keeps isolines. Domain shaders also get the vertex
shaders' one-lane subgroup rewrite: the engine's notes record two of Bayonetta 3's
domain shaders failing in SPIRV-Cross on subgroup builtins when that rewrite covered
vertex shaders only. No such domain shader is in the corpus, so that is untested.

Explicit failures: control point interfaces other than position and user `float4`,
`int4` or `uint4` locations (a domain shader reading clip distances or point size
from its control points is refused), `gl_in.size()`, and instance or vertex IDs in
a domain shader. Storage buffers in hull and domain shaders are reported without a
slot, as in the engine, which never measured those stages' `c0` table bases.
SPIRV-Cross's triangle-domain input carries outer levels 0 to 2 and inner level 0
only, so a triangle domain shader reading outer level 3 or inner level 1 reads 0.
Integer spacing places vertices exactly as the specification does. Fractional
spacing follows its rules (levels rounded up to even or odd, `n - 2` equal segments
and two shorter ones mirrored about the edge's centre) but cannot be shown to
reproduce the Switch tessellator's fractional positions: nothing on this Mac
establishes them (2026-11-08 search: Ryujinx 1.3.3 hands tessellation to the host
GPU's tessellator and has none of its own, the pinned engine has none, no Mesa or
NVIDIA hardware documentation is present, and the Metal Shader Converter headers in
Unreal Engine's tree describe Apple's emulation of Direct3D's tessellator, not the
Switch's), so the rule below is kept. The exact difference: with `f` the rounded
fractional level and `n` its segment count, the two short segments (each
`(f - n + 2) / 2f` long) are the two central segments for even `n` and the two beside
the central segment for odd `n`; the specification leaves where they sit to the
implementation, as long as they are symmetric and identical for equal levels. Any
placement it allows moves a vertex along its edge by at most `(n - f) / 2f` of the
edge (under `1 / f`), never changes the number of vertices, their mirror symmetry or
the end points, and is zero at integer levels. The triangulation between rings is
also implementation-dependent, so triangle connectivity (not the vertex set) can
differ from the Switch's at any spacing; that changes the picture only where a domain
shader's output is not affine in the domain coordinates. Outer edge
positions are 16.16 fixed point and mirror exactly, so patches sharing an edge at
one level produce the same points. Each patch is emitted by one thread, so a draw
of a few very finely tessellated patches is serial work; the host should bound
buffer sizes, as the geometry route caps its scratch.

### Host draw

`tessellation_draw.py` (graphics overlay, after `view_upscale.py`) implements the
contract above in `nvn_mtl.m` and `hle_nvn.c`. `SetPatchSize` and
`Set{Outer,Inner}TessellationLevels` are recorded as `OP_BIND_STATE` kinds 6, 7 and 8
(an empty implementation before) and kept in the draw state, so lookahead walks save
and restore them with the rest of it. A draw of primitive 14 goes to `tess_draw`
(`TessellationDraw.inc`) before the geometry route: the capture of every vertex the
draw names (indexed draws capture their sorted unique indices, and each control point
reads its index's record), then per instance the hull kernel (one threadgroup per patch,
threadgroup memory 0 of `input_bytes_per_vertex * N` bytes), the tessellator's three
kernels, and the domain shader drawn through `draw()` in the vertex stage's place with
the program's pixel stage, its NVN stage 4 resources, and the draw's render pass, raster
state and pipeline key. Instances are drawn one after another so patch numbers restart
per instance. The tessellator source is embedded from `Tessellator.swift` at build time.
Capacity is what the draw's patches can emit at level 64 (triangles 3,169 points and
18,432 indices a patch, quads 4,225 and 24,576, isolines 4,160 and 8,192), at most 524,288
points and 2,097,152 indices (8 MiB each) a draw and instance; a patch past it is dropped
whole and counted, and the count is logged. Refused with a red line and
not drawn: a program without both a translated hull and domain shader, a geometry stage
after tessellation, transform feedback, indirect-count multi-draws, a primitive restart
index inside the patch list, and a patch size never set (no default is assumed). Levels
from `Set*TessellationLevels` apply only to programs without a hull, which this route
refuses. Every other draw takes its old path; the changes on it are a primitive check
before the geometry route, the vertex stage's NVN stage number in its argument encoding
(0 unless a domain draw is encoded), and one flag check before the encode.

`NativeHost/graphics/tess-draw-check.py` (with `SATCHEL_OUT` set) proves it on the Mac's
GPU with synthetic NVN command streams replayed by the host as a game's submission is: the
overlaid `nvn_mtl.m` and `hle_nvn.c` built into a test, public-synthetic vertex, hull and
domain shaders translated by the repository's translator (`public_corpus.py`'s generators,
no game data), and thirteen streams covering triangles and quads at equal, fractional even
and fractional odd spacing, counter-clockwise and clockwise, levels from 1 to 64,
plain and indexed draws with base vertices, two instances, a back-facing patch, isolines,
and a patch draw without `SetPatchSize` (refused, nothing drawn, the draw after it
unchanged). Against a CPU reference draw of the same patches: identical coverage with back
faces culled (110,558 pixels; the 34 pixel centres within 0.01 px of a patch edge are not
compared), and at every
covered pixel the control point value of the vertex the indices name, the per-patch
value, the levels and the patch number, exactly. A plain triangle draw of the patch
corners in the same stream matches the same reference. The host's tessellator output for
all 26 patches matches the specification's vertex count, a triangulated disk's
`2V - B - 2` triangles, the domain's area and winding, and the outer edges (82 edges:
`k / n` exactly at equal spacing, `n + 1` mirrored points with `n - 2` segments of `1 / f`
at fractional spacing). It passes with Metal's validation layer, both with encoding on
the submit thread and deferred to the encode thread (`DM_NVN_ENC_THREAD=1`, the iOS
default). Changing the spacing, winding or patch size the host passes makes it fail.

`NativeShaders/check-tessellation.py --products <m5 root> --output <folder>` runs
it all on the Mac's GPU from the public corpus: 791 patches in 20 dispatches over
every domain, spacing, winding and point mode on a grid of levels (bounds,
orientation, total area, a closed edge manifold whose open edges lie on the domain
boundary, outer edge subdivision, discards, NaN levels, a shared edge between two
patches and capacity drops), then the translated public hull and domain rows
drawn through capture, hull, tessellator and domain with back faces culled, for
triangles (equal and fractional even), quads (fractional odd, clockwise) and
isolines: coverage identical to a reference draw of the same patches and the
control point, patch and level values exact at every covered pixel. It also checks
six refusals of unknown interface shapes.
