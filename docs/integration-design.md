# Satchel native integration design

2026-11-01. Worker: `design`. Status: design completed by source inspection; implementation and device feasibility await validation. This task changed only this document. Nothing was built, installed, launched, or played.

Use NX2Apple's shared Horizon host and NVN-on-Metal renderer inside Satchel, with one prepared package per game in the app's data container. Port preparation to native libraries: authenticated extraction and update composition, ARM64 rewriting, Mach-O packaging and signing, shader extraction and translation. A signed dylib supplies executable pages to the existing guest loader; it does not replace that loader with Apple's symbol resolver. Preserve Satchel's library, imports, landscape UI and bundle identity.

The route is conditional on the plan's S1 and S2 results. The pinned source demonstrates remapping code already signed into an app, not generating a dylib in the app container, signing it there and executing its remapped pages. A valid signature alone is not evidence that all three operations work under the actual provisioning profile. Do not commit major porting effort until the `spike` worker establishes that boundary. This is the feasibility gate already specified in [PLAN.md](../PLAN.md), not a new permission requirement.

## Evidence and notation

`N` references below link to source at NX2Apple commit `8227f80f9b381c680611303319656c4e0c806a50`, inspected at `/tmp/satchel-switch-integration/NX2Apple`. The checkout was clean when inspected. Paths and line intervals in citation labels are the evidence anchors; links open the first line. `P` references identify Satchel files. Recommendations, estimates and proposed interfaces are design judgments, not existing capabilities. Upstream game notes are reported evidence, not a phone test performed by this worker.

The reference docs are a troubleshooting history. Where prose and implementation disagree, the pinned implementation determines the design. For example, the graphics README mentions moving final compilation to the phone, but `shader_load` at this pin loads `.metallib` files for translated game shaders. Runtime compilation already exists for host utility shaders. A game-shader source-loading path is still work. [N01: dm/gfx/README.md:3-5][N01] [N02: dm/gfx/nvn/nvn_mtl.m:3871-3889][N02] [N03: dm/gfx/nvn/nvn_mtl.m:4043][N03]

## 1. The per-game runtime package

Separate preparation inputs from what the host needs to execute. Keep original user imports in the existing library. Publish prepared outputs atomically into a versioned private game directory, keyed by title, selected content/update, module build IDs, rewriter/profile versions and shader-chain version. Never put decrypted artifacts in this repository. The proposed package has these parts:

| Part | Existing form and runtime use | Satchel form |
|---|---|---|
| Executable modules | Rewriter emits `<module>.patched.bin`. On iOS `gen_embed.py` splits this into page-aligned original text `x0`, writable image `W`, patch code `x1`, and optional back patch code `x2`. The executable currently carries those sections. | One generated signed game dylib with those pieces and a bounded module directory. Keep original guest-relative layout metadata; do not expose guest ELF imports as Mach-O imports. |
| Layout | `<module>.fixups.tsv` is a header-only layout description: text/ro/data boundaries, patch offsets, key-page offset/size/indices, build ID, optional full image/file lengths, back patch region and frame-step state. It is not a list of instruction writes to apply after loading. | Initially retain TSV for parity, plus a versioned manifest that binds it to the dylib's content hash. Validate lengths and offsets before mapping. |
| Gates | `<module>.gates.tsv`: return-point offset, gate kind, integer argument and original word. The host adds the module base and registers dispatch entries. | Required for modules with gates. Preserve table and gate ABI exactly, including observation and spin gates. |
| Rewrite diagnostics | `.sites.tsv` describes rewritten sites/windows/jump tables/spin loops; `.cuts.tsv`, `.obs.tsv`, optional `.match.tsv`, and `build_src.sha256` describe preparation. The iOS packer copies cuts/obs but not sites. | Keep preparation provenance privately; sites are not required by the ordinary runtime loader. Do not mistake cuts/obs reports for additional load-time patches. Avoid exposing byte-bearing reports through normal logs. |
| Runtime profile | `game.tsv` holds identity, ordered modules, builds, memory settings, graphics switches and shader discovery settings. `game.py profile` exports resolved host settings; `DM_PROFILE` selects a file. | Ship reviewed profile metadata with Satchel, resolve defaults into an explicit per-game runtime profile, and pass it before any host initialization. Python patch code is not needed by the running host. |
| NVN shader assets | Shader key-to-stage map, `b-<chain>/` libraries, reflection JSON, inputs stamp and optional variants. `shader_facts.tsv`, manual facts and pairs are preparation inputs. | For M4, import a private Mac-prepared **iPhoneOS** shader package with matching chain and profile-input hashes. For M5, produce equivalent reflection/MSL on the phone and instantiate libraries through a library-provider API. |
| Warm-up and analysis | `pso_manifest.tsv` seed and `pso_user.tsv` recorded order; `shader_pairs.tsv`/`pso_pairs.tsv`; `c1_full.tsv` and `cb_extent.tsv`; optional GPU-specific offline archive. | Keep seeds with the package, mutable warm-up/archive data in per-game caches. Missing/stale c1 or extent analysis must take conservative full-buffer behavior. Optional pipeline archives are an optimization, not game content. |
| RomFS | A directory tree or NX2Apple's custom `DMROMFS1` image selected by `DM_ROMFS`. This is not a raw Nintendo RomFS or an encrypted NCA. | First use a streamed, authenticated `DMROMFS1` writer on the phone. Later a virtual-filesystem adapter can avoid duplicating decrypted data. |
| DLC | `DM_AOC_DIR/<decimal index>.img`, or `<index>/` directories. The host discovers owned indices and mounts them read-only. | Extract only supplied matching DLC. Index is DLC title ID minus base title ID minus `0x1000`; include DLC shader content in the shader-input identity. Empty set means no DLC. |
| Mutable and common resources | `DM_SAVE`, `DM_TEMP`, fonts, optional cheats and presentation settings. | Per-title saves and scratch directories, separate from prepared immutable content. Common fonts and generic runtime assets belong in the app once. |

Executable and table contracts: [N04: dm/tools/patch_nso.py:1-23][N04], [N05: dm/tools/patch_nso.py:1495-1524][N05], [N06: dm/ios/tools/gen_embed.py:1-60][N06], [N07: dm/host/loader.c:185-220][N07], [N08: dm/host/loader.c:247-267][N08], [N09: dm/ios/tools/build_ipa.sh:65-82][N09]. Shader resource contracts: [N10: dm/gfx/README.md:42-59][N10]. Profile contracts: [N11: dm/games/game.py:15-42][N11], [N12: dm/games/game.py:202-229][N12], [N13: dm/host/hle_profile.c:208-265][N13].

