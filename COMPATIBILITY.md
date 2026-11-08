# Compatibility

Games that have been tried in Satchel, how far they get, and how well they run. Every
entry is a real test on a real phone unless the row says otherwise. Add a row by opening
a [compatibility report](.github/ISSUE_TEMPLATE/compatibility_report.md) or a pull
request.

Simulator UI journeys use synthetic containers and sessions. They do not add gameplay
or shader-performance evidence to this table. Per-title FPS, render-scale and pipeline
choices require a fresh game launch and do not establish that a title supports a forced
frame rate. Reset restores its bundled title default. Save archive checks establish title
binding and byte integrity, not gameplay correctness after a restore. Duo posture
journeys observe synthetic frames and input callbacks; real hinge transitions and
native game presentation still need the phone and do not change the ratings below.

The shader warm-up is opt-in (since 2026-11-06 Prepare does not start it; the game page's
Warm up does). Warm-up scheduling and MSL transform changes are checked separately on the Mac.
In the background iOS may stop a long warm-up; it resumes when the player returns, and a large
game only gets far with Satchel open, the screen on and the phone charging.
They do not change these phone gameplay ratings or establish a phone speedup.
See [warm-up measurement steps](NativeShaders/WarmupSpeed.md).

Test hardware so far: one iPhone Duo (iPhone19,4, A20 Pro), iOS 27.1, landscape.

