# Checked interpreted NVN raster

**Implemented and host validated; iOS17 compiled. This checkpoint has no phone raster or game-frame validation.**

`satchel_guest_dispatch_enable_vulkan_raster` is an explicit real-Vulkan opt-in that preserves the existing compute and transfer paths. The common Program object/tag remains privately owned by GuestCompute/GuestPrograms; an eight-entry family table selects unassigned, compute, or raster after actual source admission. Compute↔raster replacement requires successful Finalize→Initialize. Failed admission preserves the old capture. Raster admission is not pipeline creation.

The paired compiler consumes actual bounded GPU-visible wrapper/SPH/instruction/C1 captures. The independent backend validates the entire emitted scalar vertex/fragment SPIR-V schema. It never substitutes a fixture module. Known NVN stage bits are vertex1/fragment2 (pair3), distinct from SPH stages1/5. The additive unused-state proof is derived only after full source decode; YNegate remains unknown and provably unused, not reported as observed0. All other shader state remains explicit. Ordinary owned high-level NVN viewport commands imply an enabled viewport transform, an emulated API invariant supported by public deko3d initialization and independently reviewed. Raw/called/unrecognized command streams are not admitted.

The existing queue/command/handle owner records copied actual state, retained program/texture/vertex references, and bounded caller-owned representations. Draw/Clear commit their record and command count atomically before becoming executable. Each recording supports up to16 ordered commands/eight draws, one RGBA8 target, one vertex stream, triangle lists, no indexing/instancing. Mixing raster with transfer/compute in one recording or submitting multiple raster handles together is explicitly unsupported. Recording references block recapture and finalization. Queue initialization snapshots the real owned device origin/depth modes. Their documented initialization defaults are lower-left/near-minus-W; this initial renderer supports only explicitly selected upper-left/near-zero. It does not silently change those defaults. All required per-command state must be bound; inherited partial state, views, blending/depth/stencil/multisampling, textures/samplers, arbitrary shader profiles and partial clears remain unsupported.

QueueSubmit owns a nonnull raster run before any fallible wait. Cleanup-only admission failures retain ownership without incrementing the GPU submission counter. For a successful submit, the prerequisite texture upload fence is completed within a bounded wait and the actual ordered draw batch reaches vkQueueSubmit before success returns. QueueFlush validates already-submitted state without waiting or changing CPU/GPU cache visibility. QueueFinish waits the actual draw fence, reads/repackages the target, publishes the exact block-linear bytes to the pool mirror, then commits guest queue metadata. A metadata-write failure after publication retries metadata only; it cannot redraw. Readback/repack/publication failures retain the resources and pool GPU-access holds. Every pending raster owner excludes cached legacy compute/transfer submissions through publication. All calls and retirement remain on the common serialized graphics worker; cancellation callbacks only read cancellation/deadline state.

Retirement drains queue runs, recordings and leases before paired captures, common programs, textures, pools, and devices. It stores no CPU, provider, cancellation pointer or native address in guest RAM. Common Program finalization marks a separate retiring state after tag removal; fallible pair-forget cleanup can retry without requiring the removed tag. Null-tolerant partial construction cleanup creates no jobs before attachment. Independent read-only review by the compiler agent found no new queue/composite bounds or lifetime blocker.

Resource bounds remain explicit:16MiB/pool, eight pools/owner, actual Vulkan padded buffer accounting128MiB globally; pool-copy staging64MiB globally; raster temporary tiled+linear bytes64MiB globally and four retained runs. RGBA8 targets have8MiB logical/32MiB actual global image limits and2048 dimensions. True block-linear layout supports720p/1080p within those bounds; they are limits, not game compatibility claims. No window/acquire/present-to-screen interface exists yet. Correct offscreen target publication is not a displayed game frame.

## Original validation

Run from the repository root:

```sh
zsh scripts/test_nvn_raster.sh --ios --output /tmp/satchel-nvn-raster-build
```

