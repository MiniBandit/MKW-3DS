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

## A/B switches

Create an empty file in `sd:/3ds/mkw-3ds/` to turn one change off:
`no-pipeline.txt`, `no-bloom-merge.txt`, `no-bottom-cover.txt`,
`no-shadow-rect.txt`, `no-player-vram.txt`, `no-draw-distance.txt`,
`no-spatial-course.txt`, `no-hud-shrink.txt`, `no-fakebox-rainbow.txt`,
`no-fakebox-sparkle.txt` (Fake Item Box sparkle groups share one noise offset), `no-sky-last.txt` (Rainbow
Road always draws its sky first), `no-load-cache.txt`, `no-plain-uv.txt`,
`no-fast-vertex.txt` (model draws without environment maps, projected
textures, light textures or shadow passes go back to the vertex shader's
general path; the `fast_vertex` column), `no-vram-priority.txt` (course and
sky textures take VRAM in file order instead of sky first, then by covered
surface per byte; the `vram_priority` column). Each race's `perf-races.csv` row
records which were on. `no-sector-reads.txt` reads pack entries exactly as
stored instead of as whole 512-byte sectors (load times only).
`no-read-ahead.txt` reads only the requested entry instead of the whole entries
that follow it within 12 KB (128 KB among entries a repacked pack lays out in
traced order). `no-race-cache.txt` turns off the New 3DS RAM cache that keeps
the racers, items, effects, Lakitu and pause menu textures read by one race
load for the next.
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
(15 Hz on Old 3DS); by default it redraws every frame (30 Hz there, like the
upper screen).
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
flag (the same GPU commands). `no-lazy-parts.txt` builds a racer's wheel and
accessory matrices together with its chassis matrix instead of on their first
read (the same values). `no-pose-share.txt` evaluates an animation bank's pose
again for each model it is applied to (race items pose a lit and an unlit
copy) instead of reusing the pose its other model already holds.
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
`no-opp-pres-thin.txt` presents opponents not drawn up close every tick
instead of once per drawn frame; `no-kartfx-lod-thin.txt` steps LOD racers'
kart effects every tick instead of every 4th.

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
textures left in LINEAR track+sky=… unit1=… objects=…`, count/KB).
`stereo-slider.txt` (a number from 0 to 1) pins the 3D slider for comparable
runs.
`lod-bias.txt` (a number from -2 to 0, default 0) is added to the LOD of every
mip-mapped texture: negative values sample sharper levels (`runtime.log`:
`texture lod bias N`).
`aa2x.txt` (New 3DS only, off by default) renders the race's 3D scene at
800x240 and halves it to the top screen: 2x horizontal anti-aliasing. Off in
3D (stereo) and during the course intro; menus and the HUD are unchanged. It
needs about 0.8 MB more VRAM, which the course textures give up
(`runtime.log`: `aa2x scene target on/unavailable`).
`no-menu-pipeline.txt` draws menus immediately on the main thread (CPU work,
then the GPU, one after the other) instead of through the render thread.
The Settings page's options are in `settings.dat`, not in the license:
Dynamic Shadows for benchmarks is set there (or on the Settings page).

## Ghost sync check

`ghost-test.txt` with a script whose Time Trial drives `race:N:items` (A held,
L pressed at frames 360, 660 and 960): the first 20 s of the race are saved as
the course's ghost. Race that ghost (Your Ghost on the ghost page) with the
same script: `runtime.log` lists `ghost test: frame=… distance=…` every 60
frames. A distance that stays at 0 means the recorded inputs replay exactly,
mushrooms included.
