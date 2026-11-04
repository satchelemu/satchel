# Texture-to-frame device evidence: stage73

Validated on the maintainer's physical iPhone Duo on2026-10-31 using the Release app and
the unchanged imported Castlevania XCI. The game reached67,505,918 instructions
before an unsupported UCVTF instruction. It has not reached an actual NVN call,
guest asset IPC read or game frame; this report does not establish gameplay.

The separate original diagnostic interprets273 fixture instructions and92
graphics dispatch events. A real GPU ClearBuffer updates a31×17 texture's tiled
pool mirror. Snapshotting reads that current mirror, uploads the decoded bytes,
waits for completion and copies an immutable frame. The CPU mirror remains
unchanged. Pixel guards, expected green pixels, and protected parent resources
all pass. The frame lease remains readable after interpreter destruction.

- One GPU transfer and QueueFinish; two uploads and two snapshot starts.
- One completed immutable snapshot and one separately cancelled/drained snapshot.
- One pre-submit cancellation and one pending cancellation.
- All device, buffer, job, image, pipeline, private index, frame, staging and
  dispatch-retirement resource counts/bytes return to their zero baseline.
- All14 original graphics checks pass; all5 library entries remain byte-identical.

Evidence: `/tmp/satchel-runtime-castlevania-73.json`,
`/tmp/satchel-runtime-castlevania-73.xcresult`,
`/tmp/satchel-runtime-castlevania-73-artifacts.json` and
`/tmp/satchel-texture-frame-app-staging/phone73-validation.json`.
The library SHA256 before/after is
`40904b42256953fcdfd1b2604415b4b5b7fd1b48621230427f6f515c9ea7f5f4`.

Host validation:431 normal and30 injected-failure sanitizer assertions, iOS17
compilation,16 Swift report assertions, and independent source/integration reviews.
The actual App dispatcher now selects the texture-frame composite. Automatic
game-window selection, VI/acquire/present, screen presentation and controls remain
unverified. Explicit fixture format/origin cannot substitute for those contracts.
