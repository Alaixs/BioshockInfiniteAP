"""Where the installed game is, for the tools that read its packages.

Found in the Steam libraries, as build.ps1 does. Set BIOSHOCK_INFINITE_DIR to the game folder to
use another one.
"""

import os
import pathlib
import re

DEFAULT_DIR = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\BioShock Infinite")


def steam_libraries() -> list[pathlib.Path]:
    """Steam's own folder, then the other libraries listed in its libraryfolders.vdf."""
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam = pathlib.Path(winreg.QueryValueEx(key, "SteamPath")[0])
    except (ImportError, OSError):
        return []
    libraries = [steam]
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    if vdf.exists():
        paths = re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace"))
        libraries += [pathlib.Path(path.replace("\\\\", "\\")) for path in paths]
    return libraries


def find_game() -> pathlib.Path:
    if "BIOSHOCK_INFINITE_DIR" in os.environ:
        return pathlib.Path(os.environ["BIOSHOCK_INFINITE_DIR"])
    for library in steam_libraries():
        game = library / "steamapps" / "common" / "BioShock Infinite"
        if (game / "Binaries" / "Win32" / "BioShockInfinite.exe").exists():
            return game
    return DEFAULT_DIR


GAME_DIR = find_game()
XGAME = GAME_DIR / "XGame"
# The cooked packages folder depends on the install language (CookedPCConsole, CookedPCConsole_FR...).
COOKED = next(iter(sorted(XGAME.glob("CookedPCConsole*"))), XGAME / "CookedPCConsole")

# Level packages: "S_<area>_..." (any case), plus Comstock House's sublevels, cooked without the
# prefix (DCOM_Torture, DCOM_WardenSecurity...): until 0.2.2 the tools only read S_*, and missed
# their Voxophones and Gear. Without the S_Lut_* ones: the Sea of Doors and the ending, loaded by
# S_Lut_P once the Siphon is destroyed (the goal), so nothing there can be a location.
LEVEL_GLOBS = ("S_*.xxx", "DCOM_*.xxx")
AFTER_GOAL_LEVELS = ("s_lut_",)


def level_packages() -> list[pathlib.Path]:
    found = {f for pattern in LEVEL_GLOBS for f in COOKED.glob(pattern)}
    return sorted(f for f in found if not f.stem.lower().startswith(AFTER_GOAL_LEVELS))


def level_prefix(package: str) -> str:
    """Story part of a level package's name: S_TWN_Float_B_Loot -> twn, DCOM_Torture -> dcom."""
    parts = package.lower().split("_")
    return parts[1] if parts[0] == "s" else parts[0]
