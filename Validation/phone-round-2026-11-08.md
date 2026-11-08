# Phone round, 2026-11-08

Two unattended rounds on the maintainer's iPhone, all on the Zelda title screen. These
results ran on the phone and were not played: no game input was sent (the control file
received only `shot` and `quit` lines), so nothing here is a gameplay result.

| Part | Build | What ran | Outcome |
|---|---|---|---|
| 1 | main `a4efe7d`, signed, `com.dell.satchel` 1.0 (1), engine `f8a5618d5f50` | Translation ahead trial | Stopped early: the phone locked before the view upscale and memory soak steps |
| 2 | main `679f8e8`, signed, `com.dell.satchel` 1.0 (1) | 3D view upscale cost, one-hour memory soak | Complete |

The package was Zelda 1.4.3. The Odyssey base-only check was skipped because no base-only
package was on the phone, so that check stays open.

Saves were backed up before each part (73 files, 70.6 MB). At the end of part 1, 71 of 73
files were identical; the other two, Zelda's `CacheStorageKey.dat` pair, moved only their
timestamp and their bytes were identical. At the end of part 2 all 73 hashes matched, with
the same timestamp change on that pair. No Satchel process was left after either part.

## Launch flags

Every launch in the first two tables used `-SatchelDeveloperControls YES
-SatchelAutoLaunchPreparedGame YES -SatchelPreparedGame <package> -SatchelGraphicsProfile YES`.
The pipeline mode was the default, exact.

- Translation ahead: `-SatchelPipelineTrial <one tag per setting> -SatchelPipelineLanes 4
  -SatchelPipelineAhead 128`, plus `-SatchelShaderAhead 2` or `-SatchelShaderAhead 3`.
  The "off" runs leave that last flag out.
- View upscale: `-SatchelRenderScale 0.65`, plus `-SatchelUpscaler view` on the view runs.
  Docked (opmode 1). No trial tag.
- Memory soak: `-SatchelDeveloperControls YES -SatchelCyclePreparedGames <package>
  -SatchelCycleSeconds 3600 -SatchelCycleRounds 1 -SatchelMemoryProfile YES
  -SatchelGraphicsProfile YES` ([procedure](../docs/phone-cycle.md#title-screen-memory-soak-no-input)).

## Translation ahead (part 1, build a4efe7d)

Exact mode, lanes 4, ahead 128, in run order. Waits are milliseconds in the run's slow
frames; translation includes look-ahead translation.

| Run | Translation ahead | Cache | Worst frame (ms) | Frames over 100 ms | Translation wait | Library wait | Pipeline wait |
|---|---|---|---:|---:|---:|---:|---:|
| r1 | 2 | cold | 9,325 | 13 | 1,318 | 12,068 | 1,585 |
| r2 | 2 | warm | 415 | 1 | 88 | 20 | 7 |
| r3 | 3 | cold | 1,042 | 6 | 52 | 3 | 1,442 |
| r4 | 3 | warm | discarded | | | | |
| r4b | 3 | warm | 454 | 1 | 71 | 10 | 9 |
| r5 | off | cold | 1,239 | 7 | 371 | 156 | 1,354 |
| r6 | off | warm | 506 | 2 | 221 | 120 | 7 |

All runs: 0 shader failures, 0 skipped draws, missing-services empty.

Reading it: ahead 3 cold waited 52 ms for translation against 371 ms with it off, with a
worst frame of 1,042 against 1,239 ms; warm, the worst frame was 454 against 506 ms. That
is one run each, so a promising sign and not yet a basis for a default. It stays a
developer trial.

Caveats:

- r1 went first against a cold Apple system compiler cache and is not comparable. Later
  cold runs reused its libraries, so cold library times are not comparable either.
- r4 is discarded: the user's paused Odyssey warm-up resumed beside the auto-launched Zelda
  (a race between `WarmupJob.resumeIfInterrupted` and the auto-launch), reaching thermal 2.
  r4b repeats it. The warm-up then stopped paused at 1,856 of 146,652 units and needs a
  Resume tap. Part 2 ran on the build with the warm-up fix, and the warm-up's progress
  file stayed byte-identical throughout it.
- r6 counts only the frames before the user handled the phone.
- GPU p50 moved from 11.8 ms in r1 to r3 to 16 to 19 ms in r4b to r6. The settings do not
  explain it. The ahead 3 and off cold runs sit on either side of that shift.

## 3D view upscale cost (part 2, build 679f8e8)

Scale 0.65, docked, the game's 1600x896 view into 1920x1080. GPU time from
`graphics-frames.tsv`, in run order.

| Run | Path | GPU p50 (ms) | GPU p99 (ms) | Note |
|---|---|---:|---:|---|
| v1 | plain | 12.32 | 15.38 | First launch after install, 1,527 ms pipeline stall; excluded |
| v2 | view | 11.09 | 13.38 | |
| v3 | plain | 11.33 | 13.66 | |
| v4 | view | 11.06 | 13.36 | |

The resample is 0.2 to 0.3 ms cheaper at GPU p50 than the plain path, with the same frame
pacing: frame p50 33.4 ms and p99 36.1 to 36.5 ms in all runs. The resample line appeared
once per view run and never in plain runs. 0 shader failures, 0 skipped draws.

The look on the phone is still unjudged: the title screen animates, so these runs say
nothing about image quality.

## One-hour memory soak (part 2, build 679f8e8)

Zelda title screen, 3600 s, no input.

| Reading | Result |
|---|---|
| Cycle row | ok |
| Frames | 107,976 |
| Footprint | 2,233 MiB at 5 minutes, 2,243 at 30 minutes, 2,254 at the end |
| Metal allocation | flat at 1,948.6 MiB |
| Headroom | 5,958 to 5,937 MiB |
| Thermal state | 0, then 1 (fair) from 1620 s; never 2 |
| Frame time | p50 33.37 ms, p99 36.16 ms, 5 frames over 100 ms, worst 541 ms |
| Memory warnings, background events, shader failures, skipped draws, crash lines | 0 of each |
| Quit | 0.249 s |

`NativeHost/memory/soak_summary.py --skip 300` calls two counters steady:
`footprint_bytes` at 23.7 MiB per hour and `vm:malloc_charged_bytes` at 23.2 MiB per hour
(151 to 173 MiB, with 10-minute deltas of 3 to 4 MiB). Metal, textures and pipelines are
flat.

Malloc is a leak candidate not yet attributed, and it is harmless over an hour at this
headroom. Long play on the phone remains open: this was a title screen, where nothing new
loads. The next blocker is what allocates the steady malloc growth at the title screen.

The per-second memory file of this run is `Documents/zelda-memory.tsv`, not a file in the
title's runtime folder.