The recipe uses repository source and the explicitly pinned external toolchain/dependencies, writes only to the selected scratch directory, and records its source lock, builder hash, CPU/header checkpoint, validated scalar compiler dependency and artifact hashes. Host gates use Apple clang with ASan+UBSan halt-on-error. All16 matched static archives and the framework target arm64 iOS17; the framework has44 exact exported APIs and full dependency notices, with no nested dSYM or IOKit dependency.

The final portable run passed:

- Raster compiler4,398, paired program483, independent NVN ABI10 and state32,920 assertions, plus six original modules validated for Vulkan1.1.
- Texture layout131,927,145 byte-level assertions; padded pool budget193, pool registry1,480 and texture owner245 assertions.
- Actual ordered GPU batches91,788, guest raster publication31,245, recording6,360 and shared queue6,360 assertions.
- Interpreter routing13,050 assertions,620 instructions/210 dispatch events, including bad LR, physical aliases, transactional failure, unsupported state and retained cleanup.
- Reusable diagnostic25 outer assertions in this run; each successful original journey executes558 instructions/186 events, with two source admissions/two pipelines/three commands/one draw fence. Conditional cleanup assertions depend on real fence readiness; the earlier standalone run counted26.
- Existing lifecycle48 instructions/14 events, pool107 assertions, interpreted pool109, transfer111 and compute44. The new raster opt-in also passes the existing compute438-instruction/146-event and transfer282-instruction/94-event journeys.

The recipe preserves actual pending completion, cancellation after submit, bounded handoff/reap, reentry refusal and CPU-free cleanup tests. Counts often include individual byte comparisons; they are not independent scenario counts.

The prior immutable resource checkpoint is `/tmp/satchel-nvn-raster-staging/validation-manifest.json`; it carries layout/padded-budget/image/batch/publication gates. The paired capture/compiler checkpoint is `/tmp/satchel-raster-unused-state-staging/validation-manifest.json` (483 owner,4,398 compiler,10 independent pinned ABI assertions, six unchanged validated SPIR-V modules). The state codec checkpoint is `/tmp/satchel-nvn-raster-state-staging/validation-manifest.json` (32,920 assertions against separately compiled public bitfield ABI). These are assertion counts, frequently per-byte checks, not numbers of independent test scenarios.

The first interpreter run exposed a scratch dispatch-catalog placement bug, fixed by routing new state/device/texture names through the composite rather than the pool lookup. The stricter state decoder exposed invalid zeroed reserved comparison fields in the old raw host fixtures; those fixtures now encode the independently documented default depth/stencil fields with depth write disabled. No guard or assertion was removed. The reusable diagnostic uses actual NVN default/setter calls.

## Integration contract

`Tools/GuestGraphics/GuestNVNRasterDiagnostic.h` is stable. Call `satchel_guest_nvn_raster_diagnostic(report,cancel,context)` on the same serial graphics worker as all graphics dispatch/retirement. Success: memory.status0/stage8, instructions558/events186, admissions2/pipelines2/commands3/fences1, cache/value/guard/state/cleanup flags true. Failed/cancelled paths retain any pending ownership internally and the next invocation reaps or returns BUSY. This C path does not warm the NativeAOT runtime; preserve the existing cold-first diagnostic order.

The build output `validation-manifest.json` records the complete matched artifact set. The reviewed promotion manifest separately maps its canonical destinations. GuestTransferRequest and raster reflection are additive ABI changes, so all affected static callers are rebuilt; never mix old wrappers with new dispatcher headers. The combined paired owner/compiler archive must be linked exactly once, without an additional old RasterCompiler archive. Existing ScalarCompiler and NativeAOT/ShaderBridge remain unchanged. App/project integration must use the complete matched set.

The reproduction script intentionally preserves external CPU, scalar compiler and reference dependency inputs; it verifies their identities rather than copying or silently rebuilding them.
