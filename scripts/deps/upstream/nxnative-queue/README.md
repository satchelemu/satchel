# The engine patch queue

Satchel's native host is [seedds/nxnative](https://github.com/seedds/nxnative) (MIT) at the
commit in `.upstream-ref`, plus the integration patches that
[LNPANDY/NX2Apple](https://github.com/LNPANDY/NX2Apple) (MIT) publishes in its `patches/`
folder. `scripts/bootstrap_deps.py` applies them in the order of `series` with `git apply`,
without commits, and writes `dm/fork_patches.txt` in the engine from the same list.

| Item | Value |
|---|---|
| Engine pin | `f8a5618d5f505a4c0a0bcac7c413872d3a53c009` (seedds/nxnative main, 2026-11-05) |
| Published queue | LNPANDY/NX2Apple `8d13135e2a1eec10406f6b486157ec1d2f56eafd`, written for engine `777a5f6c618a38cd2d7f795f465466fb56920534` |
| Patches applied | 29 |
| Taken unchanged from the published queue | 25 |
| Ported here (`ports/`) | 4 |

The pin is 17 upstream commits ahead of the commit the published queue was written for.
Twenty-five patches still apply unchanged. Four do not, and `ports/` holds the version
that does. The files in `ports/` are NX2Apple's patches with the conflicts resolved; their
MIT notice is `NX2Apple-LICENSE.txt`.

## Ported patches

| Patch | What changed against the published file |
|---|---|
| `0001-runtime-profile.patch` | `dm/host/hle_fpslock.c`: upstream 4e0333a5 added the 120 fps case to the frame-rate cap. The port keeps upstream's new lines and reads the cap from `dm_profile()->fps_target`. |
| `0009-mac-keymap.patch` | `dm/host/tests/Makefile`: upstream 67f1d005 added `t_output_stream` to the `run` list. The port keeps it and adds the patch's two tests after it. |
| `0049-dm-framework-env-switch-row.patch` | `docs/env_switches.tsv`: upstream 4e0333a5 reworded the `DM_FPS_TARGET` row. The port keeps upstream's row and adds the patch's two rows. |
| `0053-runtime-profile-every-site.patch` | `dm/gfx/nvn/hle_nvn.c` and `dm/host/tests/Makefile`: same two upstream changes as above. The port also adds `fps_max` to the runtime profile (`hle_profile.h`, `hle_profile.c`, `game.py profile`, both tests), because upstream 4e0333a5 introduced `DM_GAME_FPS_MAX` and three host sites that read it. Without the field, a host built once would apply the compiled game's ceiling to every game. `hle_profile.c` also gets a default of 60 for `DM_GAME_FPS_MAX`, like its neighbours, so a synthetic test profile without the macro still compiles. |

## Patches of the earlier queue that are gone

The previous pin (`bbc3b73784751d2a706932dc69fd67627e3445d4`) carried 34 patches from
NX2Apple `98f395215b7fa55b770cc1f6df7b7e40b484512d`. Five are no longer needed because
upstream has the change:

| Dropped patch | Covered by upstream |
|---|---|
| `0006-nsp-chain-changed-note` | 2716f78f (stale shader caches reported through build events, already in the previous pin; NX2Apple dropped its own note in ad80a857) |
| `0040-nsp-derived-profile-without-frame-step` | 28035370 (seedds/nxnative#794) |
| `0041-shared-fonts-a-compact-stand-in-for-a-font-engin` | b6dbb851 (the host gives a font engine a work buffer the stand-in fits in). `dm/tools/compact_font.py` went with the patch; Satchel's build takes the same pinned Noto Sans file from the dependency lock instead. |
| `0045-route-c-loader-image-header` | 694ea914 (seedds/nxnative#729, `dm_set_game_image_header`) |
| `0052-front-progress-hook` | fa4c3566 (seedds/nxnative#742, `front_progress_hook`) |

## Published patches that are not applied

The published queue has twelve more patches (`0055` to `0072`). They serve NX2Apple's own
iOS shell and launcher (localization lists, touch layout, its in-game menu, its Quit
gesture, its .NET lookup). Satchel has its own shell, menu and controls, so they are left
out, as they were never part of Satchel's queue.

## Moving the pin again

1. Pick the new engine commit and, if NX2Apple has moved, the new queue commit. Update both
   rows of `scripts/deps/inputs.lock.json` (the hash is of `git archive --format=tar COMMIT`).
2. Apply `series` to the new commit. For a patch that no longer applies, put the resolved
   file in `ports/` and add a row above. Remove a port once the published file applies again.
3. Write the new commit into `.upstream-ref`, then follow "Moving the engine pin" in
   `BUILDING.md` for the places that record the pin and the shader chain.
