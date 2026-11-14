# Building by hand

`tools/build.py` runs every stage below (`--list-stages` prints them in
order). It starts each stage once the earlier stages it reads from are done,
so several run at once (`Builder.AFTER` in `tools/build.py`); `--jobs 1` runs
them one at a time, in this order. `--only STAGE` runs one stage again. This
page is for understanding a stage or running one yourself.

The converters write next to themselves, into `data/`, `assets/`, `build/`,
`sd-card/` and `include/mkw/`. Run them on a copy of the tree, not your
checkout: `tools/build.py` keeps that copy in `build/standard/source/`, and
every command below runs from there. `{disc}` is your RMCE01 image, `{jobs}`
the number of parallel jobs.

## 1. Tools and converters

devkitARM, libctru, picasso, tex3ds, 3dsxtool, Python 3.12, Node.js, Git,
ffmpeg (with soxr), makerom, bannertool and a host C++23 compiler. The pinned
versions are in `dependencies.json` (converters) and
`tools/release/toolchain.lock.json` (host tools).

```sh
git clone https://github.com/magcius/noclip.website vendor/noclip      # then check out each pinned commit
git clone https://github.com/LagoLunatic/gclib vendor/gclib
git clone https://github.com/jamesrhg/cfl-tool vendor/cfl-tool
git clone https://github.com/skyfloogle/pycgfx.git vendor/pycgfx
git -C vendor/noclip apply --ignore-whitespace ../../patches/noclip-brres.patch
py -3.12 -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
npm ci --no-audit --no-fund
node tools/bundle_cooker.mjs tools/export_brres.ts   # and every other tools/export_*.ts
```

Install the Python requirements after `vendor/gclib` exists; it is an editable
dependency. On Linux or macOS, create the environment with
`python3.12 -m venv .venv` and use `.venv/bin/python`. For the later commands, `python` means the
interpreter in this environment.

## 2. Extract the disc

```sh
python tools/extract_disc.py {disc} data/extracted --prefix Race/ --prefix Scene/ --prefix thp/ --prefix sound/ --prefix Boot/ --prefix rel/ --prefix Mii/
```

Then `tools/build.py` (`Builder.extract`):

- writes `build/original/main.dol` from the disc's data partition and copies
  `rel/StaticR.rel` to `build/itembox-retail.rel`;
- unpacks every `Scene/UI` and `Scene/Model` archive to `assets/menu-ui/` and
  `assets/menu-models/`, `Race/Common.szs` to `assets/common/`, each course in
  `tools/course_catalog.py` to `assets/courses/<key>/`, and `Race`, `Race_U`
  and `Font` to `assets/race-ui/`;
- moves course archives the catalog doesn't use to `data/extra-courses/`.

## 3. Convert the resources

The order matters where one stage reads another's output. The finish-line
policy comes before the courses, the default Mii portraits before the course
objects, all 42 courses before the course phase, road and lighting tables,
all 288 driver/vehicle choices before the vehicle stages, and everything
before the menu texture fit.

