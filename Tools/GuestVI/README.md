# Checked high-level VI identities

Implemented and host-validated: a bounded Display→Layer→NativeWindow identity owner and exact-symbol interpreter adapter for the five public operations evidenced by pinned NX2Apple. This is not a VI Binder implementation, a native window, a displayed game frame, or proof that Castlevania has invoked these methods. Main’s matching imports were separately established through the approved metadata-only scan; no original functions/instructions/layouts were inspected here.

One live Display and Layer, stable NativeWindow per Layer,256 never-reused16-byte identities per reserved4KiB range,32 future Window references,4 simultaneous owners. No retail object bytes, host pointers, dimensions, crop, display refresh or defaults are fabricated. Duplicate Open/Create is an explicit bounded-profile BUSY stop, not an invented nn::Result. Sized/settings CreateLayer entries stop UNSUPPORTED. Close/Destroy are actual owned teardown and preserve X0; three successful Result methods atomically write the identity and set W0=0.

The trusted Process constructor creates a dedicated RW backing, reprotects it NONE before publication, permanently excludes the range from guest mapping/permission/alias changes, and allows only one owner claim for the entire Process lifetime. This is Process-enforced permanence, not CPU-intrinsic immutable provenance. Borrowed per-call validation checks same stopped Process CPU, exact reservation, actual NONE permissions and no alias exposure. Privileged host changes are detected/refused. Owner creation allocates/preflights before claim; no fallible operation follows claim before publication. Owner destruction does not reset the claimed namespace.

DestroyLayer refuses while future Window refs exist; CloseDisplay refuses while Layer exists. Detach stops new open/create/native lookup/retains, allows supported cleanup, and does not silently attach a replacement surface. Retirement rejects all dispatch and preserves owner until refs drain. Release/destroy after whole Process/CPU teardown is tested; no CPU/guard/cancellation/provider context is retained.

The adapter resolves exact names via existing GuestSymbols, requires three core definitions, and accepts optional close/destroy/unsupported-overload definitions only if individually valid. Ambiguous definitions, malformed metadata and cross-operation address aliases reject the entire entry set. The caller merges its≤7 entries with existing breakpoint owners. It executes no original body and patches no game code. Function interception is before the exact guest PC; LR and protected output ranges are checked before atomic guest output and fixed metadata commit. Failed calls preserve semantic state and guest registers/PC/bytes. The lifetime event cap counts refused attempts too; timeout is per transaction, so minutes of idle lifetime do not expire valid identities. Guard/cancel callbacks must be pure, nonmutating, nonblocking and borrowed.

## Validation and reproduction

- The normal/fault stages of `scripts/test_guest_vi.sh`:68,705 normal and70,292 fault assertions under ASan+UBSan (counts include register/byte guards), plus arm64 iOS17 compilation. Tests use independently encoded BLR/RET entry data, synthetic dynamic symbols and checked original RAM. Covers all five operations, unsupported overloads,256 identity exhaustion,32 reference limit, owner limit, stale/duplicate/wrong-parent IDs, natural alignment, reservation/physical alias guards, cancellation/reentry, one-way claim, readonly/alias mutation, event limit and exact output/register preservation.
- Test-only fault objects inject constructor allocation failure before claim, writev allocation failure after provisional LR, and early/last argument-read failure before owner entry. Deterministic test clocks prove idle gaps of hours succeed while within-transaction expiration and initial/final clock errors refuse unchanged state. These hooks are absent from production objects/iOS archive.
- The joint stage of the same recipe:142 focused original ASan+UBSan assertions against the real Process reservation/claim sources rebuilt by this recipe. Includes interpreted identity chain, actual NONE/privileged alias validator refusal/restoration, namespace burn after owner destruction and reference release after complete Process destruction. Does not rerun or claim the package agent’s broader suites.
- `<output>/ios/libSatchelGuestVI.a` contains only GuestVI.o and GuestVIDispatch.o;11 exact public exports, no test seams or duplicate CPU/GuestSymbols objects. Link existing checked CPU and GuestSymbols archives. Host sanitizer archive also has the compiler-generated `____asan_globals_registered` metadata symbol; that is explicitly checked, not ignored with a broad export filter.
- Private frozen CPU, GuestSymbols source/header, test/build inputs and all produced artifacts are bound in the manifests. Current canonical builds may advance independently. No shared build, canonical source, App/project or device changes were made by this checkpoint.

Root’s review caught and corrected a first-call lifetime deadline that would reject later legitimate VI operations after idle time. The old gate logs are retained as superseded evidence, not the final semantics. One packaging check initially rejected the sanitizer’s common registration symbol; the exact host expected set now includes that single instrumentation symbol, while iOS remains exactly11 exports. No assertion was removed to hide a behavior failure.

## Remaining integration

Root must attach a real App surface epoch, provide the concrete Process reservation/claim and protected-range adapters, merge exact entry breakpoints, and observe genuine guest calls. The standalone tests intentionally use a labeled original epoch profile, not a fabricated real surface. Missing initialization/alternate SDK API consumers remain unsupported. Any guest dereference of the protected emulated identity faults; the component does not pretend to provide retail object fields. Future NVN Window selection/acquire/present and shader-state support remain separate required owners.

## Rebuild from repository sources

The script requires macOS arm64, installed Apple clang/Xcode and Python 3. It adds no dependencies. All inputs are checked before compilation and checked again afterward. Output must be a fresh directory outside the source and dependency trees.

```sh
zsh scripts/test_guest_vi.sh --ios \
  --dependency-root "$PWD" \
  --cpu-root /path/to/frozen-cpu-bundle \
  --ipc-archive /path/to/libSatchelHorizonIPC.a \
  --output /tmp/satchel-guest-vi-validation
```

The CPU bundle contains exactly the consumed `CPUProbe.cpp`, `CPUProbe.h` and sanitizer-instrumented `libSatchelCPUProbe.a`; expected bytes are in `dependencies.json`. This checkpoint uses the reviewed DUP/SHRN CPU archive (e00bf99f…), not a claim that a later live canonical archive is identical. The IPC archive is likewise explicit and locked. To validate another CPU checkpoint, review and refresh its three hashes together and rerun this affected gate; never silently substitute the archive alone. `--source-root` selects a relocated repository tree containing this package/script. `--dependency-root` supplies the locked baseline Process/GuestSymbols source/header tree. There are no implicit scratch implementation paths.

The five Process C files are rebuilt with production guards, not test macros. The joint test links those objects with the same CPU used by the normal/fault tests and the locked IPC archive. The production VI archive contains only the two VI objects; Process, CPU, IPC and GuestSymbols are not bundled into it. Link the existing CPU and GuestSymbols archive when adding VI to the App. The App's Process already supplies its own reservation implementation.

`--verify-only` performs input/output-root checks without creating output or compiling. `validation-manifest.json` records exact input paths/hashes, toolchain, commands, counts/logs, archive hashes and exports. Test-only clock/allocation/write/register fault substitutions are confined to the separately linked fault executable. Each suite must report the preserved 68,705 / 70,292 / 142 assertion count; all include register/byte comparisons.

The reference licenses are preserved here. `checkpoint.json` and `independent-review.json` bind the original reviewed implementation; the repository recipe changes no production/test bytes. Source-lock refreshes must preserve that distinction.
