# Phone-only Switch runtime

The acceptance target is one installed iPhone app: choose an unmodified base
XCI/NSP from iCloud, choose an optional matching update, play locally, save, and
relaunch offline. Adding another supported game must not require a Mac, a
remote preparation service, a game-specific signed app, or JIT activation.
Importing a file alone does not meet this target.

## Implemented foundation

The Files picker retains security-scoped, coordinated reads. XCI/NSP packages
are inspected using bounded, 64-bit file offsets and copied through a 1 MiB
buffer into an app-owned temporary file. SHA-256 determines local identity;
same-volume atomic rename publishes only a complete, rechecked copy. Cancellation
removes that import's partial file and preserves source and previous local copy.
Fresh-start recovery reclaims only importer-owned regular `.import-<UUID>` files
left behind by a killed process.
Free-space preflight requires a complete copy plus 256 MiB reserve. The importer
has a 128 GiB policy limit; the NRO execution limit stays 64 MiB.

The container reader uses Foundation and CryptoKit already available in iOS.
It checks PFS0 or XCI/HFS0 directories, names, bounds,
overlaps and declared XCI hash regions, including a 4096-byte XCI prefix. It
accepts trimmed final secure-partition padding only when the complete secure
directory, every content extent, and every declared hash region still exist.
The container inspection itself does not decrypt content or authenticate the
whole game. The executable reader described below is a separate, deeper check.

Package entries persist beside the existing NRO library. Their detail view says
"Not playable yet" and has no Play/Run action. NSP is not assumed to mean update.
The phone can now enumerate Program NCA title IDs, but does not yet derive
application/update relationships from CNMT or apply patches.
Current Castlevania evidence from the desktop inspector remains relevant: its
XCI bundles the game and a separate gallery; the supplied NSP updates the gallery.
It must not be attached to the main game's executable based on its filename.

## On-phone executable diagnostic

Build 8 adds a Files picker for user console keys. `SwitchKeys.swift` validates
only the header and key-area entries the reader needs, then stores them with
`WhenUnlockedThisDeviceOnly` Keychain accessibility and no synchronization.
Keys are not copied into the library, bundled, logged, or exported in reports.

`SwitchExecutable.swift` uses bounded file reads and CommonCrypto through
`SwitchCrypto.c`. It supports NCA3 header XTS, key-area ECB and ordinary AES-CTR
ExeFS sections. It checks the FS header and complete hierarchical SHA-256 tree
before reading the executable PFS0. Rights-managed content, CTREx/BKTR patches,
sparse and compressed NCA storage are explicitly unsupported on the selected
title. Publisher signatures and update composition are not checked. The separate
RomFS reader verifies its IVFC chain before returning asset bytes.

`NSOLoader.c` independently decodes raw/LZ4 NSO segments, validates file/virtual
bounds and overlap, checks declared segment SHA-256 hashes and zeroes BSS/gaps.
The app checks every module and maps all decoded modules into the CPU at
contiguous page-rounded bases. A title selection is required for bundled programs; no
main executable is guessed from the XCI filename.

`ProcessMetadata.c` bounds-checks META/ACI0/ACID structures and ID ranges from
the authenticated ExeFS `main.npdm`. This is structural parsing, not signature
verification or permission authorization. `SwitchProcess.c` uses its actual
stack size and creates one initial emulated thread context. It checks all NSO
sizes, including page padding plus stack/TLS, before allocating decoded images.
Limits are eight modules, 256 MiB mapped memory and a 16 MiB stack. Only 64-bit
36/39-bit address spaces are accepted. ASan, prevent-code-read and shadow-stack
process features are explicitly rejected. Fixed virtual addresses intentionally
omit ASLR in this diagnostic.

Text/rodata use Code state 3 and RX/R permissions; data/BSS use state 4 and RW;
stack and TLS use states 11 and 12. Partial-page padding is zero. Whole-page
internal holes remain allocated Code with no user permissions. The initial
thread has an owned CPU context and handle, NPDM priority/CPU metadata, a zeroed
TLS block with the handle at offset 0x110, and a randomized odd x18 cookie.
CPU-owned TLS registers are implemented. Scheduling remains unimplemented.

