# NativeRewriter

Implemented and validated on macOS for TOTK 1.4.3 main, subsdk0 and sdk. Each patched image
and all six output tables match the pinned Python oracle byte for byte. This is a bounded,
synchronous C API over a C++17 implementation, usable from Swift through `module.modulemap`.
Python is used only in development tests and profile export. No Python runtime is linked.

This is a partial port of the complete upstream rewriter. Unsupported families fail with
`NR_UNSUPPORTED`; this library is not yet a general replacement for every upstream game.
Nothing here was installed or run on a phone, or used to play a game.

## Pins and dependency

The reference is the Zelda worker's materialized NX2Apple engine at
`bbc3b73784751d2a706932dc69fd67627e3445d4`, including its published patches. The individual
Python and profile file hashes are in `profiles/source-lock.json`. The differential runner
checks those hashes before using the oracle. The older integration-design revision is not
this task's oracle.

Capstone 5.0.7 is pinned to commit `d2e5739cbb43df93d589a3a726c8f878b95beccb` and a SHA-256
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
nice -n 10 /tmp/satchel-switch-integration/venv/bin/python3 Tools/NativeRewriter/tests/differential.py \
  --engine "$HOME/SatchelDeps/NX2Apple/engine" \
  --library "$HOME/SatchelBuild/rewriter/build/libnative_rewriter.dylib" \
  --output "$HOME/SatchelBuild/rewriter/tests" \
  --private-exefs "$HOME/SatchelBuild/zelda/work/extract/v720896/ExeFS"
nice -n 10 /tmp/satchel-switch-integration/venv/bin/python3 Tools/NativeRewriter/tests/contracts.py \
  --engine "$HOME/SatchelDeps/NX2Apple/engine" \
  --library "$HOME/SatchelBuild/rewriter/build/libnative_rewriter.dylib" \
  --output "$HOME/SatchelBuild/rewriter/contracts"
```

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
- Extended x18 jump recognizers: far/copied/forward table bases, masked predecessor and
  complement forms, zero-extension proofs, far byte tables, and tail-call thunks. A jump
  not covered by the older recognizers fails explicitly.
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
