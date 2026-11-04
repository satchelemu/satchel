# Interpreter-era README (archived 2026-11-03)

This is the README as it stood before the public release cleanup. It describes the VIXL interpreter route and the early milestones, kept as history. The current README is at the repository root.

---

# Satchel

> **Direction changed on 2026-11-01.** Satchel now follows the native-execution route
> proven by NX2Apple/nxnative, with every per-game step done on the phone. See
> [PLAN.md](PLAN.md) for the plan and current milestone and [AGENTS.md](AGENTS.md) for the
> rules. The description below is the interpreter-era status (VIXL CPU diagnostic), kept
> for reference; that path is now a fallback and diagnostic, not the main route.

# Satchel 0.3 (interpreter-era notes)

**Current target: import games from iCloud and run them entirely on the phone,
without preparing each game on a computer.** The app now has streaming XCI/NSP
imports, container validation, cancellation, local content-hash storage, and
persistent package details. **Imported XCI/NSP packages are not playable yet.**
Build 8 decrypts and verifies the selected Program archive on the phone, decodes
its NSO executables in memory, and executes original startup code through `rtld`
into the game's SDK
in a bounded no-JIT CPU diagnostic. **This is not game boot or rendering.**
The diagnostic maps all modules using NPDM process metadata and services memory
queries from the actual emulated map, owned thread/process properties and
bounded heap/session objects; unsupported Horizon calls still stop.
The check uses the unchanged 4.85 GB Castlevania XCI already imported through
the Duo’s iCloud picker. Console keys are imported through Files and stored in
device-only Keychain storage. No prepared game code is transferred from a Mac.
Update association/application, full process startup, guest filesystem services
and guest GPU execution remain incomplete. Verified RomFS reads already work.
On the Duo, stage73 executes67,505,918 original instructions and completes all63
service requests reached. It verifies and attaches the correct application
metadata directly from the imported XCI, closes service sessions correctly, and
initializes the application parental-control session with that metadata. Local
policy remains unconfigured; no permission decision is synthesized. The game now
maps a real read-only clock page seeded from the device, using the same timeline
as its CPU timer. Reviewed integer SIMD comparisons, logical operations, reductions and permutations,
GPR/SIMD FMOV bit transfers, vector count/widening reductions, vector-pair/DUP/MOVI and B/H/S/D/Q memory transfers,
real partial memory reprotection, and one-way permission locking now pass. The phone serviced two
permission changes covering491,520 bytes and three attribute calls leaving
4,456,448 bytes permission-locked. The next stop is an instruction outside the
reviewed CPU subset in the main module (UCVTF at offset `0x55db14`).
Broader services and graphics remain incomplete.
The embedded Maxwell → SPIR-V → Metal compiler path also passes original
synthetic tests on the phone, including a separate verified GPU buffer write.
Vulkan through MoltenVK also passes synthetic compute and interpreted device
creation/cleanup, guest-memory pools, and interpreted command recording/submission
with verified GPU transfer results on the phone. A cold-first compiler test also
passes14 GPU computations using its actual returned SPIR-V. Interpreted program
admission and compute commands now pass4 further real GPU submissions, including
shader changes and explicit memory visibility. Original interpreted raster also
passes a clear and two draws through two captured shader programs, with verified
pixels, cache visibility and complete cleanup. A separate immutable frame-handoff
component is host-validated and now linked to the app through a checked
texture-to-frame owner. Its original GPU-mirror snapshot diagnostic passes
431 normal and30 injected-failure host assertions. The matched Release73 phone
test verifies two uploads, one completed snapshot, one cancelled/drained snapshot,
and an immutable frame lease that remains valid after CPU destruction, with all
resources released. This is an original offscreen fixture, not a game frame.
Window presentation is still missing. A
bounded captured-program compiler and typed Vulkan pipeline are now in the
repository. Their live host test compiles32original shader pairs and verifies
64GPU draws, alongside cancellation and cleanup. The Vulkan framework
passes the full regression. A new adapter binds actual device/target facts and
explicit renderer policy to the compiler; its original-source GPU tests pass.
The captured compiler is now embedded alongside the existing compiler. On the
Duo, two original captured shader pairs produced four verified offscreen draws
with complete cleanup. The new FIRST/LAST flat-shading test verifies two additional
offscreen draws with matching colors, coverage and untouched vertex data;
all14 original graphics checks pass with zero retained resources. Actual game-state
binding remains incomplete. The actual dispatcher now supports bounded captured
source admission but refuses binding when required shader state is unavailable;
Castlevania has not yet reached that call path. The latest
installed Duo checkpoint is72. See [captured shader validation](Validation/captured-vulkan-phone.md).
These are component checks; Castlevania has not produced a game frame. See [current progress](Validation/phone-runtime-progress.md).
[Import validation](Validation/phone-import.md) · [Phone runtime work](Tools/PHONE_RUNTIME.md) ·
[Runtime validation](Validation/runtime-core.md) · [Graphics boundary](Tools/NVN_RUNTIME.md).

