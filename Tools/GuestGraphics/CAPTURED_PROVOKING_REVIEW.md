# NVN provoking-vertex contract review

Research checkpoint: 2026-10-31. Read-only investigation; no canonical/App/device changes, dependency adoption, private game decoding, or SDK-source use.

**The exact NVN setter ABI, enum encoding and initialization/default behavior remain unverified. Do not enable the setter or mark provoking state known from the evidence below.** Public sources establish useful neighboring contracts, but none establishes all three NVN requirements.

## Evidence and its limits

1. **Pinned project reference:** NX2Apple `8227f80f9b381c680611303319656c4e0c806a50` lists `nvnCommandBufferSetProvokingVertexMode` in its name/thunk tables and the unimplemented section of `nvn_protos.h:538`. No available definition supplies the enum, typed declaration, default, validation or lifetime semantics. This proves a symbol name only. [Pinned reference](https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/nvn_protos.h#L538).

2. **A public NVN reimplementation is only a lead:** TotK runtime `f469c5a7745502357fc1664c04e02522fe92c42a` comments that its candidate setter takes a command-buffer pointer and integer mode; it labels zero the default last-vertex convention. Its body merely reads argument slot1 and emits a diagnostic for nonzero. It does not record/translate mode or include an enum/default conformance test. The file attributes its implementation to another MIT runtime. This is evidence of that runtime's assumption, not verified NVN semantics; it cannot justify either zero→LAST or a no-op success here. [Runtime implementation](https://github.com/valentderah/totk-pc-recomp/blob/f469c5a7745502357fc1664c04e02522fe92c42a/runtime/nvn_ext.c).

3. **Public homebrew API, not NVN:** deko3d `373bfd43d8b2d805fa2eb7d406dbb823032df958` explicitly defines its own First=0/Last=1 enum and initializes rasterizer state to Last. Its bind path emits the boolean `ProvokingVertexLast` method, selecting0 for First and1 for Last; its GPU initialization explicitly writes1. This can support a future checked Tegra-method model, but neither deko3d's enum nor its initialization is NVN ABI/default evidence. [Own enum/default](https://github.com/devkitPro/deko3d/blob/373bfd43d8b2d805fa2eb7d406dbb823032df958/include/deko3d.h), [state emission](https://github.com/devkitPro/deko3d/blob/373bfd43d8b2d805fa2eb7d406dbb823032df958/source/maxwell/gpu_3d_state.cpp), [method0x5A1](https://github.com/devkitPro/deko3d/blob/373bfd43d8b2d805fa2eb7d406dbb823032df958/source/maxwell/engine_3d.def), [explicit initialization](https://github.com/devkitPro/deko3d/blob/373bfd43d8b2d805fa2eb7d406dbb823032df958/source/maxwell/gpu_3d_base.cpp).

4. **Verified destination API:** Vulkan defines First=0/Last=1; absent the provoking-state extension structure, pipelines use First. Last requires the corresponding enabled feature. These facts specify Vulkan, not NVN. Khronos explicitly explains that API conventions differ; OpenGL's Last convention is likewise not an NVN default. The current original FIRST-only renderer tests prove the destination behavior, not source state. [Vulkan state specification](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html), [Khronos extension rationale](https://github.com/KhronosGroup/Vulkan-Docs/issues/1527).

The differing numeric claims above are an additional reason not to reuse another API's enum. They are not evidence that any one NVN mapping is correct.

## Unresolved NVN contract

| Required fact | Status |
|---|---|
| Symbol exists | Verified in pinned public reference |
| Exact argument types/widths and return type | Unverified; candidate public stub is insufficient |
| Numeric FIRST/LAST values; other valid values | Unverified |
| Initial value, reset and inheritance behavior | Unverified |
| Whether BeginRecording/queue creation/call-recording resets or inherits it | Unverified |
| Invalid-value handling | Unverified |

Public search used the exact setter, likely enum/type spellings and NVN/LWN-qualified terms, plus available local references. Exact enum/type searches found no usable definition. Search absence is not proof no legitimate source exists. Results hosted under third-party `NintendoSDK/include/nvn` trees were deliberately not opened or used; public hosting alone does not establish acceptable SDK provenance. No proprietary/leaked SDK was requested or adopted.

## Safe capture contract once independent evidence is available

The following is a proposed emulator design, **not a claim about NVN defaults**:

- Retain an explicit unknown state until a verified source operation or actual checked GPU-method write establishes a value. Keep raw NVN values separate from the internal FIRST/LAST representation; use an explicit validated translation, never a numeric cast.
- A setter must first validate the live owned command-buffer identity, recording state and complete proven ABI. Only then append an immutable typed state command. Refusal must preserve the prior state and command list.
- Replay updates queue state in recorded order; each draw captures its own immutable value. Preserve identity/generation and command-handle lifetime checks. Recording/replay reset and inheritance rules require separate source evidence; do not invent them from Vulkan.
- A verified LAST request must remain unsupported until the renderer genuinely supports LAST or a tested semantics-preserving transformation. Never silently map it to FIRST.
- Test distinct per-vertex flat colors for FIRST/LAST, repeated changes, independent buffers, deferred/called recordings, invalid values, generation reuse, failed recording and source/target cache-key separation.

An alternative is faithful observation of actual Tegra method0x5A1 in a future implemented guest-driver command stream. That would establish observed hardware state through a checked path without guessing an NVN enum. The current HLE interception does not implement that path; this is future work, not a fallback that can be turned on now.

The current strict shader pair transports nonconstant flat varyings from vertex attributes, so provoking selection can change rendered color. Its source admission cannot prove provoking mode irrelevant. Source-only proof that YNegate is unused is a different fact: it neither sets provoking mode nor relaxes existing ABI2 known-state requirements. Keep the current unknown-provoking and known247/YNegate boundaries explicit.

## What would close the gap

A legitimately published original NVN implementation/ABI description with provenance and tests, or independent authorized black-box tests demonstrating setter values plus initialization/reset/inheritance behavior, could justify a narrow implementation. A raw observed integer argument or a successful Vulkan FIRST fixture alone cannot. No implementation change is justified by this research checkpoint.
