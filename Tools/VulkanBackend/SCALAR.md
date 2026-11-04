# Independent bounded scalar SPIR-V backend

The scalar entry points complement the existing exact-original shader fixture.
They consume dynamically generated SPIR-V from the checked scalar compiler;
they do not broaden the NativeAOT fixture facade or admit arbitrary game shaders.

`satchel_vulkan_scalar_pipeline_create` copies code/reflection and independently checks
the entire fixed-capacity SPIR-V schema: Shader-only integer compute1x1x1, declared
UBO/SSBO types/layouts/bindings, defined IDs and operation order, constant-index
UBO loads, uint32 addition and ordered constant-index SSBO stores. Derived resource
extents and stores must match reflection. Branches, dynamic indexing, extra entry
points, unsupported types/capabilities and invalid IDs reject before driver use.
The backend consumes the caller's actual copied code, never a static substitute.

Submission requires real buffer usage, parent, alignment, range and retention.
It checks the current UBO pointer pair against the expected guest storage address.
The checked guest owner must independently prove original CB/storage/code physical
separation; separate uploaded Vulkan buffers are not proof of guest disjointness.
Pipeline/descriptors/buffers remain owned until the actual fence is consumed,
including nonzero-token errors, cancellation and timeout.

Public scalar compiler declarations are sourced from Tools/ScalarCompiler. The
local forwarding header avoids divergent copies; framework packaging copies the
complete current declaration, preserving all include orders. Existing original
compute entry points and report ABIs remain unchanged. MoltenVK1.4.2 and transitive
licenses/pins are unchanged; no new external dependency is introduced.

Earlier independent schema/GPU gates passed7,512 sanitizer assertions and24 real
GPU executions across four independently encoded programs and accepted schema
mutations. Integration gates are documented in ../GuestGraphics/NVN-COMPUTE.md.
The current original interpreted NVN diagnostic is not imported game execution.
