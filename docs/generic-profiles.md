# Generic profile diagnostics

The Mac Prepare harness accepts `--generic-profile`. It clears the reviewed
profile selection before generating the format 2 recipe. Bundled overrides and
existing packages remain untouched. A reviewed shader pack is rejected in this
mode, since its runtime profile would invalidate the comparison.

The app still uses reviewed profiles when their module builds match. It does not
collect memory observations automatically or retry failed boots. These diagnostic
controls are a step toward that feedback path, not an unknown-game compatibility
guarantee.

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

The pinned engine also leaves conditional rendering and texture upload switches
for further investigation: observing an API call alone does not establish that a
game needs a workaround. The Zelda graphics result remains a separate follow-up.

Automatic phone feedback still needs package/build-bound observation collection,
a bounded retry policy, and device validation. Do not apply observations from a
different game, update, reviewed profile or already corrected layout.
