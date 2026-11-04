# Original FIRST/LAST GPU diagnostic

`satchel_guest_provoking_diagnostic` is a small reusable iOS C diagnostic. It is
independent of the previous diagnostics, and adds no Vulkan framework export.
The static archive contains only this diagnostic; it does not duplicate owners,
CPU code or the compiler/renderer implementation.

The actual production captured compiler receives original public-encoding VS/FS
source bytes and explicit original FIRST raster state. The production ABI2 adapter
resolves live device/target facts. Its returned copied SPIR-V passes independent
typed schema validation, then creates a FIRST pipeline and an indexed-LAST
pipeline. Two original triangles have different per-vertex colors. Two actual
GPU draws and fences verify the corresponding first/last colors, identical
coverage, guarded readback and unchanged guarded vertex bytes. This exercises the
new backend transform without relaxing compiler state, fabricating an NVN enum or
claiming a captured game frame. No window or display operation is involved.

The numeric report's success contract is documented in the header. Status is an
enum, not a boolean. Success requires status0/stage10 and every expected count,
result/guard flag and complete resource equality. Snapshot accounting includes
public device/buffer/job/target/pipeline registries and the separate private index
buffer count/bytes. Tests retain unrelated real resources during a run to verify
that cleanup matches a nonzero baseline instead of requiring global emptiness.
BUSY is never success, even if a second cleanup attempt drained all resources.

Run on the same serial graphics worker as all other GPU operations and retirement.
Cancellation reads only caller cancellation/deadline state. It and its borrowed
context are never retained. The ten-second cooperative deadline latches initial
clock failure immediately; compiler and individual fence waits are separately
bounded to one and2.5seconds. Driver creation calls cannot be forcibly interrupted.
One static CPU-free owner preserves every nonzero job and its resource tokens on
error, cancellation or timeout. A later invocation polls and reaps it before new
work. An always-cancel call performs cleanup only. Unrecoverable backend failure
may keep this bounded owner until process exit; it is never silently abandoned.

## Reproduce

Use a validated52-export renderer build, unchanged captured compiler build and
matching ABI2 adapter build. The recipe verifies their exact manifests and source
locks. Example (paths are configurable):

```sh
zsh scripts/test_guest_provoking_diagnostic.sh \
  --compiler-build /tmp/satchel-guest-captured-compiler-final \
  --renderer-build /tmp/satchel-provoking-flat-build \
  --adapter-build /tmp/satchel-provoking-host-facts-build \
  --ios --out /tmp/satchel-provoking-diagnostic-check
```

`--source-root` and `--renderer-source-root` permit separate complete source roots
for relocated overlay validation. Outputs must be fresh task directories under
`/tmp`. No source, canonical artifact, App or physical device is modified. The
original `GPUOriginalPair.h` fixture is taken from the checked repository compiler
source, never a hidden scratch file. All implementation/fixture/build dependency
hashes and archive exports are recorded. iOS uses arm64/iOS17 and exactly one
public export. No CPU, allocation-failure or synthetic clock hooks are linked into
the production archive. Tests compile a separate fault variant to prove pending
fence/index ownership, cleanup retry and the initial clock-failure boundary.
Assertions include bytes and callback checkpoints, not independent scenarios.
The runtime caller must still test this new diagnostic on the physical phone.

Initial gates01/02 failed because the test polled a real GPU fence100times without
allowing wall time to pass. The test now waits1ms between bounded pending polls;
its required cleanup assertion remains. No production change or fake completion
was introduced to pass this gate. The normal real fence path is unchanged.
