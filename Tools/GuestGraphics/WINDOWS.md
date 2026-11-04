# Checked Window composite checkpoint

Implemented, host-validated and iOS17-compiled in the reproducible overlay. No canonical source/artifact or App change is part of this checkpoint. No game Window call or displayed frame is claimed.

`GuestWindows` owns one Window with two or three checked same-device texture references, a real VI native-generation reference, one pending numeric-context ordering transaction, and up to three immutable frame snapshots. Its private tagged builder fits the public 64-byte size without truncation; its Window tag is an emulator representation, not a retail layout. Shared texture/frame/backend budgets remain authoritative. The separate `GuestVulkanWindows` object retains the captured-program, legacy raster, scalar compute, transfers and host-selected texture-frame paths. Old composite source and link contracts are unchanged. New routing in GuestDispatch appends statuses20 WAIT,21 surface-unavailable,22 timing-unavailable; preexisting numbers are unchanged.

## Caller contract

Create the Process/VI owner and attach a real surface epoch before `satchel_guest_dispatch_enable_vulkan_windows`. The VI dependency must outlive the active dispatcher. On teardown, hand off the dispatcher first, then attempt VI destruction; live native refs keep VI retained until CPU-free Window cleanup releases them. `try_destroy`/retirement pending results retain ownership, never discard it.

`satchel_guest_dispatch_handle_windows` borrows the exact CPU plus current Process/context numeric identity. Bridge `SatchelGuestWindowContextCheck` to the independently validated `satchel_switch_process_validate_context`. That predicate establishes live current-thread identity only. The adapter independently checks fresh common-trap PC, original LR and executable return range. The ordinary dispatcher provisionally moves PC to LR, and restores the common-trap PC for WAIT or any refusal. No borrowed CPU, Process, callback or stack context is stored after the call. A pending request stores only numeric Process/thread identity, PC/LR/args and real queue lease. Retry requires the same retained thread restored as current, fresh stop/PC/LR and unchanged arguments; a cached CPU pointer alone is insufficient. Poll is CPU-free; it cannot advance a preparation requiring fresh guest-memory validation. Cancellation retires that preparation without inventing a frame.

Use one serial graphics worker for dispatch, poll, frame take/copy/release, detach and retirement. Callbacks are pure and non-reentrant; cancellation must also report asynchronous surface epoch invalidation. The public fence timeout is capped at10ms, but synchronous publication/readback/driver work has no hard total wall-time guarantee. A naturally aligned report always receives the actual return status; early pre-dispatch refusal has operation/event counters zero rather than a cumulative event snapshot.

## State still unavailable for actual presentation

The pinned NX2Apple8227f80f9b381c680611303319656c4e0c806a50 public reconstruction establishes signatures/sizes, not a complete vendor timing specification. Its interval clamping and simplified queue implementation do not prove NVN numeric interval semantics/defaults. RGBA8_UNORM does not establish a display transfer function or image origin. App must refuse missing policy or explicitly document a separate reviewed emulator-surface policy; it must not claim a synthetic value is game-observed NVN state.

Initialize can retain actual textures/native identity without pretending that timing/crop are known. Acquire/Present require explicitly set interval/full crop and a pure policy callback with real epoch/generation and the admitted RGBA8/linear-sRGB/top-left snapshot profile. Original tests use interval7/epoch77 as explicitly synthetic host policy. Sync-based WindowAcquireTexture, unsupported formats/crops/active-count modes and missing actual shader state remain refused. Snapshot-ready is not displayed/vsync. UI copies consume an additional App budget outside the C frame registry.

## Validation and reproduction

All scripts require a fresh output under this task directory and validate the explicit frozen dependency manifest before/after. The repository-ready entrypoint is `scripts/test_guest_windows.sh`. It rebuilds the complete C owner/composite dependency set; external validated compiler/backend/scalar/MoltenVK products are hash-bound. Full matched raster and affected existing recipes have also passed against this overlay; no promotion is performed by any recipe.

- Owner gate08:12,590 added assertions plus6,361 preserved original queue/raster assertions. Includes cancellation/epoch change during work, early BUSY untouched report, release-success/destroy-BUSY retry, newer-frame FIFO, source-slot reuse, aliases and CPU-free leases.
- The unified recipe runs owner, interpreted Window, actual Process identity, captured admission/reference retry, compute, host-selected snapshot, dispatcher and output-range tests. `WindowReproduction.json` records every resulting count and manifest hash. All assertions from the earlier isolated recipes remain present; counts include polling/byte comparisons.
- Its exact production iOS exports are11 Window owner,16 additive composite and19 unchanged dispatcher API exports. No CPU test-only or fault wrapper is present in production archives.
- Full `scripts/test_nvn_raster.sh --ios` passed with the same dispatcher source and explicit private CPU archive. Affected captured owner, captured diagnostic, captured NVN, texture-frame diagnostic, texture-frame owner, output-range and presentation-queue recipes passed their refreshed locks without deleting any checks.

From a repository root, use installed validated dependency builds and a matched production CPU source/header/archive checkpoint:

```zsh
zsh scripts/test_guest_windows.sh --ios --output /tmp/satchel-window-check \
  --cpu-archive /path/to/matched/libSatchelCPUProbe.a \
  --cpu-header /path/to/matched/CPUProbe.h \
  --cpu-source /path/to/matched/CPUProbe.cpp \
  --ipc-archive /path/to/validated/libSatchelHorizonIPC.a
```

`--source-root`, `--backend-build`, `--compiler-build`, `--scalar-build` and `--moltenvk-root` are configurable. Defaults match the repository's established validated scratch artifact locations. The recipe verifies compiler/backend/scalar production artifacts against their manifests and matched sources, and pins the installed MoltenVK and IPC archive hashes. CPU input provenance is explicitly recorded/copied before use; callers must select a matched production checkpoint. It does not claim an arbitrary user-supplied archive matches arbitrary source bytes.

Existing source pins/licenses/notices remain unchanged. No new dependency is introduced. New Window code derives from the repository's reviewed texture-frame composite; the derivation patch and original checkpoint preserve that credit/history. Public NVN pointer ABI/sizes were checked against NX2Apple8227f80f9b381c680611303319656c4e0c806a50, not inferred from guest opaque layouts.

Assertion counts include byte/canary comparisons and occasional polling assertions; they are not independent scenario counts. Failed `regressions01` is preserved: generated harness initially redefined the original nested `main` macro. `regressions02` renames only top-level test entrypoints, without removing assertions, and passes. All production files are unchanged by this test-harness correction.

Expected later link additions are standalone `libSatchelGuestWindows.a` and `libSatchelGuestVulkanWindows.a`, alongside a matched GuestDispatch archive. Existing VI/texture/frame/transfer/captured compiler and owner libraries remain required; do not duplicate implementations inside these archives. No Process object is linked into the production Window archives. Exact paths/hashes and independent review bindings are in `WindowReproduction.json`; `WindowCheckpoint.json` preserves the prior isolated checkpoint.
