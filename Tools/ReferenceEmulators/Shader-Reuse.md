# Shader compiler reuse assessment

Read-only source review,2026-10-31. No compiler/dependency/canonical artifact was changed.

## Recommendation

Reuse the installed NativeAOT Ryujinx translator before adding Eden. We already
compile a broad Maxwell decoder/IR/SPIR-V implementation; the exported admission
and state/resource adapter is narrow. Eden does not remove those integration
obligations or establish an iOS performance advantage.

## Installed path and concrete gaps

[Current project](../MaxwellShaderCompiler/MaxwellShaderCompiler.csproj)
includes all Ryujinx.Graphics.Shader and Spv.Generator sources from
[NX2Apple8227f80f](https://github.com/LNPANDY/NX2Apple/tree/8227f80f9b381c680611303319656c4e0c806a50/dm/third_party/ryujinx/src).
The216 locked inputs are already NativeAOT-built with.NET10.0.403/runtime10.0.14.
`Translation/Translator.cs` CreateContext/DecodeShader reads graphics SPH80bytes
and instructions; `TranslatorContext.cs` supports Translate, SetNextStage and
combined vertex contexts. This is not an EXIT-only underlying translator.

The public Compiler.cs facade only takes code and uses IGpuAccessor defaults:
local4096/shared49152/workgroup1³/default graphics facts. It throws for binding
allocation. FixtureCompiler.cs provides strict overrides/Log rejection for one
exact original64-byte compute program. Neither interface admits arbitrary
imported shader snapshots. ScalarCompiler and staged RasterCompiler are separate
fixed-capacity original emitters; ShaderBridge is downstream SPIRV-Cross→MSL,
not another Maxwell compiler.

Required next contract: immutable actualSPH/instructions/C1/stage/control,
explicit known/unused state, actual enabled device capabilities, checked
resource snapshots, generation identity and bounded output/reflection. Existing
paired Program/pool/retirement ownership is reusable. Input/output byte caps do
not bound upstream CFG/IR/optimization allocations or CPU work.

Two concrete existing hazards require staged review:
- `CodeGen/Spirv/SpirvGenerator.cs:46–56` unconditionally declares subgroup and
  image capabilities even when unused. Capability emission must follow admitted
  semantics and actual generated operations, not general declaration stripping.
- `Translation/Optimizations/GlobalToStorage.cs:239–252` logs failed resolution
  then substitutes zero loads/deletes stores. A strict throwing Log override
  prevents this path; all newly reachable fallback paths still need auditing.
  The exact-fixture normalizer cannot safely be generalized by renaming it.

## Eden comparison

[Eden943f1fc Environment](https://github.com/eden-emulator/mirror/blob/943f1fc76087adcfc893fb1ca3aea507df15d004/src/shader_recompiler/environment.h)
requires instruction/CB/texture/SPH/state reads, plus caches.
[TranslateProgram](https://github.com/eden-emulator/mirror/blob/943f1fc76087adcfc893fb1ca3aea507df15d004/src/shader_recompiler/frontend/maxwell/translate_program.cpp)
and EmitSPIRV provide genuine general translation, but AddNVNStorageBuffers
assumes stage-specific driverCB0 descriptor tables. Our high-level NVN owner
must supply a coherent real binding representation, whichever compiler is used.
[Profile](https://github.com/eden-emulator/mirror/blob/943f1fc76087adcfc893fb1ca3aea507df15d004/src/shader_recompiler/profile.h)
is a useful richer device/lowering reference. CFG/pool/vector processing has
no demonstrated bounded wrapper in our app. Adoption adds common/fmt/sirit and
GPL-family implementation/dependency review; installed Ryujinx is MIT.
No speed/memory/compatibility comparison was run.

## Smallest meaningful reuse gate

A new strict straight-line VS/FS NativeAOT profile using actual immutable paired
bytes, initially one checked flat vec4 varying, no descriptors/resources and
no branches. Predecode operands/SPH/defined registers before upstream CFG;
charge instruction/IR/output work and allocations at audited construction
points, stop on logs/unsupported lowering, validate actual emitted interface
and capability set, then run distinct original programs through real Vulkan.
Do not add Eden until a concrete semantic/lowering gap or measured resource
problem justifies the second compiler. Neither source review proves game graphics.