`DMROMFS1` uses a 32-byte header, contiguous file bytes, then a directory index of 24-byte entries plus names; children are sorted and grouped for lookup. The host holds an index and uses `pread`, avoiding hundreds of thousands of physical files. The header/index validation rejects incomplete images, but it does not substitute for NCA/IVFC authentication during preparation. Updates must already be composed into the mounted view. `DM_ROMFS_OVERLAY` is a file override facility, not Nintendo update/BKTR composition. [N14: dm/tools/romfs_image.py:1-21][N14] [N15: dm/host/romfs_image.h:28-48][N15] [N16: dm/host/hle_fs.c:433-501][N16] [N17: scripts/build_ipa_from_nsp.py:979-1021][N17]

Some games also need prewritten NROs. Upstream collects, rewrites and maps them before gameplay, with `nro.tsv` carrying names, original SHA-256 and size; activation/relocation happens when `nn::ro` requests a module. Packed NRO sections avoid Mach-O's section-count limit. Preserve that design for M4. TOTK's profile says its main imports `nn::ro`; import presence alone does not prove which NROs a play session will require. Survey the selected content rather than assuming three ExeFS modules are sufficient forever. [N18: dm/host/loader.c:345-405][N18] [N19: dm/host/loader.c:520-565][N19] [N20: dm/games/0100F2C0115B6000/game.tsv:1-54][N20]

### What Satchel already supplies, and what it does not

Reuse the PFS0/HFS0 container inspection and bounded import copy in `SwitchPackage.swift` and `SwitchPackageImport.swift`, CommonCrypto AES helpers, authenticated ExeFS/IVFC reads, NSO LZ4 decoder and RomFS lookup. These are useful foundations, not yet the complete NX2Apple extraction pipeline. [P01: App/SwitchPackage.swift:36-65][P01] [P02: App/SwitchPackageImport.swift:29-85][P02] [P03: Core/SwitchCrypto.c:1-41][P03]

The executable reader accepts one package URL, requires an all-zero rights ID, and rejects several section modes/metadata ranges. The RomFS path requires encryption modes 1 or 3 and empty patch metadata. There is no base-plus-update composition API in these files. Rights-managed content, patch storage/BKTR and content-family selection therefore need explicit support and tests for the chosen TOTK dump. Do not treat PLAN.md's shorthand “decrypt and extract already done” as proof of this case. `NSOLoader.c` explicitly rejects ZBIC and only accepts raw/LZ4 NSO, with bounded image sizes. That is adequate only after the selected modules pass those constraints; supporting every newer format is outside the first launch. [P04: App/SwitchExecutable.swift:39-112][P04] [P05: Core/NSOLoader.c:18-65][P05]

`SwitchRomFS` provides lookup and reads, not a public full-tree enumerator/exporter. `SatchelSwitchStorage` is a serialized, synchronous, read-only byte provider with a 1 MiB bound and staging-buffer ownership. It cannot simply be handed to NX2Apple's path-oriented `nn::fs` hooks, which run on multiple guest threads. Either add enumeration and stream a `DMROMFS1` file for M4, or implement file/directory handles, enumeration, synchronized authenticated reads and lifetime management behind the native filesystem hooks. The former has less runtime integration risk; the latter saves storage and belongs after parity. [P06: App/SwitchRomFS.swift:93-218][P06] [P07: Core/SwitchStorage.h:5-21][P07]

## 2. Rewriter and profiles on the phone

`patch_nso.py` is an ARM64 binary transformer, not an emulator or a compiler from C. It loads NSO/NRO segments, validates them, applies build-bound cuts, identifies code using unwind/function metadata, and replaces instructions or control-flow windows that cannot run unchanged on Apple ARM64. Its work includes:

1. Virtualize guest `x18`, `TPIDR_EL0` and `TPIDRRO_EL0` through host pthread slots, without taking Apple's `x18` for guest storage.
2. Replace SVC and timer reads with host gates, and align dynamic stack adjustments.
3. Rewrite branches through x18, jump tables, exclusive load/store windows and retry paths while preserving control flow; insert yielding spin-loop copies.
4. Apply registered cuts only after matching original bytes. Unknown-build function matching checks unique function/callee fingerprints; missing matches are reported, not guessed.
5. Emit patch areas within ARM branch reach, including negative/front and two-area layouts, key/state pages, and all tables. Code is finalized before signing.

Evidence: [N04: dm/tools/patch_nso.py:1-23][N04], [N21: dm/tools/patch_nso.py:25-60][N21], [N22: dm/tools/patch_nso.py:62-138][N22], [N23: docs/reference/rewriter.md:16-25][N23]. This is substantially more work than replacing `svc` instructions with `bl`.

Dependencies are Python standard-library modules plus Capstone **5.x**, `nso.py`, `nso_asm.py`, `nso_code.py`, `nso_match.py`, symbol helpers, SDK patch tables and selected game profile data. NSO decompression needs `lz4.block`, optionally `zbic` for newer ZBIC modules. `nso_asm.py` is 28 lines of explicit instruction encoders with branch-range assertions. It does not invoke a system assembler. A large-text retry re-execs Python with a revised front-area size; a native port should make that an internal bounded second pass. [N24: dm/tools/nso.py:1-42][N24] [N25: dm/tools/nso_asm.py:1-28][N25] [N26: dm/tools/patch_nso.py:1475-1494][N26]

Profiles are not all data. `game.tsv`, shader facts, maps and pair tables are data, but every `patches.py` is executed through `importlib` at import time, including files for other games whose build IDs may match. `CUTS`, `OBS`, `MATCH_CUTS` and capabilities are mostly tables expressed as Python; `FRAME_STEP` is a callable receiving code-emission and patch-validation callbacks. A source-only AST census found 29 game patch files, 6 containing function definitions, 10 function definitions in total. This count does not make the other files safe to execute as untrusted input. [N22: dm/tools/patch_nso.py:62-105][N22]

TOTK's patch file is 144 lines and implements `frame_step(ctx)` with loops, emitted instructions, PC-relative `B`/`ADRP`, byte checks and writes. It also sets a render-scale capability and registers the function for a specific main build ID. A frozen byte blob alone is insufficient because addresses depend on the chosen patch/state layout. By comparison, Celeste has a plain profile and shader tables, no `patches.py`; it can still receive generic rewrites and SDK-wide patches. [N27: dm/games/0100F2C0115B6000/patches.py:1-144][N27] [N28: dm/tools/patch_nso.py:1156-1190][N28] [N29: dm/games/01002B30028F6000/game.tsv:1-41][N29]

