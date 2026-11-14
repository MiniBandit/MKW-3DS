# Mario Kart WiiDS

A native Nintendo 3DS port of Mario Kart Wii. This fork builds on
[Epic0522's Redux](https://github.com/Epic0522/MarioKartWii-3DS-Redux) and adds
Old 3DS support, ghosts, graphics settings and general optimization.

No game files are included. Everything is converted from your own disc.

## Features

- All 32 courses in Grand Prix, Time Trials and VS
- Grand Prix at 50cc, 100cc, 150cc and Mirror
- VS solo or in teams, Balloon Battle and Coin Runners
- All characters and vehicles, unlocks, award ceremonies and credits
- Mii drivers
- Four licenses with their own settings and records
- Staff ghosts, Expert Staff ghosts and your own saved ghosts, with replays
- Dual-screen menus, results and maps using the game's original art, effects and audio
- Touch screen support in the menus
- Course effects, lighting, cameras and items matched to the Wii
- Runs on New 3DS and Old 3DS, optimized for both
- 3D at 60 FPS
- 800px Mode on New 3DS for a sharper top screen
- Settings for texture quality, dynamic shadows, bloom, opponent detail and minimap frame rate
- Surround sound, following the 3DS sound setting
- Speedometer, race timer options and notification LED effects

## What you need

- Your own **Mario Kart Wii USA** disc (RMCE01) as an ISO or WBFS
- A Windows, macOS or Linux computer with an internet connection and about 30 GB free
- A 3DS with Luma3DS and FBI, and about 3 GB free on the SD card
- DSP firmware dumped with DSP1, if your console has never had homebrew sound

## Building

### Windows

1. Download this repository and unzip it, or clone it.
2. Double-click `Build.bat` and select your disc.

Anything missing, including Python, Git and devkitPro, is downloaded for you.
When it's done it deletes the converted data, leaving about 3 GB.

Or run the build script yourself. Install Python 3.12, Git, and devkitPro with
`3ds-dev` and `gcc`. Then, from a clone:

```sh
py -3.12 tools/build.py --disc "C:/Games/RMCE01.wbfs"
```

### macOS

Install [Homebrew](https://brew.sh), then the tools:

```sh
brew install git python@3.12 node ffmpeg-full gcc@16
curl -LO https://github.com/devkitPro/pacman/releases/latest/download/devkitpro-pacman-installer.pkg
sudo installer -pkg devkitpro-pacman-installer.pkg -target /
```

On Apple Silicon, also run `softwareupdate --install-rosetta`.

Open a new terminal, then build:

```sh
sudo dkp-pacman -S --needed --noconfirm 3ds-dev
git clone https://github.com/ethanseagull/MarioKartWiiDS.git
cd MarioKartWiiDS
export CXX=g++-16
export FFMPEG="$(brew --prefix ffmpeg-full)/bin/ffmpeg"
python3.12 tools/build.py --disc ~/Games/RMCE01.wbfs
```

### Linux

On Ubuntu 24.04 (x86-64), install the tools:

```sh
sudo apt install git python3.12 python3.12-venv build-essential nodejs npm ffmpeg wget
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S --needed --noconfirm 3ds-dev
```

Then build:

```sh
git clone https://github.com/ethanseagull/MarioKartWiiDS.git
cd MarioKartWiiDS
python3.12 tools/build.py --disc ~/Games/RMCE01.wbfs
```

Other distributions need the same tools: Python 3.12 exactly, GCC 13 or newer,
Node.js, an ffmpeg with soxr, and [devkitPro](https://devkitpro.org/wiki/Getting_Started).

### All systems

Only Windows has been tested so far. Add `--delete-converted` to the build
script to delete the converted data afterwards.

The first build takes between 20 minutes and an hour, depending on the PC. If it
stops, run it again and it picks up where it left off. The results are in `build/standard/output`.

## Installing

1. Copy the `sd/3ds` folder to the root of your SD card.
2. Install the `.cia` file with FBI, or run `MarioKartWiiDS.3dsx` from the Homebrew Launcher.

## Updating

Run `Build.bat` again, or from your clone:

```sh
git pull
py -3.12 tools/build.py --disc "C:/Games/RMCE01.wbfs"
```

On macOS or Linux, use the `python3.12` command from your build instead, with
the same `export` lines on macOS. Only what changed is rebuilt, which usually takes a few minutes. An update that
changes game data does a full build. Then repeat the install steps. Your saves
are kept.

Since 1.1.0 the game is its own title, "Mario Kart WiiDS", with its own folder,
`sd:/3ds/MKWiiDS/`, so it installs beside other versions instead of over them. To
keep your licenses, records, ghosts and settings from an earlier version, copy
`rksys.dat`, `settings.dat`, `last-license.txt`, the `license-options-*.dat` and
`mii-*.3dsmii` files, and the `ghosts` folder from `sd:/3ds/mkw-3ds/` into
`sd:/3ds/MKWiiDS/`. Delete `sd:/3ds/mkw-3ds/` and the old title in FBI if you no
longer want them.

See the [releases](https://github.com/ethanseagull/MarioKartWiiDS/releases) for what's new.

## Controls

| Button | Action |
| --- | --- |
| Circle Pad | Steer, tilt in the air, aim items |
| A | Accelerate |
| B | Brake and reverse |
| R | Hop and drift, trick off a ramp, dodge a POW |
| L | Use item |
| Y | Trick in the Circle Pad's direction, wheelie on bikes |
| D-pad | Trick in that direction, Up for a wheelie or to dodge a POW |
| ZR (New 3DS) | Same as D-pad Up |
| X, or ZL on New 3DS | Look behind |
| START | Pause |

## Credits

- dudewhat: the original port
- MiniBandit: the continuation this project builds on
- Epic: Redux
- DarthMDev: Old 3DS performance, race sound and menu fixes
- Codiak540 and janedoemk3ds: Old 3DS testing and research
- smol_bug: console photos for the pause screen
- vabold: Kinoko, the physics engine
- noclip.website: BRRES model parsing
- LagoLunatic: gclib
- devkitPro, libctru and Citro3D
- CFL contributors: Mii rendering
- ogws and the mkw decompilation projects
- Project CTR (makerom) and bannertool

## License

See [LICENSE](LICENSE), [NOTICE](NOTICE) and [licenses/](licenses/). The MIT
text doesn't cover every file; NOTICE explains which parts have no declared
license yet. Mario Kart Wii is copyright Nintendo. This is an unofficial fan
project and isn't affiliated with Nintendo.
