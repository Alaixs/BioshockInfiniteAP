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
