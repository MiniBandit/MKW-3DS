# Manual build operations

Version 0.9.4 / HW143. No operation below runs the next section. There is no
build script, Makefile, CI job, download/bootstrap helper or resume driver.
Examples are individual commands. Replace paths yourself and inspect output
before proceeding. Keep inputs and outputs outside the publication tree.

## 1. Obtain and install prerequisites yourself

Install devkitARM, libctru, picasso, 3dsxtool, tex3ds, Python, Node, ffmpeg and
any converters you need. Install makerom/bannertool separately for CIA work.
The reference identities in dependencies.json, vendor/sources.lock.json and
tools/release/toolchain.lock.json are data only. They perform no downloads.
Patched Kinoko/Citro3D/CFL sources are bundled; preserve those patches.
Acquire noclip, gclib and cfl-tool manually at the listed revisions, and apply
the relevant patch files yourself. Then install the packages listed in
package.json and requirements.txt using npm/pip yourself.

## 2. Extract and convert resources one stage at a time

Inspect tools/extract_disc.py for its file/target options and run it yourself
for the desired source. Run tools/unpack_szs.py for each selected archive.
There is no all-course importer or full-roster build. import_courses.py now
only contains KMP parsing helpers; cook_roster.py contains roster constants
and table helpers, and cook_garage.py contains per-choice conversion helpers.
The former batch main functions have been removed.

Prepare each required offline exporter separately. For example:

```sh
node tools/bundle_cooker.mjs tools/export_brres.ts
```

This bundles only the named helper; it does not import a course or compile
the game. The BRRES material/parser adaptations in this helper are required.
Choose export_driver.ts, export_joints.ts, export_star.ts and other exporters
in separate invocations when you need them. No-argument bundling fails.

Call the BRRES exporter with one archive/output/model, then run the model
converter explicitly:

```sh
node build/export_brres.mjs /external/source.brres /external/exported ModelName
python3 tools/cook_model.py /external/exported /external/cooked --prefix model_name
```

The latter is one format-conversion step and may invoke tex3ds for its model
textures. It does not advance to another game-resource stage. Consult its
CLI for options needed by the specific material. Existing archive/conversion
helpers still use the historical data/assets/build/sd-card conventions; adapt
paths deliberately rather than expecting a global orchestration layer.

Call course-object/overlay converters with explicit --ids. Prepare each
roster choice separately using the retained cook_garage.cook_choice helper,
then explicitly write its roster/garage metadata. No tool schedules the full
character/vehicle matrix. Mii outfits and team variants are separate work.

Use MANUAL-RESOURCE-STAGES.md as a dependency/output reference for independent
resource families. The old all-game entrypoint is gone. In particular:

- Compile tools/menu_sprite_scales.cpp and its listed menu source dependencies
  manually, then run that host utility explicitly to create
  build/host-tools/sprite-scales.json before optimize_menu_textures.py. The
  texture fitter no longer compiles or runs the measurement tool for you.
- Run menu footer, cup-record icons, styles and animations separately after
  the core menu UI conversion. cook_menu_ui.py no longer invokes them.
- For default Mii portraits, extract RFLRes01.arc, unpack it, run cook_mii_bank,
  compile the export_mii_head C++ utility yourself, and invoke the portrait
  converter yourself. Copy the authored course assignments and matching
  default portrait metadata to their required input paths manually.
- After prepare_award_assets, separately run export_award_scene, then
  cook_award_cameras, then cook_gp_rank.cook for the rank table.
- generate_native_icon.py requires an already acquired pinned cfl-tool checkout.
- Castle checkpoint boundaries and the approved triangle-mask table are
  already bundled in include/mkw/castle_regions.hpp. Reuse them; normal
  builds need no browser or visibility rebake. The existing runtime geometry
  hashes keep the full mesh if a converted model does not match the policy.
  Only developers intentionally changing the geometry/policy need the optional
  browser bake; generate_castle_regions.py requires --output outside the
  repository and cannot overwrite the approved header. That candidate must
  be reviewed separately.

No full from-disc validation has been repeated for this manual process.

## 3. Organize existing runtime files and create archives explicitly

The listing tool only reads existing resources and prints a path mapping:

```sh
python3 tools/resource_manifest.py > /external/runtime-files.json
```

It does not create directories, copy resources, remove stale files, ZIP a
package or invoke a conversion. Inspect the map and copy each selected file
to its runtime-relative SD path manually, in the intended overlay precedence.
Build the menu archive explicitly using pack_menu_assets.py once its inputs
exist. Build the race archive from the explicit mapping:

```sh
python3 tools/pack_race_assets.py /external/runtime-files.json /external/race.pak --exclude-menu /external/menu.pak
```

Apply pack_racer_lod.py and pack_mii_racers.py separately with explicit base,
overlay and output paths. Race packing never auto-merges them. Keep .list
files and other fopen-read metadata loose at their required runtime paths.
Do not substitute archive presence for a resource-completeness check.

## 4. Prepare external compile inputs and shaders

The castle policy is already included source configuration. Produce the
remaining 66 headers declared by config/generated-inputs.json through their
individual resource converters. Move/copy them outside the repository yourself
(or explicitly invoke collect_generated.py as a file-collection operation).
Select that include path yourself; there is no .local/build.json or environment
path-selection helper. Never compile against an accidental mixture of outputs.

