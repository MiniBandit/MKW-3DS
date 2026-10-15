#!/usr/bin/env python3
"""Incremental native ARM11 build. No game data downloaded or embedded by default."""

import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from build_hardware_sdk import build_citro3d
from devkitpro import arm, root as devkitpro_root, tool

ROOT = Path(__file__).resolve().parents[1]


def source_signature(signature, source):
    """Hash shared compile inputs and source contents, even when mtimes are preserved."""
    digest = hashlib.sha256(signature.encode() + b"\0")
    digest.update(source.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=6)
    # Release builds omit test hooks, logs, engine assert text, and build paths.
    parser.add_argument("--release", action="store_true")
    parser.add_argument(
        "--external-assets",
        action="store_true",
        help="3DSX test program using an existing compatible SD asset set",
    )
    args = parser.parse_args()

    dkp = devkitpro_root()
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    citro = build_citro3d(output, dkp)
    kinoko = ROOT / "vendor/kinoko"
    cxx = arm("arm-none-eabi-g++")

    # DVLE 0: scene shader; DVLE 1: NW4R EF particles (src/effect.v.pica).
    subprocess.run(
        [
            str(tool("picasso")),
            "-o",
            str(output / "mkw.shbin"),
            str(ROOT / "src/mkw.v.pica"),
            str(ROOT / "src/effect.v.pica"),
        ],
        check=True,
    )

    # The content stamp the update CIA checks against the SD card's content.txt.
    sys.path.insert(0, str(ROOT / "tools/release"))
    from build_cia import content_fingerprint, legacy_windows_fingerprint

    version_stamp = (
        "#pragma once\n"
        "// Converted game files this code expects (tools/release/build_cia.py).\n"
        f'#define MKW_CONTENT "{content_fingerprint()}"\n'
        "// Written by Windows builders of 0.5.0 and 0.5.1 (npm.cmd in the recipe): also accepted.\n"
        f'#define MKW_CONTENT_WINDOWS_LEGACY "{legacy_windows_fingerprint()}"\n'
    )
    # Shown on the lower screen at start-up, so a console shows which update it runs.
    version_stamp += (
        f'#define MKW_VERSION "{(ROOT / "tools/release/VERSION").read_text().strip()}"\n'
    )

    # Shown once on the lower screen after an update (tools/release/WHATS_NEW.txt):
    # at most 16 lines of 38 characters, the lower screen's 8x8 text grid.
    news = [
        line.rstrip()
        for line in (ROOT / "tools/release/WHATS_NEW.txt")
        .read_text(encoding="utf-8")
        .splitlines()
        if line.strip()
    ]
    if len(news) > 16 or any(
        len(line) > 38 or not line.isascii() or '"' in line or "\\" in line
        for line in news
    ):
        raise SystemExit(
            "WHATS_NEW.txt: at most 16 ASCII lines of 38 characters, "
            "no quotes or backslashes"
        )
    version_stamp += '#define MKW_WHATS_NEW "' + "\\n".join(news) + '"\n'
    # The GitHub update build (MKW_QUICK_UPDATE=1, .github/workflows/update-cia.yml).
    quick_update = "1" if os.environ.get("MKW_QUICK_UPDATE") == "1" else "0"
    version_stamp += f"#define MKW_QUICK_UPDATE {quick_update}\n"

    version_header = output / "content_version.hpp"
    if not version_header.is_file() or version_header.read_text() != version_stamp:
        version_header.write_text(version_stamp)
    shader_header = output / "shader_data.hpp"
    shader_header.write_text(
        "#pragma once\n#include <cstdint>\n"
        "alignas(4) static const uint8_t mkw_shader[]={"
        + ",".join(map(str, (output / "mkw.shbin").read_bytes()))
        + "};\n"
    )

    arch = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
    flags = arch + [
        "-std=gnu++23",
        "-O2",
        "-g",
        "-D__3DS__",
        "-DREVOLUTION",
        "-mword-relocations",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-exceptions",
        "-fno-rtti",
        "-fcheck-new",
        "-fno-strict-aliasing",
        "-ffp-contract=off",
        "-Wall",
        "-Wno-psabi",
        "-I" + str(kinoko / "include"),
        "-I" + str(kinoko / "source"),
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "vendor/citro3d/include"),
        "-I" + str(dkp / "libctru/include"),
        "-I" + str(output),
    ]
    if args.external_assets:
        flags.append("-DMKW_EXTERNAL_ASSETS")
    if args.release:
        flags += [
            "-DMKW_RELEASE",
            "-DKINOKO_QUIET",
            "-ffile-prefix-map=" + str(ROOT) + "/=",
        ]

    lto = ["-flto=auto"]
    codegen = [
        "-O2",
        "-g",
        "-mword-relocations",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-strict-aliasing",
        "-ffp-contract=off",
    ]
    sources = [
        source
        for source in sorted((kinoko / "source").rglob("*.cc"))
        if not source.name.startswith("._")
        and source.name not in ("main.cc", "KReplaySystem.cc", "Context.cc")
    ] + [
        source
        for source in sorted((ROOT / "src").glob("*.cpp"))
        if not source.name.startswith("._")
    ]

    # All headers invalidate objects; conservative but reproducible.
    headers = (
        [shader_header, version_header]
        + list(kinoko.rglob("*.hh"))
        + list((ROOT / "include").rglob("*.hpp"))
        + list((ROOT / "vendor/citro3d/include").rglob("*.h"))
        + list((dkp / "libctru/include").rglob("*.h"))
    )
    signature = hashlib.sha256(
        (
            " ".join(flags + lto)
            + subprocess.check_output([str(cxx), "--version"], text=True)
        ).encode()
        + b"".join(
            header.read_bytes()
            for header in sorted(headers)
            if not header.name.startswith("._")
        )
    ).hexdigest()
    signature_stamp = output / "compile-signature"

    def compile_source(source):
        obj = output / "obj" / source.relative_to(ROOT).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        object_stamp = obj.with_suffix(".sig")
        current = source_signature(signature, source)
        if (
            not obj.exists()
            or not object_stamp.exists()
            or object_stamp.read_text() != current
        ):
            result = subprocess.run(
                [
                    str(cxx),
                    *flags,
                    *(lto if source.is_relative_to(kinoko) else []),
                    "-c",
                    str(source),
                    "-o",
                    str(obj),
                ],
                capture_output=True,
                text=True,
                errors="replace",
            )
            if result.returncode:
                raise RuntimeError(str(source) + "\n" + result.stdout + result.stderr)
            object_stamp.write_text(current)
            print(source.relative_to(ROOT), flush=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile_source, sources))
    signature_stamp.write_text(signature)

    elf = output / "mkw-3ds.elf"
    # The background menu asset loader allocates LINEAR memory; src/linear_lock.cpp
    # serializes every LINEAR heap call, including those made inside Citro3D/libctru.
    wraps = [
        "-Wl,--wrap=" + name
        for name in (
            "linearAlloc",
            "linearMemAlign",
            "linearRealloc",
            "linearGetSize",
            "linearFree",
            "linearSpaceFree",
            "fopen",
        )
    ]
    # The response file avoids Windows command-line limits for ~240 object paths.
    objects_file = output / "objects.rsp"
    objects_file.write_text(
        "".join(f'"{Path(obj).as_posix()}"\n' for obj in objects)
    )
    subprocess.run(
        [
            str(cxx),
            *arch,
            *lto,
            *codegen,
            "-specs=3dsx.specs",
            "-Wl,--gc-sections",
            *wraps,
            "-Wl,-Map," + str(output / "mkw-3ds.map"),
            "@" + str(objects_file),
            str(citro),
            "-L" + str(dkp / "libctru/lib"),
            "-lctru",
            "-lm",
            "-o",
            str(elf),
        ],
        check=True,
    )
    subprocess.run(
        [str(tool("3dsxtool")), str(elf), str(output / "mkw-3ds.3dsx")],
        check=True,
    )
    subprocess.run([str(arm("arm-none-eabi-size")), str(elf)], check=True)

    receipt = {
        "hardware_sdk": json.loads(
            (output / "hardware-sdk/receipt.json").read_text()
        ),
        "shader_sha256": hashlib.sha256((output / "mkw.shbin").read_bytes()).hexdigest(),
        "libctru_sha256": hashlib.sha256(
            (dkp / "libctru/lib/libctru.a").read_bytes()
        ).hexdigest(),
        "kinoko_commit": subprocess.check_output(
            ["git", "-C", str(kinoko), "rev-parse", "HEAD"], text=True
        ).strip(),
        "sha256": hashlib.sha256((output / "mkw-3ds.3dsx").read_bytes()).hexdigest(),
        "flags": flags,
        "sources": {
            source.relative_to(ROOT).as_posix(): hashlib.sha256(
                source.read_bytes()
            ).hexdigest()
            for source in sources
        },
    }
    (output / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
