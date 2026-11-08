# Native shader integration

Status: the public API and general library built and passed Mac checks. Full
Zelda native parity and iOS linking have passed. The `any-game` worker owns
Prepare and host integration and has reported its first Mac rendering check.
M5 has performed no phone action. The optional publication C bridge has a
separate validation status in `RuntimeIntegration.md`.

## Translate one record

`ShaderAPI.swift` exposes `SatchelShaderRequest`, `SatchelShaderTranslator` and
`SatchelTranslatedShader`. `integration.json` lists the shipping Swift files,
`ArchiveTranslationBridge.h`, linked libraries, frameworks and notices. Do not
include any harness or Python file in the app. Build the archive decoder with
`build-archive.py iphoneos`, then the library with `build-api.py iphoneos --general`.

```swift
let request = try SatchelShaderRequest(
    control: copiedControl, code: copiedCode, stage: .vertex,
    factsJSON: capturedQueryFacts, wantedPixelLocations: observedPixelInputs)
let translated = try SatchelShaderTranslator.translate(request) { isCancelled }
// translated.sources["b"] is base MSL. All generated variants are also returned.
// translated.reflectionJSON contains bindings, entry points and variant metadata.
```

The alternate initializer takes a single `BTCDBTCD` record. Its layout matches
the pinned splitter and the games worker's `capture_shaders.py`: 0x38-byte
header, code, then control. Header offsets 0x28 and 0x2c contain little-endian
control and code lengths. The API validates the existing canonical key and
retains full SHA-256 record and request identities. `expectedKey` checks a
captured engine key. `skippedControlRanges` supports reviewed engine key masks.
No title ID, Mac directory, profile table or subprocess is required.

Runtime hooks must copy bytes while the NVN program owns them. Bind and draw
observations must supply actual effective vertex/pixel pairings. Do not infer
pairs by adjacency. Discovering additional pixel inputs can change vertex
padding and the request identity, requiring another prepared result.

Query facts are UTF-8 JSON with the pinned `tex`, `img`, `attr`, `fout`, `local`,
`shared`, `early_fragment_tests`, `debug`, `reduced` and `softfloat64` fields.
Facts cannot replace identity, stage, code or c1. Compute dimensions and aligned
shared memory are read from control when no explicit override is present.
Other absent queries retain the pinned defaults and reflection diagnostics.
Runtime capture by itself does not resolve every texture and vertex-format query.

The pinned accessor accepts this query shape (illustrative facts, not a fixture
from a game):

```json
{
  "tex": {"0": "2d", "128": "cube"},
  "img": {"0": {"type": "2d", "fmt": "R32Uint"}},
  "attr": {"0": "float", "1": "uint"},
  "fout": {"0": "sint"}
}
```

Texture type strings are `1d`, `1darray`, `buffer`, `2d`, `2darray`, `2dms`,
`2dmsarray`, `3d`, `cube` and `cubearray`. Image `fmt` uses the pinned Ryujinx
`TextureFormat` enum name, such as `R32Uint`. `attr` keys are vertex locations
0 through 31; `fout` keys are color output locations 0 through 7. Their values
are `float`, `sint` or `uint`. Use bound view/format state, not only shader
instruction guesses. The current pinned accessor falls back to 2D and
R8G8B8A8Unorm for missing texture/image answers and records that in reflection.

For the pinned NVN c2 handle layout, a combined texture key is
`(byteOffset - 0x20) / 8` below `0x168`; a separate texture key is
`128 + (byteOffset - 0x168) / 8` below `0x568`; an image key is
`(byteOffset - 0x120) / 8` below `0x160`. Offsets must be aligned to eight
bytes. The handle unpacking and bank checks in `GameShaderTranslator.Accessor`
are authoritative. A new title's binding state must supply these answers;
the successful opening-screen checks do not prove later queries are covered.

The output's source dictionary uses `b`, `bv`, `bp`, `bx`, `bf` suffixes. `bx` is the one side
source for a pixel shader's alpha test (`<entry>_a`) and logic operation (`<entry>_l`) entry
points, as the pinned engine's `dmsh_side.py` writes it; it replaced the separate `ba` and `bl`
sources at the engine pin move of 2026-11-05.
Use the reflection entry names. Do not assume every variant exists. c1 proof,
constant-buffer extents and unwritten-storage binding analysis accompany it.
MSL and reflection are private game-derived artifacts, never diagnostic logs.

All six stages are prepared. Geometry
uses the pinned compute conversion and supplies five topology kernels plus its
raster reader. Call `translated.reflection(variant:)` for each geometry variant;
`bgv` has vertex-reader bindings, distinct from the geometry kernels. The base
`reflectionJSON` remains available for ordinary variants. Public geometry analysis
uses conservative full constant-buffer ranges and no unwritten-storage suppression.
Hull and domain requests (since 2026-11-07) return one `b` source each: the hull
as a kernel reading the vertex capture, the domain shader as a plain vertex function
with `main0` and `main0_z` entries. Their reflection's `tessellation` object holds
the host contract (record sizes, buffer indices, capture words, output vertices,
and for a domain shader its domain, spacing, winding and point mode); the compute
tessellator is `SatchelShaderTessellator.source`. A domain request's facts may add
`tess_domain` (`isolines`, `triangles`, `quads`), `tess_spacing` (`equal`,
`fractional_even`, `fractional_odd`) and `tess_cw`; other stages refuse them. No
such records occur in the available Zelda, Odyssey and Castlevania inventories; the
host draws patches through this contract since 2026-11-08 (synthetic Mac checks only). This API is title independent, not a claim
that all games or stages are supported. See `RuntimeIntegration.md` for the host
compute contract and `PrepareWarmup.md` for bounded Prepare warm-up and timings.

