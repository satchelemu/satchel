# Satchel 0.3: original downloadable homebrew game

## Target and provenance

TicTacToeNX by sh3rlock97, MIT-licensed, is the first supported game target.
The emulator loads the author's unchanged downloadable ARM64 NRO. No game
function is replaced by native code, and no addresses specific to this binary
are intercepted. Original game code computes all gameplay and pixel drawing.

- [Original project](https://github.com/sh3rlock97/tictactoe)
- [Pinned NRO download](https://raw.githubusercontent.com/sh3rlock97/tictactoe/893fe0d41b0855f2b41b8f1ca1b1e3f156e98b3e/tictactoe.nro)
- [Corresponding original source](https://github.com/sh3rlock97/tictactoe/blob/893fe0d41b0855f2b41b8f1ca1b1e3f156e98b3e/source/main.c)
- [License](https://github.com/sh3rlock97/tictactoe/blob/893fe0d41b0855f2b41b8f1ca1b1e3f156e98b3e/LICENSE)
- NRO size: 108,741 bytes.
- SHA-256: `20e058deda2267215f6197bcc63cbfe91347aa5ee2c8aa3f56fefcbc0825ae5a`.

The binary, license, and source URL are supplied in the phone's Files-visible
Satchel folder for testing. It is not built into the app bundle. The source
is compiled natively only in the external test reference, never in the product.

## Implemented

- Legacy libnx application startup, memory queries/protection, kernel result
  codes, a real virtual main-thread handle, nonblocking sleep scheduling.
- Additional ARM64 integer, exclusive/acquire/release, and SIMD instructions.
- Bounded service handles, legacy HIPC/CMIF messages, application state/events,
  read-only HID shared memory, and controller samples with queued short presses.
- Transfer memory, NV map/address-space bookkeeping, VI/Binder buffer queues,
  and scanout of actual guest block-linear RGBA buffers. Display swizzle and
  vertical transform are decoded; unused alpha is ignored for an opaque layer.
- A separate continuous player with bounded instruction slices, responsive
  pause/stop, no-frame watchdog, copied image ownership, and diagnostics.
- The entire app is landscape-only from launch in build 4, including library
  and file details. Guest pixels fit the available viewport without cropping,
  with overlaid controls. iOS 27 uses window-scene orientation callbacks.
- File signatures are recognized before reading a whole large file. NRO loads;
  NSP/PFS0, XCI, NSO and ZIP receive specific unsupported/extraction guidance.
  Renaming a valid NRO does not prevent recognition, and renaming a container
  does not convert it into a supported executable.

## Host validation

`Tests/game_execution.c` executes the pinned game binary and compares every
RGB byte of twelve 1280×720 frames with a native build of the corresponding
unmodified author source. The input trace covers an empty board, selection
movement, and moves by both players. Only demonstrably black display-setup
frames are accepted before the first game frame. Alpha is excluded because
this display layer is opaque. Plus then exits through the game's own cleanup
and returns to the loader with exit code zero.

All twelve reference frames and clean exit pass. The run executes about 2.9
billion guest instructions and takes about 20 seconds of host CPU time in the
validation harness. This is roughly 1.7 seconds per game frame on this Mac,
not a prediction of iPhone performance.

Independent instruction tests compare 80 additional register results with the
physical Mac ARM64 CPU. Existing 16-register architecture tests, NRO malformed
input/protection tests, kernel permission/query/sleep tests, format tests, and
Horizon IPC/NV/display/input tests pass under AddressSanitizer and
UndefinedBehaviorSanitizer. Player input-state tests pass ThreadSanitizer.
The original 1,800-frame full-framebuffer/state regression suite also passes.

## Physical iPhone Duo validation

The user confirmed the phone was unfolded and unlocked. The actual iPhone test
imports the pinned downloaded NRO through Files, starts Play, checks the empty
board and three exact native-reference RGB hashes after on-screen A/Right/A,
pauses/resumes, stops without a fault, and returns to the file detail view.
This UI test passes with zero failures. Four additional physical-device
regression tests pass: cloud round trip, local import/persistence, nx-hbmenu
diagnostic, and the original demo controls/device check. It also verifies a landscape game window,
full available image width, and retained landscape after a portrait request.

The open inner display is 2853×2007 pixels (951×669 points); the uncropped game
image is 951×535 points. The inactive cover display remains separately listed
by devicectl, so screenshots explicitly target the active inner display.
`game-iphone.png`, `game-iphone-run.json`, and `game-ui-summary.json` retain the
proof. The final measured frame interval was 1.242 seconds (about 0.8 FPS), with
12 presentations and 2.5 billion interpreted instructions before the user-stop test.
That is enough to try this turn-based game; action-game performance is not ready.
The application is development-signed and runs without JIT or a debugger.

To play, open **Satchel → tictactoe → Play homebrew**. The white border is the
selected cell; use the D-pad and A. Yellow and green represent the two players.
Plus exits through the game's own cleanup; the toolbar Stop ends the interpreter.
To use a fresh download, save the author's NRO in iCloud Drive and use
**Import from Files → Browse → iCloud Drive**. Import keeps the original intact.

## Reproduce

Download the pinned NRO and matching source from the author, then:

```zsh
zsh scripts/test_game.sh /absolute/path/to/tictactoe.nro /absolute/path/to/main.c
zsh scripts/test_arm64_extra.sh
zsh scripts/test_kernel.sh
zsh scripts/test_horizon.sh
zsh scripts/test_formats.sh
zsh scripts/test_homebrew.sh
zsh scripts/validate.sh
```

Source reference stubs are under `Tests/NativeSwitch`; they supply pixels and
scripted input for independent testing and are not included in the app target.
Build with XcodeGen/Xcode as described in the main README. No runtime package
or library dependency was installed.

## Compatibility boundaries

This is one verified legacy software-rendered homebrew target. It does not
establish compatibility with arbitrary Switch homebrew or commercial games.
There is no GPU command execution, shader translation, emulated audio, general
thread scheduler, complete Horizon service environment, filesystem/RomFS
integration, or NSP/XCI content loader. Unsupported operations fault explicitly.

NSP is not synonymous with "update": packages can contain a base application,
patch, or DLC. XCI describes a game-card container. These commonly wrap NCA
content and require container parsing, content metadata, executable/data
loading, and update composition beyond the NRO route. See [format research](formats.md).
No encryption keys or Nintendo firmware are included. No App Store submission
or approval is implied by this device build.

The older nx-hbmenu 3.6.1 compatibility probe now reaches 32,048 instructions and
stops at `Unsupported ARM64 instruction: 0x3cc0827f at PC 0x2fb38`. Its menu is
still not supported. Historical 0.2 reports retain their original earlier boundary.

## Display follow-up: build 4

The before screenshot showed a narrow portrait-sized library centered on the
unfolded display. Launch-time Info.plist orientations and both delegate masks
now agree on landscape. Game entry/exit no longer requests geometry changes.
Plain lists with explicit margins use the wider library/detail viewport, and
file details present full-screen instead of inside a centered sheet.

The on-device journey passes after launching with portrait requested: landscape
launch, a library row wider than 80% of the window, full-window details, full-width
game frame, portrait-lock check, actual A/Right/A game-state checks, pause/resume,
stop, and landscape retained on returning through details to the library.
No interpreter changes were needed. Build and one affected end-to-end device
test passed. See `layout-ui-summary.json`, `layout-before.png`, `layout-after.png`.
The original guest image retains its aspect ratio and any background it draws.
