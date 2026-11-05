# Dependency input pins

`inputs.lock.json` separates three kinds of evidence:

- `inputs`: 19 source and package inputs exercised by the local bootstrap.
- `tool_sources`: five upstream source archive URLs and expected SHA-256 hashes
  transcribed from the installed tool formula metadata. Each provenance record hashes
  that metadata file too. These archives are absent locally, so their bytes and
  standalone build recipes have not been verified in this round.
- `gaps` and `tool_recipe_gaps`: two missing distribution hashes and five unvalidated
  source recipes. A pin does not establish that a tool can be built on a clean Mac.

All URLs for a future clean Mac fetch are in the lock. Explicit fetch-and-stage mode
includes tool sources and verifies every archive before promotion. Ordinary local
validation uses the installed tools, stages only the exercised inputs and never
fetches anything. Both paths retain the incomplete result until the closure is proven.

The five new source pins are CMake 4.4.4, XcodeGen 2.45.3, Python 3.14.4 and
SPIRV-Tools/SPIRV-Headers at Vulkan SDK 1.4.363.0, matching local installed tool
metadata. These are upstream source URLs, not Homebrew bottle URLs. The standalone
validator sources replace the previously unresolved older commit candidates; the
existing compiler recipes still run every SPIR-V validation gate with the installed
validator. No validator acceptance rule changed.

XcodeGen still needs its Swift package closure pinned. Python needs its native
library closure and a tested isolated interpreter. CMake needs a tested source
bootstrap with Xcode and bundled libraries. The validator needs its pinned matching
headers and a tested CMake build. None of these recipes is silently represented by
the installed Homebrew executables.

The local SDK 10.0.403 and fontTools 4.68.0 are available, but their original
archives and matching hashes are absent. The cached Python package index only
reaches fontTools 4.65.0; it cannot establish a 4.68.0 hash. An offline resolver
refused the requested version. No older fontTools version was substituted.

The captured compiler documentation checkpoint was reviewed independently against
`history:Tools/CapturedFlatCompiler/README.md`, without checking out that branch.
The matching reviewed SHA-256 and exact one-line punctuation difference are recorded
in that README. Its source lock includes the reconciliation note. No executable
source, fixture, renderer checkpoint or assertion changed for that reconciliation.

Shader staging defaults to the public synthetic parity gate in
`NativeShaders/public_parity.py`. The bootstrap runs it after native recipes produce
source-bound binary receipts. The private corpus remains an optional additional gate
and an explicit legacy maintainer route. See `NativeShaders/PublicParity.md` for the
coverage boundary and baseline review process. No private evidence is fabricated or copied.
