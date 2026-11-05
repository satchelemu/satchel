# Compatibility

Games that have been tried in Satchel, how far they get, and how well they run. Every
entry is a real test on a real phone unless the row says otherwise. Add a row by opening
a [compatibility report](.github/ISSUE_TEMPLATE/compatibility_report.md) or a pull
request.

Simulator UI journeys use synthetic containers and sessions. They do not add gameplay
or shader-performance evidence to this table. Per-title FPS, render-scale and pipeline
choices require a fresh game launch and do not establish that a title supports a forced
frame rate. Reset restores its bundled title default. Save archive checks establish title
binding and byte integrity, not gameplay correctness after a restore.

Test hardware so far: one iPhone Duo (iPhone19,4, A20 Pro), iOS 27.1, landscape.

## Ratings

The rating is a percentage of "fully working", judged on five things that each count
roughly equally: it prepares on the phone without a Mac, it boots to the title screen,
it reaches gameplay, it stays at its target frame rate, and it stays stable in a long
session (saves, scene changes, quit and relaunch).

| Band | Meaning |
|---|---|
| 90 to 100 | Playable start to finish at the game's own frame rate, no known blockers |
| 70 to 89 | Playable for long stretches; stutters, visual defects or rare crashes remain |
| 40 to 69 | Reaches gameplay but with stalls, memory growth or missing features that interrupt play |
| 10 to 39 | Boots to a title screen or menu; gameplay is not reached or not stable |
| 0 to 9 | Fails at import, Prepare or boot |

Frame rates are measured with the in-game FPS counter or the host's own frame log
(`Documents/NativeRuntime/<title ID>/host.log`), not estimated.

## Tested games

| Game | Version | Title ID | Rating | Prepared on | Boots | Gameplay | Frame rate | Notes | Last tested |
|---|---|---|---|---|---|---|---|---|---|
| The Legend of Zelda: Tears of the Kingdom | 1.4.3 | 0100F2C0115B6000 | 85 | Phone (shaders translated at first use) | Yes | Yes, saves load | 28 to 30 at the 30 cap, 0.65 render scale; 59 to 60 at 0.5 | Two hour session without a crash. Remaining hitches of 100 to 500 ms are first-time pipeline builds; a recorded pipeline list prewarms them on the next launch. 100% render scale at 30 FPS is the open target. | 2026-11-03 |
| Super Mario Odyssey | 1.4.1 | 0100000000010000 | 80 | Phone | Yes | Yes, into the Cap Kingdom | 60.0 measured at the game's own 60 | Opening movie and gameplay at 60. First play stutters on new shaders. The first "Look Around" hint only clears with a real controller, not scripted input. | 2026-11-03 |
| Castlevania: Belmont's Curse | 1.0.0 | 0100BA4026E94000 | 60 | Phone (generic profile plus one memory rule) | Yes | Yes, intro, Start menu and opening area | 30 steady (p99 33.3 ms over 300 frames, zero dropped) once warm | Unity IL2CPP title. A cold scene can cost over a second while 60 to 90 pipelines build. The XCI also contains a gallery app; Prepare must select the game's program ID. | 2026-11-03 |
| Mario Kart 8 Deluxe | update v1376256 | 0100152000022000 | 45 | Phone | Yes | Yes | Not measured yet | Boots and plays; synchronous shader builds stall the frame and memory grows between scenes. Quit and a second launch work. Booster Course Pass DLC imports. | 2026-11-02 |

The opt-in GPU pass diagnostics do not change these phone results. Full-resolution
Zelda and Odyssey have Mac gameplay attribution, but optimization acceptance still
requires complete target counters, paired comparisons and matched image checks.
No rendering optimization is enabled by these diagnostics.

The Mac long-session harness is separate from the phone ratings above. After
ownership fixes, Castlevania passed an uninterrupted 1801.115-second gameplay
interval with zero detected leaks, shader/fatal/save errors or remaining guest
resources. Guest join took 1.377 seconds. The shared-Mac average was 27.219 FPS;
this was not an isolated performance benchmark. The 60-minute, 12-stop switching
loop also passed zero-leak acceptance.

A matched Castlevania indoor-entry replay reported 13 shader diagnostics on both
the pre-merge main baseline (`da5830f`) and the fixed host. An earlier Zelda run
timed out at guest join, and a separate completed 30-minute interval peaked at 14.3 GiB
live footprint on the Mac. These observations do not raise a phone rating or
establish phone memory acceptance. See [session validation](NativeHost/session/README.md).

