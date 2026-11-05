# Prepared games for Satchel

`App/NativeGameProfiles.json` is the reviewed registry shared by Swift, the native session bridge and the Mac packager. A title and version select a fixed module list, module build IDs, runtime profile digest, engine pin, shader inputs and environment. Packages cannot add environment overrides. The original TOTK 1.4.3 manifest remains accepted without rewriting its files.

The dev app lists packages in `Documents/PreparedGames`. Selecting one performs full verification before loading its signed container. Verification stamps belong to an individual package and manifest. Unknown, incomplete and unsigned packages stay visible with an explanation. The native host still supports one session per process.

The supported prepared profiles are TOTK 1.4.3, Super Mario Odyssey 1.4.1 and Castlevania: Belmont's Curse 1.0.0. A registry entry is a build contract, not evidence that the package ran on a phone. The worker report records actual validation. On-phone Prepare also has an experimental format 2 path for metadata-derived profiles and dynamic NROs. It preserves imported files and all package integrity checks. Without an existing pack it translates observed shaders through the linked M5 pipeline and compiles them with Metal on first use. See `NativePrepare/README.md` and the anygame worker report for current validation.

## Build and verify

All commands run from this worktree. No command below installs or launches on a phone.

```zsh
python3 NativeGames/build-dev.py
python3 NativeHost/check_package.py
python3 NativeGames/test_packages.py
python3 NativeGames/test_library.py
```

The build helper uses the previously validated host and rewriter archives, replaces the session bridge in a task-local archive, and builds a signed Release app with two jobs and nice 10. It waits for active Xcode, Metal and shader harness work. It checks a 36 GiB starting reserve. The default output is `~/SatchelBuild/games`; `build-dev.py --out-root TASK_DIRECTORY` keeps another worker's outputs isolated. `--prepare-project PATH` selects a current preparation project snapshot when the default predates the menu or signing integration. Set `SATCHEL_HEAVY_LOCK` to the shared worker lock when coordinating builds. The package and library tests also accept task-local output paths.

`NativeHost/package.py` accepts explicit input paths, title and version. It requires a completed shader report, matching chain and input stamps, the reviewed runtime profile, exact rewritten section bytes and a valid code signature. An NRO inventory requires reviewed `nroInventorySHA256` and `nroBuildIDs` fields. The packager checks its complete inventory, packed table, section permissions and every embedded byte. The Swift verifier binds the inventory digest and each NRO layout and gate file. Profiles without those fields still reject NRO inventories. Do not bypass those checks to publish an incomplete package.

`transfer.py PACKAGE` prints the device owner's install, transfer and launch commands. `--execute` performs them and is for the device owner only. The manifest is transferred last. `ctl.zsh TITLE_ID` is the title-aware successor to the original TOTK control helper.

## Reviewed runtime profiles

`App/NativeRuntimeProfiles.json` holds, for each registry row, the exact `profile.tsv` the pinned engine's `game.py profile` writes, and the registry binds it by SHA-256. The engine decides which keys the file has, so both files are regenerated whenever the engine pin moves:

```sh
python3 -B NativeGames/runtime_profiles.py --engine "$SATCHEL_DEPS_ROOT/NX2Apple/engine"          # check, exit 1 on a difference
python3 -B NativeGames/runtime_profiles.py --engine "$SATCHEL_DEPS_ROOT/NX2Apple/engine" --write  # rewrite both files
```

The tool reads only `game.tsv` (profile metadata): the tracked one under `profiles/` when there is one, else the maintainer's private profile folder named in the text's first line, else the engine's own. It keeps that first line as recorded, because the path is part of the hashed text and `NativeHost/package.py` writes the same line from the same folder. Run against the previous engine it reproduced the TOTK and Odyssey texts byte for byte, which is the check that it writes what the packager writes. At the 2026-11-05 pin move every text gained `fps_max`.

## Castlevania bringup

At the 2026-11-05 pin move the tracked `profiles/0100BA4026E94000/game.tsv` took upstream's checked profile (nxnative `dm/games/0100BA4026E94000/game.tsv`, written for update 1.1.0) wherever the two overlap: `romfs_marker`, the memory block, `depth_mode zero` and the shader split (`shader_groups`, `shader_note`). The build row stays the reviewed 1.0.0 build, which upstream does not list. Memory values were already equal: the generic alias-pressure rule had arrived at upstream's numbers. The registry's phone environment is unchanged (30 FPS cap, handheld mode); upstream runs the game docked at its own 60 FPS on the Mac, which has not been tried on the phone. The registry's `shaderInputs` stamp moved with the two shader rows, from `ecf38f1ae766` to `6948c62cbe80`: the engine's `dmsh_chain.py --inputs` over the new rows and the maintainer's existing private shader tables (read in place, the previous rows reproduce the previous stamp). It binds Mac-prepared packs only, which have to be rebuilt with the new chain anyway; phone-prepared packages carry `runtime-capture-v1`. The paragraph below describes the 1.0.0 bringup with the previous engine.


`profiles/0100BA4026E94000/game.tsv` contains metadata for the reviewed Mac-prepared Castlevania: Belmont's Curse 1.0.0 profile. The anygame shared preparation path now prepares its dynamic module, with byte parity and Mac boot validation. The reviewed registry flag describes the older format 1 pack path. Its SDK is 19.3.16, engine Unity 2022.3.62f2 IL2CPP, graphics NVN and audio FMOD. There are three regular modules and one `cimgui.nro` plugin validated against the game's NRR.

The original address-space profile reached `nnMain` and frame submission, then faulted at `main+0x821d0c`, with address `0x3fffe0`. NX2Apple's existing `heap_signals` rule observed +4.27 GiB of a 4.38 GiB alias region in use without a heap and selected a 1 GiB application pool. With that change the guest stayed alive for the 45-second bounded run and passed 1,200 frame submissions. That initial run had no game shader libraries. The subsequent runtime capture route compiled 198 observed shaders and reached the rendered Start menu, opening movie and gameplay on the Mac. Scripted movement, jumping and an attack were visually inspected. This is opening-route coverage, not whole-game coverage or phone validation. Private evidence is in `~/SatchelBuild/castlevania/report.md`.

`select-program.patch` changes the pinned MIT NX2Apple extractor to accept `--program-id`. This XCI also contains a separate gallery application, and the default lowest-ID choice selects the gallery. Every extraction must explicitly select `0100BA4026E94000`. The gallery NSP is not an update for this game.

The reference engine is nxnative at pin `f8a5618d5f505a4c0a0bcac7c413872d3a53c009`, with the 29 fork patches of `scripts/deps/upstream/nxnative-queue` and shader chain `26dc2fa9477c`. The extractor patch is applied only to the games worker's private engine clone. Source and original MIT notices remain there. No extracted game data or generated shader data belongs in this directory.

For repeatable setup, the tested route holds A for 60 frames at brightness and B for 180 frames to skip the movie, then advances dialogue with separated A presses. Shorter scripted holds did not reliably advance these screens. Normal renderer settings worked; no renderer or service workaround was required.

Runtime capture and compilation: see [runtime-capture.md](runtime-capture.md).
