"""Packages apworld/bioshock_infinite into apworld/build/bioshock_infinite.apworld.

    python apworld/build.py [--install [CUSTOM_WORLDS_FOLDER]]

--install also copies it into Archipelago's custom_worlds folder (default:
C:\\ProgramData\\Archipelago\\custom_worlds). Same as the Launcher's "Build APWorlds" component,
which only exists in source installs.
"""

import json
import pathlib
import shutil
import sys
import zipfile

HERE = pathlib.Path(__file__).parent
WORLD = HERE / "bioshock_infinite"
OUT = HERE / "build" / "bioshock_infinite.apworld"
CUSTOM_WORLDS = pathlib.Path(r"C:\ProgramData\Archipelago\custom_worlds")

# APWorld container fields (worlds/Files.py: container_version, compatible_version).
CONTAINER = {"version": 7, "compatible_version": 5}


def build() -> pathlib.Path:
    OUT.parent.mkdir(exist_ok=True)
    manifest = json.loads((WORLD / "archipelago.json").read_text(encoding="utf-8"))
    manifest.update(CONTAINER)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(WORLD.rglob("*")):
            relative = path.relative_to(WORLD)
            if path.is_dir() or "__pycache__" in relative.parts or relative.parts[0] in ("test", "archipelago.json"):
                continue  # the tests only run from a source checkout (run_tests.py)
            zf.write(path, f"{WORLD.name}/{relative.as_posix()}")
        zf.writestr(f"{WORLD.name}/archipelago.json", json.dumps(manifest, indent=4))
    return OUT


if __name__ == "__main__":
    out = build()
    print(f"{out} ({out.stat().st_size} bytes)")
    if "--install" in sys.argv:
        args = sys.argv[sys.argv.index("--install") + 1 :]
        target = pathlib.Path(args[0]) if args else CUSTOM_WORLDS
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(out, target / out.name)
        print(f"installed in {target}")
