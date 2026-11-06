# Maxwell shader compiler feasibility probe

This builds the pinned NX2Apple-vendored Ryujinx Maxwell shader translator as
NativeAOT code callable from C. The translator runs on the target device; it does
not require CPU JIT permission or shaders prepared for each game on a Mac.

The present facade supports **resource-free compute translation only**, with a
1×1×1 workgroup. The original fixture contains a scheduling word, unconditional
Maxwell EXIT instruction, and padding. It produces 676 bytes of SPIR-V. This is
a compiler/interop test, not a rendered frame or a game compatibility result.
An optional, separately staged original compute-fixture entry point is described
below. The original `satchel_shader_probe` entry point is unchanged.

## Reproduce

Requirements: Apple Silicon macOS, Xcode with the iPhoneOS SDK, Python 3, an
existing .NET SDK **10.0.403**, and an existing checkout of the translator sources.
The game translator (the default build) reads the engine snapshot at nxnative
**f8a5618d5f505a4c0a0bcac7c413872d3a53c009** (`<deps>/NX2Apple/engine`):
`game-source-lock.json` verifies that commit, its 223 source, resource and license files
and the pinned `dmsh_xlate/Program.cs`. The original fixture (`--original-fixture`) and
the probe below keep the NX2Apple checkout at **8227f80f9b381c680611303319656c4e0c806a50**,
verified by `source-lock.json` (216 files). No SDK/workload is installed by this script.

From the repository root:

```sh
zsh scripts/test_maxwell_shader.sh \
  --checkout /path/to/NX2Apple \
  --dotnet /path/to/dotnet \
  --work /tmp/satchel-shader-aot
```

This builds a host NativeAOT shared library, compiles a pure C caller, and runs
four assertions: SPIR-V magic/output, invalid input length, insufficient output
capacity, and disabled dynamic-code feature flags. To additionally build and
link iOS artifacts, append `--platform all`. Or run `build.py` directly with
`--platform ios` when only the iOS artifacts are needed.

All packages, CLI state, intermediate files, output, and logs go under `--work`.
The script disables telemetry, ASP.NET development-certificate generation, and
global tool PATH modification. The cleared NuGet configuration permits only the
official public feed. NativeAOT compiler, linker tasks, and runtime packages use
version **10.0.14**. The host build may restore an unused ASP.NET host runtime pack
as selected by the SDK; the facade does not reference ASP.NET.

The build references upstream source files directly. It neither changes that
checkout nor copies its implementation into this repository. Five small
Ryujinx.Common source files plus the original resource facade replace a project
reference that otherwise imports unrelated Humanizer, Gommon, MsgPack.Cli, and
RecyclableMemoryStream packages. No shader source, game code, or keys are inputs
to the reproducibility test.

## Outputs and integration

- `WORK/repro/host-probe`: native C test executable.
- `WORK/repro/exit.spv`: output from the original synthetic Maxwell fixture.
- `WORK/repro/ios-static-publish/SatchelShaderProbe.a`: translated static object.
- `WORK/repro/ios-static-link-probe`: iOS executable that passed static linking;
  this is not a runnable iPhone application bundle.
- `WORK/repro/ios-static-link-args.json`: exact native runtime/bootstrap and
  system-framework link arguments.
- `WORK/SatchelShaderProbe.framework`: unsigned iOS arm64 framework, including
  public header, module map, source pin, and notices. The application must embed
  and sign it. The runtime is already linked into this framework.

The static `.a` alone is insufficient: NativeAOT runtime libraries and the direct
`libbootstrapperdll.o` object must also be linked. The script verifies that route
against the installed 10.0.14 runtime pack. The framework is the simpler embedding
route. Its install name is
`@rpath/SatchelShaderProbe.framework/SatchelShaderProbe`.

`SatchelShaderProbe.h` declares:

```c
int satchel_shader_probe(const uint8_t *code, int size,
                        uint8_t *output, int capacity);
int satchel_shader_runtime_features(void);
```

The first function returns a positive SPIR-V byte count, or `-1` for invalid
arguments, `-2` for insufficient output capacity, and `-3` for unsupported input
or a translation failure. No partial output is copied on failure. The caller
owns readable/writable native buffers and must keep them valid for the call.
Input size is 32–65,536 bytes and a multiple of eight. There is no callback,
retained caller pointer, or managed exception crossing the ABI.

