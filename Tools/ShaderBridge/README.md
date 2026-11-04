# On-device SPIR-V to Metal shader bridge

Implemented compiler stage: a bounded C ABI around the same SPIRV-Cross source
revision as NX2Apple's bundled tool. It generates **iOS MSL 2.3 in memory**; the
consumer compiles that source with `MTLDevice.newLibraryWithSource`, creates a
Metal pipeline, binds resources, and submits commands. No executable guest CPU
memory, external compiler process, per-game Mac preparation, or shader payload
logging is involved in this library.

This does **not** implement NVN/Maxwell command submission, GPU addresses, texture
layout conversion, synchronization, guest shader extraction, or game graphics.
The app must feed reviewed translator output; this is not a public raw shader
import path. SPIRV-Cross is a compiler, not a complete SPIR-V semantic validator
or hostile-code sandbox. Input/ID/instruction-count bounds and exception handling
do not establish hard limits on compiler CPU time or intermediate allocation.
Keep calls off the UI thread and do not claim arbitrary malformed shader safety.

## Dependency and reproducible build

Upstream: https://github.com/KhronosGroup/SPIRV-Cross

Exact revision: `f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7`
(release tag `vulkan-sdk-1.4.363.0`). This matches the installed reference recipe
`/tmp/satchel-switch-integration/NX2Apple/dm/gfx/tools/spirv_cross_bin/build.sh`.
That checkout includes only the tool binary, so the **same dependency's source**
was fetched for static builds. Source is unmodified and lives outside the repo.

```sh
mkdir -p /tmp/satchel-shader-bridge
git clone --depth 1 --branch vulkan-sdk-1.4.363.0 \
  https://github.com/KhronosGroup/SPIRV-Cross.git /tmp/satchel-shader-bridge/spirv-cross
zsh scripts/test_shader_bridge.sh --ios
```

The build checks exact HEAD and a clean upstream working tree. It uses installed
Xcode clang/libc++, C++17, exceptions enabled; only six upstream implementation
files are compiled: `spirv_cross`, `spirv_parser`, `spirv_cross_parsed_ir`,
`spirv_cfg`, `spirv_glsl`, and `spirv_msl`. No CLI, HLSL backend, JSON reflection,
SPIRV-Tools, GLSL compiler, package manager, or new toolchain is required.
Build outputs are under `/tmp/satchel-shader-bridge/{macos,ios}`. Mac archive/tests
use AddressSanitizer and UndefinedBehaviorSanitizer with halt-on-error. iOS is an
arm64 iOS17+ static archive without sanitizer runtimes. It has been cross-compiled,
not executed on a device by this test.

`LICENSE` and all `LICENSES/` files are copied verbatim from the pinned source.
Preserve them in distributed app notices. The main library is Apache-2.0;
individual generated SPIR-V/Khronos headers have their upstream notices.

## C / Swift / Metal integration

Include `ShaderBridge.h` in the bridging header. Link
`/tmp/satchel-shader-bridge/ios/libSatchelShaderBridge.a` and `libc++`. The bridge
itself has no Apple framework dependency; Metal/Foundation are required by its
consumer. Do not use the sanitized Mac archive in the iOS app.

Call `satchel_shader_spirv_to_msl` with SPIR-V bytes, caller-owned UTF-8 output storage
(up to 2 MiB), and `SatchelShaderInfo`. On success `source_bytes` excludes the NUL;
`entry_point` is a fixed sanitized entry name. The metadata describes stage,
compute local size, and declared resource bindings. Read outputs only on success.
Error values are stable status codes and never carry compiler messages or source.

Current supported envelope: exactly one vertex, fragment, or compute entry point;
SPIR-V1.0–1.6, <=256 KiB, <=65536 IDs, <=16384 instructions. Resources use explicit
descriptor-set0 bindings0–23, with the same Metal index in their respective
buffer/texture/sampler namespace. Duplicate indices within a namespace are
rejected, as are descriptor arrays, combined samplers, push constants,
specialization constants, unsupported stages, and shaders requiring auxiliary
Metal buffers. All declared resources remain in the metadata. Each binding has an `active`
field (0/1), derived from the pinned `get_active_interface_variables()` API's
reachable-control-flow analysis for the selected entry point. This does not
execute the shader or prove that every dynamic branch accesses a resource.
The original EXIT fixture declares support_buffer at binding0 but has active=0;
the store fixture's buffer0 has active=1. SPIRV-Cross's MSL backend itself filters
unused interfaces during compilation; metadata never pretends declarations are
absent. Consumers must rebuild against the updated C struct layout.
Compute local sizes must each be positive and their product <=1024; additionally
check the actual pipeline/device limits before dispatch.

The API checks module framing and catches C++ exceptions. It supports unaligned
input by copying words before parsing. Invalid null/size/overlap arguments return
without writing caller memory; other failures clear valid output metadata and
source byte zero. Source size is checked after compilation; the output cap is
not an intermediate allocation cap. All input/output buffers must be accessible
for the supplied lengths and mutually disjoint.

## Meaningful validation

`Fixture.h` is an original hand-authored SPIR-V compute shader: one invocation
writes `0x13579bdf` to buffer0. `MetalTest.mm` translates it to iOS MSL, compiles
that source with the native Mac Metal API, dispatches one thread, and verifies
the expected uint and three untouched neighboring guard words. This tests real
shader conversion and GPU execution, not a hand-drawn frame or game rendering.

`Test.cpp` checks binding/local-size metadata, output-capacity failure, every
truncated fixture boundary, zero/oversized instruction word counts, ID bounds,
magic, unsupported descriptor set/binding/workgroup limits, unaligned input,
invalid arguments/overlap, and 1000 deterministic malformed framing cases under
ASan/UBSan. These are bounded framing/selected semantic cases, not complete
semantic fuzz coverage.

Optional original Maxwell EXIT pipeline smoke test:

```sh
zsh scripts/test_shader_bridge.sh --ios --exit-spv /tmp/satchel-shader-aot/exit.spv
```

That SPIR-V is produced by the separately tested NativeAOT Maxwell translator
from original scheduler+EXIT instructions, not a game shader. It validates the
translator-to-bridge ABI, declared-versus-active resource metadata, iOS MSL
generation, and native Mac Metal compilation/dispatch without active bindings. EXIT alone writes no output, so
it does not demonstrate pixels or meaningful rendering. The buffer fixture's
native Metal dispatch provides the separate observable GPU check.
