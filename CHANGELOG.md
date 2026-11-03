# Changelog

Changes retained on top of the existing port, grouped by feature; withdrawn experiments, packaging-only revisions and unfinished investigations are excluded.

# Interface and menus

- Updated HOME banner conversion tools while keeping source inputs external to the repository.
- Bounded menu background residency, reclaimed retired models before replacement loads and retried failed backgrounds to prevent disappearing mode models.
- Restored the Grand Prix trophy's original diffuse lighting, weighted metal reflections and wood-base opacity.
- Restored the Time Trials stopwatch's metal and glass material combinations and reflection transforms.
- Restored the VS flagpole's environment mapping and the flag fabric's original lighting and tint.
- Restored both Battle balloon material combinations, including their reflections, intermediate clamps and mixed opacity.
- Restored the original title prompt, fades, looping glow and button confirmation feedback.
- Fixed missing title prompts, borders and confirmation controls caused by incomplete preloading.
- Restored menu movement, scaling, opacity and selection animations through the original layout hierarchy.
- Fixed transparent controls failing to reappear when deselected.
- Fixed duplicated Main Menu titles and incorrect bindings between text and glow layers.
- Rearranged menu content across both screens and corrected title, description, button and model placement.
- Fitted menu text proportionally instead of squeezing long labels horizontally.
- Corrected the size and proportions of Main Menu titles such as Single Player and Mario Kart Channel.
- Applied Main Menu confirmation squash and stretch to the glow while preserving the text and shadow proportions.
- Corrected the horizontal width of the Mario Kart Channel title on the bottom screen.
- Restored native menu fonts, text shadows, selection colors and button confirmation feedback.
- Fixed missing strokes, clipped text and incomplete digits caused by font atlas trimming.
- Standardized text clarity across race descriptions, Grand Prix results and Time Trials information.
- Corrected license card scaling, centering and title-screen preview proportions.
- Added the missing transitions between the title screen, license selection and Main Menu across both screens.
- Used the most recently selected existing license card on the title screen while retaining its light sweep.
- Added title background randomization under the established selection conditions.
- Corrected character and vehicle preview positions, name bars and performance lists.
- Used the corresponding original model animations and voices for character and vehicle confirmations.
- Corrected vehicle confirmation rotation counts, the final orientation and continuity when returning from drift selection.
- Fixed missing decorative-model entrance and exit motion at character selection boundaries.
- Kept identical decorative models continuous between adjacent pages.
- Restored selected cup emblems growing, rising and displaying their names.
- Centered cup emblems and labels using the icons' visible bounds.
- Corrected transition distances and return speeds between cup selection, course selection and confirmation.
- Fixed cup emblems briefly reappearing and top-screen text remaining during race confirmation.
- Added fades to the small preview movie, course name and full-screen movie on Grand Prix confirmation.
- Corrected character and vehicle movement around the confirmation window for random and sequential course selection.
- Restored the appropriate buttons and backgrounds for race confirmation, quitting and error messages.
- Fixed underlying menu text showing through message windows.
- Fixed bright edges and vertical seams in the scrolling striped background.
- Fixed mismatched upper and lower rings in the loading tire animation.
- Restored mode-specific pause controls, entrance animations and confirmation feedback.
- Fixed missing text, borders or backgrounds after repeatedly opening the pause menu.
- Prepared commonly used pause and quit-confirmation assets in advance.

# Licenses, rules and records

