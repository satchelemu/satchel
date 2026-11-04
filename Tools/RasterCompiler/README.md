# Bounded raster shader compiler: staged

This original C component translates admitted Maxwell vertex/fragment programs
to dynamically generated SPIR-V. It is not a fixed shader replacement and does
not call the general NativeAOT translator. No canonical app/framework/source
has been replaced. The separate Vulkan owner must validate, create and execute
a compatible graphics pipeline before this becomes a rendering capability.

`python3 build.py` builds outside the repository using installed Xcode clang and
spirv-tools. It runs 4,361 ASan/UBSan assertions, validates six emitted original
modules for Vulkan 1.1, compiles an iOS static archive and verifies its exact two
production exports. Logs and hashes are in `validation-manifest.json`. Actual
GPU draw integration is owned separately and remains pending for this checkpoint.

## Admission and state

The input contains the complete real 80-byte SPHv3/SASS3 header and a separate
instruction span starting at a Maxwell scheduling-word boundary. Vertex and
fragment stage/type must agree. The caller explicitly supplies eight known
graphics-state fields; this profile requires one vertex program, ordinary
viewport transform, zero-to-one depth, upper-left origin, no Y negation, alpha
test always, no alpha-to-coverage, and no transform feedback. There are no
compiler defaults for absent state, no stage fallback and no VertexA combination.

Accepted instructions are exact unpredicated direct `ALD`, direct position
`AST`, `MOV`, `MOV32I` and `EXIT`. Unsupported modifier/reserved/indirect forms
reject. The allowed vertex input offsets cover float32 locations 0–7; every
read component must be present in the SPH input map. All four position components
must be defined. Fragment instructions define the actual GPR values selected by
the header's full RGBA0 output map. No varying inputs, resources, textures,
arithmetic, branches, depth/sample outputs or undefined register reads are admitted.
RZ reads zero. Register moves and output overwrites preserve actual source bits.
Only zero/NOP padding is accepted after EXIT; scheduling words have no scalar
data effect. Unsupported header fields reject, including nonzero memory sizes.

Float32 vertex interpretation and a float RGBA target are deferred requirements,
not facts inferred from source bytes. A pipeline owner must check actual vertex
formats, attachment format and every draw-time vertex range. Reflection records
all admitted ALD reads, including subsequently dead values. The emitter loads a
complete vec4 for each reflected location: a partial component mask NEVER permits
a shorter buffer access. Final extracts may use a subset of that mask.

## Boundedness and ownership

Input code is at most 1,024 bytes and contains at most 64 semantic instructions.
There are 256 fixed register values, eight input locations and four output values.
Only bounded linear loops run; no allocations, recursion, callbacks, native
function pointers, logging or global mutable state exist. SPIR-V is capped at
4,096 words per stage. Compile success transfers only caller-owned output bytes
and typed reflection, never a retained input pointer or a GPU object.

Both source stages decode fully before paired output publication. Failure leaves
all outputs unchanged; overlapping output objects reject. Source/output overlap
is safe because all source reads finish before publication. Production exports
are only `satchel_raster_compile` and `satchel_raster_compile_pair`.

SPIR-V uses Shader only, Logical/GLSL450, float32/vec4, location inputs,
standalone BuiltIn Position or Location0 output, constants, loads, extracts,
constructs and one store. There is no capability stripping or silent lowering.
The backend must independently validate this exact bounded schema, stage/state
agreement, actual GRAPHICS queue support, float formats and resource lifetimes.

## Why not broaden NativeAOT yet?

The pinned general translator has unbounded optimization work and allocations,
unresolved global-memory lowering, silent FP64-to-FP32 conversion and unconditional
SPIR-V capabilities. The exact-original NativeAOT facade remains unchanged. This
small emitter expands actual supported code without pretending those larger
translator admission issues are resolved. It is intentionally not general game
shader compatibility; shader capture and admission are distinct from pipeline
creation, submission, fence completion and a visible game frame.

## Validation and review

Original field encoders and source-level native bit/vector expectations exercise
different attribute locations/swizzles, MOV chains, constants including arbitrary
IEEE bit patterns, overwritten outputs, all load widths and RZ. A separate typed
SPIR-V evaluator compares actual emitted values; spirv-val independently checks
modules. Negative fixtures cover header/state/opcode modifiers, undefined values,
input declaration mismatch, bounds, instruction budget, partial publication and
source/output aliasing. They contain no imported shader bytes.

