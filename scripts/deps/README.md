# Dependency input pins

`inputs.lock.json` separates two kinds of evidence:

- `inputs`: the 19 source and package inputs the bootstrap stages into a dependency
  root, each with its URL, revision or version, license and SHA-256.
- `tools`: the seven build tool distributions `scripts/setup_tools.py` installs into a
  tools folder (CMake and XcodeGen release binaries, the .NET SDK archive, Python and
  SPIRV-Tools/SPIRV-Headers sources, the fontTools wheel). Each row names where its hash
  comes from (`hash_source`): a publisher checksum file, publisher release metadata, a
  GitHub release asset digest, or, for the two SPIRV tag archives that have no published
  checksum, the earlier installed-formula pin (`formula_provenance`, with the formula
  file's own hash). The .NET SDK row carries Microsoft's SHA-512.

`gaps` and `tool_recipe_gaps` are empty since 2026-11-07. The two earlier distribution
gaps (.NET SDK 10.0.403 and fontTools 4.68.0) are closed by publisher hashes. The five
earlier recipe gaps are closed as follows: Python and `spirv-val` build from their
pinned sources with Xcode's compiler and SDK (Python needs no third-party native
library: the build does without `ssl`, and downloads use `curl`); CMake and XcodeGen use
the publishers' release binaries instead of source builds, because a source build of
XcodeGen needs an unpinned Swift package closure. The CMake row keeps the source archive
hash as `source_reference`; Kitware's checksum file lists the same value. No Homebrew
bottle or installed tool is an input of the clean route; the maintainer's local
diagnostic route (`--local-validation`) still uses installed tools and is never
reported as clean-Mac ready.

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
