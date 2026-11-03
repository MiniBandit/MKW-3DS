#!/usr/bin/env python3
"""Cook all twelve vehicles for each retail driver from a local disc."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from cook_model import cook
from cook_roster import ROOT, ROSTER, data_partition, sha256
from extract_disc import Disc
from unpack_szs import unpack

# Original IDs and archive stems from Kinoko include/Common.hh. Six starter
# vehicles per weight class, in the initial selection-menu category order.
VEHICLES = {
    "s": [(0, "Standard Kart S", "sdf_kart"), (3, "Baby Booster", "sa_kart"),
          (6, "Mini Beast", "sb_kart"), (18, "Standard Bike S", "sdf_bike"),
          (21, "Bullet Bike", "sa_bike"), (24, "Bit Bike", "sb_bike"),
          (9, "Cheep Charger", "sc_kart"), (12, "Tiny Titan", "sd_kart"),
          (15, "Blue Falcon", "se_kart"), (27, "Quacker", "sc_bike"),
          (30, "Magikruiser", "sd_bike"), (33, "Jet Bubble", "se_bike")],
    "m": [(1, "Standard Kart M", "mdf_kart"), (4, "Classic Dragster", "ma_kart"),
          (7, "Wild Wing", "mb_kart"), (19, "Standard Bike M", "mdf_bike"),
          (22, "Mach Bike", "ma_bike"), (25, "Sugarscoot", "mb_bike"),
          (10, "Super Blooper", "mc_kart"), (13, "Daytripper", "md_kart"),
          (16, "Sprinter", "me_kart"), (28, "Zip Zip", "mc_bike"),
          (31, "Sneakster", "md_bike"), (34, "Dolphin Dasher", "me_bike")],
    "l": [(2, "Standard Kart L", "ldf_kart"), (5, "Offroader", "la_kart"),
          (8, "Flame Flyer", "lb_kart"), (20, "Standard Bike L", "ldf_bike"),
          (23, "Flame Runner", "la_bike"), (26, "Wario Bike", "lb_bike"),
          (11, "Piranha Prowler", "lc_kart"), (14, "Jetsetter", "ld_kart"),
          (17, "Honeycoupe", "le_kart"), (29, "Shooting Star", "lc_bike"),
          (32, "Spear", "ld_bike"), (35, "Phantom", "le_bike")],
}
# Per driver, in the menu's choice order (UiVehicleNames): the six starting
# vehicles, then the six unlockable ones.
VEHICLES_PER_DRIVER = 12
EXTRA_NAMES = ["handle", "f_sus", "swingarm", "r_sus", "arm_fl", "arm_fr", "arm_rl", "arm_rr", "sus_fr"]


def inspect_driver(archive):
    result = subprocess.run(["node", str(ROOT / "build/export_driver.mjs"),
                             str(archive / "driver_model.brres"), "--inventory"],
                            check=True, capture_output=True, text=True)
    inventory = json.loads(result.stdout)
    (archive / "driver-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    return inventory


def inspect_kart(archive):
    result = subprocess.run(["node", str(ROOT / "build/export_brres.mjs"),
                             str(archive / "kart_model.brres"), str(archive / "inspection"), "body"],
                            check=True, capture_output=True, text=True)
    inventory = json.loads(result.stdout)
    (archive / "kart-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    return inventory


def write_native_garage(choices):
    if len(choices) != len(ROSTER) * VEHICLES_PER_DRIVER:
        raise ValueError("The native garage requires every vehicle for every retail driver")
    lines = ["#pragma once", "#include <array>", "namespace mkw {",
             "// Generated from the original vehicles and kartDriverDispParam.bin.",
             f"inline constexpr unsigned VehiclesPerDriver={VEHICLES_PER_DRIVER}; // menu choice order, UiVehicleNames",
             "struct GarageChoice {const char* name;const char* prefix;unsigned character,vehicle;float seatY,seatZ;bool bike;unsigned extrasMask;bool bodyAnimation;unsigned extraAnimationMask;};",
             "inline constexpr std::array<const char*,9> GarageExtraNames{{" + ",".join(json.dumps(n) for n in EXTRA_NAMES) + "}};",
             f"inline constexpr std::array<GarageChoice,{len(choices)}> GarageChoices{{{{"]
    for row in choices:
        boolean = lambda v: "true" if v else "false"
        lines.append("    {%s,%s,%d,%d,%.9ff,%.9ff,%s,%d,%s,%d}," % (
            json.dumps(row["name"]), json.dumps(row["prefix"]), row["character"], row["vehicle"],
            row["seatY"], row["seatZ"], boolean(row["bike"]), row["extrasMask"],
            boolean(row["bodyAnimation"]), row["extraAnimationMask"]))
    lines.extend(["}};", "}", ""])
    (ROOT / "include/mkw/garage.hpp").write_text("\n".join(lines))


def cook_choice(choice, output, *, source_root=None, export_root=None, models_only=False, include_driver=True):
    archive = (source_root or ROOT / "assets/vehicles") / choice["prefix"].rstrip("_")
    kart = json.loads((archive / "kart-inventory.json").read_text())
    result = subprocess.run(["node", str(ROOT / "build/export_driver.mjs"),
                             str(archive / "kart_model.brres"), "--inventory", "body"],
                            check=True, capture_output=True, text=True)
    kart_animations = json.loads(result.stdout)["chr0"]
    if any("nodeNames" not in clip for clip in kart_animations):
        raise ValueError("Rebuild the exporter with CHR0 target inventory support")
    part_clips = {}
    for model in kart["nodes"]:
        names = {node["name"] for node in model["nodes"]}
        part_clips[model["model"]] = [clip for clip in kart_animations if names.intersection(clip["nodeNames"])]
    source_parts = set(choice["parts"])
    if choice["bike"]:
        mapping = {"body": "body", "driver": "model", "tire_fl": "tire_f", "tire_fr": "tire_r"}
    else:
        # Most karts model only the front pair and reuse it at the rear; the
        # Blue Falcon models tire_fr and the rear pair, reusing tire_fr on the left.
        front_left = "tire_fl" if "tire_fl" in source_parts else "tire_fr"
        front_right = "tire_fr" if "tire_fr" in source_parts else "tire_fl"
        mapping = {"body": "body", "driver": "model", "tire_fl": front_left, "tire_fr": front_right}
        mapping.update(tire_rl="tire_rl" if "tire_rl" in source_parts else front_left,
                       tire_rr="tire_rr" if "tire_rr" in source_parts else front_right)
    if not include_driver:mapping.pop("driver")
    mapping.update((part, part) for part in EXTRA_NAMES if part in source_parts)
    choice["extrasMask"] = sum(1 << i for i, name in enumerate(EXTRA_NAMES) if name in source_parts)
    choice["bodyAnimation"] = bool(part_clips.get("body"))
    choice["extraAnimationMask"] = sum(1 << i for i, name in enumerate(EXTRA_NAMES) if part_clips.get(name))
    choice["modelSources"] = mapping
    choice["partClips"] = {name: clips for name, clips in part_clips.items() if clips}
    choice["models"] = []
    choice["animationBanks"] = []
    cooked_sources = {}
    encoded_textures = {p.stem for p in (output / 'textures').glob('*.t3x')} if models_only else None
    if models_only:
        encoded_textures.update(p.stem for p in (ROOT/'sd-card/3ds/mkw-3ds/textures').glob('*.t3x'))
    for part, source_model in mapping.items():
        name = choice["prefix"] + part
        source_file = archive / ("driver_model.brres" if part == "driver" else "kart_model.brres")
        target = output / "models" / (name + ".gmdl")
        # The original karts deliberately reuse their two source tire meshes on
        # the rear axle. Retain that geometry under explicit runtime wheel roles.
        if part != "driver" and source_model in cooked_sources:
            shutil.copyfile(cooked_sources[source_model], target)
            choice["models"].append(name)
            continue
        destination = (export_root or ROOT / "assets/garage-parts") / name
        command = ["node", str(ROOT / "build/export_brres.mjs"), str(source_file), str(destination), source_model]
        if part == "driver":
            command.extend(["drive", "15"])
        elif any(clip["name"] == "handle_drive" for clip in part_clips.get(source_model, [])):
            command.extend(["handle_drive", "15"])
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                       env={**os.environ, 'MKW_RACE_MATERIALS': '1'})
        exported = json.loads((destination / 'model.json').read_text())
        for model in exported['models']:
            for shape in model['shapes']:
                # Layered GX racer shaders end in a runtime-generated lm_1
                # light map. Selecting that as albedo renders a black racer.
                if not shape.get('retail'):
                    raise ValueError('Race material must retain its source lighting state: ' + name)
                if shape['texture'] >= 0 and exported['textures'][shape['texture']]['name'].startswith(('lm_0', 'lm_1')):
                    raise ValueError('Race albedo incorrectly selects a light map: ' + name)
        animated = part == "driver" or bool(part_clips.get(source_model))
        parts = cook(destination, output, name, preserve_vertices=animated, encoded_textures=encoded_textures)
        if len(parts) != 1:
            raise ValueError(f"Garage part must fit one model: {name}")
        shutil.copyfile(output / "models" / (parts[0]["name"] + ".gmdl"), target)
        choice["models"].append(name)
        if part != "driver":
            cooked_sources[source_model] = target
        if animated:
            bank = output / "models" / (name + ".dskm")
            if models_only:
                if not bank.is_file():raise ValueError('Missing existing animation bank: ' + str(bank))
                choice['animationBanks'].append(name)
                continue
            result = subprocess.run(["node", str(ROOT / "build/export_driver.mjs"),
                                     str(source_file), str(bank), source_model],
                                    check=True, capture_output=True, text=True)
            receipt = json.loads(result.stdout)
            receipt.update(source=source_file.relative_to(ROOT).as_posix(), source_sha256=sha256(source_file),
                           sha256=sha256(bank))
            bank.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")
            choice["animationBanks"].append(name)
    print(f"Cooked {choice['driver']} / {choice['name']}: {len(mapping)} parts, {len(choice['animationBanks'])} animation banks", flush=True)
    return choice






