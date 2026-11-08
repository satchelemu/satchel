# Developer Quit and prepared-game cycles

This is an orchestrator procedure. The worker does not connect to, install on or launch
the phone. These controls require `-SatchelDeveloperControls YES` in this process.
The saved Developer controls preference alone does not enable them. Package arguments
are exact folder names under `Documents/PreparedGames`, not title IDs or display names.
Existing prepared packages and saves are reused in place.

`quit` in `Documents/NativeRuntime/<titleID>/ctl.txt` opens the existing menu, waits for
its asynchronous pause, then performs `quit` and `confirm-quit`. The SwiftUI menu and
plain menu use those same actions. They retain the existing save-commit drain, stop/join,
library return, save-backup observer and `SatchelNativeSessionDidEnd` notification.
There is no second engine stop path. The command is rejected with developer controls off.
Each session clears `ctl.txt`. Within a session, replace it with a growing copy because
the host remembers the byte offset already consumed. Always include a final newline.

The cycle arguments are process-scoped and take priority over automatic Prepare,
automatic launch and `SatchelThenPreparedGame`. All three cycle arguments are required:

- `-SatchelCyclePreparedGames A,B,C`: visit those exact packages in that order.
- `-SatchelCycleSeconds N`: 1 through 86400 seconds per visit, timed from the first
  observed guest frame. Verification and the wait for the first frame are excluded.
- `-SatchelCycleRounds R`: 1 through 100 whole rounds, at most 64 package names.

The cycle waits for the matching session-ended notification, then two seconds for
library dismissal before opening the next package. It stops advancing on a missing
package, verification or thread-start error, 900-second launch timeout, 90-second wait
for the first frame, 60-second frame-progress gap, background interruption, rejected
Quit, failed stop or 120-second stop timeout. Live sessions are asked to Quit through
the menu. A failed native stop retains ownership as before. It does not force-kill the
app, retry a failed stop or launch a replacement. A verifier completing after a failed
cycle has its late session retired through the same menu path.

`Documents/cycle-log.tsv` contains a header and one row per attempted visit. The current
row is atomically replaced at launch, first frame, Quit and end. Invalid arguments
produce one failure row with an empty package. Columns:

| Column | Meaning |
|---|---|
| package | Exact prepared package folder |
| start | UTC ISO 8601 first-frame time, or attempt time if no frame arrived |
| seconds_played | Monotonic elapsed time from first observed frame to Quit request |
| frames | Native guest-present counter sampled at Quit request, not assumed FPS |
| footprint_at_quit_bytes | `TASK_VM_INFO.phys_footprint` at Quit request, or `unknown` |
| stop_seconds | Elapsed time from Quit request through matching end notification |
| result | `ok`, an in-progress stage, or a sanitized failure reason |

`ok` proves the cycle observed frames and the normal session-ended notification. It
does not prove playable gameplay, save contents, visual correctness, absence of shader
errors or a memory plateau. A title screen can produce frames. The long cycle includes
no gameplay input script. The orchestrator can append button commands to the active
session's control file separately. A process crash, jetsam or suspension can prevent
further writes: a final `launching`, `playing` or `stopping` row is incomplete, never a
pass. Backgrounding fails the cycle. Copy the log before a new cycle, which replaces it.
If the log cannot be written, no next visit starts and the app reports `log-write-failed`
on stderr; a failing storage device cannot guarantee a final log row.

A process launched with `-SatchelAutoLaunchPreparedGame YES` (with developer controls) or
`-SatchelCyclePreparedGames` never resumes a saved shader warm-up by itself, and no warm-up
starts, resumes or keeps running beside any game (`WarmupGate`), so these runs do not share
the CPU, the shader compiler or the phone's heat with one. A warm-up iOS stopped keeps its
place and resumes on the next ordinary launch. To check, read its `progress.json` before and
after a run: `completed` and `seconds` must not change. Do not rely on `state` alone: a run
the app ended still reads `running` on disk until the warm-up next runs.

## Orchestrator commands

Use zsh on the Mac, with the unsigned build reviewed, signed and installed separately
by the orchestrator. Do not use `--terminate-existing` on a live game with unsaved work.
Start each procedure with the app at its library after a completed Quit. Set the three
package variables from the installed folder inventory; names can differ after Prepare.
The commands below never copy a package or a save.

