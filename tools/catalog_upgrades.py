"""Inventory of the upgrade machines (Veni! Vidi! Vigor!, Minuteman's Armory) and what they sell.

    python tools/catalog_vending.py    # inventory of the vending machines (vending_catalog.json)
    python tools/catalog_upgrades.py   # writes apworld/bioshock_infinite/upgrades.py

Machines: the placed XVendingMachines that are not Dollar Bills, except the unusable ones
(EXCLUDED). Two machines at the same place (less than 3 m apart) are two versions of the same one,
depending on story progress: one machine, several paths.

Upgrades: the Loot_Upgrade_* (weapons) and Loot_StableTonic_* (Vigors) these machines sell (ItemList
of their archetype or of the placed object). The mod gives them by their number in the game's item
database (XLootDropItemDefinition.ItemLookupID), like a purchase.
"""

from __future__ import annotations

import json
import math
import pathlib
import re
import struct

from catalog_voxophones import AREAS, TRANSLATIONS, ascii_text, title_case
from game import COOKED
from upk import Package

HERE = pathlib.Path(__file__).parent
CATALOG = HERE / "vending_catalog.json"
OUT = HERE.parent / "apworld" / "bioshock_infinite" / "upgrades.py"

# Unusable machines: not placed in the game (DoNotPlace), broken (Shantytown), factory production
# line (scenery).
EXCLUDED = ("DoNotPlace", "ShantytownBroken", "S_Fink_factory_geo.")
# Added afterwards (the Fair's Veni! Vidi! Vigor!): at the end of the list, so the machines already
# there keep their number (name and id of their locations).
ADDED_LATER = ("S_TWN_FairStreet_Geo.", "S_TWN_Park_Geo.")
SAME_MACHINE = 300.0  # 3 m
WEAPONS = {
    "Pistol": "Pistol",
    "MachineGun": "Machine Gun",
    "MachineGun_VP": "Repeater",
    "ThreeShotCarbine": "Carbine",
    "ThreeShotCarbine_VP": "Burstgun",
    "Shotgun": "Shotgun",
    "Shotgun_VP": "Heater",
    "SniperRifle": "Sniper Rifle",
    "RPG": "RPG",
    "FlakCannon": "Volley Gun",
    "FlakCannon_VP": "Hail Fire",
    "HandCannon": "Hand Cannon",
    "Handcannon": "Hand Cannon",
}
STATS = {
    "ClipSize": "Clip Size",
    "Damage1": "Damage 1",
    "Damage2": "Damage 2",
    "Spread": "Accuracy",
    "Recoil": "Recoil",
    "ReloadSpeed": "Reload Speed",
    "Reserve": "Ammo Capacity",
    "ReserveSize": "Ammo Capacity",
    "FireRate": "Fire Rate",
    "ExplosionRadius": "Blast Radius",
    "ProjectileSpeed": "Projectile Speed",
    "ConeSize": "Spread",
}


def item_lists() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """(ItemList per machine archetype, ItemList of the placed machines that have their own)."""
    archetypes: dict[str, list[str]] = {}
    placed: dict[str, list[str]] = {}
    for f in sorted(COOKED.glob("S_*.xxx")):
        if f.stem.upper().startswith("S_DCOM"):
            continue
        try:
            pkg = Package(str(f))
        except Exception:
            continue
        if "XVendingMachine" not in pkg.names:
            continue
        for exp in pkg.exports:
            try:
                if pkg.class_of(exp) != "XVendingMachine":
                    continue
                path = pkg.path_of(exp, f.stem)
            except Exception:
                continue
            raw = pkg.properties(exp).get("ItemList", (None, None))[1]
            if not isinstance(raw, bytes) or len(raw) < 8:
                continue
            names = []
            for offset in range(4, len(raw) - 3, 4):
                ref = struct.unpack_from("<i", raw, offset)[0]
                if ref and -len(pkg.imports) <= ref <= len(pkg.exports):
                    try:
                        name = pkg.ref_name(ref)
                    except Exception:
                        continue
                    if name.startswith("Loot_"):
                        names.append(name)
            if ".PersistentLevel." in path:
                placed[path] = names
            else:
                archetypes.setdefault(path.rsplit(".", 1)[1], names)
    return archetypes, placed