Parent review found no transactional/bounds blocker and explicitly required full
vec4 backend range checks. That review did not independently certify Maxwell
opcode semantics. The compiler's own semantics review used the pinned sources
listed below; real GPU schema/draw validation is separate.

## Reference provenance

No runtime dependency was added. Production code is independent original C.
Public encoding/semantic references, with local copies used only for research:

* NX2Apple `8227f80f9b381c680611303319656c4e0c806a50`, bundled Ryujinx
  `InstTable.cs`, `InstDecoders.cs`, `InstEmitAttribute.cs`, `InstEmitMove.cs`,
  `ShaderHeader.cs`, `AttributeConsts.cs` and `EmitterContext.cs`.
* NVIDIA open-gpu-doc `9fdf5c4062007929d9f4e6cbad9c9771fe61b880`,
  `classes/3d/cla097sph.h`: SPHv3 type/stage/version, component maps. Its public
  HTML introduction reverses the type values; the actual header and its enum
  table agree on VTG1/PS2. This implementation follows the header, not that typo.
* devkitPro uam `5a5afc2bae8b55409ab36ba45be63fcb73f68993`,
  `source/compiler_iface.cpp` and `nv_shader_header.h`, independently confirm
  version3/SASS3 and fragment output-register ordering.

The existing pinned Ryujinx source is MIT-licensed. NVIDIA's reference header
contains its permission notice. Neither translator implementation nor external
header is compiled into this library. Imported code/control remain RAM-only;
only original fixture modules may be serialized by the test executable.

## Current paired owner and explicit unused-state proof

# Proven-unused YNegate state: staged additive overlay

This overlay extends the corrected NVN stage1|2 paired Program compiler with an
explicit reflection.unused_state_mask. Value8 means bit3: YNegate is proven
unobservable by the entire admitted shader. It is published only after complete
SPH, instruction, defined-register, output and bounded-exit validation succeeds.
No arbitrary shader, header flag or unknown opcode can receive this proof.

The installed pinned Ryujinx shader source (within NX2Apple8227f80f) consumes
YNegateEnabled in Instructions/InstEmitAttribute.cs184–192 for the fragment
PositionY/FragCoord interpolation path. The current independent emitter admits
only immediate/register bit moves in fragment code; vertex attribute access is
limited to ordinary user float32 inputs, with Position-only output. It admits no
fragment ALD, IPA, FragCoord, varyings or indirect attributes. Both admitted stage
profiles therefore prove YNegate unused. A future profile that observes it must
not inherit the proof: it needs separately reviewed semantics and actual state.
No pinned translator source or frozen compiler checkpoint was changed.

Admission now reports state_requirements.known247: all seven meaningful state
facts remain required. Actual SatchelRasterState.known may be247 (Y unknown) or255
(Y known); all unknown/reserved bits reject. Known Y must be0 or1. An unknown
Y field is ignored, even if filled with UINT32_MAX; it is never reported as an
observed0. Both known values compile identically because the proof establishes
semantic independence. No other state fact is inferred or filled by the compiler.
Paired stages compare their meaningful actual state, ignoring only this proven
unused field/known bit. Complete decode precedes state validation and publication.

The reflection field is appended, so every consumer must rebuild using this
header. The backend must independently validate mask8 alongside its complete
SPIR-V schema; it cannot accept arbitrary modules just because they claim it.
The recorder should use known247 and preserve unknown Y rather than inventing a
capture. This does not infer viewport-transform/depth/origin/alpha/feedback state.

Original gates:483 paired-owner assertions,4,398 compiler assertions and10 pinned
NVN stage-ABI assertions, ASan/UBSan. Added tests cover unknown/known true/known
false Y, reserved/nonboolean state, each other missing fact, unsupported
fragment coordinate/interpolation instructions refusing proof, and transactional
paired outputs. All six emitted SPIR-V modules remain byte-identical to the
previous corrected-stage checkpoint and pass spirv-val Vulkan1.1. Host/iOS
combined owner+compiler archives are staged only; no device test or game draw.

Historical checkpoint: /tmp/satchel-raster-unused-state-staging. Current reproduction uses scripts/test_nvn_raster.sh below.

Reproduce the matched renderer/compiler/owner gates with `zsh scripts/test_nvn_raster.sh --ios --output /tmp/satchel-nvn-raster-build`. Outputs are outside the repository; only original fixture modules are written.
