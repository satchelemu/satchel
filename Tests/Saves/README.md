# Save data checks

Run from the repository root:

```sh
ANYGAME_OUT="$HOME/SatchelBuild/dlc/check-guard" python3 -B NativePrepare/guard.py python3 -B Tests/Saves/run.py
```

This compiles the iOS save storage implementation for macOS. All fixtures are synthetic
and live under `~/SatchelBuild/dlc/save-tests`, outside the repository. Python's ZIP
reader independently verifies the exported archive. No game content, keys or user saves
are inputs. The guard waits for heavy builds, runs at nice 10, limits each child below
10 GiB RSS and stops before disk free space reaches 35 GiB.

`App/SaveStore.swift` owns snapshot inspection, archive validation, backup rotation and
atomic restore. `App/SaveArchive.swift` implements a bounded ZIP format with stored entries.
There are no new dependencies. ZIPs open in Files and ordinary archive tools. Import
accepts this format only, including its `satchel-save.json` title binding and SHA-256
inventory. Repacked archives and arbitrary emulator save ZIPs are rejected. Validation
proves the archive structure and integrity, not the meaning of each game's private bytes
or the authenticity of a manifest deliberately rewritten by a person.

Each save may contain up to 512 MiB and 10,000 entries. The ZIP is limited to 520 MiB.
The newest three backups are retained, with a 1,560 MiB per-title limit. The newest copy
is published before rotation; temporary storage can include one extra ZIP and a staged
save. A backup error refuses import before changing the current save. Interrupted scratch
trees are removed on the next operation for that title. Successful import swaps directories
atomically on the same filesystem, so process interruption cannot expose half a save.

The native adapter calls the existing MIT-licensed NX2Apple
`dm_fs_clone_committed` API at engine pin `bbc3b737`. That function uses the same locks as
Commit and preserves the engine's guest-name extended attribute. No engine source is copied
or modified. Only `save` is included, not the sibling unfinished `save.work` or `save.new`.
Separate device-save siblings are outside this task's specified `save` folder contract.

Session preparation and save operations share a lock. Export and import refuse the owning
title from preparation through a successful stop and join, including paused sessions and
failed stops. Background and quit notifications snapshot committed data asynchronously;
background expiration cancels at chunk boundaries and leaves the live save untouched.
Overlapping lifecycle requests are coalesced with a second snapshot so a final commit is
not lost. Backup failures are shown on the game's save panel and retried at the next event.

Phone journey for the orchestrator, after building and installing through the normal flow:

1. Open a game's page. Check the Save data time and size against a known in-game save.
2. Export save. Choose Save to Files in the share sheet, then an iCloud Drive folder and confirm the game/date ZIP name.
3. Start the game, pause and return to the library. Export, import and backup restore must
   remain unavailable until Quit to home succeeds.
4. Make an in-game save, background Satchel, return, then Quit to home. The save panel should
   show a backup. Repeat with four distinct saves and confirm only the newest three remain.
5. Import the exported ZIP from iCloud. Confirm the previous save appears in Backups, then
   start the game and confirm the restored progress. Quit and restore the automatic backup.
6. Select another title's ZIP and a truncated synthetic ZIP. Both must show an error and
   leave progress intact. Cancel the Files picker and confirm the panel remains usable.
7. Repeat in both landscape orientations and across front/inner-screen transitions.

The Mac checks do not establish gameplay correctness or phone lifecycle behavior. No phone
installation, launch or game play is performed by this worker.
