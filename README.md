# Satchel

Satchel is an experimental iPhone app that imports your own Nintendo Switch game dumps
from iCloud or Files, prepares them on the phone, and runs them natively. There is no
per-game computer step: decryption, the ARM64 rewrite, packaging, code signing and shader
translation all happen on the iPhone.

It is sideloaded, signed with your own Apple developer certificate, and built for the
iPhone Duo on iOS 27.1 in landscape. It is a research project, not a finished product.
Read [the compatibility list](COMPATIBILITY.md) and [the roadmap](ROADMAP.md) before
expecting anything to work.

<table>
  <tr>
    <td><img src="docs/screenshots/library.jpg" alt="The Satchel library with fictional games, on the open iPhone Duo screen" width="100%"></td>
    <td><img src="docs/screenshots/game-detail.jpg" alt="A game's page with its path to play, update and details" width="100%"></td>
  </tr>
  <tr>
    <td align="center">Library</td>
    <td align="center">Game page</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/import.jpg" alt="The Add games screen listing base game, update and homebrew formats" width="100%"></td>
    <td><img src="docs/screenshots/settings-keys.jpg" alt="The Console keys settings screen" width="100%"></td>
  </tr>
  <tr>
    <td align="center">Add games</td>
    <td align="center">Console keys (your own prod.keys, kept in the Keychain)</td>
  </tr>
</table>

Screens are from the iOS Simulator on the iPhone Duo's open inner screen, with a fictional
library. The README deliberately shows no frames or artwork from Nintendo's games.

## What works today

Measured on one iPhone Duo (A20 Pro, iOS 27.1). Dates are in the compatibility list.

- The Legend of Zelda: Tears of the Kingdom 1.4.3 boots from a package prepared entirely
  on the phone, loads saves, and holds 28 to 30 FPS in the open world at the 30 FPS cap
  and 0.65 render scale. A two hour session ran without a crash.
- Super Mario Odyssey 1.4.1 runs at its own 60 FPS; it was played into the Cap Kingdom
  with a Bluetooth controller.
- Castlevania: Belmont's Curse boots to its intro and gameplay at a steady 30 FPS, with
  hitches when a new shader is first compiled.
- Mario Kart 8 Deluxe boots and plays, with shader stalls and memory growth between
  scenes still being worked on.
- Import of XCI, XCZ, NSP and NSZ files (including split dumps) from Files or iCloud
  Drive, with updates and DLC. Titles and artwork are read from your own game files.
- Prepare runs as a background task with system progress, and signs the game code on the
  phone with your Apple Development identity.
- A pause menu (three-finger double tap, on-screen button, or holding Home on a
  controller) with resume, return to library, quit, screen fit, an FPS counter, audio
  volume, touch size and opacity, and a controller layout view. Settings > Controllers
  configures the shortcut. Home stays reserved; Menu and Options work normally alone,
  then both are suppressed from chord completion until both are released.
- Controller profiles use GameController input names, saved as JSON per product
  category beside each title's player settings, with global defaults. The settings
  page includes live inputs, stick visualization and a radial dead zone. D-pad and
  stick edges move focus through the library, game actions, Settings, Add games and the
  pause menu, with a console-style focus that follows each target's shape (the lifted
  library tile, a sliding row highlight, brightened buttons), hold-to-repeat, a light tick
  per move and an A/B hint, shown only while the controller is in use. "What iOS reports" in Settings > Controllers shows each connected pad's
  name, category, profile and live presses, to tell a pad iOS never handed to the app
  from one Satchel misreads.
- Xbox, DualSense and Pro Controller support. Touch controls hide when a controller is
  connected.

Game-page additions awaiting hardware validation: per-title FPS target, render scale,
screen mode, haptics and pipeline mode with defaults, override markers and reset. The
save manager lists dated, sized backups, keeps the newest three, restores with
confirmation and exports through the share sheet. Import accepts title-bound Satchel
ZIPs, not arbitrary emulator archives. See [the test guide](docs/testing.md).

Also built and awaiting hardware validation: per-title rumble strength (0 to 300%) and
controller rumble cutoff, on the game page and live in the pause menu; optional iCloud save
sync through a folder you choose in Settings > Storage (one file per game, synced at start,
after saves, on background and at Quit, with the replaced save kept on the phone); and the
game view following the shape of the picture a game presents. The iPhone Duo picture shape
(1.43:1) needs per-game patches that none of the tested games has, so it stays off and
hidden. Phone motion (gyro) is on for every game. TV output through a cable or AirPlay is
wired to the engine but only offered with Developer controls until it is checked.