The round 4 retained-cache Zelda retry on main `cd6fbf9` passed 1800.006
uninterrupted gameplay seconds, zero leaks, zero shader/fatal/save errors and full
guest retirement. Guest join took 0.521 seconds. Its shared-Mac average was
27.387 FPS and peak footprint 11.605 GiB. The historical timeout did not reproduce;
this is not evidence of its cause or a fix, and the live footprint is not a phone
memory result.

The same new host also passes nine 120-second visits in one Mac process:
Castlevania twice, Odyssey twice, Zelda twice, then Castlevania, Odyssey and Zelda.
All nine reach visible gameplay, stop and retire their guest resources. The final
scan finds zero leaks, with zero shader/fatal/save errors and unchanged original
saves. The largest guest join is 0.037 seconds. No post-session launch hang appears
in this matrix; the visits include startup and do not represent two full minutes
of gameplay each. Phone Quit/relaunch remains unverified by this worker.

## Homebrew and diagnostics

### Mac service regression, 2026-11-04

Three 330-second baseline runs used existing packages and private save copies.
Castlevania reached its opening area after the boat dialogue. Odyssey reached
Cap Kingdom, saved to a second slot and reloaded it. Zelda reached Great Sky
Island and saved; its baseline reload was started too late to verify before the
time limit. All three runs had zero missing IPC entries, unresolved SDK calls,
aborts or crashes. SDK counters recorded 10 no-op `FlushFile` calls in Odyssey
and eight in Zelda, which motivated the round 9 backing-file flush correction.

After the correction, all three repeated 330-second runs passed with zero missing
IPC entries, unresolved SDK calls, aborts or crashes. Castlevania made four save
commits, Odyssey 12 and Zelda four. Odyssey's six and Zelda's eight `FlushFile`
calls all reached the new implementation and succeeded. All three private save
seeds retained their original hashes.

Visual checks confirmed Castlevania's boat-to-opening-area transition, Odyssey's
new File 2 marked as playing after reload, and Zelda's loading screen followed by
Great Sky Island gameplay. Movement worked after both reloads. The Odyssey and
Zelda checks cover reloading the same area, not travel to another kingdom or
region. Missing IPC entries were already zero before the correction, so these
runs demonstrate removal of an exercised SDK no-op, not fewer missing IPC rows.

These are Mac observations, not new phone ratings. The diagnostic renderer used
existing compiled shaders for Castlevania and an optional in-memory compiler for
the retained Odyssey and Zelda Metal source caches. An earlier fresh-save Odyssey
run exited with signal 11 before that option was enabled; its cause is not
established. Seeded runs with the option reached visible gameplay. Frame-gate
success alone is not evidence of gameplay or scene coverage.

### Interpreter diagnostics

The interpreter route (`Core/`, `Tools/CPUProbe`) still runs the MIT-licensed
[TicTacToeNX](https://github.com/sh3rlock97/tictactoe) NRO at about 0.8 FPS and the
original brick-breaker demo. These are diagnostics, not the way games run. They are
hidden behind Developer controls (Settings, About, tap the Satchel name seven times).

## How to test a game

1. Import your own `prod.keys` (Settings, Console keys), then the base XCI or NSP and
   its update from Files. Satchel shows the title and artwork once keys and base match.
2. Import your Apple Development identity in Prepare game (a `.p12`; the passphrase is
   typed on the phone and never stored).
3. Run Prepare. It keeps going in the background. Expect 30 to 60 minutes for a large
   game; the shader step is the long one.
4. Play. Open the pause menu and turn on the FPS counter. Play for at least ten minutes,
   change scenes, save and reload, quit and relaunch.
5. Open Prepare, Game diagnostics for the game's report: the stage reached, missing
   services with their command numbers, the crash address if any, and shader failures.
   Copy that report into your compatibility issue. The full log is at
   `Documents/NativeRuntime/<title ID>/host.log` and `missing-services.json`, reachable
   through Finder or `xcrun devicectl device copy from`.

A report with a missing service list is more valuable than a rating: each service ported
helps every game that needs it.

## Controller acceptance

Controller remapping and navigation do not change the game ratings above. The controller
checks use synthetic GameController snapshots, not gameplay. Plus and Minus remain
normal game inputs until both are pressed for the menu chord; Home stays reserved.
Physical Xbox, DualSense and Pro Controller acceptance, audible volume changes and touch layout on the phone
remain separate checks in [the test guide](docs/testing.md).
