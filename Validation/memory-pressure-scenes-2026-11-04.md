# Mac scene-memory measurements, 2026-11-04

This is evidence from the unmerged memory-pressure candidate on `memory-2`, based
on `0856739ba64aca4fbf96a1671dc1f58f59e93eae`. It does not enable that candidate.
The comparison baseline retains main's existing staging and finalized-texture
lifetime fixes, with the new pressure callback disabled. Both variants use the
same pinned runtime translator and scripted route, with private saves.

All six fresh routes completed without reported renderer failures. Castlevania
ran 9000 frames per variant; Odyssey and TOTK ran 7200. Each candidate consumed
exactly five injected warning requests at submission boundaries. Forced Mac
measurements exclude unrelated OS notifications. This diagnostic isolation is
not a change to the app's normal pressure source.

All 37 checkpoint pairs were visually reviewed: 12 Castlevania, 13 Odyssey and
12 TOTK. All 74 ownership/VM joins passed. Matching scene labels do not establish
pixel equality: animation, weather and subtitle timing can differ. Process
footprint also depends on shared-host memory conditions. These are Mac readings,
not phone memory bounds or proof of pressure invariance.

Sizes below are MiB, baseline / candidate.

| Game | Frame | Scene | Footprint | Metal |
|---|---:|---|---:|---:|
| Castlevania | 300 | Before Playing notice | 1482.9 / 1488.7 | 304.4 / 307.9 |
| Castlevania | 900 | confirm button selection | 1492.2 / 1494.8 | 309.3 / 309.3 |
| Castlevania | 1800 | 1499 introduction | 2439.3 / 2442.7 | 754.6 / 752.6 |
| Castlevania | 2400 | burning city introduction | 2427.9 / 2429.1 | 741.3 / 740.3 |
| Castlevania | 3600 | boat dialogue | 2438.3 / 2313.6 | 743.7 / 618.7 |
| Castlevania | 4800 | boat dialogue continuation | 2439.8 / 2172.7 | 743.9 / 610.9 |
| Castlevania | 6000 | dock approach dialogue | 2438.6 / 2177.6 | 742.9 / 614.9 |
| Castlevania | 6900 | dock gameplay HUD | 2443.6 / 2188.4 | 745.0 / 624.0 |
| Castlevania | 7250 | warehouse approach movement | 2434.3 / 2192.4 | 738.6 / 622.6 |
| Castlevania | 7295 | warehouse approach jump | 2435.7 / 2192.7 | 738.7 / 621.7 |
| Castlevania | 8015 | warehouse attack | 2443.7 / 2198.1 | 741.2 / 625.2 |
| Castlevania | 9000 | warehouse idle | 2457.1 / 2209.9 | 747.5 / 632.5 |
| Odyssey | 300 | controller notice | 1104.2 / 1098.7 | 498.8 / 498.8 |
| Odyssey | 900 | resume menu | 2646.9 / 2651.2 | 1265.6 / 1265.6 |
| Odyssey | 1800 | Cap Kingdom resumed gameplay | 4333.2 / 4327.5 | 2210.4 / 2210.4 |
| Odyssey | 2400 | Cap Kingdom idle | 4331.2 / 3401.2 | 2211.0 / 2211.0 |
| Odyssey | 3600 | Cap Kingdom idle after pressure | 4331.6 / 3203.0 | 2211.0 / 2011.0 |
| Odyssey | 3700 | pause menu | 4382.1 / 3261.4 | 2261.7 / 2069.7 |
| Odyssey | 4000 | resumed Cap Kingdom | 4386.7 / 3259.0 | 2265.5 / 2073.5 |
| Odyssey | 4800 | Cap Kingdom movement | 4403.6 / 3278.0 | 2273.7 / 2081.7 |
| Odyssey | 5200 | camera facing town | 4385.3 / 3259.5 | 2258.5 / 2066.5 |
| Odyssey | 6000 | town view idle | 4396.7 / 3270.8 | 2265.8 / 2073.8 |
| Odyssey | 6100 | pause menu over town view | 4192.8 / 3305.1 | 2304.0 / 2112.0 |
| Odyssey | 6400 | resumed town view | 3498.8 / 3307.3 | 2305.9 / 2113.9 |
| Odyssey | 7200 | seated idle town view | 3499.7 / 3318.9 | 2313.2 / 2121.2 |
| TOTK | 300 | title menu | 2315.0 / 2305.5 | 1884.7 / 1882.5 |
| TOTK | 900 | title menu before continue | 2325.3 / 2335.1 | 1889.5 / 1901.8 |
| TOTK | 1800 | Great Sky Island resumed gameplay | 4233.5 / 4227.9 | 2452.3 / 2443.8 |
| TOTK | 2400 | Great Sky Island idle | 4379.6 / 4212.5 | 2593.3 / 2422.1 |
| TOTK | 3600 | Great Sky Island idle after pressure | 4626.1 / 4291.5 | 2838.9 / 2503.0 |
| TOTK | 4300 | weapons inventory | 4824.9 / 4630.3 | 3025.8 / 2823.2 |
| TOTK | 4600 | resumed island gameplay | 4856.1 / 4684.9 | 3055.9 / 2877.2 |
| TOTK | 4800 | island movement | 4926.7 / 4715.5 | 3121.9 / 2903.4 |
| TOTK | 6000 | island viewpoint after movement | 5879.7 / 5314.3 | 4042.8 / 3500.4 |
| TOTK | 6100 | weapons inventory at new viewpoint | 5883.1 / 5310.8 | 4048.1 / 3498.9 |
| TOTK | 6400 | resumed island viewpoint | 5931.7 / 5343.7 | 4096.5 / 3531.6 |
| TOTK | 7200 | island viewpoint idle | 6641.7 / 5386.1 | 4800.6 / 3567.6 |