| Stage | Command |
| --- | --- |
| startline | `generate_startline_policy.py` |
| portraits | extract `contents/RFLRes01.arc`, `cook_mii_bank.py`, compile `tools/export_mii_head.cpp` with `src/mii_head.cpp` and `src/mii_part_bank.cpp`, then `cook_default_mii_portraits.py build/original/main.dol ... --statue-courses ...` |
| courses | for each course: `node build/export_brres.mjs` on `course_model.brres` and `vrcorn_model.brres` (with `MKW_FREEZE_NONLINEAR_UV=1 MKW_COURSE_MATERIALS=1 MKW_RETAIL_LIGHTING=1`, plus `MKW_RETAIL_SKY=1` for the sky), `cook_model.cook`, a receipt in `models/courses/NN.json`; then `course-catalog.json` and `generate_runtime_tables.py catalog` |
| garage | for each driver and vehicle: unpack `Race/Kart/<vehicle>-<driver>.szs`, `cook_garage.cook_choice`; then `garage.json`, `roster.json` and the native roster and garage headers |
| objects | `cook_course_objects.py --ids 0..41 --jobs {jobs} --mii-heads assets/mii/course-portraits/assignments.json` |
| course tables | `cook_course_lighting.py`, `cook_course_bloom.py`, `generate_course_phases.py`, `generate_road_policy.py` |
| tables | `generate_runtime_tables.py math`, `generate_runtime_tables.py billboards`, `cook_gp_rank.py`, `cook_driver_look.py` |
| menus, part 1 | `cook_menu_lighting.py`, `cook_menu_animations.py` |
| battle | `cook_battle_menu_text.py`, `cook_battle_hud.py`, `cook_battle_hud_motion.py`, `cook_battle_bottom_scores.py`, `cook_battle_results.py`, `cook_battle_screens.py` |
| race | `cook_race_intro.py`, `cook_mii_voice.py build/itembox-retail.rel`, `cook_audio.py`, `cook_race_items.py sd-card/3ds/mkw-3ds --disc {disc}`, `cook_race_flow.py`, `cook_race_balloon.py`, `cook_menu_mii_bodies.py`, `generate_native_icon.py` |
| effects and objects | `cook_race_effects.py --course-only --output build/vfx/textures`, `cook_lakitu.py`, `cook_hanachan_joints.py`, `cook_pokey_joints.py`, `cook_object_sounds.py`, `cook_battle_balloons.py`, `cook_battle_coins.py`, `cook_battle_objects.py`, `cook_race_effects.py --balloon-only --output build/battle-balloon-effects/textures`, `cook_race_effects.py --coin-only --output build/battle-coin-effects/textures`, `cook_coin_hud_stack.py`, `cook_blast.py`, `cook_blooper.py`, `cook_luma.py`, `cook_coconut_objects.py`, `prepare_firebar_stream.py` |
| native Mii resource | `stage_native_mii_resource.py --none` (the game reads the console's own; or give a `CFL_Res.dat` to package it) |
| star colors | `node build/export_star.mjs assets/common/driver.brres include/mkw/star_colors.hpp` |
| awards | `prepare_award_assets.py {disc}`, `node build/export_award_scene.mjs build/award-source`, `cook_award_cameras.py`, `cook_award_drive.py`, `cook_award_lighting.py`, `cook_award_models.py --jobs {jobs}`, `cook_race_effects.py --award-source build/award-source --output build/vfx/textures` |
| credits | `cook_staff_camera.py`, `generate_staff_crowd.py`, `cook_staff_ui.py --retail-rel build/itembox-retail.rel --output build/staff` |
| maps | `cook_race_map.py --jobs {jobs}`, `cook_map_objects.py` |
| menus, part 2 | `cook_menu_models.py --machines --jobs {jobs}`, `generate_runtime_tables.py audio`, `cook_menu_ui.py --jobs {jobs}`, `cook_menu_footer.py`, `cook_menu_styles.py`, `cook_menu_record_text.py`, `cook_cup_record_icons.py`, `cook_menu_movies.py --header-only`, `cook_menu_movies.py --jobs {jobs} --skip-header`, `cook_menu_movie_chapters.py`, `cook_menu_audio.py`, `cook_staff_movies.py --output build/staff --jobs {jobs}` |
| Mii racers | `cook_mii_racers.py {disc} build/mii-racers --jobs {jobs}`, `finish_mii_racers.py {disc} build/mii-racers --jobs {jobs}`, `cook_mii_awards.py build/mii-racers` |
| vehicles | `cook_vehicle_rest.py`, `cook_item_shadows.py`, `cook_driver_effects.py --jobs {jobs}`, `cook_vehicle_shadows.py --jobs {jobs}`, `cook_team_vehicles.py {disc} --jobs {jobs}`, `cook_racer_lod.py --output build/racer-lod --work build/racer-lod-cook`, `cook_vehicle_lod.py --disc {disc} --output build/racer-lod --work build/vehicle-lod-cook --jobs {jobs}` |
| textures | `cook_water_mipmaps.py`, `cook_world_mipmaps.py --jobs {jobs}`, `cook_battle_course_feedback.py` |
| drift | `cook_effects.py assets/common/Effect/RKRace.breft sd-card/3ds/mkw-3ds/textures`, and again with `--bike` |
| menu fit | compile `tools/menu_sprite_scales.cpp` with `src/menu_{ui,state,transition,focus,index,text_motion}.cpp`, run it to write `build/host-tools/sprite-scales.json`, then `optimize_menu_textures.py --jobs {jobs}` |

Each command is `python tools/<script>` unless it starts with `node`.
[MANUAL-RESOURCE-STAGES.md](MANUAL-RESOURCE-STAGES.md) lists each converter's
inputs and outputs. The Bowser's Castle visibility table is already in
`include/mkw/castle_regions.hpp`; a normal build needs no browser bake.

## 4. Generated headers

The conversions write the 66 headers listed in `config/generated-inputs.json`
into `include/mkw/`. `tools/build.py` copies them to
`build/standard/compile/generated/` and puts that directory on the include
path after the tree's own `include/`, so the tree's headers always win.

## 5. Shaders and libraries

The game's shader program is `src/mkw.v.pica`, `src/effect.v.pica` and a Mii
variant that `mii_shader.variant` makes from `src/mkw.v.pica`, assembled in
that order by picasso into `shader_data.hpp` (`mkw_shader`,
`mkw_shader_size`). CFL's `vendor/cfl-mii/source/vshader.v.pica` becomes
`vshader_shbin.h` (`vshader_shbin`, `vshader_shbin_size`).

`libcitro3d.a` is every `.c` under `vendor/citro3d/source`, this tree's
patched copy (pipelined frames, texture unit 2 on coordinate 1). `libCFL.a`
is `vendor/cfl-mii/source/cfl_mii.c`. Their flags are in
[MANUAL-LIBRARY-FLAGS.json](MANUAL-LIBRARY-FLAGS.json).

## 6. The game

Compile every file in [MANUAL-PROGRAM-SOURCES.txt](MANUAL-PROGRAM-SOURCES.txt)
with `arm-none-eabi-g++` and:

```text
-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -g
-mword-relocations -ffunction-sections -fdata-sections -ffp-contract=off
-std=gnu++23 -D__3DS__ -DREVOLUTION -DMKW_RELEASE -DKINOKO_QUIET
-fno-exceptions -fno-rtti -fcheck-new -fno-strict-aliasing
-ffile-prefix-map=<source root>/=
```

Include paths, in order: the shader header directory, `vendor/kinoko/include`,
`vendor/kinoko/source`, `include`, `vendor/cfl-mii/include`,
`vendor/citro3d/include`, `libctru/include`, the generated headers. Kinoko's
`.cc` files also get `-flto=auto`. Don't use fast-math or `MKW_FRAME_CAPTURE`.

Link with the same architecture flags, `-flto=auto -specs=3dsx.specs
-Wl,--gc-sections`, the objects, `libCFL.a`, `libcitro3d.a` and
`-L<libctru/lib> -lctru -lm`, wrapping `linearAlloc`, `linearMemAlign`,
`linearRealloc`, `linearGetSize`, `linearFree`, `linearSpaceFree`, `fopen`
and the `vram*` allocator functions (`vramAlloc`, `vramAllocAt`, `vramMemAlign`,
`vramMemAlignAt`, `vramRealloc`, `vramGetSize`, `vramFree`, `vramSpaceFree`)
with `-Wl,--wrap=<name>`. Then
`3dsxtool mkw-3ds.elf mkw-3ds.3dsx --smdh=icon.icn`.

## 7. Packs and the SD folder

```sh
python tools/pack_menu_assets.py
python tools/resource_manifest.py > runtime-files.json
python tools/pack_race_assets.py runtime-files.json race-base.pak --exclude-menu sd-card/3ds/mkw-3ds/menu.pak
python tools/pack_racer_lod.py --base race-base.pak --overlay build/racer-lod --output race-lod.pak
python tools/pack_mii_racers.py --base race-lod.pak --overlay build/mii-racers --output race-mii.pak
python tools/pack_unlit_banks.py race-mii.pak race.pak
gcc -O2 -o pakpack tools/pakpack.c
python tools/pak_order.py sd-card/3ds/mkw-3ds/menu.pak 0 tools/pak-order/menu.txt > menu-order.txt
python tools/pak_order.py race.pak 1 tools/pak-order/race.txt > race-order.txt
./pakpack sd-card/3ds/mkw-3ds/menu.pak menu-order.txt mpk2/menu.pak @menu-loose.txt
./pakpack race.pak race-order.txt mpk2/race.pak @race-loose.txt
./pakpack --stored --align 512 - empty.txt mpk2/stream.pak @stream-loose.txt
```

`resource_manifest.py` lists every runtime file with its source; it expects
`build/mkw-3ds.3dsx` to exist. `pack_unlit_banks.py` adds compact `.dsku`
opponent animation banks (about 40% smaller). Every other entry is copied
unchanged, and the player and the award ceremony keep the full `.dskm`.

`pakpack` rewrites both packs as MPK2: entries in the order the game reads
them (`tools/pak-order/` holds the read order from DIAG pack traces; the rest
follow by name, models beside their textures), LZ4-compressed where that saves
space (lossless; every block is checked against its source). race.pak drops
from about 1.8 to 1 GB and loads read less.

Every other file in the map goes into a pack too (the `*-loose.txt` lists hold
`name=source` lines; `tools/build.py` writes them): movies and sound
(`movies/`, `audio/`) into `stream.pak`, stored and each on a 512-byte SD
sector; the Mii resources (`mii/`) into menu.pak; the rest (course archives,
model lists, collision, the fire bar stream) into race.pak. The game's `fopen`
looks in the packs first (`src/asset_pack.cpp`, `openFile`), so every reader
finds them there. The cook receipts (`.json`) aren't shipped: the game reads
none of them.

The SD folder is `menu.pak`, `race.pak`, `stream.pak` and
`MarioKartWiiDS.3dsx` (`build/mkw-3ds.3dsx` under its shipped name).
The game still opens loose files that no pack holds, so an older SD folder
keeps working. A folder copied over an older one keeps the older loose files,
which slow every file lookup on the console: `slim_sd_models.py <sd folder>`
reports loose copies of packed files, and `--move-to DIR` moves them off.

## 8. HOME banner and CIA

[HOME-BANNER.md](HOME-BANNER.md) covers the banner model, icon and sound.

For the CIA, remove the `RomFs` block from a copy of
`tools/release/mkw-3ds.rsf` (an empty embedded RomFS would hide the SD data),
then:

```sh
makerom -f cia -target t -exefslogo -elf mkw-3ds.elf -rsf external-sd.rsf -icon icon.icn -banner home-banner.bnr -ver 1040 -DAPP_REMASTER=0 -o MarioKartWiiDS-v1.1.0.cia
```

The title version is `major << 10 | minor << 4 | micro`, so 1.1.0 is 1040.
Some makerom builds write 14 instead because of an internal macro clash;
`tools/build.py` corrects the Ticket and TMD version fields, then checks
every TMD and content hash (`check_cia`).

## Probes

C++ probe sources are in `tools/` and `tools/tests/`. Compile one before
running its Python helper. They are optional checks, not part of the build.
