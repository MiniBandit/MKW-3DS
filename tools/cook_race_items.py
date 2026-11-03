#!/usr/bin/env python3
"""Cook original world items and a small, explicit selection of race HUD art."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from cook_model import cook
from cook_roster import ROOT, data_partition
from extract_disc import Disc
from unpack_szs import unpack
from gclib.gx_enums import ImageFormat, PaletteFormat
from gclib.texture_utils import decode_image
from PIL import Image
from cook_race_hud import cook_hud
from devkitpro import tool

WORLD = {
    0: ("koura_green", "koura_green", None),
    1: ("koura_red", "koura_red", None),
    2: ("banana", "banana", None),
    3: ("itemBoxNiseRtpa", "itemBoxNise", None),
    4: ("kinoko", "kinoko", None),
    6: ("bomb", "bomb", "wait_a"),
    7: ("togezo_koura", "togezo_koura", "fly"),
    9: ("star", "star", None),
    13: ("pow_bloc_plane", "pow_bloc_plane", "attack1"),
    14: ("kumo", "kumo", "kumo"),
    15: ("kart_killer", "kart_killer", "wait"),
}
ICONS = {
    0: "tt_item_kame_green", 1: "tt_item_kame_red", 2: "tt_item_banana",
    3: "tt_item_dummybox", 4: "tt_item_kinoko", 5: "tt_item_kinoko_3",
    6: "tt_item_bomb_hei", 7: "tt_item_kame_wing", 8: "tt_item_thunder",
    9: "tt_item_star", 10: "tt_item_GoldenKinoko", 11: "fm_item_kinoko_l",
    12: "fm_item_gesso", 13: "fm_item_pow", 14: "fm_item_pikakumo",
    15: "tt_item_killer", 16: "tt_item_kame_green_3", 17: "tt_item_kame_red_3",
    18: "item_banana_3", 20: "tt_item_empty",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_ui(disc_path):
    inventory = json.loads((ROOT / "data/extracted/disc-files.json").read_text())
    disc = Disc(disc_path)
    partition = data_partition(disc)
    receipt = []
    for name in ("Scene/UI/Race.szs", "Scene/UI/Race_U.szs", "Scene/UI/Font.szs"):
        entry = next(row for row in inventory["files"] if row["name"] == name)
        source = ROOT / "data/extracted" / name
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(partition.read(entry["offset"], entry["size"]))
        unpack(source, ROOT / "assets/race-ui" / source.stem)
        receipt.append({"source": name, "sha256": digest(source), "sector_h0_verified": True})
    disc.f.close()
    return receipt


def decode_tpl(path):
    data = path.read_bytes()
    magic, count, table = struct.unpack_from(">3I", data)
    if magic != 0x0020AF30 or count != 1:
        raise ValueError("Expected one-image original TPL: " + str(path))
    image_header, palette_header = struct.unpack_from(">2I", data, table)
    height, width, fmt, pixels = struct.unpack_from(">HHII", data, image_header)
    palette, palette_format, palette_count = b"", 0, 0
    if palette_header:
        palette_count = struct.unpack_from(">H", data, palette_header)[0]
        palette_format, offset = struct.unpack_from(">2I", data, palette_header + 4)
        palette = data[offset:offset + palette_count * 2]
    return decode_image(io.BytesIO(data[pixels:]), io.BytesIO(palette), ImageFormat(fmt),
                        PaletteFormat(palette_format), palette_count, width, height).convert("RGBA")


def cook_sprite(source, name, output):
    original = decode_tpl(source)
    record = cook_image_sprite(original, name, output)
    record.update(source=source.relative_to(ROOT).as_posix(), source_sha256=digest(source))
    return record


def cook_image_sprite(original, name, output):
    width, height = original.size
    padded_size = tuple(max(8, 1 << (value - 1).bit_length()) for value in original.size)
    padded = Image.new("RGBA", padded_size)
    padded.paste(original, (0, 0))
    target = output / "textures" / (name + ".png")
    target.parent.mkdir(parents=True, exist_ok=True)
    padded.save(target)
    subprocess.run([str(tool('tex3ds')), "-f", "rgba8", "-z", "none",
                    "-o", str(target.with_suffix(".t3x")), str(target)], check=True, stdout=subprocess.DEVNULL)
    return {"name": name, "path": "textures/" + name + ".t3x", "width": width,
            "height": height, "padded": list(padded_size), "u": width / padded_size[0],
            "v": height / padded_size[1]}


def cook_world(source, model, name, clip, output):
    temporary = ROOT / "assets/race-items" / name
    args = ["node", str(ROOT / "build/export_brres.mjs"), str(source), str(temporary), model]
    if clip:
        args.extend([clip, "0"])
    # Race materials (unlit, as tools/cook_garage.py exports karts) and each
    # material's first sampler as its base texture. Without them the exporter's
    # equation matching picks e.g. a shell's specular map, and shells, bananas
    # and Bullet Bill draw black.
    bank_env={}
    if name=='item_14':
        (output/'models').mkdir(parents=True,exist_ok=True)
        bank_env['MKW_OBJECT_BANK']=str((output/'models/item_14.dobj').resolve())
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL,
                   env={**os.environ, **bank_env, "MKW_FREEZE_NONLINEAR_UV": "1", "MKW_RACE_MATERIALS": "1", "MKW_FIRST_SAMPLER": "1"})
    data = json.loads((temporary / "model.json").read_text())
    vertices = [v for m in data["models"] for s in m["shapes"] for v in s["vertices"]]
    parts = cook(temporary, output, name, preserve_vertices=bool(clip))
    if len(parts) != 1:
        raise ValueError("Item geometry unexpectedly requires partitioning: " + name)
    target = output / "models" / (name + ".gmdl")
    shutil.copyfile(output / "models" / (parts[0]["name"] + ".gmdl"), target)
    record = {"name": name, "path": "models/" + name + ".gmdl",
              "source": source.relative_to(ROOT).as_posix(), "source_sha256": digest(source),
              "sourceModel": model, "minimum": [min(v[i] for v in vertices) for i in range(3)],
              "maximum": [max(v[i] for v in vertices) for i in range(3)],
              "triangles": len(vertices) // 3, "limitations": data["limitations"]}
    if clip:
        bank = output / "models" / (name + ".dskm")
        result = subprocess.run(["node", str(ROOT / "build/export_driver.mjs"), str(source),
                                 str(bank), model], check=True, capture_output=True, text=True)
        record["animation"] = "models/" + name + ".dskm"
        record["clips"] = json.loads(result.stdout)["clips"]
        record["defaultClip"] = clip
    return record


def write_header(manifest):
    def sprites(name, count, records):
        lines = [f"inline constexpr std::array<RaceSprite,{count}> {name}{{{{"]
        for i in range(count):
            r = records.get(i)
            lines.append("    {nullptr,0,0,0,0}," if r is None else
                         '    {%s,%.9ff,%.9ff,%d,%d},' %
                         (json.dumps(r["path"]), r["u"], r["v"], r["width"], r["height"]))
        return lines + ["}};"]
    lines = ["#pragma once", "#include <array>", "namespace mkw {",
             "// Generated by cook_race_items.py; original TPL pixels with transparent POT padding.",
             "struct RaceSprite {const char* path;float u,v;unsigned w,h;};"]
    lines += sprites("HudItems", 21, {int(k): v for k, v in manifest["hudItems"].items()})
    lines += sprites("HudRanks", 13, {int(k): v for k, v in manifest["hudRanks"].items()})
    lines += sprites("HudDigits", 10, {int(k): v for k, v in manifest["hudDigits"].items()})
    extra_names = list(manifest["hudExtras"])
    lines += ["enum HudExtra {" + ",".join("Hud"+"".join(word.title() for word in name.split("_")) for name in extra_names) + ",HudExtraCount};"]
    lines += sprites("HudExtras", len(extra_names), {i:manifest["hudExtras"][name] for i,name in enumerate(extra_names)})
    paths = {int(k): row["path"] for k, row in manifest["items"].items()}
    lines += ["inline constexpr std::array<const char*,21> ItemModels{{",
              "    " + ",".join(json.dumps(paths[i]) if i in paths else "nullptr" for i in range(21)), "}};",
              'inline constexpr const char* ItemBoxModel="models/item_box.gmdl";',
              'inline constexpr const char* StartSignalModel="models/start_signal.gmdl";', "}", ""]
    (ROOT / "include/mkw/race_assets.hpp").write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--disc", type=Path, default=ROOT / "data/disc/Mario Kart Wii (USA) (En,Fr,Es).wbfs")
    parser.add_argument("--hud-only", action="store_true", help="Preserve cooked world models and refresh only original HUD assets")
    args = parser.parse_args()
    if args.hud_only:
        manifest = json.loads((args.output / "models/race-items.json").read_text())
        manifest["uiSources"] = extract_ui(args.disc)
    else:
        manifest = {"version": 1, "uiSources": extract_ui(args.disc), "items": {}, "hudItems": {}, "hudRanks": {}, "hudDigits": {}}
        common = ROOT / "assets/common"
        for ident, (source, model, clip) in WORLD.items():
            manifest["items"][str(ident)] = cook_world(common / (source + ".brres"), model, "item_" + str(ident), clip, args.output)
        manifest["itemBox"] = cook_world(ROOT / "assets/luigi/itembox.brres", "itembox", "item_box", None, args.output)
        manifest["bombCore"] = cook_world(common / "bombCore.brres", "bombCore", "bomb_core", "bombCore", args.output)
        subprocess.run(["node", str(ROOT / "build/export_item_box_uv.mjs"),
                        str(ROOT / "assets/luigi/itembox.brres"),
                        str(ROOT / "include/mkw/item_box_uv.hpp"),
                        str(common / "itemBoxNiseRtpa.brres")], check=True)
        manifest["itemBox"]["runtimeUVAnimation"] = {"source": "itembox and itemBoxNise SRT0 and materials", "frames": 300,
                                                     "header": "include/mkw/item_box_uv.hpp"}
        manifest["startSignal"] = cook_world(common / "jugemu_signal.brres", "jugemu_signal", "start_signal", None, args.output)
    ui = ROOT / "assets/race-ui/Race/game_image/timg"
    localized = ROOT / "assets/race-ui/Race_U/game_image/timg"
    for ident, source in ICONS.items():
        manifest["hudItems"][str(ident)] = cook_sprite(ui / (source + ".tpl"), "hud_item_" + str(ident), args.output)
    for rank in range(1, 13):
        manifest["hudRanks"][str(rank)] = cook_sprite(localized / f"tt_position_no_st_64x64_{rank:02}.tpl", f"hud_rank_{rank}", args.output)
    for digit in range(10):
        manifest["hudDigits"][str(digit)] = cook_sprite(ui / f"tt_d_number_3d_{digit:02}.tpl", f"hud_digit_{digit}", args.output)
    cook_hud(ROOT, args.output, manifest, cook_sprite, cook_image_sprite, digest)
    (args.output / "models/race-items.json").write_text(json.dumps(manifest, indent=2) + "\n")
    write_header(manifest)
    print(json.dumps({"worldModels": len(manifest["items"]) + 2,
                      "hudSprites": len(ICONS) + 12 + 10 + len(manifest["hudExtras"])}))


if __name__ == "__main__":
    main()
