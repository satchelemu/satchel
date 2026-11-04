# Host runtime diagnostic

This developer tool runs the original XCI through the same bounded CPU, Horizon
process, NSO loader, verified application metadata and read-only RomFS provider
as the phone. It emits safe counts and the stopping code location. It does not
prepare a game build, write decoded files, or establish
phone performance or gameplay compatibility.

The exact canonical NVN stub generator is extracted at build time. The tool
requires one known `nvnBootstrapLoader` definition and installs breakpoints at
that entry and the emulator dispatch trap. Default mode stops there and never dispatches
an NVN request. The separate explicit opt-in below uses checked real backends,
without substituting a graphics result. Known module-name strings are
restricted by the existing loader. There are no raw instruction words, operands,
register dumps, logs, keys, time seeds, decoded modules or assets in the report.

The loader coordinates a read of the original file throughout execution. Key
material is read from the explicitly supplied file into memory; Keychain APIs
are not called. Control metadata is bound to the matching NPDM. The canonical
clock sampler uses actual host time, and the canonical foreground-launch model
is initialized once. Randomness and timing remain real; counts can vary between
runs. Process destruction precedes provider/verified-storage teardown.

The phone's memory and module limits are preserved. Execution is bounded to
100 million instructions and100,000 scheduler continuations. The explicit
host-only time allowance defaults to240seconds, accepts1..300seconds, and is
reported alongside elapsed milliseconds. The sanitizer-enabled Mac run needs
more time than the phone's production archive. This does not change App limits.
The launcher imposes an additional600second process deadline including loading.

## Build and run

Requires the existing Apple Silicon macOS/Xcode toolchain and a validated CPU
archive/checkpoint. No dependency is downloaded. The IPC codec is compiled
directly from canonical source rather than linked from an unverified archive.

```
python3 Tools/HostRuntime/build.py --source-root /absolute/project \
  --cpu-checkpoint /absolute/current-cpu-canonical-promotion.json \
  --output /tmp/satchel-host-runtime-build
python3 Tools/HostRuntime/run.py --build /tmp/satchel-host-runtime-build \
  --keys /absolute/user-keys-file --package /absolute/original.xci \
  --title 0100ba4026e94000 --seconds 240 --report /tmp/new-runtime-report.json
```

The builder verifies the CPU archive against its reviewed checkpoint and binds
all CPU sources/headers to its validation manifest. Historical source/archive
records and current entry-based promotions are both supported. Current records
must also match the validated build artifact and a successful full regression;
the unchanged CPU header and memory guard remain part of the source checks. It hashes consumed
sources, headers and generated stub inputs before compiling and verifies them
again afterward. The launcher checks the executable hash before passing input
paths, disables crash dumps, suppresses private child errors and validates an
explicit typed output schema. Reports are created exclusively, never overwritten.
Use the launcher rather than invoking the binary directly.

`python3 Tools/HostRuntime/test_launcher.py` exercises launch refusal, private
error suppression, strict output admission and no-overwrite behavior using only
original synthetic scripts and reports. It does not run a game.

Device validation remains separate. This tool has no permission to access an
unavailable phone, and successful host execution is not a phone-game result.


## Explicit host NVN dispatch

Build with `--enable-nvn`, then separately launch with `--dispatch-nvn` to enable the checked current raster/compute/transfer dispatcher. Default builds and launches retain stop-before-NVN behavior. The CPU checkpoint remains required; an optional `--cpu-archive` can select an immutable copy matching its reviewed macOS artifact hash. The enabled builder verifies the existing pinned MoltenVK source/distribution/header provenance and renderer/scalar source locks; no dependency is downloaded. Run builds with exclusive ownership of the selected output and dependency artifacts; every consumed input is checked again after compilation. A concurrent dependency mutation invalidates the build.

Example reproduction, with paths selected for this session:

```sh
python3 Tools/HostRuntime/build.py --cpu-checkpoint /path/to/reviewed/canonical-promotion.json --enable-nvn --output /tmp/satchel-host-nvn-build
python3 Tools/HostRuntime/test_launcher.py
python3 Tools/HostRuntime/test_opt_in.py
python3 Tools/HostRuntime/test_glue.py --build /tmp/satchel-host-nvn-build
```

The separate original glue fixture performs real GPU work through the new host seam, never inside the actual game runtime. Its checked-in fixture is an adaptation of the original raster diagnostic; `make_fixture.py --output /tmp/...` can reproduce it for comparison without writing into source.

Lazy dispatcher creation occurs only at the known bootstrap on a fresh stopped-thread CPU. Execution preserves the original absolute deadline and additionally enforces a30-second window after bootstrap in both CPU and dispatch callbacks. Scheduler continuations and dispatch events remain separately capped. A pure callback reads deadline/token state only.

On exit, handoff precedes Process/CPU destruction; bounded five-second CPU-free retirement follows. Pending ownership remains retained and reported, never forced complete. Exceptional handoff/stat-query failure leaves bounded allocated/global ownership until process exit and emits no successful JSON; it retains no CPU/provider/cancel pointer. Real attachment exhaustion and pending-submit cancellation are tested; handoff/stat failures themselves are source-reviewed, not fault-injected. Non-breakpoint HostNVN calls are a caller-precondition refusal with no report update; Swift routes only fresh CPU_STOP/BREAKPOINT results.

The opted report adds only typed status/count/byte fields, not raw names, arguments, tokens, source bytes or pixels. `graphicsImplemented` stays false. The current dispatcher still has restricted shader/state profiles and no window/acquire/present path. This is an original-file host compatibility diagnostic, not per-game preparation, phone validation or a game-frame claim.

## Focused CPU development

For a reviewed CPU family still awaiting full regression, build with
`--development-cpu-checkpoint /absolute/development-checkpoint.json` instead of
`--cpu-checkpoint`, then run with `--allow-development-cpu`. Both opt-ins are
required. This is host development only; it does not promote the CPU or authorize
a device build. Full regression and device validation remain separate milestones.

The development checkpoint has kind `focused-development`, an absolute
`cpu_source_root`, all eleven `Tools/CPUProbe/` source hashes, `host_archive`,
`artifacts` archive hashes, and `source_checkpoint`, `focused`, and `review`
records with absolute `path` and `sha256`. The source checkpoint binds its
`files`; focused evidence binds `exit_code: 0`, positive `checks`,
`source_checkpoint_sha256` and `archive_sha256`. Review binds `decision: clear`
and the same source checkpoint. The public CPU header must match the canonical
Process ABI. Inputs are checked again after compilation.

The launcher clears any inherited development handshake and supplies it only
when both the manifest and launch flag select development mode. The compiled
development executable requires that handshake before opening private inputs,
even if its manifest is relabeled. The executable itself emits `cpuDevelopment: true`; the launcher requires that
marker for development builds and refuses it for full builds. Reports therefore
cannot silently present focused-only results as full-validation results.
`test_development.py` uses synthetic fixtures to check provenance, explicit
opt-in and labeling. Existing launcher and NVN tests retain their assertions.