The current process services QueryMemory, validated GetInfo properties, owned
thread queries, real bounded SetHeapSize backing, ConnectToNamedPort and
CloseHandle. Unsupported calls stop. QueryMemory's full40-byte write must fit
one backing region; adjacent-region output remains conservatively rejected.
Reserved address ranges are Free until backed. Generated graphics stubs use
visible GeneratedCode0x14, never NSO Code3. The latter caused a real loader
regression, documented with its correction in the progress record.

The actual Castlevania main Program, `0100ba4026e94000`, contains four modules:
`rtld`, `main`, `subsdk0` and `sdk`. The Duo verified65,215,759 ExeFS bytes,
996 hash blocks and all12 declared NSO segment hashes from the imported XCI.
Stage16 interpreted66,968,689 instructions, serviced16 GetInfo requests and
opened one SM session. It stopped at SendSyncRequest (SVC0x21). No game-specific
native image or shader cache was prepared or transferred for this path.
See [current evidence](../Validation/phone-runtime-progress.md) for later stages.

Execution is bounded by instructions, dispatch events, deadline and cancellation.
`Runtime-check.json` contains safe title/module/protocol metadata and separate
synthetic GPU test results. It never includes instruction words, arbitrary
registers, asset names/data or keys. Elapsed time includes hashing and loading;
it is not interpreter throughput or FPS. RomFS verification has reached3,075
files and a4,096-byte sample on the Duo. A verified provider is now attached
for FSP current-program IStorage reads; actual game requests have not reached it.
Read-only session/domain IPC and atomic reply/data writes passed host checks.
See the current evidence for the exact implemented slice and device boundary.

## Execution architecture decision

The installed Zelda reference embeds rewritten game modules in signed Mach-O
sections (`NX2Apple/dm/host/loader.c`) and compiles shaders using a Mac toolchain.
It cannot accept a new XCI as new native CPU code inside the existing app.
Satchel's original NRO interpreter does execute imported bytes as data, but its
instruction coverage, 64 MiB guest model and limited Horizon services are not
adequate for general games. Its measured TicTacToeNX performance is not evidence
of usable commercial-game performance.

The adopted diagnostic backend is the BSD 3-Clause VIXL 8.0.0 simulator pinned at
`7b792dcee6250db405f7f692d3853786b4d2a92d`. Its embedding C API copies bounded
guest regions, enforces permissions, handles guest PCs and SVC stops, and exposes
budgets/cancellation. A small patch routes reviewed memory operations through
checked guest-address translation. Native callbacks, tracing, executable guest
allocations and unreviewed instruction families stay disabled. The library is
built once as emulator code, not once per game. See
[CPU source, license, tests and build](CPUProbe/README.md).

Stock VIXL is not a sandbox, and the patch alone does not make it one. Coverage
is deliberately restricted to independently tested instruction families. This
backend has owned TLS registers and bounded shared CPU contexts, but no process
scheduler or general Horizon environment. The old NRO
interpreter remains independent; its services and fixed 64 MiB memory model
have not been silently repurposed as a complete commercial-game environment.

