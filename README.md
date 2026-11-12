# Mario Kart Wii 3DS

A native Nintendo 3DS port of Mario Kart Wii: **dudewhat**'s original port,
continued by **MiniBandit**, with **Epic**'s Redux merged in as the game.

This repository contains **no Nintendo game files**. It builds an installable
CIA on your own computer from your own Mario Kart Wii disc.

It isn't an emulator. The game runs natively on the 3DS: kart physics come from
[Kinoko](https://github.com/vabold/Kinoko), a recreation of Mario Kart Wii's
physics, and rendering, audio and menus are written for the 3DS hardware.

## Features

- **Grand Prix** in 50cc, 100cc, 150cc and Mirror, with cup progression, unlocks, award ceremonies and the staff credits
- **Time Trials** with saved records, **solo and team VS**, **Balloon Battle and Coin Runners** on all ten arenas
- **All characters and vehicles**, including the unlockables and your own **3DS Miis as drivers**
- **Four licenses** with saved identities, rules, records and settings
- **Two-screen menus**, race results and maps, with the Wii's materials, animations, particles and audio restored
- **Dynamic shadows** and **notification LED** effects, set per license
- Distance-based opponent detail, for demanding courses

See [CHANGELOG.md](CHANGELOG.md) for the full list. Online play is not included yet.

## What you need

- **Mario Kart Wii (USA)**, game ID `RMCE01`, dumped as `.iso` or `.wbfs`.
  Convert RVZ or NKit dumps to ISO in Dolphin first.
- About **30 GB** of free space and an internet connection
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS tools (`3ds-dev`)
- [Python 3.11–3.14](https://www.python.org/downloads/) and [Node.js LTS](https://nodejs.org) (Python 3.15 is not supported yet)
- [Git](https://git-scm.com)
- A C++ compiler for your computer, for two small helper programs:
  - macOS: Xcode command line tools (`xcode-select --install`)
  - Windows: `winget install MartinStorsjo.LLVM-MinGW.UCRT`, then open a new window
  - Linux: `g++` or `clang++` from your package manager

## Building

**macOS:** double-click `Build MKW CIA.command`, drag your disc image into the
window and press Return.

**Windows:** double-click `Build MKW CIA.bat`, drag your disc image into the
window and press Enter.

**Linux** (Intel or AMD computers only, including Chromebooks with Linux turned on):
install Python, Node.js, Git and `g++` with your package manager, then devkitPro.
On Debian, Ubuntu and ChromeOS, devkitPro's package manager is installed with
its own script first:

```bash
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S 3ds-dev
```

Then open a new terminal, go into the builder folder (`cd` to where you
unzipped it) and run `bash "Build MKW CIA.command"`. Type or paste the path to
your disc image when asked.

**This version needs a new build.** Its converted game files changed, so build
it **in a new, empty folder** (not your old builder folder).

The first build takes a few hours. Your CIA is saved to
`build/release/mkw-3ds.cia` and the game files to `build/release/sd-card/3ds/mkw-3ds`.
If a build stops partway, run it again: finished steps are skipped. If it fails,
share `build-cia-log.txt` when asking for help.

**Mii faces:** license portraits and Mii drivers use the 3DS's Mii resource. The
game asks your console for its own copy. If yours doesn't show them, put your
console's `CFL_Res.dat` beside the builder before building.

## Installing

You need a 3DS with custom firmware ([3ds.hacks.guide](https://3ds.hacks.guide)).

1. Copy the game files (about 3 GB; the game reads its files from here): open `build/release/sd-card/3ds` and copy the **`mkw-3ds`** folder inside it into the **`3ds`** folder on your SD card, so it ends up at `3ds/mkw-3ds`. If your card has no `3ds` folder, make one.
   - **Don't copy the `3ds` folder itself onto your card**: on a Mac, Finder can *replace* your card's whole `3ds` folder and delete your other apps.
   - **Updating an older version?** Your saves are in the card's `mkw-3ds` folder, so don't replace that folder. Open the new `mkw-3ds`, select everything in it and copy it into the card's `mkw-3ds`, replacing the files. (Windows does this when you copy the folder over and choose *Replace the files*; on a Mac, Finder's folder *Replace* would delete the saves.)
2. Install `mkw-3ds.cia` with FBI. It's small, because the game files are on the card.
3. For sound, run DSP1 once to dump your sound firmware.

Performance varies by console and course: New 3DS aims for 60 FPS and runs in
the 40–60 range.

To keep the game files inside the CIA instead, build with `--embed-assets`
(several GB, and some consoles fail to install CIAs that large).

**Please don't share your built files**: they contain Nintendo's game files.
Share this builder instead.

## Quick updates (no rebuilding)

Once you've built this version and copied the SD folder, updates are a small
**update CIA** (about 3 MB, no game files) installed in one of these ways:

| | |
|---|---|
| **FBI**: Remote Install → Scan QR Code | <img src="docs/fbi-update-qr.png" width="160"> |
| **Universal-Updater**: Settings → Add store → scan this, then install *Mario Kart Wii 3DS (update)* | <img src="docs/universal-updater-qr.png" width="160"> |
| **Manually**: download `MKW-3DS-Update.cia` from the [latest release](../../releases/latest) and install it with FBI | |

An update is not the game: without a build from your own disc it can't run. If
an update ever needs newer game files, the game says so on start-up: download
the latest builder, build again and copy the SD folder again.

## Controls

| Button | Action |
| --- | --- |
| Circle Pad | Steer and choose the direction of an item throw |
| A | Accelerate |
| B | Brake / reverse |
| R | Hop and drift; trick off a ramp |
| Y / D-pad Up | Trick / bike wheelie |
| X / ZL | Look behind |
| L | Use item |
| D-pad / ZR | Boost panel trick |
| START | Pause |
| SELECT | Toggle the speedometer |
| Touch screen | Menus: tap a button. Races: switch the lower-screen map view |

Dynamic shadows and LED options are in License Settings.

## Contributing

Bug reports and suggestions go in [Issues](../../issues). Please say which
console and version you used, the mode and course, and the steps to reproduce.
Keep game assets, saves and system files out of pull requests.

## Credits

- **dudewhat**: the original Mario Kart Wii 3DS port
- **MiniBandit**: the continuation and the builder
- **Epic (Epic0522)**: Redux, the game and interface this project now uses
- **DarthMDev**: Old 3DS drawing performance fix, race sound fixes and menu cadence
- **Codiak540**: Old 3DS performance testing and optimisation research
- **janedoemk3ds**: Old 3DS performance research with Codiak540
- **vabold / Kinoko**: Mario Kart Wii physics
- **noclip.website**: BRRES model parsing
- **LagoLunatic / gclib**: disc and texture utilities
- **devkitPro, libctru and Citro3D**: Nintendo 3DS development and graphics libraries
- **CFL contributors**: native Mii rendering

See [NOTICE](NOTICE) for licence details.
