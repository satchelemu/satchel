# Native shader preparation

Satchel M5's full Zelda native parity passed: 28,669 records, 71,621 exact MSL
sources and 28,669 exact semantic reflection files after removing offline
compiler provenance. General translation, discovery, runtime compilation and
cache APIs are described in `API.md`; their core iOS linking has passed.
The any-game worker owns app and host integration. Read
`~/SatchelBuild/m5/report.md` for measured results and the current gate.

The native path is `ShaderRecord.normalize`, `NativeShaderTranslation.prepare`,
the NativeAOT `satchel_shader_translate_v1` facade, the pinned C++
`satchel_game_spirv_to_msl` bridge, and `ShaderSourcePipeline.prepare`. It accepts
immutable shader control/code and query facts. It produces MSL sources and NVN
semantic reflection without a subprocess, filesystem dependency, JIT or Metal
compiler call inside the translation library. File access in the harnesses is
only a development adapter.

The Swift coordinator currently accepts vertex, fragment and compute shaders.
It fails explicitly for geometry/tessellation preparation. The NativeAOT
facade preserves the pinned geometry accessor and reader contract, but this
does not yet implement the entire generic geometry source family. No unsupported
stage is silently omitted from a package.

Facts, stage pairs and mapping/dual-source requests are inputs. They must come
from authenticated discovery, reviewed profile data or captured runtime state.
The Python oracle is solely a development reference and does not belong in an
iOS package. `oracle.py` reads selected pinned functions without executing the
Mac builder's compiler, cache or deletion orchestration.

## Validation paths

Use `NativeShaders/guard.py` for heavy builds. It preserves the 35 GiB reserve,
waits for external compiler drivers, runs at nice level 10 and sets two build
workers. All outputs go under `~/SatchelBuild/m5`.

The guard checks owned descendant RSS every five seconds, stops at 9 GiB to
leave headroom below the 10 GiB maximum. It pauses when swap grows more than
1 GiB above the saved startup baseline or memory pressure reports less than
10 percent free. Existing swap alone does not block work. It records sampled peaks per PID under `~/SatchelBuild/m5/limits/`.
The light mode skips only the external compiler wait, keeping all memory,
swap and disk checks. Both Swift source harnesses drain an autorelease pool
after every request. The census restarts its child every 500 shaders, with a
maximum configurable batch size of 2,000. Checkpoints every 100 shaders and
at batch boundaries permit bounded rework after an interruption. Resume
checks the executable, comparator and ordered input inventory identities.

```zsh
python3 NativeShaders/guard.py python3 NativeShaders/build-cross.py
python3 NativeShaders/guard.py python3 NativeShaders/build-cross.py iphoneos
python3 NativeShaders/guard.py python3 NativeShaders/build-translation.py
python3 NativeShaders/guard.py --light python3 NativeShaders/check-pipeline.py --label pipeline-full
```

The last command is the full in-process test, starting from original Maxwell
records. It checks all final source variants, shader inventory and semantic
reflection. It must pass before integrating into Prepare. Each output label
must be fresh; completed evidence is never overwritten.

`check-translator.py` compares the full native C# front end against the pinned
Mac executable. `check-records.py` compares native record normalization against
`job_for`. `check-variants.py` checks every retained side source and its metadata.
`check-pipeline.py --source-stages` uses the pinned SPIRV-Cross CLI as an
intermediate fixture, then executes the same Swift source pipeline. That mode
can isolate source transforms and buffer analysis while the C++ build is queued;
it is explicitly not the full in-process proof. `check-spirv.py` covers original
synthetic subgroup-lowering cases.

The normalizer checks lengths, control magic, code size, c1 bounds and the
canonical control-plus-code SHA-256 prefix. Full hashes should also be retained
by the discovery/cache coordinator to detect truncated-key collisions. The
facade's result is caller-owned and must be released through its matching free
function. Managed translator messages stay in private reflection, never logs.
The fixed diagnostic statuses avoid exposing shader instructions or MSL.

## Parity and provenance

The bridge uses the exact SPIRV-Cross revision recorded in `build-cross.py`.
The pinned Mac CLI's default MSL platform is intentional, including when the
result will be compiled on iOS. Setting `--msl-ios` would change the source ABI.
Image atomics select MSL 3.1; other shaders retain 2.3 output.

The source coordinator produces the five side families from final base text.
For tile fetch, this matches the proven recovered Mac corpus, including the
trailing newline preserved when integer entry points are removed. It also
preserves reflection field ordering and Python's one-space JSON indentation.

Offline `chain_metal` and `chain_compiler` values are never stamped onto native
outputs. The comparison removes exactly those provenance fields from the
reference before comparing remaining bytes. Actual runtime compilation needs
its own source/options/device/OS identity, persistent source and reflection,
and a cache invalidation/rebuild path. Apple's system cache can be evicted;
there is no public promise that a source-created executable MTLLibrary is
compiled once forever.

MIT notices are in `Notices/` and the compiler tool's `licenses/` directory.
SPIRV-Cross notices are retained under `Tools/ShaderBridge` and copied alongside
its task-local build. The archive decoder adds zstd 1.5.7 under its BSD license,
retained in `Notices/Zstandard.BSD.txt`; Apple's public Compression API has no
zstd decoder. All dependency source checkouts remain outside the repository.