[UTM SE](https://github.com/utmapp/UTM/blob/main/README.md) was another research candidate and demonstrates an ARM64
threaded interpreter without JIT workarounds. UTM's
[build script](https://github.com/utmapp/UTM/blob/main/scripts/build_dependencies.sh)
selects `--enable-tcg-threaded-interpreter` for ARM64; its
[dependency manifest](https://github.com/utmapp/UTM/blob/main/patches/sources) selects
QEMU 10.0.12-utm. This is a CPU research candidate, not an adopted dependency or
a complete Switch core. QEMU machine/Linux syscall behavior is not Horizon HLE.

Before adoption, inventory component licenses and establish an embedded CPU
interface. UTM's Apache frontend license does not cover its QEMU components.
[QEMU's license](https://github.com/utmapp/qemu/blob/v10.0.12-utm/LICENSE) and
individual source grants must be preserved and evaluated. No QEMU source has
been added to Satchel.

NX2Apple's MIT host/HLE and renderer remain references, with their third-party
notices. Their direct native guest pointers and function callbacks require a
guest-memory/ABI boundary before reuse with an interpreter. Its Ryujinx shader
subset is not a full emulator. The app now embeds that source-locked translator
as a signed iOS NativeAOT framework, with SPIRV-Cross and public Metal compilation
APIs. Original fixed fixtures passed on the Duo: Maxwell EXIT translated to
676 bytes of SPIR-V and 242 bytes of MSL, compiled and completed on the GPU;
a separate SPIR-V store fixture verified its output and three guard words.
Runtime dynamic-code feature flags were zero. These are synthetic compiler/GPU
tests, not game rendering or proof of game shader compatibility.

The diagnostic accepts only its fixed reviewed fixtures. Guest NVN calls,
state-aware translation, real resource bindings, compiler resource budgets and
game GPU submission remain unimplemented. See [graphics boundary](NVN_RUNTIME.md)
and [device evidence](../Validation/runtime-core.md). Build the components in
[MaxwellShaderCompiler](MaxwellShaderCompiler/README.md) and
[ShaderBridge](ShaderBridge/README.md) before generating/building the app;
these are reusable emulator components, not per-game Mac preparation.

## Required next gates

1. Connect bounded HIPC/CMIF service requests to owned session objects, verified
   read-only storage and additional state-backed services. Add scheduling when
   actual guest requests require it. Initial process startup is incomplete.
2. Broaden instruction coverage and validate SIMD/FP, atomics, TLS and SVC round trips, then
   the unchanged TicTacToeNX NRO against existing framebuffer/input references.
   Record device throughput and memory use before making performance promises.
3. Implement the guest ABI/Horizon and GPU submission boundary. Run an unchanged
   open-source GPU-rendered homebrew with shaders translated on the phone and
   visible output plus working input. Software framebuffer output is insufficient.
4. Connect verified RomFS access to guest filesystem IPC and add CNMT matching. Import a
   base package and matching update through Files, select bundled applications by
   internal IDs, verify updated code/assets, save, and relaunch offline. Reject
   mismatched updates. Keep keys out of logs, repository and exports.
5. Only then claim compatibility for specific tested commercial titles, including
   real gameplay, saves, cold launches and thermal/memory behavior on the Duo.

App Store eligibility remains separate and unproven. Apple's
[review guidelines](https://developer.apple.com/app-store/review/guidelines/)
permit certain emulator downloads; they do not guarantee approval of this app
or supply general CPU executable-memory privileges.

## Checks

`zsh scripts/test_packages.sh` exercises malformed and sparse large containers,
hash failures, cancellation, prefixed XCI and trimmed padding. Optional file
arguments inspect real packages read-only without keys.

`zsh scripts/test_package_import.sh` verifies a 256 MiB streaming copy, progress,
independent saved-content digest, duplicate/repair behavior, cancellation cleanup
and rejection of changing source size. Files/iCloud UI validation is separately
recorded in `Validation/phone-import.md`.

`zsh scripts/test_switch_executable.sh` checks AES known vectors and synthetic
NCA/ExeFS hashes, bounds, corruption and cancellation. `test_nso.sh` checks NSO
LZ4 decoding against Apple's independent encoder, malformed inputs and memory
sanitizers. `test_switch_keys.sh` exercises parsing without changing the Mac's
Keychain. `test_cpu_probe.sh --ios` runs native ARM64 comparisons and sanitizer
checks before building the iOS archive. No real game code is used as a fixture.

`test_process_metadata.sh` validates structural NPDM parsing with synthetic
fixtures and mutations. `test_switch_process.sh` covers page boundaries,
permissions, padding, BSS, whole-page holes, contiguous module placement,
stack/TLS initialization, feature limits, SVC dispatch, cancellation and atomic
failure for invalid QueryMemory output spans.

`RuntimeJourneyTests` exercises the actual installed app, Files/iCloud key import,
selection of an existing package, decryption, verification and original loader
execution. It also checks that the package still has no misleading Play button.

Process-layout sources (ABI facts independently implemented; no GPL source
copied): Atmosphère commit `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`,
[NSO loader](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/stratosphere/loader/source/ldr_process_creation.cpp#L550),
[process startup](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/kern_k_process.cpp#L952),
[memory information](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/kern_k_page_table_base.cpp#L1911);
[libnx QueryMemory wrapper](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/source/kernel/svc.s#L40).
