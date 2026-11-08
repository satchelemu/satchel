# Mac long-play memory soaks, 2026-11-07

Lane `memory-soak`, based on main `6103cee` (engine pin f8a5618d, shader chain
26dc2fa9477c). Mac only: nothing was installed or run on the phone, and no phone data
was read. These are Mac measurements of the host's own memory; they do not establish
phone memory limits or behaviour under iOS memory pressure.

## What ran

`NativeHost/memory/soak_mac.py` on the scene harness built by
`NativeHost/memory/build_mac.py` (display, graphics policy and the app's memory
overlays, plus the Mac-only cache census). The app's current shader path: the phone
translator at runtime with an empty seed library and PipelineSettings.m's default
exact pipeline policy, translation and pipeline caches starting cold. The existing
packages under `~/SatchelBuild/anygame` were read in place; private copies of the
session soak's Mac save backups seeded each run. Inputs are the session soak's: 90
seconds of confirmations, then its balanced movement loop every six seconds.
Footprint is `TASK_VM_INFO.phys_footprint` from the scene-memory sampler; Metal is
the device's `currentAllocatedSize`. Other lanes shared the Mac, and its compressor was
active, so frame rates are not benchmarks; footprint includes compressed pages.

| Run | Seconds | Frames | Translations | In-play max frame gap | Leak scan (live, at the end) |
|---|---:|---:|---:|---:|---|
| Zelda, Great Sky Island | 3607.5 | 97,980 | 958 | 10.1 s (first load 91.3 s) | 16 leaks, 98,528 bytes |
| Odyssey 1.4.1, Cap Kingdom | 1804.0 | 105,060 | 437 | under 5 s | 74 leaks, 4,688 bytes |
| Castlevania, boat dialogue | 1802.8 | 53,520 | 162 | under 5 s | 0 leaks |
| Castlevania, warehouse gameplay | 2822.7 | 84,300 | 214 | under 5 s | 0 leaks |

Screenshots every five minutes were reviewed: Zelda stays in one grove of Great Sky
Island with Link moving and turning, Odyssey in one spot of Cap Kingdom, and
Castlevania's first run on one dialogue screen of its boat scene (the 90-second
confirmation phase does not read through that dialogue, so that run is an animated
idle scene, not gameplay). Its second run used `--confirm-seconds 900`: confirmations
read through the dialogue to the dock, and its 32-minute movement loop (from 905
seconds) shows Trevor moving and attacking in one indoor warehouse room. No run reported a shader, pipeline, fatal or crash diagnostic beyond the
two expected empty-seed notes.

## Footprint over time (MiB)

| Run | 300 s | 600 s | 900 s | 1200 s | 1800 s | 2400 s | 3000 s | 3600 s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Zelda footprint | 4091 | 4101 | 4118 | 4121 | 4255 | 4262 | 4265 | 4267 |
| Zelda Metal | 2354 | 2358 | 2362 | 2363 | 2488 | 2488 | 2493 | 2494 |
| Odyssey footprint | 3862 | 3864 | 3865 | 3864 | 3864 | | | |
| Odyssey Metal | 2372 | 2372 | 2372 | 2372 | 2372 | | | |
| Castlevania dialogue footprint | 2435 | 2435 | 2437 | 2437 | 2437 | | | |
| Castlevania dialogue Metal | 652 | 652 | 654 | 654 | 654 | | | |
| Castlevania gameplay footprint | | 2396 | 2451 | 2455 | 2455 | 2456 | | |
| Castlevania gameplay Metal | | 619 | 639 | 639 | 640 | 640 | | |

Odyssey's first-load peak (4330 MiB at 120 s, resident 3048 MiB) falls back by
300 s; Castlevania's start peak is 2475 MiB. The gameplay run's last reading is at
2820 seconds (2456 MiB, Metal 640 MiB).

## Attribution

`soak_summary.py` found no steady counter in any run, across the scene-memory fields,
all 67 census tables (each `nvn_hmap` with entries and capacity, each Foundation
collection) and the per-class VM accounting (guest, malloc, gpu, kernel graphics).

