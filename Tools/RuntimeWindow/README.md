# Original Window Swift integration fixtures

This recipe checks the actual borrowed Swift Window helper and the exact App frame-copy worker against real C owners. It uses existing installed Apple tooling; it downloads no dependencies, changes no source inputs, and performs no device actions.

Prerequisite: run the separately reproducible C producer in `Tools/RuntimeWindowFrame/build.py` against a validated Windows dependency checkpoint. Its successful manifest must bind `inputs`, `link_inputs`, `artifact`/hash, `header`/hash, `framework_flags`, and the original C test result. That producer rebuilds the C/Process dependencies and creates the actual original raster/Window READY frame. The Swift fixture does not substitute an image or fabricate owner success.

Run with explicit inputs:

```sh
scripts/test_runtime_window.sh \
  --source-root /path/to/repository \
  --dependency-root /path/to/validated-c-source-root \
  --cpu-archive /path/to/matched/libSatchelCPUProbe.a \
  --producer-manifest /path/to/producer-output/validation-manifest.json \
  --output /path/to/fresh-external-swift-output
```

`source-root` provides actual App sources. `dependency-root` provides `Core` and `Tools` C headers/sources whose hashes match the producer record. The explicit CPU archive must match the CPU hash in that record; silently changing checkpoints is rejected. Producer artifact locations remain explicit in the supplied manifest. The recipe copies all consumed C link inputs and matching headers into the fresh output directory before building. Apple frameworks and installed sanitizer runtimes remain system dependencies.

`SourceLock.json` deliberately locks reviewed App helper/report/frame/surface files and the exact worker body. Future production changes require a reviewed lock update. The body is extracted from Session using stable markers, not fixed offsets or a private staging path. It retains the exact production serial queue and methods; only test access to the private collector is appended. The production error type is independently extracted and hash checked.

The two fixtures retain their original assertions:

- 65 helper assertions: real Process identity, fresh stop/return-PC and ordinary bootstrap routing, authoritative status, explicit unknown policy, optional report compatibility. Returned-status injection is test-only.
- 8,508 frame assertions: real original GPU readback, exact 31×17 RGBA/CoreGraphics pixels, live and already-destroyed Process modes, main-queue presenter delivery, pre-copy cancellation, actual held-lease retirement refusal, and release retry/stale-token cleanup. Swift backing and actual C/GPU resources return to baseline.

C dependencies and test forwarding shims use ASan/UBSan; Swift uses ASan. The helper also compiles as an iOS 17 module. `validation-manifest.json` records commands, source/dependency/extraction hashes, private link products, and both terminal results.

The surface lifecycle is a pure state fixture, not a UIKit attachment. Presenter delivery/CoreGraphics pixels are not proof of UIKit display, a guest-title frame, outer App scheduler/timer behavior, or device execution. The fixture uses original synthetic guest instructions only.

Test-only C forwarding keeps the production C symbols untouched. The helper rebuilds only dispatcher/composite objects with test wrapper names; frame tests link their unchanged original objects and forward only Swift's imported release call. The two executables are separate.
