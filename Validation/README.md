# Original 0.1 validation record: October 31, 2026

This is the historical first milestone. See [0.2 homebrew validation](homebrew.md)
for the subsequent NRO loader, initial Horizon services, and iCloud import.

**Implemented and validated:** a standalone development-signed iPhone app running
an original AArch64 guest through a C interpreter. **Not implemented:** Switch
executable loading, Horizon OS services, NVN graphics, audio, or commercial games.

## Host execution

- Installed toolchain: Xcode 27.2 (27B52), macOS / zsh, XcodeGen 2.45.3.
- Native reference differential test: passed 1,800 frames, comparing all 153,600
  framebuffer bytes and seven state values on every frame.
- Executed 317,159,219 interpreted instructions across that trace.
- Boundary cases passed: unknown opcode, infinite loop instruction budget,
  out-of-range memory, guest code writes, unknown service, malformed code length.
- The same suite passed with AddressSanitizer and UndefinedBehaviorSanitizer;
  no sanitizer findings were reported.
- Frame 600 expected FNV-1a: `756c6ba1f06600f4`.

## Physical iPhone

The paired physical iPhone (product type iPhone19,4) runs iOS 27.1. The Release
app built, signed, installed, and launched successfully. Standalone runs use
`devicectl device process launch`; no debugger is attached. The development
signature permits debugging but the app does not request or use JIT permissions.

The first standalone check completed 600 frames in 10.10 seconds, executing
107,233,200 guest instructions. It matched the expected checksum. Measurements:
approximately 60 presented FPS, 52.9 million guest instructions per interpreter
second, and 4.37 ms p95 guest-frame execution time. These are measurements of
this small synthetic workload, not estimates for Switch games. CPU timing
excludes host image presentation; presented FPS measures the display callbacks.

The final automated UI journey passed **one test, zero failures**, on the
physical phone. It checks the 600-frame result, pause/resume, manual dragging
and actual guest paddle position, reset, and the diagnostics sheet. During
development it exposed inherited accessibility labels on the playfield; those
were corrected. A reset assertion was changed to inspect immediately because
the live guest can score again before XCTest's first one-second polling tick.
The zero-score assertion was retained. The diagnostics result was made a
separately accessible text element. All final assertions pass.

`ui-test-summary.json` records the successful run. `iphone-validation.json`
records the final standalone 600-frame run; `iphone.png` shows the installed app.
The full test result bundle is in `/tmp/satchel-build/PlayerJourney-verified.xcresult`.

The app was left running after validation. Its development provisioning profile
expires October 31, 2027 (04:19 UTC); rebuilding with a valid profile renews the
installation. No App Store submission was made.

## Interpretation

This establishes that the bounded interpreter, original guest workload, touch
input, framebuffer display, and diagnostics work on the phone without JIT.
It does not establish broad ARM64 correctness, long-session thermal/battery
behavior, Switch compatibility, or App Store acceptance. The next meaningful
test is real freely distributable Switch homebrew using its actual loader and
Horizon ABI, followed by representative CPU and graphics workloads.
