# Update overlay

Files placed here ship inside the quick-update CIA's RomFS (as `romfs:/overlay/`)
and replace the file with the same relative path in the game's own files.

Example: `textures/ui/menusingle/my_new_button.t3x` here is read by the game
whenever it opens `textures/ui/menusingle/my_new_button.t3x`, even though the
SD folder has no such file. A file with the name of an existing one overrides it.

Keep it small: the update CIA is downloaded by every player. Nothing here is
read by builders; only `tools/release/package_cia.py --update` copies it.
Dotfiles and this README are not shipped. See docs/UI_AND_MII_FOUNDATION.md.