```zsh
cycle_device=00008160-001A30E02E600036
cycle_bundle=com.dell.satchel
cycle_out="$HOME/SatchelBuild/session/phone-results"
mkdir -p "$cycle_out"
xcrun devicectl device info files --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --subdirectory Documents/PreparedGames --json-output "$cycle_out/packages.json"

# Replace these three values with the exact installed folder names from that inventory.
cycle_castlevania='CASTLEVANIA_PACKAGE_FOLDER'
cycle_odyssey='ODYSSEY_PACKAGE_FOLDER'
cycle_zelda='ZELDA_PACKAGE_FOLDER'
cycle_packages="$cycle_castlevania,$cycle_odyssey,$cycle_zelda"
```

One remote Quit, followed by an automatic relaunch of Odyssey in the same process:

```zsh
xcrun devicectl device process launch --device "$cycle_device" \
  --terminate-existing "$cycle_bundle" -- \
  -SatchelDeveloperControls YES -SatchelAutoLaunchPreparedGame YES \
  -SatchelPreparedGame "$cycle_castlevania" -SatchelThenPreparedGame "$cycle_odyssey"

# Wait for Castlevania to start. Copy its progress and inspect state=verified.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source Documents/verify-progress.json --destination "$cycle_out/one-verify.json"
# Read the title ID from the installed Castlevania manifest, without copying game files.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source "Documents/PreparedGames/$cycle_castlevania/manifest.json" \
  --destination "$cycle_out/castlevania-manifest.json"
cycle_title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleID"])' "$cycle_out/castlevania-manifest.json")
# Preserve earlier commands, then append once. For a fresh session the file is empty.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source "Documents/NativeRuntime/$cycle_title/ctl.txt" --destination "$cycle_out/ctl.txt"
printf 'quit\n' >> "$cycle_out/ctl.txt"
xcrun devicectl device copy to --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source "$cycle_out/ctl.txt" --destination "Documents/NativeRuntime/$cycle_title/ctl.txt"
# After the stop, require stage=stop and result="ended 0" for Castlevania.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source "Documents/NativeRuntime/$cycle_title/session.json" --destination "$cycle_out/one-quit.json"
```

Then copy Odyssey's `session.json` using its manifest title ID and require a newer
`boot/running` record. That second launch proves the normal end notification fired.
The native `ended 0` stage precedes the final main-thread cleanup, so it alone does not
prove the library transition. Use `shot` or `screen-shot` through the new session's
control file to inspect the game. For a library-only check omit `SatchelThenPreparedGame`
from the initial launch; the synthetic UI journey covers the library controls separately.
Quit Odyssey through its own freshly copied control file before the next procedure.
Keep copied manifests private: they contain game metadata and are not report attachments.

Three games, two rounds, 30 seconds per visit:

```zsh
xcrun devicectl device process launch --device "$cycle_device" \
  --terminate-existing "$cycle_bundle" -- \
  -SatchelDeveloperControls YES -SatchelCyclePreparedGames "$cycle_packages" \
  -SatchelCycleSeconds 30 -SatchelCycleRounds 2
# Poll this copy until there are six data rows, all ending in ok, or a failure.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source Documents/cycle-log.tsv --destination "$cycle_out/two-rounds.tsv"
```

Long cycle, Castlevania, Odyssey, Zelda, five minutes each, four rounds:

```zsh
xcrun devicectl device process launch --device "$cycle_device" \
  --terminate-existing "$cycle_bundle" -- \
  -SatchelDeveloperControls YES -SatchelCyclePreparedGames "$cycle_packages" \
  -SatchelCycleSeconds 300 -SatchelCycleRounds 4
# At least 60 minutes plus verification, startup, stop and dismissal time.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source Documents/cycle-log.tsv --destination "$cycle_out/four-rounds.tsv"
```

Require 12 `ok` rows in order and `seconds_played >= 300`. Compare footprint for each
title across visits, without calling it a leak test. On failure, keep the cycle log
and the affected title's `session.json` and sanitized game report. Runtime host logs
are overwritten on a title's next visit. Do not export shader captures, package
payloads or raw guest diagnostics. Real save flush, signed dylib retirement and phone
memory behavior remain hardware acceptance items even after the Mac tests pass.

## Title-screen memory soak (no input)

