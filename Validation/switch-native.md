# Real XCI/NSP development: 2026-10-31

Implemented: a Mac companion using pinned NX2Apple/nxnative, separate from the
installed NRO interpreter. Source commit:
`8227f80f9b381c680611303319656c4e0c806a50`.
See [tooling and commands](../Tools/NATIVE_GAMES.md).

## Verified with the user's actual files

- Zelda base XCI: Application `0100F2C0115B6000`, version 0.
- Update NSP: Patch `0100F2C0115B6800`, version 720896; its internal Application ID
  and the base's Patch ID agree. Merged control data reports version **1.4.3**.
- Four missing DeltaFragment entries are reported explicitly. These optional
  incremental-delivery fragments are not treated as missing required Program,
  Control or Data content. Required entry IDs and sizes still must match.
- LibHac opened and layered the actual base/update contents with integrity errors
  enabled. The full extraction produced 304,248 files/entries (including ExeFS),
  about 15.9 GiB, as a RomFS image and executable files.
- The three profiled modules match their registered build IDs:
  `main=277178b7dba1b6d4949a84778d4abc58`,
  `subsdk0=f39f4c431234c72eb520cfdd1001bbcf`,
  `sdk=574284b40c06cf1821a44857939083be`.
- All three completed the upstream native-code rewrite successfully, retaining
  its original-byte assertions and patch validation.

These checks do not establish gameplay, full package signature validation, or
compatibility with other games. Keys and game/code bytes remain outside the
repository. JSON evidence contains metadata, IDs and status only.

The supplied Castlevania XCI contains two Application records:
`0100BA4026E94000` and `01005F4027E94000`, both version 0. Its companion NSP is
Patch `01005F4027E94800`, version 65536, targeting the second application
(`01005F4027E94000`). Thus the update is for the bundled gallery rather than the
main game; it is not a blanket mismatch with the whole XCI. The inspector now
supports explicit `--application-id` selection. Actual-file checks validated
both applications separately and the gallery/update pair; selecting the main
application with that update returned `update_mismatch`. Omitting selection
still returns `base_ambiguous`, and an unknown ID returns `application_not_found`.
Native preparation deliberately rejects multi-application packages because the
pinned upstream extractor cannot select a member reliably. Neither title has a
registered profile in the pinned reference. This finding comes from actual
metadata, not filenames.

## Tool and implementation checks

- New content inspector: .NET build succeeded with zero warnings/errors;
  38 synthetic assertions passed, including a real LibHac PFS0 read, malformed
  directory/metadata bounds, base/update relationships, required content versus
  optional DeltaFragment handling and explicit selection from bundled titles.
  Six actual Castlevania selection cases and three malformed CLI ID checks also
  passed. These establish metadata handling, not game execution.
- Companion: 18 tests passed using synthetic metadata and harmless subprocesses.
  Tests cover update mismatch, profile/version selection, space preflight before
  hydration, original-file preservation, literal arguments, safe diagnostics,
  requested title confirmation and refusal of unsupported bundled extraction.
- Native reference host compiled on this Mac. It emitted 52 compiler warnings;
  this is not a clean-warning or runtime-validation claim.
- All 13 companion readiness checks passed. The upstream shader translator,
  SPIRV-Cross and Metal fingerprints match. A real Metal kernel compiled and
  linked for macOS and iPhone through the selected Apple compiler.
- A minimal synthetic signing probe using the intended native bundle ID built
  successfully. `codesign --verify --deep --strict` passed, and both the final
  signature and embedded provisioning profile granted the increased-memory
  entitlement. The probe was not installed. This validates signing capability,
  not Zelda's memory footprint or execution; the real app is checked again.
- Full iOS shader preparation completed: 28,669/28,669 successful (cold cache),
  with no failed shadow-input, alpha-test, logic-operation or pulling-library
  compilations and no undecoded/unrecognized binding slots. The regression step
  reported no earlier build to compare; this is not a cross-version regression
  comparison. Xcode project generation then completed successfully.

Initial full preparation correctly stopped with `disk_space`: 43.6 GiB required,
13.2 GiB available. The user freed space; 132.6 GiB was then available, and the
full pipeline started. The saved initial preflight JSON is historical evidence,
not the current blocker.

## Device result / next validation

Work directory: `/tmp/satchel-zelda-native`.
Pipeline log: `/tmp/satchel-switch-integration/zelda-full-prepare.log`.
Reference checkout/toolchain: `/tmp/satchel-switch-integration`.
Prepared project destination: `/tmp/satchel-zelda-native/build/xcode/DmIOS.xcodeproj`.
Separate bundle ID: `com.dell.satchel.native.0100f2c0115b6000`.