The feature result uses bit 0 for dynamic-code support and bit 1 for dynamic-code
compilation. NativeAOT returns **0**. This check must also pass in the consuming
iOS application before reporting on-device validation.

## Boundaries

The facade cannot answer real texture/image descriptors, constant-buffer binding
requests, or live graphics state. Resource binding requests fail explicitly.
Production translation needs a bounded state snapshot, binding/reflection output,
cancellation and allocation budgets, and tests for actual instruction semantics.
The current input-size bound alone is not a complete hostile-input resource
budget. Do not wire arbitrary game shaders to this diagnostic entry point.

SPIR-V is not Metal source. A separate bounded SPIRV-Cross layer must translate
SPIR-V to MSL and preserve its resource reflection, followed by Metal library and
pipeline creation. Metal runtime compilation alone does not translate Maxwell.
This tool does not implement GPU command submission, memory, textures, services,
presentation, or shader cache correctness.

## Original parameter-add-store fixture (staged)

`--original-fixture` additionally builds `SatchelShaderFixture.h` and
`satchel_shader_fixture_compile`. It accepts **only one byte-for-byte original
64-byte Maxwell program**: LDC64 address, two LDC32 parameters, IADD, STG32,
EXIT, with fixed scheduling words. The C test assembles those fields independently
from the facade's admitted words. No game bytes, game shader hashes, per-game
substitutions, or upstream source patches are involved. The frozen checkout,
216-file source lock, original API and canonical framework are unchanged.

```sh
python3 Tools/MaxwellShaderCompiler/build.py \
  --checkout /tmp/satchel-switch-integration/NX2Apple \
  --dotnet /tmp/satchel-switch-integration/dotnet/dotnet \
  --work /tmp/satchel-shader-original-staging --platform all --original-fixture
```

The fixture route refuses the canonical `/tmp/satchel-shader-aot` directory.
Its host gate uses the already-installed `spirv-val` (verified here as
SPIRV-Tools v2026.4); it installs no additional compiler dependency. The C caller
is built with ASan/UBSan. NativeAOT itself is not sanitizer-instrumented; its
managed span/array checks and explicit bounds remain enabled.

The new input record supplies copied code and 32 bytes of CB3, explicit local
size 1×1×1, zero local/shared memory, separate UBO/SSBO bindings 1–7, checked
storage size/alignment and a bounded 40-bit virtual address. CB3 contains the
address low/high words, storage size, inputA, inputB, and three zero words.
The one invocation computes `uint32(inputA + inputB)` and stores it at storage
offset zero. State mismatch, any changed code bit, missing binding, or any
translator log/lowering diagnostic rejects the request. Caller native spans
must be valid; input bytes are snapshotted and no pointer is retained.

The accessor explicitly disables optional host capabilities, including Float64,
instead of inheriting permissive defaults. It preserves translator reflection
and requires exactly the expected CB3 and writable storage resource. The
independent bounded `FixtureSpirv` parser then checks instruction lengths, ID
bounds/definitions/references, known 32-bit types/layouts, resource decorations,
literal address chains, one compute entry and one straight-line function. It
symbolically proves the only reads are CB bytes12/16 and the only write is their
integer sum to SSBO byte0. Float32/vector declarations exist for the translator's
uniform layout; float values may only be loaded and bitcast, never used for
floating arithmetic. Unknown opcodes/types/storage classes/extensions, images,
subgroups, dynamic addresses, branches and other writes are rejected.

Only after this admission does the parser remove seven known, unconditionally
emitted but unused capability declarations. The result declares **Shader only**.
No instruction, type, binding or memory operation is rewritten. This is not a
general SPIR-V validator or a general capability-stripping API. Host-only test
exports can exercise original malformed SPIR-V; those exports are absent from
the staged iOS framework.

The fixed reflection record distinguishes three declared buffers from two
active buffers: unused support UBO binding0, active CB3 UBO at the requested
binding (32-byte snapshot), and active writable SSBO at its requested binding
(four bytes accessed). It also carries the storage address's CB slot/byte
offset. Output bytes and reflection stay unchanged on failure, including
insufficient capacity. Destination overlap is rejected; inputs may alias output
because they are copied before publication. Calls are serialized with a
nonblocking guard and return `BUSY` if another fixture compile is active.

