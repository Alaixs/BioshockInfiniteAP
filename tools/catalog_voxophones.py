"""Inventory of the Voxophones placed in BioShock Infinite's levels.

    python tools/catalog_voxophones.py

Walks the game's .xxx packages: a placed Voxophone is an XLootContainerKActor (Outer =
PersistentLevel) whose archetype is named "Pickup_AudioLog_<id>". Its title, speaker and the
place shown by the game come from the English translations (PreCoalescedItemAssets.INT, section
[Consumable_AudioLog_<id> XAudioLogItem]). Writes apworld/bioshock_infinite/voxophones.py.

Excluded: the base archetype "Pickup_AudioLog_Base". The S_DCOM_* packages are Comstock House (base
game, end of the Emporia chapter); the Burial at Sea DLC is separate (DLC folder).
"""

from __future__ import annotations

import pathlib
import re
import struct

from game import COOKED, XGAME
from upk import Package

TRANSLATIONS = XGAME / "Localization" / "INT" / "PreCoalescedItemAssets.INT"
OUT = pathlib.Path(__file__).parent.parent / "apworld" / "bioshock_infinite" / "voxophones.py"

PREFIX = "Pickup_AudioLog_"
# Level package prefix -> game area, in story order.
AREAS = {
    "twn": "Town Center",
    "lizt": "Monument Island",
    "bw": "Battleship Bay",
    "fink": "Finkton",
    "emp": "Emporia",
    "dcom": "Emporia",  # Comstock House
    "chu": "Hand of the Prophet",
}
# Archetypes whose name differs (typo) from the recording's.
ALIASES = {"LC_Beyond_Demption": "LC_Beyond_Redemption", "ES_Take_Her_Alive": "ES_Taker_Her_Alive"}
SMALL_WORDS = {"a", "an", "the", "of", "to", "in", "on", "for", "and", "or", "by", "at", "our", "re"}


def title_case(text: str) -> str:
    words = text.lower().split()
    return " ".join(w if i and w in SMALL_WORDS else w[:1].upper() + w[1:] for i, w in enumerate(words))


def ascii_text(text: str) -> str:
    return (
        text.replace("’", "'")
        .replace("‘", "'")
        .replace("“", '"')
        .replace("”", '"')
        .replace("–", "-")
        .replace("—", "-")
        .strip()
    )


def translations() -> dict[str, dict[str, str]]:
    text = TRANSLATIONS.read_text(encoding="utf-16")
    out = {}
    for m in re.finditer(r"\[Consumable_AudioLog_(\w+) XAudioLogItem\]\s*(.*?)(?=\n\[|\Z)", text, re.S):
        out[m.group(1).lower()] = dict(re.findall(r'^(\w+)=\\"(.*?)\\"\s*$', m.group(2), re.M))
    return out


LOOT_PREFIX = "Loot_Consumable_AudioLog_"


def recording_of(pkg: Package, exp) -> str | None:
    """Recording given by a container: its LootDefinitionList (the placed object's, otherwise its
    archetype's) points to an XLootDropItemDefinition "Loot_Consumable_AudioLog_<id>". The archetype
    name is not enough: some containers keep it but change their list."""
    for _ in range(4):
        raw = pkg.properties(exp).get("LootDefinitionList", (None, None))[1]
        if isinstance(raw, bytes) and len(raw) >= 8:
            count = struct.unpack_from("<i", raw, 0)[0]
            for i in range(count):
                name = pkg.ref_name(struct.unpack_from("<i", raw, 4 + 4 * i)[0])
                if name.startswith(LOOT_PREFIX):
                    return name[len(LOOT_PREFIX) :]
        if exp.archetype <= 0:  # archetype in another package: its properties can't be read here
            break
        exp = pkg.exports[exp.archetype - 1]
    return None


def placed() -> dict[str, str]:
    """Voxophone id -> level package where it is placed (the first one found)."""
    found: dict[str, str] = {}
    for f in sorted(COOKED.glob("S_*.xxx")):
        try:
            pkg = Package(str(f))
        except Exception:
            continue
        if not any(n.startswith(PREFIX) for n in pkg.names):
            continue
        for exp in pkg.exports:
            try:
                archetype = pkg.archetype_of(exp) or ""
                path = pkg.path_of(exp, f.stem)
            except Exception:
                continue
            if archetype.startswith(PREFIX) and ".PersistentLevel." in path:
                recording = recording_of(pkg, exp)
                if recording is None:
                    print(f"  ! {path}: recording not found, archetype name kept")
                    recording = archetype[len(PREFIX) :]
                found.setdefault(recording, f.stem)
    found.pop("Base", None)
    return found


def main() -> None:
    names = translations()
    rows = []
    for vox_id, package in placed().items():
        prefix = package.split("_")[1].lower()
        if prefix not in AREAS:
            raise SystemExit(f"{vox_id}: unknown area for package {package}")
        info = names.get(ALIASES.get(vox_id, vox_id).lower())
        if not info:
            raise SystemExit(f"{vox_id}: no translation")
        speaker = ascii_text(info.get("ItemDescription", "")).lstrip("- ").strip()
        rows.append(
            (
                AREAS[prefix],
                vox_id,
                title_case(ascii_text(info["ItemName"])),
                speaker,
                ascii_text(info.get("LocationFound", "")),
            )
        )
    order = list(AREAS.values())
    rows.sort(key=lambda r: (order.index(r[0]), r[4].lower(), r[2].lower()))

    lines = [
        '"""All the Voxophones of the game. Generated by tools/catalog_voxophones.py."""',
        "",
        "# (area, game id (archetype Pickup_AudioLog_<id>), title, author, place shown by the game)",
        "VOXOPHONES: list[tuple[str, str, str, str, str]] = [",
    ]
    lines += [f"    {row!r}," for row in rows]
    lines.append("]")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(rows)} Voxophones -> {OUT}")


if __name__ == "__main__":
    main()
