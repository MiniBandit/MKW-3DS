# Notification LED (HW114 candidate)

This optional PTM presentation layer uses the existing CIA `ptm:sysm` service access. No game resources, effects, collisions, sound priorities or physics are changed for LED support. The service opens lazily; a failed service request disables the optional controller for that run. Continuous patterns are uploaded on state changes and played by the system module, not streamed each frame. Pause menus, race teardown, HOME, sleep and normal exit submit a finite fade for game-owned patterns. System notification arbitration and physical LED appearance remain subject to hardware testing.

Protocol reference: [PabloMK7's ctr-led-brary](https://github.com/PabloMK7/ctr-led-brary), SetInfoLEDPattern command 0x8010640, 100-byte/32-sample RGB pattern. The backend and policy here are new implementations; this is not the CTGP-7 gameplay plugin. Timing and colors are the approved local HTML candidates, not verified exact CTGP-7 constants.

## Mapping

| Event | Color / pattern | Trigger |
|---|---|---|
| Accepted item hit | Pure red #FF0000, 1.5s fade | Accepted native item action |
| Lightning / cloud / electric rod | Yellow, 1.5s fade | Accepted shrink or native electric action, including Mega cancellation |
| Green shell hit | Pure green #00FF00, 1.5s fade | Accepted item action retains shell type |
| Blue shell hit | Blue/white fade | Accepted Blue action |
| Any Blue launch | Two blue pulses | Actor launch, not inventory acquisition |
| Local Blooper / targeted local screen | Two white pulses | Actual local use / Blooper attack cue |
| Mushroom | Yellow → pink → red → purple | Actual 90-tick boost; interrupted boosts stop |
| Boost panel/ramp | Same four colors | Actual 60-tick boost; includes repeated panels |
| Rapid Golden use | Same colors, 4Hz continuous pulse | Two accepted uses within 30 ticks, continuing until boost ends |
| Star | Four-color smooth loop, 2s | Native Star state |
| Mega / Bullet | Yellow/red/neutral gray-white #808080 smooth loop, 1.5s | Native Mega / item Bullet state |
| POW | Orange-red pulse per stomp | Existing POW sound/presentation cue |
| Cloud charging | Yellow on model flash | Actual cloud CLR animation flash window |
| Item warning | Orange-red blink | Actual accepted warning sequence notes (0x118/0x119), after audio commit; 100ms bright pulse |
| Rainbow Road final lap | Seven-color smooth loop, 3.5s | Local lap >=3, not finished |
| Charged drift sparks | Cyan/white or orange/white loop, 1.1s | Native drift state 2/3 |
| Successful drift release | Cyan or orange flash, .5s | Native miniTurboSerial increments; canceled drift does not trigger |

Blooper targets are selected after launch by the original manager. An opponent's use therefore warns only when the local attack is actually scheduled and starts, not by guessing future rank at launch. POW follows its existing global audible cue; cloud follows its model flash. Warning pulses follow accepted sequence notes, including the existing distance-dependent tempo. The game thread samples an atomic deadline after the audio commit; physical audio/LED latency still needs hardware testing.

Priority: final-lap rainbow > item warning > electricity > other item effects > hits > drift. Latest override: every accepted impact owns its complete 1.5s hit flash/fade, ahead of all continuous effects including Rainbow, then remaining effects resume. Terrain damage is included even without an attacking racer. Electric/blue/green hits keep their specific colors; ordinary hits use red. Threat state and audio are unchanged. Both drift modes yield to every other effect; a release expires after 30 ticks even when hidden.

Colors: yellow FFD300 / pink FF00BD / red FF0000 sampled from boost `ef_rainbowRed2`; purple9800FF and cyan/orange drift colors were approved in preview as proposed colors. Hardware sample durations have 1/16-second precision; 90/60/120/210-tick cycles are exact, 66-tick drift and 21-tick POW are rounded to the nearest available duration. The final sample of finite patterns is black.

Host policy checks cover real timer termination, Golden continuity, canceled/successful drift, foreground priorities and resumption, one upload per steady continuous state, color order, no rainbow blackout, and finite black endings. ARM/package success is not evidence of PTM service behavior, apparent colors, or performance on a physical 3DS.

## License Settings

All labels are English, using the existing native game-font glyphs and LicenseManagement controls. Each license saves its options in a small checksummed `license-options-N.dat` companion with atomic temporary/backup writes; retail RKSD and ghost bytes are unchanged by these options. Old saves default to LED off, 100% brightness, and dynamic shadows on. The companion is tied to the slot's stored Mii ID and is reset on license erasure.

- `LED: Off`: no game-owned light.
- `LED: Items`: item hits, launches, POW, cloud, Mushroom/Golden, Star, Mega and Bullet.
- `LED: Items + Events`: also boost panels/ramps, item warnings and the Rainbow Road final lap.
- `LED: Items + Events + Drifts`: also charged sparks and successful mini-turbo release.
- `LED Brightness`: 50–100%, in 10% steps; scales all RGB samples without changing timing.
- `Dynamic Shadows: Off`: native static vehicle shadows replace dynamic shadows and tire contact patches; the dynamic shadow target is not allocated.

Up/down selects a row; left/right changes values; A/touch cycles values. Changes save through the existing license save worker and apply when racing. Brightness at its left/right limits clamps; A wraps. The original Change Miis and Erase License actions remain available.

All transitions to darkness fade from the sampled color of the last PTM upload (after brightness scaling). Ordinary cancellation/exit fades for 0.25s; CloudDark and WarningDark gaps fade for 0.125s so the existing blink cadence remains visible. Mushroom/panel patterns also taper over their final 20%. Each fade is one autonomous PTM upload, ends black and needs no worker or per-frame IPC. System notification overrides remain outside the game's control.

## Audio synchronization (HW114)

Accepted local damage voice, boost/panel/ramp sounds, Star/Mega/Bullet activation sounds, Blue launch flight, Blooper and POW mark their sound handles. The engine releases these markers only after a successful first sequence note or wave-channel start. The sound thread publishes them after its DSP commit; the main thread opens the corresponding presentation gate. Remote racers do not unlock local boost or damage. Warning remains per-note rather than per-sound. Rainbow, sustained drift and cloud model blinking retain their authored visual timing.

No sound priorities, sequence tempos or playback schedules are changed. Gates are bounded and allocation-free; unavailable audio bypasses them. A suppressed/missing matching sound releases pending presentation after 30 game ticks so accepted damage cannot be swallowed forever. This fallback is not audio synchronization. Markers coalesce identical events between game updates; physical output latency and crowded-event appearance require hardware testing. Audio start replaces the previous FOV80% boost gate; native boost end timers remain authoritative.

## HW115 targeted corrections and additions

Repeated Mushroom/Golden activations keep their acknowledged boost visible. Retail Dash reuse within 48 sequence ticks acknowledges the already-started sound; it does not restart audio or wait for another note. Initial activation still follows the acceleration sound, not the character voice.

Starting lights belong to Items. Existing race stage/timer matches the rendered Lakitu signal: CLR0 C2 C80050 for countdown and 0087FF for GO, with fade. Burnout replaces GO blue with a pure-red impact flash. Blooper flashes at activation for own use or a currently eligible incoming attack (rank/team/immunity/unavailable/finished); original delayed target selection remains unchanged, so later rank/status changes can alter who actually receives ink. Eye-arrival no longer generates a second LED event.

Character confirmation belongs to Events. The 24 standard-kart body palettes use two marked texture positions, normalized (62/228,47/115) and (219/228,7/115). Interpolate first to second, then fade, in 1.5 seconds. King Boo is pure white throughout; Toad is sampled red then white; Mario, Luigi and their baby variants put blue second. All other pairs retain marked order. These constants are in led_kart_colors.hpp; Mii has no entry and does not use another character's color.

New behavior is source/build tested, not physical LED acceptance.

HW116: character pairs now use two distinct pulses. The second stays dark until the first reaches 10%, including the shared PTM interpolation boundary; each pulse fades, and the pattern still ends black. Palette/order unchanged.

HW120: hand-held, Golden and dropped Mushroom pickup share the same committed Dash LED gate as boost panels. Mushroom use serial is for repeat detection only; dropped pickups now increment it too. Original activation calls/timers and audio unchanged. Host gate equivalence is not hardware onset proof.

HW121: Blue-shell launch uses RGB 0,0,255; impact blends pure blue and pure white. Accepted ordinary/Golden/ground mushroom activation now controls its LED independently of audio, exactly three simulation ticks later. Repeated activations hold the previous cycle until the delayed edge; panel and other power-up audio gating remains unchanged. Local player environment UVs bake from neutral driving pose and authored rear-camera parameters, once per load.
