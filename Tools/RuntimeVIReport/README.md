# VI report and actual C rejection regression

This standalone host gate compiles the supplied actual `App/SwitchRuntimeVI.swift`,
Bridge.h, three graphics report types and SurfaceState. It extracts the exact report
declaration prefix from `SwitchRuntimeSession.swift` using stable declaration
markers. The real VI, Process, symbols, output guard, CPU and IPC are linked.
Swift uses ASan; C uses ASan/UBSan. No App, GPU or device execution occurs.

`LegacyReport.json` is a sanitized public **actual diagnostic report**, copied
from checked-in `Validation/phone-runtime-progress.json`; it has no VI field.
The modern VI reports and four-NOP stopped CPU fixture are **synthetic original
fixtures**. No game code, assets, keys, private identity addresses or pointers are
read or printed. Direct C nil-owner rejection must preserve a sentinel output;
the actual Swift helper must report returned INVALID, not its initial zero/OK.
Legacy/modern/null Codable paths, counters, safe keys and unknown operations are
also checked. The original43 assertions are retained; six additional checks
cover the actual full report's optional Window field, absence/null decoding,
snapshot/copy counters and stable roundtrip. The current gate passes49 assertions.
The pure Window report type is compiled without linking the Window/GPU backend.

Example (run from repository root, with a validated host CPU archive):

```
python3 Tools/RuntimeVIReport/build.py \
  --source-root "$PWD" --dependency-root "$PWD" \
  --cpu-archive /tmp/satchel-vixl-probe/fcvtzu-combined-staging/build/macos/libSatchelCPUProbe.a \
  --cpu-validation /tmp/satchel-vixl-probe/fcvtzu-combined-staging/validation-manifest.json \
  --output /tmp/satchel-runtime-vi-report-check
```

An App-only overlay can replace `--source-root`; keep `--dependency-root` pointing
to its matching project. Output must be fresh and outside both roots. The recipe
copies inputs only into that output, verifies all originals/snapshots before and
after compilation, records commands and exact hashes, and checks the linked real
C symbols. The optional CPU manifest additionally proves archive/header pairing.
There are no canonical implementation edits or new dependencies. A successful
host report gate does not prove live VI calls, display creation or presentation.