- Restored the omitted Mii Outfit B unlock-message entry so subsequent rewards display their correct text and models without changing saved unlock flags.
- Added persistent loading and saving for four license slots.
- Saved the most recently used license slot and restored it on startup.
- Saved each license identity with its corresponding Mii data.
- Retained backup loading and error messages for corrupted saves and failed writes.
- Combined shared license data reads to avoid reading the same save separately for all four slots at startup.
- Replaced save CRC32 calculations with a lookup table.
- Fixed early pages such as the title screen overwriting saved rules with defaults.
- Saved VS and Battle rules separately for each license and restored them on startup.
- Preserved valid rule selections when returning from the rules page, following the original behavior.
- Fixed failed rule saves updating the in-memory cache too early and preventing retries.
- Consolidated rule persistence when leaving the page.
- Fixed incomplete race-state resets when restarting certain modes.
- Corrected visibility and navigation for locked characters, vehicles and cups.
- Set the default engine class to 50cc when entering Grand Prix.
- Disabled the speedometer by default in every mode.
- Corrected transparency, animation and the stopwatch graphic behind Best Time on the Time Trials records page.
- Corrected Time Trials HUD placement, result descriptions and record digits.
- Saved dynamic shadow settings, LED mode and LED brightness per license.

# Miis and character models

- Preserved original color values in transparent face texels across race, menu and award expressions to prevent black facial patches.
- Replaced the question-mark result icon with the selected Mii portrait on individual and team result boards.
- Added native 3DS Mii selection and portrait rendering.
- Corrected license-card Mii portrait orientation, proportions, lighting and frame coverage.
- Fixed edited Miis incorrectly failing to save.
- Preserved native Mii hairstyle, facial-feature and accessory parameters.
- Opened the system Mii list after selecting Mii Outfit A or Mii Outfit B.
- Kept the selected racing Mii independent of the license's bound Mii.
- Added Mii racing bodies and seating poses for the corresponding size, gender and outfit.
- Added Mii vehicle resources for racing, vehicle selection, teams and award ceremonies.
- Used the appropriate native head, expressions, colors, portraits and voices for each Mii.
- Fixed Mii Outfit B unlock indices being confused with race character IDs.
- Fixed vehicle selection models disappearing after choosing a Mii.
- Restored Mii character and vehicle confirmation voices.
- Fixed the Mii race name incorrectly displaying Rosalina.
- Fixed vertical navigation targets not matching the visible Mii button positions.
- Waited for character selection to finish rendering after the Mii selector returned before playing the confirmation animation.
- Fixed Mii glasses and other transparent accessories rendering as opaque rectangles.
- Applied consistent transparency cutouts to Mii accessories in dynamic shadow and bloom captures.
- Restored Mii head lighting, highlights and Star color handling.
- Corrected the personal Mii's placement, neck connection and hair occlusion in the staff credits group photo.
- Fixed normal steering selecting incorrect facial expressions and blinking producing black faces.
- Fixed intermittent Toadette braid physics jitter and pose updates.
- Fixed black polygons around Funky Kong's trouser cuffs.
- Fixed Pokey's eyes appearing to have a white overlay.
- Fixed Rosalina's Luma appearing gray during the award ceremony.

# Race flow, cameras and CPU drivers

- Corrected the paired hallway Thwomps' return path and final turn angle on N64 Bowser's Castle.

- Stopped the lightning shrink timer from repeatedly triggering the damage animation after the initial reaction ends.
- Restored cup, course, mode and engine-class information during course intros.
- Extended intro text coverage to Battle arenas.
- Corrected clipping and placement of intro text, LAP, GO and result digits.
- Fixed the bottom-screen wide-angle intro camera clipping into terrain in certain scenes.
- Fixed camera clipping during the Dry Dry Ruins intro.
- Added smooth transitions for camera pitch changes during races.
- Smoothed entry into and exit from the Bullet Bill camera.
- Set the cannon-flight camera pitch to 1 degree.
- Distinguished normal airborne bike poses from cannon-flight poses.
- Fixed ordinary falls incorrectly using the horizontal cannon-flight pose.
- Added the rear-view camera and corrected its position, target and orientation.
- Increased the neutral dead zone for small analog stick movements.
- Adjusted trick and wheelie button mappings and added the corresponding ZR action.
- Required at least 80% downward stick input to throw an item backward.
- Fixed ordinary race collision feedback and certain obstacle hit callbacks being incorrectly cleared.
- Added driving, avoidance and stuck-recovery handling for the corresponding CPU difficulty levels.
- Corrected random CPU character and vehicle assignments using the original selection rules.
- Fixed some CPU drivers slowing down when the player braked.
- Corrected CPU recovery conditions near walls and flowing water.
- Corrected CPU trick eligibility across ordinary takeoff, jump pads and damage states.
- Fixed post-race driving on GCN DK Mountain following stale route state and repeatedly hitting walls.
- Restored GP and VS starting-grid order and inheritance of the previous race's finishing positions.
- Preserved the current starting positions when restarting a race.
- Fixed lap-sign Lakitu failing to appear in certain races.
- Added terrain clipping prevention for the wrong-way Lakitu.
- Fixed the wrong-way Lakitu retaining an abnormal floating trajectory after an uphill section.
- Added terrain clipping prevention to lap-sign Lakitu movement on complex terrain.

