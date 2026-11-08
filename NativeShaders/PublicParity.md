# Public shader parity

The public gate checks the implementation that will be staged in the app. It needs
no game, key, shader capture, private result, signing identity or network access.
It is a regression and provenance check, not proof that every Switch shader is correct.

`public_corpus.py` generates 29 original Maxwell requests. Vertex and fragment
programs follow the public bitfield fixtures in `Tools/CapturedFlatCompiler/Test.c`;
compute programs follow the original EXIT fixture in `Tools/MaxwellShaderCompiler/fixture.c`.
The requests cover four varying locations, three component rotations, integer vertex
inputs and fragment outputs, vertex output padding, dual and mapping-only fragment
request flags, early fragment tests, and three compute workgroup/shared-memory settings.
A request flag does not imply that its optional variant is emitted. These compute
programs contain EXIT only and do not exercise memory or arithmetic instructions.
Six tessellation requests (2026-11-07) are assembled from the pinned translator's
instruction table: two hull programs (three and four output control points) that pass
position and location 0 through and write outer, inner and per-patch values, and one
domain program that interpolates control point positions with FADD and FFMA and reads
a control point, the patch and the levels, under four mode facts (triangles with
equal and fractional even spacing, quads with fractional odd spacing clockwise, and
isolines). `check-tessellation.py` draws them on the Mac's GPU.
The source lock stores hashes of the generated requests and their expected outputs. No fixture
was extracted from a game. Existing third-party notices remain unchanged.

The Mac harness calls the same NativeAOT, SPIRV-Cross and Swift translation functions
as the app. It hashes raw SPIR-V, canonical resource JSON, geometry-reader output (empty for these
cases), every emitted MSL variant and serialized reflection. Only hashes are logged.
It also requires the NativeAOT runtime to report dynamic code disabled. The locked
source inventory includes the app's Swift integration, C/C++ bridges, C# facade,
project and upstream source locks, public harness, generator and build recipes.

Each native build recipe writes a receipt after its existing pinned-source checks
and successful build. The receipt binds the local source hashes to the actual output
hashes. Public validation verifies the Mac and iOS receipts before and after running
the corpus, then writes a proof tied to the source lock, all outputs and receipts.
Shader staging rechecks that proof against the files it will copy. A different binary,
source, request, output inventory or checkpoint requires validation again. These are
local build provenance records, not signatures defending against a malicious builder
who can edit both the code and its evidence. Builds must run without concurrent edits
to their inputs.

## Running the check

The bootstrap runs the check after rebuilding dependencies. To rerun it on an existing
root that was built with the receipt-enabled recipes:

```sh
SATCHEL_SHADER_DEPS="$HOME/SatchelBuild/anygame/deps-parity/m5" \
  nice -n 10 python3 -B NativeShaders/public_parity.py \
  --output "$HOME/SatchelBuild/anygame/parity-check"
```

Run through the repository resource guard on a shared build machine. The compiler uses
two jobs. The output folder holds a freshly compiled Mac harness; the proof is written
to `<products>/public-parity/result.json`. The check never writes to the tracked lock.
A missing or changed receipt requires rebuilding the affected component, not editing
the receipt. Older dependency roots can still use the explicit private mode below.
Without `--products` or `SATCHEL_SHADER_DEPS`, the products are `<SATCHEL_DEPS_ROOT>/m5`
(`scripts/dependency_paths.py`).

A full run writes an incomplete `result.json` before it starts, so a failed rerun can
never leave an older success for staging. On a root several workers share, that also
fails their staging until the run finishes. To check the existing proof instead, run
the read-only mode:

```sh
SATCHEL_SHADER_DEPS="$HOME/SatchelBuild/anygame/deps-parity/m5" \
  python3 -B NativeShaders/public_parity.py --validate
```

It performs the same checks staging does: the tracked lock against the current source
inventory and corpus, every Mac and iOS product against its receipt, and the proof
against the lock hash, those products and the locked outputs. It builds no harness,
needs no `--output`, writes nothing (no file under `<products>` changes), and exits 1
with the reason when the proof is stale or incomplete. It cannot be combined with
`--candidate`. A change to a lock-covered source (including this script) makes every
existing proof stale until one full run on that root.

## Reviewing a translator change

`--candidate` writes `candidate.json` to the chosen output directory and never writes
a passing proof. It is intended for a reviewer comparing a known translator with the
proposed implementation. Review the source diff, fixture provenance, output hashes and
coverage before replacing `public-parity-lock.json`. Then run the ordinary check using
newly built products. Updating the baseline alone does not verify those products.
Do not accept changed hashes merely to make a build pass.

## Private maintainer gate

The private real-game comparison remains in `check-pipeline.py`. Use
`SATCHEL_SHADER_PRIVATE_PROOF=/path/to/pipeline-full/result.json` to require its
`complete: true` and zero differences in addition to public validation. An explicit
`SATCHEL_SHADER_PARITY=private` retains the old maintainer-only build route, defaulting
to `<products>/pipeline-full/result.json`. The public default never falls back to it.
`check-pipeline.py --label pipeline-full` writes that result to the caller's shader work
folder (`<work>/pipeline-full/result.json`, see [README.md](README.md#validation-paths)), not
into the products: name it with `SATCHEL_SHADER_PRIVATE_PROOF`, or run the check with
`SATCHEL_SHADER_WORK=<products>`, as the dependency rebuild does.
Neither route creates or copies private evidence into the repository. The staged
source manifest records which gate was used.

Only the private corpus run establishes equality to the retained real-game MSL and
reflection corpus, across the variants present in that corpus. Its legacy flag check
does not independently bind an old result to newly built binaries; maintainers must
refresh that corpus result for the translator they are releasing. The public corpus
cannot establish that coverage, geometry or tessellation correctness, rendered pixel
parity, phone Metal compilation, performance or gameplay. Even the private result
alone does not prove current phone execution or bit-for-bit builds across toolchains.
The public gate establishes the reviewed source identity, receipt-bound local binaries
and exact output agreement for its finite public corpus. Both gates can be required
for a maintainer release.
