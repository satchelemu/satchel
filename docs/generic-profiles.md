# Generic profile diagnostics

The Mac Prepare harness accepts `--generic-profile`. It clears the reviewed
profile selection before generating the format 2 recipe. Bundled overrides and
existing packages remain untouched. A reviewed shader pack is rejected in this
mode, since its runtime profile would invalidate the comparison.

The app still uses reviewed profiles when their module builds match. For a game
without one, the app collects the same bounded memory observation from the starter
launch's host log and applies the derived rules on the next launch (see Automatic
feedback below). This is not an unknown-game compatibility guarantee.

## Memory observations

`PREPARE_MEMORY_OBSERVATION` can name a JSON file when the Mac harness runs with
`--generic-profile`. Collect the values from one boot of the same game's original
starter recipe, before applying any memory correction:

- `largestFailedHeap`: largest failed `SetHeapSize` request, in bytes, or zero.
- `heapOpened`: whether any `SetHeapSize` succeeded during that boot.
- `aliasMappedHighWater`: highest mapped alias offset, in bytes, or zero.

The recipe accepts only bounded numeric fields and a Boolean. Requests must be
aligned to 2 MiB and no larger than the application pool. The alias high-water
mark cannot exceed the original alias region. Raw guest logs, arbitrary runtime
keys and environment overrides are not accepted as observations.

The rules adapt the pinned MIT engine's `dm/games/profile_guess.py:heap_signals`:

1. A request larger than the heap partition sizes the next boot's heap in 128 MiB
   increments. The alias region gets the remaining reservation, with at least
   1 GiB retained. If necessary, the reservation grows enough to hold both.
   No live address or mapping moves.
2. With NPDM system-resource memory, no opened heap and no failed heap request,
   mapping within 256 MiB of the alias end selects a 1 GiB application pool for
   the next boot. This leaves room for separate virtual allocations. The rule
   does not apply when the reported module footprint already consumes that pool.

Neither rule checks a title ID, game name, build ID or hardcoded SDK version.
The observations remain in the bounded recipe, so verification regenerates the
same runtime profile. Recipes without observations retain their existing runtime
bytes, and reviewed profiles retain priority. The existing NX2Apple MIT notice is
in `Tools/NativeRewriter/licenses/NX2Apple-MIT.txt`.

## Reproduce on a Mac

Use the repository's native preparation build and resource guard, with output
under your task's `SatchelBuild` folder. Do not modify an existing package:

```zsh
PREPARE_TITLE="$TITLE_ID" \
  "$PREPARE_OUT/prepare" --generic-profile "$BASE" "$UPDATE" "$KEYS" - "$OUTPUT"

PREPARE_TITLE="$TITLE_ID" PREPARE_MEMORY_OBSERVATION="$OBSERVATION_JSON" \
  "$PREPARE_OUT/prepare" --generic-profile "$BASE" "$UPDATE" "$KEYS" - "$NEW_OUTPUT"
```

Use `-` for an absent update. Keep observations, packages, logs and screenshots
outside the repository. A memory observation does not provide shaders. The Mac
comparison needs existing compatible Mac shader libraries or a separate shader
translation run. A zero-frame run waiting for a shader compiler is not evidence
of a profile failure.

The pinned engine leaves its conditional rendering and texture upload switches to
the developer, because a short boot does not tell which games need them. Satchel
turns on conditional rendering for a game seen using it (below): evaluating the
conditions is what the console does, and a game that does not depend on them draws
the same either way (Odyssey, measured below). The texture upload switches
(`tex_flush_upload`, `tex_dirty_upload`) can replace GPU-written image content in a
game that does not need them, so they come on only for a game the host saw writing
textures from the CPU in a way their GPU copy misses: a starter profile launch runs the
host's texture-write watch (`NativeHost/graphics/texture_writes.py`), which counts
such textures and logs only the counts.

## Automatic feedback in the app

`App/NativeBootFeedback.swift` applies the same rules on the phone, with no Mac step
and no per-game profile:

1. Before each launch, the app records which profile the launch uses in
   `Documents/NativeRuntime/<title ID>/launch-profile.json`.
2. At the next Play (or when the game report opens), it reads the previous launch's
   `host.log`, but only when that launch used the package's own starter profile and
   `session.json` names the same package. It keeps three numbers (largest failed
   heap request, whether a heap opened, the alias high-water mark) and one Boolean
   (the host's once-per-session note that the game used NVN conditional rendering,
   `SetRenderEnableConditional` or `SetRenderEnable(false)`, which it drew through),
   plus the largest count of the texture-write watch's summary line
   (`[nvn] texture writes the GPU copy misses: N textures ...`), and nothing else from
   the log. At 8 or more textures the next launch turns on both upload switches.
3. When the rules change the profile, the result is stored in Application Support,
   bound to the package name, its starter profile digest and the rewriter revision.
   The next launch regenerates the profile from the verified recipe plus the
   observation, writes it to `learned-profile.tsv` in the title's runtime folder and
   hands it to the host instead of the package's `profile.tsv`. The package itself and
   its signature are not changed.

The render condition rule sets the pinned engine's `render_cond`: the host then
evaluates the game's conditions as the GPU does instead of drawing everything. It
is enabled only for a game seen using the feature, never by title.

