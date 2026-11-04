# Runtime shader capture

This is the generic capture route for Satchel's importer. It takes NVN programs
from the running title, independent of its archive format or game engine. It is
an adapter for the pinned NX2Apple engine, not a second shader translator.

## Host hook

Apply `runtime-capture.patch` to a private engine checkout and copy
`RuntimeCapture.inc` into `dm/gfx/nvn/`, then rebuild that host with its title's
profile. Keep game captures and generated tables outside every source checkout.
The patch does not change the shader chain, title registry or shared engine.

The existing `DM_NVN_SHDUMP` path copies each control section at
`nvnProgramSetShaders`, while its lifetime is valid. The code area remains in the
program's memory pool. Previously the replay layer wrote only keys missing from
its shader map. The adapter also records every stage of submitted program binds,
including stages already in the map. Files are `<key>.ctl` and `<key>.code`.
Short control sections are copied to a zero-padded host allocation without reading
past their declared size. The converter hashes only the declared key range,
using the profile's `shader_key_skip` rule exactly as the engine does.

`bound-programs.tsv` records the first program serial and NVN stage for each
unique captured key. It is an identity inventory, not a command replay trace.
`draw-pairs.tsv` records unique effective vertex/pixel keys at draws, before a
missing library can cause the draw to be dropped. Separate stage bindings are
supported. No pairs are guessed from nearby bind calls or from a cross product.
An empty pair table is valid evidence for a run with no vertex/pixel draws.
Capture hooks run on the submit thread. The upstream debug path retains copied
controls until process exit, so sessions are bounded.

## Tools

Run commands from the games worktree. All paths below refer to private build
artifacts, never checked-in game assets.

```zsh
python3 NativeGames/capture_shaders.py CAPTURE_DIR NEW_INPUT_DIR \
  --engine ENGINE --profiles PRIVATE_PROFILE_ROOT --title TITLE_ID --require-pairs
python3 NativeGames/compile_capture.py \
  --inputs NEW_INPUT_DIR --output PRIVATE_SHADER_OUTPUT \
  --engine ENGINE --profiles PRIVATE_PROFILE_ROOT \
  --tools TOOL_ROOT --toolchain-stamp TOOLCHAIN_JSON --target mac
```

`capture_shaders.py` also accepts legacy missing-only captures without
`--require-pairs`. Its output includes `pkg/` records, `tables/shader_map.tsv`,
`shader_facts.tsv`, `shader_pairs.tsv`, `pso_pairs.tsv`, `pso_manifest.tsv` and
`capture-index.json`. The record format and key implementation come from the
pinned offline splitter. Inputs are rejected on a digest, length, stage or pair
mismatch. The summary reports observed coverage only.

`compile_capture.py` copies those tables into the selected private profile,
checks all tools against `dmsh_chain.lock`, then invokes `own_compilers.py` with
`J=2`, `nice -n 10` and the pinned Metal toolchain. Its default target is macOS;
`--target ios` or `--target mac,ios` also produces iPhoneOS libraries. The normal
chain stamps, reflection, c1 guard and constant-buffer extent tables are retained.
A zero process exit is insufficient: every captured key must have an `ok` build
record. The wrapper filters compiler source context from its diagnostic log.

`run_capture.py` starts an already-built host with capture, screenshot and control
paths. It accepts `--input-script` using the engine's `DM_KEYS` syntax, such as
`8:A,12:A,25:LRight~2`. It stops after `--seconds`, default 60, and writes a JSON
summary plus filtered host diagnostics. It supports 1 to 600 seconds per run.

`capture_loop.py` combines these steps. Give it `--host`, `--patched`, `--profile`
(the exported runtime TSV), `--romfs`, `--profiles` (the profile root), `--output`,
`--engine`, `--tools`, `--toolchain-stamp` and `--title`. It runs, normalizes,
compiles, then reruns until the observed map and pairs stop changing or a bounded
round limit is reached. It never calls stable coverage a rendered boot. Inspect
the screenshots before choosing gameplay inputs. It leaves every iteration's
inputs and logs for diagnosis. Restarting it appends another timestamped sequence
and reuses cumulative captures and the shader compile cache.

The host can also accept input and screenshots through the runner's
`run/ctl.txt`: append `A~0.2` for a button or `shot /absolute/private/path.png` for
a screenshot. These are Mac host files. These tools perform no phone operations.

Heavy compilation waits for other workers. The tools require 36 GiB free before
starting and stop at 35.5 GiB during shader compilation or a host run, preserving
the requested 35 GiB reserve. Coordinate waiting drivers as well as active
compiler processes, since process polling alone is not an atomic shared lock.

## Coverage and phone work

The same binding hook, hash calculation and record format can run in the iOS
host. The adapter already uses Foundation and the engine's NVN types. A phone
implementation can write the records and tables directly in C or Swift; Python
is orchestration for the Mac experiment, not a runtime requirement.

Translation, runtime query answers, on-phone Metal compilation and persistence
belong to M5. This change does not implement them. The converter obtains compute
threadgroup and shared-memory facts from captured controls, and merges existing
manual facts for captured keys. Other translator queries still use the pinned
chain's defaults. Missing texture or vertex-format facts must be diagnosed from
translated output and host rendering; a capture alone does not answer them.

Only executed paths are captured. New menus, levels, materials and effects can
introduce more programs or pairs. Captured coverage and a visually correct title
screen do not establish whole-game compatibility. Phone packaging still needs a
reviewed profile and support for that title's complete module inventory.

## Validation

`test_capture.py` uses synthetic controls and code, including all six stages,
short controls and masked keys. It exercises the pinned compiler's record reader
without starting a shader build. `test_capture_adapter.m` runs the actual adapter
with synthetic NVN objects, including separate bindings, repeated binds, a null
fragment stage and multiple vertex entries. Its output is then accepted by the
same converter. These tests contain no game-derived shader bytes.
