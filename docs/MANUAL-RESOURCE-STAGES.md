# Manual resource stage reference

Each row describes one former stage. Execute individual operations yourself;
there is no scheduler, resume mechanism or build-all command. Prerequisites and
output locations must be supplied manually. This is an operation reference,
not a newly validated from-disc recipe. `{py}`, `{disc}`, `{jobs}` are placeholders.
Rows referencing removed batch entrypoints must be split as described in
MANUAL-BUILD.md; those entrypoints are unavailable.

| Stage | Explicit operation / former inputs | Expected outputs |
| --- | --- | --- |
| Node packages | `npm ci --no-audit --no-fund` | `node_modules/.package-lock.json` |
| BRRES exporters | Invoke bundle_cooker.mjs separately for each named tools/*.ts exporter (MANUAL-BUILD.md) | `build/export_brres.mjs`, `build/export_award_scene.mjs`, `build/export_joints.mjs` |
| game module | `{py} tools/extract_disc.py {disc} data/extracted --file rel/StaticR.rel --to build/itembox-retail.rel` | `build/itembox-retail.rel` |
| menus, movies and sound | `{py} tools/extract_disc.py {disc} data/extracted --prefix Scene/ --prefix thp/ --prefix sound/ --prefix Boot/` | `data/extracted/Scene/UI/Title.szs`, `data/extracted/sound/revo_kart.brsar`, `data/extracted/thp/title`, `data/extracted/Boot/Strap/us/English.szs` |
| Coconut Mall archive | `{py} tools/unpack_szs.py data/extracted/Race/Course/shopping_course.szs assets/coconut-mall` | `assets/coconut-mall/vrcorn_model.brres` |
| Coconut Mall sky export | `node build/export_brres.mjs assets/coconut-mall/vrcorn_model.brres assets/cooked-coconut-sky` | `assets/cooked-coconut-sky/model.json` |
| Coconut Mall sky | `{py} tools/cook_model.py assets/cooked-coconut-sky sd-card/3ds/mkw-3ds --prefix coconut_sky` | `sd-card/3ds/mkw-3ds/models/coconut_sky.list` |
| finish-line source policy | `{py} tools/generate_startline_policy.py` | `tools/course_startline_faces.json` |
| disc default Mii portraits | Manual operation; batch entrypoint removed (see MANUAL-BUILD.md) | `sd-card/3ds/mkw-3ds/mii/parts.mpr`, `assets/mii/course-portraits/defaults.json` |
| runtime catalog metadata | `{py} tools/generate_runtime_tables.py catalog` | `include/mkw/course_catalog.hpp` |
| public native Mii icon geometry | `{py} tools/generate_native_icon.py` | `include/mkw/native_mii_icon_body.hpp` |
| race courses and arenas | Manual extraction/conversion and metadata assembly; batch CLI removed (MANUAL-BUILD.md) | `sd-card/3ds/mkw-3ds/models/course-catalog.json` |
| course objects | `{py} tools/cook_course_objects.py --jobs {jobs}` | `sd-card/3ds/mkw-3ds/models/course_00_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_01_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_02_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_03_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_04_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_05_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_06_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_07_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_08_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_09_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_10_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_11_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_12_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_13_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_14_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_15_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_16_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_17_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_18_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_19_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_20_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_21_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_22_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_23_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_24_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_25_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_26_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_27_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_28_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_29_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_30_visuals.list`, `sd-card/3ds/mkw-3ds/models/course_31_visuals.list` |
| Coconut Mall escalators and palms | `{py} tools/cook_coconut_objects.py` | `sd-card/3ds/mkw-3ds/models/coconut_escalator_000.gmdl`, `sd-card/3ds/mkw-3ds/models/coconut_palm_000.gmdl` |
| course lighting | Manual operation; batch entrypoint removed (see MANUAL-BUILD.md) |  |
| course material overlay | `{py} tools/cook_course_overlay.py --ids 1 5 7 8 10 15 19 27 29 31` | `build/course-materials/models` |
| drivers | Manual extraction/conversion and metadata assembly; batch CLI removed (MANUAL-BUILD.md) | `sd-card/3ds/mkw-3ds/models/roster.json` |
| karts and bikes | Manual extraction/conversion and metadata assembly; batch CLI removed (MANUAL-BUILD.md) | `sd-card/3ds/mkw-3ds/models/garage.json` |
| full roster | Manual operation; batch entrypoint removed (see MANUAL-BUILD.md) | `build/roster` |
| driver LOD | `{py} tools/cook_racer_lod.py --output sd-card/3ds/mkw-3ds --work build/racer-lod-cook` | `sd-card/3ds/mkw-3ds/models/mario_mdf_kart_driver_lod.gmdl` |
| vehicle LOD | `{py} tools/cook_vehicle_lod.py --disc {disc} --output sd-card/3ds/mkw-3ds --work build/vehicle-lod-cook` | `sd-card/3ds/mkw-3ds/models/mario_mdf_kart_body_lod.gmdl`, `include/mkw/vehicle_lod_layout.hpp` |
| driver effect roots | `{py} tools/cook_driver_effects.py --jobs {jobs}` | `build/driver-effects/receipt.json` |
| team vehicle colors | `{py} tools/cook_team_vehicles.py {disc} --jobs {jobs}` | `build/team-vehicles/team-vehicles.json` |
| vehicle shadows | `{py} tools/cook_vehicle_shadows.py --jobs {jobs}` | `build/vehicle-shadows/models/vehicle-shadows.json` |
| kart effects | `{py} tools/cook_effects.py assets/common/Effect/RKRace.breft sd-card/3ds/mkw-3ds/textures` | `sd-card/3ds/mkw-3ds/textures/drift-effects.bin` |
| bike effects | `{py} tools/cook_effects.py assets/common/Effect/RKRace.breft sd-card/3ds/mkw-3ds/textures --bike` | `sd-card/3ds/mkw-3ds/textures/drift-effects-bike.bin` |
| Luigi Circuit archive | `{py} tools/unpack_szs.py data/extracted/Race/Course/beginner_course.szs assets/luigi` | `assets/luigi/itembox.brres` |
| items and HUD | `{py} tools/cook_race_items.py sd-card/3ds/mkw-3ds --disc {disc}` | `sd-card/3ds/mkw-3ds/models/race-items.json` |
| item shadows | `{py} tools/cook_item_shadows.py` | `build/item-shadows/models/item-shadows.json` |
| race effects | `{py} tools/cook_race_effects.py --course-only --output build/vfx/textures` | `build/vfx/textures/course-effects.json`, `include/mkw/cannon_effect_assets.hpp` |
| blast effects | `{py} tools/cook_blast.py` | `build/vfx/models/bomb_blast.gmdl` |
| Lakitu | `{py} tools/cook_lakitu.py` | `build/vfx/models/lakitu_body.dskm`, `build/vfx/models/lakitu_shadow.gmdl` |
| name balloons | `{py} tools/cook_race_balloon.py` | `build/vfx/textures/hud_balloon_line.t3x` |
| ceremony source assets | `{py} tools/prepare_award_assets.py {disc}` | `build/award-source/award-sources.json` and unpacked ceremony archives; scene bindings, cameras and GP times are separate operations |
| ceremony scene bindings | Run the separately bundled `export_award_scene` exporter on the unpacked ceremony resources | `build/award-source/scene-bindings.json` |
| ceremony cameras | Run `tools/cook_award_cameras.py` after preparing scene bindings | `include/mkw/award_camera_data.hpp` |
| GP reference times | Run `tools/cook_gp_rank.py` after unpacking `ranktimeGP.krt` to `assets/common/` | `include/mkw/gp_rank_times.hpp` |
| ceremony models and animation | `{py} tools/cook_award_models.py --jobs {jobs}` | `build/award-models/award-models.json`, `include/mkw/award_model_assets.hpp`, `include/mkw/award_environment_data.hpp` |
| menu archives | `{py} tools/unpack_szs.py data/extracted/Scene/UI/MenuSingle.szs assets/menu-ui/MenuSingle<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/MenuSingle_U.szs assets/menu-ui/MenuSingle_U<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/MenuOther.szs assets/menu-ui/MenuOther<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/MenuOther_U.szs assets/menu-ui/MenuOther_U<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Title.szs assets/menu-ui/Title<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Title_U.szs assets/menu-ui/Title_U<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Award.szs assets/menu-ui/Award<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Award_U.szs assets/menu-ui/Award_U<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Globe.szs assets/menu-ui/Globe<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Channel.szs assets/menu-ui/Channel<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Channel_U.szs assets/menu-ui/Channel_U<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Present.szs assets/menu-ui/Present<br>{py} tools/unpack_szs.py data/extracted/Scene/UI/Present_U.szs assets/menu-ui/Present_U` | `assets/menu-ui/Title/title/timg/tt_title_screen_mario2.tpl`, `assets/menu-ui/Present_U/message/Common.bmg` |
| race flow results and awards | `{py} tools/cook_race_flow.py` | `build/race-flow/textures/flow_wipe.t3x`, `build/race-flow/textures/flow_award_font.t3x`, `build/race-flow/race-flow.json` |
| audio | `{py} tools/cook_audio.py` | `build/audio/audio/manifest.json` |
| Wiggler effect joints | `{py} tools/cook_hanachan_joints.py` | `include/mkw/hanachan_joint_assets.hpp` |
| Pokey separation joints | `{py} tools/cook_pokey_joints.py` | `include/mkw/pokey_joint_assets.hpp` |
| object animation sounds | `{py} tools/cook_object_sounds.py` | `include/mkw/object_sound_assets.hpp`, `build/object-sounds.json` |
| menu model archives | `{py} tools/unpack_szs.py data/extracted/Scene/Model/Driver.szs assets/menu-models/Driver<br>{py} tools/unpack_szs.py data/extracted/Scene/Model/BackModel.szs assets/menu-models/BackModel<br>{py} tools/unpack_szs.py data/extracted/Scene/Model/MiiBody.szs assets/menu-models/MiiBody` | `assets/menu-models/Driver`, `assets/menu-models/BackModel` |
| Battle menu labels | `{py} tools/cook_battle_menu_text.py` | `build/battle-menu-assets/receipt.json`, `include/mkw/battle_menu_text.inc` |
| menu interface | `{py} tools/cook_menu_ui.py` | `sd-card/3ds/mkw-3ds/models/menu-ui.json` |
| menu lighting | `{py} tools/cook_menu_lighting.py` | `include/mkw/menu_lighting.hpp`, `build/menu-lighting.json` |
| menu models | `{py} tools/cook_menu_models.py --machines --jobs {jobs}` | `sd-card/3ds/mkw-3ds/models/menu-models.json` |
| menu movies | `{py} tools/cook_menu_movies.py --jobs {jobs}` | `sd-card/3ds/mkw-3ds/models/menu-movies.json`, `sd-card/3ds/mkw-3ds/movies/title.wav` |
| menu texture fitting | `{py} tools/optimize_menu_textures.py` | `sd-card/3ds/mkw-3ds/models/menu-texture-fit.json` |
| menu audio | `{py} tools/cook_menu_audio.py` | `sd-card/3ds/mkw-3ds/models/menu-audio.json` |
| race bottom screen | `{py} tools/cook_race_map.py --jobs {jobs}` | `build/race-map/textures/race_bottom_ui.t3x` |
| Luma model | `{py} tools/cook_luma.py` | `build/vfx/luma-receipt.json` |
| firebar phase stream | `{py} tools/prepare_firebar_stream.py` | `build/course-water-feedback/textures/firebar-indirect.ftan` |
| course lighting tables | `{py} tools/cook_course_lighting.py` | `include/mkw/course_lighting.hpp` |
| course bloom tables | `{py} tools/cook_course_bloom.py` | `include/mkw/course_bloom.hpp` |
| star color table | `node build/export_star.mjs assets/common/driver.brres include/mkw/star_colors.hpp` | `include/mkw/star_colors.hpp` |
| driver look data | `{py} tools/cook_driver_look.py` | `include/mkw/driver_look_data.hpp` |
| menu Mii bodies | `{py} tools/cook_menu_mii_bodies.py` | `build/menu-mii-bodies/menu-mii-bodies.json` |
| race intro UI | `{py} tools/cook_race_intro.py` | `build/race-intro/receipt.json` |
| Battle balloons | `{py} tools/cook_battle_balloons.py` | `build/battle-balloon-assets/receipt.json` |
| Battle coins | `{py} tools/cook_battle_coins.py` | `build/battle-coin-assets/receipt.json` |
| Battle objects | `{py} tools/cook_battle_objects.py` | `build/battle-object-assets/receipt.json` |
| Battle screens | `{py} tools/cook_battle_screens.py` | `build/battle-screen-assets/receipt.json` |
| Battle HUD | `{py} tools/cook_battle_hud.py` | `build/battle-hud-assets/receipt.json` |
| Battle HUD motion | `{py} tools/cook_battle_hud_motion.py` | `build/battle-hud-motion/receipt.json` |
| Battle bottom scores | `{py} tools/cook_battle_bottom_scores.py` | `build/battle-bottom-scores/receipt.json` |
| Battle results | `{py} tools/cook_battle_results.py` | `build/battle-results-assets/receipt.json` |
| Coin HUD stacks | `{py} tools/cook_coin_hud_stack.py` | `build/battle-coin-hud-assets/receipt.json` |
| Balloon effects | `{py} tools/cook_race_effects.py --balloon-only --output build/battle-balloon-effects/textures` | `build/battle-balloon-effects/textures/balloon-effects.json` |
| Coin effects | `{py} tools/cook_race_effects.py --coin-only --output build/battle-coin-effects/textures` | `build/battle-coin-effects/textures/coin-effects.json` |
| water mipmaps | `{py} tools/cook_water_mipmaps.py` | `build/water-mipmaps/receipt.json` |
| world mipmaps | `{py} tools/cook_world_mipmaps.py --jobs {jobs}` | `build/world-mipmaps/receipt.json` |
| Blooper model | `{py} tools/cook_blooper.py` | `build/blooper/receipt.json` |
| Staff UI | `{py} tools/cook_staff_ui.py --retail-rel build/itembox-retail.rel --output build/staff` | `build/staff/models/staff-ui.json` |
| Staff movies | `{py} tools/cook_staff_movies.py --output build/staff` | `build/staff/movies/staff_true.source.json` |
| Staff crowd joints | `{py} tools/generate_staff_crowd.py` | `include/mkw/staff_crowd_data.hpp` |
| Staff camera | `{py} tools/cook_staff_camera.py` | `include/mkw/staff_camera_data.hpp` |
| dynamic map sprites | `{py} tools/cook_map_objects.py` | `include/mkw/map_object_assets.hpp` |
| runtime voice tables | `{py} tools/generate_runtime_tables.py audio` | `include/mkw/race_voice_tables.hpp` |
| runtime trig table | `{py} tools/generate_runtime_tables.py math` | `include/mkw/nw4r_sin_table.hpp` |
| runtime Mii billboards | `{py} tools/generate_runtime_tables.py billboards` | `include/mkw/course_mii_billboards.hpp` |
| road depth policy | `{py} tools/generate_road_policy.py` | `include/mkw/road_sampler_ranges.hpp` |
| course draw phases | `{py} tools/generate_course_phases.py` | `include/mkw/course_phase_ranges.hpp` |
| castle checkpoint regions | Reuse the bundled approved HW132 policy; no browser or generation step | `include/mkw/castle_regions.hpp` |
| Battle legacy material migration | `{py} tools/cook_battle_course_feedback.py` | `build/battle-course-feedback/receipt.json` |
| Mii race models | `{py} tools/cook_mii_racers.py {disc} build/mii-racers --jobs {jobs}` | `build/mii-racers/models/mii-garage.json` |
| Mii menu/team/metadata | `{py} tools/finish_mii_racers.py {disc} build/mii-racers --jobs {jobs}` | `build/mii-racers/models/mii-complete.json` |
| Mii award bodies | `{py} tools/cook_mii_awards.py build/mii-racers` | `build/mii-racers/models/mii-awards.json` |
| Mii voice tables | `{py} tools/cook_mii_voice.py build/itembox-retail.rel` | `include/mkw/mii_voice.hpp` |