Generate the Mii shader variant with the retained mii_shader.variant function;
shader_outputs.check validates a shader without compiling it. The effect shader
is already authored as src/effect.v.pica.
The production program combines src/mkw.v.pica, src/effect.v.pica and the Mii
variant in that order using picasso. Manually invoke picasso, then explicitly
turn the resulting bytes into shader_data.hpp's mkw_shader array. CFL likewise
needs its source/vshader.v.pica compiled and vshader_shbin.h prepared with
vshader_shbin and vshader_shbin_size. These small binary-to-C conversions are
manual steps; they are not triggered by game compilation.

## 5. Compile libraries, then compile each program source yourself

Use these architecture/code-generation flags consistently:

```text
-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -g
-mword-relocations -ffunction-sections -fdata-sections -ffp-contract=off
```

For the app add:

```text
-std=gnu++23 -D__3DS__ -DREVOLUTION -DMKW_RELEASE -DKINOKO_QUIET
-fno-exceptions -fno-rtti -fcheck-new -fno-strict-aliasing
```

Supply explicit -I paths for vendor/kinoko/include, vendor/kinoko/source,
include, your external generated header root, vendor/cfl-mii/include,
vendor/citro3d/include, libctru/include and your shader header output directory.
Use -ffile-prefix-map=<absolute-source-root>/= for release path removal.
Do not define MKW_FRAME_CAPTURE in the normal release.

Compile every Citro3D source individually with the appropriate __3DS__ and
include settings, then invoke arm-none-eabi-ar to create libcitro3d.a.
Compile cfl_mii.c with its generated shader header, then archive libCFL.a.
The exact reference library flags are in MANUAL-LIBRARY-FLAGS.json; replace
placeholder include paths manually. It is a reference, not an executable recipe.

MANUAL-PROGRAM-SOURCES.txt lists the 279 translation units. Manually
compile one source at a time with arm-none-eabi-g++; retain its relative object
path. Kinoko .cc files additionally use -flto=auto; application .cpp files do
not. Do not use fast-math. Example shape (fill the flags/include paths above):

```sh
arm-none-eabi-g++ <flags> <include-paths> -c src/animation.cpp -o /external/objects/animation.o
```

Construct the object list yourself and link with the same architecture flags,
-flto=auto, -specs=3dsx.specs, -Wl,--gc-sections, the explicit objects,
libCFL.a, libcitro3d.a, -L<libctru/lib> -lctru -lm. Add these wrapper options:

```text
-Wl,--wrap=linearAlloc -Wl,--wrap=linearMemAlign -Wl,--wrap=linearRealloc
-Wl,--wrap=linearGetSize -Wl,--wrap=linearFree -Wl,--wrap=linearSpaceFree
-Wl,--wrap=fopen
```

Run arm-none-eabi-size yourself, then separately convert the ELF:

```sh
3dsxtool /external/mkw-3ds.elf /external/mkw-3ds.3dsx
```

No tool in this tree compiles, links or automatically selects all of these
sources for you. The documented list and flags are not build drivers.

## 6. Create HOME assets and CIA manually

The icon, logo PNGs and banner sound are supplied in tools/release/artwork/.
Generate the main banner model from your own disc.
Banner-only tools are allowed; see HOME-BANNER.md for the explicit extraction,
scene and CGFX stages, audio packaging and verification status. Tool downloaders remain removed. Invoke
bannertool individually to create the SMDH, banner and required audio formats;
use the installed bannertool's own help for the corresponding operation.

Copy tools/release/mkw-3ds.rsf to an external working directory. Set its fields
and version deliberately. For an external-SD title remove the RomFs block
from that working copy. Preserve the hardware/access flags needed by this port.
Then call your installed makerom yourself with the ELF, edited RSF, icon,
banner, title version and output path. The template's APP_REMASTER (and
APP_ROMFS if retained) must be provided explicitly. The project supplies no
automatic version counter or CIA package builder; no prebuilt CGFX is supplied.

An external-SD HW143 example, after preparing all inputs yourself:

```sh
makerom -f cia -target t -exefslogo -elf /external/mkw-3ds.elf -rsf /external/external-sd.rsf -icon /external/icon.icn -banner /external/banner.bnr -ver 148 -DAPP_REMASTER=0 -o /external/MKW-3DS-v0.9.4.cia
```

The version is chosen manually: 0.9.4 packs as 148. There is no version
incrementer. This external-SD RSF must have its RomFs block removed; mounting
an empty embedded RomFS would hide the game's SD resource path.

Some makerom builds use an internal version macro that conflicts with the
requested title version. Inspect the final CIA's actual Ticket/TMD version
and content; for 0.9.4 the title version must be 148. A successful makerom exit does not
establish correct versioning or gameplay. Copy resources to SD manually and
preserve your own saves. No emulator or console is launched automatically.

## Explicit diagnostic probes

C++ probe sources remain under tools/ and tools/tests/. Python probe helpers
may emit their fixture source, but require a separately compiled executable;
they never invoke a host compiler. Compile each probe manually before asking
its helper to execute it. These are optional checks, not a game-build stage.
