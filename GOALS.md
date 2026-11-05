> Maintainer's working log. For the public summary see [ROADMAP.md](ROADMAP.md) and
> [COMPATIBILITY.md](COMPATIBILITY.md). Owners named here are the maintainer's coding
> workers; dates are when a line was verified on the phone.

# Satchel goals

The one list of what we are working towards, who owns each piece, and where it stands.
Set with the user on 2026-11-01. The orchestrator keeps it current; `PLAN.md` holds the
technical detail and history. Ask about any line by its number (for example "status of G5").

Status: `[x]` done (verified on the phone unless noted) · `[~]` in progress · `[ ]` not
started · `[!]` blocked (the line says on what).
Owners: `redesign` (Opus 5.5 session), `display`, `shaders`, `games` (Codex workers),
`orchestrator` (this session: reviews, merges, installs and tests on the phone), `user`.

The end state: **one real app**. Import any Switch game from iCloud, prepare it on the phone,
and play it with a controller or touch, at the best frame rate the phone can hold, with no
computer involved.

## Current push (2026-11-01 evening)

User priorities: a second game playable on the phone today, shaders prepared on the phone (no Mac),
and one app. Workers: `odyssey` (finish Mario Odyssey's shaders, package, phone), `castlevania`
(runtime shader capture to title screen, then phone package), `m5` (shader translation and compile
on the phone, replacing the Mac shader pack), `oneapp` (merge Satchel Dev into Satchel with a safe
on-phone data migration), and an on-phone UI test rerun. Mac disk was down to 16 GB; finished
parity copies were deleted (47 GB free) and a guard watches disk and memory.

## User goals

### G1. Full redesign of the app, by Opus 5.5
- [x] Name: **Satchel**. The 2026-11-03 clean break uses `com.dell.satchel*`
  and Satchel project names. Back up saves before the fresh install; re-import
  credentials and games and run Prepare again. No rename migration.
- [~] First pass done on branch `claude/app-redesign-6607c2` (2470362): shelf, detail, import,
  settings, artwork extraction; second pass adds polish, the in-game menu look and Duo states: `redesign`
- [~] Switch-inspired design system with personality (original, no Nintendo assets): `redesign`
- [~] Library with each game's real artwork, read from the user's own game files: `redesign`
- [~] Import flow from iCloud/Files with progress and plain-language errors: `redesign`
- [~] Settings (keys, storage, install help) restyled. The first-launch guide screen was
  removed (app audit, 2026-11-03): first launch lands on the home screen's three steps: `redesign`
- [~] No right-side gutter on any screen, both rotations, front and inner screen: `redesign`
- [x] Redesign merged into main (4c1ba73) with the display and multi-game work; the SwiftUI pause
  menu is wired to the native bridge; Satchel installed on the phone over Satchel (library kept)
- [x] Satchel's actions live in the iOS 27.1 right-side vertical bar as system buttons; panels open
  and close, also after an interruption (user tested 2026-11-01; merged 437e99e)
- [x] App icon for Satchel, and a DEV-banded variant for Satchel Dev (merged 437e99e)
- [x] More outer padding around the app (merged 437e99e)
- [x] Satchel Dev shows the redesigned pause menu over the game, full screen, with a working close
  button (user confirmed; 24b4153)
- [x] Pause menu presented with the same right-side system bar as the app (status icons, close
  button in the bar), and a visible in-game pause button to open it (user confirmed; 470f217)

### G2. Controller support
- [x] Engine GameController support (Xbox, DualSense, Pro Controller) confirmed compiled into
  Satchel Dev with the required Info.plist keys; touch buttons hide while a controller is
  connected; `controller '<name>' connected` is logged (source check, 1d688cc)
- [ ] Pair the Xbox controller (Settings > Bluetooth) and play a minute of Zelda: `user`, then
  `orchestrator` reads the log
- [ ] Controller can open the in-game menu (G5): `display`

### G3. Import any game to see its state and debug it
- [~] Satchel Dev holds and launches more than one prepared game: built and checked on the Mac
  (branch `more-games`, not merged yet): `games`
- [~] Prepare says plainly when a game has no profile yet (checked against the real dumps, not
  merged yet): `games`
- [x] Super Mario Odyssey 1.4.1 plays on the phone (2026-11-01, 20:13): 27,984 shaders, Mac
  package (6.65 GB) verified and signed, copied in 4 minutes, phone verify 58 s; boots, opening
  movie at 30 FPS, the user played into the Cap Kingdom with their controller. Mac-prepared for
  now; phone Prepare for Odyssey follows the phone shader work: `games`
- [ ] Scripted Mac input (DM_CTL stick holds) turns the camera but does not clear Odyssey's first
  "Look Around" hint; a real controller does. Only affects automated tests: later
- [ ] Odyssey's mode-select screen showed no label on the top option in one screenshot; check
  on the next launch: later
- [~] Castlevania: Belmont's Curse (Unity): generic starter profile plus one memory fix runs 45 s
  on the Mac host with no missing services; blocked on shaders (Unity packing is not parsed).
  Runtime shader capture built as a general tool (any game's capture to shader inputs; 14
  checks pass); 16 Castlevania shaders captured; compile queued behind the Mac shader test: `games`
- [ ] General importer: a starter profile generated from the game's own metadata, so an
  unknown game always tries to boot: next after `games` reports
- [ ] Per-game debug report in the app: boot stage reached, missing services, crash address,
  shader failures, exportable log: next after `games` reports
- [ ] Shaders found at runtime instead of per-game offline discovery (G7)

### G4. Every game at its own frame rate and full resolution
User direction (2026-11-01): no Performance or Graphics modes. Each game runs at the frame rate it
expects (Zelda 30, Odyssey 60) at 100% render scale; make that fast by optimizing the host, not by
rendering smaller. A manual frame setting may come back later.
- [x] Modes, frame cap and render scale removed from the menu, settings and game page; per-game
  profile instead (`db7110a`, not merged yet): built, phone check pending
- [x] Odyssey's slow motion and audio stutter: a 30 FPS target slowed the display timer and the
  game's clock with it; Odyssey now runs at its own 60, measured 60.0 FPS on the phone
- [ ] Zelda at 30 with 100% render scale on the phone: measure, then optimize where it falls short
- [ ] Odyssey at 100% (its own size): measure FPS and thermal over a longer play session
- [x] Zelda holds 28 to 30 FPS in the open world at the 30 cap, 0.65 render scale
  (Great Sky Island, 2026-11-01)
- [x] Per-game frame cap (30/60) and render scale settings, from the dev app or launch arguments (1d688cc)
- [x] Zelda at 60: render scale 0.5 holds 59 to 60 FPS in the open world (GPU about 12 ms per
  frame, thermal 0). At 0.65 it needs about 22 ms and falls to 30 (2026-11-01)
- [x] Zelda mode values measured on the phone (Great Sky Island route): Graphics = 30 FPS at
  0.85, locked even when hot; Performance = 60 FPS at 0.5 (engine minimum), holds 60 while the
  phone is cool and drops to 30 when the engine's heat protection kicks in (thermal state 2)
- [x] (Superseded) Performance and Graphics presets: removed by user direction, see above
- [ ] Edges look rough at low render scale (user report). The 3D view renders at half resolution
  in Performance mode. Graphics mode helps now; real anti-aliasing on the 3D view is a later
  engine task. At 60, 0.6 is too heavy on Great Sky Island (about 20 ms GPU per frame)
- [ ] Remove shader hitches: asynchronous pipeline builds (no frame stall on a new shader): queued after `shaders`
- [ ] 60 for other games where the game itself allows it (some need a per-game patch): after G3

### G5. In-game emulator menu
- [ ] Open from a touch gesture or a controller button combination without stealing game input: `display` (hooks)
- [ ] Pauses the game; resume; return to the library with the game paused in the background: `display`
- [ ] Settings on the fly: screen mode (fit 16:9, stretch, zoom), frame cap 30/60, render
  scale, FPS counter, touch controls opacity and size, haptics, audio volume, controller
  layout: `display` (hooks), `redesign` (menu look)
- [ ] Quit game. Today the engine runs one game per app launch, so starting a different game
  needs the app reopened; the menu says so plainly until the host supports more: `display`
- [x] Screen modes fit (16:9), stretch and zoom, cycled in game with a two-finger double tap;
  checked on the phone's inner screen (1d688cc)
- [~] Remove zoom (user tested it: crops too much). Keep fit (default) and stretch; a saved zoom
  migrates to fit: built (`display-menu`) and in the redesign's menu; merge pending
- [x] The menu itself: pause button on the game screen, three-finger double tap, or Options + Menu
  held; pause, resume, library with the game paused, settings, quit (user confirmed)

### G6. Every state of the iPhone Duo
- [~] Closed (front screen): game and controls fill the screen. Controls stay clear of the
  camera cutout only on the side where it is (flips with orientation and screen), and go to
  the edge on the other side: `display` second run (gamepad), `redesign` (app screens)
- [~] Open (inner screen): full layout: `display`, `redesign`
- [~] Half-folded: game on the top half, controls on the bottom half, like a handheld. The
  hinge crosses the long side of the inner screen, so the game turns upright while half
  folded and back to landscape when opened, closed or sent to the library (user report
  2026-11-05: the landscape split put both halves across the fold). The app turns the game
  itself and fills the screen; asking iOS for a portrait scene only gave a small upright
  window inside the landscape screen (second user report, same day): `display`
- [~] Switching between states while playing keeps the game running and relayouts at once:
  built with `UIHingeInteraction`; the phone reports the open posture correctly: `display`

## Work already running or needed to reach the end state

### G7. No computer step per game
- [x] On-phone content pipeline (base plus update, RomFS image) byte-identical to the Mac's
- [x] On-phone CPU rewrite, byte-identical to the Mac's
- [x] On-phone Prepare action builds the game package (unsigned)
- [x] On-phone signing works (2026-11-01, user ran the matrix): code signed on the phone with the
  user's certificate loads and runs, for Mac-compiled and phone-generated dylibs (6 of 6 real
  variants; the 2 intended failure controls failed). Rule: load signed code from a fresh file,
  never the file the signer mapped (that caused CODESIGNING "Invalid Page")
