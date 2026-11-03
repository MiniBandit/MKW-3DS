# Changelog video cards

Run this standalone tool with Python 3 and Pillow:

```sh
python3 tools/export_changelog_cards.py CHANGELOG.md --output ~/Downloads/MKW-Changelog-1080p
```

Each populated `#` category produces one 1920x1080 PNG containing its heading and bullet points, with no page label or version caption. Text is white on a transparent background, arranged in three columns and read down each column before moving right. All cards share one body font size, chosen to fit the densest section, and one heading size. The source Markdown is not changed.

Use `--background dark` for solid black instead of transparency, or `--heading-level 3` for a document whose categories use `###`. Use `--font` and `--bold-font` to provide font files on systems without Arial or DejaVu Sans. The tool fails instead of clipping content if a section cannot fit at the minimum font size.

`manifest.json` records each image's title, bullet count, font size and layout. Regenerating overwrites matching output filenames; use a new output directory after renaming or removing categories to avoid retaining old images.
