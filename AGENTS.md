# Satchel: rules for agents

Satchel is an iPhone app (iPhone Duo, iOS 27.1, landscape) that imports Switch games
(XCI/NSP plus updates) from iCloud and plays them entirely on the phone. No computer step
per game. Distribution target: sideloaded (Xcode, SideStore/AltStore), signed with the
user's own certificate. Not the App Store.

The goals and their owners are in `GOALS.md`; the plan and technical history are in
`PLAN.md`. Read both first. The public-facing summaries are `README.md`, `ROADMAP.md`,
`COMPATIBILITY.md` and `BUILDING.md`; keep them consistent with any change you make.
The previous instructions file (interpreter-era status log) is archived at
`Validation/history/AGENTS-pre-pivot-2026-11-01.md`; it is history, not rules.

## Name

The app, project, targets, schemes and source identifiers use Satchel. The app ID is
`com.dell.satchel`; variants use the same prefix. Build and dependency folders are
`~/SatchelBuild` and `~/SatchelDeps`. The 2026-11-03 rename is a clean break: the user
backs up saves, removes the previous apps, installs the new app, imports credentials
and games again, and runs Prepare again. No old identity, ABI alias or data migration
is provided by the rename. Keep third-party notices and `Validation/history` intact.

## Direction

- Route: run the game's own ARM64 code natively, the way NX2Apple/nxnative does
  (binary rewrite of SVC/TLS/x18 sites, generic Horizon host, NVN on Metal). The phone
  does the rewrite, packages the result as a dylib, signs it with the user's certificate,
  and loads it. Signed code loaded this way is allowed; runtime JIT is not the plan.
- The VIXL interpreter (`Tools/CPUProbe`, `App/SwitchRuntimeSession.swift`) is kept as a
  fallback and diagnostic. Do not extend it unless `PLAN.md` says so.
- Reuse before writing: NX2Apple/nxnative (MIT) at
  `/tmp/satchel-switch-integration/NX2Apple` (durable copy in
  `~/SatchelDeps/tmp-snapshot-2026-11-01/tmp/satchel-switch-integration`), Ryujinx (MIT):
  full source at `~/SatchelDeps/reference/ryujinx-1.3.3` (Ryubing tag 1.3.3, read-only
  reference; NX2Apple vendors 1.3.2's shader parts). Port missing Horizon services from
  `src/Ryujinx.HLE` and `src/Ryujinx.Horizon` before writing new ones, keeping the MIT
  notice. Its CPU JIT (`ARMeilleure`) does not apply: game code runs natively here.
  GPL code (Eden, Citron, yuzu forks) only after asking, because it changes the license.
- Graphics: Metal is the native API on iOS. Vulkan runs only through MoltenVK on top of
  Metal; do not move rendering to Vulkan for speed.

## Hard rules

- No keys, firmware, game files, decrypted code or game-derived binaries in this repository
  or in logs. Users supply their own dumps and `prod.keys`.
- Never commit, push, or delete outside your task's own files. The orchestrator commits.
- Never handle the user's passwords or certificate passphrases. If a step needs one, stop
  and report what the user must do.
- Do not weaken a check to make it pass. Report failures with the exact output.
- No em dashes or double hyphens as punctuation in anything you write (UI copy, docs,
  comments, commit messages). Use commas, colons, periods or parentheses.

## Builds and device

- Build outputs go in `~/SatchelBuild/<task>/`, never inside the repo and not in `/tmp`
  for anything that must survive a reboot.
- Device: the maintainer's iPhone (iPhone19,4, UDID 00008160-001A30E02E600036). The signing
  team is never written into the repository: builds read `SATCHEL_TEAM`, then the untracked
  `local/signing-team`, then the Mac's single Apple Development identity.
  Device builds use `-configuration Release`. Only the task that `PLAN.md` names as the
  device owner may install or launch on the phone at a given time.
- Satchel bundle ID `com.dell.satchel`; experiments use their own bundle IDs
  (`com.dell.satchel.<name>`) so the installed app and its library stay untouched.
- Keep the UI landscape-locked, with one exception the maintainer asked for (2026-11-05): a
  game on a half-folded Duo plays upright, picture on the top half and controls on the
  bottom half (`NativeHost/DisplayControls.h`). Preserve the existing library and imported files.

## Reporting

End every task with: what was done, what was verified (exact commands and results), what
failed or was skipped, and the single next blocker. Distinguish "built" from "ran on the
phone" from "played".