# Award ceremonies and staff credits

- Restored live diffuse lighting and rotating metal reflections for all nine award trophies while retaining their gold, silver and bronze material frames.
- Moved the award trophy and its emitter left with the result-page exit transition before hiding them off-screen.
- Added Grand Prix award scenes, camera rails, actors and trophy presentation.
- Added driving, expressions, crowds and scene animations to award ceremonies.
- Added missing rainbow, smoke, confetti and firework effects to award scenes.
- Fixed award scenes failing to load because of resource-directory ordering.
- Prepared award camera rails in advance to avoid allocating memory during camera cuts after the heap was locked.
- Corrected award content, text and team results across both screens.
- Reduced repeated processing of non-player models and lighting in award scenes.
- Fixed award scenes using the old incorrect rainbow material.
- Added the movies and subtitles for the normal and full endings' staff credits.
- Corrected staff subtitle control order, movie framing and group photo placement.
- Reduced both staff movies to approximately 30 fps while preserving the original audio and subtitle clock.
- Fixed crashes when entering a race after viewing the staff credits and returning to the title screen.

# Offline Battle modes

- Added offline Balloon Battle and Coin Runners across all ten arenas.
- Added the missing Battle mode, arena selection, team assignment and rules pages.
- Added CPU, vehicle, item, arena-order and win-target settings to Battle rules.
- Added balloon damage, stealing, loss, respawning and scoring.
- Restored balloon attachment points and physics constraints relative to the vehicle body pose.
- Fixed balloons shaking, jumping unpredictably or floating upward continuously.
- Blocked vehicle input while the racer was hidden after losing all balloons.
- Restored the blackout and respawn transition after losing all balloons.
- Added coin spawning, airdrops, drops, collection, stealing and recycling states.
- Added arbitration and deferred submission for competing attempts to collect the same coin.
- Fixed mismatches between coin collision positions, visual offsets and CPU target positions.
- Added coin ground binding, water states and arena-specific height offsets.
- Corrected stationary coin rendering and coin attachment to moving platforms.
- Added coin spawn scaling, rotation, collection and stealing effects.
- Restored the three-dimensional coin stack and column layout in Coin Runners.
- Added Battle CPU navigation, target selection, item use and starting behavior.
- Added live Battle rankings, individual scores, team totals and cumulative results.
- Fixed the same round's score being added again when re-entering the results flow.
- Restored controls, looping animations and team-score changes for both Battle result pages.
- Fixed overlapping Battle results, incorrect mode labels and engine-class text.
- Restored the appropriate Battle ending and team victory or defeat music.
- Changed the white fill of bottom-screen ranking digits to each team's color while preserving the black outline.
- Restored the original red and blue team-score colors on the top screen.
- Fixed overlapping character portraits on the Teams page.
- Fixed the Battle minimap to the full-arena view.
- Fixed forward-thrown Fake Item Boxes immediately hitting their owner in Battle.

# Items and course objects