Engine update of 2026-11-05: the native engine moved from nxnative `bbc3b737` to
`f8a5618d`. Every row below was measured by hand with the previous engine. On the phone on
2026-11-06, on the new engine (main 96242d7), Castlevania and Zelda passed the unattended
cold and warm cycle and Super Mario Odyssey 1.4.1, prepared again, passed 6 of 6 visits at
60 FPS; those were unattended runs, so the hands-on checks behind each row are not
repeated yet. A build with the new engine refuses packages prepared before it and starts with
empty shader caches, so each game has to be prepared again (the library marks it as
needing Prepare instead of offering Play) and will stutter on first play until its caches
refill. Castlevania's reviewed profile now follows upstream's checked
profile where the two overlap (files, memory, depth mode, shader split); its tested phone
settings (30 FPS cap, handheld mode) are unchanged. See
[BUILDING.md](BUILDING.md#the-engine-pin). The on-phone rewriter's later move to the same
upstream revision leaves the rewritten code of Zelda 1.4.3, Odyssey 1.0.0 and 1.4.1 and
Castlevania 1.0.0 unchanged (checked on the Mac), so they need no further Prepare. Mario
Kart 8 Deluxe was not checked: a package of it from before that move needs Prepare again.
The phone's shader translator then moved to the same engine pin; its shader caches start
empty again, prepared packages stay valid
([BUILDING.md](BUILDING.md#the-shader-translator-pin)). Castlevania, Zelda and Odyssey 1.4.1
ran cold and warm with it on the phone (2026-11-06) with no shader failures; no row's rating
was measured with it.

Super Mario Odyssey 1.0.0 prepared without its update crashed at startup on the phone
(2026-11-05): its SDK hands a game play recording buffer to a system applet the host does
not run. The host now takes those calls over and implements the transfer memory calls
behind them. On the Mac with the new engine, the 1.0.0 base alone boots and passes its
frame 300, 1200 and 3000 checkpoints with no missing service; this is not yet checked on
the phone, so the table still lists Odyssey 1.4.1 only. Mac runs on the new engine have
no translated shader libraries yet, so they check boot progress, not the picture.

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
| The Legend of Zelda: Tears of the Kingdom | 1.4.3 | 0100F2C0115B6000 | 85 | Phone (shaders translated at first use) | Yes | Yes, saves load | 28 to 30 at the 30 cap, 0.65 render scale; 59 to 60 at 0.5 | Two hour session without a crash. Remaining hitches of 100 to 500 ms are first-time pipeline builds; the pipeline archive keeps them for the next launch (the recorded list itself is read only by the opt-in launch prewarm). 100% render scale at 30 FPS is the open target. | 2026-11-03 |
| Super Mario Odyssey | 1.4.1 | 0100000000010000 | 80 | Phone | Yes | Yes, into the Cap Kingdom | 60.0 measured at the game's own 60 | Opening movie and gameplay at 60. First play stutters on new shaders. The first "Look Around" hint only clears with a real controller, not scripted input. | 2026-11-03 |
| Castlevania: Belmont's Curse | 1.0.0 | 0100BA4026E94000 | 60 | Phone (generic profile plus one memory rule) | Yes | Yes, intro, Start menu and opening area | 30 steady (p99 33.3 ms over 300 frames, zero dropped) once warm | Unity IL2CPP title. A cold scene can cost over a second while 60 to 90 pipelines build. The XCI also contains a gallery app; Prepare must select the game's program ID. | 2026-11-03 |
| Mario Kart 8 Deluxe | update v1376256 | 0100152000022000 | 45 | Phone | Yes | Yes | Not measured yet | Boots and plays; synchronous shader builds stall the frame and memory grows between scenes. Quit and a second launch work. Booster Course Pass DLC imports. | 2026-11-02 |

The opt-in GPU pass diagnostics do not change these phone results. Full-resolution
Zelda and Odyssey have Mac gameplay attribution, but optimization acceptance still
requires complete target counters, paired comparisons and matched image checks.
No rendering optimization is enabled by these diagnostics. The optional per-title
MetalFX spatial presentation setting defaults to off and does not change these
phone ratings. It still needs matched phone captures and GPU-cost acceptance. The same
holds for the optional edge smoothing present (`edge`) and the optional 3D view resample
(`view`), which are also off by default.
Opt-in frame GPU timing uses command-buffer feedback rather than the OS power
counter. Its CPU/GPU limit labels are diagnostic inferences, pending phone checks.

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

Mac long-play memory soaks on 2026-11-07 (main `6103cee`, the app's runtime shader
path, cold caches) ran Zelda for 60 minutes and Odyssey and Castlevania for at least
30 minutes each. Zelda's footprint rose in steps with new scenery, from 4.09 to 4.27 GiB, then
12 MiB over its last 30 minutes; Odyssey stayed at 3.86 GiB and Castlevania at
2.44 to 2.46 GiB (a dialogue scene and a separate 32-minute gameplay loop) after their
first loads. No host
cache grew steadily. See [the soak evidence](Validation/memory-soak-2026-11-07.md).
These are Mac readings and do not change a phone rating.

The same new host also passes nine 120-second visits in one Mac process:
Castlevania twice, Odyssey twice, Zelda twice, then Castlevania, Odyssey and Zelda.
All nine reach visible gameplay, stop and retire their guest resources. The final
scan finds zero leaks, with zero shader/fatal/save errors and unchanged original
saves. The largest guest join is 0.037 seconds. No post-session launch hang appears
in this matrix; the visits include startup and do not represent two full minutes
of gameplay each. On the phone on 2026-11-04, the developer-only [Quit and cycle procedure](docs/phone-cycle.md)
played Castlevania, Odyssey and Zelda twice each in one process through the menu's Quit:
six of six visits ended cleanly, stops took 0.15 to 0.32 seconds, and footprint at Quit
was no higher in the second round. The visits had no controller input, so this covers
starting and stopping, not gameplay, and does not change any compatibility rating.
That cycle ran on the previous engine. On the f8a5618d engine (2026-11-05), Quit in
Castlevania aborted the app: the engine and Satchel's session reset both freed the copy of
an empty `game_env`. A fix with a Mac regression check followed (commit bcbef50). On
2026-11-06 the unattended phone cycle ran on that build, Castlevania then Zelda, 60 seconds
each, 2 rounds: 4 of 4 runs ended cleanly, stops took 0.07 to 0.15 seconds, the last window
had no dropped frames and no missing services, and the same app process stayed alive
through every Quit. On the later main 96242d7 (which also carries the new shader
translator), also on 2026-11-06, Castlevania and Zelda passed 4 of 4 unattended cold and
warm visits with Quit returning home each time, and Super Mario Odyssey 1.4.1, prepared again with its update,
passed 6 of 6 visits at 60 FPS with a warm p99 of 16.67 ms. The cycles were driven from the
Mac: Quit and Library from the pause menu by hand have not been repeated on the new engine.

## Import and Prepare checks

The native importer changes documented in [the validation guide](docs/importer-validation.md)
cover compressed PartitionFS, key-area AES-XTS, a second bucket-tree index level,
skip-layer encryption (types 5 and 6), SHA3 hash layers (iOS 26 or later) and IVFC
trees of 3 to 7 layers. Synthetic format checks and Mac package comparisons do not
change the phone ratings above; no tested game uses the newest layouts. The guide's
format table lists the remaining explicit gaps. Phone progress, signing and loading
still need the orchestrator's checks for this change.

## Homebrew and diagnostics

### Generic runtime profiles on the Mac, 2026-11-07

Mac checks on the f8a5618d engine, not phone results; they do not change the ratings
above. Each game was prepared fresh on the Mac with `--generic-profile` and the reviewed
profile resources emptied (RomFS bytes discarded after their digest matched the existing
image), then run on the Mac pipeline host with the phone shader translator, a private save
copy and scripted A presses. The learned settings were derived from each run's own log by
the app's feedback code (`App/NativeBootFeedback.swift`), with no title lookup.

| Game | Starter profile alone | Learned settings and result |
|---|---|---|
| Super Mario Odyssey 1.4.1 | Cap Kingdom gameplay ("Throw Cap" tutorial) at frame 3,600; no missing service, no crash | The game uses conditional rendering, so the next launch evaluates it (`render_cond`); the same Cap Kingdom scene renders with no visible change through frame 3,666 |
| Castlevania: Belmont's Curse 1.0.0 | Crash (signal 11) after 107 frames, with 4.25 of 4.38 GiB of alias space mapped | `app_pool` 1 GiB, the reviewed value; reaches the opening boat dialogue at frame 3,600 with no crash or missing service |
| Zelda: Tears of the Kingdom 1.4.3 | `SetHeapSize OOM: req 0x84600000 > cap 0x8000000`, then a crash (signal 10) before the first frame | First: `mem_alias` 0x98000000 (a 2.125 GiB heap, the reviewed value): Great Sky Island gameplay with the HUD, but the 3D scene draws as black silhouettes. That launch shows conditional rendering and, with the host's texture-write watch (added later the same day), 14 to 16 textures whose GPU copy missed CPU writes, so the second launch adds `render_cond`, `tex_flush_upload` and `tex_dirty_upload` together: Great Sky Island with the reviewed profile's tall grass and plants through frame 3,631, as close to the reviewed run as two reviewed runs are to each other. (`tex_bc_cpu` matters only on GPUs without BC textures.) Before the watch, only `render_cond` was learned and small plants drew dark with most grass missing |

None of the three games reported a missing service or SVC. With the texture-write watch,
Odyssey and Castlevania count 0 textures and learn no upload switch (details in
[the diagnostic contract](docs/generic-profiles.md)). The 2026-11-04 checks on the
previous engine reached the same memory values. Reviewed profiles remain overrides. See
[the diagnostic contract](docs/generic-profiles.md).

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
   services with their command numbers, the crash address if any, shader failures and,
   for a game without reviewed settings, the memory settings learned from its last
   launch. Tap Copy diagnostics and paste the text into your compatibility issue. The full log is at
   `Documents/NativeRuntime/<title ID>/host.log` and `missing-services.json`, reachable
   through Finder or `xcrun devicectl device copy from`.

A report with a missing service list is more valuable than a rating: each service ported
helps every game that needs it.

Developer pipeline experiment: `-SatchelPipelineMode async` uses a 4 ms optional
wait budget and equivalent ready fallbacks. It does not remove required first-use
waits, change the default exact mode, or establish new phone compatibility. See
[graphics validation](NativeHost/graphics/README.md#async-validation).

The opt-in `skip` pipeline mode can omit a terminal draw in the current submission while compilation
continues. A missing final layer or flicker is possible. Exact remains the default;
the Game settings panel saves the choice per title for the next launch. This
experiment adds no phone compatibility or gameplay claim to the table above.
The opt-in `bounded` mode starts translation and base libraries when a program is bound and
changes no draw; its Mac measurements are in `NativeHost/ShaderStalls.md`. It adds no phone
compatibility claim either. The opt-in launch prewarm (`-SatchelPipelinePrewarm YES`) builds the
pipelines a device recorded before the first frame; it does nothing on a game's first launch, was
measured on the Mac only, and adds no phone claim. The opt-in translation ahead
(`-SatchelShaderAhead <lanes>`) changes when shaders are translated, never which; it was measured on
the Mac only and adds no phone claim.

## Controller acceptance

Controller remapping and navigation do not change the game ratings above. The controller
checks use synthetic GameController snapshots, not gameplay. Plus and Minus remain
normal game inputs until both are pressed for the menu chord; Home stays reserved.
Physical Xbox, DualSense and Pro Controller acceptance, audible volume changes and touch layout on the phone
remain separate checks in [the test guide](docs/testing.md). The first physical pad tried
(an Xbox-style Bluetooth controller, 2026-11-06) did nothing in the app or in Castlevania;
Settings > Controllers now shows what iOS reports for each pad to diagnose that on the phone.

The unmerged memory-pressure candidate has separate Mac scene-memory evidence
for Castlevania, Odyssey and TOTK. It does not change these phone ratings or prove
exact pressure invariance. See
[the measured scenes and replay limits](Validation/memory-pressure-scenes-2026-11-04.md).
