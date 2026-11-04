# Castlevania graphics integration boundary

Updated 2026-10-31: the shader compiler chain now runs on the actual iPhone Duo
with original synthetic fixtures. A checked interpreted NVN call boundary and
real Vulkan-backed device lifecycle are implemented; no game renderer or
guest-originated graphics call has been validated yet.

The actual main Program archive for title `0100ba4026e94000` was decrypted and
its complete ExeFS verified. Its four NSOs were decoded in RAM. Dynamic symbol
metadata establishes the graphics boundary without relying on the filename:

| Module | Dynamic symbols | Undefined symbols | NVN evidence |
| --- | ---: | ---: | --- |
| main | 768 | 715 | imports `nvnBootstrapLoader` |
| subsdk0 | 10,395 | 665 | imports `nvnBootstrapLoader` |
| sdk | 23,015 | 8 | defines `nvnBootstrapLoader` and four related entry points |
| rtld | 3 | 2 | no graphics imports |

Main rodata contains `UnityPlayer`, `UnityEngine`, and `il2cpp` markers. These
support Unity/IL2CPP identification, not a particular engine version. Only
symbol metadata was recorded; game code, keys and decompressed modules were not
written to files. Scratch metadata: `/tmp/satchel-graphics-audit/report.txt`.

## Reuse requirements

Pinned NX2Apple's `dm/gfx/nvn/hle_nvn.c` intercepts `nvnBootstrapLoader` and returns
native assembly thunks. Those addresses cannot be returned as executable guest
functions by the new interpreter. A compatible path needs guest-address stubs,
ARM64 argument/return marshalling, bounded guest-memory access, guest callbacks,
GPU virtual-address mappings, and actual fence completion. Returning success
for an unimplemented operation would not establish rendering.

The reference Metal backend supplies useful implementations for textures,
render state, command encoding and presentation. Its object pointers and
callbacks assume native execution and need a checked boundary before reuse.
The guest NVN API and lower-level Maxwell submission interface are different:
libnx/deko3d homebrew usually submits GPFIFO commands through `nvdrv`, so an NVN
interception layer alone would not run those programs.

## Shader dependency

The reference shader pipeline uses its vendored MIT Ryujinx C# translator for
Maxwell machine code to SPIR-V, Python driver/ABI/reflection transformations,
SPIRV-Cross for SPIR-V to MSL, then offline `xcrun metal/metallib`.
The shader translator takes texture/image formats, vertex attributes, constant
buffers and other current GPU state; a generic runtime cannot substitute
per-game tables prepared on a computer.

`Tools/MaxwellShaderCompiler` now builds the pinned C# translator as a signed,
embedded iOS NativeAOT framework. It uses .NET SDK 10.0.403 / runtime packs
10.0.14 and a source-locked subset without four unrelated Common dependencies.
The app's runtime feature check reports dynamic CPU code support/compilation
disabled. Compiling this emulator component once is not per-game preparation.

The current facade supports only resource-free compute fixtures, with bounded
input/output buffers. Real descriptor/binding requests fail explicitly. It does
not yet accept a complete live GPU-state snapshot or expose the reflection
needed for arbitrary game shaders. Existing size bounds are not a full hostile
shader CPU-time/allocation budget; no imported game shader reaches this facade.

`Tools/ShaderBridge` builds SPIRV-Cross as a static iOS library. The reference pins
`f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7`; its minimal MSL implementation needs
`spirv_cross.cpp`, `spirv_parser.cpp`, `spirv_cross_parsed_ir.cpp`, `spirv_cfg.cpp`,
`spirv_glsl.cpp`, and `spirv_msl.cpp`. Preserve the pinned component notices and
catch C++ exceptions at the bridge. The app uses its bounded C API, active
resource metadata and public `MTLDevice.newLibraryWithSource:options:error:`
for runtime Metal compilation. Full dependency notices are bundled in the app.

`Core/GraphicsDiagnostic.m` was validated on the Duo by the stage-06 runtime UI
test. It executes two separate original fixtures, without game shaders or a Mac
compiler process:

1. A Maxwell EXIT-only compute shader becomes 676 SPIR-V bytes, then 242 bytes
   of iOS MSL. Metal compiles it and completes a GPU dispatch. The translator's
   declared support buffer is retained in reflection and marked inactive; no
   resource is bound or used by this EXIT shader.