The final staging-owner readings were 137.1 / 21.1 MiB for Castlevania,
205.1 / 5.1 MiB for Odyssey and 225.1 / 29.1 MiB for TOTK. Renderer owner counts
separate guest pool buffers, staging, textures, heaps, pipelines and shader host
counters. Guest VM accounting overlaps no-copy Metal pools; do not add those
columns. Sparse mapped coverage is virtual coverage, not unique physical RAM.

Validation commands, run in the worker worktree:

```sh
python3 -B NativeHost/memory/summarize.py "$ANYGAME_OUT/runtime" --label pressure-route
python3 -B NativeHost/memory/attribute.py "$ANYGAME_OUT/runtime" --label pressure-route
```

Results: `PASS: paired routes, complete captures and nearby memory samples for all
three games.` and `PASS: 74 reviewed checkpoint ownership and VM joins.`
`ANYGAME_OUT` was `~/SatchelBuild/dlc/memory-2-part2`. Private result JSON, scene
captures, measurements and ownership tables remain there, outside the repository.
The exact guarded build and route commands are in the worker's `report.md`.
No game payload, shader payload or decrypted code belongs in this evidence file.

Rejected runs remain separate: a Castlevania candidate received six events due to
an unrelated OS warning; one baseline checkpoint showed loading rather than the
introduction; an early baseline was stopped by the disk guard. None contributes
to this table. Replacement runs passed the original checks.

The strict replay gate remains open. Castlevania's current same-process pressure
pair passes all 8,294,400 raw bytes with exactly two events and matches its live
capture. Odyssey's pressure-free control differs by 39 bytes; TOTK's pressure-free
control differs by 109 bytes. These control failures prevent attributing their
pair differences to the pressure handler. No tolerance or extra warm game frame
is accepted. Follow-up must establish identical plain controls and then identical
pressure pairs before claiming the candidate's three-game pressure invariance.

This evidence does not justify increasing main's 65% performance/stability rating
or changing phone compatibility ratings. The device owner still needs pressure,
scene-transition and relaunch acceptance on the phone. This worker built and ran
Mac diagnostics only, with no phone actions.