- Destroyed Red Shells that remain locked onto a racer after that racer finishes.
- Added missing animation banks and state transitions for course objects.
- Fixed invalid poses caused by interpolation reading beyond the final animation frame.
- Added missing vehicle and item hit handling for objects including bats, Piranha Plants, cannonballs and rolling iron balls.
- Restored hit and recovery handling for wooden crates, factory blocks and other breakable objects.
- Added hiding, recovery and fragment resources for DK rocks destroyed by items.
- Restored target-facing behavior and hit feedback for the Piranha Plants on GCN Mario Circuit.
- Added reactions and recovery when Moonview Highway traffic was hit by powered-up racers or items.
- Restored Chain Chomp mouth morph animations.
- Added leaf-pile scattering, recovery and item drops.
- Restored the original hit feedback when powered-up racers contacted course obstacles.
- Added attachment relationships for moving platforms, bridges, disks and carried items.
- Corrected collision and CPU traversal when the bridge changed on DS Delfino Square.
- Corrected coins and items riding large rotating disks.
- Fixed SNES Ghost Valley walls losing collision on later laps without changing visually.
- Made SNES Ghost Valley ghosts face the camera using their billboard rules.
- Fixed stationary Thwomp Desert quicksand making vehicles and CPU drivers bounce continuously.
- Fixed Thwomp Desert quicksand incorrectly triggering jump tricks.
- Made vehicles sink into the terrain when falling into the center of Thwomp Desert.
- Fixed Grumble Volcano Fire Snakes disappearing before contacting a racer.
- Corrected Thunder Cloud model selection, scale and height above the player and opponents.
- Restored the Thunder Cloud face, lightning parts and flash timing.
- Corrected the overly dark lighting on the Thunder Cloud's lightning model.
- Expanded item warnings to cover more dangerous items and powered-up racers.
- Corrected item warning icon movement, direction and red flashes.
- Added the corresponding warning sounds beyond the existing shell alerts.
- Corrected Bullet Bill route progression and ground following on GBA Bowser Castle.
- Corrected Bullet Bill successor waypoint selection using the original branch order.
- Fixed Bullet Bill weaving and direction handling on sloped terrain.
- Made Bullet Bill ignore the appropriate obstacle collisions while retaining racer-hit feedback.
- Fixed items remaining held during cannon flight when they should have been cleared.
- Corrected dropped-item lifetimes, capacity-based recycling and landing states.

# Scene materials and rendering

- Restored the original white environment highlights in the Thunder Cloud's eyes.

- Restored the original lava color layers on N64 Bowser's Castle, including lava pillars and splash models.

- Restored spectators' original light-texture combinations and kept their lighting maps fixed.
- Restored the original internal lighting alpha for Peach Beach Nokis and a Bowser Castle crowd material to correct their dark appearance.
- Kept spectator crowds at their near-model appearance to prevent distance-dependent lighting changes.
- Restored the original GX lighting channels and environment reflections for racing characters and vehicles.
- Preserved source normals during GPU skinning and applied the corresponding normal transforms.
- Restored original course lighting, fog and supported layered material combinations.
- Fixed missing blue-purple layers and scrolling textures on half-pipe trick panels.
- Corrected boost-panel transparency, color combinations and independent texture coordinates.
- Corrected the sky-cloud blending equation on Peach Beach.
- Corrected Mario Circuit sky texture seams and Daisy Circuit cloud sampling.
- Restored the fixed sky rainbow and corrected its material binding.
- Fixed reflected Coconut Mall tree trunks failing to show their intended light color.
- Restored layered materials and environment reflections on supported statues.
- Fixed statues such as those on Daisy Circuit losing shading after lighting optimizations.
- Fixed Wario's Gold Mine flags to dark baked lighting.
- Used the same flag model at near and far distances to prevent color changes at LOD transitions.
- Fixed certain Maple Treeway models appearing white because of incorrect environment-reflection combinations.
- Added missing water materials on courses including Daisy Circuit, DS Delfino Square and Dry Dry Ruins.
- Restored original bloom parameters and adjusted composition strength and tint for the 3DS display.
- Adapted the relevant scene and menu presentation to the 400x240 aspect ratio.
- Corrected shared resources, depth and render state between the two stereoscopic views.
- Added and organized mipmap chains for static scene textures.
- Corrected distant-texture mip transitions and sampling artifacts on hardware.
- Applied targeted sampling adjustments to selected Mushroom Gorge and N64 DK's Jungle Parkway road surfaces and DS Delfino Square signs.
- Applied start-line sampling rules to the actual geometry without affecting grass and walls sharing the same texture atlas.
- Restored separate opaque and translucent drawing for course objects.
- Corrected color and alpha translation for grass, transparent objects and layered surfaces.
- Corrected live-screen capture, orientation, texture formats and stereo-eye state.
- Limited course-screen captures to scene content so countdowns and HUD elements were excluded.
- Restored missing Funky Stadium screens and Block Plaza dynamic displays.
- Added moving roads, bridges and course objects to the dynamic minimap.
- Corrected inverted minimap item icons, explosion-ring orientation and portrait outlines.
- Regenerated Bowser's Castle rotating-corridor minimap frames without the roof.

