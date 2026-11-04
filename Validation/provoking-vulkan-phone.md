# FIRST/LAST Vulkan validation on iPhone Duo

Validated on the maintainer's physical iPhone Duo in Release 71 on 2026-10-31. The runtime
UI journey passed one test with zero failures, including all 13 original graphics
checks. This new check is an original offscreen GPU fixture, not a Castlevania
frame, a game-compatible NVN provoking-state contract or screen presentation.

The diagnostic compiles original encoded vertex/fragment sources on the phone
through the existing production captured compiler and live ABI2 host adapter.
Compiler state remains explicitly original FIRST. The copied result is admitted
by the independent typed Vulkan schema and used for two real pipelines: native
FIRST and bounded indexed-LAST. Two triangles carry different per-vertex colors;
the test checks both expected color selections, identical coverage, readback
guards and unchanged guarded vertex inputs.

## Observed phone result

| Field | Actual Release 71 result |
| --- | --- |
| Returned/reported status; stage |0 /0;10 |
| Compiler/backend status |0 /0 |
| Compiled pairs / pipelines |1 /2 |
| Submitted draws / completed fences |2 /2 |
| Pre-submit / pending cancellation checks |2 /2 |
| Colors / coverage / guards / unchanged vertices |All true |
| Native extension / LAST support / LAST enabled |All false |
| Rotated LAST supported |True |
| Private index allocation |16,380 bytes |
| Device finalized / cleanup matched |True /true |
| Retained resources |False |

Device, buffer, job, target, pipeline and private-index counts were all 0 before
and after. Public buffer bytes, target bytes and private-index bytes were also
0 before and after. Native LAST was not enabled and no private Metal API was used.
The tested operation uses immutable cyclic triangle indices within the admitted
pure triangle-list profile; it does not establish arbitrary shader compatibility.
The cancellation checks are controlled original-fixture checkpoints, not a new
claim about actual App background cancellation or display timing.

The new numeric report is optional for decoding older saved reports. Its App
success predicate requires both status enums, version/size, stage 10, every exact
count and guard flag, full resource equality and no retained resources. The
same serial graphics worker performs GPU work and cleanup-only retries.

## Bound artifacts and source

Release App binary SHA256: `b87f7533824d4fdf7583fa1a35d208f63e658031d21f90b4ff06b33cf98bde9d`.
Signed 52-export SatchelVulkan SHA256:
`927296ca40ddc2a38e4af0d3a0953a873a0dc27f8ca211d835973de5bb3fbf0d`.
Diagnostic iOS17 archive SHA256:
`1096d8c0611982b58ac19f91a74cb31f702c36a8f67138820d21be7f52a74901`.
Both existing NativeAOT frameworks are embedded in this same tested App; their
signed hashes and exports are recorded in the artifacts manifest below. The
pre-test artifact record intentionally still says result pending; this document
and the separate completed report/log add later device evidence without rewriting
that immutable build record.

| Evidence/source path | SHA256 |
| --- | --- |
| `/tmp/satchel-runtime-castlevania-71.json` | `c9443ef9ce03a52dd96141b463417286bcfae258d46735de6c7b9daa8c1e9912` |
| `/tmp/satchel-runtime-castlevania-71-artifacts.json` | `8f234dfcb133b3843eb8c106bef1aeb79ad113254ff8e45118b971e78feef595` |
| `/tmp/satchel-runtime-castlevania-71.log` | `410f9c0d063a2751758dbfef398eb1fbcd3bd1cc3f5f82c97136ac77e5fb601a` |
| `/tmp/satchel-vulkan-provoking-staging/canonical-promotion.json` | `8ccfb6523753dd2242a86de1fcaa557ee4301a4caffb6ef7d720cdeba7cb34d7` |
| `/tmp/satchel-vulkan-provoking-staging/promotion-plan.json` | `22dd8a0017f62299bc8c9c71085de84d29dc19f0958b0c2abc546029e3adc3b8` |
| `/tmp/satchel-provoking-diagnostic-staging/canonical-promotion.json` | `0fd9194d32a58ef194991f1535faf3d6134d382d936dd9de273db74316f569bc` |
| `/tmp/satchel-provoking-diagnostic-staging/promotion-plan.json` | `f543f59e1936340b498c31d46e500d675e5191fc7b0a6466fdaa5443ced36112` |
| `/tmp/satchel-provoking-diagnostic-repro-build/validation-manifest.json` | `a51067c7ea4d7e1fa986060c952759ad4b18349ddb1b74666657884cbc64c749` |
| `/tmp/satchel-provoking-diagnostic-independent-review.json` | `dcf533250e9d97d3da865d31fa841dd03bd6f885f91b9cdb9a4e4dfecdb2cdbd` |
| `/tmp/satchel-provoking-diagnostic-repro-review.json` | `e7611c31b184ceb7f4ecf59e7317044f0779db63d1e8f485b04f079d11f715d4` |
| `Tools/GuestGraphics/GuestProvokingDiagnostic.c` | `b33ce78224b6d1727375d9d7df89747ac0c3170700b2630c3018df7d01c30d35` |
| `Tools/GuestGraphics/GuestProvokingDiagnostic.h` | `f117ab7884a60cd8ba01192b75fd894363872d9c2eb582ff904527f74bb0af07` |
| `App/SwitchProvokingVulkanReport.swift` | `de4f38f3e5ba9297d0b3dec56a21eb94f3f4a678b603be6eb3ba69ac4fe7c132` |

The source promotion plans bind every relevant backend/diagnostic implementation,
fixture, recipe, lock and promoted artifact. Host reproduction passed 7,305 new
backend assertions, the full 22-gate raster suite and preserved captured GPU
paths. The separately relocated diagnostic passed 271 normal and 17 fault/retry
assertions plus iOS17 compilation and its sole public export. Counts include
byte/pixel comparisons, not that many independent scenarios. Independent reviews
were read-only; reviewers did not run the device test.

## Scope still incomplete

Release 71's imported Castlevania check reached 67,475,569 instructions and stopped
at SDK+0x126de0. It still reported no guest NVN call, no asset IPC read and no game
frame. Actual NVN provoking enum/default/reset behavior remains unresolved; the
captured program path still refuses missing real state. This renderer capability
does not relax that gate. Guest window/acquire/present, VI/native-window ownership
and App display integration remain separate work. No source changes or builds
were performed to write this device-validation note.
