# Scalar compiler provenance

The C compiler, bounded admission/IR, SPIR-V emitter and original test programs
are independently written for Satchel. No upstream translator implementation
is compiled, linked or vendored by this component. No third-party runtime dependency
or game shader is embedded. Project ownership applies to the original code;
this notice does not impose a new project license.

Instruction field/semantic references were inspected from Ryujinx Graphics.Shader
in NX2Apple commit `8227f80f9b381c680611303319656c4e0c806a50`:
Decoders/InstDecoders.cs, Decoders/InstTable.cs, Decoders/Decoder.cs,
Instructions/InstEmitMemory.cs and Instructions/InstEmitIntegerArithmetic.cs.
The upstream MIT notice is retained as Reference-LICENSE.txt for attribution.
The source lock records these reference hashes; this is provenance only, and a
reference checkout is not needed to reproduce the independent implementation.

SPIR-V definitions follow Khronos SPIR-V1.3/Vulkan1.1. The installed spirv-val
command is a development validation tool, not a linked runtime dependency.