# Particles and visual effects

- Restored the original large and small statue fire effects and proximity triggers on N64 Bowser's Castle.

- Added missing vehicle particles for starts, burnouts, drifting, landings and road contact.
- Added missing activation and stopping conditions for leaf-area and chimney particles.
- Restored layered start smoke emission and rear-wheel contact conditions.
- Added CPU boost flames, drift sparks and wheelie wind effects.
- Corrected inward-drifting bike spark direction and placement along the inner side.
- Added missing bike brake smoke, scraping sparks and surface-specific dust.
- Added the relevant paths for shallow-water wakes, submerged bubbles, wheel sprays and spinning water splashes.
- Corrected the relationship between original water-contact states and particle emission positions.
- Added missing Fake Item Box fragments, power-up smoke and electrical effects.
- Restored the original texture combinations for Bob-omb and Blue Shell explosion cores.
- Restored fixed environment lighting on explosion cores to prevent them appearing too white after dynamic lighting was removed.
- Made blue smoke trigger on every valid Blue Shell explosion.
- Added missing lava-pillar splash effects on Bowser's Castle and Grumble Volcano.
- Added lava effects to grouped eruption vents in the latter part of Bowser's Castle.
- Added missing cannon smoke on GCN DK Mountain and Maple Treeway.
- Added the corresponding cannon-flight particles and speed presentation.
- Fixed phase jumps in Rainbow Road debris and star particles.
- Made boost panels and boost ramps clear ink from the screen.
- Added fades to Blooper's screen-darkening effect.

# Dynamic shadows and tire contact shadows

- Added dynamic silhouette shadows for the player's character, vehicle, tires and accessories.
- Used the course sun or the corresponding fixed light source to determine shadow direction.
- Fixed distant scenery bounds distorting the calculated light direction on courses such as Coconut Mall.
- Reversed Moonview Highway shadow direction and applied the established length cap.
- Added tire-layout contact patches for karts, three-wheelers and bikes.
- Adjusted contact patch length, width and placement for tire width and vehicle type.
- Fixed bike contact patches being too short.
- Matched contact patch color and coverage to the dynamic shadow.
- Fixed bright edges around tire contact patches and their boundaries with dynamic shadows.
- Added contact patch fades for takeoff, landing and state changes.
- Set the contact patch ground-clearance cutoff to 25.
- Disabled tire contact patches during pit, water and lava rescue states.
- Disabled tire contact patches when vehicle roll exceeded 40 degrees.
- Kept only the rear tire contact patch during bike wheelies.
- Disabled contact patches on specified vertically moving platforms and suspension bridges.
- Retained contact patches on working horizontal platforms such as those on Grumble Volcano and Toad's Factory.
- Limited contact patch projection stretching to prevent abnormal elongation in bowls.
- Capped dynamic shadow stretching at 1.5 times its reference length.
- Fixed a second, mirrored shadow appearing on the opposite side of DK Summit bowls.
- Fixed missing and fragmented shadows on flat roads, slopes, road decals and road-to-grass seams.
- Fixed missing dynamic shadows in hazard areas, on sand and on moving terrain.
- Fixed missing shadows and floating contact patches on Grumble Volcano moving platforms.
- Projected shadows onto the underlying terrain when driving through snow instead of onto the snow surface.
- Faded shadows smoothly when Rainbow Road sections changed road opacity.
- Added shadows for lap-sign, wrong-way and rescue Lakitu.
- Combined overlapping Lakitu and vehicle dynamic shadows without darkening the overlap twice.
- Fixed oversized Lakitu static shadows when dynamic shadows were disabled.
- Fixed bright borders where Lakitu and vehicle shadows merged.
- Fixed crashes when pressing START to open the pause menu after the shadow changes.
- Added a per-license dynamic shadow option enabled by default.