Validation completed: **7,087 original C fixture assertions**, ASan/UBSan caller
checks, independent `spirv-val --target-env vulkan1.1` validation of the
1,208-byte result, the original probe's four checks and zero dynamic-code flags,
and iOS NativeAOT static/shared compilation plus static link. Tests cover every
Maxwell bit, invalid state, output canaries/aliasing, changed bindings/parameters,
SPIR-V truncations/lengths/IDs, forbidden operations/types/capabilities and a
supported-opcode mutation changing A+B to A+A. A negative test originally wrote
the unchanged constant ID; it was corrected to change CB byte12 to byte4, and
every word mutation now asserts that it actually differs before testing rejection.

The superseding cold-call-corrected staged output is
`/tmp/satchel-shader-cold-staging/SatchelShaderProbe.framework`. Its notices
include source hashes for the original facade/parser/fixture and the unchanged
upstream lock. It is not installed or promoted. Do not load it alongside the
old framework: it is an opt-in replacement build containing the same old exports.

**Still required:** a real Vulkan pipeline/descriptor submission and guarded
readback on the phone, using this translator output. That bridge must query
actual buffer ranges/alignment and descriptor limits, retain resources to true
completion, and test overflow/NaN-bit-pattern parameter values without float
canonicalization. The current validation proves compiler/ABI/structural and
symbolic fixture behavior, not GPU execution. No imported shader is admitted.
Generic decoder/IR allocation/deadline cancellation and general GPU memory/loop
safety remain unresolved; the fixed input graph is the scope bound here, not a
new claim that the upstream translator is safe for arbitrary inputs.

## Cold production entry point

The initial warmed-up fixture suite missed an actual first-call abort in the
production export (`SIGABRT`, `RhFailFast`). NativeAOT 10.0.14 loads its stack
cookie in the unmanaged entry prologue, then initializes the cookie during lazy
runtime entry. A protected first-call frame therefore observes a different
cookie at return. The generated ARM64 prologue/epilogue and the upstream
[NativeAOT fix](https://github.com/dotnet/runtime/pull/136265) confirm this ordering
issue. Moving the lock into `try` alone did not fix the abort.

The public export now has a small transition frame and calls a non-inlined
managed worker; the larger protected frame is created after runtime entry.
The worker still retains stack protection, and lock acquisition is inside the
exception boundary. No caller warm-up, runtime patch, disabled cookie checks,
or dependency update is required. Public ABI and exact fixture admission are
unchanged.

`build.py --original-fixture` now separately publishes a production host library
without test exports and starts `fixture_cold.c` in a fresh process. Its first
managed call must successfully compile the original fixture; it then verifies
reflection, failure canaries, gate release and unchanged output with different
parameter bits. All **16,541 cold checks**, the existing **7,087 mutation checks**,
the old probe's **four checks**, independent Vulkan1.1 SPIR-V validation, and
iOS static/shared build/link passed. The original warmed stage is preserved;
no canonical framework has been replaced. The production host library is
`WORK/repro/host-production-publish/SatchelShaderProbe.dylib`.

## Sources and licenses

- [Pinned engine translator](https://github.com/seedds/nxnative/tree/f8a5618d5f505a4c0a0bcac7c413872d3a53c009/dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader)
  for the game translator, and the
  [NX2Apple translator](https://github.com/LNPANDY/NX2Apple/tree/8227f80f9b381c680611303319656c4e0c806a50/dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader)
  for the original fixture: Ryujinx Team and Contributors, MIT. The accessor port is from
  nxnative (MIT, `licenses/nxnative.MIT.txt`).
- Vendored Spv.Generator: MIT; its complete license is retained separately.
- .NET NativeAOT: MIT and third-party notices from runtime pack 10.0.14.
- [Microsoft's iOS NativeAOT framework/static-library instructions](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/ios-like-platforms/creating-and-consuming-custom-frameworks).

Full license and notice texts are in `licenses/` and included in the framework.
The build does not bundle SPIRV-Cross; that component has separate notices in
`Tools/ShaderBridge`.
