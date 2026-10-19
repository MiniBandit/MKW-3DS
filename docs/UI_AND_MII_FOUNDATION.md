# Two-screen menus, the update overlay and the Mii foundation

This is what the rebuild release carries so that later menu and Mii work can ship
as quick updates (a small CIA), without players rebuilding.

## Principle

A rebuild is needed only when the *converted game files* change. Screens, layouts,
touch handling and Mii logic are code, and code ships in every quick update. So the
rebuild must hold (1) every asset those features could draw, (2) the loading
mechanisms they need, and (3) the empty slots where later content plugs in.

## 1. The update overlay (`include/mkw/overlay.hpp`, `src/overlay.cpp`)

A quick update's RomFS may carry `overlay/`. At start-up the game lists
`romfs:/overlay/` once. From then on:

- `fopen` (wrapped in `src/linear_lock.cpp`) opens `romfs:/overlay/<path>` instead of
  `<path>` for reads of relative paths that exist there;
- the asset packs (`menu.pak`, `race.pak`) report such a path as absent, so the
  loose (overlay) file wins.

So an update can add or replace any small asset: a UI image, an icon, a layout
data file, a Mii portrait. Put the files in `tools/release/overlay/` (same relative
paths as in the SD folder); `package_cia.py --update` copies them into the CIA.
Without files there, nothing changes and nothing is paid at run time.

Limits: only reads of paths relative to the game folder (never `sdmc:/` saves or
logs); the update CIA is downloaded by everyone, so keep it small.

## 2. The lower screen of the menus (`include/mkw/menu_bottom*.hpp`, `src/menu_bottom*.cpp`)

- `composeMenuBottom(MenuState, MenuBottomLayout, …)` lays out the current page's
  choices as touch targets (pure code, tested on the host:
  `python3 tools/tests/run_necessary.py --only menu_bottom`). Pages: license, main
  menu, mode, engine class, VS and battle mode, character, vehicle, drift type, cup,
  course, battle cup and stage, ghost, ready, error, quit, pause, results.
- A tap selects the control and accepts it (as the d-pad and A do) via
  `MenuState::selectSlot`; the Back button is B. The d-pad cursor is mirrored.
- `MenuBottomScreen` draws it with flat fills and the built-in text. A paused race
  keeps the race's own lower screen.
- To change a page or add one, edit `menu_bottom.cpp` only: that is a code update.

## 3. The Mii foundation (`include/mkw/mii_store.hpp`, `src/mii_store.cpp`)

The game reads `miis/miis.txt` (SD folder or overlay) and lists those Miis (names and
portraits show on the character page's two Mii slots). Format, one Mii per line:

    id | name | source | portrait | data

`id` is up to 15 characters of `[A-Za-z0-9_-]`; `source` is `custom`, `console` or
`wii`; `portrait` is the relative path of a `.t3x` with power-of-two sides (for
example 32x32 or 64x64) or empty; `data` is the raw Mii data in hex (3DS 96 bytes,
Wii 74 bytes) or empty. `#` starts a comment. Tested on the host
(`--only mii_manifest`).

What a later update adds, with no rebuild: the Mii files themselves (SD or overlay),
the code that turns the data into a racer, and the screens to pick one. What exists
now: the file format, the loader, the portrait slots in the UI, and the Mii outfits
already converted by the builder (`award_*mii*` models and textures, the Mii bodies).

## Asset audit: what the builder already converts for future UI

| Content | Where | Status |
|---|---|---|
| Every English UI texture (menus, race, results, award, title, channel, present, event, globe) | `textures/ui/<archive>/` (about 2,000 images) | converted since 0.5.0 (`tools/cook_battle.py`) |
| UI fonts and message text (BRFNT, BMG) | `ui-data/` | converted raw; a later update parses them |
| Menu layouts | generated headers in `include/mkw/` (code) | code: changeable by update |
| Menu sounds and music, menu movies | sound archive, `movies/` | converted |
| Mii bodies and award Mii textures, default Mii textures | `models/`, `textures/` | converted |
| Mii *face* data | not on the disc (Wii system resource) | cannot be converted: user data or art, via overlay/SD |

Not converted on purpose: Spanish and French UI variants (the port is English).