**Recommendation: port the generic rewriter to C/C++ with a Swift preparation coordinator, and represent reviewed profile operations as declarative data.** Use the same Capstone major version and port the exact encoders and matching rules. Convert fixed cuts/observations/fingerprints into bounded records. Represent frame-step stubs as reviewed templates with explicit relocations, symbolically referring to the state page, emitted labels and original module offsets. Initially implementing TOTK's recipe as a compiled native emitter is reasonable; the eventual declarative representation must preserve every original-byte and overlap check.

| Option | Assessment |
|---|---|
| Native port plus declarative profiles | Recommended. Matches PLAN.md, uses ordinary signed native preparation code, provides cancellation and memory control, and makes supported operations reviewable. Differential parity is the main cost. |
| Precompute shareable profile metadata | Useful complement. Ship build IDs, offsets, fingerprints, facts and reviewed recipes with the app. It cannot replace game-specific disassembly or generate patched images without the user's content. Do not distribute patched games, extracted shaders or copied game instruction payloads as profile updates. |
| Embed Python | Technically possible with a non-JIT Python runtime and compiled extensions, but not a “small interpreter” drop-in. Full CPython semantics, Capstone/LZ4 bindings, filesystem assumptions, dynamic imports and re-exec need integration. It preserves arbitrary executable profile behavior and duplicates runtime/memory cost. Do not choose it for the main route. |

Use offline Python as the development oracle, not as a per-game user requirement. Differential tests should compare native and pinned Python outputs, gate/layout records and accepted/rejected cases. Synthetic fixtures cover malformed inputs, branch reach, x18 jumps, exclusive windows and frame-state relocations; private user dumps can establish exact TOTK parity without copying artifacts into the repo. Preserve the strict checks described in the rewriter reference. [N30: docs/reference/rewriter.md:330-340][N30]

## 3. Complete shader chain and its migration

`dmsh_xlate_bin/dmsh_xlate` is the published .NET apphost for `dm/gfx/tools/dmsh_xlate`, a C# front end using the vendored **Ryujinx.Graphics.Shader** translator and `Ryujinx.Common`. It is not a Nintendo compiler, not SPIRV-Cross, and not NativeAOT at this pin. The project targets .NET 10; the packaged apphost needs that runtime. Its `TargetApi.Vulkan` setting describes SPIR-V translation conventions, not a requirement to render through Vulkan. [N31: dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj:1-18][N31] [N32: docs/reference/packing.md:12-17][N32] [N33: dm/gfx/tools/dmsh_xlate/Program.cs:162-208][N33]

The actual transformation chain has the following stages. Python orchestration is portable logic; the Mac-specific pieces are executable subprocesses, Apple offline compiler tools, and capture by running a Mac host.

| Stage | Pinned implementation and artifacts | Phone replacement |
|---|---|---|
| Discover/decompress | `shader_split.py` traverses merged RomFS and main, nested archives and compressed formats. TOTK needs dictionary zstd, SARC and BNSH/BFSHA paths. Other engines need their own readers, sometimes the game's decoder invoked through a host tool mode. | Native readers over the authenticated content view. Port TOTK's formats first, not every engine. Reuse the guest's signed native decoder only where its host dependencies are supported. |
| Normalize shader records | Control section plus code area become `BTCDBTCD` records in stage/group packages. Key is SHA-256 truncated to 16 hex digits over the bounded control section plus code, not control alone. Generate map, facts and program pairs. | Same canonical hashing and metadata, with full content hashes retained internally to check collisions. Records can stay in a private cache instead of recreating package files. |
| Form translation job | `dmsh_build.py` extracts c1, merges VertexA/VertexB where present, strips the graphics container header while retaining SPH, and locates compute instructions at the validated program start (including 0x100 for the header-bearing form). It answers texture/image types, attribute/output types, compute local size/shared memory and profile-specific facts. Preserve early-Z, transform-feedback, reduced-precision options and geometry metadata. | A typed request and immutable state snapshot to NativeAOT. Static profile facts alone are insufficient for arbitrary runtime-generated programs. |
| Maxwell to SPIR-V | Ryujinx accessor builds resources and translates; retry after inferred image types; FP64 is lowered to FP32 when needed, explicitly reported. Geometry translation can emit a compute stage and vertex reader. A code-bound vertex-counter lowering is also applied. Output `.spv` plus `.res.json` bindings/notes. | Extend the existing NativeAOT facade to the upstream accessor and resource/result contract, including these passes. Keep approximation notes and compatibility scope visible. |
| SPIR-V fixes and MSL conversion | Vertex subgroup operations can be lowered to one-lane behavior; geometry sets are flattened. Converter uses MSL 2.3 normally, MSL 3.1 for native image atomics, argument buffers, native texture buffers and padded fragment outputs. Raw-pointer image-atomic emulation is rejected. Integer-only vertex inputs can trigger retranslation. | Link pinned SPIRV-Cross C++ in process. Port the SPIR-V passes and exact option/ABI contract. |
| MSL semantics | Atomic checks; precise min/max/clamp; descriptor-pool arrays; sampler border-color emulation; vertex output initialization, layer handling, base-instance behavior and partner-dependent output padding. | Port these transformations as a shared native preparation library. Bare SPIRV-Cross output is not equivalent to this host ABI. |
| Entry points and variants | Vertex depth conventions, geometry capture, discard/storage-write path, transform feedback and layer entry points; integer and dual-source fragment outputs. Side libraries: shadow vertex inputs (`_bv`), vertex pulling (`_bp`), alpha test (`_ba`), logic ops (`_bl`), readable tile fetch (`_bf`). General geometry uses topology-specific compute/readers, optional lean and mesh variants. | Produce the variants required by the selected game's renderer paths. Optional optimizations can wait only when the host has a semantically correct fallback. Do not silently omit a required entry point. |
| Reflection and analysis | Custom JSON carries bindings, c1 bytes, driver reads, entry names, raster facts and chain stamp. c1/constant-buffer extent analysis and pair metadata drive safe binding and pass behavior. | Emit equivalent metadata; validate against `dmsh_abi.h`. Keep conservative allocation fallbacks until analysis is ported. Metal's own reflection cannot replace the NVN semantic metadata. |
| Offline Apple compile | `xcrun -sdk iphoneos metal` compiles MSL to AIR, then `metallib` links it. iOS flags include `-O1`, relaxed math and preserve-invariance. Python also hashes real tools/SDK and caches outputs. | `newLibraryWithSource:options:error:` or its asynchronous form using system Metal. This compiles GPU code and does not require guest CPU JIT. Retain MSL/reflection for rebuilding. |
| Pipelines and persistence | Host loads a library, creates functions with constants, constructs render/compute PSOs and warms from manifests. Optional offline `metal-tt` archives are GPU/OS targeted; Metal 4 compiler/archive support is separate from ordinary PSO creation. | Create PSOs on the phone. Use a versioned, disposable `MTLBinaryArchive` for the classic path, or preserve the host's Metal 4 archive path when enabled. Retain application-level manifests and permit misses/rebuilds. |