The separate NX2Apple native build of Zelda 1.4.3 remains a limited gameplay
proof: its cave scene and touch-driven menu/stick movement were verified on the
Duo. That build required Mac preparation and game-specific signed code; it does
not satisfy the phone-only goal. Save/reload and broader traversal remain
unverified. [Native tooling](Tools/NATIVE_GAMES.md) ·
[Native evidence](Validation/switch-native.md).

**The author's downloadable TicTacToeNX game now runs on the physical iPhone Duo.**
Its unchanged ARM64 NRO executes in the interpreter, including all game logic,
pixel drawing, controller handling, and clean exit. The entire app opens directly in landscape and stays there through the library,
file details, and gameplay. The library uses the open display width; the player
fits the original game image across it with overlaid controls.

The landscape library pairs an import/workshop panel with a searchable, sortable
collection on the inner display. On the smaller cover display, the collection
uses the available width and the import/workshop actions remain in the native
side toolbar. Menus explicitly request system status indicators. These are visible on the inner
display; the current OS still omits them on the cover display in landscape. The layout follows current
window width, so opening the keyboard does not switch the library's layout.
File details and player diagnostics use padded, wrapping cards. Tile colors and
initials are abstract placeholders, not game artwork or compatibility ratings.
See the [Duo UI validation record](Validation/duo-polish.md) for open/cover display
checks, accessibility text testing, screenshots, and remaining limitations.

Open **Satchel → tictactoe → Play homebrew**. Use the D-pad to move the white
selection border and A to place a tile. Yellow and green are the two players.
Plus exits; Pause and Stop are in the toolbar. Measured performance was about
**0.8 FPS** on the Duo, usable for testing this turn-based game, not action-game
performance or broad Switch compatibility.

