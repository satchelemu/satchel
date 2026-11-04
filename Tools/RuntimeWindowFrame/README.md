# Original Window frame producer for Swift tests

This host integration fixture creates an original Process/VI graph, executes original interpreted NVN device/pool/program/texture/queue/Window calls, rasterizes a31×17 red image on the real Vulkan backend, and leaves its immutable frame READY for the Swift consumer. It contains no game files or substitute precomputed pixels. Its interval7 and linear-sRGB/top-left image policy are explicitly synthetic. It does not establish a displayed frame or NVN defaults.

`WindowFrameBridge.h` defines two C functions. `begin(epoch,mode,report)` uses the caller's real test surface epoch and returns host-only fixture/window tokens. Mode0 retains the original Process until finish; mode1 destroys it before READY while the dispatcher remains active, so retirement cannot discard an untaken frame. The consumer uses production Window take/copy/release on the same serial graphics worker. `finish(token,report)` then retires the C owners; a held frame lease produces retryable BUSY. It preserves every nonzero owner until drain. No CPU/provider pointer escapes C.

The frozen C/header/Test source is identical to the earlier reviewed host fixture. Four thousand two hundred thirty-three ASan/UBSan assertions cover both CPU lifetimes, exact pixels, actual held-lease cleanup refusal/retry and resource equality. Counts include per-byte comparisons. This is a macOS/Metal test, not device validation. No fault-injection switches are added to production libraries.

## Reproduce

Run from a checkout containing this directory and the reviewed Window component:

```zsh
zsh scripts/test_runtime_window_frame.sh \
  --dependency-root /path/to/full-checkout \
  --windows-manifest /path/to/passed-window-build/validation-manifest.json \
  --cpu-archive /path/to/matched/libSatchelCPUProbe.a \
  --cpu-header /path/to/matched/CPUProbe.h \
  --cpu-source /path/to/matched/CPUProbe.cpp \
  --ipc-archive /path/to/validated/libSatchelHorizonIPC.a \
  --backend-build /path/to/passed-backend-build \
  --compiler-build /path/to/passed-captured-compiler-build \
  --scalar-build /path/to/passed-scalar-build \
  --moltenvk-root /path/to/installed-validated-MoltenVK \
  --output /tmp/fresh-window-frame-producer
```

`--source-root` optionally selects another checkout containing this fixture; it defaults to the recipe's repository root. All dependency products are explicit. The passed Window manifest's absolute source identities are mapped onto `--dependency-root`; no old scratch source path is reopened. The selected CPU source/header/archive must exactly match that Window gate. IPC and MoltenVK archives are pinned by the matched Window source lock. Backend/compiler/scalar products are checked against their validated artifact manifests and repository source hashes. Every C and Process dependency is rebuilt locally, with private copies of CPU/IPC inputs. Output must be a fresh directory under `/tmp`, outside both source roots. No dependency installation, canonical mutation or device action occurs.

To select a different compatible CPU checkpoint, first produce a validated Window gate using `scripts/test_guest_windows.sh` with that checkpoint, then pass its manifest here. An arbitrary source/archive pair is never silently accepted.

The output manifest preserves the Swift recipe's existing schema: `artifact`/hash, `header`/hash, exact `link_inputs`, `framework_flags`, `rpath`, sanitizer flags and two exports. Link the fixture archive once and each listed dependency once; never link Test.o or the C test executable. `GuestDispatch.o` and `GuestVulkanWindows.o` retain their names for the Swift harness's separately reviewed test-only forwarding wrappers. UI-copy bytes are separately budgeted by Swift.

Existing renderer/compiler/reference source pins, licenses and notices remain in their respective repository components. This package adds no dependency and copies no third-party implementation. The original fixture encoders and lifecycle derive from the project's already-reviewed RasterDispatch/ProcessWindow tests; `source-lock.json` retains the original gate hash.