Extraction/record evidence: [N34: dm/gfx/tools/shader_split.py:1-63][N34], [N20: TOTK game.tsv:1-5,52-54][N20]. Job normalization: [N76: dmsh_build.py:408-462][N76]. Translation and transforms: [N33: Program.cs:162-208][N33], [N35: dm/gfx/tools/dmsh_build.py:1-50][N35], [N36: dmsh_build.py:960-1011][N36], [N37: dmsh_build.py:1020-1161][N37], [N38: dmsh_build.py:1225-1237][N38], [N77: dmsh_atomic.py:1-58][N77]. Compiler and ABI: [N39: dm/ios/tools/ios_metal.py:1-14][N39], [N10: dm/gfx/README.md:42-59][N10]. Archive packaging: [N40: dm/ios/tools/build_ipa.sh:89-99][N40].

Satchel already has a NativeAOT build of the same pinned Ryujinx sources, but its public facade accepts resource-free compute with a 1x1x1 workgroup and throws for real resource bindings. The separately staged fixture is also tightly limited. Reuse the build/runtime integration; extend the facade rather than calling it a finished game shader compiler. The existing SPIRV-Cross bridge uses the correct source revision, but its current contract rejects arrays, combined samplers, specialization constants and auxiliary buffers, and does not provide the argument-buffer ABI above. It too needs extension. [P08: Tools/MaxwellShaderCompiler/README.md:3-20,89-112][P08] [P09: Tools/MaxwellShaderCompiler/Compiler.cs:27-51][P09] [P10: Tools/ShaderBridge/README.md:3-24,56-77][P10]

### Runtime compilation is available, but compiler equivalence is unproven

The installed iPhoneOS Metal headers expose source-library creation, `mathMode`, `preserveInvariance` and `optimizationLevel`. Use relaxed math and preserve-invariance explicitly; `fastMathEnabled` is deprecated in favor of `mathMode`. The public optimization enum has `Default` and `Size`, not an exact command-line `-O1` selector. Do not claim those options recreate upstream's offline compiler. The system compiler follows the phone OS and cannot simply be pinned to the Mac toolchain. [A01: installed MTLLibrary.h:234-246,318-392][A01] [A02: installed MTLDevice.h:708-717][A02]

`MTLBinaryArchive` persists compiled pipeline functions for later PSO creation. It is not a public serializer for an arbitrary source-created `MTLLibrary`, and it does not turn Maxwell into MSL or remove the need to recover library/function descriptions. Store MSL and semantic reflection, retain live library objects during a session, then use archives/system caches to reduce later pipeline work. Apple's header recommends its automatic cache for ordinary runtime use and warns that updating archives requires corruption resilience and storage management. Treat explicit on-phone archives as expendable caches with atomic writes and a rebuild path. [A03: installed MTLBinaryArchive.h:43-58,121-128][A03] [Apple MTLBinaryArchive documentation](https://developer.apple.com/documentation/metal/mtlbinaryarchive)

