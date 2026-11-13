# Diagnostic build

A build of the game from this tree with the QA layer and automatic per-race
profiling (`MKW_AUTO_PROFILE`, no `MKW_RELEASE`). It is packaged as its own
title, `000400000F4B5D00` "MKW3DSDIAG", so it installs beside the release CIA
and shares its SD data (`sd:/3ds/mkw-3ds/`).

For developers measuring performance. To play the game, build the standard
version with `tools/build.py` (see the main README).

## Inputs

Run `tools/build.py` once first. These scripts reuse its work directory
(`MKW_WORK`, default `build/standard`): the libraries, shaders and generated
headers in `compile/`, the HOME banner in `home-banner/production/` and the
makerom it found or downloaded. Nothing else outside this repository is read.

| Variable | Contents |
| --- | --- |
| `MKW_WORK` | the standard build's work directory (default `build/standard`) |
| `DEVKITPRO` | devkitPro root (falls back to `C:/devkitPro`, `/opt/devkitpro`) |
| `MKW_BANNER` | another banner instead of the standard build's |
| `MAKEROM` | makerom, when it is neither on `PATH` nor in the standard build |

## Build

```sh
python tools/diag/compile.py       # build/diag/mkw-3ds-diag.elf
python tools/diag/package_cia.py   # build/diag/...-DIAG.cia
```

`compile.py` recompiles only changed translation units and links against the
standard build's `libcitro3d.a` and `libCFL.a`. `package_cia.py` forces title
version 142, verifies every TMD hash, and writes `build-info.json` with the
commit and hashes.

## Unattended benchmark (32 courses)

Copy both files from `tools/diag/benchmark/` into `sd:/3ds/mkw-3ds/`, start
the DIAG title and leave the console on charge. It goes through the menus,
races all 32 courses in order in VS with the CPU driving your kart, then exits
to the HOME Menu (about 100 minutes).

- `menu-autoplay-result.json` says whether the script finished.
- `perf-races.csv` has one row per race; each race also writes
  `perf-race-NNN-*.csv` (per second, spikes, passes, draws, skinning).
- After every race the game writes `benchmark-progress.txt` with the next
  course (0–31, cup order). A stopped run resumes there; delete the file to
  start again from Luigi Circuit.
- A race with no forward progress for 60 s is finished on the spot and
  listed under `"stuck"` in the result file; leave those rows out of
  comparisons.
- `benchmark-course.txt` (a course number, 0–31) races every race of the
  set on that course instead, for a short single-course A/B. With it,
  `unit1-alternate.txt` turns `no-unit1-vram.txt`'s change on for the first
  race load and off for the next, and so on (`runtime.log`: `unit1 vram
  on/off`); every load then places the course textures again. The set still
  counts 32 races: end the script with `quit` after the last race you want.
- The benchmark forces VS rules to Courses: In Order, Races: 32, which can be
  saved to your license. Delete both files afterwards, or the DIAG title
  autoplays every launch.

`perf-race-NNN-passes.csv` leaves one pass out of one frame in eight, in
turn, and compares that frame's GPU time with its neighbours. Its `bottom`
row leaves out the whole lower screen (no target bind, no draws, no display
transfer); `bottom_quads` keeps the lower target and its display transfer
(nothing is drawn on it, so its picture stays) and drops only its draws, so `bottom` minus `bottom_quads` is the lower
screen's fixed cost and `bottom_quads` its draws and fill.

## A/B switches

