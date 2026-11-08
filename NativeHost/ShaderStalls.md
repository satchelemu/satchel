# Shader stall measurement

`bench_shaders.py` runs an idle opening route for each allowed game with no controller
input. It captures frames 300, 1200 and 1800, then a DM_CTL image. Castlevania reaches
the language menu, Odyssey reaches mode selection, and Zelda reaches the title menu
using a private copy of the existing `host/saves/dmios-final/Documents/save` snapshot.
The original saves and packages are read-only. Every launch starts with the same save
state. The host receives full render scale and Zelda's 30 FPS target.

`M5_BENCH_OUT` is an external scratch directory containing instrumented `baseline/dmhost`
and `candidate/dmhost` executables. `M5_BENCH_HOST` can select another executable.
The package and host input roots are the three named directories under
`~/SatchelBuild/anygame`. No ROM search or directory discovery is performed.

Use a scratch folder of your own for both (not the shared shader products folder):

```sh
ANYGAME_OUT=$HOME/SatchelBuild/<task>/stalls M5_BENCH_OUT=$HOME/SatchelBuild/<task>/stalls \
python3 -B NativePrepare/guard.py python3 -B NativeHost/bench_shaders.py \
  --route baseline --game TOTK --cache clean
```

Repeat with `--cache warm`, then the candidate, for `Castlevania`, `Odyssey`, and `TOTK`.
Clean mode requires a new application cache directory. Warm mode reuses that directory
in a new process. Neither mode clears Apple's private compiler cache. Do not call this
an OS-cold measurement. Captures and frame counts must establish a matching scene before
a timing comparison is accepted. Castlevania's disappearing labels are a known baseline
issue; record their state separately from the scene match.

`DM_SHADER_EVENTS` enables payload-free TSV events with monotonic start nanoseconds,
duration nanoseconds, thread ID and operation. `submit-thread` identifies submission
threads. `translation` and `library-create` measure translator and source-library calls.
`pipeline-create` measures Metal render/compute creation and specialization;
`pipeline-archive` measures archive lookups, including misses. `prepare-use`,
`library-use`, `pipeline-use`, and `pipeline-wait` include required first-use waits.
`pipeline-own` (an ordinary key) and `pipeline-own-merged` (a merged-pass or specialized-fragment
key) time the pipelines a draw builds itself; `pipeline-taken` marks a draw taking a speculative
build. With the trace on, each prediction walk logs its draw and key counts (`parallel precompile walk`),
and each ordinary pipeline a draw builds itself adds one `own-*` event naming how its key relates to the
last walk's keys (`own-predicted`, `own-unseen`, or the first differing part: shaders, formats, vertex
layout, blend).
Do not add nested durations. Union their intervals on each submit thread for a total.
The existing `DM_NVN_TSV` supplies frame intervals, draw counts and worst-frame evidence.
Events cover the whole process route; frame statistics stop at frame 1800.

The trace is diagnostic and includes per-event file I/O overhead. Production tracing is
off unless requested. The app's `-SatchelGraphicsProfile YES` writes `shader-events.tsv`
beside `graphics-frames.tsv` under the selected runtime or graphics trial directory.

Each route result also carries `timing.stalls` from `stall_attribution.py`, which reads the
same frame TSV and event TSV (a phone trial's `graphics-frames.tsv` and `shader-events.tsv`
too): frames over 50 and 100 ms, the worst frames, and for every frame over 50 ms its time
split into library, translation (the draw's own stages), look-ahead translation (a walk
resolving a later program), descriptor, shader setup, pipeline, prediction walk and other.
Frame boundaries are the host's own `frame` events; each submit-thread instant goes to one
category, innermost first. Other is everything without a shader event: guest work, GPU
waits, pacing and loading.

# Bounded first use (2026-11-06, Mac)

Where cold first use went (exact mode, first run of the table below): Zelda's title frame
(155 new libraries, 120 pipelines, 6.3 s) spent 1.35 s in the draw's own translation, 0.84 s
in look-ahead walks translating later programs one by one on the frame path, 1.78 s in
libraries and 2.10 s in pipelines. Descriptor building stayed under 2 ms per route. Odyssey's slow frames were mostly
libraries and translation; Castlevania's worst frames had no shader work at all.