Issue [#242](https://github.com/seedds/nxnative/issues/242) reports severe BOTW corruption despite shaders being present. The discussion identified a different compiler and requested a rebuild, but the pinned reference explicitly leaves compiler causation unresolved. The confirmed issue was cache provenance: override libraries could be restored under the tested compiler identity. The lock names Metal `32023.921 (metalfe-32023.921.6)`; pinned SPIRV-Cross is commit `f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7`. Preserve compiler/SDK, chain, profile inputs, MSL, device/OS and PSO state in cache identities. Give the runtime-compiled path a distinct identity; never label its output as Mac-pinned output. Shader compilation success is not visual correctness. [N41: docs/reference/shaders.md:444-450][N41] [N42: dm/gfx/tools/dmsh_chain.lock:1-8][N42] [N43: dm/gfx/tools/spirv_cross_bin/build.sh:1-13][N43]

### Runtime shader capture

Fire Emblem: Three Houses and Catherine: Full Body compile built-in GLSL through their own `glslcCompile`; upstream runs the native game on a Mac and captures the generated GLSLC objects before splitting/translating them. Link's Awakening carries GLSLC but documented early scenes used offline shaders. Captain Toad demonstrates why an import alone is insufficient evidence of runtime generation. Other GL/Vulkan titles have their own capture paths in the packaging script. [N44: docs/reference/shaders.md:938-956][N44] [N45: dm/host/hle_glslc.c:1-45][N45] [N46: scripts/build_ipa_from_nsp.py:2437-2475][N46]

On the phone, let the already rewritten and signed guest GLSLC execute, snapshot the resulting control/code/facts at the host boundary, then run the native shader pipeline before first use. Its GPU-code output is data, not newly executable ARM64. A missing program must trigger bounded translation and a draw dependency, not become permanently cached as missing. Capture program pairs and resource facts at NVN program binding/draw time when absent offline. A preparation boot can warm known paths, but cannot guarantee all gameplay paths were visited. This removes the Mac capture dependency without promising universal game compatibility. Actual self-modifying guest CPU code or generated CPU JIT code would require a different route.

## 4. Signed dylib integration with the loader

The current iOS loader uses `IMAGE_MH = &_mh_execute_header`. It finds signed `__TEXT` sections and writable `__DM<module>W` sections, reserves a contiguous module range, then calls `vm_remap` with fixed overwrite: executable pages shared (`copy=FALSE`), writable pages copy-on-write (`copy=TRUE`). This reconstructs original guest-relative addresses. Front patch areas may precede the base; optional back areas follow it. Alignment is 16 KiB. [N47: dm/host/loader.c:507-519][N47] [N19: dm/host/loader.c:520-565][N19] [N06: dm/ios/tools/gen_embed.py:1-29][N06]

The recommended adaptation is:

1. Write a minimal ARM64 Mach-O dylib container on the phone, with correctly aligned code/data ranges, load commands, identity/version and code-signature storage. Reuse the split rules from `gen_embed.py`; replace its `.incbin`, assembler and linker subprocesses with a bounded writer. No per-game Xcode installation or executable compiler process is required in the design.
2. Sign the final file through an on-device signing component using a user-controlled signing identity. A certificate without access to its matching private-key operation is insufficient. Password/passphrase entry belongs to user-controlled setup, never the agent. Renewal and sideloader re-signing must account for generated dylibs in the data container, not just bundled frameworks.
3. `dlopen` the completed file by an explicit private path. Resolve a validated image descriptor/header for that handle, account for ASLR, and keep the handle alive while any remapped page or guest thread can refer to it. Validate bounds and package identity before remapping. Use a constructor-free container to avoid executing guest initialization during `dlopen`.
4. Change code-section lookup to use that game's Mach-O header instead of the main executable header. Keep host `swhle` and `swforce` registration sections in Satchel's executable; their lookups must still refer to the host image. Static-library linking must retain these registration entries and assembly stubs.
5. Preserve the existing guest loader: install runtime TSD indices in a data key page, register gates, parse MOD0/ELF dynamic tables, apply RELA/RELR and import takeover to writable data, then initialize guest modules and enter `nnMain`. Apple's dyld loads the container, not Horizon modules or guest symbols.

Host import lookup and relocation evidence: [N48: dm/host/hle.c:23-55][N48], [N49: dm/host/loader.c:150-163][N49], [N50: dm/host/loader.c:270-338][N50], [N08: dm/host/loader.c:247-267][N08], [N51: dm/host/main.c:1-40][N51]. Guest gates and static stub tables must stay precompiled into the host. [N52: dm/host/entry.S:1-58][N52]

No executable page may be changed after signing. Do not patch absolute host addresses into text after `dlopen`; the TSD/key-page/gate mechanism exists to avoid that. Retain permission checks after remapping and reject any unexpected relocation into executable text. The general Mach-O writer and signer are new components, not supplied by `gen_embed.py`.

If S1 permits ordinary dylib execution but rejects executable remapping, an alternative would be a dylib whose loaded virtual layout directly matches every guest module. That changes packing, large gaps, BSS and branch-range constraints and needs a separate spike. It is not an established fallback. If neither executable remapping nor a direct-layout dylib works, the proposed native single-app route is blocked under that signing setup.

### Making the host a library, not a second app entry point

Expose a native session boundary, conceptually `prepare(config)`, `start(surface, input)`, `pause`, `resume` and eventually `stop`. Satchel owns UIKit/SwiftUI. Adapt `front_ios.m`'s Metal surface and input bridge instead of importing a second `UIApplicationMain`/launcher. The existing iOS entry sets paths and runs `dm_main` on a 64 MiB-stack worker; keep guest and compiler work off the UI thread. [N53: dm/ios/Sources/main.m:233-282][N53] [N54: dm/ios/project.yml:14-24,38-40][N54]

The runtime profile implementation is helpful but incomplete. `game.py profile` does not export all compile-time settings: examples include `tex_bc_cpu`, `stdalloc_cap`, `enc_thread` and `ue_texture_pool`. `main.c` still uses compiled module defaults, arguments and ID; explicitly pass the selected module list and convert remaining per-game consumers to session data. Also preserve resolved defaults: TOTK's profile comment says NPDM system resource is 24 MiB but omits the row, while the header generator defaults it to 32 MiB. Export the actual resolved behavior for parity before intentionally changing it. [N12: game.py:202-229][N12] [N55: game.py:150-181][N55] [N56: dm/host/main.c:65-104][N56] [N57: dm/gfx/nvn/nvn_mtl.m:388-400][N57] [N20: TOTK game.tsv:28-37][N20]

A second launch in the same process is not currently safe by construction. Profile state is loaded once, memory uses `pthread_once`, modules/gates/renderer caches are global, and `dm_host_shutdown` ultimately calls `_exit(0)`. For the first M4 demonstration, document a single guest session per app process, requiring a user relaunch to switch; that still proves launch from Satchel. Before calling the library experience complete, implement quiescence, GPU drain, guest thread joins, callback cancellation, file/save closure, table/cache reset and module unmapping. Do not use `dlclose` as a substitute for stopping threads. Removing `_exit` alone is insufficient. [N13: hle_profile.c:250-265][N13] [N58: dm/host/hle_svc.c:42-74][N58] [N59: dm/host/hle_lifecycle.c:1-28][N59]

## 5. Address space, physical memory and entitlements

The host's guest block is an anonymous `PROT_NONE` reservation, partitioned into a 256 MiB front region, alias region, stack region and remaining heap. Pages are exposed as the guest maps/allocates memory. Module images are separately reserved in the iOS loader, so the profile reserve is not the entire process address-space cost. Dylib source mappings, remapped module views, host stacks, NativeAOT, UI, Metal allocations and shader compiler intermediates are additional costs. Shared executable mappings avoid copying code bytes but still consume virtual ranges. [N58: dm/host/hle_svc.c:42-74][N58] [N19: loader.c:549-565][N19]

| Profile | Reservation and partition | Consequence |
|---|---|---|
| TOTK 1.4.3 | `0x140000000` = 5 GiB: 256 MiB front + `0x98000000` = 2.375 GiB alias + 256 MiB stacks + 2.125 GiB heap capacity. Application pool `0xCD500000` = 3285 MiB. | `nninitStartup` asks for `0x86000000` = 2.09375 GiB heap. SDK node-area reduction from roughly 1 GiB to 62 MiB is part of the compatibility path; removing it risks resource-region exhaustion. |
| Celeste 1.4.1.1 | `0x100000000` = 4 GiB: 256 MiB front + 256 MiB alias + 256 MiB stacks + 3.25 GiB heap capacity. `sys_resource=0`, `mem_used=100 MiB`. | Uses the heap path rather than virtual-address memory. A simpler graphics workload still asks for a large virtual heap. |

Source values and upstream observations: [N20: TOTK game.tsv:28-37][N20], [N29: Celeste game.tsv:27-37][N29]. These are GiB/MiB conversions, not measurements from this task. Upstream's reference reports a single 6 GiB reservation succeeding and 7 GiB failing without extended addressing, with 5 GiB used to leave room for Metal. Those observations are not universal iOS limits. [N60: docs/reference/memory.md:7-14][N60]

The standard native build requests `com.apple.developer.kernel.increased-memory-limit`. The script embeds it in the ad-hoc placeholder signature or generated Xcode entitlement file. It does **not** request extended virtual addressing or a CPU JIT entitlement by default. Entitlement requests must survive re-signing and be authorized by the App ID/provisioning profile. [N61: dm/ios/tools/build_ipa.sh:124-130,166-178][N61] [N62: dm/ios/ENTITLEMENTS.md:3-32][N62]

Increased memory limit concerns resident/footprint budget. Extended virtual addressing concerns addressable space; neither substitutes for the other. Apple documents extended addressing as a capability for apps needing larger address spaces, but documentation is not proof that this team's final signature obtains the desired behavior. [Apple extended virtual addressing documentation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing)

Upstream reports a 12 GiB phone killing TOTK near 3342 MB resident with 33 MB remaining without the increased budget; render-scale reduction did not solve it. Its 6 to 8 GB entitled budget is device-dependent reported evidence. S2 must measure the actual Satchel host under the maintainer's team, both reservations and committed-memory headroom, including UI/translator/Metal overhead. Free-personal-team behavior remains S3. [N62: dm/ios/ENTITLEMENTS.md:8-32][N62]

Do preparation before the guest reserves its heap; release transient NSO/decompression/compiler buffers before starting the game. Bound runtime compilation concurrency and keep hot/cold shader caches separate. Current TOTK notes also report large transient upload peaks and explicitly leave some iOS validation pending. Budget must cover streaming and transitions, not just title-screen residency. M2 owns that performance evidence. [N63: dm/games/0100F2C0115B6000/README.md:27-36][N63]

## 6. Components linked once and license inventory

The host build list is the starting inventory, not a command to execute per game. `gen_build.py` currently derives iOS sources from the host Makefile and substitutes frontend files. Freeze an explicit Satchel target at the pin, preserving assembly, registration sections and required media dependencies. [N64: dm/host/Makefile:19-55][N64] [N65: dm/ios/tools/gen_build.py:1-39][N65]

Preserve the scheduler and renderer as coherent units. Guest threads map to pthreads but rely on emulated core/priority handoffs, address arbitration and host-work accounting, not ordinary pthread behavior alone. The host also does not implement an entire Horizon firmware environment: it takes over SDK symbols and implements selected service paths; unknown paths can still abort a title. NVN recording, dirty-page tracking, texture initialization, resource synchronization and actual GPU-completion signaling must stay paired with the renderer. The integration's first objective is preserving the pinned behavior, not replacing these subsystems. [N78: dm/host/hle_thread.c:1-11][N78] [N79: docs/reference/threads.md:7-20][N79] [N80: docs/reference/services.md:7-13][N80] [N81: docs/reference/rendering.md:22-28][N81] [N82: dm/gfx/README.md:16-25][N82]

| Component | Integration and license evidence |
|---|---|
| Horizon host | `loader.c`, `entry.S`, `thread.c`, import/gate dispatch, memory/SVC, thread synchronization and `hle_*.c` services. Reuse filesystem, audio/audren, movie/codec, input, account/time/OS/offline service behavior and NRO support. NX2Apple/nxnative MIT, retain copyright/license. [N66: LICENSE:1-23][N66] |
| NVN renderer | `dm/gfx/nvn` interface, command recording/replay, Metal backend, reflection, dirty tracking and `dm/gfx/tex`; display/driver glue in `dm/gfx/*.c`. MIT except attributed third-party portions below. NVN ABI sizes and guest-inline writes are correctness contracts, not refactoring opportunities. [N67: docs/reference/nvn-objects.md:7-28][N67] |
| Apple frontend adaptation | Reuse relevant `front_ios.m`, gamepad, motion/haptics and lifecycle bridges under NX2Apple MIT. Satchel retains its existing app entry/library UI. Apple system frameworks remain SDK/platform dependencies. |
| Rewriter/profile engine | New native port of MIT Python implementation, Capstone 5 as a dependency. Capstone is BSD 3-clause licensed; preserve the exact vendored license/notices when selecting its source. No Capstone dependency was installed by this task. [Capstone 5.0.6 license, lines 9-18](https://github.com/capstone-engine/capstone/blob/5.0.6/LICENSE.TXT#L9). |
| Shader compiler | Existing NativeAOT Ryujinx shader sources, Spv.Generator and .NET runtime, MIT notices already present in Satchel. Include .NET third-party notices. No need to embed the desktop apphost's unrelated package graph. [P08: Maxwell README:44-68][P08] [P11: licenses/Ryujinx.MIT.txt:1-9][P11] [P12: licenses/Spv.Generator.MIT.txt:1-20][P12] [P13: licenses/DotNET.LICENSE.txt:1-21][P13] |
| SPIRV-Cross | Existing bridge can reuse pinned revision. Apache-2.0 with additional MIT/Khronos notices in the upstream distribution; retain license/notice set. [N43: spirv_cross_bin/build.sh:1-13][N43] [P10: ShaderBridge README:18-24][P10] |
| Texture layout / decoder | `nvn_tex.c` attributes deko3d layout algorithm to fincs under zlib. `bcdec` offers MIT or Unlicense; select MIT and retain attribution. [N68: dm/gfx/nvn/nvn_tex.c:1-7][N68] [N69: dm/third_party/bcdec/LICENSE:1-24][N69] |
| NVN declarations | `dm/gfx/nvn/include/nvn.h`, reconstructed declarations, MIT notice by h1k421. [N70: nvn.h:1-24][N70] |
| Media dependencies | libvpx VP8/VP9 decoder is BSD 3-clause, preserve its LICENSE and applicable patent notice. stb_vorbis offers MIT or public-domain alternative; select MIT. [N71: libvpx/LICENSE:1-30][N71] [N72: stb_vorbis/LICENSE:1-12][N72] |
| Preparation decompression | Existing independent LZ4 decoder can remain. TOTK dictionary-zstd support needs a native zstd dependency if none is already linked; pin and use its BSD license. [Zstandard v1.5.7 license, lines 1-18](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE#L1). Optional LibHac reuse for update composition is MIT, as identified in [P04: SwitchExecutable.swift:39-44][P04]; assess a NativeAOT/library boundary separately rather than porting the desktop extraction executable wholesale. |

`dm/gfx/metal` is a standalone offscreen example, explicitly excluded from the host. `dm/gfx/vk` and `gl` are separate compatibility layers for games using those APIs; they are not needed to turn TOTK's NVN path into Metal. Packaging scripts, XcodeGen, offline Apple compilers, fonts tooling and profile-survey Python run during development only after their per-game logic has been replaced. [N73: dm/gfx/metal/README.md:1-3][N73] [N74: dm/gfx/README.md:69-110][N74]

Font packaging needs separate attention: upstream copies macOS Arial Unicode by default and generates a Nintendo-extension stand-in. A copied operating-system font is not covered by NX2Apple's MIT license. Choose a redistributable font with its own notice, or an appropriate platform font strategy, before shipping. [N75: dm/ios/tools/build_ipa.sh:109-111][N75]

This inventory describes source licenses, not a legal determination about user game content. No GPL emulator code is required for this design. New dependencies above are proposals, not additions made by this task.

## 7. Sequence to TOTK inside Satchel

Effort is engineering person-time for someone comfortable with ARM64, Mach-O and Metal. Ranges include focused validation and debugging, exclude waiting for S1/S2, and are not commitments. Several tasks share prerequisites; do not add all rows and call the result a calendar deadline.

| Step and state | Deliverable / acceptance evidence | Rough effort and main risk |
|---|---|---|
| 0. S1/S2, externally pending with `spike` | Device execution from a Mac-signed container dylib, then phone-signed dylib, then the real RX/RW remap layout. Test after cold launch without debugger. Measure guest-sized reservations and committed memory under actual entitlements. | 2 to 5 days for probes, potentially route-blocking. Signing/provisioning or executable mapping policy may reject the route. This worker does not own the phone. |
| 1. Package contract and exact content selection, proposed | Identify the supported TOTK 1.4.3 main/subsdk/sdk build set. Manifest binds update, modules, profile, RomFS and shader chain. Transactional prepared-output publication leaves imported originals untouched. | 3 to 5 days. Wrong update/profile pairing can appear to load while silently omitting required patches. |
| 2. Extraction/update/RomFS completion, proposed | On-phone base/update selection, rights/key handling where required, authenticated patch-storage composition, module decoding and streamed `DMROMFS1`. Compare selected file bytes and module hashes with the pinned private reference extraction. | 2 to 4 weeks. BKTR, content metadata and large storage requirements are a real gap; importing a package is not proof of merged content. |
| 3. Native rewriter for the supported build, proposed | Port generic analysis/emitters and SDK cuts plus TOTK frame-step recipe. Differential layout/code/table results against pinned Python; original-byte and branch-range failures remain hard failures. | 3 to 6 weeks. x18 control flow, exclusive windows, spin gates and front/back layout parity dominate. |
| 4. Mach-O writer and signer, proposed after S1 | Produce the same signed pieces on the phone, load with `dlopen`, remap, pass all permission/layout checks. Re-sign/renew without losing saves. | 1 to 3 weeks after probe. Minimal dyld format, signature generation, key availability and renewal behavior may add time. |
| 5. Shared host integration, proposed | Link generic host/NVN/media once. Supply runtime profile and explicit modules/paths, adapt surface/input/audio and guest startup. Run synthetic guest integration before privately testing TOTK. | 2 to 4 weeks. Partial runtime-profile conversion, Apple/guest ABI differences, global signal handlers and memory overhead. |
| 6. M4 launch with Mac-prepared shaders, proposed | Import matched iPhoneOS libraries/reflection/tables as the only per-game Mac crutch. From a fresh Satchel process: select imported TOTK, prepare code/content on phone, launch to title, enter playable scene, exercise input/audio/save and cold relaunch. Record those as separate achievements. | 1 to 2 weeks after steps 2 to 5. Visual correctness, service gaps and resident-memory spikes. A title screen meets “launched”; it does not prove stable play. |
| 7. Runtime MSL path first, proposed | Feed identical privately prepared MSL/reflection to a source-library provider on phone. Compare with M4 metallibs on the same scenes and compile options; measure cold/warm compile and footprint. | 1 to 2 weeks. Establishes whether the system compiler can replace pinned offline Metal before porting every upstream Python transformation. |
| 8. Full phone shader preparation, proposed | Extend NativeAOT accessor/results; port TOTK shader discovery/facts, SPIR-V/MSL transformations, variants, reflection and analysis. With no imported shader outputs, prepare shaders entirely from imported content and reproduce M4 scenes. | 4 to 8 weeks. ABI parity, manual facts, compiler bugs, long compilation and heat/storage pressure. |
| 9. Runtime misses and cache lifecycle, proposed | Add synchronous first-use dependencies, bounded background preparation, captured GLSLC/NVN state, cache invalidation and recovery. Validate representative runtime-generating games separately. | 2 to 4 weeks, with additional per-engine work beyond TOTK. Static extraction and seed manifests do not cover unknown runtime variants. |
| 10. Session teardown and distribution, proposed | Return to library, launch another title, then TOTK again without stale resources. Verify clean/cancelled preparation, saves, signature renewal, entitlement preservation and free-team behavior before making those promises. | 2 to 4 weeks. Global state and `_exit` must become a real lifecycle. Free-team route remains independently gated by S3. |

Allow roughly 10 to 20 person-weeks for the narrow M4 demonstration after successful feasibility probes, another 7 to 14 for the phone shader route, and 2 to 4 for the wider lifecycle/distribution work described here. Independent implementation can overlap in calendar time; this does not reduce person-time. Broader game compatibility and sustained performance remain separate work.

The Mac-prepared crutch must be narrowly defined: shaders and their reflection/analysis tables only. During development, private reference artifacts are useful for parity, but a launch that still uses Mac-rewritten CPU code or Mac-composed game content has not met the proposed M4 acceptance condition. M5 acceptance starts from original imports, reviewed app-shipped metadata and an empty shader cache, with network/Mac assistance absent during per-game preparation.

### Conditions that could block the phone-only route

1. **Executable acceptance:** the final signing/provisioning setup cannot execute a generated container dylib, or cannot remap its signed code and no direct-layout design passes. This blocks the chosen native route. PLAN.md's JIT-enabler fallback requires an explicit route reassessment; it is not silently part of this design.
2. **Signing capability:** the on-phone component cannot perform the user's authorized private-key signing operation or cannot maintain usable signatures across expiry/refresh. A server/Mac signer would violate “all per-game work on the phone.”
3. **Memory:** allowed VA or physical budget is below the necessary guest-plus-host working set even after measured corrections. Increased VA cannot repair jetsam limits, and render scaling cannot fix every heap shortage.
4. **Unreplaceable compiler behavior:** the phone's system Metal compiler cannot correctly or practically compile required generated MSL, with no equivalent transform/workaround. That would block fully phone-only shaders for the affected OS/device/title even if Mac-prepared shaders work. Issue #242 is a risk signal, not proof of this outcome.
5. **Guest CPU generation or unsupported architecture:** a title requires CPU JIT/self-modification or non-ARM64 execution that cannot be handled by pre-rewriting signed modules. This is a per-title limitation; it does not negate TOTK feasibility already reported by the project.

Nothing in the reviewed source proves that all five are resolved. Nothing in the shader chain requires a Mac in principle once translator, transformation and runtime library paths are native. The strongest unresolved system boundary is still S1.

## Verification and handoff

Done: documented the seven requested areas, inspected the pinned source and Satchel loader/compiler boundaries, and produced this file only. Source evidence is validated by inspection; proposed implementation remains awaiting validation.

Read-only commands used included `uname -s` (Darwin), `print -r -- "$SHELL"` (`/bin/zsh`), `cat AGENTS.md`, `cat PLAN.md`, `git -C /tmp/satchel-switch-integration/NX2Apple rev-parse HEAD` (full pin above), `git -C /tmp/satchel-switch-integration/NX2Apple status --short` (empty), `rg --files`, targeted `rg -n` searches and `nl -ba <file> | sed -n '<range>p'` source reads. A `python3` AST-only scan of `dm/games/*/patches.py` reported 29 files, 6 with function definitions and 10 definitions; no profile was imported or executed. Installed SDK headers were read directly; no Xcode build was invoked.

Documentation checks: `git diff --check` and `git diff --no-index --check /dev/null docs/integration-design.md` passed with no output. The second explicitly includes the new, untracked document. The source-reference/punctuation check below passed for 98 references. It checks each cited source path and numeric starting line, plus the absence of em dash; double hyphens occur only in Markdown table delimiters and literal command options. These are document checks, not runtime tests.

```sh
python3 - <<'PY'
from pathlib import Path
import re
p = Path('docs/integration-design.md')
s = p.read_text()
root = Path('/tmp/satchel-switch-integration/NX2Apple')
refs = dict(re.findall(r'^\[([NPA]\d+)\]: (.+)$', s, re.M))
for key, link in refs.items():
    if key.startswith('N'):
        name, line = link.split('/8227f80f9b381c680611303319656c4e0c806a50/')[1].split('#L')
        target = root / name
    elif key.startswith('P'):
        name, _, line = link.partition('#L')
        target = p.parent / name
    else:
        name, line = link.rsplit(':', 1)
        target = Path(name)
    assert target.exists(), (key, target)
    if target.is_file():
        assert 1 <= int(line or 1) <= len(target.read_text().splitlines()), key
assert set(re.findall(r'\]\[([NPA]\d+)\]', s)) <= refs.keys()
assert chr(0x2014) not in s
print('PASS:', len(refs), 'source references; em-dash check')
PY
```

Skipped by scope: code changes, dependency installation, builds, tests, signing, credential access, game-file inspection and all device actions. Apple's Markdown links initially failed in the web reader with `Unsupported content-type: text/markdown`; the archive and entitlement pages were then read through ordinary HTTPS and API declarations were verified in the installed SDK. Initial exploratory missing-path/glob errors were resolved by file discovery and did not block the analysis.

Single next blocker: obtain the `spike` worker's S1/S2 result, especially **phone-signed dylib execution after executable `vm_remap` under the actual team profile**. If it passes, start the package/content contract and extraction-gap work; if it fails, record the exact policy/error boundary before choosing another execution route.

## Source links

[N01]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/README.md#L3
[N02]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/nvn_mtl.m#L3871
[N03]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/nvn_mtl.m#L4043
[N04]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L1
[N05]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L1495
[N06]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/gen_embed.py#L1
[N07]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L185
[N08]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L247
[N09]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/build_ipa.sh#L65
[N10]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/README.md#L42
[N11]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/game.py#L15
[N12]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/game.py#L202
[N13]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_profile.c#L208
[N14]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/romfs_image.py#L1
[N15]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/romfs_image.h#L28
[N16]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_fs.c#L433
[N17]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/scripts/build_ipa_from_nsp.py#L979
[N18]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L345
[N19]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L520
[N20]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/0100F2C0115B6000/game.tsv#L1
[N21]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L25
[N22]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L62
[N23]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/rewriter.md#L16
[N24]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/nso.py#L1
[N25]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/nso_asm.py#L1
[N26]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L1475
[N27]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/0100F2C0115B6000/patches.py#L1
[N28]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/tools/patch_nso.py#L1156
[N29]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/01002B30028F6000/game.tsv#L1
[N30]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/rewriter.md#L330
[N31]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj#L1
[N32]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/packing.md#L12
[N33]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_xlate/Program.cs#L162
[N34]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/shader_split.py#L1
[N35]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_build.py#L1
[N36]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_build.py#L960
[N37]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_build.py#L1020
[N38]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_build.py#L1225
[N39]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/ios_metal.py#L1
[N40]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/build_ipa.sh#L89
[N41]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/shaders.md#L444
[N42]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_chain.lock#L1
[N43]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/spirv_cross_bin/build.sh#L1
[N44]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/shaders.md#L938
[N45]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_glslc.c#L1
[N46]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/scripts/build_ipa_from_nsp.py#L2437
[N47]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L507
[N48]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle.c#L23
[N49]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L150
[N50]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/loader.c#L270
[N51]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/main.c#L1
[N52]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/entry.S#L1
[N53]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/Sources/main.m#L233
[N54]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/project.yml#L14
[N55]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/game.py#L150
[N56]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/main.c#L65
[N57]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/nvn_mtl.m#L388
[N58]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_svc.c#L42
[N59]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_lifecycle.c#L1
[N60]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/memory.md#L7
[N61]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/build_ipa.sh#L124
[N62]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/ENTITLEMENTS.md#L3
[N63]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/0100F2C0115B6000/README.md#L27
[N64]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/Makefile#L19
[N65]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/gen_build.py#L1
[N66]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/LICENSE#L1
[N67]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/nvn-objects.md#L7
[N68]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/nvn_tex.c#L1
[N69]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/third_party/bcdec/LICENSE#L1
[N70]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/nvn/include/nvn.h#L1
[N71]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/third_party/libvpx/LICENSE#L1
[N72]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/third_party/stb_vorbis/LICENSE#L1
[N73]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/metal/README.md#L1
[N74]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/README.md#L69
[N75]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/build_ipa.sh#L109
[N76]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_build.py#L408
[N77]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/tools/dmsh_atomic.py#L1
[N78]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/host/hle_thread.c#L1
[N79]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/threads.md#L7
[N80]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/services.md#L7
[N81]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/docs/reference/rendering.md#L22
[N82]: https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/gfx/README.md#L16
[P01]: ../App/SwitchPackage.swift#L36
[P02]: ../App/SwitchPackageImport.swift#L29
[P03]: ../Core/SwitchCrypto.c#L1
[P04]: ../App/SwitchExecutable.swift#L39
[P05]: ../Core/NSOLoader.c#L18
[P06]: ../App/SwitchRomFS.swift#L93
[P07]: ../Core/SwitchStorage.h#L5
[P08]: ../Tools/MaxwellShaderCompiler/README.md#L3
[P09]: ../Tools/MaxwellShaderCompiler/Compiler.cs#L27
[P10]: ../Tools/ShaderBridge/README.md#L3
[P11]: ../Tools/MaxwellShaderCompiler/licenses/Ryujinx.MIT.txt#L1
[P12]: ../Tools/MaxwellShaderCompiler/licenses/Spv.Generator.MIT.txt#L1
[P13]: ../Tools/MaxwellShaderCompiler/licenses/DotNET.LICENSE.txt#L1
[A01]: /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLLibrary.h:234
[A02]: /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLDevice.h:708
[A03]: /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Metal.framework/Headers/MTLBinaryArchive.h:43
