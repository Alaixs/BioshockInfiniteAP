"""Runs the world's tests (apworld/bioshock_infinite/test) with Archipelago's test runner.

    python apworld/run_tests.py <Archipelago source checkout> [--general] [pytest arguments]

--general also runs Archipelago's own tests of every world (test/general, a few minutes; the CI
runs them with "-n auto", which needs pytest-xdist from ci-requirements.txt).

The checkout's requirements and pytest must be installed in the Python running this script:

    git clone --branch 0.6.7 https://github.com/ArchipelagoMW/Archipelago
    cd Archipelago
    python ModuleUpdate.py --yes
    python -m pip install -r ci-requirements.txt

The world is copied into the checkout's worlds folder for the run. A copy installed in
custom_worlds would register the game twice: it is moved aside meanwhile.
"""

import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).parent
WORLD = HERE / "bioshock_infinite"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    archipelago = pathlib.Path(sys.argv[1]).resolve()
    if not (archipelago / "Generate.py").exists():
        print(f"{archipelago} is not an Archipelago source checkout (no Generate.py).")
        return 2
    target = archipelago / "worlds" / WORLD.name
    installed = archipelago / "custom_worlds" / f"{WORLD.name}.apworld"
    aside = installed.with_name(installed.name + ".aside")
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(WORLD, target, ignore=shutil.ignore_patterns("__pycache__"))
    if installed.exists():
        installed.replace(aside)
    arguments = sys.argv[2:]
    paths = [f"worlds/{WORLD.name}"]
    if "--general" in arguments:
        arguments.remove("--general")
        paths.append("test/general")
    try:
        command = [sys.executable, "-m", "pytest", *paths, *arguments]
        return subprocess.run(command, cwd=archipelago).returncode
    finally:
        if os.environ.get("BSAP_UPDATE_IDS"):  # test_ids.py recorded new ids in the copy
            shutil.copy2(target / "test" / "ids.json", WORLD / "test" / "ids.json")
        shutil.rmtree(target, ignore_errors=True)
        if aside.exists():
            aside.replace(installed)


if __name__ == "__main__":
    sys.exit(main())
