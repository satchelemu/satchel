# On-phone package execution work: 2026-10-31

## Result and limits

Build 8 of `com.dell.satchel` performs real package decryption, executable
integrity checks, NSO decoding and bounded interpretation on the maintainer's iPhone Duo.
The original imported Castlevania XCI is the input. No prepared game code,
native game modules or shader cache is transferred from the Mac for this path.

**Castlevania has not booted or rendered.** This is an executable/process
diagnostic, not playable Switch compatibility. The latest phone run executes
**66,965,844 original instructions**, services **22 QueryMemory calls**, reaches
the `sdk` module and stops before unimplemented `GetInfo` (`SVC 0x29`).
Its package detail retains
“Not playable yet” and has no Play button. GPU submission, NVN runtime calls,
state-aware game shader translation, RomFS assets, audio, scheduling, general
Horizon services and update composition are not implemented.
The embedded shader compiler chain is separately validated on the Duo with
original synthetic fixtures; it is not connected to guest NVN calls.

The older separate native Zelda app is unchanged; its Mac-prepared gameplay
proof must not be counted as phone-only emulator validation.

## Genuine phone journey

The selected package was already copied from iCloud through Files in the
[import milestone](phone-import.md). Runtime tests exercise the installed app
against that local copy, not a synthetic game fixture.

1. Import the user's console key file through the actual Files/iCloud picker.
   Relevant entries are saved in device-only, non-synchronizing Keychain storage.
2. Inspect encrypted Program headers and explicitly select title
   `0100ba4026e94000`. The XCI also contains a separate gallery application.
3. Recheck the local package SHA-256 against its library identity.
4. Decrypt the selected NCA's ExeFS and verify its entire hash tree: **65,215,759
   bytes and 996 blocks**. Decode `rtld`, `main`, `subsdk0`, `sdk` in RAM and
   verify **12 declared NSO segment hashes**.
5. Parse the verified `main.npdm`, match its program ID and preflight all decoded
   memory before allocation. The supplied game requests a 39-bit address space,
   1 MiB stack, priority 44 and CPU 0. Its flags are supported by the diagnostic.
6. Map the four original NSOs and an initial thread context, execute with the
   no-JIT CPU backend, and report the exact stop without presenting gameplay.

Decoded image sizes: rtld 20,480; main 120,438,784; subsdk0 6,844,416; sdk
12,967,936 bytes. Total page-rounded module allocation is 140,271,616 bytes.
Nothing is written as decompressed game code. Reports omit keys, game
instruction words/operands and module payloads.

The supplied NSP is an update to the gallery (`01005f4027e94000`), not to the
main game. It is not applied to this diagnostic. The phone still needs CNMT
association and patch-layer support before it can offer update installation.

## Device evidence

Device UDID: `00008160-001A30E02E600036`. Actual iOS 27.1 / Xcode iOS 27.2 SDK.
All artifacts below are task-owned local development evidence under `/tmp`.

| Test | Evidence | Result |
| --- | --- | --- |
| Original NRO import/run/persist/picker | `/tmp/satchel-runtime-nro-01.xcresult` | Passed |
| Files key import + real XCI executable check | `/tmp/satchel-runtime-castlevania-02.xcresult` | Passed, 14 original rtld instructions |
| Cold launch with saved Keychain keys | `/tmp/satchel-runtime-castlevania-03.xcresult` | Passed, 15 instructions after CPU expansion |
| Full multi-NSO/NPDM process map | `/tmp/satchel-runtime-castlevania-04.xcresult` | Passed, 365 instructions, no QueryMemory call reached yet |
| Expanded CPU and map-backed SVC execution | `/tmp/satchel-runtime-castlevania-05.xcresult` | Passed, 7,653,869 instructions and 22 QueryMemory calls |
| SDK startup boundary + embedded shader/GPU tests | `/tmp/satchel-runtime-castlevania-06.xcresult` | Passed, 66,965,844 instructions, 22 queries, SVC 0x29 in sdk; synthetic graphics checks passed |
| Original NRO journey after shader-framework integration | `/tmp/satchel-runtime-nro-02.xcresult` | Passed on the final installed build |

The stage-04 stop was guest PC `0x80017dc`, a variable-register shift outside
that installed CPU subset. Its JSON is `/tmp/satchel-runtime-castlevania-04.json`.
Elapsed 2.165 seconds includes hashing the entire 4.85 GB file, decryption and
decoding; it is not a CPU-throughput or FPS measurement. `graphicsImplemented`
is false. A host-side diagnostic identifies subsequent CPU gaps, but those
host results are not substituted for real phone evidence.

Stage 05 reached `rtld+0x116c`, an unsigned division instruction outside that
installed CPU subset. The sanitizer-backed host diagnostic reproduced the
**same instruction count, query count and virtual PC**. The phone's whole
check took 3.207 seconds; this also includes file verification and loading.
The subsequent device test now requires at least this instruction/query
milestone, so an earlier unsupported stop cannot silently pass the regression.

Stage 06 uses the final Stage16 CPU archive: **17,780 ASan/UBSan checks** passed;
iOS archive SHA-256
`006a13ac89a485a793d2919fd03fbab4a0db5e8e260b287af7bcb37c33afb2bd`.
The actual phone stop is `sdk` PC `0xfad81a0`, before GetInfo. The diagnostic
limits are 100 million instructions and ten seconds of CPU execution, with
cancellation checked each instruction. This run stopped at the unsupported
service, not either limit. Whole-check time **10.942 seconds** includes loading
and the graphics self-tests; it is not FPS.