def lookup_ids(loots: set[str]) -> dict[str, int]:
    """Loot_* -> ItemLookupID (searched in the main maps, which contain them all)."""
    found: dict[str, int] = {}
    for f in sorted(COOKED.glob("S_*_P.xxx")):
        pkg = Package(str(f))
        for exp in pkg.exports:
            if exp.name in loots and exp.name not in found:
                raw = pkg.properties(exp).get("ItemLookupID", (None, None))[1]
                if isinstance(raw, bytes) and len(raw) == 8:
                    found[exp.name] = struct.unpack("<q", raw)[0]
        if len(found) == len(loots):
            break
    missing = loots - found.keys()
    if missing:
        raise SystemExit(f"ItemLookupID not found: {sorted(missing)}")
    return found


def vigor_names() -> dict[str, str]:
    """Tonic_* -> the game's name ("Devil's Kiss Aid")."""
    text = TRANSLATIONS.read_text(encoding="utf-16")
    out = {}
    for m in re.finditer(r"\[(Tonic_\w+) XWeaponModItem\]\s*(.*?)(?=\n\[|\Z)", text, re.S):
        name = re.search(r'ItemName=\\"(.*?)\\"', m.group(2))
        if name:
            out[m.group(1)] = title_case(ascii_text(name.group(1).replace("\\'", "'")))
    return out


def upgrade_name(loot: str, vigors: dict[str, str]) -> str:
    if loot.startswith("Loot_StableTonic_"):
        return vigors["Tonic_" + loot[len("Loot_StableTonic_") :]]
    rest = loot[len("Loot_Upgrade_") :]
    weapon, stat = rest.rsplit("_", 1)
    return f"{WEAPONS[weapon]} {STATS[stat]}"


def main() -> None:
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    machines = [
        m
        for m in catalog
        if m["kind"] != "Dollar Bill" and not any(x in m["path"] or x in m["archetype"] for x in EXCLUDED)
    ]
    archetypes, placed = item_lists()

    # One machine = one or several paths at the same place.
    groups: list[list[dict]] = []
    for m in machines:
        for g in groups:
            if g[0]["kind"] == m["kind"] and math.dist(g[0]["location"], m["location"]) < SAME_MACHINE:
                g.append(m)
                break
        else:
            groups.append([m])

    sold: set[str] = set()
    for m in machines:
        for loot in placed.get(m["path"]) or archetypes.get(m["archetype"], []):
            if loot.startswith(("Loot_Upgrade_", "Loot_StableTonic_")):
                sold.add(loot)
    for names in archetypes.values():  # known archetypes without a readable machine: same catalog
        sold |= {n for n in names if n.startswith(("Loot_Upgrade_", "Loot_StableTonic_"))}
    lookups = lookup_ids(sold)
    vigors = vigor_names()
    upgrades = sorted((upgrade_name(loot, vigors), loot, lookups[loot]) for loot in sold)
    if len({u[0] for u in upgrades}) != len(upgrades):
        raise SystemExit("two upgrades have the same name")

    order = list(AREAS.values())
    rows = []
    for g in groups:
        prefix = g[0]["package"].split("_")[1].lower()
        rows.append((AREAS[prefix], g[0]["kind"], sorted(m["path"] for m in g)))

    def added_later(row) -> int:
        return next((i for i, x in enumerate(ADDED_LATER) if x in row[2][0]), -1)

    rows.sort(key=lambda r: (added_later(r), order.index(r[0]), r[1], r[2][0].lower()))

    lines = [
        '"""Upgrade machines and upgrades of the game. Generated by tools/catalog_upgrades.py."""',
        "",
        "# (area, machine type, paths of the object in the game: several versions of one machine)",
        "UPGRADE_MACHINES: list[tuple[str, str, list[str]]] = [",
    ]
    lines += [f"    {row!r}," for row in rows]
    lines += [
        "]",
        "",
        "# (name, loot sold by the machines, the item's number in the game's database)",
        "UPGRADES: list[tuple[str, str, int]] = [",
    ]
    lines += [f"    {u!r}," for u in upgrades]
    lines.append("]")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(rows)} machines, {len(upgrades)} upgrades -> {OUT}")


if __name__ == "__main__":
    main()