The opt-in `bounded` pipeline mode (`NativeHost/graphics/README.md`, Bounded first use) starts
translation and base libraries when the game binds a program, lets the draw do its own work on
its thread instead of queuing behind background lanes, keeps prediction walks from translating
unready programs, and gives the submitting thread the next pipeline lane. It never draws with a
different program or pipeline. Cold opening routes, render scale 1.0, final build, one run each
(exact twice), 1,800 frames. "Shader wait" is the shader part of the worst frame; the second
value is the next worst frame's.

| Route | Frames > 50 / > 100 ms | Worst frame | Shader wait, worst / next | Frames with shader wait > 100 ms |
|---|---|---|---|---|
| Castlevania exact (two runs) | 6/3, 5/2 | 207, 214 ms | 120 / 63, 138 / 56 ms | 1, 1 |
| Castlevania bounded | 5/3 | 233 ms | 117 / 64 ms | 1 |
| Odyssey exact (two runs) | 3/3, 3/3 | 238, 245 ms | 221 / 112, 229 / 104 ms | 3, 3 |
| Odyssey bounded | 3/1 | 132 ms | 113 / 57 ms | 1 |
| Zelda exact (two runs) | 15/11, 13/12 | 6,330, 3,814 ms | 6,112 / 790, 3,592 / 853 ms | 11, 11 |
| Zelda bounded | 13/10 | 2,407 ms | 2,181 / 531 ms | 8 |

In those Zelda slow frames, translation fell from 3.8 and 3.2 s (exact) to 0.68 s; pipelines
stayed at 3.2 to 3.5 s and are now most of what remains. The worst Zelda frame is the title
scene's first frame. After it, Zelda still has frames over 100 ms: bursts of 7 to 23 new
pipelines in the next frames (119 to 556 ms, pipeline waits 95 to 428 ms) and a vertex shader
translated again because a new pixel shader pairing widened its padding (frame 875: 48 ms
translation and 217 ms library). So the
goal of no shader stall over 100 ms after a scene's first frame is not met on the Mac for
Zelda, and only Castlevania's and Odyssey's single worst first-use frames come close.

Warm routes are unchanged in kind: exact and bounded have no frame with shader wait over
100 ms in Castlevania or Odyssey, and one in Zelda (the title frame: 294 ms exact, 258 ms
bounded). The order of runs matters: exact ran first in every set, so later runs reuse Apple's
system compiler cache (exact's second Zelda run spent 0.2 s in libraries against 3.0 s in its
first). Translation is the application's own work and its reduction does not depend on that.
Mac CPU has more cores than the phone; the lanes are the phone's (two ahead, two pipeline,
three library compilations), but no phone result follows.