# Sound and music

- Made menu music replacement atomic and retired detached music instances to prevent overlapping BGM after settings changes.
- Kept audio automation on a continuous audio-thread clock during saves and loading while preserving game-controlled sound expiry.
- Raised audio refill priority and split scene reads into smaller pieces with active-stream refills between them.
- Restored the original voice ranges, fallback behavior and parameters for character and vehicle confirmations.
- Added missing sounds for damage, wall impacts, vehicle collisions and selected course obstacles.
- Added missing special collision sounds such as Coconut Mall traffic impacts.
- Restored course animal, moving-object and animation-linked sounds.
- Corrected cow and cowbell event routing and playback.
- Restored water ambience, splash sounds and selected short finish cues.
- Fixed ADPCM loop crackling in the second-lap cue and its rising high-note tail.
- Fixed duplicated opening-movie audio and asynchronous PCM queue ordering.
- Reduced the opening movie to approximately 30 fps while retaining its original audio track.
- Fixed opening audio repeating when multiple licenses existed.
- Adjusted the balance between sound effects and music to reduce excessive volume and distortion.
- Added the ordinary Bob-omb explosion echo and extended the audible range of the corresponding explosion sounds.
- Increased the playback priority of Lightning audio.
- Fixed item-box pickup sounds interrupting crowd cheers.
- Kept result music playing during loading after a race.
- Prevented ordinary sounds from the next race mixing into result-page loading.
- Managed sound instances by player category and event requirements.
- Added distance priority and stealing hysteresis for dense positional sound playback.
- Adjusted sound competition in response to truncation during simultaneous effects on Chain Chomp Wheel.
- Moved character voices and required sound resources to on-demand caching.
- Retained audible events outside the camera view, including item warnings from behind.

# Notification LED and settings

- Added item and key-event effects for the 3DS notification LED.
- Added Off, Items, Items + Events and Items + Events + Drifts modes.
- Added LED brightness adjustment from 50% to 100% and disabled LED effects by default.
- Fixed the brightness menu showing only 0 and 100.
- Made all game-owned LED effects fade out when ending or being canceled.
- Assigned drift effects the lowest LED priority.
- Prioritized Rainbow Road's final lap, item warnings, electric hits, other item effects, ordinary hits and drift effects in that order.
- Used pure red for ordinary damage while retaining item-specific damage colors.
- Used pure green for Green Shell hits.
- Used pure blue for Blue Shell launches and pure blue-to-white transitions for Blue Shell hits.
- Used yellow for Lightning, Thunder Cloud and electric-obstacle hits.
- Matched orange-red item-warning flashes to the corresponding warning cadence.
- Fixed item warnings overriding the following damage flash.
- Added an extra three-frame delay to damage LED effects.
- Added audio-start acknowledgments for boosts and item events with matching sounds.
- Unified Mushroom, Golden Mushroom and dropped Mushroom LEDs to trigger three frames after actual activation.
- Cycled Mushroom and boost-panel colors through yellow, pink, red and purple.
- Matched each single-boost LED cycle to the corresponding boost duration.
- Distinguished single Golden Mushroom activations from repeated rapid use.
- Added the corresponding sustained color cycles for Star, Mega Mushroom and Bullet Bill.
- Added gray to the existing Mega Mushroom and Bullet Bill palettes.
- Flashed orange-red with each POW cue.
- Matched Thunder Cloud LEDs to model flashes and followed activation with the yellow electric-hit effect.
- Triggered a white flash when a Blooper affecting the player was activated.
- Used a smooth rainbow gradient for Rainbow Road's final lap.
- Cycled drift colors while charged sparks were visible and flashed cyan or orange on release.
- Used the original texture's magenta countdown and blue start colors for the starting lights.
- Replaced the blue start effect with a red damage flash on burnout.
- Added two-color character confirmation effects sampled from specified locations on Standard Kart textures.
- Delayed the second character color until the first had fallen to 10% brightness.
- Applied the specified colors and ordering for King Boo, Toad, the Mario family, Yoshi and Toadette.
- Replaced nearly red character theme colors with pure red.

