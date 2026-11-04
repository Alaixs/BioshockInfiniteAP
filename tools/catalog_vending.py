"""Inventory of the vending machines placed in BioShock Infinite's levels.

    python tools/catalog_vending.py

Walks every .xxx package of the game, keeps the XVendingMachines placed in a level
(Outer = PersistentLevel) and writes tools/vending_catalog.json:
    [{"package", "path", "archetype", "kind", "world", "location": [x, y, z], "yaw"}, ...]
- "path" is the same as the PathName the mod sees in game (poi / vend commands);
- "world" is the main map (*_P package) loading that sublevel: teleporting only works to a machine
  of the current map;
- "yaw" in engine units (65536 = 360 degrees).
"""

import json
import pathlib
import sys
import time

from game import COOKED
from upk import Package

OUT = pathlib.Path(__file__).with_name("vending_catalog.json")


def kind_of(archetype: str) -> str:
    if "ConsumableVendingMachine" in archetype:
        return "Dollar Bill"
    if "VigorVending" in archetype:
        return "Veni! Vidi! Vigor!"
    return "Minuteman's Armory"


def streaming_map(files: list[pathlib.Path]) -> dict[str, str]:
    """Sublevel (lower case) -> main map loading it."""
    owner: dict[str, str] = {}
    for f in files:
        if not f.stem.upper().endswith("_P"):
            continue
        try:
            pkg = Package(str(f))
        except Exception:
            continue
        owner.setdefault(f.stem.lower(), f.stem)
        for exp in pkg.exports:
            cls = pkg.class_of(exp)
            if cls.startswith("LevelStreaming") or cls == "XLevelStreamingVolume":
                name = pkg.properties(exp).get("PackageName", (None, None))[1]
                if isinstance(name, str):
                    owner.setdefault(name.lower(), f.stem)
    return owner


def scan(path: pathlib.Path) -> list[dict]:
    pkg = Package(str(path))
    if "XVendingMachine" not in pkg.names:
        return []
    found = []
    for exp in pkg.exports:
        if pkg.class_of(exp) != "XVendingMachine":
            continue
        full = pkg.path_of(exp, path.stem)
        if ".PersistentLevel." not in full:
            continue  # templates (archetypes), not placed machines
        props = pkg.properties(exp)
        archetype = pkg.archetype_of(exp)
        found.append(
            {
                "package": path.stem,
                "path": full,
                "archetype": archetype,
                "kind": kind_of(archetype),
                "location": [round(v, 1) for v in props.get("XPrivateLocalLocation", (None, (0, 0, 0)))[1]],
                "yaw": props.get("XPrivateLocalRotation", (None, (0, 0, 0)))[1][1],
            }
        )
    return found


def main() -> None:
    files = sorted(COOKED.glob("*.xxx"))
    start, catalog, errors = time.time(), [], 0
    owners = streaming_map(files)
    print(f"{len(owners)} sublevels tied to a map ({time.time() - start:.0f} s)")
    for f in files:
        try:
            catalog += scan(f)
        except Exception as e:  # code packages or another format
            errors += 1
            print(f"  ! {f.name}: {e}", file=sys.stderr)
    for machine in catalog:
        machine["world"] = owners.get(machine["package"].lower(), "?")
    OUT.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(catalog)} machines in {len(files)} packages ({errors} errors) -> {OUT}")


if __name__ == "__main__":
    main()
