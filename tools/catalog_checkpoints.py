"""Inventory of the story checkpoints of BioShock Infinite.

    python tools/catalog_checkpoints.py   # writes apworld/bioshock_infinite/checkpoints.py

The game saves when a script runs XSeqAct_Checkpoint; the action points to an entry of the
SPCheckpointDisplayInfo table (XCheckpointDisplayInfo, DefaultPCBenchmarkMap package) by
CheckpointDisplayInfoStructLookupName. That table gives the title shown while loading
(Localization/INT/Arc_XGameInfo.INT, same order).

Chapters: the "Load Chapter" menu lists the bookmark saves (Steam userdata,
8870/remote/SaveData, <step>-<date>.bkm; the other autosaves have another extension). The game
writes one for a script checkpoint with bIsBookMark (XSeqAct_Checkpoint bit 1, passed to the save
function at 0xAECE20 as CurrentCheckpointIsABookmark), and for some travels to a new map, named
after their loading screen (XSeqAct_ShowLoadingScreen): that second rule can't be read from the
packages. So the chapters come from the .bkm files of a complete playthrough (40): the table's
entries 0 to 36 (Welcome Center ... Command Deck), plus Fink (Finkton Docks, entry 41, until 0.2.2
a checkpoint here), S_Light_P (The Lighthouse, no script checkpoint: not a step) and LutT_Start
(after the goal).

The mod sees a step reached when the action's SequenceOp.ActivateCount counter becomes 1.
Kept: the entries an XSeqAct_Checkpoint uses (the others are never seen by the mod), except those
after the end of the game (Siphon destroyed: The Sea of Doors, Lutece).
"""

from __future__ import annotations

import pathlib
import re
import struct

from catalog_voxophones import title_case
from game import COOKED, XGAME, level_packages
from upk import Package

OUT = pathlib.Path(__file__).parent.parent / "apworld" / "bioshock_infinite" / "checkpoints.py"
TABLE = "SPCheckpointDisplayInfo"
CHAPTERS = 37  # entries 0..36: "Load Chapter" chapters
EXTRA_CHAPTERS = {"Fink"}  # also bookmark saves (see above)
AFTER_GOAL = ("LutT_", "Lut", "OFB", "ReturnToMain")
FIXES = {"Z.h. ": "Z.H. ", "Sky-line": "Sky-Line"}
# Story area from the name prefix (the opening lighthouse counts as Town Center; Comstock House is
# visited during Emporia).
AREAS = [
    ("TWN", "Town Center"),
    ("TC_", "Town Center"),
    ("Light", "Town Center"),
    ("LizT", "Monument Island"),
    ("BW", "Battleship Bay"),
    ("Fink", "Finkton"),
    ("SFink", "Finkton"),
    ("Emp", "Emporia"),
    ("EMP", "Emporia"),
    ("DCom", "Emporia"),
    ("DCOM", "Emporia"),
    ("CHU", "Hand of the Prophet"),
]


def area_of(name: str) -> str:
    for prefix, area in AREAS:
        if name.startswith(prefix):
            return area
    raise SystemExit(f"unknown area for step {name}")


def table_names() -> list[str]:
    """CheckpointLookupName of each table entry, in order."""
    pkg = Package(str(COOKED / "DefaultPCBenchmarkMap.xxx"))
    exp = next(e for e in pkg.exports if e.name == TABLE)
    raw = pkg.properties(exp)["CheckpointDisplayInfoStructArray"][1]
    tag = pkg.names.index("CheckpointLookupName")
    names = []
    for offset in range(4, len(raw) - 31):  # entries are not aligned on 4 bytes
        index, number = struct.unpack_from("<ii", raw, offset)
        if index == tag and number == 0:
            # property tag: name, type, size, index, then the value (FName).
            value, value_number = struct.unpack_from("<ii", raw, offset + 24)
            name = pkg.names[value]
            names.append(name if value_number == 0 else f"{name}_{value_number - 1}")
    return names


def titles() -> list[str]:
    text = (XGAME / "Localization" / "INT" / "Arc_XGameInfo.INT").read_text(encoding="utf-16")
    found = re.findall(r'CheckpointDisplayInfoStructArray\[(\d+)\]=\(CheckpointDisplayString=\\"(.*?)\\"', text)
    out = []
    for _, t in sorted(found, key=lambda m: int(m[0])):
        title = title_case(t.replace("\\'", "'"))
        for bad, good in FIXES.items():
            title = title.replace(bad, good)
        out.append(title)
    return out


def used_by_scripts() -> set[str]:
    used: set[str] = set()
    for f in level_packages():
        try:
            pkg = Package(str(f))
        except Exception:
            continue
        if "XSeqAct_Checkpoint" not in pkg.names:
            continue
        for exp in pkg.exports:
            try:
                if pkg.class_of(exp) == "XSeqAct_Checkpoint":
                    name = pkg.properties(exp).get("CheckpointDisplayInfoStructLookupName", (None, None))[1]
                    if isinstance(name, str):
                        used.add(name)
            except Exception:
                continue
    return used


def main() -> None:
    names, shown = table_names(), titles()
    if len(names) != len(shown):
        raise SystemExit(f"table: {len(names)} names for {len(shown)} titles")
    used = used_by_scripts()
    rows = [
        (i < CHAPTERS or name in EXTRA_CHAPTERS, area_of(name), name, title)
        for i, (name, title) in enumerate(zip(names, shown))
        if name in used and not name.startswith(AFTER_GOAL)
    ]
    lines = [
        '"""Story checkpoints. Generated by tools/catalog_checkpoints.py."""',
        "",
        '# (chapter of the "Load Chapter" menu?, area, step name in the game, shown title), '
        "in the order of the game's table",
        "CHECKPOINTS: list[tuple[bool, str, str, str]] = [",
    ]
    lines += [f"    {row!r}," for row in rows]
    lines.append("]")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(rows)} steps, {sum(r[0] for r in rows)} of them chapters -> {OUT}")


if __name__ == "__main__":
    main()