This measures memory over a long idle visit without sending any game input, so the
user's Zelda save cannot advance: the title screen (and any attract sequence the game
plays there by itself) runs for 60 minutes, then the cycle quits through the menu.
`-SatchelMemoryProfile YES` makes the scene-memory sampler append counts and sizes of
the host's textures, buffers, pipelines, shaders and staging every 60 guest frames to
`scene-memory.tsv` in the title's runtime folder, started fresh at launch. The
profile's DM_MEMSTAT file (`memory.tsv`, or a reviewed profile's own name such as
`zelda-memory.tsv`) records footprint, Metal allocation and per-class charged memory
every second. A relative DM_MEMSTAT name resolves to the container's `Documents/`, so
this file is `Documents/zelda-memory.tsv`, not a file in the runtime folder. `-SatchelGraphicsProfile YES` adds frame times. Start at
the library after a completed Quit. Do not write button commands to `ctl.txt` during
this procedure; a `shot` line is the only allowed control command.

```zsh
soak_out="$cycle_out/zelda-title-soak"; mkdir -p "$soak_out"
# Read the title ID from the installed Zelda manifest, without copying game files.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source "Documents/PreparedGames/$cycle_zelda/manifest.json" \
  --destination "$soak_out/zelda-manifest.json"
soak_title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleID"])' "$soak_out/zelda-manifest.json")
xcrun devicectl device process launch --device "$cycle_device" \
  --terminate-existing "$cycle_bundle" -- \
  -SatchelDeveloperControls YES -SatchelCyclePreparedGames "$cycle_zelda" \
  -SatchelCycleSeconds 3600 -SatchelCycleRounds 1 \
  -SatchelMemoryProfile YES -SatchelGraphicsProfile YES
# After at least 3,600 seconds plus verification, startup and stop: one row, result ok.
xcrun devicectl device copy from --device "$cycle_device" \
  --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
  --source Documents/cycle-log.tsv --destination "$soak_out/cycle-log.tsv"
for name in scene-memory.tsv graphics-frames.tsv; do
  xcrun devicectl device copy from --device "$cycle_device" \
    --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
    --source "Documents/NativeRuntime/$soak_title/$name" --destination "$soak_out/$name" || true
done
# The DM_MEMSTAT file is in Documents/, not in the runtime folder.
for name in memory.tsv zelda-memory.tsv; do
  xcrun devicectl device copy from --device "$cycle_device" \
    --domain-type appDataContainer --domain-identifier "$cycle_bundle" \
    --source "Documents/$name" --destination "$soak_out/$name" || true
done
```

Analysis, from a worktree on the Mac (numbers only, no game data):

```zsh
python3 -B NativeHost/memory/soak_summary.py --skip 300 "$soak_out"
cat "$soak_out/soak-summary.md"
```

Keep the copied manifest private. Require the cycle row to read `ok` with
`seconds_played >= 3600`, and report the
footprint and Metal rows and every counter the summary calls `steady`. On a title
screen nothing new should load after the first minutes, so any steady counter is a
leak candidate; steps are expected only at the first load. Repeat with Odyssey and
Castlevania by changing the package, the title ID and the memory file name
(`odyssey-memory.tsv`, `castlevania-memory.tsv`). Compare `footprint_at_quit_bytes`
with the summary's end value.

## Mac checks

```zsh
SATCHEL_OUT="$HOME/SatchelBuild/session/cycle-tests" \
  nice -n 10 python3 -B Tests/PreparedCycle/run.py
SATCHEL_OUT="$HOME/SatchelBuild/session/phone-cycle-ui" \
SATCHEL_BUILD_ROOT="$HOME/SatchelBuild" SATCHEL_DEPS_ROOT="$HOME/SatchelDeps" \
SATCHEL_UI_SIMULATOR=YOUR_DEDICATED_SIMULATOR \
SATCHEL_UI_ONLY_TESTING=testDeveloperQuitGateAndMenuStillWorks,testDeveloperQuitReturnsHome,testDeveloperCycleTwoRounds,testDeveloperCycleMissingPackage,testQuitReturnsHomeAndCanLaunchAgain,testPlainMenuQuitReturnsHome \
  nice -n 10 python3 -B NativeApp/build.py --unsigned --ui-tests-simulator
sh scripts/check_repo_hygiene.sh
```

The policy test uses a synthetic clock on macOS. The UI journeys run production
prepared-game routing, menu actions and cycle integration over a synthetic session
with an asynchronous end notification. The test-only remote button calls the actual
control-command parser. It does not stand in for phone `devicectl` transport or native
save/graphics teardown.
