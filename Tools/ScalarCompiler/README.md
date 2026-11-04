# Bounded scalar compute compiler

This original no-JIT C component admits a narrow Maxwell compute program into
fixed-capacity IR and emits SPIR-V from that actual IR. It is independent of the
separate C# exact-fixture diagnostic, and neither runtime loads a replacement
shader to pretend an arbitrary program compiled. Unknown instructions stop.

Build from the repository with:

```sh
zsh scripts/test_scalar_compiler.sh --ios
```

The default output is `/tmp/satchel-scalar-compiler`, with
`macos/libSatchelScalarCompiler.a` and `ios/libSatchelScalarCompiler.a`. Use
`--output /absolute/scratch/path` for isolated validation. Nothing is downloaded,
installed, signed or written into the repository. Required tools are installed
Apple Xcode clang/libtool on macOS arm64, Python3 and `spirv-val` with Vulkan1.1
support. The builder verifies tool capabilities, the checked source lock and
all16 generated original fixture hashes, then records exact tool versions,
source/archive hashes, flags and exports in `validation-manifest.json`.

The tested toolchain was Apple clang21.0.0 and SPIRV-Tools2026.4. Archives contain
DWARF paths, so moving source/output directories may change archive hashes even
when code/header/test hashes and emitted modules remain identical. App builds
must use a jointly validated matching compiler/header/graphics checkpoint.

`satchel_scalar_admit` consumes actual copied compute instruction bytes and control
metadata without CB values, addresses, dispatch groups or invented device
state. It returns an immutable1,736-byte pointer-free copied code/IR object and
deferred CB/storage requirements. The owner commits it transactionally with the
capture generation; failure preserves the old program. Admission is not Vulkan
pipeline creation or execution. Before NVN SetShaders success, the owner also
checks actual Shader/Vulkan1.1/descriptor/range capabilities.

`satchel_scalar_compile_program` re-decodes and checks the complete canonical IR,
then enforces real dispatch groups, CB extent/pointer values, storage ranges,
alignment, device limits and physical-disjointness proof. Its copied program
supplies code/control metadata; dispatch supplies actual resources. The owner
must retain/revalidate the exact program/pool identities and GPU-visible CB/code
snapshots, and prove writable output disjoint from both source CB and code/C1.
Opaque Vulkan buffers alone do not prove original guest memory disjointness.

The shared decoder admits only direct LDC32/64, plain unconditional IADD32,
checked STG32 through an intact CB-loaded pointer pair, and EXIT, with exact
predicate/modifier masks. It rejects undefined registers, branches, atomics,
images, floating-point operations, local/shared accesses and unknown encodings.
Local/shared sizes may remain unknown only under unused-memory proofmask3;
actual local and dispatch dimensions remain1³. One CBslot is derived from code;
the NVN adapter separately limits it to uniform-mappable slots3–17.

Bounds:512 instruction bytes,32 semantic operations,64 IR nodes,16 stores,
256 registers,4096 SPIR-V words, one64KiB declared UBO and one checked storage
range≤1MiB. Maximum deferred CB read extent is32772 bytes. No heap, callbacks,
recursion or unbounded loops occur in the compiler. Clang-O1 stack audit found
3,920 bytes for admission,20,336 for compile,3,760 for compile_program: maximum
nested production path28,016 bytes before instrumentation.

Validation covers853,611 existing compiler/native-value fixture assertions and
20,365,499 admission/failure-canary assertions under ASan/UBSan. Four independent
original programs produce distinct SPIR-V validated by spirv-val. All generated
SPIR-V/native-expected/CB/Maxwell fixtures match the earlier frozen checkpoint.
Separate graphics tests must validate real GPU execution; these compiler tests
alone do not establish guest-originated rendering or game compatibility.

See `NOTICES.md`, `Reference-LICENSE.txt`, `source-lock.json` and the retained
`admission-checkpoint.json` for provenance. The implementation is original;
public instruction-layout references are pinned, not built or vendored.
No imported shader/code/CB/keys may be logged or persisted by these APIs.
