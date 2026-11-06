# NativeRewriter

Implemented and validated on macOS against the pinned Python oracle, nxnative `patch_nso.py` at
`f8a5618d`: every module of Zelda TOTK 1.4.3 (main, subsdk0, sdk), Super Mario Odyssey 1.0.0 and
1.4.1 (main, subsdk0, sdk) and Castlevania: Belmont's Curse 1.0.0 (main, subsdk0, sdk and its NRO)
matches in the patched image and all six output tables, byte for byte. This is a bounded,
synchronous C API over a C++17 implementation, usable from Swift through `module.modulemap`.
Python is used only in development tests and profile export. No Python runtime is linked.

This is a partial port of the complete upstream rewriter. Unsupported families fail with
`NR_UNSUPPORTED`; this library is not yet a general replacement for every upstream game.
Nothing here was installed or run on a phone, or used to play a game.

## Pins and dependency

The reference is the engine pin's materialized nxnative snapshot at
`f8a5618d5f505a4c0a0bcac7c413872d3a53c009`, including its fork patches
(`~/SatchelBuild/engine-pin/deps/NX2Apple/engine` on the maintainer's Mac). The individual Python
and profile file hashes are in `profiles/source-lock.json`, and `profiles/registry.json` is
exported from the same snapshot by `tests/export_profiles.py`. The differential runner checks
those hashes before using the oracle.

The port followed upstream from `bbc3b737` to `f8a5618d` on 2026-11-05. The `patch_nso.py`
changes between them, and what each one changes in the output:

- Register writes read by what the registers are (`written_w`, `writes_reg`): a write of wN
  counts as a write of xN, x29 and x30 count under their Capstone names fp and lr, and a compare
  no longer counts as a write. Used by the table base walk, the candidate table search, both
  offset table forms and the halfword pad count. Output changes for a function whose table base
  register is compared (now accepted) or written as wN (now refused) between its adrp + add and
  the load.
- A register copy replaces the index instead of adding to it, in the offset table and halfword
  forms: a check of the old value no longer bounds the table. The offset form also refuses an
  index changed between its check and the load (a call counts for x0 to x18), and the masked
  copy check (GLSLC's `and` before the compare) is now ported too. A halfword table whose check
  was on the old value now gets all 65,536 pads.
- Tail calls: after the terminal form, a pointer thunk followed only by zero trap padding, and
  a br x18 with code after it when the frame is provably gone (a 16-byte frame thunk with its
  error blocks after the br, or an ordinary function with sub sp and add sp around one saved
  pair). These were refused before; Prepare now accepts them as upstream does.
- The frame rate target lookup (`fps_target.py`, main only): a global object's int32 field read
  by at least eight 60.0 / target divisions and set to 30 somewhere. When exactly one exists,
  the fixups table gets `FPS_TARGET_PTR` and `FPS_TARGET_FIELD`, which the pinned host's
  `loader.c` reads and `hle_fpslock.c` writes the frame rate target into. It follows GOT slots
  through the module's relative relocations (`loader_census.py got_values`).
- Snowdrop's three-pool page allocator (`snowdrop_pools.py`, main only): its constructor is
  found by structure and eight cuts give its 12 GB reservation the pool sizes of the title's
  profile (`snowdrop_pools` in its `game.tsv`), else 2048, 512 and 1280 MB. The registry carries
  the reviewed sizes by base title ID; Prepare passes them through ABI 2 (`snowdrop_pools_mb`).
- Not ported, and still refused as before: the larger back patch area split (two patch areas),
  Unreal's engine region size set after its store, the other new jump table recognizers, and
  the pool shutdown change (no output effect). The registry adds two Switch 2 SDK node cuts,
  two new game profiles and Sparks of Hope's changed cuts, and drops Kingdom Battle's build cuts.

Capstone 5.0.7 is pinnedCapstone 5.0.7 is pinned to commit `d2e5739cbb43df93d589a3a726c8f878b95beccb` and a SHA-256
archive digest in CMake. The installed oracle reports Capstone 5.0.7. Only ARM64 is compiled.
Capstone is necessary to reproduce its operand spelling, register accesses and rejection
behavior. It is statically linked into the Mac test library. Licenses are retained in
`licenses/`; CommonCrypto and the C++ standard library are Apple platform dependencies.

## Build and test

Run from the isolated worktree. Keep build and test outputs outside any repository. This
example uses zsh on macOS and at most two compile jobs:

```sh
nice -n 10 cmake -S Tools/NativeRewriter -B "$HOME/SatchelBuild/rewriter/build" -DCMAKE_BUILD_TYPE=Release
nice -n 10 cmake --build "$HOME/SatchelBuild/rewriter/build" -j2
nice -n 10 ctest --test-dir "$HOME/SatchelBuild/rewriter/build" --output-on-failure
nice -n 10 swiftc -typecheck -I Tools/NativeRewriter/include Tools/NativeRewriter/tests/swift_api.swift
PY="$HOME/SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration/venv/bin/python3"
ENGINE="$HOME/SatchelBuild/engine-pin/deps/NX2Apple/engine"
PYTHONDONTWRITEBYTECODE=1 nice -n 10 "$PY" -B Tools/NativeRewriter/tests/differential.py \
  --engine "$ENGINE" \
  --library "$HOME/SatchelBuild/rewriter/build/libnative_rewriter.dylib" \
  --output "$HOME/SatchelBuild/rewriter/tests" \
  --private-modules 0100F2C0115B6000=/private/zelda/modules
PYTHONDONTWRITEBYTECODE=1 nice -n 10 "$PY" -B Tools/NativeRewriter/tests/contracts.py \
  --engine "$ENGINE" \
  --library "$HOME/SatchelBuild/rewriter/build/libnative_rewriter.dylib" \
  --output "$HOME/SatchelBuild/rewriter/contracts"
```

Without private inputs the differential run covers the synthetic cases: one per proof added
since `bbc3b737` (table base compares and wN writes, a changed or copied index, the masked
copy, the halfword pad count, the padded, thunk and frame tail calls and a pair store that must
be refused, the frame rate target and Snowdrop's pools with default and reviewed sizes), plus
the original eight. A case marked to be refused passes only when both the oracle and the port
refuse it. `--private-modules TITLE=DIR` adds every module file in a folder (main, subsdkN, sdk,
nroN, as NSO or NRO files), with TITLE the base title ID the oracle selects as `DM_GAME` and
whose reviewed Snowdrop sizes the port receives. Keep those folders and the output outside the
repository; they hold game code.

For an already extracted, verified Capstone source tree, pass
`-DFETCHCONTENT_SOURCE_DIR_CAPSTONE="$HOME/SatchelBuild/rewriter/capstone-src"`.
`NR_BUILD_SHARED=OFF` produces a static library for app integration. No iOS build is claimed.
`NR_SANITIZE=ON` enables address and undefined-behavior instrumentation in the rewriter.

The harness sets `DM_REWRITE_J=1`, captures oracle diagnostics without printing or retaining
byte-bearing errors, and reports only status, artifact names, lengths and difference offsets.
The oracle is actually executed on each input. `--reuse-oracle` is for local debugging only:
it reuses existing files only when input, source-lock, module name and selected profile
match the stored oracle manifest. Acceptance runs should omit this option.

## Calling contract

1. Supply three decoded segments at their NSO memory offsets, BSS size, full 32-byte build ID,
   and module name. The caller owns decoding, decompression, and NSO segment-hash validation.
   The library validates page alignment, ordering, size limits and metadata accesses. It does
   not accept a container file or read paths, keys or environment variables.
2. Resolve the selected build/profile from the reviewed `profiles/registry.json`. Supply CUTS,
   OBS, capabilities and the explicit native recipe identifier. A registered empty CUTS row
   suppresses fallback matching just as upstream does. Otherwise supply selected game rules
   and SDK-wide rules for this module. Keep the two name namespaces distinct when composing
   rules: equal names within one namespace are alternative implementations. Preserve order.
   With `abi_version` 2, set `snowdrop_pools_mb` for main to the base title's row of the
   registry's `snowdrop_pools` (large, 512 KB and 64 KB pools in MB, each above zero, under
   4,096 in all), or leave it zero for upstream's default. Version 1 requests are still
   accepted; they end at `context` and get the default.
3. Each `nr_match_rule` contains one or several `nr_match_function` records. Functions must
   match uniquely and at distinct starts; exactly one same-name implementation must match.
   Missing, ambiguous and original-byte-mismatched function rules apply no cuts, as upstream
   does. Registered CUTS and frame original-byte mismatches are hard errors. Replacement
   instructions in matched rules cannot be PC-relative; overlapping matched cuts fail.
4. Recipe 1 is TOTK 1.4.3 main only and checks build
   `277178b7dba1b6d4949a84778d4abc58`. The native emitter uses current patch/state addresses,
   retains every recipe original-byte assertion and rewrite-site conflict check, and emits
   the nine cut records in oracle order. Other executable recipes must be rejected by the
   coordinator; any unknown nonzero recipe identifier fails inside the library too.
5. Set every limit explicitly. The tested settings are image 512 MiB, patches 64 MiB,
   function scratch 4 MiB and 2,000,000 records. Implementation ceilings are respectively
   1 GiB, 256 MiB, 16 MiB and 8,000,000. The counters bound structural resources, not resident
   memory in bytes. C++ container/Capstone overhead and the caller-owned input are additional.
   Patch/table limits apply individually to each artifact. Aggregate input profile payloads
   are also limited to `max_patch_bytes` before copying, and nested profile records count
   against the record limit. Retained instruction and graph
   records scale with the record limit; temporary detailed decoding is function-bounded.
   Rejecting a limit returns `NR_LIMIT`, never partial output. Large functions currently
   fail earlier at the unported 256 KiB splitting boundary.
6. Keep all pointers, strings and callback context alive until return. Call on a preparation
   queue. Cancellation is checked before work and at bounded iteration checkpoints. The
   callback must not throw. Each invocation owns its state and Capstone handle.
7. On success, copy or consume views returned by `nr_result_get`, then call `nr_result_free`.
   Views remain valid until that free. On any failure the result is null, caller input is
   unchanged, and `nr_error` contains a code, offset and byte-free message. Do not log the
   image, cuts, observations or other byte-bearing artifacts.

The outputs are the binary image and exact UTF-8 `fixups.tsv`, `gates.tsv`, `sites.tsv`,
`cuts.tsv`, `obs.tsv`, and `changed.tsv`. Store them together transactionally before signing.
The runtime needs the same guest-relative layout and gate ABI as NX2Apple. A successful
rewrite does not sign, package, map or execute the result.

## Port coverage and remaining work

Implemented: unwind boundaries and text-data exclusion, x18 register substitution,
plain/SP/conditional/PC-relative/call sites, thread-slot accesses, SVC/timer gates, cache
line constant, stack alignment, acquire-lock strengthening, exclusive windows and retry
copies, byte/halfword jump pads, basic and ADR-relative offset tables, symbol-derived
SDK observations, spin-region path/liveness checks and emission, near and ordinary front
layouts, function fingerprints with callee/import identity, declarative cuts/observations,
capabilities, and TOTK 1.4.3 frame timing and render scale.

Not yet ported:

- Large-text front-size retry, two patch areas, and large-function splitting. These fail
  explicitly instead of attempting an unverified layout or analysis boundary.
- Extended x18 jump recognizers: far/copied/forward table bases, a check before the index copy,
  masked predecessor and complement forms, zero-extension proofs and far byte tables. A jump
  not covered by the ported forms or the tail call proofs fails explicitly.
- Unreal console-variable, Binned3 allocator and engine-region analyzers. Discovery markers
  cause an unsupported error, potentially more conservative than Python's no-match result.
- Non-ASCII imported-symbol fingerprint JSON escaping. Such a matching candidate fails
  explicitly rather than producing a different digest.
- Optional `.match.tsv` explanations and Python-specific `build_src.sha256`. The reviewed
  source lock replaces provenance for this port; it is not an oracle-format output table.
- Other executable frame recipes and TOTK 1.4.2. The registry marks these unsupported.

The current upstream census has six game profile files with executable definitions:

| Profile | Logic and work remaining |
| --- | --- |
| TOTK `0100F2C0115B6000` | Shared frame emitter and build-specific sites. Only 1.4.3 is implemented and privately validated. 1.4.2 needs its own native recipe selection and parity input. |
| Age of Calamity `01002B00111A2000` | Frame/update pacing emitter. Port its recipe and original-byte checks; validate its module privately. |
| Paper Mario TTYD `0100ECD018EBE000` | Frame-step emitter. Needs native relocation/emission and private parity. |
| Breath of the Wild `01007EF00011E000` | Frame-step/render-scale emitter. Needs native recipe and private parity. |
| ASTRAL CHAIN `01007300020FA000` | Frame-step emitter plus signed/width/relocate helpers and nested destination mapping. Needs reviewed relocation rules and private parity. |
| Smash Ultimate `01006A800016E000` | `_function` constructs declarative matching records. Its resolved data is exported; broader rewriting of its modules has not been validated. |

SDK `mov`, `node`, and `align` helpers also construct data at development time. Their resolved
records are exported. The export script loads trusted pinned upstream code only, never user
profiles. Runtime profile loading must accept declarative records only.

The next blocker to complete oracle coverage is implementing and differentially testing the
remaining generic recognizers/layout paths. Phone integration remains a separate task.