Pixels: no bounded run used a fallback pipeline or skipped a draw (zero `pipeline-fallback`
events, zero skips), so every draw used the exact pipeline exact mode makes. Captured frames
300, 1200 and 1800: Odyssey is byte-identical across both exact runs and bounded. Castlevania
and Zelda differ between the two exact runs as much as between exact and bounded (the opening
advances with wall time: Castlevania's frame 300 showed the notice screen in one run and the
middleware logo in another; Zelda's title animates). A detclock run was tried for parity and
Castlevania reached only 115 frames in 300 s, so it is not used.

# Launch prewarm of recorded pipelines (2026-11-06, Mac)

Why Zelda's title frame (frame 144: 159 new libraries, 122 new pipelines, 1.8 to 2.0 s on the phone)
was built at draw time on every cold trial, in both exact and bounded:

- The package seed is empty. Every format 2 (phone-prepared or runtime-capture) package's
  `dm/pso_manifest.tsv` holds only a comment, so a trial that keeps the seed keeps nothing.
- The recorded list was never read. Each launch appends what it builds to `pso_user.tsv`, but the
  launch policy sets `DM_PSO_MANIFEST=off` (since the first release), so the engine's prewarm never ran
  in the app; a warm launch is faster only because the pipeline archive and Apple's compiler cache
  keep the previous launch's work. A cold trial also starts its list fresh.
- Turning it on alone would not help: the prewarm drops every line whose shader key is not in the
  shader map, and a phone-translated shader enters the map only when the game hands its bytes over at
  its first draw. The descriptors could not be rebuilt before the program is bound.

So on a game's first launch nothing is known and the title frame still builds everything at draw
time. What can move is everything a device has already recorded: the next launch after an OS update
or a cache namespace bump (an empty archive with the list kept), and the library, translation and
archive-lookup residue of every warm launch. The opt-in launch prewarm
(`NativeHost/graphics/README.md`, Launch prewarm of recorded pipelines) builds the recorded list
before the first frame, preparing shaders from the device's own first-use capture. The alternative,
building exact pipelines in the optional warm-up job, was not taken: on a new package that job runs
before anything is recorded, and it builds only vertex-pulling bases without a vertex layout, which exact draws never
use; building exact descriptors there would mean reimplementing the engine's descriptor construction.

Routes: exact, render scale 1.0, 1,800 frames, first-use capture on in every run (as in the app). Clean
is a fresh namespace (nothing recorded); warm reuses it; relist is a new namespace with the clean run's
recorded list, translated sources and capture but no archive. Main is c2c4c45's host, prewarm is this
change with `--prewarm` (budget 3,000 ms). Each was run twice in the order main then prewarm, then
prewarm then main, because macOS keeps one system Metal compiler cache for every process. Two Odyssey
routes (one main relist, one prewarm clean) ended with the same guest crash at the same address in
frames 6 to 11 on both hosts and were repeated as a third pair. Values are the two runs. Split is time
inside frames over 50 ms; first present is seconds from host start.

| Route | Worst frame, ms | > 50 / > 100 ms | Translation / library / pipeline, ms | First present, s |
|---|---|---|---|---|
| Zelda clean, main | 14,363; 13,252 | 19/13; 19/12 | 4,500 / 16,136 / 3,600; 4,811 / 14,865 / 3,594 | 1.08; 0.77 |
| Zelda clean, prewarm | 14,509; 5,780 | 18/14; 14/12 | 4,917 / 15,760 / 3,654; 4,265 / 2,390 / 3,535 | 0.76; 0.78 |
| Zelda relist, main | 2,652; 3,031 | 22/10; 15/10 | 258 / 185 / 3,556; 345 / 208 / 4,131 | 0.81; 0.87 |
| Zelda relist, prewarm | 367; 353 | 6/2; 6/1 | 7 / 0 / 0.2; 7 / 0 / 0.2 | 2.69; 2.86 |
| Zelda warm, main | 566; 742 | 9/3; 11/4 | 257 / 149 / 61; 347 / 174 / 85 | 0.61; 0.69 |
| Zelda warm, prewarm | 343; 404 | 5/1; 3/1 | 7 / 0 / 0.2; 8 / 0 / 0.2 | 1.13; 1.19 |
| Odyssey clean, main | 264; 213 | 3/3; 3/2 | 157 / 254 / 85; 171 / 134 / 81 | 1.16; 0.92 |
| Odyssey clean, prewarm | 204; 266 | 3/2; 3/3 | 167 / 117 / 84; 145 / 273 / 90 | 0.97; 1.06 |
| Odyssey relist, main | 71; 69 | 2/0; 2/0 | 8 / 5 / 75; 10 / 5 / 66 | 0.92; 0.90 |
| Odyssey relist, prewarm | 24; 26 | 0/0; 0/0 | 0 / 0 / 0; 0 / 0 / 0 | 0.95; 0.98 |
| Odyssey warm, main | 37; 36 | 0/0; 0/0 | none | 0.85; 0.85 |
| Odyssey warm, prewarm | 31; 30 | 0/0; 0/0 | none | 0.87; 0.91 |
| Castlevania clean, main | 305; 294 | 5/4; 5/5 | 206 / 249 / 106; 196 / 251 / 100 | 1.86; 1.93 |
| Castlevania clean, prewarm | 382; 261 | 10/8; 5/3 | 250 / 422 / 116; 116 / 9 / 112 | 1.86; 0.98 |
| Castlevania relist, main | 282; 216 | 5/3; 5/3 | 18 / 6 / 123; 15 / 8 / 110 | 0.89; 1.00 |
| Castlevania relist, prewarm | 226; 217 | 2/2; 2/2 | 0 / 0 / 0; 0 / 0 / 0 | 1.09; 0.92 |
| Castlevania warm, main | 292; 215 | 3/2; 3/2 | 9 / 4 / 1; 8 / 2 / 1 | 0.80; 0.75 |
| Castlevania warm, prewarm | 236; 214 | 2/2; 2/2 | 0 / 0 / 0; 0 / 0 / 0 | 1.18; 0.78 |

The prewarm's own time before the first frame: Zelda relist 1.84 and 2.02 s for 469 recorded lines
(264 shaders, 188 render and 17 compute pipelines, 177 bases compiled on two lanes), Zelda warm 0.17 to
0.20 s (archive hits), Odyssey 0.09 s for 15 lines, Castlevania 0.17 to 0.27 s for 24. Every recorded
shader and pipeline was claimed before the first draw, and the routes built no pipeline on the submit
thread afterwards. Clean routes have nothing recorded, so prewarm and main do the same work there; their
spread (Zelda's library time from 2.4 to 16 s) is Apple's compiler cache and shared load, not the
change. Zelda's worst relist and warm frames are now its title frame's other work (about 0.3 s:
guest work, 260 MB of first texture uploads, GPU), not shader waits. No route used a fallback pipeline
or skipped a draw. Odyssey's captured frames 300, 1200 and 1800 are byte-identical between main and
prewarm in clean, warm and relist, and between main runs; Castlevania and Zelda differ between two main
runs as much as between main and prewarm (their openings animate with wall time).

Bounded with prewarm (one Zelda relist) reached the 3,000 ms budget with 190 of 264 shaders: bounded is
hybrid, so the prewarm also loads every vertex shader's pulling library. Its worst frame was 1,020 ms;
the prewarm is meant for exact. Nothing here is a phone result: the phone has fewer cores, and its cold
library compiles after an OS update would make the prewarm slower, up to its budget.

# Prediction coverage and compiler lanes (2026-11-07, Mac)

Question: does building more of a frame's new pipelines at once (more compiler lanes, a larger
speculative budget for the prediction walk) shorten Zelda's title frame (120 new pipelines)? Exact
mode, relist routes (one clean seed run on main d6443ff's host; each relist copies its recorded list,
translated sources and capture into a new namespace with no archive), 1,800 frames, render scale 1.0.
`bench_shaders.py --lanes N --speculative N` sets the two measurement knobs (`DM_PSO_LANES`,
`DM_PSO_SPECULATIVE`; unset, nothing changes). Five other workers shared the Mac (load average 8 to
150) and Apple's system Metal compiler cache: library time in the title frame ranged from 0.15 to
36 s across runs of the same host, so frame totals are not compared. The value below is the submit
thread's pipeline time inside the title frame (merged spans of `pipeline-use`), one run each unless
noted.

| Host and setting | Submit pipeline time, title frame | Pipeline waits on the submit thread |
|---|---|---|
| main, lanes 2, engine budget 2 (three runs) | 3,179; 4,205; 4,456 ms | 119 |
| this branch, unset (same as main) | 2,760 ms | 119 |
| lanes 2, speculative 64 | 1,915 ms | 90 |
| lanes 4, speculative 16 | 2,241 ms | 106 |
| lanes 4, speculative 64 | 2,127 ms | 92 |
| lanes 6, speculative 64 | 2,348 ms | 92 |
| lanes 4, speculative 128 | 2,216 ms | 92 |

Every run passed with zero shader failures, zero fallback pipelines and zero skipped draws; a
predicted pipeline is taken only on an equal pipeline key, so no draw changes. A base compile costs
21 to 25 ms on this Mac (172 to 176 per route, 3.7 to 5.4 s in all) and a specialization 0.1 ms.

Why more lanes did not help: the title frame is one submission with one prediction walk, and that
walk computed only 40 keys not yet built (29 likely, 11 alternative attachment modes). With all 40
admitted the draws still built 81 pipelines themselves (108 with the engine's budget of 2), none a
merged-pass key. A classifier in the opt-in trace (`own-*` events: each pipeline a draw builds itself
against the last walk's keys) put 69 of those 81 in `own-unseen`: the walk never reached their draws.
The first miss of the submission is inside a called command list, and the walk's cursor started
without the replay's return stack, so it ended with that list and skipped the rest of its callers.
The engine's other look-aheads already follow that stack.

The fix (applied only when a speculative budget is set, so default modes are unchanged): the walk and
the library prefetch start with the replay's return stack. The title frame's walk then covers 1,372
to 1,412 draws and 109 likely plus 66 alternative keys, and 105 of the 108 pipelines the draws built
at the default budget were among them. Same seed, relist, after the fix:

| Setting | Title frame | Submit pipeline time | Draws building their own pipeline | Compile concurrency |
|---|---|---|---|---|
| fix, engine budget 2 | 3,184 ms | 2,581 ms | 108 | 1.08 |
| fix, lanes 2, ahead 128 | 2,434 ms | 194 ms own builds, 1,560 ms waiting for started ones | 3 | 1.90 |
| fix, lanes 4, ahead 128 | 1,536 ms | 166 ms own, 703 ms waiting | 3 | 3.71 |

The speculative builds started 272 pipelines on the route against 162 taken; the extra ones are
alternative attachment modes, and the route compiled the same number of bases (176 against 175), so
the extra work is mostly specializations (0.1 ms each).

Cold and warm opening routes, exact, 1,800 frames, main 3ed0a5e (A) against this branch with
`--lanes 4 --speculative 128` (C), run A then C, then C then A, then a third C on the final build.
Values are the runs in that order. Shader time is the stall split inside frames over 50 ms. The
branch with nothing set (Zelda, one run) behaved as main: 16/14 frames over 50/100 ms, pipeline
4,252 ms in slow frames, warm worst 853 ms.

| Route | Worst frame, ms | > 50 / > 100 ms | Translation / library / pipeline / walk, ms |
|---|---|---|---|
| Zelda clean, A | 8,749; 6,806 | 16/16; 16/13 | 6,209 / 8,492 / 4,292 / 38; 6,088 / 8,578 / 4,412 / 36 |
| Zelda clean, C | 7,593; 7,244; 5,363 | 30/15; 37/16; 14/13 | 6,875 / 3,836 / 2,310 / 189; 8,436 / 3,034 / 2,798 / 197; 4,810 / 4,627 / 1,445 / 161 |
| Zelda warm, A | 682; 678 | 9/4; 6/2 | 312 / 140 / 80 / 43; 240 / 134 / 77 / 36 |
| Zelda warm, C | 858; 802; 624 | 14/3; 17/7; 7/2 | 359 / 218 / 14 / 176; 388 / 206 / 13 / 164; 284 / 144 / 8 / 155 |
| Odyssey clean, A | 299; 233 | 3/3; 3/3 | 176 / 318 / 126 / 2; 158 / 164 / 87 / 1 |
| Odyssey clean, C | 246; 392 (a third ended at frame 11, see below) | 3/3; 3/3 | 187 / 193 / 97 / 1; 248 / 268 / 100 / 1 |
| Odyssey warm, A and C | 26 to 34 | 0/0 | none |
| Castlevania clean, A | 325; 355 | 5/5; 5/5 | 208 / 275 / 114 / 4; 240 / 394 / 136 / 4 |
| Castlevania clean, C | 323; 232 | 5/5; 5/3 | 192 / 272 / 121 / 4; 100 / 9 / 117 / 4 |
| Castlevania warm, A | 231; 237 | 3/2; 2/2 | 6 / 2 / 1 / 3; none |
| Castlevania warm, C | 219; 216 | 2/2; 2/2 | none |

What it changes: in Zelda's cold opening the title frame's pipeline time falls from 2.4 to 2.5 s to
0.6 to 1.1 s, pipeline time in slow frames from 4.3 to 4.4 s to 1.4 to 2.8 s, and library time from
8.5 s to 3.0 to 4.6 s (the prefetch now loads the whole frame's libraries in parallel); shader time in
slow frames falls from about 19 s to 11 to 14 s. The worst frame improves less (6.8 to 8.7 s against
5.4 to 7.6 s), because translation (5 to 8 s on this Mac, done one program at a time by the draws and
the walk) now dominates it; bounded mode is what moves translation. The extra frames
over 50 ms in C hold no shader time at all (the 14 to 21 frames of 50 to 100 ms are guest, GPU and pacing
time; some ran while the Mac's load average was over 100), so they are not shader stalls, but background
compiles competing with the game for the CPU cannot be ruled out on the Mac and must be checked on the
phone. In Zelda's warm opening the walk takes 155 to 176 ms against 36 to 43 ms, with almost nothing left to
build, and the worst frame (624 to 858 ms against 678 to 853 ms for main and the unset branch) moves
within the run spread; on the phone that longer walk is a cost to watch. Odyssey and
Castlevania build only a few pipelines and are unchanged within run-to-run spread.

Exactness: every route above passed with zero shader failures, zero fallback pipelines and zero
skipped draws. Odyssey's captured frames 300, 1,200 and 1,800 are byte-identical between A and both
passing C runs, clean and warm, and between two A runs; Castlevania and Zelda differ between two A runs as much as between A
and C (their openings animate with wall time). One Odyssey C clean route ended at frame 11 with exit
42 before any pipeline was predicted (the walk had 0 draws); earlier rounds saw the same early Odyssey
exit on both hosts, and the repeat passed.

So the scheduling is offered as a developer launch trial (`-SatchelPipelineLanes 4 -SatchelPipelineAhead
128`), not as a default: the cold gain is in pipeline and library waits, the warm title frame pays for
the longer walk, and the phone has two performance and four efficiency cores, so the right lane count
and the cost to the game's own threads need a phone trial. The final build repeated the relist: title
frame 1,647 ms, submit pipeline time 880 ms, 3 draws building their own pipeline. Nothing here is a phone result.

Ideas from the same round that were checked and not taken:

- Seeding a phone-prepared package's pipeline list at Prepare time. There is nothing to seed from:
  in every format 2 package `dm/shader_map.tsv`, `shader_pairs.tsv`, `pso_pairs.tsv` and
  `pso_manifest.tsv` hold one comment line, and a pipeline key needs the draw's attachment formats,
  blend and vertex layout, which only a run of the game produces. Prepare can find shaders in the
  RomFS (the opt-in warm-up's inventory), not pipelines.
- Metal binary archives for a first launch: the archive already keeps every base a launch builds
  for later launches; a first launch has none, and Apple's API gives no way to build one before the
  pipelines are known.

# Translation ahead (2026-11-07, Mac)

After the walk fix above, Zelda's cold title frame waited mostly for translation and libraries: the
draws and the prediction walk translated each of its about 150 new programs (264 keys in the route)
and compiled its libraries one at a time on the submit thread. The opt-in translation ahead
(`-SatchelShaderAhead <lanes>`, `bench_shaders.py --shader-ahead N`; `NativeHost/graphics/README.md`,
Translation ahead) sends the rest of the submission's programs to that many ahead lanes, in draw
order, at its first new shader and at its first pipeline miss, and holds each finished translation
and base library for its draw. The draws still prepare every program themselves; they make the same
requests in the same order, so nothing they draw changes.

Cold (`clean`) and warm opening routes, exact mode, 1,800 frames, render scale 1.0. A is main
f5872ae's host with `--lanes 4 --speculative 128` (the previous trial); T is this branch with the
same plus `--shader-ahead 4`; T2 with `--shader-ahead 2`; S is `--shader-ahead 4` with the pipeline
knobs unset; D is main with nothing set. Runs alternated (A, T, T2, S, D, then D, S, T2, T, A, then
pairs T, A). Five other workers shared the Mac (load average 4 to 15 at the start of every route below; one
A pair started at load 61 and ran at up to 120, and is left out). The routes do not clear Apple's shared Metal
compiler cache, and it was evicted between runs at random (its compile time for the route's
libraries, summed over workers, ranged from 1.2 to 46 s; given per run as "Apple cache"), so library
waits are compared only between runs with a similar compile sum. Translation is the application's own
work and does not depend on it. Values are the worst frame (Zelda's title frame) and its translation
(the draw's own plus the walk's look-ahead) and library waits on the submit thread.

| Route | Worst frame, ms | > 50 / > 100 ms | Translation / library wait in it, ms | Apple cache, s |
|---|---|---|---|---|
| Zelda clean, A | 10,942; 7,942; 10,544; 11,119 | 16/13; 15/12; 17/13; 17/14 | 2,274 / 8,228; 1,670 / 5,712; 2,042 / 7,973; 1,924 / 8,593 | 28.7; 16.7; 27.6; 23.0 |
| Zelda clean, T | 3,940; 5,158; 1,875; 6,848 | 14/12; 28/16; 13/11; 14/13 | 432 / 2,864; 511 / 3,586; 792 / 35; 482 / 5,577 | 26.3; 40.5; 3.5; 46.1 |
| Zelda clean, T with held translations (the third on the final build) | 6,253; 4,733; 6,092 | 15/13; 18/11; 17/13 | 632 / 4,758; 511 / 3,363; 438 / 4,700 | 46.1; 26.8; 40.6 |
| Zelda clean, T2 (the third with held translations) | 2,405; 2,047; 2,602 | 20/12; 18/10; 12/9 | 1,012 / 57; 1,053 / 40; 1,313 / 43 | 2.0; 1.2; 8.4 |
| Zelda clean, S | 3,540; 4,352 | 16/11; 14/12 | 330 / 13; 255 / 1,250 | 12.9; 30.0 |
| Zelda clean, D | 12,511; 6,063 | 14/13; 14/11 | 3,159 / 6,471; 2,992 / 488 | 21.2; 5.4 |
| Zelda warm, A | 709; 527; 761 | 7/3; 6/1; 7/2 | 179 / 79; 149 / 59; 183 / 81 | 0.1 |
| Zelda warm, T | 612; 593; 2,693; 787 | 5/1; 5/2; 14/10; 13/9 | 171 / 4; 175 / 3; 133 / 2,194; 20 / 709 | 0.2; 0.1; 17.1; 8.0 |
| Zelda warm, T with held translations (T2 third, final build last) | 487; 542; 381; 524 | 4/1; 3/1; 3/1; 6/1 | 48 / 6; 53 / 6; 43 / 3; 55 / 15 | 0.2 to 0.3 |
| Zelda warm, T2 and S | 528; 587; 593; 551 | 8/1; 7/2; 5/2; 4/1 | 153 / 3; 174 / 8; 161 / 2; 155 / 12 | 0.1 to 0.3 |
| Zelda warm, D | 858; 957 | 10/5; 8/4 | 237 / 99; 263 / 121 | 0.1; 0.3 |
| Odyssey clean, A | 442; 270 | 3/3; 3/2 | 127 / 249; 81 / 131 | |
| Odyssey clean, T (the third with held translations, the fourth on the final build) | 111; 82; 115; 80 | 3/1; 3/0; 3/1; 3/0 | 18 / 29; 19 / 0; 18 / 31; 16 / 3 | |
| Odyssey warm, A and T | 24 to 38 | 0/0 | none | |
| Castlevania clean, A | 224; 219 | 5/5; 5/4 | none in the worst frame | |
| Castlevania clean, T (the third with held translations) | 257; 210; 265 | 5/5; 5/3; 5/5 | 53 / 114; none; 61 / 112 | |
| Castlevania warm, A | 264; 216 | 4/3; 3/2 | none | |
| Castlevania warm, T (the third with held translations) | 232; 220; 215 | 3/1; 3/2; 3/2 | none | |

What it changes: the translation the submit thread waits for in Zelda's cold title frame falls from
1.7 to 2.3 s (A) and 3.0 to 3.2 s (D) to 0.43 to 0.79 s with four lanes, 1.0 to 1.3 s with two, and 0.26 to
0.33 s with four lanes and the pipeline knobs unset (the shorter default walk waits less for later
programs; their draws find them done). The libraries move off the submit thread with it: compiled on
the lanes before the walk reaches their program, so the title frame's library wait falls from 5.7 to
8.6 s to 2.9 to 5.6 s when Apple's cache is cold for the route (16 to 46 s of compiles; the lanes
compile at most four at a time) and to 35 to 57 ms when it is warm. Worst frames: 7.9 to 11.1 s (A)
against 3.9 to 6.8 s (T) with a cold Apple cache and 1.9 s (T) or 2.0 to 2.6 s (T2) with a warm one.
Odyssey's cold opening loses its frames over 100 ms (worst 270 to 442 ms to 82 to 111 ms). Castlevania's
slow frames hold no shader work and are unchanged. In warm openings the libraries the draws used to
compile again (the source store keeps sources, Metal keeps no library) are ready on the lanes, and with
held translations the title frame's translation wait falls from 150 to 180 ms to about 50 ms (worst
frame 381 to 542 ms against 527 to 761 ms). Two warm T routes ran after Apple's cache was evicted
(8 and 17 s of compiles) and waited 0.7 and 2.2 s for libraries; their A counterparts did not meet an
eviction, so those two are not a comparison. The extra frames over 50 ms in one T route (28) hold
guest and pacing time, not shader time, as in the previous round.

The walk that sends the jobs costs at most 1.2 ms per submission (Zelda: 18 or 19 per route, 3 ms in
all). The lanes do more work in all: translation summed over threads was 11 to 14 s per Zelda route
against 7.5 to 9 s, because concurrent translations run slower on the shared Mac (median 11 to 14 ms
against 7 to 9 ms) and a job whose padding no draw reaches (a vertex shader paired before its key's
mask grew) is translated and left unused (22 extra source-store entries in the first route, 440 against
418). That costs CPU and the game's threads share it; on the phone (two performance and four efficiency
cores) the lane count needs the trial.

Exactness: every route above passed with zero shader failures, zero fallback pipelines and zero
skipped draws. The draws translated the same 264 keys the same number of times as main (the same
multiset of `PhoneShader ... translated` lines) and left the same persisted vertex padding
(`wanted.json` equal). Odyssey's captured frames 300, 1,200 and 1,800 are byte-identical between A and
T, clean and warm, and between two A runs; Castlevania and Zelda differ between two A runs as much as
between A and T (their openings animate with wall time). The final build with nothing set ran
Odyssey's cold opening as main does (worst 265 ms, frames byte-identical to A); its first attempt ended
at frame 5 with the guest crash earlier rounds saw on both hosts, with every new code path inert, and
the repeat passed. Nothing here is a phone result.

Ideas from the same round that were checked and not taken:

- Reusing identical translations across stages and joining a shader translated twice in flight: the
  source store already joins identical requests in flight and serves later ones from disk, and the
  compiler joins and caches libraries by source hash, so two keys with the same source share one
  library. Distinct keys are distinct requests (the key is part of the request).
- More than four concurrent library compilations: the shared compiler allows at most four
  (`NativeShaders/ShaderCompiler.swift`, `setMaximumConcurrentCompilations`). With a cold Apple cache
  the lanes ran 3.2 compilations at a time on average and the title frame was bound by them. Raising
  the clamp is a NativeShaders change (it does not change translator output) and was not made; on the
  phone four compilations beside the game's threads are likely already the limit.

# Scheduling and cache behavior

Mapped shader registration copies control and code into bounded background requests.
Unmapped shaders retain exact first-use translation. Vertex library compilation waits
for the actual fragment-input padding or a previously observed startup request.
Fragment and compute base modules can compile after registration. Optional variants
compile only when required or previously observed. Two translation/library workers
share the existing compiler and persistent source store with Prepare and warm-up.
Once a launch has recorded requests, registration speculation yields to that exact
startup list. Replay takes at most 32 library variants, leaving space in the 64-library
live cache for demand instead of evicting its own work with a 128-request replay.

Exact pipeline creation uses two serial worker lanes. Dispatch is asynchronous and the
caller joins completion before encoding its draw. Nested work stays on the same lane.
New work reserves an available lane, so a long compile cannot trap later jobs behind
it while the other lane is idle. Only actual Metal creation calls occupy lanes;
duplicate base requests wait outside them, leaving unrelated work able to progress.
The pending-pipeline fixture retains its 52 checks,
with demand compilation and prediction ordering verified on the worker thread.
The default pipeline policy is `exact`: Metal 4 and persistent archives remain enabled,
with hybrid stand-ins disabled. Diagnostic `hybrid` and `legacy` policies remain explicit
launch options. The bounded graphics suspend overlay is unchanged.

The source store validates translator identity and payload hashes; Metal library keys
include source hash, GPU, OS build and compile options. Pipeline archive namespaces add
the pinned engine and translation chain to GPU and OS identity. Prepare and runtime use
the same archive directory. Update the namespace revision when pipeline construction or
translation semantics change.

The namespace is `<engine pin, 8 digits>-<chain>-v<revision>-<GPU>-<OS build>`
(`NativeHost/graphics/PipelineSettings.m`). It is `f8a5618d-26dc2fa9477c-v2` since the
engine pin move of 2026-11-05: the pin, the chain and the revision all changed, because
the new engine builds pipelines differently (one `_bx` side library for the alpha test
and logic operation entry points, continued render passes, sparse and chunked pools).
Archives under the earlier `bbc3b737-e65a4ebd3322-v1` namespace are left on disk and are
never read again; every game rebuilds its pipelines on first play after the update.
It is `f8a5618d-26dc2fa9477c-v3` since the phone's shader translator moved to the engine pin
(also 2026-11-05): the translated shaders changed, so `-v2` archives are not read either.

A source cache hit does not prove that Metal avoided compilation. Apple's public
[MTLLibrary API](https://developer.apple.com/documentation/metal/mtllibrary/)
provides no executable-library serialization method. The existing Metal 4 archives
persist pipeline bases; the OS manages the source compiler's own cache. No private
metallib extraction or unsupported serializer is used here.

The runtime source working set is 64 MiB. Each retained resource pool has at most 64
compiler library references; the pool retains three game directories. Speculative
requests are limited to 64 outstanding jobs per resource, 32 MiB for translation input
and 64 MiB for compilation input. Startup metadata retains 128 request identities.
These are bounds on owned references and queued inputs, not a bound on Metal's internal
allocation or the renderer's existing shader and pipeline caches. Measure process RSS,
GPU allocation, and persisted cache size on the route and on a scene-switch soak before
claiming a total memory bound.
