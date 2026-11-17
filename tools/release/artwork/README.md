# Supplied HOME artwork

- `icon.png`: the supplied Mario icon, copied without modification.
- `logo.png`: the supplied Mario Kart WiiDS logo (2000x718, converted losslessly
  from the supplied WebP). The HOME banner, the title screen (`tools/title_logo.py`)
  and the project README use it.
- `banneraudio.wav`: the earlier banner sound, kept for manual builds. `tools/build.py` uses the start of the Disc Channel banner sound from your disc instead.
- `manifest.json`: SHA-256 fingerprints and audio source/format details.

These two PNGs and the banner sound are explicit publication
exceptions alongside the eight inherited MK7 map markers. Inclusion does not
extend the code license to these media assets.
Do not add a CGFX model, banner binary, game texture dump, native Mii resource
or personal save to this directory. Generate the main banner model from the
builder's own USA Mario Kart Wii disc outside the repository.