Full preparation and the Release device build passed. The final app signature
passed verification, and its signature and provisioning profile both grant the
increased-memory entitlement. Source stamp `8227f80f9b38`, shader chain
`b3ca4b7c621f`, display version 1.4.3. The separate app was installed and its
17,046,402,254-byte merged RomFS image transferred successfully.

The first explicit-display launch was rejected by iOS with
`Can't look up displays without the proper entitlement, bub`. Removing the
optional `--display` flag and letting iOS choose the active display succeeded;
no entitlement or security check was bypassed. The game launched as PID 14293,
mounted the expected image, entered `nnMain`, completed its initial pipeline
warm-up, and presented its first frame at 9.929 seconds. Captures show the actual
opening title sequence and Zelda's rendered 3D torch/cave scene on the inner
2853×2007 display. The original Satchel app/data were preserved.

The run targets 30 FPS and has cloud save sync disabled. The cave captures showed
29–30 FPS; this is a single-scene observation, not a broader performance claim.
The log reports non-silent audio buffers and initial save commits; audible output,
game-progress save/reload and broader traversal remain unverified. The same
process survived the wait and subsequent tests, reaching over 155,000 presents;
most of that run stayed in one area and is not a campaign stability test.

**Resolved authentication blocker:** the separate XCTest touch harness triggered the
iOS prompt `Enable UI Automation: Touch ID to continue with XCTest`. The user
was asked to authenticate or cancel. The runner timed out with
`Timed out while enabling automation mode`; it did not execute its capture/touch
test. The native game remained alive with the same PID and was paused behind
that prompt. The user then authenticated with Touch ID. Four subsequent UI test
cases passed: capture, Plus, B and left-stick hold. The real game window measured
951×669 points (landscape), although XCTest's outer application frame was
669×951. Controls were targeted using the observed window.

Touch input was verified beyond XCTest success:

- Plus opened the System/Basic Controls menu; guest recording:
  `f151896:Plus~f8`.
- B returned to the cave; recording: `f153359:B~f7`.
- A one-second hold above the virtual stick's center turned and moved Link;
  recording: `f154251:L-0.01/0.62~f30`. Before/after captures show the changed
  position and facing direction relative to the stone column.
- The game PID remained 14293 throughout; the latest collected log contained
  zero `[RED]` lines and zero `CRASH sig=` markers. This is limited gameplay and
  UIKit-to-guest-HID validation, not proof of full-game compatibility.

The local continuation `/tmp/satchel-zelda-native/build_and_install.py` completed.
Its status is `device-stage.json` (`installed_awaiting_launch`, its final stage)
and its logs
are `xcodebuild.log`, `codesign-verify.log`, `install.log` and `transfer.log` in the
same work directory. Do not rebuild or recopy the unchanged game to resume.
The `device_check.py` helper launched with the diagnostic settings above,
`DM_CTL=satchel-control.txt` and `DM_REC=satchel-touch-record.txt`. The latter records
actual UIKit/controller input at guest HID reads and excludes scripted input.
These scripts are local task artifacts, not a distributed installer.

Evidence is in `/tmp/satchel-zelda-native/device-evidence/`: `launch.json`,
`log.txt`, `satchel-nvn.tsv`, opening captures and the authentication-prompt
capture, `controls-open.png`, `controls-closed.png`, and
`gameplay-after-move.png`. The touch recording contains the verified inputs above.
The touch harness is `/tmp/satchel-zelda-native/touch-probe`; its README gives
capture/hold commands. `capture-01.xcresult` records the authentication timeout;
`capture-02.xcresult`, `plus-01.xcresult`, `back-01.xcresult` and
`move-01.xcresult` passed. Use new result names for later runs. Both temporary
probe apps were uninstalled after validation, while Zelda kept running:
`com.dell.satchel.native.touchprobe` and
`com.dell.satchel.native.touchprobe.uitests.xctrunner`.

## Architecture boundary

NX2Apple's pipeline embeds rewritten game code into a signed per-game app. It
does not let the existing Satchel app dynamically execute arbitrary XCI/NSP
imports. Research found MeloNX's dynamic iOS core requires JIT, and its inspected
downstream licensing/provenance needs clarification before reuse; Eden has no
verified iOS target in the examined official build documentation. This native
path is the first actual-game proof, not the final general-emulator design or
an App Store acceptance claim.

Sources: [NX2Apple build pipeline](https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/scripts/build_ipa_from_nsp.py),
[Zelda profile](https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/games/0100F2C0115B6000/game.tsv),
[native iOS packaging](https://github.com/LNPANDY/NX2Apple/blob/8227f80f9b381c680611303319656c4e0c806a50/dm/ios/tools/build_ipa.sh),
[CNMT metadata](https://switchbrew.org/wiki/CNMT).