2. A separate original SPIR-V compute shader becomes 185 MSL bytes and writes
   the expected uint to a shared Metal buffer. Three neighboring guard words
   remain unchanged. This second test starts at SPIR-V, not Maxwell.

These tests prove the embedded compiler/Metal path operates on the phone. They
do not establish game shader compatibility, guest-originated rendering or a
visible Castlevania frame. `Runtime-check.json` keeps `graphicsImplemented=false`
and records the synthetic `graphicsSelfTest` separately. GPU completion waits
have five-second deadlines; synchronous compilation is only exercised with the
fixed, tiny fixtures and is not a cancellable arbitrary-shader service.

References: [SPIRV-Cross README](https://github.com/KhronosGroup/SPIRV-Cross),
[pinned build](https://github.com/KhronosGroup/SPIRV-Cross/blob/f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7/CMakeLists.txt),
[deko3d API primer](https://github.com/devkitPro/deko3d/blob/master/Primer.md).

## Vulkan and interpreted call boundary

Tools/GuestGraphics now resolves the real NVN bootstrap through checked guest
reads. Original guest-address stubs dispatch through bounded interpreter traps;
no native pointers or executable guest memory are used. DeviceInitialize/Finalize
own actual MoltenVK devices with generation-checked guest objects and cleanup.
Seven compute capability selectors derive directly from those devices; unsupported
selectors/operations stop. See the component README for exact limits.

Stage16 on the Duo passed an independent original interpreted lifecycle fixture:
18 instructions/6 calls, real create/finalize, guard bytes, saved state and cleanup.
Stage17 passed the expanded48-instruction/14-call fixture including seven
queries on the Duo. These fixtures never use the game CPU.
The original Vulkan compute test also passed on-device with its output/guards.
MoltenVK maps Vulkan to Metal on iOS; this is no claim of native Vulkan support,
performance superiority or App Store approval. Licenses remain bundled.

## Current interpreted compute milestone

Stage46 on the Duo also validates the bounded program/compute path in
`Tools/GuestGraphics/NVN-COMPUTE.md`. Actual original shader instructions are
admitted into immutable program generations, compiled on the phone by the
restricted scalar emitter, recorded through interpreted NVN calls and submitted
to Vulkan. Four pipelines/fences complete; changed code and explicit flush
visibility change the results as expected. Resources return to zero. The older
NativeAOT compiler remains a separate cold-first diagnostic; its restricted
facade is not an arbitrary game shader compiler.

The app selects this compute-capable dispatcher, including its existing pool
and transfer operations, for the actual game process. No game-originated NVN
call has reached it yet. Ten passing graphics fixtures are component evidence,
not Castlevania rendering. Vertex/fragment translation, image/render-target
ownership, raster draws and presentation are still required.

## Acceptance gates

Next use the checked guest-memory/NVN boundary for a guest-originated
copy/clear with completion tied to actual Metal work. Then translate a guest
vertex/fragment shader pair entirely on the phone and compare the rendered
output with a reference. Only a real game's submitted frame establishes its
rendering path. Host-drawn samples, loader diagnostics and imported files do not.

## Interpreted offscreen raster: device59b

Validated on the actual Duo: original558-instruction/186-dispatch fixture captures
a vertex shader and two fragment shaders, records clear+two draws, compiles two
pipelines on-device, waits a real fence and verifies the final tiled pixels,
padding, cache visibility and resource cleanup. All tracked resources return
to zero. The game dispatcher now enables the same bounded raster owner alongside
compute/transfers; Castlevania itself has not reached an NVN call. There is no
window/acquire/present-to-screen path and no game-frame claim.

[Supported profiles and reproduction](GuestGraphics/RASTER.md) describe the
actual resource/state limits. The complete matched source/build/artifact record
is `/tmp/satchel-nvn-raster-repro-overlay/canonical-promotion.json`; the framework
SHA begins44bb1b32. Four new archives are linked exactly once; the paired Program
archive already includes its compiler. Use `zsh scripts/test_nvn_raster.sh --ios`
for relevant changes; do not mix old callers with the expanded transfer ABI.