[Download the verified original NRO](https://raw.githubusercontent.com/sh3rlock97/tictactoe/893fe0d41b0855f2b41b8f1ca1b1e3f156e98b3e/tictactoe.nro)
from the [author's MIT-licensed project](https://github.com/sh3rlock97/tictactoe).
Save it to iCloud Drive, then choose **Import from Files → Browse → iCloud Drive**.
A validated local copy is kept for offline play; originals are preserved.
The library is local and is not automatically synchronized between devices.

**Supported execution format: NRO. Imported storage formats: NRO, XCI, NSP.**
The importer identifies contents, including renamed files. XCI is a game-card
container; NSP can contain a base game, update, or DLC. Packages are copied in
1 MiB chunks and kept locally, with bounded directory and XCI hash-region
checks. Those checks do not decrypt or authenticate the entire game, establish
update relationships, or imply compatibility. Package detail views expose no
Play action. A separate executable check verifies the selected archive's ExeFS
hash tree and declared NSO segment hashes; it does not authenticate publisher
signatures or verify every asset in the package. [Format research](Validation/formats.md).

The app includes an original minimal Horizon sample for testing import and debug
output. **Save sample to Files…** and **Load Horizon sample** remain available.
**Run diagnostic** is a short bounded execution check; **Play homebrew** runs
continuously with a no-frame watchdog and responsive pause/stop.

Validation includes twelve full RGB-frame comparisons against the unmodified
original game source, native ARM64 instruction comparisons, sanitizer-backed
memory/IPC/format tests, and real-device UI assertions for both players' moves,
landscape lock, width, pause/resume, and stop. See the [game validation record](Validation/game.md).
Historical first NRO/iCloud milestone: [0.2 validation](Validation/homebrew.md).

The homebrew environment implements a limited legacy libnx software-framebuffer
path: application services, HID shared memory, memory management, VI/Binder
buffers, and NV memory bookkeeping. GPU submissions, audio, general threading,
filesystem/RomFS access, and complete Horizon services remain unsupported and
fault explicitly. Most other homebrew will not run. nx-hbmenu 3.6.1 now executes
32,048 instructions before an unsupported SIMD load; its menu does not render.

NRO inputs and guest memory are limited to 64 MiB. Container imports have a
128 GiB policy limit and require space for a full local copy plus a reserve. Guest code never executes
natively and never obtains executable host memory. The new diagnostic embeds a
reviewed subset of the BSD-licensed VIXL simulator; its source pin, memory guards,
and build instructions are in [Tools/CPUProbe](Tools/CPUProbe/README.md).
The shader self-test embeds the MIT Ryujinx/Spv.Generator translator through
.NET NativeAOT and a pinned SPIRV-Cross library; their complete notices ship in
app resources. They compile shaders as data and do not enable CPU JIT.
User-imported console keys stay on the device; no keys or game code are bundled
in the project. This is development-signed proof-of-concept
software, not an App Store release or a hardened sandbox.

## Original interpreter demo

A working iPhone proof of concept for **interpreting ARM64 code without JIT**.
It runs an original brick-breaker program compiled from freestanding C. Game
logic and every framebuffer pixel are produced by the guest instruction stream;
the SwiftUI host displays the framebuffer and supplies touch input.

**This is not yet a complete Nintendo Switch emulator.** It contains no Nintendo code,
games, firmware, keys, or copied NX2Apple engine components. NX2Apple's documented
architecture informed the experiment. Its graphics and OS layers have not yet
been integrated. The demo's speed does not establish Switch-game viability.

## On the iPhone

Open **Satchel → Open interpreter demo**. The demo starts in auto play and checks the first 600 frames
against a native reference in approximately ten seconds. Drag on the playfield
to take control. The buttons pause/resume, restart, toggle auto play, and open
diagnostics. Diagnostics includes a fresh validation run and a shareable JSON
report. A touch before frame 600 interrupts that validation; it is not a failure.

The installed build is development-signed with the user's existing Apple team.
It was launched directly with `devicectl`, without attaching a debugger. It is
not an App Store distribution. Ordinary development-signing expiry still applies.
The app needs no network connection or Mac during play after installation.

## Architecture and limits

`Guest/brick.c` → Xcode clang → `brick.arm64` data → C interpreter → RGBA framebuffer → SwiftUI

- `Core/Interpreter.c`: bounded 4 MiB guest memory; ARM64 integer instruction
  subset; flags, branches, arithmetic, conditional operations, stack pairs,
  loads/stores. Guest writes into the code region are rejected. Unsupported
  instructions, out-of-bounds accesses, and unknown services stop the guest.
- `Guest/brick.c`: original 160×240 game. The compiled image is 1,244 bytes with
  this installed Xcode version. It uses a **private** `SVC #0xF001` to yield a
  frame and receive input. This is not the Switch Horizon service ABI.
- `App/EmulatorSession.swift`: one guest frame per display callback, capped at
  one million interpreted instructions per frame. The host displays pixels,
  provides touch input, and records timings; it does not implement game physics.
- No JIT, `mmap(PROT_EXEC)`, dynamic guest libraries, downloaded native code,
  hypervisor, or debugger attachment. Guest bytes remain ordinary data.
- The original demo and NRO paths remain independent of the package diagnostic.
  The new NCA/NSO loader verifies executables but does not provide full process
  startup. There is no complete SIMD/floating-point implementation,
  full Horizon environment, NVN translation, emulated audio, or commercial-game compatibility.
- This prototype is not a hardened sandbox for arbitrary untrusted programs.
  Import only homebrew from sources you trust.

## Reproduce

Requires macOS, Xcode with the iOS SDK, Python 3, XcodeGen and .NET SDK 10.0.403.
Before building, fetch the pinned sources using the instructions in
[CPUProbe](Tools/CPUProbe/README.md),
[MaxwellShaderCompiler](Tools/MaxwellShaderCompiler/README.md) and
[ShaderBridge](Tools/ShaderBridge/README.md). Build and validate all three
emulator components. This does not compile game-specific native code.

```zsh
cd '~/Documents/Repo - Personal/satchel'
zsh scripts/validate.sh
zsh scripts/test_cpu_probe.sh --ios
zsh scripts/test_maxwell_shader.sh --checkout /path/to/NX2Apple --dotnet /path/to/dotnet --work /tmp/satchel-shader-aot --platform all
zsh scripts/test_shader_bridge.sh --ios --exit-spv /tmp/satchel-shader-aot/repro/exit.spv
xcodegen generate
open Satchel.xcodeproj
```

Select the **Satchel** scheme and the physical iPhone. `project.yml` holds the
existing signing team. Use a team you control if building elsewhere.

CLI build/install (replace `YOUR_DEVICE_UDID` with a paired device):

```zsh
xcodebuild -project Satchel.xcodeproj -scheme Satchel \
  -configuration Release -destination 'id=YOUR_DEVICE_UDID' \
  -derivedDataPath /tmp/satchel-build/DerivedData \
  -allowProvisioningUpdates build
xcrun devicectl device install app --device YOUR_DEVICE_UDID \
  /tmp/satchel-build/DerivedData/Build/Products/Release-iphoneos/Satchel.app
xcrun devicectl device process launch --device YOUR_DEVICE_UDID com.dell.satchel
```

`Tests/validate.c` compares **every framebuffer byte and all seven exported state
fields over 1,800 frames** with independent native execution of the guest C
source. The input trace exercises auto play, missed balls, lives, paddle clamps,
and reset. It also checks invalid instructions, infinite-loop budgets, memory
bounds, code-write protection, unknown service calls, and invalid image size.
It writes the expected frame-600 checksum used on the phone.

`Tests/UI/PlayerJourneyTests.swift` checks the device validation, pause/resume,
manual paddle movement, reset, and diagnostics through the actual app UI.

## Next feasibility milestone

1. Improve interpreter throughput and profile sustained gameplay/thermals.
2. Bring up a second freely downloadable game, using its missing instructions
   and services to expand compatibility with independent tests.
3. Extend filesystem/RomFS, modern libnx, audio, and thread behavior before
   attempting GPU-rendered workloads. Keep unsupported operations explicit.
4. Add container/content loading and update composition for NSP/XCI as a separate
   feature; recognition does not imply execution. Audit upstream licenses before
   adapting other emulator components.
5. Assess App Review eligibility separately; this device build is not approval.

Reference: [NX2Apple architecture](https://github.com/LNPANDY/NX2Apple/blob/main/docs/development.md).

Pinned source reviews of Eden, Citron Neo and NxEmu, including reusable components
and iOS execution limits, are in [Reference emulators](Tools/ReferenceEmulators/README.md).
No code or dependencies from those projects have been adopted by this research.
