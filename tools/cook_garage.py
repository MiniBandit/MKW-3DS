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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--extract-only", action="store_true")
    parser.add_argument("--cook-only", action="store_true", help="Use the previously verified local extraction")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--characters", nargs="+", choices=[r[0] for r in ROSTER])
    args = parser.parse_args()
    if args.cook_only:
        audit = json.loads((ROOT / "assets/vehicles/garage-audit.json").read_text())
        for choice in audit["choices"]:
            archive = ROOT / "assets/vehicles" / choice["prefix"].rstrip("_")
            if sha256(archive / "driver_model.brres") != choice["driver_sha256"] or sha256(archive / "kart_model.brres") != choice["kart_sha256"]:
                raise ValueError("Source extraction changed: " + choice["prefix"])
        cook_audit(audit, args)
        return
    subprocess.run([sys.executable, str(ROOT / "tools/extract_disc.py"), str(args.disc),
                    str(ROOT / "data/extracted"), "--list-only"], check=True)
    inventory = json.loads((ROOT / "data/extracted/disc-files.json").read_text())
    files = {row["name"]: row for row in inventory["files"]}
    disc = Disc(args.disc)
    partition = data_partition(disc)
    display = (ROOT / "assets/common/kartDriverDispParam.bin").read_bytes()
    if struct.unpack_from(">II", display) != (36, 48):
        raise ValueError("Unexpected source display table")
    audit = {"version": 1, "identity": inventory["identity"],
             "sector_h0_verified": True, "choices": []}
    for driver_index, (slug, name, character, code, size) in enumerate(ROSTER):
        if args.characters and slug not in args.characters:
            continue
        for slot, (vehicle, vehicle_name, stem) in enumerate(VEHICLES[size]):
            archive_name = f"Race/Kart/{stem}-{code}.szs"
            entry = files[archive_name]
            compressed = ROOT / "data/extracted" / archive_name
            compressed.parent.mkdir(parents=True, exist_ok=True)
            compressed.write_bytes(partition.read(entry["offset"], entry["size"]))
            archive = ROOT / "assets/vehicles" / f"{slug}_{stem}"
            unpack(compressed, archive)
            driver = inspect_driver(archive)
            kart = inspect_kart(archive)
            seat_y, seat_z = struct.unpack_from(">2f", display, 8 + (vehicle * 48 + character) * 56)
            choice = {
                "driverIndex": driver_index, "slot": slot, "driver": name,
                "driverSlug": slug, "name": vehicle_name,
                "prefix": f"{slug}_{stem}_", "character": character,
                "vehicle": vehicle, "bike": vehicle >= 18,
                "seatY": seat_y, "seatZ": seat_z,
                "archive": archive_name, "archive_sha256": sha256(compressed),
                "driver_sha256": sha256(archive / "driver_model.brres"),
                "kart_sha256": sha256(archive / "kart_model.brres"),
                "clips": driver["chr0"], "parts": [m["model"] for m in kart["nodes"]],
            }
            audit["choices"].append(choice)
            print(f"{name} / {vehicle_name}: {len(driver['chr0'])} clips; {choice['parts']}", flush=True)
    disc.f.close()
    destination = ROOT / "assets/vehicles"
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "garage-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    if not args.extract_only:
        cook_audit(audit, args)


def cook_audit(audit, args):
    choices = [choice for choice in audit["choices"] if not args.characters or choice["driverSlug"] in args.characters]
    with ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 6))) as workers:
        cooked = list(workers.map(lambda choice: cook_choice(choice, args.output), choices))
    path = args.output / "models/garage.json"
    if args.characters and path.exists():
        records = {row["prefix"]: row for row in json.loads(path.read_text())["choices"]}
        records.update((row["prefix"], row) for row in cooked)
        cooked = sorted(records.values(), key=lambda row: (row["driverIndex"], row["slot"]))
    manifest = dict(audit, choices=cooked, extraNames=EXTRA_NAMES)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    if len(cooked) == len(ROSTER) * VEHICLES_PER_DRIVER:
        write_native_garage(cooked)


if __name__ == "__main__":
    main()
