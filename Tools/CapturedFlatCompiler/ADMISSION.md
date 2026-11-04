# Source-only admission contract

The additive `satchel_captured_flat_admit` C entry reports requirements for an
immutable copied pair admitted by the SAME StrictSource decoder as v1/v2. It does
not create SPIR-V, query a target, or claim NVN/game shader compilation.

Source-unused host mask0x06 preserves the two existing excluded driver-bug paths.
Source-unused state mask0x08 (bit3) proves YNegate cannot affect this source: the
pinned translator's sole translation read is fragment PositionY IPA at
`Instructions/InstEmitAttribute.cs:186`, while this profile admits only flat user
varying IPA offsets0x80..0xff and refuses all system attributes/resources. Remaining
YNegate references store or expose the field. The proof is not an observed false
value. V1/v2 still require known255 and extra_known4095, including actual provoking
mode; this new entry changes neither ABI nor state admission.

The entry has a tiny unmanaged cold-call boundary, non-inlined managed worker,
shared non-reentrant gate, copied sources before callbacks, bounded decoder work
and monotonic deadline. Complete output is preserved on errors. Sources are
SPH80 plus at most1024code bytes/stage, at most64semantic operations, and one full
flat varying. The current owner uses work65536/time1second.

See `../GuestGraphics/CAPTURED_PROGRAMS.md` for full guest wrapper/control/C1 and
pool generation/lifetime contracts. Current guest known247 and unknown provoking
remain explicit blockers; there is no dispatcher opt-in or actual game evidence.
Independent source review is in admission-review.json; licenses/source pins remain
unchanged. All test shader/guest data is independently encoded original material.