- Zelda grows in steps, all on new content. Between 1200 and 1800 seconds Link's
  camera brought new parts of the grove into view: footprint +134 MiB, Metal +125 MiB,
  textures +133, texture aliases +56, view cache +59. Translated shaders went from 918
  at 300 seconds to 958, render pipelines from 1136 to 1197. Over the last 30 minutes
  footprint rose 12 MiB and Metal 6 MiB; the last three ten-minute windows added 10, 0
  and 1 MiB. Over the last eight minutes no host table changed except the texture
  registration index (+25 entries) and the deferred-release queue (-6).
- Odyssey and Castlevania are flat after their first load: over the last 25 minutes
  footprint moves within 3 MiB and Metal within 2 MiB (Castlevania gameplay: 2450 to
  2456 MiB over its 32-minute loop, Metal 639 to 640). Shaders, pipelines, textures and
  buffers change only in the first ten-minute window of the loop (Odyssey: 17 shaders,
  14 render pipelines; Castlevania dialogue: 2 and 5; Castlevania gameplay: 18 and 20),
  then not at all.
- Bounded by design, and flat in these runs: the staging arenas (32 MiB Zelda, 68 MiB
  Odyssey, 8 MiB Castlevania, against the 64 MiB idle budget plus in-flight use), the
  512-entry ghost table (5, 129 and 9 entries) and the texture registration index
  (one entry per registered descriptor ID; Zelda's stays between 27,796 and 27,911).
  `g_cpu_tex_retired` keeps finalized CPU-written texture metadata while its pool lives
  (an engine comment says so); in Zelda it stopped at 2,669 to 2,671 entries, about
  16 bytes each.
- Quit: the census tables are the same set the session teardown retires
  (`NativeHost/session/SessionGraphicsState.inc`), except the eight `g_cap_*` tables of
  the engine's GPU capture diagnostic, which stayed empty. This harness cannot Quit
  and relaunch in one process; the phone's 2026-11-04 cycle (footprint at Quit no
  higher in the second round) remains the Quit evidence.

## Leak scans

The live scans found 98,528 bytes in Zelda after an hour and 4,688 bytes in Odyssey
after 30 minutes; both Castlevania runs found none. Neither amount grows footprint measurably.

A separate 15-minute Zelda run with in-memory allocation stacks (`--allocation-stacks`,
symbolic host and system frames only) reports 14 leaks of 16 bytes, all
`calloc in nvn_dirty_add_range` under `h_nvnMemoryPoolInitialize`: the same 14 x 16
bytes as the hour-long run, so they do not grow with play time. The pinned engine
deliberately drops a dirty-page array it replaces when a tracked range grows, because
a fault handler may still read it. The app does not lose them: its session overlay
(`NativeHost/session/dirty_overlay.py`) keeps the replaced arrays on a retired list
and frees them at Quit, which `check_dirty_retire.py` covers. The scene harness lacks
that session overlay, so its scan sees them as unreferenced. A 10-minute Odyssey stack
run attributes all 74 of its leaks the same way (37 state and 37 generation arrays,
the same count as its 30-minute run). The hour-long Zelda run's other
two blocks (one 48 KiB Metal `MTLResourceList`, one 48 KiB malloc) did not recur in
the stack run, so their owner is not attributed; at 96 KiB once in an hour they are
not a growth source.

## Commands

From the worktree, with `source ~/SatchelBuild/engine-pin/tools/app-env.sh` and
`SATCHEL_SHADER_DEPS` set to a parity-validated copy of the shader products:

```sh
SATCHEL_OUT=<out>/candidate ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/build_mac.py
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py TOTK candidate --seconds 3600 --label totk-60 --leak-check
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py Odyssey candidate --seconds 1800 --label odyssey-30 --leak-check
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py Castlevania candidate --seconds 1800 --label castlevania-30 --leak-check
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py Castlevania candidate --seconds 2820 --confirm-seconds 900 --label castlevania-play --leak-check
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py TOTK candidate --seconds 900 --label totk-stacks --leak-check --allocation-stacks
SATCHEL_OUT=<out> ANYGAME_OUT=<guard> python3 -B NativePrepare/guard.py python3 -B NativeHost/memory/soak_mac.py Odyssey candidate --seconds 600 --label odyssey-stacks --leak-check --allocation-stacks
python3 -B NativeHost/memory/soak_summary.py <out>/soaks/<game>/candidate/<label>
```

Private run folders (metrics, census, screenshots) stay outside the repository.
