#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-MKW3DS
# Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
"""Print CHANGELOG.md's sections for some versions, for a release's notes.

    changelog_section.py 0.8.1 0.8.0

prints the 0.8.1 and 0.8.0 sections, each under a "##" heading, so the
release page carries the whole list instead of pointing at CHANGELOG.md.
A full release lists every version since the last full release.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sections(text):
    found = {}
    version = None
    for line in text.splitlines():
        if line.startswith("# ") and line[2:3].isdigit():
            version = line[2:].split(":")[0].strip()
            found[version] = [line]
        elif version:
            found[version].append(line)
    return found


def main(argv):
    if not argv:
        sys.exit(__doc__)
    found = sections((ROOT / "CHANGELOG.md").read_text(encoding="utf-8"))
    out = []
    for version in argv:
        if version not in found:
            sys.exit(f"CHANGELOG.md has no section for {version}")
        lines = found[version]
        body = ["#" + l if l.startswith("#") else l for l in lines]
        out.append("\n".join(body).strip())
    print("\n\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1:])
