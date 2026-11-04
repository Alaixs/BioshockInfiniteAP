"""Inventory of BioShock Infinite's Gear (clothing): packages placed in the levels, and pieces.

    python tools/catalog_gear.py

Packages: an XLootContainerKActor (Outer = PersistentLevel) with a "Pickup_Gear_*" archetype. Some
give a fixed piece (Pickup_Gear_FistsOfFire), others draw one at random from their area's table
(Pickup_Gear_GearBW...). The mod recognizes them by "level package.object name", like the Infusion
bottles, and the place comes from the nearest Voxophone (catalog_infusions).

Excluded: the packages hidden at start (bStartHidden): the 10 of the Blue Ribbon
(S_TWN_Float_B_Loot) are Season Pass bonuses, missing without it.

Pieces: one per XGearStaticEffectComponent of Arc_XGearComponents_SP (GlobalXItemDatabase_SF,
always loaded). The game names a piece with a 64-bit number (XItem.ItemLookupID) that its database
decodes (XItemDatabase.TheGearItem):
    (1 << 48) | (effect LookupID << 21) | (slot << 3)
The mod gives it with XPlayerController.ServerTransactLootItem(number, 1, 1, 0), in any level.
Writes apworld/bioshock_infinite/gear.py.
"""

from __future__ import annotations

import math
import pathlib
import struct

from catalog_infusions import NEAR
from catalog_voxophones import ALIASES, AREAS, PREFIX, ascii_text, recording_of, translations
from game import COOKED
from upk import Package

OUT = pathlib.Path(__file__).parent.parent / "apworld" / "bioshock_infinite" / "gear.py"

GEAR = "Pickup_Gear_"
EFFECTS = "Arc_XGearComponents_SP"
# Lutece labs (S_Lut_*): visited during the Emporia chapter.
EXTRA_AREAS = {"lut": "Emporia"}
# EGearSlot: the player's four slots.
SLOTS = {"CGS_Brain": (10, "Hat"), "CGS_Heart": (11, "Shirt"), "CGS_Arms": (12, "Pants"), "CGS_Lungs": (13, "Boots")}


def area_of(package: str) -> str:
    prefix = package.split("_")[1].lower()
    area = AREAS.get(prefix) or EXTRA_AREAS.get(prefix)
    if not area:
        raise SystemExit(f"unknown area for package {package}")
    return area


def lookup_id(effect: int, slot: int) -> int:
    return (1 << 48) | (effect << 21) | (slot << 3)


def text_of(raw) -> str:
    """FString read as is by upk (length + zero-terminated text)."""
    if not isinstance(raw, bytes) or len(raw) < 5:
        return ""
    length = struct.unpack_from("<i", raw, 0)[0]
    if length < 0:
        return raw[4 : 4 - 2 * length].decode("utf-16-le").rstrip("\0")
    return raw[4 : 4 + length].decode("latin1").rstrip("\0")


def pieces() -> list[tuple[str, int, int, str]]:
    """(name, effect LookupID, slot, slot name), in LookupID order."""
    pkg = Package(str(COOKED / "GlobalXItemDatabase_SF.xxx"))
    out = []
    for exp in pkg.exports:
        try:
            path = pkg.path_of(exp, "GlobalXItemDatabase_SF")
            if pkg.class_of(exp) != "XGearStaticEffectComponent" or path.split(".")[1] != EFFECTS:
                continue
        except Exception:
            continue
        props = pkg.properties(exp)
        name = text_of(props.get("FriendlyName", (None, None))[1])
        name = name[3:] if name.startswith("of ") else name
        raw_slots = props.get("ValidForLoadoutSlots", (None, None))[1]
        lookup = props.get("LookupID", (None, None))[1]
        if not name or not isinstance(raw_slots, bytes) or not isinstance(lookup, bytes):
            continue  # StaticEffect__GearValue_*: not a piece
        count = struct.unpack_from("<i", raw_slots, 0)[0]
        slots = [pkg.names[struct.unpack_from("<i", raw_slots, 4 + 8 * i)[0]] for i in range(count)]
        if len(slots) != 1 or slots[0] not in SLOTS:
            raise SystemExit(f"unexpected slot for {path}: {slots}")
        slot, slot_name = SLOTS[slots[0]]
        out.append((ascii_text(name), lookup[0], slot, slot_name))
    out.sort(key=lambda p: p[1])
    names = [p[0] for p in out]
    if len(set(names)) != len(names):
        raise SystemExit("two Gear pieces have the same name")
    return out


def scan() -> tuple[list[tuple[str, str, tuple]], list[tuple[str, str, tuple]]]:
    """(Gear packages, Voxophones): (area, id or place, position)."""
    names = translations()
    gear, voxophones = [], []
    for f in sorted(COOKED.glob("S_*.xxx")):
        try:
            pkg = Package(str(f))
        except Exception:
            continue
        if not any(n.startswith((GEAR, PREFIX)) for n in pkg.names):
            continue
        for exp in pkg.exports:
            try:
                archetype = pkg.archetype_of(exp) or ""
                path = pkg.path_of(exp, f.stem)
            except Exception:
                continue
            if ".PersistentLevel." not in path or not archetype.startswith((GEAR, PREFIX)):
                continue
            props = pkg.properties(exp)
            pos = props.get("XPrivateLocalLocation", (None, None))[1]
            if not isinstance(pos, tuple):
                print(f"  ! {path}: no position")
                continue
            if archetype.startswith(GEAR):
                if props.get("bStartHidden", (None, False))[1]:
                    print(f"  - {f.stem}.{path.rsplit('.', 1)[1]} ({archetype}): hidden at start, excluded")
                    continue
                gear.append((area_of(f.stem), f"{f.stem}.{path.rsplit('.', 1)[1]}", pos))
            else:
                recording = recording_of(pkg, exp) or archetype[len(PREFIX) :]
                info = names.get(ALIASES.get(recording, recording).lower(), {})
                place = ascii_text(info.get("LocationFound", ""))
                if place:
                    voxophones.append((area_of(f.stem), place, pos))
    return gear, voxophones


def main() -> None:
    gear, voxophones = scan()
    rows = []
    for area, ident, pos in gear:
        near = [(math.dist(pos, vpos), place) for varea, place, vpos in voxophones if varea == area]
        distance, place = min(near, default=(math.inf, ""))
        rows.append((area, ident, place if distance <= NEAR else "", distance))
    order = list(AREAS.values())
    rows.sort(key=lambda r: (order.index(r[0]), r[1].lower()))

    lines = [
        '"""All the Gear of the game. Generated by tools/catalog_gear.py."""',
        "",
        '# Placed packages: (area, game id (package.object), place of the nearest Voxophone or "")',
        "GEAR_PICKUPS: list[tuple[str, str, str]] = [",
    ]
    for area, ident, place, distance in rows:
        lines.append(f"    {(area, ident, place)!r},  # Voxophone {distance / 100:.0f} m away")
    lines += [
        "]",
        "",
        "# Pieces: (name, slot, the game's item number (XItem.ItemLookupID))",
        "GEAR_ITEMS: list[tuple[str, str, int]] = [",
    ]
    for name, effect, slot, slot_name in pieces():
        lines.append(f"    {(name, slot_name, lookup_id(effect, slot))!r},")
    lines.append("]")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(rows)} Gear packages -> {OUT}")


if __name__ == "__main__":
    main()
