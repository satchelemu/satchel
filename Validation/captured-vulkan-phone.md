# Captured original shader validation on the Duo

Phone stage 68 passed on the maintainer's physical iPhone on 2026-10-31. The app compiled
two original vertex/fragment pairs from checked guest-memory wrappers, created
two real Vulkan pipelines, submitted four offscreen draws, waited for four
fences, and verified the resulting pixels and guard bytes. Source replacement,
private CPU register preservation and all resource cleanup checks passed.
Each of the four draws also exercised cancellation before submission and while
retaining a submitted job. All tracked devices, buffers, jobs, targets, pipelines
and allocated byte counts returned from zero to zero.

These are original synthetic shaders with explicit original raster state. They
are not Castlevania shaders, guest-originated NVN calls or displayed game frames.
The app still reports `graphicsImplemented=false` and does not offer package Play.

Both separately signed NativeAOT frameworks now execute in the same actual iOS
app: existing `SatchelShaderProbe` first, new `SatchelCapturedFlat` second. All 11
previous graphics diagnostics and the new captured diagnostic passed together.
This verifies this startup order; other cold orders and actual background
cancellation during the captured compiler have not yet been tested on the phone.
Frameworks remain loaded for the process lifetime; no managed objects cross the
C boundary. Existing component license notices are embedded intact.

The C diagnostic returns a status enum, where zero means success. Swift requires
the complete version/size/stage, expected counts, values, guards, state and cleanup
before showing a synthetic success. A zeroed or stale report cannot pass. Pending
ownership remains in the bounded C owner after CPU destruction; the shared serial
graphics worker polls it using a permanent always-cancel callback with no borrowed
session token. That path drains old resources without starting another fixture.

Stage 68 deliberately used the already validated combined integer SIMD CPU while
FMOV validation finished separately. Castlevania reached 67,464,985instructions,
main+0x17477b4, the same unsupported instruction boundary as67b. All 63 IPC requests
completed; guest asset IPC reads remained zero. No game-rendering improvement is
claimed from the synthetic GPU result. All 5 library entries remained byte-identical.

Evidence:

- `/tmp/satchel-runtime-castlevania-68.json`, `.xcresult`, `-artifacts.json` and build/test logs.
- `/tmp/satchel-captured-app-staging/canonical-promotion.json`: App/source/review binding.
- `/tmp/satchel-captured-diagnostic-repro-overlay/canonical-promotion.json`:9source files and diagnostic-only archives.
- Host diagnostic: 94 normal assertions and 27 fault assertions in final relocated run; fault retry counts can vary by one nonblocking poll.
- `zsh scripts/test_captured_vulkan_report.sh`: 10 Swift/ASan report assertions passed.
- `/tmp/satchel-library-before-runtime-68.json` and `after-runtime-68.json`:SHA256 `40904b42256953fcdfd1b2604415b4b5b7fd1b48621230427f6f515c9ea7f5f4`.

Rebuild the diagnostic with `scripts/test_captured_diagnostic.sh` and explicit
validated inputs documented in `Tools/GuestGraphics/CAPTURED_DIAGNOSTIC.md`.
App builds must use Release with the existing custom physical-device xctestrun,
whose target paths are `Release-iphoneos`. Verify runner paths and linked artifact
hashes before claiming a new installed build was tested.