- [~] Real signer in Prepare: identity imported once and kept in the device Keychain, game.dylib
  signed on the phone, manifest marked signed: `signer` (Codex)
- [~] Phone's Metal compiler replaces the Mac's for shaders: all 71,621 Zelda shaders compile
  with the runtime compiler on the Mac, 0 failures (median 0.12 s each). Next: phone batch
  compile and matched screenshots; the phone package clones the existing 17 GB game data
  instead of copying it (works, after a /var vs /private/var fix): `shaders`, then `orchestrator`
- [x] The phone's own Metal compiler (iOS 27.1, A20 Pro) accepts Zelda's shaders: 436 of 436
  compiled, 0 failures, median 0.33 s each (Mac 0.12 s). A full up-front compile would take about
  3 hours with 2 jobs, so plan background compile during Prepare with more cores plus caching
- [ ] Matched screenshots, phone-compiled vs Mac-compiled shaders, when the phone is free: `orchestrator`
- [ ] Shader translation on the phone (Ryujinx translator and SPIR-V to MSL in process): next after `shaders`
- [x] Prepare end to end on the phone (2026-11-01, 15:10 to 15:56): from the user's imported XCI and
  update, the phone authenticated, composed, rewrote, built the 17 GB RomFS, signed game.dylib with
  the user's certificate, verified and published TOTK-1.4.3-2; the full package verifier accepted
  it; Zelda booted from it and loaded the user's save on Great Sky Island at 28 to 30 FPS. Only
  the shader pack still comes from the Mac. 45 minutes in all; copying the 100,298-file shader
  pack took about 20 of them (next speed target)
