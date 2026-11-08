# Zelda title-screen malloc growth, 2026-11-08

Lane `title-malloc`, based on main `779ca2b`. Mac runs only; nothing was installed or run
on the phone. The cause is not established: the Mac does not reproduce the growth, so this
note records what is ruled out and the phone check that comes next.

## The phone finding

One hour at Zelda's title screen with no input (main `679f8e8`, flags
`-SatchelMemoryProfile YES -SatchelGraphicsProfile YES`, 107,976 frames): DM_MEMSTAT malloc
charged went from 151 to 173 MiB, 3 to 4 MiB every ten minutes, with Metal flat at 1948.6 MiB.
Footprint in `scene-memory.tsv` has byte resolution every 60 frames. After 300 seconds it
rises by one or two 16 KiB pages in most samples (890 of 1649 are +16 KiB, 114 are +32 KiB,
323 are 0), on top of a roughly 1 MiB up and down toggle that nets out. The small steps
alone come to 195, 191 and 185 bytes per frame over the three thirds of the run, so the
rate is constant. With the toggle excluded, every phase of the 300-frame and 600-frame
cycles (the period of the engine's GPU profile and log summaries) averages 13.1 to 14.1 KiB
per 60-frame sample, so the growth is not tied to those events.

This is the only long idle phone run with memory data. The three other phone logs with
footprint rows past five minutes grow by 1 to 10 GiB per 108,000 frames, so they are not
comparable and do not separate the flags from the app.

## Mac reproduction attempts

`NativeHost/memory/soak_mac.py TOTK` on the scene harness, `--no-input` (no confirmations and
no movement), private caches (cold for run A, warm for B and C). Screenshots show the title menu with its animated
background. Heap diffs compare `heap` totals by class and size between the named seconds.
At the phone's rate the 1800-second span would add about 11.6 MiB and the 1400-second spans
about 9 MiB.

| Run | Harness and options | Seconds | Frames | malloc charged | Live heap diff |
|---|---|---:|---:|---|---|
| A | offscreen, no audio, `--app-profile` | 2700 | 80,280 | 229 to 229 MiB | 600 to 2400 s: +80 nodes, +14,448 bytes |
| B | A plus `--window --audio` (display link, real output path, muted) | 2100 | 62,700 | 223 to 224 MiB | 600 to 2000 s: -1,056 nodes, -306,128 bytes |
| C | B plus the app's service overlay (`build_mac.py --services`) | 2100 | 62,100 | 224 to 225 MiB | 600 to 2000 s: +56 nodes, +4,736 bytes |

`soak_summary.py` calls malloc charged, footprint and Metal flat in all three. Its one steady
counter is `g_deferred.count` in run B (58 to 95 entries, a single late rise of a release
queue; falling or flat in A and C, and falling on the phone). Run B's negative diff is mostly
Objective-C method caches shrinking. Run C links the service overlay, but this harness does not
show whether the title reaches its code every frame.

## Ruled out

- The engine writers that `-SatchelGraphicsProfile` and `-SatchelMemoryProfile` turn on, as
  they run on the Mac: frame TSV, shader events, pipeline skip stats, boot profile, slow-frame
  ring, wait ledger, GPU profile every 300 frames, the scene-memory sampler and DM_MEMSTAT.
  On the phone they could still behave differently; run A is not evidence about iOS.
- The per-frame guest and render path at the title screen, shared with the phone: the same
  frames allocate nothing that stays live on the Mac.
- `CAMetalDisplayLink` presentation and the AudioUnit path, at the Mac's level (run B).
- By reading, bounded or not per-frame: the cycle timer's rows (one per visit), the controller
  monitor's history (12 lines), the service report and missing-command tables (1024 entries
  each), the thermal read (once a second) and the screen-rate query (once).

## Not covered

The session and controller overlays; the iOS front end (`front_ios.m`, `DmGamepad.m`, UIKit
timers); RemoteIO and the audio session; the app's `shader-capture` setting; iOS system
libraries. Because the growth is about 190 bytes per frame, an allocator's per-frame
autorelease or cache on one of these paths would fit, but no such site was found. Nothing
here says which it is, or whether it is host code, a system framework or the flags on iOS.

## Next check, on the phone

Neither step has been run. Step 2's commands have not been tried on this project.

1. The same launch as the soak without the two profile flags, for 1800 seconds:
   `-SatchelDeveloperControls YES -SatchelCyclePreparedGames <package> -SatchelCycleSeconds 1800 -SatchelCycleRounds 1`.
   DM_MEMSTAT comes from the reviewed profile, so `Documents/zelda-memory.tsv` is still written.
   From 300 to 1800 seconds the malloc column gains about 10 MiB if the growth does not
   depend on the flags; flat means the flags cause it on iOS.
2. If it still grows, attach the Allocations instrument for ten minutes and compare live
   bytes by responsible library and caller:
   `xcrun xctrace record --device <udid> --template Allocations --attach Satchel --time-limit 600s --output <file>.trace`.