Create an empty file in `sd:/3ds/mkw-3ds/` to turn one change off:
`no-pipeline.txt`, `no-bloom-merge.txt`, `no-bottom-cover.txt`,
`no-shadow-rect.txt`, `no-player-vram.txt`, `no-draw-distance.txt`,
`no-spatial-course.txt`, `no-hud-shrink.txt`, `no-fakebox-rainbow.txt`,
`no-fakebox-sparkle.txt` and `no-itembox-sparkle.txt` (Fake Item Box or Item Box sparkle groups share one noise offset), `no-sky-last.txt` (Rainbow
Road always draws its sky first), `no-load-cache.txt`, `no-plain-uv.txt`,
`no-fast-vertex.txt` (model draws without environment maps, projected
textures, light textures or shadow passes go back to the vertex shader's
general path; the `fast_vertex` column), `no-sparkle-pair.txt` (Item Box / Fake Item Box
outer passes one per sparkle group again instead of the shell with group 0, then groups
1 and 2 together), `no-rigid-skin.txt` (lit skinned draws whose bones are all rotations
times a uniform scale take the cofactor normal path again; runtime.log `lit skin draws`
counts both), `no-bloom-triple.txt` (bloom blur rings back to two taps a quad instead of
three), `no-hook-prep.txt` (CPU driver poses, object CPU skinning and fire
phase copies run on the frame hook again instead of at frame start; runtime.log
"hook prepare off"), `no-skin-direct.txt` (course objects' CPU skins and Coconut Mall's
escalator treads are written on the frame hook into the copy the last frame drew, as
before, instead of at frame start into a copy no unfinished frame draws; the escalators
then also skip treads out of view or at the phase their copy already holds),
`no-stadium-clear-skip.txt` (DK Summit, Waluigi Stadium and Funky Stadium capture frames
without bloom clear the left LCD's colour again, although the capture then covers it),
`no-clip-reuse.txt` (the clip fuse
multiplies projection x modelView on every upload again instead of reusing an unchanged
product or drawNow's culling product; exact: `cmd-hash.txt` equal either way),
`no-box-precull.txt` (Item Box / Fake Item Box shells are drawn whole
again instead of without the triangles their cull mode removes; runtime.log
`box precull` counts the triangles kept), `no-fast-yaz0.txt` (course archives decode
with retail's Yaz0 loop again instead of the faster one with the same bytes; runtime.log
`kinoko scene profile` has the decode time), `no-vram-priority.txt` (course and
sky textures take VRAM in file order instead of sky first, then by covered
surface per byte; the `vram_priority` column). Each race's `perf-races.csv` row
records which were on. `no-sector-reads.txt` reads pack entries exactly as
stored instead of as whole 512-byte sectors (load times only).
`no-read-ahead.txt` reads only the requested entry instead of the whole entries
that follow it within 12 KB (128 KB among entries a repacked pack lays out in
traced order). `no-race-cache.txt` turns off the New 3DS RAM cache that keeps
the racers, items, effects, Lakitu and pause menu textures read by one race
load for the next.
`no-sound-cache.txt` reads the race sound waves from revo_kart.brsar on every
race load, as before, instead of keeping each range read on the New 3DS heap
(up to 8 MB) for the set's later races and reading new ranges unbuffered, in
256 KB pieces while no stream needs refills (`runtime.log`: `sound ram cache`).
`no-set-reuse.txt` releases the opponents and the item/HUD assets after every
race, as before; by default (New 3DS) the next race of a set takes them again
when its racer has the same character, vehicle and team and the settings are
unchanged, with their baked light redone for the new course and every race
state reset (`runtime.log`: `set reuse opponents N of M`, `set reuse items and HUD`).
`no-pause-keep.txt` releases the racers' graphics on every pause, as before.
`no-bank-keep.txt` makes a race load read every animation bank again instead of
reusing the banks the last race had loaded (`runtime.log`: `animation banks reused N, unused M`).
`no-prefetch.txt` turns off the background thread that reads the next 128 KB
of a repacked pack's traced entries while the game parses the last read.
`pack-trace.txt` writes every pack read to `pack-trace.csv` (archive, offset,
size, ms, read-ahead hit, matches a direct read) for `tools/pak_order.py`.

Old 3DS only:
`o3ds-core1.txt` puts the render thread and the job worker back on core 1
(with an 80% CPU time limit), as before; by default every race frame is
recorded and drawn on core 0 and core 1 keeps only audio (`runtime.log`:
`Old 3DS: race frames and presentation jobs on core 0`).
`bottom-half-rate.txt` redraws the race's lower screen on every second frame
on Old 3DS too (15 Hz there); by default Old 3DS redraws it every frame (30 Hz,
like the upper screen). New 3DS redraws it every second frame (30 Hz);
`no-bottom-30hz.txt` redraws it every frame there. A frame not due leaves the
lower LCD's last race picture up (`bottom keep: … skipped=…`).
`bottom-rgba8.txt` keeps the lower screen's UI atlas in RGBA8; by default Old 3DS
stores it as RGBA4 (256 KB less VRAM, so more course textures leave LINEAR).
`full-quality.txt` turns off every Old 3DS detail reduction.
On Old 3DS the pause textures move to the VRAM the course textures left above
their 1 MiB reserve (`no-pause-vram.txt` keeps them in LINEAR), and the pause
menu is not preloaded when that would leave under 1.5 MiB of LINEAR
(`runtime.log`: `pause preload linear KB: ... (pause textures in VRAM N)`, or
`Old 3DS: pause menu not preloaded (LINEAR)`; race end lines add `linear_low`,
the lowest LINEAR seen at the 300-step heartbeats). A failed
`new` writes `FAIL out of heap: ...` to `runtime.log` before the game aborts.

Render CPU switches (each change is exact unless noted; both consoles unless
noted): `no-light-blend.txt` blends the course light textures with the
reference loop and rewrites and flushes them every frame, instead of through
tables and only when their bytes change. `no-bloom-list.txt` builds bloom's
GPU commands every frame instead of copying the commands recorded on the
race's first bloom frame (immediate mono frames: Old 3DS). `no-c3d-fast.txt`
makes citro3d compare its register shadows with memcmp and scan every uniform
on each draw, instead of word compares and a per-shader "any uniform dirty"
flag (the same GPU commands). `no-tex-cache-keep.txt` clears the GPU texture
cache on every texture bind again, instead of only when a unit's registers or
the enabled units change, or after a frame, target or command-list boundary or
`C3D_TexFlush` (runtime.log `texture cache` counts clears and kept caches).
`no-lazy-parts.txt` builds a racer's wheel and
accessory matrices together with its chassis matrix instead of on their first
read (the same values). `no-pose-share.txt` evaluates an animation bank's pose
again for each model it is applied to (race items pose a lit and an unlit
copy) instead of reusing the pose its other model already holds.
`no-gpu-pose.txt` poses race items (Bob-omb, Blue Shell, Blooper, POW, Bullet Bill)
and Lakitu's non-rigid parts on the CPU each frame again instead of skinning them on
the GPU from one palette (the hook-writes CSV's `driver pose cpu` rows). Every DIAG
run also writes `cpu-pose-banks.txt`: each animation bank posed on the CPU, once,
with its bone and vertex counts. `pause-transition.txt` gets each pause's stages
(appended) when its menu appears; the first pause's `pause wait frame` lines in
`runtime.log` say what it waited for.
`runtime.log` also lists `course draws step=… render_ms=…` every 600 steps: the render
thread's time in the course pass's model draws per profiled frame (first eye), the
share a recorded course command list could save.
Old 3DS runs the presentation jobs (racer posing, the local racer's pose) on a
job worker on core 1 at 30%; `no-core1-jobs.txt` keeps them on core 0. The worker stops for the award ceremony and starts
again with the next race load; `core1-ceremony-keep.txt` keeps it running.
The ceremony logs `ceremony hb: ...` lines (phase, frame, tick, drive, sound,
loader, worker) about once a second and `ceremony watchdog ...` when the main
thread stops coming round a loop for 3 s or more; `award-load-times.jsonl` gets
`board` when the result board shows (a GP finished 4th or lower has no
`podium` and waits there for A).
`no-live-light.txt` goes back to light baked into the vertices at load for
released items, other racers' items and course objects (crowds stay baked);
by default their G3D light is computed in the vertex shader, from the camera's
side as on the Wii. `no-rear-bake.txt` bakes the opponents' light as seen from
the front, as before, instead of from behind (no cost either way).
`live-opponents.txt` lights the opponents in the vertex shader like the local
racer (full driver banks, lit GPU skinning; LOD models stay baked).
`no-light-zones.txt` keeps every racer on BLIGHT light 0 and ambient 0 instead of
the light zone under it (KCL bits 8-10; lit racers only).
`no-wii-driver-lod.txt` keeps opponents' drivers at full detail at any
distance with Opponent Detail Full; by default a driver beyond 4,000 units of
camera distance draws its model_lod, as on the Wii (bodies, wheels and
accessories stay full; `runtime.log`: `race capture rNN-NNNNN low drivers=N`
beside each race-capture.txt frame).
`no-opp-pres-thin.txt` presents opponents not drawn up close every tick
instead of once per drawn frame; `no-kartfx-lod-thin.txt` steps LOD racers'
kart effects every tick instead of every 4th.

Render thread and course objects (each exact; both consoles):
`no-state-cache.txt` builds every submesh's combiners again on each draw
instead of copying the ones its last setup built while their inputs are
unchanged (`state-cache-verify.txt` builds them anyway and counts mismatches).
`no-quad-merge.txt` draws each font sprite and material-quad command alone
instead of merging consecutive ones with the same texture and constants.
`no-volume-rects.txt` shades shadow volumes over their union rectangle only,
instead of one rectangle per volume when those cover less.
`no-static-precull.txt` runs every course object's presenter instead of
skipping objects whose padded world box is clearly outside the view
(`precull-check.txt` runs the skipped ones anyway and counts any that would
have drawn; `object-set.txt` writes each frame's object records hashed to
`object-set.csv`). `no-reset-skip.txt` resets overrides, rigid pose and skin
palette after every object record again, not only after records that set
them. `runtime.log` lists the counts every 600 steps (`render switches`).
`draw-timing.txt` adds per-second columns for drawNow: `draw_setup_ms`
(material setup), `draw_call_ms` (C3D_DrawElements), `draw_reset_ms`
(end-of-draw resets), `draw_now_ms`, `draw_setups` and `state_hits`. Its
timer calls add render-thread time: take on-time numbers from a run without it.
`cmd-hash.txt` writes `cmd-hash.csv`: each frame's GPU command words hashed,
with its animation tick and which of the two sets of per-frame buffers it
used (their addresses are in the words); `cmd-dump.txt` (ticks) saves those
frames' words as `cmd-dump-<tick>-<frame>.bin`. Compare runs of one build
only. With race captures, `capture-no-drop.txt` waits for the render thread
instead of dropping frames, so two runs draw the same frames.

## Repacked packs

`tools/pakpack.c` (build: `gcc -O2 -o pakpack tools/pakpack.c`) rewrites an
MPK1 pack as MPK2: entries in the order `tools/pak_order.py` gives from pack
traces, LZ4-compressed where that saves space (lossless; every block is
checked against its source), loose files optionally added
(`textures/firebar-indirect.ftan=...`). The game reads MPK1 and MPK2.
`tools/build.py` does this for both packs with the read order saved in
`tools/pak-order/` (the traced part of `pak_order.py`'s output: its first
3,803 race.pak and 276 menu.pak lines). After new traces, regenerate the
order and save its traced lines there.

Two files turn an Old 3DS reduction back on for a New 3DS comparison:
`racer-lod.txt` (opponents switch to low-detail models with distance; the
`racer_lod` column) and `animation-budget.txt` (crowds and animated items drop
to 30/15 Hz when frames run slow). Without them every opponent keeps full
detail and animations run every tick, as in the Wii's single-player races.

The per-second and spike files also log `hook_ms` (the deferred writes run
between two GPU frames) and `gpu_gap_ms` (how long the GPU sat idle before
each frame's commands were queued). The per-second file adds `gpu_gap_min_ms`,
the second's smallest gap: what a frame that was ready in time still waits.
`no-unit1-vram.txt` keeps 4-bit textures that texture unit 1 samples (the
baked shadow maps) in LINEAR; without it their ETC1 goes to VRAM as ETC1A4.
`runtime.log` reports after each race load what stays in LINEAR (`course
textures left in LINEAR track+sky=… unit1=… objects=…`, count/KB; a texture
several object models share counts once), then one `linear object texture
model=…` line per object texture still in LINEAR.
`no-bloom-release.txt` keeps the bloom targets a stadium-screen course (DK
Summit, GCN Waluigi Stadium, Funky Stadium) or a Bloom: On race created
(~1.1 MB of VRAM) through later races with Bloom: Off and no stadium screen;
by default they are freed before the course textures move to VRAM.
`no-shared-depth.txt` gives the top LCD target its own depth buffer while
bloom's targets exist (Bloom: On, stadium-screen courses). By default it uses
the scene target's depth buffer (384 KB less VRAM), which is cleared again
after the scene; runtime.log `texture cache ... shared depth=1`.
`no-texture-share.txt` loads every course object model's textures as cooked;
by default a texture identical to one an earlier object model of the course
loaded (size, format, sampler state, every byte) uses that copy (Daisy
Circuit's MiiObjD signs, Coconut Mall's cara1-3; `runtime.log`: `course
objects share textures copies=… bytes=…`).
`texture-use.txt` writes `perf-race-NNN-textures.csv` after each race: every
course, sky and object texture (each once, under its first model), then every
other texture the race loaded from a file (`file:<name>`: opponents, items,
HUD, effects, Lakitu, transitions), whether it is in VRAM or LINEAR, its size,
format and bytes, and the draws per frame that bound it on any unit (models,
sprites, HUD quads, effect batches); with `model-pixels.txt` also the screen
pixels those draws covered (sprites and quads always; GPU-skinned meshes and
effects count 0). Slow; for Azahar runs.
`no-racer-vram.txt` (New 3DS) leaves opponent, item, HUD, effect and Lakitu
textures in LINEAR; by default they take the VRAM the course left after the
opponents load, down to the race reserve (`runtime.log`: `race textures in
VRAM moved=…`, and a `race texture` line for each).
`racer-vram-4bit.txt` lets that move take 4-bit textures (ETC1, L4, A4) too,
except those marked as sampled on texture units 1 or 2 (`Texture::unitOne`):
check `gpu-guard.log` has no `4-BIT VRAM` line, and run it on the console
through kart select and a race load before making it the default.
`stereo-slider.txt` (a number from 0 to 1) pins the 3D slider for comparable
runs. As with the slider, the right eye turns on above 0.03 and off at or below 0.01.
`lod-bias.txt` (a number from -2 to 0, default 0) is added to the LOD of every
mip-mapped texture: negative values sample sharper levels (`runtime.log`:
`texture lod bias N`).
`no-menu-pipeline.txt` draws menus immediately on the main thread (CPU work,
then the GPU, one after the other) instead of through the render thread.
`no-menu-vram.txt` keeps the New 3DS menu sprites in LINEAR; by default the
sprites a frame draws move to VRAM (a GPU copy inside the frame), most screen
area per byte first, while 2.5 MiB of VRAM stay free, and sprites no page
needs go back to LINEAR to make room (none while a race is loaded; `runtime.log`:
`menu vram page=… sprites=… KB copied in … out …`).
`no-menu-bottom-keep.txt` clears and redraws the lower LCD on every menu frame;
by default pages that only use the upper LCD (Strap, Intro, Pause, Quit) draw it
once and then leave its picture (no clear, no draw, no transfer) until the page,
its fade over the paused race, the loading wheel, a loading screen or the HOME
Menu or sleep changes it.
`no-menu-cover.txt` clears the upper LCD and fills it black under every menu
backdrop again; by default an opaque backdrop drawn at full alpha over the whole
screen only clears depth (2D frames).
With `menu-capture.txt`, each capture also writes `…-lcd.ppm`: the lower LCD's
last presented picture.

The race's lower screen (each file read at the race load):
`rows-margin.txt` (pixels, e.g. 0.5) sets the map margin above the first
standings row and below the last (default 1); `timer-scale.txt` (0.3-1) draws
the timer at that scale instead of its cooked 21 px digits (21/32). For
comparisons only.
`no-race-bottom-keep.txt` redraws the lower screen on every race frame; by
default a frame whose quads, textures and draws equal those of the previous
frame, which was shown, leaves the lower LCD's picture as it is (no draw, no
clear, no transfer), mostly through the countdown. `runtime.log` has `bottom
keep: course=… kept=… drawn=…` for each race.
`no-bottom-depth-off.txt` binds the lower target with depth reads and writes
on again for the race's 2D lower screen; by default it is bound without them
(nothing there tests or writes depth). The intro's 3D lower view keeps depth.
`no-bottom-depth-free.txt` gives the lower target its own depth buffer in races
again; by default it has none there (300 KB more VRAM for the course textures),
and the intro's 3D lower view uses the top LCD's depth buffer once the upper
screen is drawn, cleared first (`runtime.log`: `bottom screen … vram_free=…`).
The Settings page's options are in `settings.dat`, not in the license:
Dynamic Shadows for benchmarks is set there (or on the Settings page).

## Ghost sync check

`ghost-test.txt` with a script whose Time Trial drives `race:N:items` (A held,
L pressed at frames 360, 660 and 960): the first 20 s of the race are saved as
the course's ghost. Race that ghost (Your Ghost on the ghost page) with the
same script: `runtime.log` lists `ghost test: frame=… distance=…` every 60
frames. A distance that stays at 0 means the recorded inputs replay exactly,
mushrooms included.
