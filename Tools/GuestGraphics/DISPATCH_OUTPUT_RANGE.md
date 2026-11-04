# Checked output ranges for other emulated services

`satchel_guest_dispatch_check_output_range` lets a serialized caller validate output
from another HLE owner against graphics resources owned by the same stopped
process. The motivating consumer is the high-level VI adapter, whose pointer
outputs must not overwrite device, memory-pool, texture, program, queue or command
objects. This query alone does not attach VI or enable game graphics.

The complete span must be writable. Live and provisional device records are
checked for physical overlap, including aliases. If a pool/composite service is
attached, its checked unowned-range method must also accept the span. Legacy
services lacking physical-range validation refuse. Unknown backing refuses.
Transient builders have no persistent registry ownership and are not attested.

The query uses the dispatch owner try-lock and rejects active/reentrant handlers,
retirement, and retiring contexts. It sets the existing active-handler guard
while invoking the pure checked service callback. It does not write guest memory,
change registers/PC, consume dispatch events, allocate resources or retain a CPU.
Caller must keep the dispatch and CPU alive and serialize this query with the
following output commit on the same process. This is an instantaneous check,
not a lease or stale-pointer validator. Frame-owner APIs also require the same
serial graphics worker; this mutex does not replace their lifetime contract.

Reproduce the focused original fixtures and unchanged dispatcher regression:

```
python3 Tools/GuestGraphics/build_dispatch_range.py \
  --cpu /absolute/path/to/frozen/libSatchelCPUProbe.a \
  --cpu-sha256 FULL_SHA256 \
  --output /tmp/fresh-dispatch-range-output
```

The recipe checks the explicit CPU hash, binds its own source and the full quoted
header closure before/after the build, runs ASan/UBSan fixtures, compiles iOS17
and verifies the exact19 production exports. Output must be new and outside the
source tree. No dependency is downloaded. The fixtures use a labeled synthetic
backend and checked-service implementation; their pass is not a real GPU result.

The focused fixture covers valid and invalid writable spans, live and retained
provisional device objects, writable physical aliases, checked-service refusal,
legacy-service refusal, reentrant and concurrent calls, active CPU rejection,
register/PC/byte/exclusive-reservation preservation, finalized objects and
CPU-free retirement refusal. The provisional case deliberately fails creation
validation and backend cleanup, then verifies protection until actual teardown.

The first two scratch build attempts failed for incomplete include paths before
executing tests. A subsequent source-root recipe resolves all consumed headers.
A command with an incomplete CPU digest correctly refused before producing any
build. None of these attempts is counted as a passing behavior check.

Real texture-frame composite coverage is tracked separately; the existing
composites already negate zero-success texture status values correctly. A
review suspicion of inversion was retracted after comparison with immutable
promotion hashes; no composite bug fix was made.