- [x] Prepare keeps running in the background as an iOS continued processing task, with the
  system progress display (user confirmed)
- [~] Prepare shows real progress for every step and reads the 17 GB file fewer times: `signer` worker
- [~] Preparation progress card on the home screen that reopens the Prepare screen (job and
  progress owned at app level); the Prepare close button works during a run: `redesign`
- [~] On-phone UI test suite for Satchel and Satchel Dev: every button and panel, plus background
  and return, checked on the real Duo (the Simulator misses the vertical bar and hinge): `uitests` (Codex)

### G8. One real app
- [x] Zelda boots and plays inside Satchel Dev (2026-11-01)
- [x] Two-hour session at 30 FPS without a crash
- [x] One app (2026-11-01): Satchel holds Prepare, signing, prepared games and the pause menu
  (branch `one-app`, not merged yet). Zelda and Odyssey, both saves, the signing identity, keys
  and settings are in Satchel; the user played both from its home screen. The in-app clone and
  verify migration failed twice on the phone (a /var path bug, then a failed final check), so the
  packages and saves were copied in from the Mac and Satchel adopts only the credentials. Satchel
  Dev deleted by the user (2026-11-01, freed about 53 GB). Castlevania also plays from Satchel
  (boots to its intro; picture fills only the top-left 1280x720: `castlevania` worker)
- [x] Every prepared game shows on the home screen; Play starts its newest package (`a591190`)
- [ ] After a verified Prepare, delete the imported game file, update file and shader pack (user
  direction: no duplicate copies; the game stays in the library with its art): next
- [ ] Quit returns to the home screen instead of closing the app: `session` worker. Same-process
  restart not solved yet (guest threads have no proven stop-and-join point; 14 failures in 5
  synthetic cycles)
- [ ] Saves: import from the Mac-built Zelda app done by hand; in-app save import/export and
  backup: later
- [ ] Distribution: SideStore source; keys import, the install guide (Settings > About) and
  signature-expiry warnings exist; the separate first-launch guide screen is gone: later

### G9. Stability and housekeeping
- [x] Builds no longer depend on `/tmp` (durable snapshot)
- [x] Mac test harness: launch, input and in-game screenshots from the Mac (`DM_CTL`)
- [x] Full Ryujinx 1.3.3 source as a reference for porting missing services
- [ ] A launch right after a long session ended once hung before the game started; reproduce and fix: `orchestrator`
- [x] Phone cleanup (2026-11-01, approved by the user): removed the Mac-built Zelda app
  (`com.dell.satchel.native.0100f2c0115b6000`, about 22 GB) and the UI test runner. Its six
  save slots are backed up on the Mac (`~/SatchelBuild/host/saves/dmios-final`) and restored
  into Satchel Dev. Benchmarks restore them after every run.
- [x] Sign Load Probe removed from the phone (results kept on the Mac in `~/SatchelBuild/spike2/phone-run2`)
- [x] Test certificate copies deleted from the phone and iCloud Drive (user, 2026-11-01)
- [ ] The signing probe README carried the user's certificate name in git history since 57839c3
  (removed from the current file in f4d0047); rewrite history only if the repo is ever published: `user` decision
