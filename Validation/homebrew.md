# Satchel 0.2: homebrew milestone, October 31, 2026

## Implemented

The installed iPhone app has a local NRO library, Apple's Files import browser,
and an exporter for the included original sample. Security-scoped, coordinated
reads support iCloud Drive and other enabled Files providers. Imports are
validated, hashed, and copied into the app; selected originals stay unchanged.
Repeated imports keep one library entry and atomically repair its cached bytes.
The library itself is local, not an iCloud-synced database.

The interpreter now loads NRO0 text/rodata/data/BSS into bounded guest memory,
provides initial homebrew loader configuration and TLS, and implements Horizon
OutputDebugString, GetSystemTick, and ExitProcess plus a clean loader return.
New integer, addressing, signed-load, and limited SIMD instructions support
startup code. Guest page permissions, unsupported-service faults, cancellation,
and execution limits remain enforced. No JIT or native guest execution is used.

## Validated

- Native ARM64 comparison: all 16 observed register results match the independently
  executed assembly probe. AddressSanitizer and UndefinedBehaviorSanitizer report
  no findings in the instruction and NRO tests.
- The original standard NRO self-check completes 41 instructions, exits with 0,
  and verifies segments, BSS, stack, loader arguments, TLS, and Horizon output.
  It is freestanding, does not link libnx, and is not verified on Switch hardware.
- Malformed/truncated headers, segment/asset bounds, protected or unmapped memory,
  invalid syscall pointers, missing services, and private-demo ABI separation pass.
- The original 1,800-frame full-framebuffer/native-state differential suite passes.
- Actual iPhone UI tests pass for local import/run, persistence across relaunch,
  system picker presentation, and the original gameplay controls/600-frame check.
- The cloud round-trip UI test saves a uniquely named `.nro` to the real iCloud
  Drive provider, browses to that exact document, imports it, and checks successful
  execution and debug output. This establishes provider integration; it does not
  test an evicted file or synchronization to a second device.
- Recovery was tested by replacing only our sample's app-owned cached copy with
  a 50-byte invalid fixture. Reimporting through the app repaired it, execution
  passed, and the retrieved bytes matched the original SHA-256.

Sample SHA-256:
`aab1722b76e532e657e3f93fd7764477b321d8758ac325a2e02a07b0165c2b66`

`homebrew-iphone-run.json` records the final sample run. `homebrew-iphone.png`
shows completion and actual debug output on the device. `homebrew-ui-summary.json`
records the final passing test results for each affected journey. Failed interim
picker tests exposed incorrect UI navigation/selectors; assertions were retained
and the correct native provider cells are now selected.

## Existing homebrew and limits

The official [nx-hbmenu v3.6.1 release](https://github.com/switchbrew/nx-hbmenu/releases/tag/v3.6.1)
loads and executes 31,956 startup instructions in both the host interpreter and the actual iPhone app, then
stops with `Horizon SVC not implemented: 0x2 at PC 0x33b2c`.
This is SetMemoryPermission. The menu does not render. The physical-device UI
test imported the official binary through Files and asserted this exact diagnostic;
`hbmenu-iphone-run.json` and `hbmenu-iphone.png` record the result. Its NRO is an external
compatibility fixture, not bundled into the app or this project; its ISC license
accompanies the fixture copied into the phone's Documents folder.

Most existing homebrew will stop at missing instructions or Horizon services.
Graphics/NVN, audio, complete SIMD/floating point, threading, and a full libnx
service environment remain unimplemented. Maximum input is 64 MiB, guest memory
is 64 MiB, and runs stop after two seconds or 20 million instructions. This is
an exploratory interpreter, not a complete Switch emulator or a hardened sandbox.

## Reproduce / continue

Use **Satchel → Import from Files → Browse → iCloud Drive → select .nro →
Run homebrew**. **Save sample to Files…** exports an original working fixture.
**Load Horizon sample** imports that fixture directly without requiring cloud.

Host: `zsh scripts/test_homebrew.sh /absolute/path/to/hbmenu.nro` and
`zsh scripts/validate.sh`. Build with XcodeGen/Xcode as described in the root
README. No additional dependencies were installed. The Release build is signed
with the existing development team and installed on the paired physical iPhone.
No App Store submission or external distribution was performed.

UI tests require the paired phone unlocked with iCloud Drive available. The
existing-homebrew diagnostic test additionally requires the official fixture at
`Documents/hbmenu.nro` in the app's container. These are integration tests with
explicit external prerequisites, not hermetic CI tests. Test result bundles and
logs are in `/tmp/satchel-homebrew/` (temporary, not durable).

Next: implement real SetMemoryPermission semantics with permission-boundary
tests, then follow hbmenu's next startup dependency. Keep unsupported operations
explicit; reaching another instruction does not establish game compatibility.
Reference layouts/ABI: [libnx NRO definitions](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/nro.h)
and [Homebrew ABI](https://switchbrew.org/wiki/Homebrew_ABI).