# Loading, memory and performance

- Reorganized VRAM, ordinary heap and LINEAR memory use for course textures and models.
- Reduced repeated character skinning, normal processing and rigid-part calculations.
- Added visibility culling for models, mesh sections and rigid parts.
- Reused identical poses and render state for consecutive matching materials.
- Shared shadow captures and related dynamic resources between both eyes at the same simulation tick.
- Reduced duplicate live-screen captures and intermediate buffers.
- Loaded effect and sound resources according to the current scene and actual dependencies.
- Avoided keeping unrelated effect libraries resident while retaining required recursive child-effect resources.
- Optimized spatial queries and submission for tire contact patches.
- Decoupled input and physics updates from contact patch drawing decisions.
- Disabled unnecessary runtime logs and diagnostic output in release builds.
- Added checkpoint-based indoor and outdoor visibility regions for Bowser's Castle.
- Added model fingerprints to castle region data and fell back to full drawing on a mismatch.
- Switched castle visibility regions at the entrance after the rotating corridor and near the final loop.
- Added the original low-detail character and vehicle models for opponents.
- Selected opponent LODs using distance, field of view and vehicle scale with switching hysteresis.
- Progressively omitted opponent front wheels and driver models at the farther LOD tiers.
- Kept full-detail models for the player, intros and results.
- Merged LOD models into race.pak and shared duplicate texture references.
- Fixed greatly increased course-loading, pause and resume times after LOD integration.
- Sampled player lighting textures every five simulation ticks and interpolated between samples.
- Used fixed lighting for static scenery and opponent materials that did not need dynamic updates.
- Restored full lighting for Item Boxes and Fake Item Boxes within the specified near range and blended the distance transition.
- Switched thrown items to their corresponding fixed-lighting path.
- Reduced temporary work in stable lighting-texture sampling and sound-distance sorting.
- Added adaptive animation update budgets with hysteresis for opponents and crowds.
- Exempted the player's own pose and head-turning animations from adaptive frame reduction.
- Retained per-tick updates for the player's effects while allowing lower update rates for opponents and scenery.
- Cached firebar texture phases within a bounded loading-time budget instead of synchronously reading each phase during racing.
- Adjusted worker-thread core assignments and priorities on New 3DS.
- Added atomic state rechecks after rendering-buffer wait timeouts.
- Cached identical vertex layouts, buffer configurations and texture-combiner register state to reduce duplicate GPU commands.
- Reused unchanged texture and floating-point uniform state to avoid submitting identical data again.
- Fixed missing shadow spatial-index restoration after resuming from pause making the rest of the race consistently heavier.
- Kept the static course loaded during pause while releasing reloadable drivers, vehicles and active-object resources.
- Moved the oversized renderer object off the main-thread stack to fix a startup crash.
- Prepared license portraits earlier to reduce loading stalls during the title transition across screens.