Safe final phone metadata is retained in [runtime-core.json](runtime-core.json).
No game code or keys are included. The user can reproduce the same check from
the app using its local XCI and persisted keys.

## On-phone shader compiler evidence

`Core/GraphicsDiagnostic.m` executes only original, fixed fixtures. In the same
stage-06 phone test:

- Embedded NativeAOT reports dynamic-code support and compilation **disabled**.
- Original Maxwell EXIT-only compute data becomes **676 SPIR-V bytes**, then
  **242 MSL bytes**. On-device Metal compilation and GPU completion succeed.
- A separate original SPIR-V store fixture becomes **185 MSL bytes**. GPU
  execution produces the expected buffer value and preserves three guard words.
- Both commands finish successfully. GPU completion waits have five-second
  deadlines. No game shader or per-game Mac shader cache is used.

The store fixture starts at SPIR-V; do not describe it as a verified Maxwell
memory-store translation. The EXIT shader proves the full compiler chain but
does not write pixels. Neither is a Castlevania frame. The user-facing report
labels this **synthetic**, and `graphicsImplemented` remains false.

After stage 04 the library JSON remained exactly equal to the post-import
index: **five entries**, including all three original NROs and both packages.
Comparison: `/tmp/satchel-library-after-packages.json` against
`/tmp/satchel-library-after-runtime.json`. No library migration or game-file
replacement was required.
The final post-framework/post-NRO-regression comparison also matched all five
entries exactly (`/tmp/satchel-library-after-runtime-final.json`). All ten full
license/notice files matched their source copies inside the built app bundle.

## Independent checks

- Package parser: 49 synthetic assertions passed earlier in this turn.
- Executable reader: 43 synthetic assertions passed, including published AES
  vectors, multi-sector XTS, encrypted ExeFS integrity, malformed extents,
  cancellation, metadata extraction and size/tamper boundaries.
- NSO decoder: 22 assertions and 1,000 ASan/UBSan mutations passed. LZ4 was
  compared with Apple's independent encoder, not a second copy of the decoder.
- Key parsing passed, including strict hexadecimal validation. Tests did not
  alter the Mac Keychain or use the user's key values as fixtures.
- NPDM metadata: 1,678 synthetic assertions and 2,000 ASan/UBSan mutations passed.
  These validate structure, not publisher signatures or authorization.
- Process mapping / QueryMemory: 85 ASan/UBSan assertions passed, including
  maximum module count, page padding/holes, memory types/permissions, TLS/stack,
  rejected features, free/inaccessible extents, cancellation and mutation-free
  output-pointer failures. The SVC fixture is original synthetic assembly.
- CPU coverage is checked against independently assembled native ARM64 fixtures
  with ASan/UBSan before producing the iOS archive. The current count and exact
  covered instruction families are recorded in `Tools/CPUProbe/README.md`.
- SPIRV-Cross bridge: **1,242 ASan/UBSan checks**, including active/declared
  resource metadata, malformed framing and bounds. Both original Metal
  dispatches also passed on the Mac before device integration.
- Maxwell compiler: **216 pinned source/resource/license files** verified;
  **4 native C → NativeAOT assertions** passed. iOS static/shared compilation,
  native static linking, embedded framework signing and actual phone invocation
  passed. Compiler/runtime packs 10.0.14 stay in the task scratch directory;
  no SDK/workload or global tool install was required.
- Xcode `build-for-testing` passed for the app and actual physical device.

Initial failures were fixed rather than hidden: linker error
`library 'SatchelCPUProbe' not found` required the static-library search path;
the first UI check's `XCTAssertTrue failed` came from a parent accessibility
identifier masking the import button, fixed with a containing accessibility
element. Native tests exposed an invalid W-register rotate causing undefined
behavior in upstream VIXL; the adapter now rejects that encoding before decode.

## Reproduce / continue

See `Tools/CPUProbe/README.md` to fetch the pinned VIXL source. Run
`zsh scripts/test_cpu_probe.sh --ios`, the affected loader/process scripts,
and build both shader components using their READMEs before `xcodegen generate`
and the `Satchel` scheme. On this Mac the exact shader commands are:

```sh
zsh scripts/test_maxwell_shader.sh --checkout /tmp/satchel-switch-integration/NX2Apple --dotnet /tmp/satchel-switch-integration/dotnet/dotnet --work /tmp/satchel-shader-aot --platform all
zsh scripts/test_shader_bridge.sh --ios --exit-spv /tmp/satchel-shader-aot/repro/exit.spv
```

The CPU library and shader framework are ordinary signed emulator components;
these steps do not compile a game or prepare its shaders.

On the phone: open the imported Castlevania package, scroll to **On-phone
executable check**, choose **Find programs**, then **Check title
0100ba4026e94000**. Existing device keys can be reused. A new phone needs its own
Files key import. The result remains a diagnostic.

For device automation, `RuntimeJourneyTests` takes an explicit package name,
title ID and optional key-import flag. Existing `/tmp/satchel-runtime-build`
contains configurations for the original Files import and saved-key relaunch.
Never place key values or raw game payloads in test environment variables.

Next: implement the actual `GetInfo` request reached by the SDK against real
process/resource/address-space state, followed by the required startup services.
Do not reuse the old NRO kernel's fixed values as arbitrary commercial-game
answers. TLS-register access, process services and assets
must precede a usable game environment. The graphics dependency is documented
in `Tools/NVN_RUNTIME.md`: NVN interception needs a checked guest-call boundary
and state-aware shader translation. No game graphics frame or playable performance has
been established for this path.