Translation is synchronous and uses an autorelease pool per call. Schedule at
most two calls at once for the current validation. Cancellation is checked before
and after native preparation; an in-flight NativeAOT or SPIRV-Cross call cannot
be interrupted safely. Report per-record progress when a call returns.

## Compile and cache contract

`SatchelShaderCompiler(device:)` defaults to two compilation slots and an explicitly
bounded live library cache. Optional warm-up can configure up to four slots with
`setMaximumConcurrentCompilations(_:)`, restoring two before returning to gameplay. Use `compile(translated, variant: "b", cancelled:)`.
It returns the `MTLLibrary`, source SHA-256, device identity, elapsed compile time,
live-cache hit status and layer-output flag. Same-source concurrent calls are
coalesced. Call `trimLiveLibraries()` on memory pressure.

`SatchelShaderStore(directory:deviceIdentity:maximumBytes:reserveBytes:)` persists
checksummed MSL/reflection and analysis, keyed by the complete normalized request.
Use the compiler's `deviceIdentity` and one shared store per partition.
`prepare(request,cancelled:)` reloads a valid entry or translates and saves it.
Corrupt entries rebuild; failed writes do not produce successful cache markers.
Storage defaults to 2 GiB with a 512 MiB free-space reserve. The app should supply
its own storage budget. Source-cache hits do not imply executable-library hits.

`SatchelPipelineArchiveCache` persists actual compute/render pipeline archives in
a separate bounded device/OS partition. `compute(descriptor,identity:cancelled:)`
and `render(...)` return the pipeline and distinct persistent-hit/persisted flags.
The identity should include shader hashes, entry points and all pipeline state.
Metal independently checks the actual descriptor using `failOnBinaryArchiveMiss`.
A stale or mismatched archive rebuilds. Archive write failure is reported by
`persisted=false`; it does not invalidate an already-created pipeline. Pipeline
archive operations are currently serialized per cache instance.

```swift
let compiler = SatchelShaderCompiler(device: device)
let store = try SatchelShaderStore(directory: sourceCache,
    deviceIdentity: compiler.deviceIdentity)
let shader = try store.prepare(request) { isCancelled }
let compiled = try compiler.compile(shader, variant: "b") { isCancelled }
// compiled.library.makeFunction(name: reflectionEntryName)
```

The proven runtime options are MSL 3.2, relaxed math, preserved invariance, default
optimization and executable libraries. There is no public serializer for a
source-created executable `MTLLibrary`. Metal's system cache can be evicted and
pipeline archives can be invalidated by OS changes. The API must expose rebuilds
honestly instead of promising that source compilation happens once forever.

## Discovery contract

`SatchelShaderDiscovery.discover(input,origin:decoder:cancelled:emit:)` discovers
BNSH and BAHS records, including nested SARC members. `SatchelShaderInput` wraps
`count` and a synchronous bounded read closure. Each read is at most 1 MiB.
Each emitted record includes the canonical key, request, origin and optional
archive program variation. Group only explicit program identities when deriving
pairs. BNSH and recognized BAHS variation tables supply those identities.

`SatchelZstdArchiveDecoder` expands one frame at a time into private spill storage,
with a 128 MiB maximum window, 512 MiB default expanded-file limit and free-space
reserve. It removes its spill after the borrowed read view returns, including
errors/cancellation. Use one decoder per content view. `loadDictionaryArchive`
loads an explicitly supplied zstd/SARC dictionary archive, such as the file at
`Pack/ZsDic.pack.zs` in TOTK. It never imports another title's dictionary IDs.
Missing dictionaries fail explicitly. Multi-frame and other compression formats
are not yet accepted by this adapter.

Archive discovery and runtime capture feed the same request type. Enumeration and
reads must use the authenticated content pipeline already on the phone. Archive
adapters should emit one bounded control/code snapshot at a time. Unknown archive
formats require first-use NVN capture, not a success claim with zero shaders.
Observed coverage does not establish whole-game shader coverage.

The reusable MIT capture hook and synthetic fixtures live in the games worker's
`NativeGames/RuntimeCapture.inc`, `runtime-capture.patch` and
`test_capture_adapter.m`. The any-game host overlay adds replacement/finalization
lifetime management for copied controls. Reuse it and deduplicate persistent requests.
No host or phone capture has been launched by m5.

## Build artifacts

The iOS builds are available under `~/SatchelBuild/m5/`: the NativeAOT framework
in `aot/SatchelShaderProbe.framework`, SPIRV-Cross static library in
`cross-iphoneos/libGameShaderBridge.a`, and decoder in
`archive-iphoneos/libShaderArchive.a`. Compile the Swift sources listed in
`integration.json` into the app, import `ArchiveTranslationBridge.h`, link the two
static libraries and C++, and embed/sign the NativeAOT framework with the app's
existing signing setup. The `api-general-iphoneos` dylib proves linking; it is not
a packaged shipping framework. No certificate or phone operation was performed.

The full game facade and the original fixed diagnostic fixture are separate
build modes. The full facade exports `satchel_shader_translate_v1` and its free
function, but does not export `satchel_shader_fixture_compile`. An app still using
that fixture must retain its original fixture framework. The any-game staging
code preserves it as `SatchelShaderProbe.framework` and relocates the full game
facade to `SatchelShaderTranslator.framework`, updating its install name and
checking which imports bind to each. Use that app staging path; do not overwrite
the fixture framework or embed two frameworks with the same name. M5's standalone
link-check artifact still uses the original full-facade build name above.