Retry policy: only a package prepared on the starter profile is observed: format 2,
current rewriter, no reviewed version, no observation in its recipe. Memory
settings come only from a starter launch and are fixed once derived. The first
learned launch may still add the one-way graphics settings it shows that are still off
(render conditions, texture uploads), in one step, because a game that failed its heap
request stopped before drawing anything; no later launch is read. At most two corrections are made, so a correction that still fails cannot
change itself again. Without a change, the latest starter launch is read again, so
a later or longer boot can still teach. Game report, Forget learned settings removes them
and the launch that taught them; Prepare again starts over too.

The game report shows the observation and the learned settings. Copy diagnostics puts
a plain-text report on the clipboard (local only, expires after five minutes): app
version, device model and iOS version, title ID and version, settings kind, rewriter
revision, engine pin, stage and result, crash signal and module offsets, missing
service commands with counts, unimplemented SVC numbers, shader failure kinds with
their counts but not their shader keys, and the memory feedback. It never contains
keys, guest log text, game code, game data or save data. Mac checks:
`SATCHEL_OUT=<folder> python3 -B NativePrepare/check_feedback.py`; the simulator journey
is `testPrepareDiagnosticsLearnedSettingsAndCopy`.

Do not apply observations from a different game, update, reviewed profile or already
corrected layout.

## Test packages (developer option)

To run the learned flow on the phone with a game that has reviewed settings, turn on
Settings > About > Developer controls (tap the app name seven times to show the switch).
The Prepare screen then shows "Test without reviewed settings", off each time the screen
opens. With it on, Prepare makes the package exactly as for an unknown game (no reviewed
settings, no shader pack) and marks its manifest `testPackage`. The Mac harness's
`--generic-profile` sets the same mark. A test package:

- never replaces the title's other packages: it replaces only an earlier test package of the
  same title, and the original imports are kept (no cleanup after Prepare);
- is not what Play in the library starts while the title has another current package; the
  game page shows "Play test package" (developer controls on), and the Prepare screen and
  home card offer Play right after Prepare. Settings > Prepared games tags it "Test package";
- plays with its own copy of the title's save (`NativeRuntime/<title ID>/save-test`, copied
  from `save` at its first launch), with cloud save sync off, so the save the player uses
  never changes. Its learned settings, launch record and host log follow the rules above.

A normal Prepare of the title later removes the test package like any other older package.

## Results on the Mac, 2026-11-07

Fresh `--generic-profile` Prepare with emptied profile resources, then the Mac
pipeline host with the phone shader translator; learned settings derived by the app's
own feedback code from each run's log (details in COMPATIBILITY.md):

- Odyssey 1.4.1: Cap Kingdom gameplay on the starter profile; the render condition
  setting it then learns leaves the scene unchanged.
- Castlevania 1.0.0: the starter crashes after 107 frames with the alias region nearly
  full; the learned 1 GiB pool reaches the opening boat dialogue.
- Zelda 1.4.3: the starter fails its heap request; the learned heap split reaches
  Great Sky Island with a black 3D scene; the render condition setting added from that
  launch renders it. Small plants and grass still differed from the reviewed profile,
  whose texture upload switches had no generic signal yet (now learned, below).

## Results on the Mac with the texture-write watch, 2026-11-07

Fresh `--generic-profile` Prepares again (same settings as above), the Mac pipeline host
with the watch (`NativeHost/graphics/texture_writes.py`), and the learned settings from the
app's feedback code applied to each run's own log. The counted texture writes per launch:

| Game | Watch on the launch that teaches | Learned settings, in order | Result |
|---|---|---|---|
| Zelda 1.4.3 | starter: crash before the first frame (no count); first learned launch: 14 to 16 textures (5 or 6 flushed over shared storage, 9 to 11 written after first use; 2 written before first use, not counted) | `mem_alias` from the starter; then `render_cond`, `tex_flush_upload` and `tex_dirty_upload` together from the first learned launch | Great Sky Island with the tall grass and plants of the reviewed profile; frames 1,800 to 3,600 match the reviewed run as closely as two reviewed runs match each other |
| Odyssey 1.4.1 | starter: 0 (89 written before first use, not counted) | `render_cond` | Cap Kingdom, unchanged |
| Castlevania 1.0.0 | first learned launch: 0 (8 written before first use, not counted) | `app_pool` 1 GiB | opening boat dialogue at frame 3,600 |

Comparison method: the lower half of each 1280x720 frame (ground and grass) in 32x32
blocks, the share of blocks whose average colour differs by more than 12 from the reviewed
run at the same frame, which grass and Link animating does not move much. Zelda learned
without the texture switches: 70 to 77% at frames 1,800 to 3,600. With them, in two runs:
20 to 50% and 32 to 60%, against 37 to 50% between two reviewed runs; the frames show the
same tall grass and plants, with wind and Link's idle pose as the visible differences. Whole-frame pixel differences are dominated
by animation (43 to 49% of pixels between two reviewed runs) and do not separate the cases.

A first version of the watch counted writes before first use too. Odyssey then learned the
upload switches from 89 such textures and crashed (signal 6) after 1,200 frames, and
Castlevania learned them from 8 (it still reached frame 3,630). Those writes are logged but
no longer counted. The threshold of 8 sits between 0 for both of them and 14 for Zelda; a
game whose count falls near it is the case to watch on the phone.