Native import format limits, Prepare read measurements and synthetic coverage are
documented in [the importer validation guide](docs/importer-validation.md). These Mac
checks do not add phone compatibility claims.

Duo posture previews cover the library, game page, Settings and pause menu. Half-folded
play turns upright, so the picture fills the top half and the controls and menu the bottom half. Synthetic transition journeys check
Fit geometry and held touch input; real hinge and display-switch validation remains.
See [the posture test guide](docs/testing.md#duo-postures).

## What does not work yet

- Only four games have been tried. Any other game may fail at import, Prepare or boot.
  The app records exactly where (missing service, crash address, shader) so a report is
  useful. See [COMPATIBILITY.md](COMPATIBILITY.md).
- A pinned-input bootstrap rebuilds native dependencies in an isolated root using local
  caches and installed tools. Five additional tool sources now have URL and hash pins.
  Public synthetic shader parity now validates and stages fresh shader dependencies
  without game data. Full unsigned-app validation and the remaining clean-Mac tool
  pins and standalone recipes are still open. This is the first thing we want help with: see [BUILDING.md](BUILDING.md) and [ROADMAP.md](ROADMAP.md). Signing
  itself is account-neutral: the build reads your team from your Mac and the app reads
  its own team at run time, but only the maintainer's account has been tried.
- First play of a game stutters while shaders are translated and compiled on the phone.
  Translation and caching happen in the background, but a brand new shader still costs a
  frame. The optional async pipeline mode uses equivalent ready fallbacks and a
  4 ms optional wait budget. Required first-use work can still exceed that budget.
  Experimental skip mode can omit a final draw while its pipeline builds, with
  visible artifacts possible. Experimental bounded mode starts translation and shader
  libraries when the game binds a program, so fewer frames wait for them; it changes no
  draw, and pipeline builds still wait. Game settings save the mode per title for the next
  launch; exact stays the default. An opt-in launch prewarm (`-SatchelPipelinePrewarm YES`,
  measured on the Mac only) builds the pipelines a device recorded before the first frame,
  for a bounded loading time; it does nothing on a game's first launch. The shader warm-up is
  opt-in: Prepare does not start it, only the game page's Warm up does, and a game plays
  without it. It can take hours for a large game. It now keeps a bounded queue fed,
  reuses identical translated source, and uses extra lanes when foreground or
  charging, subject to thermal and power constraints. iOS sees progress after every
  finished shader, and shaders the game already used in play are built first. A large
  game only gets far with Satchel open, the screen on and the phone charging: in the
  background iOS may stop it, and it resumes when the player returns. Phone speed
  measurements remain pending. See [warm-up measurements](NativeShaders/WarmupSpeed.md) and
  [graphics notes](NativeHost/graphics/README.md).
- No cheats, no multiplayer, no App Store release (and none planned).
- The phone's hardware matters. The app requests the increased memory limit and extended
  virtual addressing entitlements; those capabilities, the free developer account limits
  and prepared game loading have not been verified together on other phones.
  Settings > Device check shows the running signature's entitlements, live memory
  allowance, a bounded virtual address reservation, model, iOS version, profile expiry
  and container volume free space. Copy report omits the signing team unless enabled.
  This screen has simulator coverage; its signed hardware readings still need validation.

Long-session validation is still open. After ownership fixes, the Mac harness passed
an uninterrupted 30-minute Castlevania interval and a 60-minute, 12-stop switching
loop with zero detected leaks. A retained-cache Zelda retry also passed thirty
minutes and guest join on main `cd6fbf9`. The historical timeout did not reproduce;
its cause and live memory growth remain under investigation. Mac results do not
establish phone memory acceptance. Nine same-process Mac visits across the three
titles also complete without a launch hang or stop timeout.
See [session validation](NativeHost/session/README.md). Developer-only remote Quit
and timed prepared-game cycles reuse the menu teardown and write per-visit telemetry.
[The phone procedure](docs/phone-cycle.md) is ready for orchestrator validation;
these controls do not establish a new phone stability result.

## Engine update of 2026-11-05: prepare your games again

The native engine moved from nxnative `bbc3b737` to `f8a5618d` (265 upstream commits,
shader chain `26dc2fa9477c`). This build has been compiled and checked on the Mac. On the
phone on 2026-11-06 (main 96242d7), Castlevania, Zelda and Super Mario Odyssey 1.4.1
(prepared again) passed unattended start and Quit cycles, cold and warm; the hands-on
results under "What works today" were measured with the previous engine.

It is a clean break. After installing a build with this engine:

- Run Prepare again for every game. A package made by an earlier build is refused,
  because it records the engine it was made for. The library shows such a game as
  "Needs preparing" with a Prepare again button instead of Play; if its original was
  removed after the last Prepare, import it again first. Your imported game files, keys,
  saves and per-game settings stay as they are.
- A Prepare that passes verification removes the game's older prepared copies. Settings,
  Prepared games lists each copy with its date and a Current or Needs Prepare again tag,
  and can delete one. A warm-up card left over for an older package offers Prepare again,
  never Play. A game that is ready also keeps a quiet Prepare again button on its page,
  for a package that was prepared without its update.
- Expect first-play stutter again. Translated shaders and pipeline caches are kept per
  engine, so every game refills its caches the first time it is played.
- The code rewriter Prepare runs now follows the same upstream revision. For Zelda,
  Super Mario Odyssey and Castlevania its output is unchanged, so a package already
  prepared with this engine stays current; another game's earlier package is marked as
  needing Prepare again.

What the new engine brings, as far as it reaches Satchel: one Metal compile for a pixel
shader's alpha test and logic operation entry points instead of two, lower submit-thread
CPU cost (remembered uniform lookups, cached program facts), large memory pools and sparse
textures mapped only as far as a game uses them, render passes that continue when a game
names the same targets again, AV1 movies through a bundled decoder, the screen capture
services that Super Smash Bros. Ultimate needs to start a match, Bluetooth LE and
purchase-notification calls answered offline, SDK 19 file system calls, and several
texture format fixes (RGBA4 channel order, sRGB and linear twins, compressible pools).
The phone's shader translator follows the same pin (moved on 2026-11-05 from NX2Apple
`8227f80f`): float comparisons that treat a denormal as zero, uniform reads whose bank is
chosen at run time, storage words read whole, large compute shared memory and more. On the
Mac its output matches the engine's own tools for every shader compared from Zelda, Odyssey
and Castlevania. It changes shader output, so shader caches start empty once more; prepared
games do not need another Prepare for it. Switch 2 shaders are not translated. The details
and the steps for the next move are in [BUILDING.md](BUILDING.md#the-engine-pin).

## How it works

The route follows [NX2Apple](https://github.com/LNPANDY/NX2Apple) and nxnative (MIT):
run the game's own ARM64 code natively instead of interpreting or JIT compiling it.

1. **Import.** The user's XCI or NSP and `prod.keys` are read from Files. Keys go into a
   device-only Keychain item. Containers are copied locally and verified.
2. **Prepare.** On the phone: the base game and update are composed and authenticated,
   RomFS is written as one image, the startup modules are rewritten (SVC, TLS and x18 sites
   are redirected to the host), the result is written as a Mach-O dylib, and the dylib is
   signed with the user's own Apple Development certificate. The signed code is then loaded
   from a fresh file. Runtime JIT is not used and not planned.
3. **Play.** A generic Horizon OS host (services ported from NX2Apple and Ryujinx) runs the
   game, and NVN graphics are translated to Metal. Shaders are translated on the phone
   (Ryujinx's translator through .NET NativeAOT, then SPIRV-Cross to Metal Shading
   Language) and compiled with the phone's own Metal compiler, cached per game.

Unknown builds receive a starter profile from their metadata. The Mac diagnostic
path can now apply bounded memory observations without adding a title-specific
profile. Automatic phone feedback and generic graphics corrections remain open.
See [generic profile diagnostics](docs/generic-profiles.md).

The folders map onto those steps:

| Folder | What is in it |
|---|---|
| `App/` | SwiftUI app: library, import, Prepare, settings, pause menu, game reports |
| `NativePrepare/` | On-phone content pipeline, ARM64 rewriter, dylib writer, signer (zsign subset plus OpenSSL) |
| `NativeHost/` | Horizon host, services, session lifetime, display and controller overlays |
| `NativeShaders/` | Shader translation, runtime compilation and caching |
| `NativeGames/` | Reviewed per-game profiles and the Mac capture harness |
| `NativeApp/` | The single build driver, `NativeApp/build.py` |
| `Core/`, `Tools/`, `Guest/` | The earlier interpreter route (VIXL based), kept as a fallback and diagnostic |
| `Tests/`, `scripts/` | Component tests, UI journeys and the hygiene check |
| `Validation/`, `docs/` | Evidence records, design notes and screenshots |

`PLAN.md` and `GOALS.md` are the maintainer's working log. `AGENTS.md` holds the rules
that automated workers follow in this repository.

## Requirements

- An iPhone that can run iOS 27.1. Only the iPhone Duo (iPhone19,4) has been tested.
  Games need several gigabytes of memory; older phones will not get far.
- Your own Switch, your own `prod.keys`, and your own game dumps (XCI or NSP, plus
  updates and DLC as NSP). Satchel never downloads games, keys or firmware.
- An Apple developer account for signing, and one you can afford to lose: Prepare signs
  game code with your certificate, which Apple's agreement does not contemplate, and Apple
  can revoke it. Prepare needs an Apple Development identity (PKCS#12) imported on the
  phone; free accounts also expire every seven days. See [docs/install.md](docs/install.md).
- A Mac with Xcode to build and install (or SideStore or AltStore for a prebuilt IPA).

## Install, build, test

- [docs/install.md](docs/install.md): installing and keeping the app installed with
  SideStore, AltStore Classic or Xcode, the seven day limit, first launch.
- [BUILDING.md](BUILDING.md): building from source, the native dependencies, and the
  known gaps in reproducibility.
- [docs/testing.md](docs/testing.md): the Mac test scripts, simulator journeys for
  import cancellation, warm-up recovery and paused games, how to read a game's report
  and logs on the phone, and how to try a new game.
- [COMPATIBILITY.md](COMPATIBILITY.md): what has been tried, how well it runs, and how to
  add a game to the list.

Local distribution tooling exports a deterministic unsigned IPA with a hash manifest,
checks an ad hoc re-sign on the Mac, and generates a validated SideStore/AltStore
source using the existing screenshots. URLs remain placeholders. Real installer
capability preservation and refresh still need phone validation. See
[the distribution guide](NativeApp/Distribution/README.md). The release workflow is
manual-only and hard-disabled pending clean-runner dependency reproduction.

Opt-in GPU pass attribution and paired analysis are available in the
[graphics diagnostics](NativeHost/graphics/README.md#gpu-pass-cost-diagnostics).
An optional per-title MetalFX spatial presentation setting is available for reduced
render scales. It defaults to off, falls back to bilinear when unsupported, and
still needs phone image-quality and GPU-cost acceptance.
The graphics profile also provides paired GPU submission and CPU frame timing
summaries in `host.log`, with phone timing verification still pending.
They do not establish a full-resolution speedup or change the compatibility claims.

## Contributing

Contributions are welcome, from compatibility reports to engine work. Start with
[CONTRIBUTING.md](CONTRIBUTING.md) and the "help wanted" section of
[ROADMAP.md](ROADMAP.md). The most useful things right now:

1. Make the build reproducible on a clean Mac, and try the build with a second Apple account.
2. Try more games and file compatibility reports with the app's game report attached.
3. Horizon services: port missing commands from Ryujinx (MIT) when a game reports them.
   The [service harness](NativeHost/services/README.md) records Mac SDK call counts and
   tests save behavior with private copies.
4. Shader stalls: asynchronous pipeline builds and a better first-play warm-up.
5. Memory: keep long sessions and scene changes inside the phone's limits.

## Legal

Satchel contains no Nintendo code, games, firmware or keys. You must own the console and
the games you play, and dump them yourself. Do not open issues asking where to get
games or keys; they will be closed.

Satchel is released under the [MIT License](LICENSE). It reuses MIT, BSD, Apache 2.0
and zlib licensed components: NX2Apple and nxnative, Ryujinx (services, shader
translator and Spv.Generator), VIXL, zsign, OpenSSL, SPIRV-Cross, SPIRV-Tools,
MoltenVK, Capstone, LibHac, NSZ, Zstandard, libvpx, stb_vorbis, bcdec and Noto Sans.
Their notices are kept in `App/Resources/Notices`, `NativeHost/THIRD_PARTY.md`,
`Tools/VulkanBackend/Notices` and next to each vendored source tree. No GPL code is
used. Nintendo Switch is a trademark of Nintendo; this project is not affiliated with
Nintendo or Apple.

Mac memory research has 37 reviewed scene pairs across Castlevania, Odyssey and
TOTK. The pressure candidate remains unmerged while strict replay controls are
resolved; these readings do not establish phone savings. See
[the scene-memory evidence](Validation/memory-pressure-scenes-2026-11-04.md).
